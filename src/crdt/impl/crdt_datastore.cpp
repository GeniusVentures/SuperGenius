#include <fmt/std.h>
#include "crdt/crdt_datastore.hpp"
#include <storage/rocksdb/rocksdb.hpp>
#include <iostream>
#include "crdt/proto/bcast.pb.h"
#include <google/protobuf/unknown_field_set.h>
#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>
#include <thread>
#include <utility>

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::crdt, CrdtDatastore::Error, e )
{
    using CrdtDatastoreErr = sgns::crdt::CrdtDatastore::Error;
    switch ( e )
    {
        case CrdtDatastoreErr::INVALID_PARAM:
            return "Invalid parameter";
        case CrdtDatastoreErr::FETCH_ROOT_NODE:
            return "Can't fetch the root node";
        case CrdtDatastoreErr::NODE_DESERIALIZATION:
            return "Can't deserialize node buffer into node";
        case CrdtDatastoreErr::FETCHING_GRAPH:
            return "Can't fetch graph";
        case CrdtDatastoreErr::NODE_CREATION:
            return "Can't create a node";
        case CrdtDatastoreErr::GET_NODE:
            return "Can't fetch the node";
    }
    return "Unknown error";
}

namespace sgns::crdt
{

    using CRDTBroadcast = pb::CRDTBroadcast;

    std::shared_ptr<CrdtDatastore> CrdtDatastore::New( std::shared_ptr<RocksDB>            aDatastore,
                                                       const HierarchicalKey              &aKey,
                                                       std::shared_ptr<DAGSyncer>          aDagSyncer,
                                                       std::shared_ptr<Broadcaster>        aBroadcaster,
                                                       const std::shared_ptr<CrdtOptions> &aOptions )
    {
        if ( ( aDatastore == nullptr ) || ( aDagSyncer == nullptr ) || ( aBroadcaster == nullptr ) )
        {
            return nullptr;
        }
        auto crdtInstance = std::shared_ptr<CrdtDatastore>( new CrdtDatastore( std::move( aDatastore ),
                                                                               aKey,
                                                                               std::move( aDagSyncer ),
                                                                               std::move( aBroadcaster ),
                                                                               aOptions ) );

        return crdtInstance;
    }

    void CrdtDatastore::Start()
    {
        if ( started_ == true )
        {
            return;
        }
        //heads_->PrimeCache();
        handleNextThreadRunning_ = true;
        // Starting HandleNext worker thread
        handleNextFuture_ = std::async(
            [weakptr{ weak_from_this() }]()
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

        rebroadcastThreadRunning_ = true;
        // Starting Rebroadcast worker thread
        rebroadcastFuture_ = std::async(
            [weakptr{ weak_from_this() }]()
            {
                auto self = weakptr.lock();
                if ( !self )
                {
                    return;
                }

                const auto interval = std::chrono::milliseconds(
                    self->options_ ? self->options_->rebroadcastIntervalMilliseconds : 100 );
                std::unique_lock<std::mutex> lock( self->rebroadcastMutex_ );

                while ( self->rebroadcastThreadRunning_ )
                {
                    self->RebroadcastHeads();
                    self->rebroadcastCv_.wait_for( lock, interval );
                }
            } );

        dagWorkerJobListThreadRunning_ = true;
        dagWorkers_.reserve(numberOfDagWorkers);
        for ( int i = 0; i < numberOfDagWorkers; ++i )
        {
            auto dagWorker                     = std::make_shared<DagWorker>();
            dagWorker->dagWorkerThreadRunning_ = true;
            dagWorker->dagWorkerFuture_        = std::async(
                [weakptr{ weak_from_this() }, dagWorker]()
                {
                    auto dagThreadRunning = true;
                    while ( dagThreadRunning )
                    {
                        if ( auto self = weakptr.lock() )
                        {
                            std::unique_lock<std::mutex> cvlock( self->dagWorkerCvMutex_ );
                            self->dagWorkerCv_.wait_for(
                                cvlock,
                                threadSleepTimeInMilliseconds_,
                                [&]
                                { return !self->dagWorkerJobList.empty() || !dagWorker->dagWorkerThreadRunning_; } );
                            cvlock.unlock();
                            if ( dagWorker->dagWorkerThreadRunning_ )
                            {
                                self->SendJobWorkerIteration( dagWorker );
                            }
                            else
                            {
                                dagThreadRunning = false;
                            }
                        }
                        else
                        {
                            dagThreadRunning = false;
                        }
                    }
                } );
            dagWorkers_.push_back( dagWorker );
        }
        started_ = true;
    }

    CrdtDatastore::CrdtDatastore( std::shared_ptr<RocksDB>            aDatastore,
                                  const HierarchicalKey              &aKey,
                                  std::shared_ptr<DAGSyncer>          aDagSyncer,
                                  std::shared_ptr<Broadcaster>        aBroadcaster,
                                  const std::shared_ptr<CrdtOptions> &aOptions ) :
        dataStore_( std::move( aDatastore ) ),
        namespaceKey_( aKey ),
        broadcaster_( std::move( aBroadcaster ) ),
        dagSyncer_( std::move( aDagSyncer ) ),
        crdt_filter_( true )
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

        auto getListResult = heads_->GetList();
        if ( !getListResult.has_failure() )
        {
            auto [head_map, maxHeight] = getListResult.value();
            for ( const auto &[topic_name, cid_set] : head_map )
            {
                numberOfHeads += cid_set.size();
            }
        }

        logger_->info( "crdt Datastore created. Number of heads: {} Current max-height: {}", numberOfHeads, maxHeight );
    }

