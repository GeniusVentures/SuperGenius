#include "crdt/crdt_datastore.hpp"
#include <storage/rocksdb/rocksdb.hpp>
#include <iostream>
#include "crdt/proto/bcast.pb.h"
#include <google/protobuf/unknown_field_set.h>
#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>
#include <thread>
#include <utility>

namespace sgns::crdt
{

#define LOG_INFO( msg )                                                                                                \
    if ( this->logger_ != nullptr )                                                                                    \
    {                                                                                                                  \
        std::ostringstream msgStream;                                                                                  \
        msgStream << msg << std::ends;                                                                                 \
        this->logger_->info( msgStream.str() );                                                                        \
    }
#define LOG_ERROR( msg )                                                                                               \
    if ( this->logger_ != nullptr )                                                                                    \
    {                                                                                                                  \
        std::ostringstream msgStream;                                                                                  \
        msgStream << msg << std::ends;                                                                                 \
        this->logger_->error( msgStream.str() );                                                                       \
    }
#define LOG_DEBUG( msg )                                                                                               \
    if ( this->logger_ != nullptr )                                                                                    \
    {                                                                                                                  \
        std::ostringstream msgStream;                                                                                  \
        msgStream << msg << std::ends;                                                                                 \
        this->logger_->debug( msgStream.str() );                                                                       \
    }

    using CRDTBroadcast = pb::CRDTBroadcast;

    CrdtDatastore::CrdtDatastore( std::shared_ptr<DataStore>          aDatastore,
                                  const HierarchicalKey              &aKey,
                                  std::shared_ptr<DAGSyncer>          aDagSyncer,
                                  std::shared_ptr<Broadcaster>        aBroadcaster,
                                  const std::shared_ptr<CrdtOptions> &aOptions ) :
        dataStore_( std::move( aDatastore ) ),
        namespaceKey_( aKey ),
        broadcaster_( std::move( aBroadcaster ) ),
        dagSyncer_( std::move( aDagSyncer ) )
    {
        // <namespace>/s
        auto fullSetNs = aKey.ChildString( std::string( setsNamespace_ ) );
        // <namespace>/h
        auto fullHeadsNs = aKey.ChildString( std::string( headsNamespace_ ) );

        int numberOfDagWorkers = 5;
        if ( aOptions != nullptr && !aOptions->Verify().has_failure() &&
             aOptions->Verify().value() == CrdtOptions::VerifyErrorCode::Success )
        {
            this->options_        = aOptions;
            this->putHookFunc_    = options_->putHookFunc;
            this->deleteHookFunc_ = options_->deleteHookFunc;
            this->logger_         = options_->logger;
            numberOfDagWorkers    = options_->numWorkers;
        }

        this->set_   = std::make_shared<CrdtSet>( dataStore_, fullSetNs, this->putHookFunc_, this->deleteHookFunc_ );
        this->heads_ = std::make_shared<CrdtHeads>( dataStore_, fullHeadsNs );

        int      numberOfHeads = 0;
        uint64_t maxHeight     = 0;
        if ( this->heads_ != nullptr )
        {
            std::vector<CID> heads;
            auto             getListResult = this->heads_->GetList( heads, maxHeight );
            if ( !getListResult.has_failure() )
            {
                numberOfHeads = heads.size();
            }
        }

        LOG_INFO( "crdt Datastore created. Number of heads: " << numberOfHeads
                                                              << " Current max-height: " << maxHeight );

        // Starting HandleNext worker thread
        this->handleNextFuture_ = std::async( std::bind( &CrdtDatastore::HandleNext, this ) );

        // Starting Rebroadcast worker thread
        this->rebroadcastFuture_ = std::async( std::bind( &CrdtDatastore::Rebroadcast, this ) );

        // Starting DAG worker threads
        for ( int i = 0; i < numberOfDagWorkers; ++i )
        {
            auto dagWorker              = std::make_shared<DagWorker>();
            dagWorker->dagWorkerFuture_ = std::async( std::bind( &CrdtDatastore::SendJobWorker, this, dagWorker ) );
            this->dagWorkers_.push_back( dagWorker );
        }
    }

    CrdtDatastore::~CrdtDatastore()
    {
        this->Close();
    }

