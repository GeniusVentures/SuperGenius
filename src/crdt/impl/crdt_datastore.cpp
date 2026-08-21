#include <fmt/std.h>
#include "crdt/crdt_datastore.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include <storage/rocksdb/rocksdb.hpp>
#include <iostream>
#include "crdt/proto/bcast.pb.h"
#include "storage/database_error.hpp"
#include <google/protobuf/unknown_field_set.h>
#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>
#include <thread>
#include <utility>
#include <boost/format.hpp>

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
            [weakptr( std::weak_ptr<CrdtDatastore>(
                crdtInstance ) )]( const std::string &key, const base::Buffer &value, const std::string &cid )
            {
                if ( auto strong = weakptr.lock() )
                {
                    strong->PutElementsCallback( key, value, cid );
                }
            },
            [weakptr( std::weak_ptr<CrdtDatastore>( crdtInstance ) )]( const std::string &key, const std::string &cid )
            {
                if ( auto strong = weakptr.lock() )
                {
                    strong->DeleteElementsCallback( key, cid );
                }
            } );
        crdtInstance->dagWorkerJobListThreadRunning_ = true;
        crdtInstance->dagWorkers_.reserve( crdtInstance->numberOfDagWorkers );
        for ( int i = 0; i < crdtInstance->numberOfDagWorkers; ++i )
        {
            auto  dagWorker  = std::make_unique<DagWorker>();
            auto *worker_ptr = dagWorker.get();

            dagWorker->dagWorkerThreadRunning_ = true;
            dagWorker->dagWorkerFuture_        = std::async(
                std::launch::async,
                [weakptr( std::weak_ptr<CrdtDatastore>( crdtInstance ) ), worker_ptr]
                {
                    if ( auto self = weakptr.lock() )
                    {
                        std::lock_guard lock( self->workerThreadIdsMutex_ );
                        worker_ptr->threadId_ = std::this_thread::get_id();
                    }
                    auto dagThreadRunning = true;
                    while ( dagThreadRunning )
                    {
                        if ( auto self = weakptr.lock() )
                        {
                            if ( !self->ShouldContinueWorkerThread( *worker_ptr ) )
                            {
                                dagThreadRunning = false;
                                continue;
                            }

                            // Process jobs in priority order
                            if ( self->ProcessJobs( self->selfCreatedJobList_ ) )
                            {
                                continue;
                            }
                            if ( self->ProcessJobs( self->rootCIDJobList_ ) )
                            {
                                continue;
                            }
                            if ( self->SeedNextExternalRoot() )
                            {
                                continue;
                            }
                        }
                        else
                        {
                            dagThreadRunning = false;
                        }
                    }
                    if ( auto self = weakptr.lock() )
                    {
                        self->logger_->debug( "DAG worker thread finished at {}", std::this_thread::get_id() );
                    }
                } );
            crdtInstance->dagWorkers_.emplace_back( std::move( dagWorker ) );
        }
        return crdtInstance;
    }

    bool CrdtDatastore::ShouldContinueWorkerThread( DagWorker &dagWorker )
    {
        std::unique_lock lk( dagWorkerMutex_ );
        dagWorkerCv_.wait_for( lk,
                               threadSleepTimeInMilliseconds_,
                               [&]
                               {
                                   return closeStarted_ || !dagWorkerJobListThreadRunning_ ||
                                          !dagWorker.dagWorkerThreadRunning_ || !selfCreatedJobList_.empty() ||
                                          !rootCIDJobList_.empty() ||
                                          ( !activeRootCID_.has_value() && !pendingRootQueue_.empty() );
                               } );

        return !closeStarted_ && dagWorkerJobListThreadRunning_ && dagWorker.dagWorkerThreadRunning_;
    }

    bool CrdtDatastore::ProcessJobs( std::queue<RootCIDJob> &jobs )
    {
        std::unique_lock lk( dagWorkerMutex_ );
        if ( jobs.empty() )
        {
            return false;
        }

        RootCIDJob job_to_process = jobs.front();
        jobs.pop();
        lk.unlock();

        logger_->debug( "Processing job for CID {}", job_to_process.root_node_->getCID().toString().value() );

        auto process_res = ProcessJobIteration( job_to_process );
        if ( process_res.has_failure() )
        {
            HandleJobProcessingFailure( job_to_process );
        }
        else
        {
            HandleJobProcessingSuccess( job_to_process );
        }

        return true; // Processed a job
    }

    bool CrdtDatastore::SeedNextExternalRoot()
    {
        std::unique_lock lk( dagWorkerMutex_ );
        if ( activeRootCID_.has_value() || pendingRootQueue_.empty() )
        {
            return false;
        }

        CID next = pendingRootQueue_.front();
        pendingRootQueue_.pop();
        activeRootCID_ = next;
        lk.unlock();

        logger_->debug( "Seeding new external root CID {}", next.toString().value() );
        auto res = HandleRootCIDBlock( next );
        if ( res.has_failure() )
        {
            std::unique_lock lk2( dagWorkerMutex_ );
            activeRootCID_.reset();
            dagWorkerCv_.notify_all();
        }

        return true; // Seeded a root
    }

    void CrdtDatastore::HandleJobProcessingFailure( const RootCIDJob &job )
    {
        // Signal job failure
        {
            std::lock_guard lock_jobs( dagWorkerMutex_ );
            pending_jobs_[job.root_node_->getCID()] = JobStatus::FAILED;
        }

        const std::string_view jobType = job.created_by_self_ ? "SELF-CREATED" : "EXTERNAL";
        logger_->error( "{} JOB PROCESSING ERROR: Failed to process CID {}",
                        jobType,
                        job.root_node_->getCID().toString().value() );

        CleanupFailedJob( job );

        // Delete blocks
        (void)dagSyncer_->DeleteCIDBlock( job.root_node_->getCID() );
        if ( job.node_ && job.node_->getCID() != job.root_node_->getCID() )
        {
            (void)dagSyncer_->DeleteCIDBlock( job.node_->getCID() );
        }

        if ( !job.created_by_self_ )
        {
            std::lock_guard<std::mutex> g( pendingHeadsMutex_ );
            pendingHeadsByRootCID_.erase( job.root_node_->getCID() );
        }

        dagWorkerCv_.notify_all();
    }

    void CrdtDatastore::HandleJobProcessingSuccess( const RootCIDJob &job )
    {
        {
            // Mark self-created job as completed
            std::lock_guard lock_jobs( dagWorkerMutex_ );
            pending_jobs_[job.root_node_->getCID()] = JobStatus::COMPLETED;
        }
        dagWorkerCv_.notify_all();
        if ( job.created_by_self_ )
        {
            logger_->debug( "Successfully completed self-created job for CID {}",
                            job.root_node_->getCID().toString().value() );
        }
        // External jobs are handled in ProcessJobIteration when they complete
    }

    void CrdtDatastore::CleanupFailedJob( const RootCIDJob &job )
    {
        std::unique_lock lock( dagWorkerMutex_ );

        if ( job.created_by_self_ )
        {
            // Clean up self-created job queue
            std::queue<RootCIDJob> tmp;
            while ( !selfCreatedJobList_.empty() )
            {
                auto j = selfCreatedJobList_.front();
                selfCreatedJobList_.pop();
                if ( j.root_node_->getCID() != job.root_node_->getCID() )
                {
                    tmp.push( j );
                }
            }
            std::swap( selfCreatedJobList_, tmp );
        }
        else
        {
            // Clean up external job queue
            std::queue<RootCIDJob> tmp;
            while ( !rootCIDJobList_.empty() )
            {
                auto j = rootCIDJobList_.front();
                rootCIDJobList_.pop();
                if ( j.root_node_->getCID() != job.root_node_->getCID() )
                {
                    tmp.push( j );
                }
            }
            std::swap( rootCIDJobList_, tmp );

            // Reset activeRootCID for external jobs
            activeRootCID_.reset();
        }
    }

    void CrdtDatastore::Start()
    {
        StartCIDProcessing();
        StartRebroadcastHeads();
        started_ = true;
    }

    void CrdtDatastore::StartCIDProcessing()
    {
        if ( handleNextThreadRunning_ )
        {
            return;
        }
        if ( root_cid_sync_enabled_ )
        {
            return;
        }

        handleNextThreadRunning_ = true;
        // Starting HandleNext worker thread
        handleNextFuture_ = std::async(
            [weakptr{ weak_from_this() }]
            {
                auto threadRunning = true;
                bool thread_id_set = false;
                while ( threadRunning )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        if ( !thread_id_set )
                        {
                            std::lock_guard lock( self->workerThreadIdsMutex_ );
                            self->handleNextThreadId_ = std::this_thread::get_id();
                            thread_id_set             = true;
                        }

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

        root_cid_sync_enabled_ = true;
    }

    void CrdtDatastore::StartRebroadcastHeads()
    {
        if ( rebroadcastThreadRunning_ )
        {
            return;
        }
        if ( broadcast_enabled_ )
        {
            return;
        }

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

                {
                    std::lock_guard lock( self->workerThreadIdsMutex_ );
                    self->rebroadcastThreadId_ = std::this_thread::get_id();
                }

                const auto interval = std::chrono::milliseconds(
                    self->options_ ? self->options_->rebroadcastIntervalMilliseconds : 100 );
                std::unique_lock lock( self->rebroadcastMutex_ );

                while ( self->rebroadcastThreadRunning_ )
                {
                    lock.unlock();
                    self->RebroadcastHeads();
                    lock.lock();
                    self->rebroadcastCv_.wait_for( lock,
                                                   interval,
                                                   [&]
                                                   {
                                                       return !self->rebroadcastThreadRunning_ || self->closeStarted_ ||
                                                              !self->pendingBroadcastTopics_.empty();
                                                   } );
                }
            } );

        broadcast_enabled_ = true;
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
        work_journal_( CRDTWorkJournal::New( dataStore_ ) ),
        crdt_filter_( work_journal_, true ),
        crdt_cb_manager_( work_journal_ )
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
        CancelAndCloseNow();
    }

    void CrdtDatastore::CancelAndCloseNow()
    {
        bool expected = false;
        if ( !shutdown_started_.compare_exchange_strong( expected, true ) )
        {
            logger_->warn( "CancelAndCloseNow called but shutdown has already started" );
            return;
        }

        logger_->info(
            "CancelAndCloseNow: begin (pending_jobs={}, self_queue={}, root_queue={}, pending_roots={}, active_root={})",
            pending_jobs_.size(),
            selfCreatedJobList_.size(),
            rootCIDJobList_.size(),
            pendingRootQueue_.size(),
            activeRootCID_.has_value() );

        closeStarted_ = true;
        StopWorkerLoops();

        if ( IsCurrentThreadInternalWorker() )
        {
            logger_->error( "{}: CancelAndCloseNow called from CRDT worker thread; deferring waits to helper thread",
                            __func__ );
            auto keep_alive = shared_from_this();
            std::thread( [keep_alive = std::move( keep_alive )]() { keep_alive->WaitForWorkersToExit(); } ).detach();
            return;
        }

        WaitForWorkersToExit();

        started_ = false;
        logger_->info( "CancelAndCloseNow: CRDT workers stopped" );
        logger_->debug(
            "CancelAndCloseNow: end (pending_jobs={}, self_queue={}, root_queue={}, pending_roots={}, active_root={})",
            pending_jobs_.size(),
            selfCreatedJobList_.size(),
            rootCIDJobList_.size(),
            pendingRootQueue_.size(),
            activeRootCID_.has_value() );
    }

    void CrdtDatastore::StopWorkerLoops()
    {
        if ( dagSyncer_ )
        {
            dagSyncer_->Stop();
        }

        if ( handleNextThreadRunning_ )
        {
            handleNextThreadRunning_ = false;
        }

        if ( rebroadcastThreadRunning_ )
        {
            rebroadcastThreadRunning_ = false;
            rebroadcastCv_.notify_all();
        }

        if ( dagWorkerJobListThreadRunning_ )
        {
            dagWorkerJobListThreadRunning_ = false;
            for ( auto &dagWorker : dagWorkers_ )
            {
                dagWorker->dagWorkerThreadRunning_ = false;
            }
        }

        dagWorkerCv_.notify_all();
    }

    bool CrdtDatastore::IsCurrentThreadInternalWorker() const
    {
        const auto      caller_id = std::this_thread::get_id();
        std::lock_guard lock( workerThreadIdsMutex_ );

        if ( caller_id == handleNextThreadId_ || caller_id == rebroadcastThreadId_ )
        {
            return true;
        }

        for ( const auto &dagWorker : dagWorkers_ )
        {
            if ( caller_id == dagWorker->threadId_ )
            {
                return true;
            }
        }
        return false;
    }

    void CrdtDatastore::WaitForWorkersToExit()
    {
        logger_->debug( "WaitForWorkersToExit: waiting for {} DAG worker(s)", dagWorkers_.size() );
        std::size_t worker_index = 0;
        for ( auto &dagWorker : dagWorkers_ )
        {
            if ( dagWorker->dagWorkerFuture_.valid() )
            {
                logger_->debug( "WaitForWorkersToExit: waiting for DAG worker {} thread_id={}",
                                worker_index,
                                dagWorker->threadId_ );
                dagWorker->dagWorkerFuture_.wait();
                logger_->debug( "WaitForWorkersToExit: DAG worker {} finished", worker_index );
            }
            ++worker_index;
        }

        if ( handleNextFuture_.valid() )
        {
            logger_->debug( "WaitForWorkersToExit: waiting for HandleNext thread_id={}", handleNextThreadId_ );
            handleNextFuture_.wait();
            logger_->debug( "WaitForWorkersToExit: HandleNext finished" );
        }

        if ( rebroadcastFuture_.valid() )
        {
            logger_->debug( "WaitForWorkersToExit: waiting for rebroadcast thread_id={}", rebroadcastThreadId_ );
            rebroadcastFuture_.wait();
            logger_->debug( "WaitForWorkersToExit: rebroadcast finished" );
        }

        {
            std::lock_guard        lock( dagWorkerMutex_ );
            std::queue<RootCIDJob> empty1, empty2;
            std::swap( rootCIDJobList_, empty1 );
            std::swap( selfCreatedJobList_, empty2 );
            pending_jobs_.clear();
        }

        {
            std::lock_guard lock( workerThreadIdsMutex_ );
            handleNextThreadId_  = {};
            rebroadcastThreadId_ = {};
            for ( auto &dagWorker : dagWorkers_ )
            {
                dagWorker->threadId_ = {};
            }
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

        for ( const auto &bCastHeadCID : decodeResult.value() )
        {
            logger_->trace( "{}: Received CID {}", __func__, bCastHeadCID.toString().value() );
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
                // If the CID request was already triggered but node didn't finish processing
                bool retry_failed = false;
                {
                    std::lock_guard lock( dagWorkerMutex_ );
                    auto            it = pending_jobs_.find( bCastHeadCID );
                    if ( it != pending_jobs_.end() && it->second == JobStatus::FAILED )
                    {
                        pending_jobs_.erase( it );
                        retry_failed = true;
                    }
                }

                if ( retry_failed )
                {
                    logger_->warn( "{}: Clearing failed job for CID {}, allowing retry",
                                   __func__,
                                   bCastHeadCID.toString().value() );
                    (void)dagSyncer_->DeleteCIDBlock( bCastHeadCID );
                }
                else
                {
                    logger_->trace( "{}: Processing block {} on graphsync", __func__, bCastHeadCID.toString().value() );
                    continue;
                }
            }

            if ( IsRootCIDPendingOrActive( bCastHeadCID ) )
            {
                logger_->trace( "{}: Root CID {} already pending/active", __func__, bCastHeadCID.toString().value() );
                continue;
            }

            if ( EnqueueRootCID( bCastHeadCID ) )
            {
                logger_->debug( "{}: Queueing processing for block {}", __func__, bCastHeadCID.toString().value() );
                dagWorkerCv_.notify_one(); // wake a worker to possibly seed the next root
            }
            else
            {
                logger_->trace( "{}: Root CID {} could not be enqueued (already pending)",
                                __func__,
                                bCastHeadCID.toString().value() );
            }
        }
    }

    outcome::result<void> CrdtDatastore::HandleRootCIDBlock( const CID &aCid )
    {
        logger_->debug( "{}: begin for root CID {}", __func__, aCid.toString().value() );
        auto root_job_result = CreateRootJob( aCid );
        if ( root_job_result.has_failure() )
        {
            MarkJobFailed( aCid );
            return root_job_result.as_failure();
        }
        logger_->debug( "{}: root job created for root CID {}", __func__, aCid.toString().value() );

        auto links_result = GetLinksToFetch( root_job_result.value() );
        if ( links_result.has_failure() )
        {
            MarkJobFailed( aCid );
            return links_result.as_failure();
        }
        logger_->debug( "{}: {} link(s) discovered for root CID {}",
                        __func__,
                        links_result.value().size(),
                        aCid.toString().value() );

        auto fetch_result = FetchNodes( root_job_result.value(), links_result.value() );
        if ( fetch_result.has_failure() )
        {
            MarkJobFailed( aCid );
            return fetch_result.as_failure();
        }
        logger_->debug( "{}: fetch complete for root CID {}", __func__, aCid.toString().value() );
        return outcome::success();
    }

    outcome::result<CrdtDatastore::RootCIDJob> CrdtDatastore::CreateRootJob( const CID &aRootCID )
    {
        logger_->debug( "{}: Creating the Root Job for CID {}", __func__, aRootCID.toString().value() );
        dagSyncer_->InitCIDBlock( aRootCID );
        logger_->debug( "{}: InitCIDBlock complete for root CID {}", __func__, aRootCID.toString().value() );
        BOOST_OUTCOME_TRY( auto root_node, dagSyncer_->getNode( aRootCID ) );

        logger_->debug( "{}: Root Job created for CID {}", __func__, aRootCID.toString().value() );

        RootCIDJob rootJob{ root_node, root_node, false };

        return rootJob;
    }

    outcome::result<std::set<CID>> CrdtDatastore::GetLinksToFetch( const RootCIDJob &job )
    {
        std::set<CID> cids_to_fetch;
        auto          node_to_process = job.node_;
        if ( node_to_process == nullptr )
        {
            node_to_process = job.root_node_;
        }

        std::set<std::string> topics_to_update_cid = node_to_process->getDestinations();

        for ( auto &topic : topics_to_update_cid )
        {
            logger_->debug( "{}: Recording head to add: {}, {}",
                            __func__,
                            job.root_node_->getCID().toString().value(),
                            topic );
            std::lock_guard<std::mutex> lock( pendingHeadsMutex_ );
            pendingHeadsByRootCID_[job.root_node_->getCID()].emplace( job.root_node_->getCID(), topic );
        }
        if ( !node_to_process->getLinks().empty() )
        {
            logger_->debug( "{}: Checking links for CID {}", __func__, node_to_process->getCID().toString().value() );
            for ( auto &topic : topics_to_update_cid )
            {
                logger_->trace( "{}: Verifying topic {}", __func__, topic );

                auto [links_to_fetch, known_cids] = dagSyncer_->TraverseCIDsLinks( *node_to_process, topic, {} );

                for ( const auto &[cid, _dontcare] : known_cids )
                {
                    if ( logger_->level() <= spdlog::level::trace )
                    {
                        logger_->trace( "{}: known cid: {}, {}", __func__, cid.toString().value(), _dontcare );
                    }
                    if ( heads_->IsHead( cid, _dontcare ) )
                    {
                        if ( logger_->level() <= spdlog::level::debug )
                        {
                            logger_->debug( "{}: Recording replacement of {} with {} on topic {} ({}) ",
                                            __func__,
                                            cid.toString().value(),
                                            job.root_node_->getCID().toString().value(),
                                            topic,
                                            _dontcare );
                        }
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
                    logger_->debug( "{}: Not known cid: {}, {}", __func__, cid.toString().value(), link_name );
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
            if ( logger_->level() <= spdlog::level::debug )
            {
                logger_->debug( "{}: Trying to fetch node {} from Root Job {} ",
                                __func__,
                                cid.toString().value(),
                                aRootJob.root_node_->getCID().toString().value() );
            }

            dagSyncer_->InitCIDBlock( cid );
            logger_->debug( "{}: InitCIDBlock complete for node {} from Root Job {}",
                            __func__,
                            cid.toString().value(),
                            aRootJob.root_node_->getCID().toString().value() );
            logger_->debug( "{}: getNode begin for node {} from Root Job {}",
                            __func__,
                            cid.toString().value(),
                            aRootJob.root_node_->getCID().toString().value() );
            BOOST_OUTCOME_TRY( auto node, dagSyncer_->getNode( cid ) );
            logger_->debug( "{}: getNode complete for node {} from Root Job {}",
                            __func__,
                            cid.toString().value(),
                            aRootJob.root_node_->getCID().toString().value() );

            RootCIDJob newRootJob;

            newRootJob.root_node_       = aRootJob.root_node_;
            newRootJob.node_            = node;
            newRootJob.created_by_self_ = false;

            if ( logger_->level() <= spdlog::level::debug )
            {
                logger_->debug( "{}: Got the node {} sending to workers. Root Job {} ",
                                __func__,
                                cid.toString().value(),
                                aRootJob.root_node_->getCID().toString().value() );
            }
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
        BOOST_OUTCOME_TRY( auto cid_string, node_cid.toString() );
        logger_->debug( "{}: Merging node {} On CRDT", __func__, cid_string );
        BOOST_OUTCOME_TRY( set_->Merge( aDelta, cid_string ) );
        return outcome::success();
    }

    outcome::result<void> CrdtDatastore::ProcessJobIteration( const RootCIDJob &job_to_process )
    {
        logger_->debug( "{}: Starting to process Root CID", __func__ );

        BOOST_OUTCOME_TRY( auto root_cid_string, job_to_process.root_node_->getCID().toString() );
        logger_->debug( "{}: Processing Root CID job {}", __func__, root_cid_string );

        auto node_to_process = job_to_process.node_;
        bool is_root         = false;
        if ( node_to_process == nullptr )
        {
            node_to_process = job_to_process.root_node_;
            is_root         = true;
        }

        BOOST_OUTCOME_TRY( auto cid_string, node_to_process->getCID().toString() );

        BOOST_OUTCOME_TRY( auto delta, GetDeltaFromNode( *node_to_process, job_to_process.created_by_self_ ) );

        logger_->debug( "{}: Merging Deltas from {}", __func__, cid_string );

        BOOST_OUTCOME_TRY( MergeDataFromDelta( node_to_process->getCID(), delta ) );
        logger_->debug( "{}: Merge complete for {}", __func__, cid_string );

        logger_->debug( "{}: Recording block on DAG Syncher {}", __func__, cid_string );
        BOOST_OUTCOME_TRY( dagSyncer_->addNode( node_to_process ) );
        logger_->debug( "{}: addNode complete for {}", __func__, cid_string );

        (void)dagSyncer_->DeleteCIDBlock( node_to_process->getCID() );
        logger_->debug( "{}: DeleteCIDBlock complete for {}", __func__, cid_string );

        BOOST_OUTCOME_TRY( auto links, GetLinksToFetch( job_to_process ) );
        logger_->debug( "{}: GetLinksToFetch complete for {}, links={}", __func__, cid_string, links.size() );
        const bool should_fetch_links = !job_to_process.created_by_self_ && !links.empty();

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
        else if ( should_fetch_links )
        {
            logger_->debug( "{}: Fetching {} links for Root job: {}", __func__, links.size(), root_cid_string );
            BOOST_OUTCOME_TRY( FetchNodes( job_to_process, links ) );
            logger_->debug( "{}: Nodes fetched for Root job: {}", __func__, root_cid_string );
        }
        else if ( is_root )
        {
            if ( job_to_process.created_by_self_ && !links.empty() )
            {
                logger_->error( "{}: Self-created job {}, skipping fetch of {} links and finalizing heads",
                                __func__,
                                root_cid_string,
                                links.size() );
            }
            logger_->debug( "{}: Root finalized: {}, Updating CRDT Heads", __func__, root_cid_string );
            UpdateCRDTHeads( job_to_process.root_node_->getCID(),
                             delta.priority(),
                             job_to_process.created_by_self_ || has_full_node_topic_ );
            logger_->debug( "{}: UpdateCRDTHeads complete for {}", __func__, root_cid_string );
            {
                std::unique_lock lk( dagWorkerMutex_ );
                activeRootCID_.reset(); // this root fully done
            }
            dagWorkerCv_.notify_all(); // let one worker seed the next root

            // Signal job completion after UpdateCRDTHeads is done
            {
                std::lock_guard lock( dagWorkerMutex_ );
                auto            it = pending_jobs_.find( job_to_process.root_node_->getCID() );
                if ( it != pending_jobs_.end() )
                {
                    it->second = JobStatus::COMPLETED;
                }
            }
            dagWorkerCv_.notify_all();
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
            auto encodedHead = bcastData.add_heads();

            // Check cache first to avoid expensive base58 encoding
            std::string cid_string;
            {
                std::lock_guard<std::mutex> lock( cid_string_cache_mutex_ );
                auto                        it = cid_string_cache_.find( head );
                if ( it != cid_string_cache_.end() )
                {
                    cid_string = it->second;
                    logger_->trace( "CID string cache hit for CID {}", cid_string );
                }
            }

            // Cache miss - compute and cache the string
            if ( cid_string.empty() )
            {
                auto strHeadResult = head.toString();
                if ( !strHeadResult.has_failure() )
                {
                    cid_string = strHeadResult.value();
                    std::lock_guard<std::mutex> lock( cid_string_cache_mutex_ );
                    cid_string_cache_[head] = cid_string;
                    logger_->trace( "CID string cache miss - cached CID {}", cid_string );
                }
                else
                {
                    continue; // Skip this CID if conversion fails
                }
            }

            encodedHead->set_cid( cid_string );
        }

        Buffer outputBuffer;
        outputBuffer.put( bcastData.SerializeAsString() );
        return outputBuffer;
    }

    outcome::result<CrdtDatastore::Buffer> CrdtDatastore::EncodeBroadcastStatic( const std::set<CID> &heads )
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
        std::unordered_set<std::string> pending_topics;
        {
            std::lock_guard lock( rebroadcastMutex_ );
            pending_topics.swap( pendingBroadcastTopics_ );
        }

        std::unordered_set<std::string> topics_to_broadcast = GetTopicNames();
        topics_to_broadcast.insert( pending_topics.begin(), pending_topics.end() );

        if ( topics_to_broadcast.empty() )
        {
            return;
        }

        auto getListResult = heads_->GetList( topics_to_broadcast );
        if ( getListResult.has_failure() )
        {
            logger_->error( "RebroadcastHeads: Failed to get list of heads (error code {})", getListResult.error() );
            return;
        }
        auto [head_map, maxHeight] = getListResult.value();

        // Get PeerInfo once before the loop to avoid repeated calls
        boost::optional<libp2p::peer::PeerInfo> peerInfo;
        if ( broadcaster_ )
        {
            // Cast the broadcaster's DAG syncer to GraphsyncDAGSyncer to get PeerInfo
            auto dagSyncerPtr = std::static_pointer_cast<GraphsyncDAGSyncer>( broadcaster_->GetDagSyncer() );
            if ( dagSyncerPtr )
            {
                auto peerInfoResult = dagSyncerPtr->GetPeerInfo();
                if ( peerInfoResult.has_value() )
                {
                    peerInfo = peerInfoResult.value();
                }
                else
                {
                    logger_->warn( "RebroadcastHeads: Failed to get peer info, broadcasts will retry per-call" );
                }
            }
        }

        for ( const auto &[topic_name, cid_set] : head_map ) // Changed from cid_map to head_map
        {
            auto broadcastResult = Broadcast( cid_set, topic_name, peerInfo );
            if ( broadcastResult.has_failure() )
            {
                logger_->error( "RebroadcastHeads: Broadcast failed" );
            }
            else
            {
                logger_->trace( "RebroadcastHeads: Broadcasted CIDs to topic {} ", topic_name );
                for ( const auto &cid : cid_set )
                {
                    if ( logger_->level() == spdlog::level::trace )
                    {
                        logger_->trace( "RebroadcastHeads: CID {} ", cid.toString().value() );
                    }
                }
            }
        }
    }

    outcome::result<void> CrdtDatastore::BroadcastHeadsForTopics( const std::set<std::string> &topics )
    {
        if ( !broadcast_enabled_ )
        {
            logger_->error( "{}: broadcast suppressed", __func__ );
            return outcome::success();
        }

        if ( topics.empty() )
        {
            logger_->debug( "BroadcastHeadsForTopics: No topics requested" );
            return outcome::success();
        }

        auto head_list_result = heads_->GetList();
        if ( head_list_result.has_error() )
        {
            logger_->error( "BroadcastHeadsForTopics: Failed to get head list" );
            return outcome::failure( head_list_result.error() );
        }

        auto [head_map, maxHeight] = head_list_result.value();

        // Get PeerInfo once to reuse across broadcasts
        boost::optional<libp2p::peer::PeerInfo> peerInfo;
        {
            auto dagSyncerPtr = std::static_pointer_cast<GraphsyncDAGSyncer>( broadcaster_->GetDagSyncer() );
            if ( dagSyncerPtr )
            {
                auto peerInfoResult = dagSyncerPtr->GetPeerInfo();
                if ( peerInfoResult.has_value() )
                {
                    peerInfo = peerInfoResult.value();
                }
                else
                {
                    logger_->warn( "BroadcastHeadsForTopics: Failed to get peer info, broadcasts will retry per-call" );
                }
            }
        }

        // Broadcast heads for each requested topic
        for ( const auto &topic_name : topics )
        {
            auto it = head_map.find( topic_name );
            if ( it == head_map.end() || it->second.empty() )
            {
                logger_->debug( "BroadcastHeadsForTopics: No heads to broadcast for topic {}", topic_name );
                continue;
            }

            auto broadcastResult = Broadcast( it->second, topic_name, peerInfo );
            if ( broadcastResult.has_failure() )
            {
                logger_->error( "BroadcastHeadsForTopics: Broadcast failed for topic {}", topic_name );
            }
            else
            {
                logger_->debug( "BroadcastHeadsForTopics: Broadcasted {} heads for topic {}",
                                it->second.size(),
                                topic_name );
            }
        }

        return outcome::success();
    }

    outcome::result<CrdtDatastore::Buffer> CrdtDatastore::GetKey( const HierarchicalKey &aKey ) const
    {
        return set_->GetElement( aKey.GetKey() );
    }

    std::string CrdtDatastore::GetKeysPrefix() const
    {
        return set_->KeysKey( "" ).GetKey();
    }

    std::string CrdtDatastore::GetValueSuffix()
    {
        return '/' + CrdtSet::GetValueSuffix();
    }

    outcome::result<CrdtDatastore::QueryResult> CrdtDatastore::QueryKeyValues( std::string_view aPrefix ) const
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
            return outcome::failure( storage::DatabaseError::UNITIALIZED );
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

    outcome::result<CID> CrdtDatastore::PutKey( const HierarchicalKey                 &aKey,
                                                const Buffer                          &aValue,
                                                const std::unordered_set<std::string> &topics )
    {
        auto deltaResult = CreateDeltaToAdd( aKey.GetKey(), std::string( aValue.toString() ) );
        if ( deltaResult.has_failure() )
        {
            return outcome::failure( deltaResult.error() );
        }

        return Publish( deltaResult.value(), topics );
    }

    outcome::result<CID> CrdtDatastore::PutConvergentImmutableKey(
        const HierarchicalKey                 &aKey,
        const Buffer                          &aValue,
        const std::unordered_set<std::string> &topics )
    {
        auto deltaResult = CreateDeltaToAdd( aKey.GetKey(), std::string( aValue.toString() ) );
        if ( deltaResult.has_failure() )
        {
            return outcome::failure( deltaResult.error() );
        }

        deltaResult.value()->set_priority( CrdtSet::ConvergentImmutablePriority );
        return Publish( deltaResult.value(), topics );
    }

    outcome::result<CID> CrdtDatastore::DeleteKey( const HierarchicalKey                 &aKey,
                                                   const std::unordered_set<std::string> &topics )
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

        return Publish( deltaResult.value(), topics );
    }

    outcome::result<CID> CrdtDatastore::Publish( const std::shared_ptr<Delta>          &aDelta,
                                                 const std::unordered_set<std::string> &topics )
    {
        BOOST_OUTCOME_TRY( auto node, CreateDAGNode( aDelta, topics ) );
        BOOST_OUTCOME_TRY( auto newCID, AddDAGNode( node ) );
        return newCID;
    }

    outcome::result<void> CrdtDatastore::Broadcast( const std::set<CID>                    &cids,
                                                    const std::string                      &topic,
                                                    boost::optional<libp2p::peer::PeerInfo> peerInfo )
    {
        if ( !broadcast_enabled_ )
        {
            logger_->error( "{}: No broadcaster, Failed to broadcast", __func__ );
            return outcome::success();
        }

        if ( !broadcaster_ )
        {
            logger_->error( "{}: No broadcaster, Failed to broadcast", __func__ );
            return outcome::failure( boost::system::error_code{} );
        }
        if ( cids.empty() )
        {
            logger_->error( "{}: Cids Empty, Failed to broadcast", __func__ );
            return outcome::success();
        }
        auto encodedBufferResult = EncodeBroadcast( cids );
        if ( encodedBufferResult.has_failure() )
        {
            logger_->error( "{}: Encoding failed, Failed to broadcast", __func__ );
            return outcome::failure( encodedBufferResult.error() );
        }

        auto bcastResult = broadcaster_->Broadcast( encodedBufferResult.value(), topic, peerInfo );
        if ( bcastResult.has_failure() )
        {
            logger_->error( "{}: Broadcaster failed to broadcast", __func__ );
            return outcome::failure( bcastResult.error() );
        }
        return outcome::success();
    }

    outcome::result<std::shared_ptr<CrdtDatastore::IPLDNode>> CrdtDatastore::CreateIPLDNode(
        const std::vector<std::pair<CID, std::string>> &aHeads,
        const std::shared_ptr<Delta>                   &aDelta,
        const std::unordered_set<std::string>          &topics ) const
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
        //Log expensive toString only if trace enabled
        if ( logger_->level() == spdlog::level::trace )
        {
            logger_->trace( "{}: added destination for block {{ cid=\"{}\" }}",
                            __func__,
                            node->getCID().toString().value() );
        }

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
            ipfs_lite::ipld::IPLDLink link( head, topic, cidByte.value().size() );
            node->addLink( link );
            //Log expensive toString only if trace enabled
            if ( logger_->level() == spdlog::level::trace )
            {
                logger_->trace( "{}: added link {{ cid=\"{}\", name=\"{}\", size={} }}",
                                __func__,
                                link.getCID().toString().value(),
                                link.getName(),
                                link.getSize() );
            }
        }

        return node;
    }

    outcome::result<std::shared_ptr<CrdtDatastore::IPLDNode>> CrdtDatastore::CreateDAGNode(
        const std::shared_ptr<Delta>          &aDelta,
        const std::unordered_set<std::string> &topics )
    {
        BOOST_OUTCOME_TRY( auto head_list, heads_->GetList( topics ) );
        auto [head_map, height] = head_list;

        height = height + 1; // This implies our minimum height is 1
        if ( aDelta->priority() != CrdtSet::ConvergentImmutablePriority )
        {
            aDelta->set_priority( height );
        }

        std::vector<std::pair<CID, std::string>> headsWithTopics;

        for ( const auto &[topic_name, cid_set] : head_map )
        {
            for ( const auto &cid : cid_set )
            {
                headsWithTopics.emplace_back( cid, topic_name );
            }
        }

        BOOST_OUTCOME_TRY( auto node, CreateIPLDNode( headsWithTopics, aDelta, topics ) );

        //Log expensive toString only if trace enabled
        if ( logger_->level() == spdlog::level::debug )
        {
            logger_->debug( "{}: Created Node to insert in DAG: {} (instance {})",
                            __func__,
                            node->getCID().toString().value(),
                            reinterpret_cast<uint64_t>( this ) );
        }
        return node;
    }

    outcome::result<CID> CrdtDatastore::AddDAGNode( const std::shared_ptr<CrdtDatastore::IPLDNode> &node )
    {
        if ( closeStarted_ )
        {
            logger_->warn( "{}: datastore is closing, refusing AddDAGNode", __func__ );
            return outcome::failure( Error::NODE_CREATION );
        }

        RootCIDJob rootJob{ nullptr, node, true };

        {
            MarkJobPending( node->getCID() );
            std::unique_lock lock( dagWorkerMutex_ );
            selfCreatedJobList_.push( rootJob ); // Use high-priority self-created queue
            if ( logger_->level() == spdlog::level::trace )
            {
                logger_->trace(
                    "AddDAGNode: Added SELF-CREATED job for CID {}, self-queue size: {}, external-queue size: {}",
                    node->getCID().toString().value(),
                    selfCreatedJobList_.size(),
                    rootCIDJobList_.size() );
            }
        }

        // Notify all workers to ensure immediate processing
        dagWorkerCv_.notify_all();

        return WaitForJob( node->getCID() );
    }

    outcome::result<CID> CrdtDatastore::WaitForJob( const CID &cid )
    {
        auto cid_string_result = cid.toString();
        logger_->debug( "WaitForJob: Starting to wait for CID {} completion", cid_string_result.value() );

        auto timeout_duration = std::chrono::minutes( 20 );
        auto start_time       = std::chrono::steady_clock::now();
        auto deadline         = start_time + timeout_duration;
        auto next_log_time    = start_time + std::chrono::seconds( 30 );

        std::unique_lock lock( dagWorkerMutex_ );
        while ( std::chrono::steady_clock::now() < deadline )
        {
            if ( closeStarted_ )
            {
                pending_jobs_.erase( cid );
                logger_->warn( "WaitForJob: Aborting wait for CID {} due to datastore shutdown",
                               cid_string_result.value() );
                return outcome::failure( Error::NODE_CREATION );
            }

            auto it = pending_jobs_.find( cid );
            if ( it != pending_jobs_.end() )
            {
                if ( it->second == JobStatus::COMPLETED )
                {
                    pending_jobs_.erase( it );
                    logger_->debug( "WaitForJob: CID {} completed successfully", cid_string_result.value() );
                    return cid;
                }
                if ( it->second == JobStatus::FAILED )
                {
                    pending_jobs_.erase( it );
                    logger_->error( "WaitForJob: CID {} processing failed", cid_string_result.value() );
                    return outcome::failure( Error::NODE_CREATION );
                }
            }
            else
            {
                logger_->error( "WaitForJob: CID {} not found in pending jobs", cid_string_result.value() );
                return outcome::failure( Error::NODE_CREATION );
            }

            auto current_time = std::chrono::steady_clock::now();

            // Log progress every 30 seconds
            if ( current_time >= next_log_time )
            {
                auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>( current_time - start_time )
                                           .count();
                logger_->info( "WaitForJob: Still waiting for CID {} (elapsed: {}s)",
                               cid_string_result.value(),
                               elapsed_seconds );
                next_log_time = current_time + std::chrono::seconds( 30 );
                continue;
            }

            const auto wait_until = next_log_time < deadline ? next_log_time : deadline;
            dagWorkerCv_.wait_until( lock,
                                     wait_until,
                                     [&]
                                     {
                                         if ( closeStarted_ )
                                         {
                                             return true;
                                         }
                                         const auto it = pending_jobs_.find( cid );
                                         return it == pending_jobs_.end() || it->second != JobStatus::PENDING;
                                     } );
        }

        // Timeout reached
        logger_->error( "WaitForJob: Timeout (20 minutes) waiting for CID {} - size of the rootCIDJobList_: {}",
                        cid_string_result.value(),
                        rootCIDJobList_.size() );
        // Clean up the pending job
        pending_jobs_.erase( cid );
        return outcome::failure( Error::NODE_CREATION );
    }

    void CrdtDatastore::MarkJobPending( const CID &cid )
    {
        std::lock_guard lock( dagWorkerMutex_ );
        auto            it = pending_jobs_.find( cid );
        if ( it == pending_jobs_.end() || it->second != JobStatus::COMPLETED )
        {
            pending_jobs_[cid] = JobStatus::PENDING;
        }
    }

    void CrdtDatastore::MarkJobFailed( const CID &cid )
    {
        {
            std::lock_guard lock( dagWorkerMutex_ );
            pending_jobs_[cid] = JobStatus::FAILED;
        }
        dagWorkerCv_.notify_all();
    }

    outcome::result<CrdtDatastore::JobStatus> CrdtDatastore::GetJobStatus( const CID &cid )
    {
        auto has_block = dagSyncer_->HasBlock( cid );
        if ( has_block.has_value() && has_block.value() )
        {
            return JobStatus::COMPLETED;
        }

        std::lock_guard lock( dagWorkerMutex_ );
        auto            it = pending_jobs_.find( cid );
        if ( it != pending_jobs_.end() )
        {
            return it->second;
        }
        return outcome::failure( boost::system::error_code{} );
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
            BOOST_OUTCOME_TRY( PrintDAGRec( link.get().getCID(), aDepth + 1, aSet ) );
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

    void CrdtDatastore::UnregisterElementFilter( const std::string &pattern )
    {
        crdt_filter_.UnregisterElementFilter( pattern );
    }

    void CrdtDatastore::UnregisterNewElementCallback( const std::string &pattern )
    {
        crdt_cb_manager_.UnregisterNewDataCallback( pattern );
    }

    void CrdtDatastore::UnregisterDeletedElementCallback( const std::string &pattern )
    {
        crdt_cb_manager_.UnregisterDeletedDataCallback( pattern );
    }

    void CrdtDatastore::PutElementsCallback( const std::string &key, const Buffer &value, const std::string &cid )
    {
        crdt_cb_manager_.PutDataCallback( key, value, cid );
    }

    void CrdtDatastore::DeleteElementsCallback( const std::string &key, const std::string &cid )
    {
        crdt_cb_manager_.DeleteDataCallback( key, cid );
    }

    std::shared_ptr<CRDTWorkJournal> CrdtDatastore::GetWorkJournal() const
    {
        return work_journal_;
    }

    void CrdtDatastore::UpdateCRDTHeads( const CID &rootCID, uint64_t rootPriority, bool add_topics_to_broadcast )
    {
        std::lock_guard<std::mutex> lock( pendingHeadsMutex_ );
        auto                        it = pendingHeadsByRootCID_.find( rootCID );
        if ( it == pendingHeadsByRootCID_.end() )
        {
            logger_->error( "{}: Error, untracked head {}", __func__, rootCID.toString().value() );
            return;
        }
        std::set<std::string> updated_topics;
        for ( const auto &[cid, topic] : it->second )
        {
            if ( cid == rootCID )
            {
                auto resolve_result = dagSyncer_->markResolved( cid );
                if ( resolve_result.has_failure() )
                {
                    if ( logger_->level() <= spdlog::level::err )
                    {
                        logger_->error( "{}: error marking Root CID {} as resolved", __func__, cid.toString().value() );
                    }
                }
                auto add_result = heads_->Add( rootCID, rootPriority, topic );
                if ( add_result.has_failure() )
                {
                    if ( logger_->level() <= spdlog::level::err )
                    {
                        logger_->error( "{}: error adding head {}", __func__, rootCID.toString().value() );
                    }
                }
                updated_topics.insert( topic );
                if ( logger_->level() <= spdlog::level::debug )
                {
                    logger_->debug( "{}: Marking Head CID {} as resolved", __func__, rootCID.toString().value() );
                }
            }
            else
            {
                auto is_resolved_result = dagSyncer_->isResolved( cid );
                if ( is_resolved_result.has_failure() )
                {
                    if ( logger_->level() <= spdlog::level::err )
                    {
                        logger_->error( "{}: error checking if CID {} IS resolved", __func__, cid.toString().value() );
                    }
                    continue;
                }
                if ( !is_resolved_result.value() )
                {
                    //Log expensive toString only if trace enabled
                    if ( logger_->level() == spdlog::level::trace )
                    {
                        logger_->trace( "{}: Previous Head {} not resolved before replacement with {}",
                                        __func__,
                                        cid.toString().value(),
                                        rootCID.toString().value() );
                    }
                    auto resolve_result = dagSyncer_->markResolved( cid );
                    if ( resolve_result.has_failure() && logger_->level() <= spdlog::level::err )
                    {
                        logger_->error( "{}: error marking old Head CID {} as resolved",
                                        __func__,
                                        cid.toString().value() );
                    }
                }
                auto resolve_result = dagSyncer_->markResolved( rootCID );
                if ( resolve_result.has_failure() && logger_->level() <= spdlog::level::err )
                {
                    logger_->error( "{}: error marking new Head CID {} as resolved", __func__, cid.toString().value() );
                }
                auto replace_result = heads_->Replace( cid, rootCID, rootPriority, topic );
                if ( replace_result.has_failure() && logger_->level() <= spdlog::level::err )
                {
                    logger_->error( "{}: error replacing head {} with {}",
                                    __func__,
                                    cid.toString().value(),
                                    rootCID.toString().value() );
                }
                updated_topics.insert( topic );
            }
        }
        if ( add_topics_to_broadcast )
        {
            std::lock_guard<std::mutex> lock( rebroadcastMutex_ );
            pendingBroadcastTopics_.insert( updated_topics.begin(), updated_topics.end() );
        }

        rebroadcastCv_.notify_one();
    }

    bool CrdtDatastore::IsRootCIDPendingOrActive( const CID &cid )
    {
        std::lock_guard lk( dagWorkerMutex_ );
        return IsRootCIDPendingOrActiveLocked( cid );
    }

    bool CrdtDatastore::IsRootCIDPendingOrActiveLocked( const CID &cid ) const
    {
        if ( activeRootCID_.has_value() && activeRootCID_.value() == cid )
        {
            return true;
        }

        auto queue_copy = pendingRootQueue_;
        while ( !queue_copy.empty() )
        {
            if ( queue_copy.front() == cid )
            {
                return true;
            }
            queue_copy.pop();
        }

        return false;
    }

    bool CrdtDatastore::EnqueueRootCID( const CID &cid )
    {
        std::unique_lock lk( dagWorkerMutex_ );
        if ( IsRootCIDPendingOrActiveLocked( cid ) )
        {
            return false;
        }

        pendingRootQueue_.push( cid );
        auto it = pending_jobs_.find( cid );
        if ( it == pending_jobs_.end() || it->second != JobStatus::COMPLETED )
        {
            pending_jobs_[cid] = JobStatus::PENDING;
        }
        return true;
    }

    outcome::result<CrdtHeads::CRDTListResult> CrdtDatastore::GetHeadList()
    {
        return heads_->GetList();
    }

    outcome::result<void> CrdtDatastore::RemoveHead( const CID &aCid, const std::string &topic )
    {
        return heads_->Remove( aCid, topic );
    }

    outcome::result<uint64_t> CrdtDatastore::GetHeadHeight( const CID &aCid, const std::string &topic )
    {
        return heads_->GetHeadHeight( aCid, topic );
    }

    outcome::result<void> CrdtDatastore::AddHead( const CID &aCid, const std::string &topic, uint64_t priority )
    {
        return heads_->Add( aCid, priority, topic );
    }

    void CrdtDatastore::AddTopicName( const std::string &topic )
    {
        if ( topic == "SuperGNUSNode.TestNet.FullNode" )
        {
            has_full_node_topic_ = true;
        }
        std::lock_guard lock( topicNamesMutex_ );
        topicNames_.emplace( topic );
    }

    std::unordered_set<std::string> CrdtDatastore::GetTopicNames() const
    {
        std::lock_guard lock( topicNamesMutex_ );
        return topicNames_;
    }

    outcome::result<std::vector<std::pair<std::string, base::Buffer>>> CrdtDatastore::GetILPDNodeContent(
        const std::string &cid_string )
    {
        BOOST_OUTCOME_TRY( auto cid, CID::fromString( cid_string ) );

        BOOST_OUTCOME_TRY( auto node, dagSyncer_->GetNodeWithoutRequest( cid ) );

        //TODO - Check if filtering is needed here. Currently not filtering.
        BOOST_OUTCOME_TRY( auto delta, GetDeltaFromNode( *node, true ) );

        //TODO - Maybe check tombstones, right now just grabbing elements.
        std::vector elements( delta.elements().begin(), delta.elements().end() );

        std::vector<std::pair<std::string, base::Buffer>> result;
        for ( const auto &elem : elements )
        {
            Buffer valueBuffer;
            valueBuffer.put( elem.value() );
            result.emplace_back( elem.key(), valueBuffer );
        }
        return result;
    }
}