    CrdtDatastore::~CrdtDatastore()
    {
        logger_->debug( "~CrdtDatastore CALLED at {} ", std::this_thread::get_id() );
        Close();
    }

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
        dagSyncer_->Stop();
        if ( handleNextThreadRunning_ )
        {
            handleNextThreadRunning_ = false;
            handleNextFuture_.wait();
        }

        if ( rebroadcastThreadRunning_ )
        {
            rebroadcastThreadRunning_ = false;
            rebroadcastCv_.notify_one();
            rebroadcastFuture_.wait();
        }

        if ( dagWorkerJobListThreadRunning_ )
        {
            for ( const auto &dagWorker : dagWorkers_ )
            {
                dagWorker->dagWorkerThreadRunning_ = false;
            }
            dagWorkerCv_.notify_all();
            for ( const auto &dagWorker : dagWorkers_ )
            {
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
        std::vector<CID> heads_to_process_cids;
        heads_to_process_cids.reserve( decodeResult.value().size() );

        for ( const auto &bCastHeadCID : decodeResult.value() )
        {
            auto dagSyncerResult = dagSyncer_->HasBlock( bCastHeadCID );
            if ( dagSyncerResult.has_failure() )
            {
                logger_->error( "HandleBlock: error checking for known block" );
                continue;
            }
            if ( dagSyncerResult.value() )
            {
                // cid is known. Skip walking tree
                logger_->trace( "HandleBlock: Already processed block {}", bCastHeadCID.toString().value() );
                continue;
            }

            if ( dagSyncer_->IsCIDInCache( bCastHeadCID ) )
            {
                //If the CID request was already triggered but ProcessNode not ran
                logger_->trace( "HandleBlock: Processing block {} on graphsync", bCastHeadCID.toString().value() );
                continue;
            }
            logger_->debug( "HandleBlock: Starting processing block {}", bCastHeadCID.toString().value() );
            dagSyncer_->InitCIDBlock( bCastHeadCID );

            heads_to_process_cids.emplace_back( bCastHeadCID );
        }
        for ( const auto &bCastHeadCID : heads_to_process_cids )
        {
            auto handleBlockResult = HandleBlock( bCastHeadCID );
            if ( handleBlockResult.has_failure() )
            {
                logger_->error( "Broadcaster: Unable to handle block (error code {})",
                                std::to_string( handleBlockResult.error().value() ) );
                continue;
            }
        }
    }

    void CrdtDatastore::SendJobWorkerIteration( std::shared_ptr<DagWorker> dagWorker )
    {
        std::vector<DagJob>       jobs_to_process;
        CID                       current_root_cid;
        std::set<CID>             aggregated_cids_to_fetch;
        std::shared_ptr<IPLDNode> root_node;
        uint64_t                  root_priority = 0;

        logger_->trace( "In SendJobWorkerIteration. Jobs left: {}", dagWorkerJobList.size() );

        // Extract all jobs with the same rootCid
        {
            std::unique_lock lock( dagWorkerMutex_ );
            if ( dagWorkerJobList.empty() )
            {
                return;
            }

            // Get the first job to determine the rootCid we're processing
            DagJob first_job = dagWorkerJobList.front();
            dagWorkerJobList.pop();
            jobs_to_process.push_back( first_job );
            current_root_cid = first_job.rootCid_;
            root_priority    = first_job.rootPriority_;
            root_node        = first_job.root_node_;

            // Collect all remaining jobs with the same rootCid
            std::queue<DagJob> temp_queue;
            while ( !dagWorkerJobList.empty() )
            {
                DagJob job = dagWorkerJobList.front();
                dagWorkerJobList.pop();

                if ( job.rootCid_ == current_root_cid )
                {
                    jobs_to_process.push_back( job );
                }
                else
                {
                    temp_queue.push( job );
                }
            }

            // Put back the jobs with different rootCid
            dagWorkerJobList = std::move( temp_queue );
        }

        logger_->info( "SendJobWorker processing {} jobs for rootCid={}",
                       jobs_to_process.size(),
                       current_root_cid.toString().value() );

        // Process all jobs and aggregate CIDs to fetch
        for ( const auto &dagJob : jobs_to_process )
        {
            logger_->info( "SendJobWorker CID={} nodeCID={} priority={}",
                           current_root_cid.toString().value(),
                           dagJob.node_->getCID().toString().value(),
                           std::to_string( root_priority ) );

            auto childrenResult = ProcessNode( current_root_cid, root_priority, dagJob.delta_, dagJob.node_, true );
            if ( childrenResult.has_failure() )
            {
                logger_->error( "SendNewJobs: failed to process node:{}", current_root_cid.toString().value() );
                continue; // Continue processing other nodes
            }
            logger_->info( "SendJobWorker: Processed CID={} nodeCID={}",
                           current_root_cid.toString().value(),
                           dagJob.node_->getCID().toString().value() );

            // Aggregate CIDs to fetch
            auto CIDs_to_fetch = childrenResult.value();
            aggregated_cids_to_fetch.insert( CIDs_to_fetch.begin(), CIDs_to_fetch.end() );

            // Handle non-root nodes
            if ( current_root_cid != dagJob.node_->getCID() )
            {
                auto dagSyncerResult = dagSyncer_->addNode( dagJob.node_ );
                logger_->debug( "DAGSyncer: Adding new block {}", dagJob.node_->getCID().toString().value() );
                if ( dagSyncerResult.has_failure() )
                {
                    logger_->error( "DAGSyncer: error writing new block {}",
                                    dagJob.node_->getCID().toString().value() );
                }
                dagSyncer_->DeleteCIDBlock( dagJob.node_->getCID() );
            }
            else
            {
                logger_->debug( "ROOT NODE RECEIVED, will process later" );
                // Store root node for later processing
                if ( !root_node )
                {
                    root_node = dagJob.node_;
                }
            }
        }

        // Now handle the aggregated results
        if ( aggregated_cids_to_fetch.empty() )
        {
            // No more children, now record the root CID if we have it
            if ( root_node )
            {
                auto dagSyncerResult = dagSyncer_->addNode( root_node );
                logger_->debug( "DAGSyncer: Adding ROOT block {}", root_node->getCID().toString().value() );
                if ( dagSyncerResult.has_failure() )
                {
                    logger_->error( "DAGSyncer: error writing ROOT block {}", current_root_cid.toString().value() );
                }
            }
            else
            {
                logger_->error( "No Root node to add after getting the children {}",
                                current_root_cid.toString().value() );
            }
            dagSyncer_->DeleteCIDBlock( current_root_cid );
        }
        else
        {
            // Make a single call to SendNewJobs with all aggregated CIDs
            logger_->info( "Sending {} aggregated CIDs to SendNewJobs for rootCid={}",
                           aggregated_cids_to_fetch.size(),
                           current_root_cid.toString().value() );

            auto send_jobs_ret = SendNewJobs( current_root_cid, root_priority, aggregated_cids_to_fetch, root_node );
            if ( send_jobs_ret.has_error() )
            {
                dagSyncer_->DeleteCIDBlock( current_root_cid );
                logger_->error( "Error getting children CIDs from root {}", current_root_cid.toString().value() );
            }
        }
    }

    outcome::result<std::vector<CID>> CrdtDatastore::DecodeBroadcast( const Buffer &buff )
    {
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

        auto msgReflect = bcastData.GetReflection();

        if ( msgReflect == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        if ( !msgReflect->GetUnknownFields( bcastData ).empty() )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        std::vector<CID> bCastHeads;
        for ( const auto &head : bcastData.heads() )
        {
            auto cidResult = CID::fromString( head.cid() );
            if ( cidResult.has_failure() )
            {
                return outcome::failure( boost::system::error_code{} );
            }
            else
            {
                bCastHeads.push_back( cidResult.value() );
            }
        }
        return bCastHeads;
    }

    outcome::result<CrdtDatastore::Buffer> CrdtDatastore::EncodeBroadcast( const std::set<CID> &heads )
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
        auto getListResult = heads_->GetList();
        if ( getListResult.has_failure() )
        {
            logger_->error( "RebroadcastHeads: Failed to get list of heads (error code {})", getListResult.error() );
            return;
        }
        auto [head_map, maxHeight] = getListResult.value();

        for ( const auto &[topic_name, cid_set] : head_map ) // Changed from cid_map to head_map
        {
            auto broadcastResult = Broadcast( cid_set, topic_name );
            if ( broadcastResult.has_failure() )
            {
                logger_->error( "RebroadcastHeads: Broadcast failed" );
            }
            else
            {
                logger_->trace( "RebroadcastHeads: Broadcasted CIDs to topic {} ", topic_name );
                for ( const auto &cid : cid_set )
                {
                    logger_->trace( "RebroadcastHeads: CID {} ", cid.toString().value() );
                }
            }
        }
    }

    outcome::result<void> CrdtDatastore::HandleBlock( const CID &aCid )
    {
        return SendNewJobs( aCid, 0, { aCid } );
    }

    outcome::result<void> CrdtDatastore::SendNewJobs( const CID                &aRootCID,
                                                      uint64_t                  aRootPriority,
                                                      const std::set<CID>      &aChildren,
                                                      std::shared_ptr<IPLDNode> aRootNode )
    {
        // sendNewJobs calls getDeltas with the given
        // children and sends each response to the workers.

        if ( aChildren.empty() )
        {
            return CrdtDatastore::Error::INVALID_PARAM;
        }
        // @todo figure out how should dagSyncerTimeoutSec be used
        std::chrono::seconds dagSyncerTimeoutSec = std::chrono::seconds( 5 * 60 ); // 5 mins by default
        if ( options_ != nullptr )
        {
            dagSyncerTimeoutSec = std::chrono::seconds( options_->dagSyncerTimeoutSec );
        }

        uint64_t                  rootPriority = aRootPriority;
        std::shared_ptr<IPLDNode> rootNode     = aRootNode;

        if ( rootPriority == 0 )
        {
            std::unique_lock lock( dagSyncherMutex_ );
            auto             getNodeResult = dagSyncer_->getNode( *( aChildren.begin() ) );
            lock.unlock();
            if ( getNodeResult.has_failure() )
            {
                return Error::FETCH_ROOT_NODE;
            }

            logger_->debug( "Getting Node: TRYING TO FETCH NODE {}", reinterpret_cast<uint64_t>( this ) );

            auto node       = getNodeResult.value();
            auto nodeBuffer = node->content();

            auto delta = std::make_shared<Delta>();

            if ( !delta->ParseFromArray( nodeBuffer.data(), nodeBuffer.size() ) )
            {
                return CrdtDatastore::Error::NODE_DESERIALIZATION;
            }

            rootPriority = delta->priority();

            // If this is the first call and we're fetching the root, store it
            if ( !rootNode && aChildren.size() == 1 && ( *( aChildren.begin() ) ) == aRootCID )
            {
                rootNode = node;
            }
        }

        for ( const auto &cid : aChildren )
        {
            logger_->debug( "SendNewJobs: TRYING TO FETCH NODE : {} from {}",
                            cid.toString().value(),
                            reinterpret_cast<uint64_t>( this ) );

            // Single attempt to fetch the graph - getNode internally already has retry logic
            std::unique_lock lock( dagSyncherMutex_ );
            auto             nodeResult = dagSyncer_->getNode( cid );
            if ( nodeResult.has_error() )
            {
                logger_->error( "SendNewJobs: Can't fetch node: {}", nodeResult.error().message() );
                return CrdtDatastore::Error::GET_NODE;
            }
            auto node       = nodeResult.value();
            auto nodeBuffer = node->content();

            auto delta = std::make_shared<Delta>();
            if ( !delta->ParseFromArray( nodeBuffer.data(), nodeBuffer.size() ) )
            {
                logger_->error( "SendNewJobs: Can't parse data with size {}", nodeBuffer.size() );
                return CrdtDatastore::Error::NODE_DESERIALIZATION;
            }

            DagJob dagJob;
            dagJob.rootCid_      = aRootCID;
            dagJob.rootPriority_ = rootPriority;
            dagJob.delta_        = delta;
            dagJob.node_         = node;
            dagJob.root_node_    = rootNode; // Pass the root node through the job

            logger_->debug( "SendNewJobs PUSHING CID={} nodeCID={} priority={} ",
                            dagJob.rootCid_.toString().value(),
                            node->getCID().toString().value(),
                            std::to_string( dagJob.rootPriority_ ) );
            {
                std::unique_lock lock( dagWorkerMutex_ );
                dagWorkerJobList.push( dagJob );
            }
            dagWorkerCv_.notify_one();
        }
        return outcome::success();
    }

    outcome::result<CrdtDatastore::Buffer> CrdtDatastore::GetKey( const HierarchicalKey &aKey )
    {
        return set_->GetElement( aKey.GetKey() );
    }

    outcome::result<std::string> CrdtDatastore::GetKeysPrefix()
    {
        return set_->KeysKey( "" ).GetKey() + "/";
    }

    outcome::result<std::string> CrdtDatastore::GetValueSuffix()
    {
        return "/" + set_->GetValueSuffix();
    }

    outcome::result<CrdtDatastore::QueryResult> CrdtDatastore::QueryKeyValues( const std::string &aPrefix )
    {
        return set_->QueryElements( aPrefix, CrdtSet::QuerySuffix::QUERY_VALUESUFFIX );
    }

    outcome::result<CrdtDatastore::QueryResult> CrdtDatastore::QueryKeyValues( const std::string &prefix_base,
                                                                               const std::string &middle_part,
                                                                               const std::string &remainder_prefix )
    {
        if ( set_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        return set_->QueryElements( prefix_base,
                                    middle_part,
                                    remainder_prefix,
                                    CrdtSet::QuerySuffix::QUERY_VALUESUFFIX );
    }

    outcome::result<bool> CrdtDatastore::HasKey( const HierarchicalKey &aKey )
    {
        return set_->IsValueInSet( aKey.GetKey() );
    }

    outcome::result<void> CrdtDatastore::PutKey( const HierarchicalKey &aKey,
                                                 const Buffer          &aValue,
                                                 std::set<std::string>  topics )
    {
        auto deltaResult = CreateDeltaToAdd( aKey.GetKey(), std::string( aValue.toString() ) );
        if ( deltaResult.has_failure() )
        {
            return outcome::failure( deltaResult.error() );
        }

        auto publishResult = Publish( deltaResult.value(), topics );
        if ( deltaResult.has_failure() )
        {
            return outcome::failure( publishResult.error() );
        }

        return outcome::success();
    }

    outcome::result<void> CrdtDatastore::DeleteKey( const HierarchicalKey &aKey, const std::set<std::string> &topics )
    {
        auto deltaResult = CreateDeltaToRemove( aKey.GetKey() );
        if ( deltaResult.has_failure() )
        {
            return outcome::failure( deltaResult.error() );
        }

        if ( deltaResult.value()->tombstones().empty() )
        {
            return outcome::success();
        }

        auto publishResult = Publish( deltaResult.value(), topics );
        if ( deltaResult.has_failure() )
        {
            return outcome::failure( publishResult.error() );
        }

        return outcome::success();
    }

    outcome::result<CID> CrdtDatastore::Publish( const std::shared_ptr<Delta> &aDelta,
                                                 const std::set<std::string>  &topics )
    {
        OUTCOME_TRY( auto &&newCID, AddDAGNode( aDelta, topics ) );

        return newCID;
    }

    outcome::result<void> CrdtDatastore::Broadcast( const std::set<CID> &cids, std::string topic )
    {
        if ( !broadcaster_ )
        {
            logger_->error( "Broadcast: No broadcaster, Failed to broadcast" );
            return outcome::failure( boost::system::error_code{} );
        }
        if ( cids.empty() )
        {
            logger_->error( "Broadcast: Cids Empty, Failed to broadcast" );
            return outcome::success();
        }
        auto encodedBufferResult = EncodeBroadcast( cids );
        if ( encodedBufferResult.has_failure() )
        {
            logger_->error( "Broadcast: Encoding failed, Failed to broadcast" );

            return outcome::failure( encodedBufferResult.error() );
        }

        auto bcastResult = broadcaster_->Broadcast( encodedBufferResult.value(), topic );
        if ( bcastResult.has_failure() )
        {
            logger_->error( "Broadcast: Broadcaster failed to broadcast" );
            return outcome::failure( bcastResult.error() );
        }
        return outcome::success();
    }

    outcome::result<std::shared_ptr<CrdtDatastore::IPLDNode>> CrdtDatastore::PutBlock(
        const std::vector<std::pair<CID, std::string>> &aHeads,
        const std::shared_ptr<Delta>                   &aDelta,
        std::set<std::string>                           topics )
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
        logger_->info( "PutBlock: added destination for block {{ cid=\"{}\" }}", node->getCID().toString().value() );
        for ( auto &topic : topics )
        {
            logger_->info( "Topics {{ name=\"{}\" }}", topic );
            node->addDestination( topic );
        }
        for ( const auto &h : aHeads )
        {
            const CID         &head  = h.first;
            const std::string &topic = h.second;

            auto cidByte = head.toBytes();
            if ( cidByte.has_failure() )
            {
                continue;
            }
            ipfs_lite::ipld::IPLDLinkImpl link = ipfs_lite::ipld::IPLDLinkImpl( head, topic, cidByte.value().size() );
            node->addLink( link );

            logger_->info( "PutBlock: added link {{ cid=\"{}\", name=\"{}\", size={} }}",
                           link.getCID().toString().value(),
                           link.getName(),
                           link.getSize() );
        }

        return node;
    }

    outcome::result<std::set<CID>> CrdtDatastore::ProcessNode( const CID                       &aRoot,
                                                               uint64_t                         aRootPrio,
                                                               std::shared_ptr<Delta>           aDelta,
                                                               const std::shared_ptr<IPLDNode> &aNode,
                                                               bool                             filter_crdt )
    {
        if ( aDelta == nullptr || aNode == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        std::set<std::string> topics_to_update_cid = aNode->getDestinations();

        auto current         = aNode->getCID();
        auto strCidResult    = current.toString();
        bool skip_if_visited = false;
        if ( strCidResult.has_failure() )
        {
            return outcome::failure( strCidResult.error() );
        }
        HierarchicalKey hKey( strCidResult.value() );
        if ( filter_crdt )
        {
            crdt_filter_.FilterElementsOnDelta( aDelta );
            //crdt_filter_.FilterTombstonesOnDelta( aDelta );
            logger_->error( "ProcessNode: Processing INCOMING root {} node {}",
                            aRoot.toString().value(),
                            aNode->getCID().toString().value() );
        }
        else
        {
            logger_->error( "ProcessNode: Processing OUTGOING root {} node {}",
                            aRoot.toString().value(),
                            aNode->getCID().toString().value() );
            skip_if_visited = true;
        }

        {
            std::unique_lock lock( dagSetMutex_ );
            auto             mergeResult = set_->Merge( aDelta, hKey.GetKey() );
            if ( mergeResult.has_failure() )
            {
                logger_->error( "ProcessNode: error merging delta from {}", hKey.GetKey() );
                return outcome::failure( mergeResult.error() );
            }
        }

        auto priority = aDelta->priority();
        if ( priority % 10 == 0 )
        {
            logger_->info( "ProcessNode: merged delta from {} (priority: {})", strCidResult.value(), priority );
        }

        std::set<CID> children;

        if ( aNode->getLinks().empty() )
        {
            for ( auto &topic : topics_to_update_cid )
            {
                logger_->debug( "Adding: {} to heads on topic {} ", aNode->getCID().toString().value(), topic );
                auto addHeadResult = heads_->Add( aRoot, aRootPrio, topic );
                if ( addHeadResult.has_failure() )
                {
                    logger_->error( "ProcessNode: error adding head {}", aRoot.toString().value() );
                    return outcome::failure( addHeadResult.error() );
                }
            }
        }
        else
        {
            for ( auto &topic : topics_to_update_cid )
            {
                logger_->error( "ProcessNode: Traversing to find links on topic {}", topic );
                std::unique_lock lock( dagSyncherMutex_ );
                auto [links_to_fetch,
                      known_cids] = dagSyncer_->TraverseCIDsLinks( aNode, topic, {}, skip_if_visited, 50 );
                lock.unlock();
                for ( const auto &[cid, link_name] : known_cids )
                {
                    logger_->error( "ProcessNode: known cid: {}, {}", cid.toString().value(), link_name );
                    if ( heads_->IsHead( cid, link_name ) )
                    {
                        logger_->debug( "Replacing: {} with {} on topic {} ",
                                        cid.toString().value(),
                                        aRoot.toString().value(),
                                        link_name );
                        auto replaceResult = heads_->Replace( cid, aRoot, aRootPrio, link_name );
                        if ( replaceResult.has_failure() )
                        {
                            logger_->error( "ProcessNode: error replacing head {} -> {}",
                                            cid.toString().value(),
                                            aRoot.toString().value() );
                            return outcome::failure( replaceResult.error() );
                        }
                        continue;
                    }
                }
                if ( known_cids.empty() )
                {
                    logger_->debug( "Adding: {} to heads on topic {}", aRoot.toString().value(), topic );
                    auto addHeadResult = heads_->Add( aRoot, aRootPrio, topic );
                    if ( addHeadResult.has_failure() )
                    {
                        logger_->error( "ProcessNode: error adding head {}", aRoot.toString().value() );
                        return outcome::failure( addHeadResult.error() );
                    }
                }
                for ( const auto &[cid, link_name] : links_to_fetch )
                {
                    if ( topicNames_.find( link_name ) != topicNames_.end() )
                    {
                        children.emplace( cid );
                    }
                }
            }
        }
        rebroadcastCv_.notify_one();

        return children;
    }

    outcome::result<CID> CrdtDatastore::AddDAGNode( const std::shared_ptr<Delta> &aDelta,
                                                    const std::set<std::string>  &topics )
    {
        auto getListResult = heads_->GetList( topics );
        if ( getListResult.has_failure() )
        {
            return outcome::failure( getListResult.error() );
        }
        auto [head_map, height] = getListResult.value();

        height = height + 1; // This implies our minimum height is 1
        aDelta->set_priority( height );

        std::vector<std::pair<CID, std::string>> headsWithTopics;

        for ( const auto &[topic_name, cid_set] : head_map ) // Changed from cid_map to head_map
        {
            for ( const auto &cid : cid_set )
            {
                //logger_->debug( "AddDAGNode: pairing head {} with topic '{}'", cid.toString().value(), topic_name );
                headsWithTopics.emplace_back( cid, topic_name );
            }
        }

        auto putBlockResult = PutBlock( headsWithTopics, aDelta, topics );
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
        else
        {
            auto dagSyncerResult = dagSyncer_->addNode( node );
            logger_->debug( "DAGSyncer: Adding new block {}", node->getCID().toString().value() );
            if ( dagSyncerResult.has_failure() )
            {
                logger_->error( "DAGSyncer: error writing new block {}", node->getCID().toString().value() );
                return outcome::failure( dagSyncerResult.error() );
            }
            dagSyncer_->DeleteCIDBlock( node->getCID() );
        }

        return node->getCID();
    }

    outcome::result<void> CrdtDatastore::PrintDAG()
    {
        auto getListResult = heads_->GetList();
        if ( getListResult.has_failure() )
        {
            return outcome::failure( getListResult.error() );
        }
        auto [head_map, height] = getListResult.value();

        std::vector<CID> set;
        for ( const auto &[topic_name, cid_set] : head_map )
        {
            for ( const auto &cid : cid_set )
            {
                auto printResult = PrintDAGRec( cid, 0, set );
                if ( printResult.has_failure() )
                {
                    return outcome::failure( printResult.error() );
                }
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

    void CrdtDatastore::PrintDataStore()
    {
        set_->PrintDataStore();
    }

    bool CrdtDatastore::RegisterElementFilter( const std::string &pattern, CRDTElementFilterCallback filter )
    {
        return crdt_filter_.RegisterElementFilter( pattern, std::move( filter ) );
    }
}