    //static
    std::shared_ptr<CrdtDatastore::Delta> CrdtDatastore::DeltaMerge( const std::shared_ptr<Delta> &aDelta1,
                                                                     const std::shared_ptr<Delta> &aDelta2 )
    {
        auto result = std::make_shared<CrdtDatastore::Delta>();
        if ( aDelta1 != nullptr )
        {
            for ( const auto &elem : aDelta1->elements() )
            {
                auto newElement = result->add_elements();
                newElement->CopyFrom( elem );
            }
            for ( const auto &tomb : aDelta1->tombstones() )
            {
                auto newTomb = result->add_tombstones();
                newTomb->CopyFrom( tomb );
            }
            result->set_priority( aDelta1->priority() );
        }
        if ( aDelta2 != nullptr )
        {
            for ( const auto &elem : aDelta2->elements() )
            {
                auto newElement = result->add_elements();
                newElement->CopyFrom( elem );
            }
            for ( const auto &tomb : aDelta2->tombstones() )
            {
                auto newTomb = result->add_tombstones();
                newTomb->CopyFrom( tomb );
            }
            auto d2Priority = aDelta2->priority();
            if ( d2Priority > result->priority() )
            {
                result->set_priority( d2Priority );
            }
        }
        return result;
    }

    void CrdtDatastore::Close()
    {
        if ( handleNextThreadRunning_ )
        {
            this->handleNextThreadRunning_ = false;
            this->handleNextFuture_.wait();
        }

        if ( this->rebroadcastThreadRunning_ )
        {
            this->rebroadcastThreadRunning_ = false;
            this->rebroadcastFuture_.wait();
        }

        for ( const auto &dagWorker : this->dagWorkers_ )
        {
            dagWorker->dagWorkerThreadRunning_ = false;
            dagWorker->dagWorkerFuture_.wait();
        }
    }

    void CrdtDatastore::HandleNext()
    {
        if ( broadcaster_ == nullptr )
        {
            // offline
            return;
        }

        handleNextThreadRunning_ = true;

        //LogDebug( "HandleNext thread started" );
        while ( handleNextThreadRunning_ )
        {
            std::this_thread::sleep_for( threadSleepTimeInMilliseconds_ );

            auto broadcasterNextResult = broadcaster_->Next();
            if ( broadcasterNextResult.has_failure() )
            {
                if ( broadcasterNextResult.error().value() !=
                     static_cast<int>( Broadcaster::ErrorCode::ErrNoMoreBroadcast ) )
                {
                    //LogDebug( "Failed to get next broadcaster (error code " +
                    //          std::to_string( broadcasterNextResult.error().value() ) + ")" );
                }
                continue;
            }

            auto decodeResult = DecodeBroadcast( broadcasterNextResult.value() );
            if ( decodeResult.has_failure() )
            {
                LogError( "Broadcaster: Unable to decode broadcast (error code " +
                          std::to_string( broadcasterNextResult.error().value() ) + ")" );
                continue;
            }

            // For each head, we process it.
            for ( const auto &bCastHeadCID : decodeResult.value() )
            {
                auto handleBlockResult = HandleBlock( bCastHeadCID );
                if ( handleBlockResult.has_failure() )
                {
                    LogError( "Broadcaster: Unable to handle block (error code " +
                              std::to_string( handleBlockResult.error().value() ) + ")" );
                    continue;
                }
                std::unique_lock lock( seenHeadsMutex_ );
                seenHeads_.push_back( bCastHeadCID );
            }

            // We should store trusted-peer signatures associated to
            // each head in a timecache. When we broadcast, attach the
            // signatures (along with our own) to the broadcast.
            // Other peers can use the signatures to verify that the
            // received CIDs have been issued by a trusted peer.
        }

        LogDebug( "HandleNext thread finished" );
    }

    void CrdtDatastore::Rebroadcast()
    {
        //LogDebug( "Rebroadcast thread started" );
        auto rebroadcastIntervalMilliseconds = std::chrono::milliseconds( threadSleepTimeInMilliseconds_ );
        if ( options_ != nullptr )
        {
            rebroadcastIntervalMilliseconds = std::chrono::milliseconds( options_->rebroadcastIntervalMilliseconds );
        }

        std::chrono::milliseconds elapsedTimeMilliseconds = std::chrono::milliseconds( 0 );
        rebroadcastThreadRunning_                         = true;
        while ( rebroadcastThreadRunning_ )
        {
            if ( elapsedTimeMilliseconds >= rebroadcastIntervalMilliseconds )
            {
                RebroadcastHeads();
                elapsedTimeMilliseconds = std::chrono::milliseconds( 0 );
            }
            std::this_thread::sleep_for( threadSleepTimeInMilliseconds_ );
            elapsedTimeMilliseconds += threadSleepTimeInMilliseconds_;
        }
        LogDebug( "Rebroadcast thread finished" );
    }

