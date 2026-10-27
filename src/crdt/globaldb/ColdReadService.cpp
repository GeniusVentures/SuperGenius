/**
 * @file       ColdReadService.cpp
 * @brief      Cold-read chain implementation (READ-01, v1.1 Phase 9
 *             plan 09-01).
 *
 * GATED TU: compiled only under SGNS_COLD_STORAGE.
 *
 * The D-01/D-02 chain: (1) LocalBlockStore::Get under cold_store_mutex_;
 * (2) on an expected Local miss, S3BlockStore::Get under
 * remote_store_mutex_. Every library result is guarded has_value()
 * BEFORE .value()/.error() (the library result alias uses
 * policy::terminate - the Cid.hpp landmine), and every library call is
 * wrapped for std::invalid_argument from out-of-set multihash CIDs
 * (programmer-error taxonomy; degraded to NotFound classification,
 * never a crash - Phase-8 precedent, BlockStoreBackedIpfsDatastore).
 *
 * Honesty rules (SC1/SC3/D-03, 09-RESEARCH §3.2):
 *  - a Local-tier NotFound is expected and non-terminal (the chain
 *    continues to S3; the common case is a block only in S3);
 *  - a Local-tier Corrupt logs loud with LOCAL attribution and continues
 *    to S3 (the remote copy may be good); a Remote-tier Corrupt ends the
 *    chain with REMOTE attribution;
 *  - transport failures at either tier are logged + counted and continue
 *    per D-03; a Remote-tier NotFound is surfaced ONLY when no
 *    transport-shaped Local-tier failure was observed on the way (a
 *    non-authoritative tier can never license a definitive-absence
 *    claim - SC1's envelope rule);
 *  - Unauthorized at either tier stops the chain with its own class;
 *  - no library call ever bypasses CID verification - the library's
 *    internal Get verification is the integrity control.
 */

#include "ColdReadService.hpp"

namespace sgns::crdt
{
    namespace
    {
        /// Precedence for aggregating non-NotFound classifications across
        /// tiers (the plan's D-03 rule: Corrupt > Unauthorized >
        /// TemporarilyUnavailable; NotFound never overrides anything).
        int ClassPrecedence( ColdReadStatus::Class cls )
        {
            switch ( cls )
            {
                case ColdReadStatus::Class::Corrupt:
                    return 3;
                case ColdReadStatus::Class::Unauthorized:
                    return 2;
                case ColdReadStatus::Class::TemporarilyUnavailable:
                    return 1;
                case ColdReadStatus::Class::NotFound:
                    return 0;
            }
            return 0;
        }
    } // namespace

    ColdReadService::ColdReadService( std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
                                      std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store,
                                      std::shared_ptr<std::mutex>                        cold_store_mutex,
                                      std::shared_ptr<std::mutex>                        remote_store_mutex,
                                      std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore>    rehydrate_target )
        : local_block_store_( std::move( local_block_store ) ),
          remote_block_store_( std::move( remote_block_store ) ),
          cold_store_mutex_( std::move( cold_store_mutex ) ),
          remote_store_mutex_( std::move( remote_store_mutex ) ),
          rehydrate_target_( std::move( rehydrate_target ) )
    {
        BOOST_ASSERT_MSG( local_block_store_ != nullptr, "local block store is nullptr" );
        BOOST_ASSERT_MSG( remote_block_store_ != nullptr, "remote block store is nullptr" );
        BOOST_ASSERT_MSG( cold_store_mutex_ != nullptr, "cold store mutex is nullptr" );
        BOOST_ASSERT_MSG( remote_store_mutex_ != nullptr, "remote store mutex is nullptr" );
        BOOST_ASSERT_MSG( rehydrate_target_ != nullptr, "rehydrate target datastore is nullptr" );
    }

    ColdReadService::~ColdReadService()
    {
        Stop(); // idempotent; joins before members die (Pitfall-5 ordering)
    }

    bool ColdReadService::ShouldLogColdFailure()
    {
        // Runs on both the CRDT DAG worker and the re-warm worker - the
        // limiter state carries its own guard.
        std::lock_guard<std::mutex> log_lock( cold_failure_log_mutex_ );
        const auto                  now = std::chrono::steady_clock::now();
        if ( last_cold_failure_log_.time_since_epoch().count() != 0
             && ( now - last_cold_failure_log_ ) < kColdFailureLogInterval )
        {
            ++suppressed_cold_failures_;
            return false;
        }
        last_cold_failure_log_ = now;
        return true;
    }

