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
 * around it would hold the publisher mutex across S3 latency. The
 * MarkUploadedDurable transition is deliberately NEVER invoked (Phase-10
 * boundary).
 */

#include "ColdStorageManager.hpp"

namespace sgns::crdt
{
    ColdStorageManager::ColdStorageManager( std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
                                            std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store,
                                            std::shared_ptr<std::mutex>                        cold_store_mutex,
                                            std::shared_ptr<std::mutex>                        remote_store_mutex,
                                            BackpressureOptions                                options )
        : local_block_store_( std::move( local_block_store ) ),
          remote_block_store_( std::move( remote_block_store ) ),
          cold_store_mutex_( std::move( cold_store_mutex ) ),
          remote_store_mutex_( std::move( remote_store_mutex ) ),
          options_( options )
    {
        BOOST_ASSERT_MSG( local_block_store_ != nullptr, "local block store is nullptr" );
        BOOST_ASSERT_MSG( remote_block_store_ != nullptr, "remote block store is nullptr" );
        BOOST_ASSERT_MSG( cold_store_mutex_ != nullptr, "cold store mutex is nullptr" );
        BOOST_ASSERT_MSG( remote_store_mutex_ != nullptr, "remote store mutex is nullptr" );
        current_delay_ = std::chrono::milliseconds( options_.drain_interval_ms );
    }

    ColdStorageManager::~ColdStorageManager()
    {
        Stop(); // idempotent; joins before members die (Pitfall-5 ordering)
    }

    void ColdStorageManager::Start()
    {
        if ( thread_.joinable() )
        {
            return; // already running - idempotent
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
                while ( !stop_flag_.load() )
                {
                    MarkBusy();
                    DrainOnce();
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

    void ColdStorageManager::DrainOnce()
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
                // item ran - a failed cycle for backoff (D-03).
                RecordFailedCycle();
                return;
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
            return;
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
        // remote-present outcomes. UploadedDurable is NEVER written here
        // (Phase-10 boundary).
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            for ( const auto &cid : succeeded )
            {
                auto marked = local_block_store_->MarkUploadedAccepted( cid );
                if ( !marked.has_value() )
                {
                    any_failure = true; // mark failure: CID stays pending, retried
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