    void CrdtDatastore::SendJobWorker( std::shared_ptr<DagWorker> dagWorker )
    {
        if ( dagWorker == nullptr )
        {
            return;
        }

        LogDebug( "SendJobWorker thread started" );
        DagJob dagJob;
        dagWorker->dagWorkerThreadRunning_ = true;
        while ( dagWorker->dagWorkerThreadRunning_ )
        {
            std::this_thread::sleep_for( threadSleepTimeInMilliseconds_ );

            {
                std::unique_lock lock( dagWorkerMutex_ );
                if ( dagWorkerJobList.empty() )
                {
                    continue;
                }
                dagJob = dagWorkerJobList.front();
                dagWorkerJobList.pop();
            }
            LogInfo( "SendJobWorker CID=" + dagJob.rootCid_.toString().value() +
                     " priority=" + std::to_string( dagJob.rootPriority_ ) );

            auto childrenResult = ProcessNode( dagJob.rootCid_, dagJob.rootPriority_, dagJob.delta_, dagJob.node_ );
            if ( childrenResult.has_failure() )
            {
                LogError( "SendNewJobs: failed to process node:" + dagJob.rootCid_.toString().value() );
            }
            else
            {
                SendNewJobs( dagJob.rootCid_, dagJob.rootPriority_, childrenResult.value() );
            }
        }
        LogDebug( "SendJobWorker thread finished" );
    }

    void CrdtDatastore::LogError( std::string message )
    {
        LOG_ERROR( message );
    }

    void CrdtDatastore::LogInfo( std::string message )
    {
        LOG_INFO( message );
    }

    void CrdtDatastore::LogDebug( std::string message )
    {
        LOG_DEBUG( message );
    }