    ColdReadStatus ColdReadService::Get( const CID &cid, sgns::common::Buffer &out )
    {
        out.clear();
        ColdReadStatus status;

        // Remembered non-NotFound Local-tier classification (if any): a
        // non-authoritative local tier can never license a definitive
        // absence claim, so it outranks a later Remote NotFound (SC1).
        bool           have_local_classification = false;
        ColdReadStatus local_classification{};

        // ---- Tier 1: LocalBlockStore cold dir (D-02) ----
        try
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto local = local_block_store_->Get( cid );
            if ( local.has_value() )
            {
                // Success: the library verified the bytes against the CID
                // (never bypassed). DAG blocks are never empty - a
                // non-empty buffer is the success signal.
                out = sgns::common::Buffer( std::move( local.value().bytes ) );
                return status;
            }
            const auto &block_status = local.error();
            switch ( block_status.code )
            {
                case sgns::blockstore::StatusClass::NotFound:
                    // Expected, non-terminal (§3.2 row 1): continue to S3.
                    break;
                case sgns::blockstore::StatusClass::Corrupt:
                    // LOCAL attribution, loud; continue to S3 (the remote
                    // copy may be good).
                    if ( ShouldLogColdFailure() )
                    {
                        logger_->error( "cold read: CID verify failed at local tier (SC3)" );
                    }
                    local_classification = ColdReadStatus{ ColdReadStatus::Class::Corrupt, false };
                    have_local_classification = true;
                    break;
                case sgns::blockstore::StatusClass::Unauthorized:
                    // Stop the chain (§3.2): distinct failure class, local
                    // attribution.
                    if ( ShouldLogColdFailure() )
                    {
                        logger_->error( "cold read: local tier rejected CID request (SC3)" );
                    }
                    return ColdReadStatus{ ColdReadStatus::Class::Unauthorized, false };
                case sgns::blockstore::StatusClass::TemporarilyUnavailable:
                default:
                    // Log + count (D-03), then continue to S3.
                    ++local_transport_failures_;
                    if ( ShouldLogColdFailure() )
                    {
                        logger_->warn( "cold read: local tier temporarily unavailable (code {}), "
                                       "continuing to s3 tier",
                                       static_cast<int>( block_status.code ) );
                    }
                    local_classification = ColdReadStatus{ ColdReadStatus::Class::TemporarilyUnavailable, false };
                    have_local_classification = true;
                    break;
            }
        }
        catch ( const std::invalid_argument &e )
        {
            // Out-of-set multihash CID at library entry (programmer-error
            // taxonomy) - degrade to NotFound classification, never a
            // crash through the embedder (Phase-8 precedent).
            if ( ShouldLogColdFailure() )
            {
                logger_->warn( "cold read: local tier rejected CID: {}", e.what() );
            }
            return ColdReadStatus{ ColdReadStatus::Class::NotFound, false };
        }

        // ---- Tier 2: S3BlockStore remote (D-01/D-02) ----
        try
        {
            std::lock_guard<std::mutex> lock( *remote_store_mutex_ );
            auto remote = remote_block_store_->Get( cid );
            if ( remote.has_value() )
            {
                out = sgns::common::Buffer( std::move( remote.value().bytes ) );
                return status;
            }
            const auto &block_status = remote.error();
            switch ( block_status.code )
            {
                case sgns::blockstore::StatusClass::NotFound:
                    // Definitive absence at the remote tier. Surface
                    // NotFound ONLY when the local tier was authoritative
                    // (a remembered transport-shaped/corrupt local
                    // classification outranks it - SC1/D-03 precedence
                    // Corrupt > Unauthorized > TemporarilyUnavailable).
                    if ( have_local_classification
                         && ClassPrecedence( local_classification.cls ) > ClassPrecedence(
                                ColdReadStatus::Class::NotFound ) )
                    {
                        return local_classification;
                    }
                    return ColdReadStatus{ ColdReadStatus::Class::NotFound, true };
                case sgns::blockstore::StatusClass::Corrupt:
                    // REMOTE attribution; ends the chain (§3.2).
                    if ( ShouldLogColdFailure() )
                    {
                        logger_->error( "cold read: CID verify failed at remote tier (SC3)" );
                    }
                    return ColdReadStatus{ ColdReadStatus::Class::Corrupt, true };
                case sgns::blockstore::StatusClass::Unauthorized:
                    if ( ShouldLogColdFailure() )
                    {
                        logger_->error( "cold read: s3 tier rejected CID request (SC3)" );
                    }
                    return ColdReadStatus{ ColdReadStatus::Class::Unauthorized, true };
                case sgns::blockstore::StatusClass::TemporarilyUnavailable:
                default:
                    // The SC1 headline case: transport-shaped failure is
                    // logged + counted and NEVER surfaces as NotFound.
                    ++remote_transport_failures_;
                    if ( ShouldLogColdFailure() )
                    {
                        logger_->warn( "cold read: s3 tier temporarily unavailable (code {}), "
                                       "falling through to peers",
                                       static_cast<int>( block_status.code ) );
                    }
                    if ( have_local_classification
                         && ClassPrecedence( local_classification.cls ) > ClassPrecedence(
                                ColdReadStatus::Class::TemporarilyUnavailable ) )
                    {
                        return local_classification;
                    }
                    return ColdReadStatus{ ColdReadStatus::Class::TemporarilyUnavailable, true };
            }
        }
        catch ( const std::invalid_argument &e )
        {
            if ( ShouldLogColdFailure() )
            {
                logger_->warn( "cold read: s3 tier rejected CID: {}", e.what() );
            }
            return ColdReadStatus{ ColdReadStatus::Class::NotFound, true };
        }

