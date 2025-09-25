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
        case CrdtDatastoreErr::INVALID_JOB:
            return "The job is invalid";
    }
    return "Unknown error";
}

namespace sgns::crdt
{

    using CRDTBroadcast = pb::CRDTBroadcast;

    std::shared_ptr<CrdtDatastore> CrdtDatastore::New( std::shared_ptr<RocksDB>     aDatastore,
                                                       const HierarchicalKey       &aKey,
                                                       std::shared_ptr<DAGSyncer>   aDagSyncer,
                                                       std::shared_ptr<Broadcaster> aBroadcaster,
                                                       std::shared_ptr<CrdtOptions> aOptions )
    {
        if ( ( aDatastore == nullptr ) || ( aDagSyncer == nullptr ) || ( aBroadcaster == nullptr ) )
        {
            return nullptr;
        }
        if ( ( aDatastore == nullptr ) || aOptions->Verify().has_failure() ||
             ( aOptions->Verify().value() != CrdtOptions::VerifyErrorCode::Success ) )
        {
            return nullptr;
        }
        auto crdtInstance = std::shared_ptr<CrdtDatastore>( new CrdtDatastore( std::move( aDatastore ),
                                                                               aKey,
                                                                               std::move( aDagSyncer ),
                                                                               std::move( aBroadcaster ),
                                                                               std::move( aOptions ) ) );

        crdtInstance->set_ = std::make_shared<CrdtSet>(
            crdtInstance->dataStore_,
            aKey.ChildString( std::string( setsNamespace_ ) ),
            [weakptr( std::weak_ptr<CrdtDatastore>( crdtInstance ) )]( const std::string  &key,
                                                                       const base::Buffer &value )
            {
                if ( auto strong = weakptr.lock() )
                {
                    strong->PutElementsCallback( key, value );
                }
            },
            [weakptr( std::weak_ptr<CrdtDatastore>( crdtInstance ) )]( const std::string &key )
            {
                if ( auto strong = weakptr.lock() )
                {
                    strong->DeleteElementsCallback( key );
                }
            } );
        crdtInstance->dagWorkerJobListThreadRunning_ = true;
        crdtInstance->dagWorkers_.reserve( crdtInstance->numberOfDagWorkers );
        for ( int i = 0; i < crdtInstance->numberOfDagWorkers; ++i )
        {
            auto dagWorker                     = std::make_shared<DagWorker>();
            dagWorker->dagWorkerThreadRunning_ = true;
            dagWorker->dagWorkerFuture_        = std::async(
                [weakptr( std::weak_ptr<CrdtDatastore>( crdtInstance ) ), dagWorker]
                {
                    auto dagThreadRunning = true;
                    while ( dagThreadRunning )
                    {
                        if ( auto self = weakptr.lock() )
                        {
                            std::unique_lock cvlock( self->dagWorkerCvMutex_ );
                            self->dagWorkerCv_.wait_for(
                                cvlock,
                                threadSleepTimeInMilliseconds_,
                                [&] { return !self->rootCIDJobList_.empty() || !dagWorker->dagWorkerThreadRunning_; } );
                            if ( dagWorker->dagWorkerThreadRunning_ )
                            {
                                // Pop the job here
                                RootCIDJob job_to_process;
                                {
                                    std::unique_lock lock( self->dagWorkerMutex_ );
                                    if ( self->rootCIDJobList_.empty() )
                                    {
                                        continue; // No job, wait again
                                    }
                                    job_to_process = self->rootCIDJobList_.front();
                                    self->rootCIDJobList_.pop();
                                }

                                // Process the job
                                auto process_res = self->ProcessJobIteration( job_to_process );
                                if ( process_res.has_failure() )
                                {
                                    self->logger_->error( "CID PROCESSING ERROR: Cleaning up jobs for root CID {}",
                                                          job_to_process.root_node_->getCID().toString().value() );

                                    {
                                        std::unique_lock       lock( self->dagWorkerMutex_ );
                                        std::queue<RootCIDJob> temp_queue;
                                        while ( !self->rootCIDJobList_.empty() )
                                        {
                                            auto job = self->rootCIDJobList_.front();
                                            self->rootCIDJobList_.pop();
                                            if ( job.root_node_->getCID() != job_to_process.root_node_->getCID() )
                                            {
                                                temp_queue.push( job );
                                            }
                                        }
                                        self->rootCIDJobList_ = std::move( temp_queue ); // Restore the filtered queue
                                    }

                                    // Cleanup: Delete CID block for the root node
                                    (void)self->dagSyncer_->DeleteCIDBlock( job_to_process.root_node_->getCID() );

                                    // Cleanup: Erase from pendingHeadsByRootCID_
                                    {
                                        std::lock_guard<std::mutex> lock( self->pendingHeadsMutex_ );
                                        self->pendingHeadsByRootCID_.erase( job_to_process.root_node_->getCID() );
                                    }
                                }
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
            crdtInstance->dagWorkers_.push_back( dagWorker );
        }
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
            [weakptr{ weak_from_this() }]
            {
                auto threadRunning = true;
                while ( threadRunning )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        self->HandleCIDBroadcast();
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
            [weakptr{ weak_from_this() }]
            {
                auto self = weakptr.lock();
                if ( !self )
                {
                    return;
                }

                const auto interval = std::chrono::milliseconds(
                    self->options_ ? self->options_->rebroadcastIntervalMilliseconds : 100 );
                std::unique_lock lock( self->rebroadcastMutex_ );

                while ( self->rebroadcastThreadRunning_ )
                {
                    self->RebroadcastHeads();
                    self->rebroadcastCv_.wait_for( lock, interval );
                }
            } );

        started_ = true;
    }

    CrdtDatastore::CrdtDatastore( std::shared_ptr<RocksDB>     aDatastore,
                                  const HierarchicalKey       &aKey,
                                  std::shared_ptr<DAGSyncer>   aDagSyncer,
                                  std::shared_ptr<Broadcaster> aBroadcaster,
                                  std::shared_ptr<CrdtOptions> aOptions ) :
        dataStore_( std::move( aDatastore ) ),
        options_( std::move( aOptions ) ),
        namespaceKey_( aKey ),
        broadcaster_( std::move( aBroadcaster ) ),
        dagSyncer_( std::move( aDagSyncer ) ),
        crdt_filter_( true ),
        crdt_cb_manager_()
    {
        logger_            = options_->logger;
        numberOfDagWorkers = options_->numWorkers;

        heads_ = std::make_shared<CrdtHeads>( dataStore_, aKey.ChildString( std::string( headsNamespace_ ) ) );

        size_t   numberOfHeads = 0;
        uint64_t maxHeight     = 0;

        auto getListResult = heads_->GetList();
        if ( !getListResult.has_failure() )
        {
            auto [head_map, height] = getListResult.value();
            for ( const auto &[topic_name, cid_set] : head_map )
            {
                numberOfHeads += cid_set.size();
                maxHeight      = std::max( maxHeight, height );
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
        auto result = std::make_shared<Delta>();
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

    void CrdtDatastore::HandleCIDBroadcast()
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
                logger_->error( "{}: error checking for known block", __func__ );
                continue;
            }
            if ( dagSyncerResult.value() )
            {
                // cid is known. Skip walking tree
                logger_->trace( "{}: Already processed block {}", __func__, bCastHeadCID.toString().value() );
                continue;
            }

            if ( dagSyncer_->IsCIDInCache( bCastHeadCID ) )
            {
                //If the CID request was already triggered but node didn't finish processing
                logger_->trace( "{}: Processing block {} on graphsync", __func__, bCastHeadCID.toString().value() );
                continue;
            }
            logger_->debug( "{}: Starting processing block {}", __func__, bCastHeadCID.toString().value() );
            dagSyncer_->InitCIDBlock( bCastHeadCID );

            heads_to_process_cids.emplace_back( bCastHeadCID );
        }
        for ( const auto &bCastHeadCID : heads_to_process_cids )
        {
            auto handleBlockResult = HandleRootCIDBlock( bCastHeadCID );
            if ( handleBlockResult.has_failure() )
            {
                logger_->error( "Broadcaster: Unable to handle block (error {})", handleBlockResult.error().message() );
            }
        }
    }

    outcome::result<void> CrdtDatastore::HandleRootCIDBlock( const CID &aCid )
    {
        OUTCOME_TRY( auto &&root_job, CreateRootJob( aCid ) );

        OUTCOME_TRY( auto &&links, GetLinksToFetch( root_job ) );

        OUTCOME_TRY( FetchNodes( root_job, links ) );
        return outcome::success();
    }

    outcome::result<CrdtDatastore::RootCIDJob> CrdtDatastore::CreateRootJob( const CID &aRootCID )
    {
        logger_->debug( "{}: Creating the Root Job for CID {}", __func__, aRootCID.toString().value() );
        OUTCOME_TRY( auto &&root_node, dagSyncer_->getNode( aRootCID ) );

        logger_->debug( "{}: Root Job created for CID {}", __func__, aRootCID.toString().value() );

        RootCIDJob rootJob{ root_node, root_node, false };

        return rootJob;
    }

    outcome::result<std::set<CID>> CrdtDatastore::GetLinksToFetch( const RootCIDJob &job )
    {
        std::set<CID> cids_to_fetch;
        auto          node_to_process = job.node_;
        bool          processing_root = false;
        if ( node_to_process == nullptr )
        {
            node_to_process = job.root_node_;
            processing_root = true;
        }

        std::set<std::string> topics_to_update_cid = node_to_process->getDestinations();

        if ( node_to_process->getLinks().empty() )
        {
            for ( auto &topic : topics_to_update_cid )
            {
                logger_->debug( "{}: Recording head to add: {}, {}",
                                __func__,
                                job.root_node_->getCID().toString().value(),
                                topic );
                std::lock_guard<std::mutex> lock( pendingHeadsMutex_ );
                pendingHeadsByRootCID_[job.root_node_->getCID()].emplace( job.root_node_->getCID(), topic );
            }
        }
        else
        {
            logger_->debug( "{}: Checking links for CID {}", __func__, node_to_process->getCID().toString().value() );
            for ( auto &topic : topics_to_update_cid )
            {
                logger_->debug( "{}: Verifying topic {}", __func__, topic );

                auto [links_to_fetch,
                      known_cids] = dagSyncer_->TraverseCIDsLinks( *node_to_process, topic, {} );

                for ( const auto &[cid, _dontcare] : known_cids )
                {
                    logger_->debug( "{}: known cid: {}, {}", __func__, cid.toString().value(), _dontcare );
                    if ( heads_->IsHead( cid, _dontcare ) )
                    {
                        logger_->debug( "{}: Recording replacement of {} with {} on topic {} ({}) ",
                                        __func__,
                                        cid.toString().value(),
                                        job.root_node_->getCID().toString().value(),
                                        topic,
                                        _dontcare );
                        if ( topic != _dontcare )
                        {
                            logger_->error( "{}: Topic {} different from known {} ", __func__, topic, _dontcare );
                        }
                        std::lock_guard<std::mutex> lock( pendingHeadsMutex_ );
                        pendingHeadsByRootCID_[job.root_node_->getCID()].emplace( cid, topic );
                        logger_->debug( "{}: Recorded replacement of {} with {} on topic {} ({}) ",
                                        __func__,
                                        cid.toString().value(),
                                        job.root_node_->getCID().toString().value(),
                                        topic,
                                        _dontcare );
                    }
                }

                if ( known_cids.empty() )
                {
                    std::lock_guard<std::mutex> lock( pendingHeadsMutex_ );
                    pendingHeadsByRootCID_[job.root_node_->getCID()].emplace( job.root_node_->getCID(), topic );
                }

                for ( const auto &[cid, link_name] : links_to_fetch )
                {
                    if ( topicNames_.find( link_name ) != topicNames_.end() )
                    {
                        cids_to_fetch.emplace( cid );
                    }
                }
            }
        }
        return cids_to_fetch;
    }

    outcome::result<void> CrdtDatastore::FetchNodes( const RootCIDJob &aRootJob, const std::set<CID> &aLinks )
    {
        if ( aLinks.empty() )
        {
            logger_->debug( "{}: No links to fetch, sending root CID", __func__ );
            {
                RootCIDJob       root_node_only_job{ nullptr, aRootJob.root_node_, aRootJob.created_by_self_ };
                std::unique_lock lock( dagWorkerMutex_ );
                rootCIDJobList_.push( root_node_only_job );
            }
            dagWorkerCv_.notify_one();
            return outcome::success();
        }

        for ( const auto &cid : aLinks )
        {
            logger_->debug( "{}: Trying to fetch node {} from Root Job {} ",
                            __func__,
                            cid.toString().value(),
                            aRootJob.root_node_->getCID().toString().value() );

            OUTCOME_TRY( auto &&node, dagSyncer_->getNode( cid ) );

            RootCIDJob newRootJob;

            newRootJob.root_node_       = aRootJob.root_node_;
            newRootJob.node_            = node;
            newRootJob.created_by_self_ = false;

            logger_->debug( "{}: Got the node {} sending to workers. Root Job {} ",
                            __func__,
                            cid.toString().value(),
                            aRootJob.root_node_->getCID().toString().value() );
            {
                std::unique_lock lock( dagWorkerMutex_ );
                rootCIDJobList_.push( newRootJob );
            }
            dagWorkerCv_.notify_one();
        }
        return outcome::success();
    }

    outcome::result<pb::Delta> CrdtDatastore::GetDeltaFromNode( const IPLDNode &aNode, bool created_by_self )
    {
        auto nodeBuffer = aNode.content();

        auto delta = Delta();
        if ( !delta.ParseFromArray( nodeBuffer.data(), nodeBuffer.size() ) )
        {
            logger_->debug( "{}: Can't parse delta from node buffer {}", __func__, aNode.getCID().toString().value() );
            return CrdtDatastore::Error::NODE_DESERIALIZATION;
        }

        if ( !created_by_self )
        {
            crdt_filter_.FilterElementsOnDelta( delta );
            //crdt_filter_.FilterTombstonesOnDelta( aDelta );
            logger_->debug( "{}: Filtering node {} ", __func__, aNode.getCID().toString().value() );
        }
        else
        {
            logger_->debug( "{}: Posting node {} without filtering", __func__, aNode.getCID().toString().value() );
        }
        return delta;
    }

    outcome::result<void> CrdtDatastore::MergeDataFromDelta( const CID &node_cid, const Delta &aDelta )
    {
        OUTCOME_TRY( auto &&cid_string, node_cid.toString() );
        logger_->debug( "{}: Merging node {} On CRDT", __func__, cid_string );
        OUTCOME_TRY( set_->Merge( aDelta, cid_string ) );
        return outcome::success();
    }

    outcome::result<void> CrdtDatastore::ProcessJobIteration( const RootCIDJob &job_to_process )
    {
        logger_->debug( "{}: Starting to process Root CID", __func__ );

        OUTCOME_TRY( auto &&root_cid_string, job_to_process.root_node_->getCID().toString() );
        logger_->debug( "{}: Processing Root CID job {}", __func__, root_cid_string );

        auto node_to_process = job_to_process.node_;
        bool is_root         = false;
        if ( node_to_process == nullptr )
        {
            node_to_process = job_to_process.root_node_;
            is_root         = true;
        }

        OUTCOME_TRY( auto &&cid_string, node_to_process->getCID().toString() );

        OUTCOME_TRY( auto &&delta, GetDeltaFromNode( *node_to_process, job_to_process.created_by_self_ ) );

        logger_->debug( "{}: Merging Deltas from {}", __func__, cid_string );

        OUTCOME_TRY( MergeDataFromDelta( node_to_process->getCID(), delta ) );

        logger_->debug( "{}: Recording block on DAG Syncher {}", __func__, cid_string );
        OUTCOME_TRY( dagSyncer_->addNode( node_to_process ) );

        //OUTCOME_TRY( dagSyncer_->DeleteCIDBlock( job_to_process.node_->getCID() ));

        (void)dagSyncer_->DeleteCIDBlock( node_to_process->getCID() );

        OUTCOME_TRY( auto &&links, GetLinksToFetch( job_to_process ) );

        if ( links.empty() && !is_root )
        {
            //create one last job to finalize the root node
            logger_->debug( "{}: Finishing root job: {}, Creating the root CID job.", __func__, root_cid_string );
            RootCIDJob root_final_job{ nullptr, job_to_process.root_node_, job_to_process.created_by_self_ };
            {
                std::unique_lock lock( dagWorkerMutex_ );
                rootCIDJobList_.push( root_final_job );
            }
        }
        else if ( !links.empty() )
        {
            logger_->debug( "{}: Fetching {} links for Root job: {}", __func__, links.size(), root_cid_string );
            OUTCOME_TRY( FetchNodes( job_to_process, links ) );
            logger_->debug( "{}: Nodes fetched for Root job: {}", __func__, root_cid_string );
        }
        else if ( is_root )
        {
            logger_->debug( "{}: Root finalized: {}, Updating CRDT Heads", __func__, root_cid_string );
            UpdateCRDTHeads( job_to_process.root_node_->getCID(), delta.priority() );
        }
        return outcome::success();
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

            bCastHeads.push_back( cidResult.value() );
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

    outcome::result<CrdtDatastore::Buffer> CrdtDatastore::GetKey( const HierarchicalKey &aKey ) const
    {
        return set_->GetElement( aKey.GetKey() );
    }

    std::string CrdtDatastore::GetKeysPrefix() const
    {
        return set_->KeysKey( "" ).GetKey();
    }

    std::string CrdtDatastore::GetValueSuffix() const
    {
        return '/' + set_->GetValueSuffix();
    }

    outcome::result<CrdtDatastore::QueryResult> CrdtDatastore::QueryKeyValues( const std::string &aPrefix ) const
    {
        return set_->QueryElements( aPrefix, CrdtSet::QuerySuffix::QUERY_VALUESUFFIX );
    }

    outcome::result<CrdtDatastore::QueryResult> CrdtDatastore::QueryKeyValues(
        const std::string &prefix_base,
        const std::string &middle_part,
        const std::string &remainder_prefix ) const
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

    outcome::result<bool> CrdtDatastore::HasKey( const HierarchicalKey &aKey ) const
    {
        return set_->IsValueInSet( aKey.GetKey() );
    }

    outcome::result<void> CrdtDatastore::PutKey( const HierarchicalKey       &aKey,
                                                 const Buffer                &aValue,
                                                 const std::set<std::string> &topics )
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

    outcome::result<void> CrdtDatastore::Broadcast( const std::set<CID> &cids, const std::string &topic )
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
        const std::set<std::string>                    &topics ) const
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
        for ( const auto &[head, topic] : aHeads )
        {
            auto cidByte = head.toBytes();
            if ( cidByte.has_failure() )
            {
                continue;
            }
            ipfs_lite::ipld::IPLDLinkImpl link( head, topic, cidByte.value().size() );
            node->addLink( link );

            logger_->info( "PutBlock: added link {{ cid=\"{}\", name=\"{}\", size={} }}",
                           link.getCID().toString().value(),
                           link.getName(),
                           link.getSize() );
        }

        return node;
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

        RootCIDJob rootJob{ nullptr, node, true };

        {
            std::unique_lock lock( dagWorkerMutex_ );
            rootCIDJobList_.push( rootJob );
        }
        dagWorkerCv_.notify_one();

        // Timeout check for node insertion
        auto start      = std::chrono::steady_clock::now();
        bool node_found = false;
        while ( std::chrono::steady_clock::now() - start < std::chrono::seconds( 5 ) )
        {
            auto get_result = dagSyncer_->GetNodeWithoutRequest( node->getCID() );
            if ( get_result.has_value() )
            {
                node_found = true;
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); // Poll every 100ms
        }
        if ( !node_found )
        {
            logger_->error( "AddDAGNode: Timeout waiting for node {} to be inserted",
                            node->getCID().toString().value() );
            return outcome::failure( Error::NODE_CREATION ); // Or a custom error code like Error::NODE_TIMEOUT
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

        auto getNodeResult = dagSyncer_->GetNodeWithoutRequest( aCID );

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
        // This is a quick write-up of the internals from the time when
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
        return CrdtSet::CreateDeltaToAdd( key, value );
    }

    outcome::result<std::shared_ptr<CrdtDatastore::Delta>> CrdtDatastore::CreateDeltaToRemove(
        const std::string &key ) const
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

    bool CrdtDatastore::RegisterNewElementCallback( const std::string &pattern, CRDTNewElementCallback callback )
    {
        return crdt_cb_manager_.RegisterNewDataCallback( pattern, std::move( callback ) );
    }

    bool CrdtDatastore::RegisterDeletedElementCallback( const std::string         &pattern,
                                                        CRDTDeletedElementCallback callback )
    {
        return crdt_cb_manager_.RegisterDeletedDataCallback( pattern, std::move( callback ) );
    }

    void CrdtDatastore::PutElementsCallback( const std::string &key, const Buffer &value )
    {
        crdt_cb_manager_.PutDataCallback( key, value );
    }

    void CrdtDatastore::DeleteElementsCallback( const std::string &key )
    {
        crdt_cb_manager_.DeleteDataCallback( key );
    }

    void CrdtDatastore::UpdateCRDTHeads( const CID &rootCID, uint64_t rootPriority )
    {
        std::lock_guard<std::mutex> lock( pendingHeadsMutex_ );
        auto                        it = pendingHeadsByRootCID_.find( rootCID );
        if ( it == pendingHeadsByRootCID_.end() )
        {
            logger_->error( "{}: Error, untracked head {}", __func__, rootCID.toString().value() );
            return;
        }
        for ( const auto &[cid, topic] : it->second )
        {
            if ( cid == rootCID )
            {
                auto resolve_result = dagSyncer_->markResolved( cid );
                if ( resolve_result.has_failure() )
                {
                    logger_->error( "{}: error marking Root CID {} as resolved", __func__, cid.toString().value() );
                }
                auto add_result = heads_->Add( rootCID, rootPriority, topic );
                if ( add_result.has_failure() )
                {
                    logger_->error( "{}: error adding head {}", __func__, rootCID.toString().value() );
                }
                logger_->debug( "{}: Marking Head CID {} as resolved", __func__, rootCID.toString().value() );
            }
            else
            {
                auto is_resolved_result = dagSyncer_->isResolved( cid );
                if ( is_resolved_result.has_failure() )
                {
                    logger_->error( "{}: error checking if CID {} IS resolved",
                                               __func__,
                                               cid.toString().value() );
                    continue;
                }
                if ( !is_resolved_result.value() )
                {
                    logger_->debug( "{}: Previous Head {} not resolved before replacement with {}",
                                    __func__,
                                    cid.toString().value(),
                                    rootCID.toString().value() );
                    auto resolve_result = dagSyncer_->markResolved( cid );
                    if ( resolve_result.has_failure() )
                    {
                        logger_->error( "{}: error marking old Head CID {} as resolved", __func__, cid.toString().value() );
                    }
                }
                auto replace_result = heads_->Replace( cid, rootCID, rootPriority, topic );
                if ( replace_result.has_failure() )
                {
                    logger_->error( "{}: error replacing head {} with {}",
                                    __func__,
                                    cid.toString().value(),
                                    rootCID.toString().value() );
                }
            }
        }
        pendingHeadsByRootCID_.erase( it );
        rebroadcastCv_.notify_one();
    }
}