    outcome::result<std::vector<CID>> CrdtDatastore::DecodeBroadcast( const Buffer &buff )
    {
        // Make a list of heads we received
        CRDTBroadcast bcastData;
        auto          string_data = std::string( buff.toString() );

        if ( !string_data.size() )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        if ( !bcastData.MergeFromString( string_data ) )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        if ( !bcastData.IsInitialized() )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // Compatibility: before we were publishing CIDs directly
        auto msgReflect = bcastData.GetReflection();

        if ( msgReflect == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        if ( !msgReflect->GetUnknownFields( bcastData ).empty() )
        {
            // Backwards compatibility
            //c, err := cid.Cast(msgReflect.GetUnknown())
            //sgns::common::getCidOf(msgReflect->MutableUnknownFields(&bcastData)->);
            return outcome::failure( boost::system::error_code{} );
        }

        std::vector<CID> bCastHeads;
        for ( const auto &head : bcastData.heads() )
        {
            auto cidResult = CID::fromString( head.cid() );
            if ( cidResult.has_failure() )
            {
                LOG_ERROR( "DecodeBroadcast: Failed to convert CID from string (error code "
                           << cidResult.error().value() << ")" )
            }
            bCastHeads.push_back( cidResult.value() );
        }
        return bCastHeads;
    }

    outcome::result<CrdtDatastore::Buffer> CrdtDatastore::EncodeBroadcast( const std::vector<CID> &heads )
    {
        CRDTBroadcast bcastData;
        for ( const auto &head : heads )
        {
            auto encodedHead   = bcastData.add_heads();
            auto strHeadResult = head.toString();
            if ( !strHeadResult.has_failure() )
            {
                encodedHead->set_cid( strHeadResult.value() );
            }
        }

        Buffer outputBuffer;
        outputBuffer.put( bcastData.SerializeAsString() );
        return outputBuffer;
    }

    void CrdtDatastore::RebroadcastHeads()
    {
        uint64_t         maxHeight = 0;
        std::vector<CID> heads;
        if ( this->heads_ != nullptr )
        {
            auto getListResult = this->heads_->GetList( heads, maxHeight );
            if ( getListResult.has_failure() )
            {
                LOG_ERROR( "RebroadcastHeads: Failed to get list of heads (error code " << getListResult.error()
                                                                                        << ")" );
                return;
            }
        }

        std::vector<CID> headsToBroadcast;
        {
            std::shared_lock lock( this->seenHeadsMutex_ );
            for ( const auto &head : heads )
            {
                if ( std::find( seenHeads_.begin(), seenHeads_.end(), head ) == seenHeads_.end() )
                {
                    headsToBroadcast.push_back( head );
                }
            }
        }

        auto broadcastResult = this->Broadcast( headsToBroadcast );
        if ( broadcastResult.has_failure() )
        {
            LOG_ERROR( "Broadcast failed" );
        }

        // Reset the map
        std::unique_lock lock( this->seenHeadsMutex_ );
        this->seenHeads_.clear();
    }

    outcome::result<void> CrdtDatastore::HandleBlock( const CID &aCid )
    {
        // Ignore already known blocks.
        // This includes the case when the block is a current
        // head.

        if ( dagSyncer_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        //auto dagSyncerResult = dagSyncer_->HasBlock( aCid );
        //if ( dagSyncerResult.has_failure() )
        //{
        //    LOG_ERROR( "HandleBlock: error checking for known block" );
        //    return outcome::failure( dagSyncerResult.error() );
        //}

        //if ( dagSyncerResult.value() )
        //{
        //    // cid is known. Skip walking tree
        //    return outcome::success();
        //}

        if ( !ContainsCID( aCid ) )
        {
            std::vector<CID> children;
            children.push_back( aCid );
            SendNewJobs( aCid, 0, children );
        }

        return outcome::success();
    }

    void CrdtDatastore::SendNewJobs( const CID &aRootCID, uint64_t aRootPriority, const std::vector<CID> &aChildren )
    {
        // sendNewJobs calls getDeltas with the given
        // children and sends each response to the workers.

        if ( this->dagSyncer_ == nullptr || aChildren.empty() )
        {
            return;
        }

        // @todo figure out how should dagSyncerTimeoutSec be used
        std::chrono::seconds dagSyncerTimeoutSec = std::chrono::seconds( 5 * 60 ); // 5 mins by default
        if ( this->options_ != nullptr )
        {
            dagSyncerTimeoutSec = std::chrono::seconds( options_->dagSyncerTimeoutSec );
        }

        uint64_t rootPriority = aRootPriority;
        if ( rootPriority == 0 )
        {
            std::unique_lock lock( dagWorkerMutex_ );
            auto             getNodeResult = dagSyncer_->getNode( aChildren[0] );

            if ( getNodeResult.has_failure() )
            {
                return;
            }
            auto node       = getNodeResult.value();
            auto nodeBuffer = node->content();

            auto delta = std::make_shared<Delta>();

            if ( !delta->ParseFromArray( nodeBuffer.data(), nodeBuffer.size() ) )
            {
                return;
            }

            if ( rootPriority == 0 )
            {
                rootPriority = delta->priority();
            }
        }

        for ( const auto &cid : aChildren )
        {
            //Fetch only root node with all children, but without children of their children
            constexpr int maxRetries = 3;
            int           retryCount = 0;
            bool          success    = false;

            std::shared_ptr<ipfs_lite::ipfs::merkledag::Leaf> leaf;
            common::Buffer                                    nodeBuffer;
            LOG_DEBUG( "SendNewJobs: TRYING TO FETCH NODE : " << cid.toString().value() );

            // Retry loop for fetching the graph
            while ( retryCount < maxRetries )
            {
                std::unique_lock lock( dagWorkerMutex_ );
                auto             graphResult = dagSyncer_->fetchGraphOnDepth( cid, 1 );
                lock.unlock();

                if ( graphResult.has_failure() )
                {
                    LOG_ERROR( "SendNewJobs: error fetching graph for CID:" << cid.toString().value() << " Retry "
                                                                            << retryCount + 1 << "/" << maxRetries );
                    retryCount++;
                    continue;
                }

                // Fetch successful
                leaf       = graphResult.value();
                nodeBuffer = leaf->content();
                success    = true;
                break;
            }
            if ( !success )
            {
                LOG_ERROR( "SendNewJobs: failed to fetch graph for CID:" << cid.toString().value() << " after "
                                                                         << maxRetries << " retries." );
                continue;
            }

            // @todo Check if it is OK that the node has only content and doesn't have links
            auto nodeResult = ipfs_lite::ipld::IPLDNodeImpl::createFromRawBytes( nodeBuffer );
            if ( nodeResult.has_failure() )
            {
                LOG_ERROR( "SendNewJobs: Can't create IPLDNodeImpl with leaf content " << nodeBuffer.size() );
                continue;
            }
            auto node = nodeResult.value();

            auto delta = std::make_shared<Delta>();
            if ( !delta->ParseFromArray( nodeBuffer.data(), nodeBuffer.size() ) )
            {
                LOG_ERROR( "SendNewJobs: Can't parse data with size " << nodeBuffer.size() );
                continue;
            }

            DagJob dagJob;
            dagJob.rootCid_      = aRootCID;
            dagJob.rootPriority_ = rootPriority;
            dagJob.delta_        = delta;
            dagJob.node_         = node;
            LogInfo( "SendNewJobs PUSHING CID=" + dagJob.rootCid_.toString().value() +
                     " priority=" + std::to_string( dagJob.rootPriority_ ) );
            {
                std::unique_lock lock( dagWorkerMutex_ );

                dagWorkerJobList.push( dagJob );
            }
        }
    }

    outcome::result<CrdtDatastore::Buffer> CrdtDatastore::GetKey( const HierarchicalKey &aKey )
    {
        if ( this->set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return this->set_->GetElement( aKey.GetKey() );
    }

    outcome::result<std::string> CrdtDatastore::GetKeysPrefix()
    {
        if ( this->set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return this->set_->KeysKey( "" ).GetKey() + "/";
    }

    outcome::result<std::string> CrdtDatastore::GetValueSuffix()
    {
        if ( this->set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return "/" + this->set_->GetValueSuffix();
    }

    outcome::result<CrdtDatastore::QueryResult> CrdtDatastore::QueryKeyValues( const std::string &aPrefix )
    {
        if ( this->set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return this->set_->QueryElements( aPrefix, CrdtSet::QuerySuffix::QUERY_VALUESUFFIX );
    }

    outcome::result<bool> CrdtDatastore::HasKey( const HierarchicalKey &aKey )
    {
        if ( this->set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return this->set_->IsValueInSet( aKey.GetKey() );
    }

    outcome::result<void> CrdtDatastore::PutKey( const HierarchicalKey &aKey, const Buffer &aValue )
    {
        if ( this->set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto deltaResult = CreateDeltaToAdd( aKey.GetKey(), std::string( aValue.toString() ) );
        if ( deltaResult.has_failure() )
        {
            return outcome::failure( deltaResult.error() );
        }
        return this->Publish( deltaResult.value() );
    }

    outcome::result<void> CrdtDatastore::DeleteKey( const HierarchicalKey &aKey )
    {
        if ( this->set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto deltaResult = CreateDeltaToRemove( aKey.GetKey() );
        if ( deltaResult.has_failure() )
        {
            return outcome::failure( deltaResult.error() );
        }

        if ( deltaResult.value()->tombstones().empty() )
        {
            return outcome::success();
        }

        return this->Publish( deltaResult.value() );
    }

    outcome::result<void> CrdtDatastore::Publish( const std::shared_ptr<Delta> &aDelta )
    {
        auto addResult = this->AddDAGNode( aDelta );
        if ( addResult.has_failure() )
        {
            return outcome::failure( addResult.error() );
        }
        std::vector<CID> cids;
        cids.push_back( addResult.value() );
        return this->Broadcast( cids );
    }

    outcome::result<void> CrdtDatastore::Broadcast( const std::vector<CID> &cids )
    {
        if ( this->broadcaster_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        if ( !cids.empty() )
        {
            auto encodedBufferResult = this->EncodeBroadcast( cids );
            if ( encodedBufferResult.has_failure() )
            {
                return outcome::failure( encodedBufferResult.error() );
            }

            //LOG_DEBUG( "Sending CIDs: " );
            //for ( auto id : cids )
            //{
            //    LOG_DEBUG( id.toString().value() );
            //}

            auto bcastResult = this->broadcaster_->Broadcast( encodedBufferResult.value() );
            if ( bcastResult.has_failure() )
            {
                return outcome::failure( bcastResult.error() );
            }
        }
        return outcome::success();
    }

    outcome::result<std::shared_ptr<CrdtDatastore::IPLDNode>> CrdtDatastore::PutBlock(
        const std::vector<CID>       &aHeads,
        uint64_t                      aHeight,
        const std::shared_ptr<Delta> &aDelta )
    {
        if ( aDelta == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        aDelta->set_priority( aHeight );

        auto node = ipfs_lite::ipld::IPLDNodeImpl::createFromString( aDelta->SerializeAsString() );
        if ( node == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        for ( const auto &head : aHeads )
        {
            auto cidByte = head.toBytes();
            if ( cidByte.has_failure() )
            {
                continue;
            }
            ipfs_lite::ipld::IPLDLinkImpl link = ipfs_lite::ipld::IPLDLinkImpl( head, "", cidByte.value().size() );
            node->addLink( link );
        }

        if ( this->dagSyncer_ != nullptr )
        {
            // @todo Check if dagSyncer should add the node hash to DHT
            auto dagSyncerResult = this->dagSyncer_->addNode( node );
            if ( dagSyncerResult.has_failure() )
            {
                LOG_ERROR( "DAGSyncer: error writing new block " << node->getCID().toString().value() )
                return outcome::failure( dagSyncerResult.error() );
            }
        }

        return node;
    }

    outcome::result<std::vector<CID>> CrdtDatastore::ProcessNode( const CID                       &aRoot,
                                                                  uint64_t                         aRootPrio,
                                                                  const std::shared_ptr<Delta>    &aDelta,
                                                                  const std::shared_ptr<IPLDNode> &aNode )
    {
        if ( this->set_ == nullptr || this->heads_ == nullptr || this->dagSyncer_ == nullptr || aDelta == nullptr ||
             aNode == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // merge the delta
        auto current      = aNode->getCID();
        auto strCidResult = current.toString();
        if ( strCidResult.has_failure() )
        {
            return outcome::failure( strCidResult.error() );
        }
        HierarchicalKey hKey( strCidResult.value() );

        {
            std::unique_lock lock( dagWorkerMutex_ );
            auto             mergeResult = this->set_->Merge( aDelta, hKey.GetKey() );
            if ( mergeResult.has_failure() )
            {
                LOG_ERROR( "ProcessNode: error merging delta from " << hKey.GetKey() );
                return outcome::failure( mergeResult.error() );
            }
        }

        auto priority = aDelta->priority();
        if ( priority % 10 == 0 )
        {
            LOG_INFO( "ProcessNode: merged delta from " << strCidResult.value() << " (priority: " << priority << ")" );
        }

        std::vector<CID> children;
        auto             links = aNode->getLinks();

        if ( links.empty() )
        {
            // we reached the bottom, we are a leaf.
            auto addHeadResult = this->heads_->Add( aRoot, aRootPrio );
            if ( addHeadResult.has_failure() )
            {
                LOG_ERROR( "ProcessNode: error adding head " << aRoot.toString().value() );
                return outcome::failure( addHeadResult.error() );
            }
        }
        else
        {
            // walkToChildren
            for ( const auto &link : links )
            {
                auto child        = link.get().getCID();
                auto isHeadResult = this->heads_->IsHead( child );

                if ( isHeadResult )
                {
                    // reached one of the current heads.Replace it with
                    // the tip of this branch
                    auto replaceResult = this->heads_->Replace( child, aRoot, aRootPrio );
                    if ( replaceResult.has_failure() )
                    {
                        LOG_ERROR( "ProcessNode: error replacing head " << child.toString().value() << " -> "
                                                                        << aRoot.toString().value() );
                        return outcome::failure( replaceResult.error() );
                    }

                    continue;
                }

                std::unique_lock lock( dagWorkerMutex_ );
                auto             knowBlockResult = this->dagSyncer_->HasBlock( child );
                if ( knowBlockResult.has_failure() )
                {
                    LOG_ERROR( "ProcessNode: error checking for known block " << child.toString().value() );
                    return outcome::failure( knowBlockResult.error() );
                }

                if ( knowBlockResult.value() )
                {
                    // we reached a non-head node in the known tree.
                    // This means our root block is a new head.
                    auto addHeadResult = this->heads_->Add( aRoot, aRootPrio );
                    if ( addHeadResult.has_failure() )
                    {
                        // Don't let this failure prevent us from processing the other links.
                        LOG_ERROR( "ProcessNode: error adding head " << aRoot.toString().value() );
                    }
                    continue;
                }

                children.push_back( child );
            }
        }

        AddProcessedCID( aRoot );
        return children;
    }

    outcome::result<CID> CrdtDatastore::AddDAGNode( const std::shared_ptr<Delta> &aDelta )
    {
        if ( this->heads_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        uint64_t         height = 0;
        std::vector<CID> heads;
        auto             getListResult = this->heads_->GetList( heads, height );
        if ( getListResult.has_failure() )
        {
            return outcome::failure( getListResult.error() );
        }

        height = height + 1; // This implies our minimum height is 1
        aDelta->set_priority( height );

        auto putBlockResult = this->PutBlock( heads, height, aDelta );
        if ( putBlockResult.has_failure() )
        {
            return outcome::failure( putBlockResult.error() );
        }

        auto node = putBlockResult.value();

        // Process new block. This makes that every operation applied
        // to this store take effect (delta is merged) before
        // returning. Since our block references current heads, children
        // should be empty
        LOG_INFO( "AddDAGNode: Processing generated block " << node->getCID().toString().value() );

        auto processNodeResult = this->ProcessNode( node->getCID(), height, aDelta, node );
        if ( processNodeResult.has_failure() )
        {
            LOG_ERROR( "AddDAGNode: error processing new block" );
            return outcome::failure( processNodeResult.error() );
        }

        if ( !processNodeResult.value().empty() )
        {
            LOG_ERROR( "AddDAGNode: bug - created a block to unknown children" );
        }

        return node->getCID();
    }

    outcome::result<void> CrdtDatastore::PrintDAG()
    {
        if ( this->heads_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        uint64_t         height = 0;
        std::vector<CID> heads;
        auto             getListResult = this->heads_->GetList( heads, height );
        if ( getListResult.has_failure() )
        {
            return outcome::failure( getListResult.error() );
        }

        std::vector<CID> set;
        for ( const auto &head : heads )
        {
            auto printResult = this->PrintDAGRec( head, 0, set );
            if ( printResult.has_failure() )
            {
                return outcome::failure( printResult.error() );
            }
        }
        return outcome::success();
    }

    outcome::result<void> CrdtDatastore::PrintDAGRec( const CID &aCID, uint64_t aDepth, std::vector<CID> &aSet )
    {
        std::ostringstream line;
        for ( uint64_t i = 0; i < aDepth; ++i )
        {
            line << " ";
        }

        // add a Cid to the set only if it is
        // not in it already.
        if ( std::find( aSet.begin(), aSet.end(), aCID ) != aSet.end() )
        {
            line << "...";
            std::cout << line.str() << std::endl;
            return outcome::success();
        }
        aSet.push_back( aCID );

        if ( this->dagSyncer_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto getNodeResult = this->dagSyncer_->getNode( aCID );

        if ( getNodeResult.has_failure() )
        {
            return outcome::failure( getNodeResult.error() );
        }
        auto node = getNodeResult.value();

        auto delta      = std::make_shared<Delta>();
        auto nodeBuffer = node->content();
        if ( !delta->ParseFromArray( nodeBuffer.data(), nodeBuffer.size() ) )
        {
            LOG_ERROR( "PrintDAGRec: failed to parse delta from node" );
            return outcome::failure( boost::system::error_code{} );
        }

        std::string strCID = node->getCID().toString().value();
        strCID             = strCID.substr( strCID.size() - 4 );
        line << " - " << delta->priority() << " | " << strCID << ": ";
        line << "Add: {";
        for ( const auto &elem : delta->elements() )
        {
            line << elem.key() << ":" << elem.value() << ",";
        }
        line << "}. Rmv: {";
        for ( const auto &tomb : delta->tombstones() )
        {
            line << tomb.key() << ",";
        }
        line << "}. Links: {";
        for ( const auto &link : node->getLinks() )
        {
            auto strCid = link.get().getCID().toString().value();
            strCid      = strCid.substr( strCid.size() - 4 );
            line << strCid << ",";
        }
        line << "}:";
        std::cout << line.str() << std::endl;

        for ( const auto &link : node->getLinks() )
        {
            this->PrintDAGRec( link.get().getCID(), aDepth + 1, aSet );
        }

        return outcome::success();
    }

    outcome::result<void> CrdtDatastore::Sync( const HierarchicalKey &aKey )
    {
        // This is a quick write up of the internals from the time when
        // I was thinking many underlying datastore entries are affected when
        // an add operation happens:
        //
        // When a key is added:
        // - a new delta is made
        // - Delta is marshalled and a DAG-node is created with the bytes,
        //   pointing to previous heads. DAG-node is added to DAGService.
        // - Heads are replaced with new CID.
        // - New CID is broadcasted to everyone
        // - The new CID is processed (up until now the delta had not
        //   taken effect). Implementation detail: it is processed before
        //   broadcast actually.
        // - processNode() starts processing that branch from that CID
        // - it calls set.Merge()
        // - that calls putElems() and putTombs()
        // - that may make a batch for all the elems which is later committed
        // - each element has a datastore entry /setNamespace/elemsNamespace/<key>/<block_id>
        // - each tomb has a datastore entry /setNamespace/tombsNamespace/<key>/<block_id>
        // - each value has a datastore entry /setNamespace/keysNamespace/<key>/valueSuffix
        // - each value has an additional priority entry /setNamespace/keysNamespace/<key>/prioritySuffix
        // - the last two are only written if the added entry has more priority than any the existing
        // - For a value to not be lost, those entries should be fully synced.
        // - In order to check if a value is in the set:
        //   - List all elements on /setNamespace/elemsNamespace/<key> (will return several block_ids)
        //   - If we find an element which is not tombstoned, then value is in the set
        // - In order to retrieve an element's value:
        //   - Check that it is in the set
        //   - Read the value entry from the /setNamespace/keysNamespace/<key>/valueSuffix path

        // Be safe and just sync everything in our namespace
        if ( aKey.GetKey() == "/" )
        {
            return this->Sync( this->namespaceKey_ );
        }

        // attempt to be intelligent and sync only all heads and the
        // set entries related to the given prefix.
        std::vector<HierarchicalKey> keysToSync;
        keysToSync.push_back( this->set_->ElemsPrefix( aKey.GetKey() ) );
        keysToSync.push_back( this->set_->TombsPrefix( aKey.GetKey() ) );
        keysToSync.push_back( this->set_->KeysKey( aKey.GetKey() ) ); // covers values and priorities
        keysToSync.push_back( this->heads_->GetNamespaceKey() );
        return this->SyncDatastore( keysToSync );
    }

    outcome::result<void> CrdtDatastore::SyncDatastore( const std::vector<HierarchicalKey> &aKeyList )
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // Call the crdt set sync. We don't need to
        // Because a store is shared with SET. Only
        return this->set_->DataStoreSync( aKeyList );
    }

    outcome::result<std::shared_ptr<CrdtDatastore::Delta>> CrdtDatastore::CreateDeltaToAdd( const std::string &key,
                                                                                            const std::string &value )
    {
        return set_->CreateDeltaToAdd( key, value );
    }

    outcome::result<std::shared_ptr<CrdtDatastore::Delta>> CrdtDatastore::CreateDeltaToRemove( const std::string &key )
    {
        return set_->CreateDeltaToRemove( key );
    }



    void CrdtDatastore::AddProcessedCID( const CID &cid )
    {
        std::lock_guard<std::mutex> lock( mutex_processed_cids );
        processed_cids.insert( cid );
    }

    // Check if a CID exists in the set
    bool CrdtDatastore::ContainsCID( const CID &cid )
    {
        std::lock_guard<std::mutex> lock( mutex_processed_cids );
        return processed_cids.find( cid ) != processed_cids.end();
    }

    // Delete multiple CIDs from the set
    bool CrdtDatastore::DeleteCIDS( const std::vector<CID> &cids )
    {
        std::lock_guard<std::mutex> lock( mutex_processed_cids );
        bool                        all_deleted = true;

        for ( const auto &cid : cids )
        {
            if ( processed_cids.erase( cid ) == 0 )
            {
                all_deleted = false; // If a CID is not found, mark as not fully successful
            }
        }

        return all_deleted;
    }
}
