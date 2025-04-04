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

    using CRDTBroadcast = pb::CRDTBroadcast;

    std::shared_ptr<CrdtDatastore> CrdtDatastore::New( std::shared_ptr<RocksDB>            aDatastore,
                                                       const HierarchicalKey              &aKey,
                                                       std::shared_ptr<DAGSyncer>          aDagSyncer,
                                                       std::shared_ptr<Broadcaster>        aBroadcaster,
                                                       const std::shared_ptr<CrdtOptions> &aOptions,
                                                       std::shared_ptr<ElementFilterCB>    elem_filter_cb,
                                                       std::shared_ptr<ElementFilterCB>    tomb_filter_cb )
    {
        auto crdtInstance = std::shared_ptr<CrdtDatastore>( new CrdtDatastore( std::move( aDatastore ),
                                                                               aKey,
                                                                               std::move( aDagSyncer ),
                                                                               std::move( aBroadcaster ),
                                                                               aOptions,
                                                                               std::move( elem_filter_cb ),
                                                                               std::move( tomb_filter_cb ) ) );

        crdtInstance->handleNextThreadRunning_ = true;
        // Starting HandleNext worker thread
        crdtInstance->handleNextFuture_ = std::async(
            [weakptr = std::weak_ptr<CrdtDatastore>( crdtInstance )]()
            {
                auto threadRunning = true;
                while ( threadRunning )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        self->HandleNextIteration();
                        if ( !self->handleNextThreadRunning_ )
                        {
                            self->logger_->debug( "HandleNext thread finished" );
                            threadRunning = false;
                        }
                    }
                    else
                    {
                        threadRunning = false;
                    }

                    if ( threadRunning )
                    {
                        std::this_thread::sleep_for( threadSleepTimeInMilliseconds_ );
                    }
                }
            } );

        crdtInstance->rebroadcastThreadRunning_ = true;
        // Starting Rebroadcast worker thread
        crdtInstance->rebroadcastFuture_ = std::async(
            [weakptr = std::weak_ptr<CrdtDatastore>( crdtInstance )]()
            {
                std::chrono::milliseconds elapsedTimeMilliseconds = std::chrono::milliseconds( 0 );
                auto                      threadRunning           = true;
                while ( threadRunning )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        self->RebroadcastIteration( elapsedTimeMilliseconds );
                        if ( !self->rebroadcastThreadRunning_ )
                        {
                            self->logger_->debug( "Rebroadcast thread finished" );
                            threadRunning = false;
                        }
                    }
                    else
                    {
                        threadRunning = false; // Object destroyed
                    }
                    if ( threadRunning )
                    {
                        // move this outside weakptr.lock() scope to release temporary strong reference to CrdtDatastore
                        std::this_thread::sleep_for( threadSleepTimeInMilliseconds_ );
                    }
                }
            } );

        crdtInstance->dagWorkerJobListThreadRunning_ = true;

        // Starting DAG worker threads
        for ( int i = 0; i < crdtInstance->numberOfDagWorkers; ++i )
        {
            auto dagWorker                     = std::make_shared<DagWorker>();
            dagWorker->dagWorkerThreadRunning_ = true;
            dagWorker->dagWorkerFuture_        = std::async(
                [weakptr = std::weak_ptr<CrdtDatastore>( crdtInstance ), dagWorker]()
                {
                    DagJob dagJob;
                    auto   dagThreadRunning = true;
                    while ( dagThreadRunning )
                    {
                        if ( auto self = weakptr.lock() )
                        {
                            self->SendJobWorkerIteration( dagWorker, dagJob );
                            if ( !dagWorker->dagWorkerThreadRunning_ )
                            {
                                self->logger_->debug( "SendJobWorker thread finished" );
                                dagThreadRunning = false;
                            }
                        }
                        else
                        {
                            dagThreadRunning = false; // Object destroyed
                        }

                        if ( dagThreadRunning )
                        {
                            // move this outside weakptr.lock() scope to release temporary strong reference to CrdtDatastore
                            // while the thread is sleeping
                            std::this_thread::sleep_for( threadSleepTimeInMilliseconds_ );
                        }
                    }
                } );
            crdtInstance->dagWorkers_.push_back( dagWorker );
        }

        return crdtInstance;
    }

    CrdtDatastore::CrdtDatastore( std::shared_ptr<RocksDB>            aDatastore,
                                  const HierarchicalKey              &aKey,
                                  std::shared_ptr<DAGSyncer>          aDagSyncer,
                                  std::shared_ptr<Broadcaster>        aBroadcaster,
                                  const std::shared_ptr<CrdtOptions> &aOptions,
                                  std::shared_ptr<ElementFilterCB>    elem_filter_cb,
                                  std::shared_ptr<ElementFilterCB>    tomb_filter_cb ) :
        dataStore_( std::move( aDatastore ) ),
        namespaceKey_( aKey ),
        broadcaster_( std::move( aBroadcaster ) ),
        dagSyncer_( std::move( aDagSyncer ) ),
        elem_filter_cb_( std::move( elem_filter_cb ) ),
        tomb_filter_cb_( std::move( tomb_filter_cb ) )
    {
        // <namespace>/s
        auto fullSetNs = aKey.ChildString( std::string( setsNamespace_ ) );
        // <namespace>/h
        auto fullHeadsNs = aKey.ChildString( std::string( headsNamespace_ ) );

        if ( aOptions != nullptr && !aOptions->Verify().has_failure() &&
             aOptions->Verify().value() == CrdtOptions::VerifyErrorCode::Success )
        {
            options_           = aOptions;
            putHookFunc_       = options_->putHookFunc;
            deleteHookFunc_    = options_->deleteHookFunc;
            logger_            = options_->logger;
            numberOfDagWorkers = options_->numWorkers;
        }

        set_   = std::make_shared<CrdtSet>( dataStore_, fullSetNs, putHookFunc_, deleteHookFunc_ );
        heads_ = std::make_shared<CrdtHeads>( dataStore_, fullHeadsNs );

        int      numberOfHeads = 0;
        uint64_t maxHeight     = 0;
        if ( heads_ != nullptr )
        {
            std::vector<CID> heads;
            auto             getListResult = heads_->GetList( heads, maxHeight );
            if ( !getListResult.has_failure() )
            {
                numberOfHeads = heads.size();
            }
        }

        logger_->info( "crdt Datastore created. Number of heads: {} Current max-height: {}", numberOfHeads, maxHeight );
    }

    CrdtDatastore::~CrdtDatastore()
    {
        Close();
    }

    bool CrdtDatastore::SetFilterCallback( std::shared_ptr<ElementFilterCB> elem_filter_cb )
    {
        bool ret = false;

        if ( !elem_filter_cb_ )
        {
            elem_filter_cb_ = std::move( elem_filter_cb );
            ret             = true;
        }
        return ret;
    }

    /**
     * @brief       Clear the CRDT Merge Filter Callback
     * @return      true if not filter is cleared, false otherwise
     * @warning     This method is used only for testing, so we don't care about race conditions here
     */
    bool CrdtDatastore::ClearFilterCallback()
    {
        bool ret = false;

        if ( elem_filter_cb_ )
        {
            elem_filter_cb_ = nullptr;
            ret             = true;
        }
        return ret;
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
            handleNextThreadRunning_ = false;
            handleNextFuture_.wait();
        }

        if ( rebroadcastThreadRunning_ )
        {
            rebroadcastThreadRunning_ = false;
            rebroadcastFuture_.wait();
        }

        if ( dagWorkerJobListThreadRunning_ )
        {
            for ( const auto &dagWorker : dagWorkers_ )
            {
                dagWorker->dagWorkerThreadRunning_ = false;
                dagWorker->dagWorkerFuture_.wait();
            }
            dagWorkers_.clear();
            dagWorkerJobListThreadRunning_ = false;
        }
    }

    void CrdtDatastore::HandleNextIteration()
    {
        if ( broadcaster_ == nullptr )
        {
            handleNextThreadRunning_ = false;
            return;
        }

        auto broadcasterNextResult = broadcaster_->Next();
        if ( broadcasterNextResult.has_failure() )
        {
            if ( broadcasterNextResult.error().value() !=
                 static_cast<int>( Broadcaster::ErrorCode::ErrNoMoreBroadcast ) )
            {
                // logger_->debug("Failed to get next broadcaster (error code " +
                //                std::to_string(broadcasterNextResult.error().value()) + ")");
            }
            return;
        }

        auto decodeResult = DecodeBroadcast( broadcasterNextResult.value() );
        if ( decodeResult.has_failure() )
        {
            logger_->error( "Broadcaster: Unable to decode broadcast (error code {})",
                            std::to_string( broadcasterNextResult.error().value() ) );

            return;
        }

        for ( const auto &bCastHeadCID : decodeResult.value() )
        {
            auto handleBlockResult = HandleBlock( bCastHeadCID );
            if ( handleBlockResult.has_failure() )
            {
                logger_->error( "Broadcaster: Unable to handle block (error code {})",
                                std::to_string( handleBlockResult.error().value() ) );
                continue;
            }
            std::unique_lock lock( seenHeadsMutex_ );
            seenHeads_.insert( bCastHeadCID );
        }
    }

    void CrdtDatastore::RebroadcastIteration( std::chrono::milliseconds &elapsedTimeMilliseconds )
    {
        auto rebroadcastIntervalMilliseconds = std::chrono::milliseconds( threadSleepTimeInMilliseconds_ );
        if ( options_ != nullptr )
        {
            rebroadcastIntervalMilliseconds = std::chrono::milliseconds( options_->rebroadcastIntervalMilliseconds );
        }

        if ( elapsedTimeMilliseconds >= rebroadcastIntervalMilliseconds )
        {
            RebroadcastHeads();
            elapsedTimeMilliseconds = std::chrono::milliseconds( 0 );
        }
        elapsedTimeMilliseconds += threadSleepTimeInMilliseconds_;
    }

    void CrdtDatastore::SendJobWorkerIteration( std::shared_ptr<DagWorker> dagWorker, DagJob &dagJob )
    {
        if ( dagWorker == nullptr )
        {
            return;
        }
        //std::cout << "Dag Worker QUant: " << dagWorkerJobList.size() << std::endl;
        {
            std::unique_lock lock( dagWorkerMutex_ );
            if ( dagWorkerJobList.empty() )
            {
                return;
            }
            dagJob = dagWorkerJobList.front();
            dagWorkerJobList.pop();
        }
        logger_->info( "SendJobWorker CID={} priority={}",
                       dagJob.rootCid_.toString().value(),
                       std::to_string( dagJob.rootPriority_ ) );

        auto childrenResult = ProcessNode( dagJob.rootCid_, dagJob.rootPriority_, dagJob.delta_, dagJob.node_ );
        if ( childrenResult.has_failure() )
        {
            logger_->error( "SendNewJobs: failed to process node:{}", dagJob.rootCid_.toString().value() );
        }
        else
        {
            SendNewJobs( dagJob.rootCid_, dagJob.rootPriority_, childrenResult.value() );
        }
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
                logger_->error( "DecodeBroadcast: Failed to convert CID from string (error code {})",
                                cidResult.error().value() );
            }
            else
            {
                bCastHeads.push_back( cidResult.value() );
            }
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
        if ( heads_ != nullptr )
        {
            auto getListResult = heads_->GetList( heads, maxHeight );
            if ( getListResult.has_failure() )
            {
                logger_->error( "RebroadcastHeads: Failed to get list of heads (error code {})",
                                getListResult.error() );
                return;
            }
        }

        std::vector<CID> headsToBroadcast;
        {
            std::shared_lock lock( seenHeadsMutex_ );
            for ( const auto &head : heads )
            {
                if ( std::find( seenHeads_.begin(), seenHeads_.end(), head ) == seenHeads_.end() )
                {
                    headsToBroadcast.push_back( head );
                }
            }
        }

        auto broadcastResult = Broadcast( headsToBroadcast );
        if ( broadcastResult.has_failure() )
        {
            logger_->error( "Broadcast failed" );
        }

        // Reset the map
        std::unique_lock lock( seenHeadsMutex_ );
        seenHeads_.clear();
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
        //    logger_->error( "HandleBlock: error checking for known block" );
        //}
        //else {
        //if (dagSyncerResult.value())
        //{
        //    AddProcessedCID(aCid);
        //}

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
        //if (dagSyncerResult.has_failure())
        //{
        //    return outcome::failure(dagSyncerResult.error());
        //}

        return outcome::success();
    }

    void CrdtDatastore::SendNewJobs( const CID &aRootCID, uint64_t aRootPriority, const std::vector<CID> &aChildren )
    {
        // sendNewJobs calls getDeltas with the given
        // children and sends each response to the workers.

        if ( dagSyncer_ == nullptr || aChildren.empty() )
        {
            return;
        }

        // @todo figure out how should dagSyncerTimeoutSec be used
        std::chrono::seconds dagSyncerTimeoutSec = std::chrono::seconds( 5 * 60 ); // 5 mins by default
        if ( options_ != nullptr )
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

            logger_->debug( "Getting Node: TRYING TO FETCH NODE {}", reinterpret_cast<uint64_t>( this ) );

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
            logger_->debug( "SendNewJobs: TRYING TO FETCH NODE : {} from {}",
                            cid.toString().value(),
                            reinterpret_cast<uint64_t>( this ) );

            // Single attempt to fetch the graph - getNode internally already has retry logic
            std::unique_lock lock( dagWorkerMutex_ );
            auto             graphResult = dagSyncer_->fetchGraphOnDepth( cid, 1 );
            lock.unlock();

            if ( graphResult.has_failure() )
            {
                logger_->error( "SendNewJobs: error fetching graph for CID:{} - {}",
                                cid.toString().value(),
                                graphResult.error().message() );
                continue;
            }

            auto leaf       = graphResult.value();
            auto nodeBuffer = leaf->content();

            // @todo Check if it is OK that the node has only content and doesn't have links
            auto nodeResult = ipfs_lite::ipld::IPLDNodeImpl::createFromRawBytes( nodeBuffer );
            if ( nodeResult.has_failure() )
            {
                logger_->error( "SendNewJobs: Can't create IPLDNodeImpl with leaf content {}", nodeBuffer.size() );
                continue;
            }
            auto node = nodeResult.value();

            auto delta = std::make_shared<Delta>();
            if ( !delta->ParseFromArray( nodeBuffer.data(), nodeBuffer.size() ) )
            {
                logger_->error( "SendNewJobs: Can't parse data with size {}", nodeBuffer.size() );
                continue;
            }

            DagJob dagJob;
            dagJob.rootCid_      = aRootCID;
            dagJob.rootPriority_ = rootPriority;
            dagJob.delta_        = delta;
            dagJob.node_         = node;
            logger_->debug( "SendNewJobs PUSHING CID={} priority={} ",
                            dagJob.rootCid_.toString().value(),
                            std::to_string( dagJob.rootPriority_ ) );
            {
                std::unique_lock lock( dagWorkerMutex_ );
                dagWorkerJobList.push( dagJob );
            }
        }
    }

    outcome::result<CrdtDatastore::Buffer> CrdtDatastore::GetKey( const HierarchicalKey &aKey )
    {
        if ( set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return set_->GetElement( aKey.GetKey() );
    }

    outcome::result<std::string> CrdtDatastore::GetKeysPrefix()
    {
        if ( set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return set_->KeysKey( "" ).GetKey() + "/";
    }

    outcome::result<std::string> CrdtDatastore::GetValueSuffix()
    {
        if ( set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return "/" + set_->GetValueSuffix();
    }

    outcome::result<CrdtDatastore::QueryResult> CrdtDatastore::QueryKeyValues( const std::string &aPrefix )
    {
        if ( set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return set_->QueryElements( aPrefix, CrdtSet::QuerySuffix::QUERY_VALUESUFFIX );
    }

    outcome::result<bool> CrdtDatastore::HasKey( const HierarchicalKey &aKey )
    {
        if ( set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return set_->IsValueInSet( aKey.GetKey() );
    }

    outcome::result<void> CrdtDatastore::PutKey( const HierarchicalKey &aKey, const Buffer &aValue )
    {
        if ( set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto deltaResult = CreateDeltaToAdd( aKey.GetKey(), std::string( aValue.toString() ) );
        if ( deltaResult.has_failure() )
        {
            return outcome::failure( deltaResult.error() );
        }
        return Publish( deltaResult.value() );
    }

    outcome::result<void> CrdtDatastore::DeleteKey( const HierarchicalKey &aKey )
    {
        if ( set_ == nullptr )
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

        return Publish( deltaResult.value() );
    }

    outcome::result<void> CrdtDatastore::Publish( const std::shared_ptr<Delta> &aDelta )
    {
        auto addResult = AddDAGNode( aDelta );
        if ( addResult.has_failure() )
        {
            return outcome::failure( addResult.error() );
        }
        std::vector<CID> cids;
        cids.push_back( addResult.value() );
        return Broadcast( cids );
    }

    outcome::result<void> CrdtDatastore::Broadcast( const std::vector<CID> &cids )
    {
        if ( broadcaster_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        if ( !cids.empty() )
        {
            auto encodedBufferResult = EncodeBroadcast( cids );
            if ( encodedBufferResult.has_failure() )
            {
                return outcome::failure( encodedBufferResult.error() );
            }

            //LOG_DEBUG( "Sending CIDs: " );
            //for ( auto id : cids )
            //{
            //    LOG_DEBUG( id.toString().value() );
            //}

            auto bcastResult = broadcaster_->Broadcast( encodedBufferResult.value() );
            if ( bcastResult.has_failure() )
            {
                return outcome::failure( bcastResult.error() );
            }
        }
        return outcome::success();
    }

    outcome::result<std::shared_ptr<CrdtDatastore::IPLDNode>> CrdtDatastore::PutBlock(
        const std::vector<CID>       &aHeads,
        const std::shared_ptr<Delta> &aDelta )
    {
        if ( aDelta == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

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

        if ( dagSyncer_ != nullptr )
        {
            // @todo Check if dagSyncer should add the node hash to DHT
            auto dagSyncerResult = dagSyncer_->addNode( node );
            if ( dagSyncerResult.has_failure() )
            {
                logger_->error( "DAGSyncer: error writing new block {}", node->getCID().toString().value() );
                return outcome::failure( dagSyncerResult.error() );
            }
        }

        return node;
    }

    outcome::result<std::vector<CID>> CrdtDatastore::ProcessNode( const CID                       &aRoot,
                                                                  uint64_t                         aRootPrio,
                                                                  std::shared_ptr<Delta>           aDelta,
                                                                  const std::shared_ptr<IPLDNode> &aNode )
    {
        if ( set_ == nullptr || heads_ == nullptr || dagSyncer_ == nullptr || aDelta == nullptr || aNode == nullptr )
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
        bool            filter_ret = true;

        FilterElementsOnDelta( aDelta );
        FilterTombstonesOnDelta( aDelta );

        {
            std::unique_lock lock( dagWorkerMutex_ );
            auto             mergeResult = set_->Merge( aDelta, hKey.GetKey() );
            if ( mergeResult.has_failure() )
            {
                logger_->error( "ProcessNode: error merging delta from {}", hKey.GetKey() );
                return outcome::failure( mergeResult.error() );
            }
            logger_->trace( "ProcessNode: merged delta from {} (priority: {})",
                            strCidResult.value(),
                            aDelta->priority() );
        }

        std::vector<CID> children;
        auto             links = aNode->getLinks();

        if ( links.empty() )
        {
            // we reached the bottom, we are a leaf.
            auto addHeadResult = heads_->Add( aRoot, aRootPrio );
            if ( addHeadResult.has_failure() )
            {
                logger_->error( "ProcessNode: error adding head {}", aRoot.toString().value() );
                return outcome::failure( addHeadResult.error() );
            }
        }
        else
        {
            // walkToChildren
            for ( const auto &link : links )
            {
                auto child        = link.get().getCID();
                auto isHeadResult = heads_->IsHead( child );

                if ( isHeadResult )
                {
                    // reached one of the current heads.Replace it with
                    // the tip of this branch
                    auto replaceResult = heads_->Replace( child, aRoot, aRootPrio );
                    if ( replaceResult.has_failure() )
                    {
                        logger_->error( "ProcessNode: error replacing head {} -> {}",
                                        child.toString().value(),
                                        aRoot.toString().value() );
                        return outcome::failure( replaceResult.error() );
                    }
                    AddProcessedCID( child );
                    continue;
                }

                std::unique_lock lock( dagWorkerMutex_ );
                auto             knowBlockResult = dagSyncer_->HasBlock( child );
                if ( knowBlockResult.has_failure() )
                {
                    logger_->error( "ProcessNode: error checking for known block {}", child.toString().value() );
                    return outcome::failure( knowBlockResult.error() );
                }

                if ( knowBlockResult.value() )
                {
                    // we reached a non-head node in the known tree.
                    // This means our root block is a new head.
                    auto addHeadResult = heads_->Add( aRoot, aRootPrio );
                    if ( addHeadResult.has_failure() )
                    {
                        // Don't let this failure prevent us from processing the other links.
                        logger_->error( "ProcessNode: error adding head {}", aRoot.toString().value() );
                    }
                    AddProcessedCID( child );
                    continue;
                }

                children.push_back( child );
                AddProcessedCID( child );
            }
        }

        AddProcessedCID( aRoot );
        return children;
    }

    outcome::result<CID> CrdtDatastore::AddDAGNode( const std::shared_ptr<Delta> &aDelta )
    {
        if ( heads_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        uint64_t         height = 0;
        std::vector<CID> heads;
        auto             getListResult = heads_->GetList( heads, height );
        if ( getListResult.has_failure() )
        {
            return outcome::failure( getListResult.error() );
        }

        height = height + 1; // This implies our minimum height is 1
        aDelta->set_priority( height );

        auto putBlockResult = PutBlock( heads, aDelta );
        if ( putBlockResult.has_failure() )
        {
            return outcome::failure( putBlockResult.error() );
        }

        auto node = putBlockResult.value();

        // Process new block. This makes that every operation applied
        // to this store take effect (delta is merged) before
        // returning. Since our block references current heads, children
        // should be empty
        logger_->info( "AddDAGNode: Processing generated block {} from {}",
                       node->getCID().toString().value(),
                       reinterpret_cast<uint64_t>( this ) );

        auto processNodeResult = ProcessNode( node->getCID(), height, aDelta, node );
        if ( processNodeResult.has_failure() )
        {
            logger_->error( "AddDAGNode: error processing new block" );
            return outcome::failure( processNodeResult.error() );
        }

        if ( !processNodeResult.value().empty() )
        {
            logger_->error( "AddDAGNode: bug - created a block to unknown children" );
        }

        return node->getCID();
    }

    outcome::result<void> CrdtDatastore::PrintDAG()
    {
        if ( heads_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        uint64_t         height = 0;
        std::vector<CID> heads;
        auto             getListResult = heads_->GetList( heads, height );
        if ( getListResult.has_failure() )
        {
            return outcome::failure( getListResult.error() );
        }

        std::vector<CID> set;
        for ( const auto &head : heads )
        {
            auto printResult = PrintDAGRec( head, 0, set );
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

        if ( dagSyncer_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto getNodeResult = dagSyncer_->getNode( aCID );

        if ( getNodeResult.has_failure() )
        {
            return outcome::failure( getNodeResult.error() );
        }
        auto node = getNodeResult.value();

        auto delta      = std::make_shared<Delta>();
        auto nodeBuffer = node->content();
        if ( !delta->ParseFromArray( nodeBuffer.data(), nodeBuffer.size() ) )
        {
            logger_->error( "PrintDAGRec: failed to parse delta from node" );
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
            PrintDAGRec( link.get().getCID(), aDepth + 1, aSet );
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
            return Sync( namespaceKey_ );
        }

        // attempt to be intelligent and sync only all heads and the
        // set entries related to the given prefix.
        std::vector<HierarchicalKey> keysToSync;
        keysToSync.push_back( set_->ElemsPrefix( aKey.GetKey() ) );
        keysToSync.push_back( set_->TombsPrefix( aKey.GetKey() ) );
        keysToSync.push_back( set_->KeysKey( aKey.GetKey() ) ); // covers values and priorities
        keysToSync.push_back( heads_->GetNamespaceKey() );
        return SyncDatastore( keysToSync );
    }

    outcome::result<void> CrdtDatastore::SyncDatastore( const std::vector<HierarchicalKey> &aKeyList )
    {
        if ( dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // Call the crdt set sync. We don't need to
        // Because a store is shared with SET. Only
        return set_->DataStoreSync( aKeyList );
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

    void CrdtDatastore::PrintDataStore()
    {
        set_->PrintDataStore();
    }

    void CrdtDatastore::FilterElementsOnDelta( std::shared_ptr<Delta> &delta )
    {
        if ( elem_filter_cb_ )
        {
            logger_->info( "Checking if filter this delta or not" );
            std::vector<int> indices_to_keep;
            indices_to_keep.reserve( delta->elements_size() );

            for ( int i = 0; i < delta->elements_size(); i++ )
            {
                if ( ( *elem_filter_cb_ )( delta->elements( i ) ) )
                {
                    indices_to_keep.push_back( i );
                }
            }

            // If some elements need to be removed or all were filtered out, rebuild the delta
            if ( static_cast<int>( indices_to_keep.size() ) < delta->elements_size() )
            {
                std::vector<Element> kept_elements;
                kept_elements.reserve( indices_to_keep.size() );

                // Collect all kept elements
                for ( int idx : indices_to_keep )
                {
                    kept_elements.push_back( delta->elements( idx ) );
                }

                // Rebuild the delta with only the kept elements (might be empty)
                delta->clear_elements();
                for ( const auto &element : kept_elements )
                {
                    auto *new_element = delta->add_elements();
                    *new_element      = element;
                }
            }
        }
    }

    void CrdtDatastore::FilterTombstonesOnDelta( std::shared_ptr<Delta> &delta )
    {
        if ( tomb_filter_cb_ )
        {
            logger_->info( "Checking if filter this delta or not" );
            std::vector<int> indices_to_keep;
            indices_to_keep.reserve( delta->elements_size() );

            for ( int i = 0; i < delta->elements_size(); i++ )
            {
                if ( ( *tomb_filter_cb_ )( delta->elements( i ) ) )
                {
                    indices_to_keep.push_back( i );
                }
            }

            // If some elements need to be removed or all were filtered out, rebuild the delta
            if ( static_cast<int>( indices_to_keep.size() ) < delta->elements_size() )
            {
                std::vector<Element> kept_elements;
                kept_elements.reserve( indices_to_keep.size() );

                // Collect all kept elements
                for ( int idx : indices_to_keep )
                {
                    kept_elements.push_back( delta->elements( idx ) );
                }

                // Rebuild the delta with only the kept elements (might be empty)
                delta->clear_elements();
                for ( const auto &element : kept_elements )
                {
                    auto *new_element = delta->add_elements();
                    *new_element      = element;
                }
            }
        }
    }
}