        // Unreachable - both tier switches return on every path.
        return ColdReadStatus{ ColdReadStatus::Class::NotFound, true };
    }

    bool ColdReadService::HasLocalCold( const CID &cid )
    {
        // D-08/D-09: LOCAL TIER ONLY - never S3, never network. The bool
        // contract must never block or throw for the freeze-sensitive
        // pubsub-receive-path callers; a non-authoritative failure maps to
        // false plus a rate-limited log.
        try
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto has = local_block_store_->Has( cid );
            if ( has.has_value() )
            {
                return has.value();
            }
            if ( ShouldLogColdFailure() )
            {
                logger_->warn( "cold read: cold-dir presence check failed (code {}) - reporting absent",
                               static_cast<int>( has.error().code ) );
            }
            return false;
        }
        catch ( const std::invalid_argument &e )
        {
            if ( ShouldLogColdFailure() )
            {
                logger_->warn( "cold read: cold-dir presence check rejected CID: {}", e.what() );
            }
            return false;
        }
    }

    bool ColdReadService::HasDisposed( const CID &cid )
    {
        // D-11 (v1.1 Phase 11): disposed-marker point lookup, LOCAL TIER
        // ONLY - never S3, never network. Same freeze-safe bool contract
        // as HasLocalCold (the pubsub receive path calls this); a marker
        // hit means already-integrated (D-12 dedup ONLY - reads never
        // consult the marker as absence).
        try
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto disposed = local_block_store_->IsDisposed( cid );
            if ( disposed.has_value() )
            {
                return disposed.value();
            }
            if ( ShouldLogColdFailure() )
            {
                logger_->warn( "cold read: disposed-marker check failed (code {}) - reporting absent",
                               static_cast<int>( disposed.error().code ) );
            }
            return false;
        }
        catch ( const std::invalid_argument &e )
        {
            if ( ShouldLogColdFailure() )
            {
                logger_->warn( "cold read: disposed-marker check rejected CID: {}", e.what() );
            }
            return false;
        }
    }

    bool ColdReadService::EnqueueRewarm( const CID &cid )
    {
        // D-07 (READ-02): a real try-op over the bounded queue - NEVER
        // blocks the enqueuer (the responder thread) and NEVER runs the
        // fetch inline (SC2). One in-flight re-warm per CID: an existing
        // map entry means attach (the peer's next request finds it hot).
        bool enqueued = false;
        {
            std::lock_guard<std::mutex> lock( rewarm_mutex_ );
            if ( rewarm_in_flight_.count( cid ) > 0 )
            {
                return true; // attached to the in-flight re-warm (D-07)
            }
            if ( rewarm_queue_.size() < kMaxRewarmQueueDepth )
            {
                rewarm_in_flight_.emplace( cid, RewarmState{ std::chrono::steady_clock::now() } );
                rewarm_queue_.push_back( cid );
                enqueued = true;
            }
        }
        if ( !enqueued )
        {
            // Bounded overflow: drop + count + rate-limited warn; the next
            // request for this CID re-enqueues (D-07 makes drops cheap).
            ++rewarm_overflow_drops_;
            if ( ShouldLogColdFailure() )
            {
                logger_->warn( "cold read: re-warm queue full (bound {}), request dropped (D-07)",
                               kMaxRewarmQueueDepth );
            }
            return false;
        }
        rewarm_cv_.notify_one();
        return true;
    }

    void ColdReadService::Start()
    {
        if ( rewarm_thread_.joinable() )
        {
            return; // already running - idempotent
        }
        rewarm_stop_flag_.store( false );
        rewarm_thread_ = std::thread( [this]() { RewarmWorkerLoop(); } );
    }

    void ColdReadService::Stop()
    {
        rewarm_stop_flag_.store( true );
        rewarm_cv_.notify_all();
        if ( rewarm_thread_.joinable() )
        {
            // Bounded grace for an in-flight re-warm, then the
            // unconditional join (D-11 shape) - queued-but-unstarted
            // re-warms are abandoned; the hot store stays cold and the
            // next request re-enqueues.
            std::unique_lock<std::mutex> lock( rewarm_idle_mutex_ );
            rewarm_idle_cv_.wait_for( lock, kRewarmShutdownGraceWindow, [this] { return rewarm_idle_; } );
            lock.unlock();
            rewarm_thread_.join();
        }
    }

    void ColdReadService::MarkRewarmBusy()
    {
        std::lock_guard<std::mutex> lock( rewarm_idle_mutex_ );
        rewarm_idle_ = false;
    }

    void ColdReadService::MarkRewarmIdle()
    {
        {
            std::lock_guard<std::mutex> lock( rewarm_idle_mutex_ );
            rewarm_idle_ = true;
        }
        rewarm_idle_cv_.notify_all();
    }

    void ColdReadService::RewarmWorkerLoop()
    {
        while ( !rewarm_stop_flag_.load() )
        {
            CID                                    cid;
            std::chrono::steady_clock::time_point enqueued_at{};
            bool                                   have_work = false;
            {
                std::unique_lock<std::mutex> lock( rewarm_mutex_ );
                MarkRewarmIdle();
                rewarm_cv_.wait( lock, [this] { return rewarm_stop_flag_.load() || !rewarm_queue_.empty(); } );
                if ( rewarm_stop_flag_.load() )
                {
                    break;
                }
                cid = std::move( rewarm_queue_.front() );
                rewarm_queue_.pop_front();
                auto it = rewarm_in_flight_.find( cid );
                if ( it != rewarm_in_flight_.end() )
                {
                    enqueued_at = it->second.enqueued_at;
                    have_work   = true;
                }
            }

            if ( !have_work )
            {
                continue; // defensive: popped entry without map state
            }

            MarkRewarmBusy();
            // §6.4: the budget bounds SCHEDULING AND WAITING only - a
            // task found past-deadline before starting is skipped; the
            // underlying op is bounded by the executor's single-thread
            // bound (at most one pinned thread; the node's freeze-safety
            // never depends on the deadline firing).
            if ( std::chrono::steady_clock::now() - enqueued_at >= kRewarmBudget )
            {
                ++rewarm_expired_drops_;
                if ( ShouldLogColdFailure() )
                {
                    logger_->warn( "cold read: re-warm skipped - budget expired before start (D-07)" );
                }
            }
            else
            {
                // The 09-01 chain does the CID-verifying fetch + re-cache
                // (T-9-07: verification cannot be bypassed; corrupted
                // bytes classify Corrupt and are never re-cached).
                sgns::common::Buffer   bytes;
                const ColdReadStatus status = Get( cid, bytes );
                if ( !bytes.empty() )
                {
                    Rehydrate( cid, bytes );
                }
                (void)status; // classifications are logged inside the chain
            }
            {
                std::lock_guard<std::mutex> lock( rewarm_mutex_ );
                rewarm_in_flight_.erase( cid );
            }
            MarkRewarmIdle();
        }
    }

    void ColdReadService::Rehydrate( const CID &cid, const sgns::common::Buffer &bytes )
    {
        // D-04/SC4: BYTES ONLY through the Phase-8 decorator set()
        // (journal + hot write; re-journaling is idempotent -
        // LocalBlockStore journals only NEW blocks). No resolved/seal
        // marker this phase (§2.4 ruling), no pubsub, no routing, no
        // head changes.
        auto written = rehydrate_target_->set( cid, bytes );
        if ( !written.has_value() )
        {
            if ( ShouldLogColdFailure() )
            {
                logger_->error( "cold read: rehydrate write failed for CID (re-cache skipped)" );
            }
        }
    }

} // namespace sgns::crdt
