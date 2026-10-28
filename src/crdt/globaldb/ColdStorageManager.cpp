/**
 * @file       ColdStorageManager.cpp
 * @brief      Drainer-thread implementation (SPILL-02, v1.1 Phase 8).
 *
 * GATED TU: compiled only under SGNS_COLD_STORAGE.
 *
 * The split-phase DrainOnce is the D-01 heart: the snapshot/fetch phase and
 * the journal-mark phase hold the SHARED cold-store mutex (local RocksDB
 * ops, microseconds), while the remote Put phase runs with NO lock held -
 * a set() on the dagMutex_-held write path can never wait behind S3
 * latency. The Phase-2 library surfaces (GetPendingUploads / local Get /
 * remote Put createOnly / MarkUploadedAccepted) are composed directly per
 * UploadDrainer.hpp's own composition note; the one-shot free function
 * interleaves remote Puts between journal ops, so a single lock scope
 * around it would hold the publisher mutex across S3 latency.
 *
 * DUR-01 (v1.1 Phase 10): the loop's AdvanceDurabilityOnce step is the
 * flush-gated advancement the Phase-8 "boundary" comments anticipated -
 * POST /objects/flush plus a pendingObjects==0 observation from
 * /stats/uploads is the ONLY evidence that advances the pre-flush
 * UploadedAccepted snapshot to MarkUploadedDurable (accepted-is-durable
 * is never assumed; D-01/D-02/D-03).
 */

#include "ColdStorageManager.hpp"

#include <algorithm>
#include <deque>
#include <limits>

#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>

#include "crdt/crdt_datastore.hpp"
#include "crdt/graphsync_dagsyncer.hpp"

namespace sgns::crdt
{
    namespace
    {
        /// Wall-clock unix-ms (the D-01 spill-timestamp clock).
        uint64_t UnixNowMs()
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() );
        }
    } // namespace

    ColdStorageManager::ColdStorageManager( std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
                                            std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store,
                                            std::shared_ptr<std::mutex>                        cold_store_mutex,
                                            std::shared_ptr<std::mutex>                        remote_store_mutex,
                                            BackpressureOptions                                options,
                                            DurabilityOptions                                  durability_options,
                                            std::shared_ptr<sgns::blockstore::S3AdminClient>    admin_client,
                                            RetentionOptions                                   retention_options )
        : local_block_store_( std::move( local_block_store ) ),
          remote_block_store_( std::move( remote_block_store ) ),
          cold_store_mutex_( std::move( cold_store_mutex ) ),
          remote_store_mutex_( std::move( remote_store_mutex ) ),
          options_( options ),
          admin_client_( std::move( admin_client ) ),
          durability_( durability_options ),
          retention_( retention_options )
    {
        BOOST_ASSERT_MSG( local_block_store_ != nullptr, "local block store is nullptr" );
        BOOST_ASSERT_MSG( remote_block_store_ != nullptr, "remote block store is nullptr" );
        BOOST_ASSERT_MSG( cold_store_mutex_ != nullptr, "cold store mutex is nullptr" );
        BOOST_ASSERT_MSG( remote_store_mutex_ != nullptr, "remote store mutex is nullptr" );
        current_delay_ = std::chrono::milliseconds( options_.drain_interval_ms );
        // 0-equals-default normalization (BackpressureOptions
        // discipline): durability knobs resolve to the built-in
        // constants, and the flush clocks seed to construction time so
        // the cadence floor gates even the first attempt.
        if ( durability_.flush_min_interval_ms == 0 )
        {
            durability_.flush_min_interval_ms =
                static_cast<uint32_t>( kDefaultFlushMinIntervalMs.count() );
        }
        if ( durability_.flush_max_interval_ms == 0 )
        {
            durability_.flush_max_interval_ms =
                static_cast<uint32_t>( kDefaultFlushMaxIntervalMs.count() );
        }
        const auto now = std::chrono::steady_clock::now();
        last_flush_attempt_ = now;
        last_advancement_  = now;
        flush_delay_       = std::chrono::milliseconds( durability_.flush_min_interval_ms );

        // GC-01 (v1.1 Phase 11): retention normalization - the D-02 gate is
        // retention_age_ms > 0 (absent/0 = the GC actor never runs,
        // byte-identical node behavior). The two cadence knobs follow the
        // 0-equals-default discipline and normalize ONLY when the GC is
        // active (an inactive posture keeps its knobs inert).
        gc_enabled_ = retention_.retention_age_ms > 0;
        gc_dry_run_ = retention_.dry_run;
        if ( gc_enabled_ )
        {
            if ( retention_.gc_interval_ms == 0 )
            {
                retention_.gc_interval_ms = kDefaultGcIntervalMs;
            }
            if ( retention_.grace_ms == 0 )
            {
                retention_.grace_ms = kDefaultGraceMs;
            }
        }
    }

    ColdStorageManager::~ColdStorageManager()
    {
        Stop(); // idempotent; joins before members die (Pitfall-5 ordering)
    }

    void ColdStorageManager::SetAdminPasswordPresent( bool present )
    {
        admin_password_present_ = present;
    }

    void ColdStorageManager::AttachGcDependencies( CrdtDatastore                          *crdt_datastore,
                                                    GraphsyncDAGSyncer                      *dag_syncer,
                                                    std::shared_ptr<storage::BufferStorage> hot_store )
    {
        // The §6.1 post-construction seam: the manager is built BEFORE
        // CrdtDatastore exists, so the GC dependencies attach here -
        // between construction and Start(). Every pointer must be
        // non-null (attaching a partial dependency set would arm the GC
        // against a missing universe).
        if ( crdt_datastore == nullptr || dag_syncer == nullptr || hot_store == nullptr )
        {
            logger_->error(
                "gc: AttachGcDependencies rejected a null dependency - nothing attached "
                "(the GC stays fail-closed until a complete set attaches)" );
            return;
        }
        gc_crdt_datastore_ = crdt_datastore;
        gc_dag_syncer_     = dag_syncer;
        gc_hot_store_      = std::move( hot_store );
    }

    void ColdStorageManager::Start()
    {
        if ( thread_.joinable() )
        {
            return; // already running - idempotent
        }
        // Missing-admin-password posture (planner ruling 1, 10-RESEARCH
        // §5.3): default-on flush + ONE loud startup WARN naming the env
        // var - never silent (records would sit at UploadedAccepted with
        // flush failing to authenticate), never a hard startup fail (that
        // would couple flush activation to operator bookkeeping).
        if ( admin_client_ != nullptr && !admin_password_present_ )
        {
            logger_->warn(
                "cold-storage durability: SGNS_BLOCKSTORE_S3_ADMIN_PASSWORD not set - flush cannot "
                "authenticate; records will stay UploadedAccepted until it is configured" );
        }
        stop_flag_.store( false );
        drain_requested_.store( false );
        current_delay_ = std::chrono::milliseconds( options_.drain_interval_ms );
        failure_streak_ = 0;
        thread_        = std::thread(
            [this]()
            {
                // D-12: the FIRST poll IS the crash-restart reconciliation -
                // it enumerates GetPendingUploads leftovers and re-drains
                // idempotently (S3 createOnly convergence), so node start
                // latency is unaffected and no synchronous startup gate
                // exists.
                //
                // D-08 restart seeding (v1.1 Phase 10): the SAME boundary
                // seeds the per-state counters from one journal scan -
                // counts survive restart while GetStatus stays scan-free.
                {
                    std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
                    auto accepted = local_block_store_->GetUploadsInState(
                        sgns::blockstore::UploadState::UploadedAccepted );
                    if ( accepted.has_value() )
                    {
                        accepted_count_.store( accepted.value().size(), std::memory_order_relaxed );
                    }
                    auto durable = local_block_store_->GetUploadsInState(
                        sgns::blockstore::UploadState::UploadedDurable );
                    if ( durable.has_value() )
                    {
                        durable_count_.store( durable.value().size(), std::memory_order_relaxed );
                    }
                }
                while ( !stop_flag_.load() )
                {
                    MarkBusy();
                    // DUR-01: the drain cycle exposes its quiescence
                    // outcome, and the flush-gated advancement runs on
                    // the SAME thread between the drain and the idle
                    // mark - the admin calls never run on any other
                    // thread (and never inline on the write path).
                    const bool quiescent = DrainOnce();
                    AdvanceDurabilityOnce( quiescent );
                    MarkIdle();

                    std::unique_lock<std::mutex> lock( wait_mutex_ );
                    const bool                   woken = wait_cv_.wait_for(
                        lock, current_delay_, [this] { return stop_flag_.load() || drain_requested_.load(); } );
                    lock.unlock();

                    if ( stop_flag_.load() )
                    {
                        break; // between-cycles stop check guarantees join termination
                    }
                    (void)woken; // drain_requested wake falls through to the next cycle
                }
            } );

        // GC-01 (v1.1 Phase 11, D-06): the GC runs on its OWN std::thread
        // with its own cv-poll loop (never io_, never the pubsub asio
        // context) - the drainer lifecycle shape on a second thread. The
        // retention gate is retention_age_ms > 0 (D-02 absent = no GC
        // actor, byte-identical posture); retention configured but
        // dependencies unattached is a fail-closed programmer error (no
        // thread, one error-level line).
        if ( gc_enabled_ )
        {
            if ( gc_crdt_datastore_ == nullptr || gc_dag_syncer_ == nullptr
                 || gc_hot_store_ == nullptr )
            {
                logger_->error(
                    "gc: retention configured (age {} ms) but dependencies unattached - GC "
                    "thread NOT started; AttachGcDependencies must run between construction "
                    "and Start() (programmer error posture)",
                    retention_.retention_age_ms );
            }
            else
            {
                gc_stop_flag_.store( false );
                gc_inhibit_streak_.store( 0 );
                gc_abort_streak_.store( 0 );
                gc_thread_ = std::thread(
                    [this]()
                    {
                        while ( !gc_stop_flag_.load() )
                        {
                            (void)GcPassOnce();
                            std::unique_lock<std::mutex> lock( gc_wait_mutex_ );
                            gc_cv_.wait_for(
                                lock,
                                std::chrono::milliseconds( retention_.gc_interval_ms ),
                                [this] { return gc_stop_flag_.load(); } );
                            lock.unlock();
                            if ( gc_stop_flag_.load() )
                            {
                                break; // between-pass stop check guarantees join termination
                            }
                        }
                    } );
            }
        }
    }

    void ColdStorageManager::Stop()
    {
        stop_flag_.store( true );
        wait_cv_.notify_all();
        if ( thread_.joinable() )
        {
            // D-11: bounded grace for in-flight cycle work, then the
            // unconditional join - only after it returns may the owner
            // close/destroy the stores (Pitfall-5 ordering). The thread is
            // never left running without its owner.
            std::unique_lock<std::mutex> lock( idle_mutex_ );
            idle_cv_.wait_for( lock, kShutdownGraceWindow, [this] { return idle_; } );
            lock.unlock();
            thread_.join();
        }

        // D-04 (v1.1 Phase 10): ONE bounded final flush+observe+mark
        // attempt AFTER the join (the last point where the stores
        // provably live; the Phase-9 re-warm worker stops LATER in
        // ShutdownNow, so the snapshot still takes cold_store_mutex_).
        // final_flush_done_ makes the attempt exactly-once across the
        // idempotent Stop()/destructor pair. ANY failure leaves records
        // Accepted - they resume via the first-poll/first-flush-cycle
        // paths at next startup (the single resume-code-path property).
        if ( !final_flush_done_.exchange( true ) && admin_client_ != nullptr )
        {
            std::vector<sgns::blockstore::Cid> snapshot;
            {
                std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
                auto accepted = local_block_store_->GetUploadsInState(
                    sgns::blockstore::UploadState::UploadedAccepted );
                if ( accepted.has_value() )
                {
                    snapshot = std::move( accepted.value() );
                }
            }
            if ( !snapshot.empty() )
            {
                // Review WR-01: the final flush's admin calls take the SAME
                // admin_mutex_ as the drainer tick and RefreshStagingStats -
                // the admin client is never used concurrently.
                bool                               flush_ok = false;
                bool                               evidence_ok = false;
                sgns::blockstore::UploadStats      observed{};
                {
                    std::lock_guard<std::mutex> admin_lock( admin_mutex_ );
                    auto flushed = admin_client_->Flush( kFinalFlushBudgetMs );
                    if ( flushed.has_value() )
                    {
                        auto stats = admin_client_->GetUploadStats();
                        if ( stats.has_value() )
                        {
                            observed    = stats.value();
                            flush_ok    = true;
                            evidence_ok = observed.pendingObjects == 0;
                        }
                    }
                }
                if ( !flush_ok )
                {
                    logger_->warn( "cold-storage final flush at shutdown failed ({} record(s) left "
                                   "UploadedAccepted for next startup)",
                                   snapshot.size() );
                }
                else if ( !evidence_ok )
                {
                    logger_->info( "cold-storage final flush observed non-zero pending objects - "
                                   "{} record(s) left UploadedAccepted for next startup",
                                   snapshot.size() );
                }
                else
                {
                    std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
                    auto transition = local_block_store_->MarkUploadedDurableMany(
                        gsl::span<const sgns::blockstore::Cid>( snapshot.data(), snapshot.size() ) );
                    if ( transition.has_value() )
                    {
                        durable_count_.fetch_add( snapshot.size(), std::memory_order_relaxed );
                        accepted_count_.store( 0, std::memory_order_relaxed );
                        last_flush_valid_.store( true, std::memory_order_relaxed );
                        last_flush_ok_.store( true, std::memory_order_relaxed );
                        last_flush_pending_objects_.store( 0, std::memory_order_relaxed );
                        logger_->info( "cold-storage final flush advanced {} record(s) to UploadedDurable",
                                       snapshot.size() );
                    }
                    else
                    {
                        logger_->warn( "cold-storage final flush mark failed ({} record(s) left "
                                       "UploadedAccepted for next startup)",
                                       snapshot.size() );
                    }
                }
            }
        }

        // GC-01 (v1.1 Phase 11, D-06 shutdown ordering): stop+join the GC
        // thread AFTER the drainer join and the Phase-10 final flush,
        // before returning - the last point where the GC's dependencies
        // (CRDT datastore, DAG syncer, hot face) provably live. Bounded by
        // construction: the GC loop's between-pass stop check guarantees
        // the join terminates.
        gc_stop_flag_.store( true );
        gc_cv_.notify_all();
        if ( gc_thread_.joinable() )
        {
            gc_thread_.join();
        }
    }

    void ColdStorageManager::RequestDrain()
    {
        // SC2: enqueue ONLY - flag + notify; the single-thread loop picks
        // it up and the drain body never runs on the requester's thread.
        drain_requested_.store( true );
        wait_cv_.notify_all();
    }

    ColdStorageStatus ColdStorageManager::GetStatus() const
    {
        ColdStorageStatus status;
        status.pending_upload_count = pending_count_.load( std::memory_order_relaxed );
        status.watermark            = watermark_.load( std::memory_order_relaxed );
        status.drainer_healthy      = healthy_.load( std::memory_order_relaxed );
        status.accepted_upload_count    = accepted_count_.load( std::memory_order_relaxed );
        status.durable_upload_count     = durable_count_.load( std::memory_order_relaxed );
        status.last_flush_valid         = last_flush_valid_.load( std::memory_order_relaxed );
        status.last_flush_ok            = last_flush_ok_.load( std::memory_order_relaxed );
        status.last_flush_unix_ms       = last_flush_unix_ms_.load( std::memory_order_relaxed );
        status.last_flush_pending_objects = last_flush_pending_objects_.load( std::memory_order_relaxed );
        status.flush_backoff_active     = flush_backoff_active_.load( std::memory_order_relaxed );
        status.staging_pending_objects  = staging_pending_objects_.load( std::memory_order_relaxed );
        status.staging_buffer_used      = staging_buffer_used_.load( std::memory_order_relaxed );
        status.staging_buffer_limit     = staging_buffer_limit_.load( std::memory_order_relaxed );
        status.staging_failed_uploads   = staging_failed_uploads_.load( std::memory_order_relaxed );
        status.staging_healthy          = staging_healthy_.load( std::memory_order_relaxed );
        // GC-01 (v1.1 Phase 11): gc status fields - plain values from
        // atomics, never a journal scan (the D-08 discipline).
        status.gc_enabled               = gc_enabled_;
        status.gc_dry_run               = gc_dry_run_;
        status.gc_inhibited_staging     = gc_inhibited_staging_.load( std::memory_order_relaxed );
        status.gc_evicting_count        = gc_evicting_count_.load( std::memory_order_relaxed );
        status.gc_last_pass_unix_ms     = gc_last_pass_unix_ms_.load( std::memory_order_relaxed );
        status.gc_last_pass_candidates  = gc_last_pass_candidates_.load( std::memory_order_relaxed );
        status.gc_last_pass_reclaimed_count = gc_last_pass_reclaimed_count_.load( std::memory_order_relaxed );
        status.gc_last_pass_reclaimed_bytes = gc_last_pass_reclaimed_bytes_.load( std::memory_order_relaxed );
        status.gc_disposed_marker_count = gc_disposed_marker_count_.load( std::memory_order_relaxed );
        return status;
    }

    void ColdStorageManager::MarkBusy()
    {
        std::lock_guard<std::mutex> lock( idle_mutex_ );
        idle_ = false;
    }

    void ColdStorageManager::MarkIdle()
    {
        {
            std::lock_guard<std::mutex> lock( idle_mutex_ );
            idle_ = true;
        }
        idle_cv_.notify_all();
    }

    bool ColdStorageManager::DrainOnce()
    {
        struct PendingItem
        {
            sgns::blockstore::Cid cid;
            std::vector<uint8_t>  bytes;
            bool                  have_bytes;
        };

        // (i) snapshot the pending set and fetch each item's bytes - local
        // RocksDB ops only, under the SHARED mutex honoring the library's
        // single-threaded-use contract against the publisher.
        std::vector<PendingItem> items;
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto pending = local_block_store_->GetPendingUploads();
            if ( !pending.has_value() )
            {
                // Wholesale enumeration failure aborts the cycle before any
                // item ran - a failed cycle for backoff (D-03), and NOT a
                // quiescent cycle (the drain could not even observe).
                RecordFailedCycle();
                return false;
            }
            items.reserve( pending.value().size() );
            for ( const auto &cid : pending.value() )
            {
                auto bytes = local_block_store_->Get( cid );
                if ( bytes.has_value() )
                {
                    items.push_back( PendingItem{ cid, std::move( bytes.value().bytes ), true } );
                }
                else
                {
                    // Local miss (e.g. bytes evicted): per-item failure -
                    // recorded, never rolls back siblings.
                    items.push_back( PendingItem{ cid, {}, false } );
                }
            }
        }

        if ( items.empty() )
        {
            // Healthy no-op cycle: count published, backoff reset.
            BackoffReset();
            UpdateWatermark( 0 );
            return true;
        }

        // (ii) remote Puts with createOnly semantics - the cold-store
        // mutex stays released (D-01: remote S3 latency must never block
        // the publisher). READ-01 (v1.1 Phase 9): each Put takes the
        // shared remote-store mutex PER ITEM so the cold-read chain's S3
        // Gets interleave between Puts instead of waiting out a whole
        // drain cycle (MinioS3Client single-threaded-use contract,
        // 09-RESEARCH §6.3).
        bool                                any_failure = false;
        std::vector<sgns::blockstore::Cid>  succeeded;
        for ( const auto &item : items )
        {
            if ( !item.have_bytes )
            {
                any_failure = true; // stays pending; re-drains next cycle
                continue;
            }
            std::lock_guard<std::mutex> remote_lock( *remote_store_mutex_ );
            auto put = remote_block_store_->Put(
                item.cid, gsl::span<const uint8_t>( item.bytes.data(), item.bytes.size() ) );
            if ( put.has_value() )
            {
                // Success slot = achieved OR alreadyPresent - both are
                // remote-present outcomes eligible for the journal mark.
                succeeded.push_back( item.cid );
            }
            else
            {
                any_failure = true; // per-item isolation: siblings continue
            }
        }

        // (iii) re-acquire the mutex: MarkUploadedAccepted ONLY for
        // remote-present outcomes. The UploadedDurable transition is
        // owned exclusively by AdvanceDurabilityOnce below - it fires
        // only on observed flush evidence (DUR-01), never here.
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            for ( const auto &cid : succeeded )
            {
                auto marked = local_block_store_->MarkUploadedAccepted( cid );
                if ( !marked.has_value() )
                {
                    any_failure = true; // mark failure: CID stays pending, retried
                }
                else
                {
                    accepted_count_.fetch_add( 1, std::memory_order_relaxed );
                }
            }
        }

        // D-03 / OQ3: a partial-success cycle counts as a failed cycle
        // while its successes still advanced.
        if ( any_failure )
        {
            RecordFailedCycle();
        }
        else
        {
            BackoffReset();
        }
        // Documented approximation (A4): concurrent Puts appear next cycle.
        UpdateWatermark( items.size() - succeeded.size() );
        // Quiescence signal for the D-01 hybrid cadence: every item this
        // cycle observed succeeded (an empty enumeration returned true
        // above).
        return !any_failure;
    }

    void ColdStorageManager::AdvanceDurabilityOnce( bool quiescent_this_cycle )
    {
        // No admin facade (Phase-8 5-argument posture) or shutting down:
        // nothing to advance. The stop check is defensive here; the Stop()
        // final flush (D-04) makes its own bounded attempt after the join.
        if ( admin_client_ == nullptr || stop_flag_.load() )
        {
            return;
        }

        // (0) cadence gate (D-01 hybrid): the floor elapsed AND (quiescent
        // OR the max-interval backstop elapsed); a flush-failure backoff
        // replaces the floor while a streak is held (D-07/Pitfall 6 - the
        // flush backoff state is SEPARATE from the drain backoff state).
        const auto now = std::chrono::steady_clock::now();
        const auto floor = flush_failure_streak_ > 0
                               ? flush_delay_
                               : std::chrono::milliseconds( durability_.flush_min_interval_ms );
        if ( now - last_flush_attempt_ < floor )
        {
            return;
        }
        if ( !quiescent_this_cycle
             && now - last_advancement_
                    < std::chrono::milliseconds( durability_.flush_max_interval_ms ) )
        {
            return;
        }

        // (i) the D-03 snapshot: UploadedAccepted records ONLY,
        // journal-derived under the shared mutex (crash-re-derivable by
        // construction - a restart re-runs this enumeration). Records
        // accepted AFTER this point are provably excluded from this
        // cycle's marks: the snapshot vector is the only mark input.
        // An EMPTY snapshot does NOT skip the cycle (10-02 construction
        // fix): the flush + stats observation still runs on the cadence -
        // staging health stays fresh and an idempotent replay over an
        // all-Durable journal observes flush_count advancing while
        // marking exactly nothing.
        std::vector<sgns::blockstore::Cid> snapshot;
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto accepted = local_block_store_->GetUploadsInState(
                sgns::blockstore::UploadState::UploadedAccepted );
            if ( !accepted.has_value() )
            {
                // Review WR-03: a failed enumeration IS a flush attempt -
                // advance the clock so the floor/backoff applies (sustained
                // scan failures must not retry every drain tick).
                last_flush_attempt_ = now;
                RecordFlushFailedCycle( "accepted enumeration failed" );
                return;
            }
            snapshot = std::move( accepted.value() );
        }
        accepted_count_.store( snapshot.size(), std::memory_order_relaxed );
        last_flush_attempt_ = now;

        // (ii) admin calls UNLOCKED from the publisher's perspective (a
        // NEW connection - remote_store_mutex_ is NOT involved; D-02)
        // but serialized against RefreshStagingStats through admin_mutex_
        // (D-13: the admin client is the only contended object).
        sgns::blockstore::UploadStats observed{};
        {
            std::lock_guard<std::mutex> admin_lock( admin_mutex_ );
            auto flushed = admin_client_->Flush( kCadenceFlushReadTimeoutMs );
            if ( !flushed.has_value() )
            {
                UpdateStagingHealth( observed, false );
                RecordFlushFailedCycle( "flush call failed: " + flushed.error().message );
                return;
            }
            auto stats = admin_client_->GetUploadStats();
            if ( !stats.has_value() )
            {
                UpdateStagingHealth( observed, false );
                RecordFlushFailedCycle( "stats observation failed: " + stats.error().message );
                return;
            }
            observed = stats.value();
            // The observation succeeded: staging health evaluates INSIDE
            // the lock (D-12 on-tick freshness; review WR-02 - the shared
            // failedUploads baseline cannot interleave with a concurrent
            // refresh).
            UpdateStagingHealth( observed, true );
        }

        // (iii) evidence gate (SC1): ONLY a pendingObjects == 0
        // observation permits marks - anything else leaves every record
        // Accepted for a later cycle (never accepted-is-durable).
        if ( observed.pendingObjects != 0 )
        {
            RecordFlushNoEvidence( observed.pendingObjects );
            return;
        }

        // (iv) mark EXACTLY the pre-flush snapshot (re-enumerating after
        // the stats read and marking the fresh set is forbidden - D-03).
        // ONE batched MarkUploadedDurableMany = one WriteBatch + one
        // sync Write (single fsync, all-or-nothing crash window over
        // the snapshot; per-item skip-and-continue on a vanished
        // record - no fabrication, never an abort).
        size_t marked = 0;
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto transition = local_block_store_->MarkUploadedDurableMany(
                gsl::span<const sgns::blockstore::Cid>( snapshot.data(), snapshot.size() ) );
            if ( transition.has_value() )
            {
                marked = snapshot.size();
            }
            // A batch failure leaves every record Accepted - the next
            // cycle re-derives the snapshot from the journal.
        }

        // (v) success: backoff reset, clocks, and the evidence atomics.
        BackoffResetFlush();
        last_advancement_ = now;
        accepted_count_.store( snapshot.size() - marked, std::memory_order_relaxed );
        durable_count_.fetch_add( marked, std::memory_order_relaxed );
        last_flush_valid_.store( true, std::memory_order_relaxed );
        last_flush_ok_.store( true, std::memory_order_relaxed );
        last_flush_pending_objects_.store( 0, std::memory_order_relaxed );
        last_flush_unix_ms_.store(
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() ),
            std::memory_order_relaxed );
    }

    sgns::blockstore::result<sgns::blockstore::UploadState, sgns::blockstore::BlockStatus>
    ColdStorageManager::GetUploadState( const sgns::blockstore::Cid &cid )
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        return local_block_store_->GetUploadState( cid );
    }

    void ColdStorageManager::RefreshStagingStats()
    {
        // D-13 on-demand entry: one bounded observation, predicate
        // re-evaluated, no poller thread. Serialized against the drainer
        // tick's admin calls through admin_mutex_.
        if ( admin_client_ == nullptr )
        {
            return;
        }
        std::lock_guard<std::mutex> lock( admin_mutex_ );
        auto stats = admin_client_->GetUploadStats();
        if ( stats.has_value() )
        {
            UpdateStagingHealth( stats.value(), true );
        }
        else
        {
            // Fail-safe on unreadable evidence (D-12).
            UpdateStagingHealth( sgns::blockstore::UploadStats{}, false );
        }
    }

    void ColdStorageManager::SetGcMidPassHookForTesting( std::function<void()> hook )
    {
        std::lock_guard<std::mutex> lock( gc_hook_mutex_ );
        gc_mid_pass_hook_ = std::move( hook );
    }

    void ColdStorageManager::SetGcBetweenHalvesHookForTesting( std::function<void()> hook )
    {
        std::lock_guard<std::mutex> lock( gc_hook_mutex_ );
        gc_between_halves_hook_ = std::move( hook );
    }

    bool ColdStorageManager::HeadsDiffer( const CrdtHeads::CRDTHeadList &lhs,
                                          const CrdtHeads::CRDTHeadList &rhs )
    {
        // The §2.2 exactness lemma's abort predicate: content equality of
        // the per-topic head sets (unordered-container == compares sizes
        // then per-element membership - order-insensitive).
        return !( lhs == rhs );
    }

    std::pair<std::unordered_set<CID>, bool> ColdStorageManager::WalkReachable(
        const CrdtHeads::CRDTHeadList &snapshot )
    {
        // GC-01 (D-05, v1.1 Phase 11): FRESH full walk, per pass, from the
        // heads snapshot - no persisted reachability state. Iterative
        // deque work-queue + visited set (the TraverseCIDsLinks mechanics
        // ONLY): every link is traversed UNCONDITIONALLY - the isResolved
        // skips of the CRDT processing walk mark processing state, not
        // reachability, and skipping them under-counts reachability, the
        // wrongful-eviction direction (Pitfall 1).
        std::unordered_set<CID> visited;
        std::deque<CID>         work;
        for ( const auto &[topic, cids] : snapshot )
        {
            (void)topic;
            for ( const auto &cid : cids )
            {
                work.push_back( cid );
            }
        }

        constexpr size_t kStopCheckInterval = 512;
        size_t           since_stop_check   = 0;
        while ( !work.empty() )
        {
            if ( ++since_stop_check >= kStopCheckInterval )
            {
                since_stop_check = 0;
                if ( gc_stop_flag_.load() )
                {
                    // Stopped mid-walk: reachability is incomplete - the
                    // caller must abort the pass (never a partial set).
                    return { {}, false };
                }
            }

            CID current = work.front();
            work.pop_front();
            if ( !visited.insert( current ).second )
            {
                continue; // already walked - content-addressed blocks are immutable
            }

            // Per-node read via GetNodeWithoutRequest ONLY: dagMutex_ is
            // held just inside GetNodeFromMerkleDAG (per-node granularity,
            // never across batches), and the Phase-9 cold chain serves any
            // hot-missing block (the D-10 re-learn - self-healing).
            auto node = gc_dag_syncer_->GetNodeWithoutRequest( current );
            if ( !node.has_value() )
            {
                // Walk gap: a head-reachable link is missing from EVERY
                // tier (hot, LRU, cold dir, remote). Reachability below it
                // is not computable - abort the pass toward under-eviction
                // rather than guessing (the only safe direction).
                logger_->warn(
                    "gc: pass-aborted-walk-gap - CID {} is head-reachable but missing from "
                    "every tier; reachability not computable (under-eviction)",
                    current.toString().value() );
                return { {}, false };
            }

            for ( const auto &link : node.value()->getLinks() )
            {
                work.push_back( link.get().getCID() ); // unconditional - no resolution skips
            }
        }
        return { std::move( visited ), true };
    }

    std::vector<sgns::blockstore::Cid> ColdStorageManager::GcPassOnce()
    {
        // (0) stop check: a stopped manager never evaluates policy.
        if ( gc_stop_flag_.load() )
        {
            return {};
        }

        // (1) staging gate (D-12/D-13, Phase-10 surfaces exactly as
        // shipped): refresh, then consult the conservative predicate -
        // unhealthy evidence (unreadable, buffer pressure, failing
        // uploads) inhibits the tick entirely.
        RefreshStagingStats();
        if ( !staging_healthy_.load( std::memory_order_relaxed ) )
        {
            gc_inhibited_staging_.store( true, std::memory_order_relaxed );
            const uint32_t streak = gc_inhibit_streak_.fetch_add( 1 ) + 1;
            if ( streak == 1 || streak % 10 == 0 )
            {
                logger_->warn(
                    "gc: tick inhibited - staging unhealthy (streak {}); no candidates this pass",
                    streak );
            }
            gc_last_pass_candidates_.store( 0, std::memory_order_relaxed );
            return {};
        }
        gc_inhibited_staging_.store( false, std::memory_order_relaxed );
        gc_inhibit_streak_.store( 0 );

        // GC-02 (v1.1 Phase 11): tick bookkeeping - the tick counter is
        // the defer-to-next-tick clock, and the reclaimed accumulators
        // are per-pass.
        ++gc_tick_counter_;
        pass_reclaimed_count_  = 0;
        pass_reclaimed_bytes_  = 0;

        // GC-02 (§4.2): the orphaned-hot-bytes re-scan runs FIRST - a
        // crash between the D-16 halves left hot bytes behind a committed
        // library half, and this pass completes them before evaluating
        // anything new. Suppressed under dry_run (observe-only posture).
        if ( !gc_dry_run_ )
        {
            FinishOrphanedHotCleanups();
        }

        // (2)-(5b) + candidate marking: the evaluate half.
        const auto candidates = EvaluatePass();

        // (6) the destructive half: due marks only (this tick's fresh
        // marks are NOT due - defer-to-next-tick grace, D-13).
        ProcessDueMarks();

        // (7) marker bounds (D-10).
        TrimDisposedMarkers();

        // (8) status update + the one-line per-pass summary, then the SC5
        // epoch watermark (computation + storage ONLY - nothing in v1.1
        // is deletion-gated on it).
        gc_last_pass_unix_ms_.store( UnixNowMs(), std::memory_order_relaxed );
        gc_last_pass_candidates_.store( candidates.size(), std::memory_order_relaxed );
        gc_last_pass_reclaimed_count_.store( pass_reclaimed_count_, std::memory_order_relaxed );
        gc_last_pass_reclaimed_bytes_.store( pass_reclaimed_bytes_, std::memory_order_relaxed );
        logger_->info( "gc: pass complete - {} candidate(s), {} evicted (retention {} ms, dry_run {})",
                       candidates.size(),
                       pass_reclaimed_count_,
                       retention_.retention_age_ms,
                       gc_dry_run_ );
        WriteEpochWatermark( last_pass_snapshot_ );
        return candidates;
    }

    std::vector<sgns::blockstore::Cid> ColdStorageManager::EvaluatePass()
    {
        // (2) heads snapshot: the pass universe. A read failure aborts the
        // pass (under-eviction - reachability without a universe is a
        // guess).
        auto heads_result = gc_crdt_datastore_->GetHeadList();
        if ( heads_result.has_error() )
        {
            const uint32_t streak = gc_abort_streak_.fetch_add( 1 ) + 1;
            if ( streak == 1 || streak % 10 == 0 )
            {
                logger_->warn(
                    "gc: pass-aborted-heads-read (streak {}); no candidates this pass",
                    streak );
            }
            gc_last_pass_candidates_.store( 0, std::memory_order_relaxed );
            return {};
        }
        const CrdtHeads::CRDTHeadList snapshot = heads_result.value().first;

        // (3) fresh reachability walk (D-05): heads are pinned by
        // construction (walk roots are IN the reachable set), and the
        // /crdt/gc/ epoch-record prefix is pinned alongside heads (SC5 -
        // keys under that prefix are never GC state, never candidates).
        const auto [reachable, walk_exact] = WalkReachable( snapshot );
        if ( !walk_exact )
        {
            const uint32_t streak = gc_abort_streak_.fetch_add( 1 ) + 1;
            if ( streak == 1 || streak % 10 == 0 )
            {
                logger_->warn(
                    "gc: pass-aborted-walk (streak {}); no candidates this pass", streak );
            }
            gc_last_pass_candidates_.store( 0, std::memory_order_relaxed );
            return {};
        }

        // (4) eligibility: enumerate the journal (MI-01 - the journal is
        // the truth, never the advisory status counters) under the shared
        // cold-store mutex; local RocksDB point reads, microseconds.
        std::vector<sgns::blockstore::Cid> candidates;
        const uint64_t                     now_ms = UnixNowMs();
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto durable = local_block_store_->GetDurableWithSpillTime();
            if ( !durable.has_value() )
            {
                // Enumeration failure poisons the evidence: abort the pass
                // (an unreadable scan is never eligibility evidence, MI-02).
                const uint32_t streak = gc_abort_streak_.fetch_add( 1 ) + 1;
                if ( streak == 1 || streak % 10 == 0 )
                {
                    logger_->warn(
                        "gc: pass-aborted-journal-read (streak {}); no candidates this pass",
                        streak );
                }
                gc_last_pass_candidates_.store( 0, std::memory_order_relaxed );
                return {};
            }

            for ( auto &entry : durable.value() )
            {
                // D-01 backfill fairness: a record with NO spill timestamp
                // (pre-Phase-11 store) gets one NOW and waits a full
                // retention period - never an assumed age.
                if ( entry.spilled_at_unix_ms == 0 )
                {
                    (void)local_block_store_->SetSpilledAt( entry.cid, now_ms );
                    continue;
                }
                // Clock-skew guard: a future-dated stamp reads young -
                // never a wrapped huge age.
                if ( entry.spilled_at_unix_ms > now_ms )
                {
                    continue;
                }
                // Age gate: STRICTLY past the threshold - age == threshold
                // is NOT eligible (ties break toward under-eviction).
                if ( ( now_ms - entry.spilled_at_unix_ms ) <= retention_.retention_age_ms )
                {
                    continue;
                }
                // Reachability gate: heads are pinned by construction
                // (walk roots); a reachable old block is never a candidate.
                if ( reachable.count( CID( entry.cid ) ) > 0 )
                {
                    continue;
                }
                // Durability gate (MI-02): the journal state must be
                // EXACTLY kEvictionDurabilityGate - anything else
                // (NoRecord/Pending/Accepted) or an unreadable read is not
                // eligible.
                auto state = local_block_store_->GetUploadState( entry.cid );
                if ( !state.has_value() || state.value() != kEvictionDurabilityGate )
                {
                    continue;
                }
                // Pin rule (SC5): heads are pinned by construction (walk
                // roots) and every hot-store key under the /crdt/gc/
                // epoch-record prefix is pinned alongside them - GC state
                // (the watermark record) is never a candidate and never
                // deleted. Journal CIDs and /crdt/gc/ text keys are
                // disjoint keyspaces BY CONSTRUCTION (candidates come only
                // from the journal column family); the rule binds the
                // delete path to the same disjointness.
                candidates.push_back( entry.cid );
            }
        }

        // (5) heads-equality re-check (the §2.2 exactness lemma): blocks
        // are immutable and content-addressed, so heads-equality against
        // the pass-start snapshot proves the snapshot's reachable set is
        // STILL exact. ANY difference aborts the pass - zero candidates,
        // under-eviction, one rate-limited line. The TEST-ONLY mid-pass
        // hook fires just before the re-check: the heads-change abort
        // case mutates heads inside the pass window through it.
        {
            std::lock_guard<std::mutex> lock( gc_hook_mutex_ );
            if ( gc_mid_pass_hook_ )
            {
                gc_mid_pass_hook_();
            }
        }
        auto heads_now = gc_crdt_datastore_->GetHeadList();
        if ( heads_now.has_error()
             || HeadsDiffer( heads_now.value().first, snapshot ) )
        {
            const uint32_t streak = gc_abort_streak_.fetch_add( 1 ) + 1;
            if ( streak == 1 || streak % 10 == 0 )
            {
                logger_->warn(
                    "gc: pass-aborted-on-heads-change (streak {}) - heads moved during the "
                    "pass; zero candidates (under-eviction)",
                    streak );
            }
            gc_last_pass_candidates_.store( 0, std::memory_order_relaxed );
            return {};
        }
        gc_abort_streak_.store( 0 );

        // (5a) GC-02 (D-13): mark every candidate in-memory. EVERYTHING
        // the destructive step needs is captured NOW, before anything is
        // deleted: the heads snapshot this evaluation rests on, the
        // block's cold-dir size, its parsed delta row keys, and its hot
        // seal state. A CID already carrying a mark (a prior pass's
        // unfinished grace) is not re-marked.
        for ( const auto &cid : candidates )
        {
            bool already_marked = false;
            for ( const auto &mark : evicting_marks_ )
            {
                if ( mark.cid == cid )
                {
                    already_marked = true;
                    break;
                }
            }
            if ( already_marked )
            {
                continue;
            }

            EvictingMark mark( cid );
            mark.cid              = cid;
            mark.marked_at_unix_ms = now_ms;
            mark.mark_tick        = gc_tick_counter_;
            mark.heads_snapshot   = snapshot;
            {
                std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
                auto block = local_block_store_->Get( cid );
                if ( block.has_value() )
                {
                    mark.size_bytes = block.value().bytes.size();
                    // Parse the delta to compute EXACTLY this block's own
                    // membership rows (never enumerated): a raw orphan has
                    // no rows, a delta's rows are ElemsPrefix(key)/<cid>.
                    auto node = sgns::ipfs_lite::ipld::IPLDNodeImpl::createFromRawBytes(
                        gsl::span<const uint8_t>( block.value().bytes.data(),
                                                  block.value().bytes.size() ) );
                    if ( node.has_value() )
                    {
                        const auto &content = node.value()->content();
                        CrdtDatastore::Delta delta;
                        if ( delta.ParseFromArray( content.data(),
                                                   static_cast<int>( content.size() ) ) )
                        {
                            const auto cid_string = CID( cid ).toString().value();
                            // The elems namespace is the SIBLING of the
                            // k-namespace under the set root: keys prefix
                            // "<set>/s/k" -> rows "<set>/s/s/<key>/<cid>"
                            // (GetKeysPrefix carries NO trailing slash).
                            const auto keys_prefix = gc_crdt_datastore_->GetKeysPrefix();
                            const auto elems_base  = keys_prefix.substr( 0, keys_prefix.size() - 1 ) + "s/";
                            for ( const auto &elem : delta.elements() )
                            {
                                mark.hot_row_keys.push_back( elems_base + elem.key() + "/"
                                                             + cid_string );
                            }
                        }
                    }
                }
            }
            if ( gc_hot_store_ != nullptr )
            {
                sgns::storage::Buffer seal_key;
                seal_key.put( CID( cid ).toString().value() + "sealed" );
                mark.was_sealed = gc_hot_store_->contains( seal_key );
            }
            evicting_marks_.push_back( std::move( mark ) );
        }
        gc_evicting_count_.store( evicting_marks_.size(), std::memory_order_relaxed );
        last_pass_snapshot_ = snapshot;
        return candidates;
    }

    void ColdStorageManager::ProcessDueMarks()
    {
        if ( evicting_marks_.empty() )
        {
            gc_evicting_count_.store( 0, std::memory_order_relaxed );
            return;
        }
        const uint64_t now_ms = UnixNowMs();
        std::vector<EvictingMark> retained;
        retained.reserve( evicting_marks_.size() );
        for ( auto &mark : evicting_marks_ )
        {
            // Defer-to-next-tick (D-13): a mark is due only on a LATER
            // tick AND after the grace elapsed.
            if ( gc_tick_counter_ <= mark.mark_tick
                 || now_ms < mark.marked_at_unix_ms + retention_.grace_ms )
            {
                retained.push_back( std::move( mark ) );
                continue;
            }
            // Delete-time exactness re-check (D-14): CURRENT heads vs the
            // snapshot THIS mark rests on. ANY difference (even one head)
            // cancels: clear the mark, keep the bytes, the CID returns to
            // normal evaluation (under-eviction).
            auto heads_now = gc_crdt_datastore_->GetHeadList();
            if ( heads_now.has_error()
                 || HeadsDiffer( heads_now.value().first, mark.heads_snapshot ) )
            {
                logger_->info( "gc: delete-time cancel - heads changed since mark; CID kept "
                               "and returns to normal evaluation" );
                continue; // mark dropped, bytes kept
            }
            if ( gc_dry_run_ )
            {
                // D-03: identical evaluation and metrics, BOTH halves
                // suppressed - the mark stays cycling so the observe-only
                // posture is honest.
                retained.push_back( std::move( mark ) );
                continue;
            }
            // D-16 HALF 1: the library batch (cold bytes + journal +
            // timestamp + disposed marker) under the shared mutex.
            sgns::blockstore::DisposedRecord record;
            record.disposed_at_unix_ms = now_ms;
            record.reclaimed_bytes     = mark.size_bytes;
            record.original_size       = mark.size_bytes;
            record.reason              = kEvictionReasonRetention;
            record.was_sealed          = mark.was_sealed;
            {
                std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
                auto evicted = local_block_store_->EvictLocalAndMarkDisposed( mark.cid, record );
                if ( !evicted.has_value() )
                {
                    // Library failure: the mark survives for a later tick
                    // (under-eviction; nothing partial committed).
                    logger_->warn( "gc: eviction batch failed - mark retained for a later pass" );
                    retained.push_back( std::move( mark ) );
                    continue;
                }
            }
            // D-16 between-halves window: the library-atomic cold half is
            // committed (bytes + journal + timestamp + disposed marker)
            // and the hot cleanup half has NOT started. The evict-x-serve
            // race tests park the GC thread here while a serve-side S3
            // fetch is in flight over the same CID. TEST-ONLY seam - null
            // in production, invoked OUTSIDE cold_store_mutex_ (the
            // split-phase discipline stands; the window is driven, never
            // locked - Pitfall 6).
            {
                std::lock_guard<std::mutex> hook_lock( gc_hook_mutex_ );
                if ( gc_between_halves_hook_ )
                {
                    gc_between_halves_hook_();
                }
            }
            // D-16 HALF 2: the idempotent hot cleanup + LRU invalidation.
            RunHotCleanup( mark.cid, mark.hot_row_keys );
            ++pass_reclaimed_count_;
            pass_reclaimed_bytes_ += mark.size_bytes;
            logger_->info( "gc: evicted - reclaimed {} local byte(s)",
                           mark.size_bytes );
        }
        evicting_marks_ = std::move( retained );
        gc_evicting_count_.store( evicting_marks_.size(), std::memory_order_relaxed );
    }

    void ColdStorageManager::RunHotCleanup( const sgns::blockstore::Cid  &cid,
                                            const std::vector<std::string> &row_keys )
    {
        if ( gc_hot_store_ == nullptr )
        {
            return;
        }
        const CID cid_key( cid );
        // ONE hot-store batch: the binary CID key, the text <cid>sealed
        // key, and EXACTLY the block's own membership rows (computed from
        // the delta content at mark/recovery time - never enumerated).
        // Deleting absent keys succeeds (idempotent post-state).
        auto batch = gc_hot_store_->batch();
        auto encoded = cid_key.toBytes();
        if ( encoded.has_value() )
        {
            sgns::storage::Buffer binary_key;
            binary_key.put( encoded.value() );
            (void)batch->remove( binary_key );
        }
        sgns::storage::Buffer seal_key;
        seal_key.put( cid_key.toString().value() + "sealed" );
        (void)batch->remove( seal_key );
        for ( const auto &row : row_keys )
        {
            sgns::storage::Buffer row_key;
            row_key.put( row );
            (void)batch->remove( row_key );
        }
        (void)batch->commit();
        // A6: LRU invalidation AFTER the hot deletes - post-cleanup the
        // hot and LRU HasBlock terms miss and the disposed-marker term
        // answers (HasBlock stays honest through the whole window).
        (void)gc_dag_syncer_->DeleteCIDBlock( cid_key );
    }

    void ColdStorageManager::FinishOrphanedHotCleanups()
    {
        // §4.2: complete HALF 2 for markers whose hot binary key survived
        // (the crash-between-halves window). The library half already
        // committed; the cold-dir bytes are gone, so the block's own
        // membership rows are recomputed from the REMOTE copy.
        if ( gc_hot_store_ == nullptr )
        {
            return;
        }
        std::vector<std::pair<sgns::blockstore::Cid, sgns::blockstore::DisposedRecord>> markers;
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto disposed = local_block_store_->GetDisposed();
            if ( !disposed.has_value() )
            {
                return; // unreadable markers: skip this pass (safe)
            }
            markers = std::move( disposed.value() );
        }
        for ( const auto &entry : markers )
        {
            const CID cid_key( entry.first );
            auto encoded = cid_key.toBytes();
            if ( !encoded.has_value() )
            {
                continue;
            }
            sgns::storage::Buffer binary_key;
            binary_key.put( encoded.value() );
            if ( !gc_hot_store_->contains( binary_key ) )
            {
                continue; // hot already clean - nothing to finish
            }
            // Recompute the block's own rows from the remote copy.
            std::vector<std::string> rows;
            {
                std::lock_guard<std::mutex> remote_lock( *remote_store_mutex_ );
                auto block = remote_block_store_->Get( entry.first );
                if ( block.has_value() )
                {
                    auto node = sgns::ipfs_lite::ipld::IPLDNodeImpl::createFromRawBytes(
                        gsl::span<const uint8_t>( block.value().bytes.data(),
                                                  block.value().bytes.size() ) );
                    if ( node.has_value() )
                    {
                        const auto &content = node.value()->content();
                        CrdtDatastore::Delta delta;
                        if ( delta.ParseFromArray( content.data(),
                                                   static_cast<int>( content.size() ) ) )
                        {
                            const auto cid_string = cid_key.toString().value();
                            // The sibling-of-k derivation (see the mark
                            // capture in EvaluatePass).
                            const auto keys_prefix = gc_crdt_datastore_->GetKeysPrefix();
                            const auto elems_base  = keys_prefix.substr( 0, keys_prefix.size() - 1 ) + "s/";
                            for ( const auto &elem : delta.elements() )
                            {
                                rows.push_back( elems_base + elem.key() + "/" + cid_string );
                            }
                        }
                    }
                }
            }
            RunHotCleanup( entry.first, rows );
            logger_->info( "gc: orphaned-hot-bytes re-scan completed the hot-cleanup half" );
        }
    }

    void ColdStorageManager::TrimDisposedMarkers()
    {
        const uint64_t now_ms = UnixNowMs();
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            (void)local_block_store_->TrimDisposed(
                sgns::blockstore::kDefaultDisposedMarkerCap,
                now_ms > sgns::blockstore::kDefaultDisposedMarkerMaxAgeMs
                    ? now_ms - sgns::blockstore::kDefaultDisposedMarkerMaxAgeMs
                    : 0 );
            auto markers = local_block_store_->GetDisposed();
            if ( markers.has_value() )
            {
                gc_disposed_marker_count_.store( markers.value().size(),
                                                 std::memory_order_relaxed );
            }
        }
    }

    void ColdStorageManager::WriteEpochWatermark( const CrdtHeads::CRDTHeadList &snapshot )
    {
        // SC5 (GC-01): per-topic {min head height (the floor), max height,
        // computed_at} - computed from the pass-start snapshot, stored as
        // ONE node-local record under the PINNED /crdt/gc/ prefix. A
        // DIRECT hot-store write through the attached face: never through
        // the CRDT datastore Put path (which would publish a block and
        // mutate heads). Layout documented on the declaration.
        if ( gc_hot_store_ == nullptr )
        {
            return; // unattached (should not happen post-Start) - advisory
        }

        struct TopicWatermark
        {
            std::string topic;
            uint64_t    min_height{ std::numeric_limits<uint64_t>::max() };
            uint64_t    max_height{ 0 };
        };
        std::vector<TopicWatermark> watermarks;
        for ( const auto &[topic, cids] : snapshot )
        {
            if ( cids.empty() )
            {
                continue;
            }
            TopicWatermark mark;
            mark.topic = topic;
            for ( const auto &cid : cids )
            {
                // Cache read under the heads' own shared lock; a failed
                // read skips the head conservatively (advisory record).
                auto height = gc_crdt_datastore_->GetHeadHeight( cid, topic );
                if ( height.has_error() )
                {
                    continue;
                }
                mark.min_height = std::min( mark.min_height,
                                            static_cast<uint64_t>( height.value() ) );
                mark.max_height = std::max( mark.max_height,
                                            static_cast<uint64_t>( height.value() ) );
            }
            if ( mark.min_height == std::numeric_limits<uint64_t>::max() )
            {
                mark.min_height = 0; // every height read failed - empty floor
            }
            watermarks.push_back( std::move( mark ) );
        }

        auto append_u16 = []( std::vector<uint8_t> &out, uint16_t v ) {
            out.push_back( static_cast<uint8_t>( v & 0xFF ) );
            out.push_back( static_cast<uint8_t>( ( v >> 8 ) & 0xFF ) );
        };
        auto append_u32 = []( std::vector<uint8_t> &out, uint32_t v ) {
            for ( unsigned i = 0; i < 4; ++i )
            {
                out.push_back( static_cast<uint8_t>( ( v >> ( 8 * i ) ) & 0xFF ) );
            }
        };
        auto append_u64 = []( std::vector<uint8_t> &out, uint64_t v ) {
            for ( unsigned i = 0; i < 8; ++i )
            {
                out.push_back( static_cast<uint8_t>( ( v >> ( 8 * i ) ) & 0xFF ) );
            }
        };

        std::vector<uint8_t> record;
        append_u64( record, UnixNowMs() );
        append_u32( record, static_cast<uint32_t>( watermarks.size() ) );
        for ( const auto &mark : watermarks )
        {
            append_u16( record, static_cast<uint16_t>( mark.topic.size() ) );
            record.insert( record.end(), mark.topic.begin(), mark.topic.end() );
            append_u64( record, mark.min_height );
            append_u64( record, mark.max_height );
        }

        sgns::storage::Buffer key;
        key.put( std::string( kGcEpochWatermarkKey ) );
        sgns::storage::Buffer value;
        value.put( record );
        auto written = gc_hot_store_->put( key, value );
        if ( !written.has_value() )
        {
            // Advisory bookkeeping: one rate-limited warn, never an abort.
            logger_->warn( "gc: epoch watermark write failed (advisory record; no pass impact)" );
        }
    }

    void ColdStorageManager::UpdateStagingHealth( const sgns::blockstore::UploadStats &stats,
                                                  bool                              admin_reachable )
    {
        // The conservative three-clause D-12 predicate. GC inhibition
        // defaults to INHIBITED on unreadable, garbage, or absent
        // evidence - never healthy-by-omission.
        if ( !admin_reachable )
        {
            admin_reachable_last_tick_.store( false, std::memory_order_relaxed );
            staging_healthy_.store( false, std::memory_order_relaxed );
            return; // counters keep their last-known values
        }
        admin_reachable_last_tick_.store( true, std::memory_order_relaxed );
        const bool buffer_ok =
            stats.bufferLimit == 0
            || static_cast<double>( stats.bufferUsed )
                   <= kStagingBufferFraction * static_cast<double>( stats.bufferLimit );
        const bool failed_flat =
            stats.failedUploads == previous_failed_uploads_.load( std::memory_order_relaxed );
        staging_healthy_.store( buffer_ok && failed_flat, std::memory_order_relaxed );
        previous_failed_uploads_.store( stats.failedUploads, std::memory_order_relaxed );
        staging_pending_objects_.store( stats.pendingObjects, std::memory_order_relaxed );
        staging_buffer_used_.store( stats.bufferUsed, std::memory_order_relaxed );
        staging_buffer_limit_.store( stats.bufferLimit, std::memory_order_relaxed );
        staging_failed_uploads_.store( stats.failedUploads, std::memory_order_relaxed );
    }

    void ColdStorageManager::RecordFlushFailedCycle( const std::string &context )
    {
        ++flush_failure_streak_;
        last_flush_valid_.store( false, std::memory_order_relaxed );
        last_flush_ok_.store( false, std::memory_order_relaxed );
        flush_backoff_active_.store( true, std::memory_order_relaxed );
        const auto doubled = flush_delay_ * kBackoffFactor;
        if ( flush_delay_ < kFlushBackoffCapMs )
        {
            flush_delay_ = doubled < kFlushBackoffCapMs ? doubled : kFlushBackoffCapMs;
        }
        // D-07: the flush-failure alarm family - first failure carries the
        // BlockStatus context and the engaged backoff delay, repeats are
        // rate-limited while the streak is held, and recovery logs on the
        // next success. Wording stays "cold-storage flush ..." - always
        // distinguishable from the "cold-storage drainer ..." family.
        // drainer_healthy is NEVER touched here (Pitfall 6 separation -
        // the sustained-flush signal surfaces via last_flush_ok=false,
        // flush_backoff_active, and this alarm, not by falsifying drain
        // health).
        if ( flush_failure_streak_ == 1 )
        {
            logger_->warn( "cold-storage flush failed: {} (flush backoff engaged, next attempt in {} ms)",
                           context,
                           flush_delay_.count() );
        }
        else if ( flush_failure_streak_ % 10 == 0 )
        {
            logger_->warn( "cold-storage flush still failing (streak {}): {}",
                           flush_failure_streak_,
                           context );
        }
    }

    void ColdStorageManager::BackoffResetFlush()
    {
        if ( flush_failure_streak_ != 0 )
        {
            logger_->info( "cold-storage flush recovered: backoff reset ({} failed cycle(s) cleared)",
                           flush_failure_streak_ );
        }
        flush_failure_streak_ = 0;
        flush_backoff_active_.store( false, std::memory_order_relaxed );
        flush_delay_          = std::chrono::milliseconds( durability_.flush_min_interval_ms );
    }

    void ColdStorageManager::RecordFlushNoEvidence( uint64_t observed_pending )
    {
        // A valid observation whose evidence was not met: records stay
        // Accepted ("not this cycle") and NOTHING resets - the cadence
        // clock already advanced at the attempt, the backoff stays
        // untouched (this is not a failure), and the evidence atomics
        // record valid-but-not-ok so operators can see the staging
        // pipeline is still draining.
        last_flush_valid_.store( true, std::memory_order_relaxed );
        last_flush_ok_.store( false, std::memory_order_relaxed );
        last_flush_pending_objects_.store( observed_pending, std::memory_order_relaxed );
        last_flush_unix_ms_.store(
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() ),
            std::memory_order_relaxed );
    }

    void ColdStorageManager::RecordFailedCycle()
    {
        ++failure_streak_;
        healthy_.store( false, std::memory_order_relaxed );
        const auto cap     = std::chrono::milliseconds( options_.backoff_cap_ms );
        const auto doubled = current_delay_ * kBackoffFactor;
        if ( current_delay_ < cap )
        {
            current_delay_ = doubled < cap ? doubled : cap;
        }
    }

    void ColdStorageManager::BackoffReset()
    {
        if ( failure_streak_ != 0 )
        {
            logger_->info( "cold-storage drainer recovered: backoff reset ({} failed cycle(s) cleared)",
                           failure_streak_ );
        }
        failure_streak_ = 0;
        healthy_.store( true, std::memory_order_relaxed );
        current_delay_ = std::chrono::milliseconds( options_.drain_interval_ms );
    }

    void ColdStorageManager::UpdateWatermark( size_t pending_after )
    {
        pending_count_.store( pending_after, std::memory_order_relaxed );

        const ColdStorageWatermark previous = watermark_.load( std::memory_order_relaxed );
        ColdStorageWatermark      current   = ColdStorageWatermark::None;
        if ( pending_after >= options_.critical_pending )
        {
            current = ColdStorageWatermark::Critical;
        }
        else if ( pending_after >= options_.warn_pending )
        {
            current = ColdStorageWatermark::Warn;
        }

        if ( current != previous )
        {
            watermark_.store( current, std::memory_order_relaxed );
            last_heartbeat_ = std::chrono::steady_clock::now();
            switch ( current )
            {
                case ColdStorageWatermark::Critical:
                    logger_->critical(
                        "cold-storage pending uploads crossed to CRITICAL watermark: {} pending "
                        "(warn={}, critical={})",
                        pending_after,
                        options_.warn_pending,
                        options_.critical_pending );
                    break;
                case ColdStorageWatermark::Warn:
                    logger_->warn( "cold-storage pending uploads crossed to WARN watermark: {} pending "
                                   "(warn={}, critical={})",
                                   pending_after,
                                   options_.warn_pending,
                                   options_.critical_pending );
                    break;
                case ColdStorageWatermark::None:
                    // Down-crossings/recovery are logged too - recovery is
                    // signal (D-09).
                    logger_->info( "cold-storage watermark recovered: {} pending", pending_after );
                    break;
            }
        }

        if ( current != ColdStorageWatermark::None )
        {
            // Held-watermark heartbeat so a multi-hour outage stays visible
            // in logs (D-09) - one line per heartbeat period.
            const auto now = std::chrono::steady_clock::now();
            if ( now - last_heartbeat_ >= kHeartbeatPeriod )
            {
                logger_->warn( "cold-storage watermark still held: {} pending (watermark level={})",
                               pending_after,
                               current == ColdStorageWatermark::Critical ? "critical" : "warn" );
                last_heartbeat_ = now;
            }
        }
    }

} // namespace sgns::crdt
