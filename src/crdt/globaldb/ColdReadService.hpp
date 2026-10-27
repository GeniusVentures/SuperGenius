/**
 * @file       ColdReadService.hpp
 * @brief      Cold-read fallback chain + rehydrate service (READ-01,
 *             v1.1 Phase 9 plan 09-01).
 *
 * GATED TU HEADER: compiled (and includable) ONLY under
 * SGNS_COLD_STORAGE - it carries sgns-blockstore types. globaldb.hpp,
 * globaldb.cpp, and ColdStorageStack.hpp reach this class EXCLUSIVELY
 * through the ColdStorageStack bundle (D-10 - no library type crosses
 * the ungated boundary); the DAGSyncer-side interface it implements
 * (ColdReadFallback) lives in the ungated crdt/cold_read_fallback.hpp.
 *
 * Implements the D-01/D-02 read chain over the SAME store handles the
 * Phase-8 stack constructed (LocalBlockStore cold dir, then S3BlockStore
 * remote tier, both CID-verifying internally - verification is never
 * bypassed), the D-04 rehydrate path (bytes only, through the Phase-8
 * decorator set() - SC4), and the D-08/D-09 local-only cold-dir presence
 * check. Thread discipline: every LocalBlockStore access runs under the
 * SHARED cold_store_mutex_ (library single-threaded-use contract against
 * the publisher and the drainer); every S3BlockStore access runs under
 * the SHARED remote_store_mutex_ (the same mutex serializes the
 * drainer's remote Put phase - MinioS3Client has no concurrent-call
 * contract, 09-RESEARCH §6.3).
 */

#ifndef SUPERGENIUS_CRDT_COLDREADSERVICE_HPP
#define SUPERGENIUS_CRDT_COLDREADSERVICE_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <ipfs_lite/ipfs/datastore.hpp>

#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>

#include "base/logger.hpp"
#include "crdt/cold_read_fallback.hpp"

namespace sgns::crdt
{
    class ColdReadService final : public ColdReadFallback
    {
    public:
        /**
         * @brief       Construct the cold-read chain over the shared
         *              Phase-8 stack handles.
         * @param[in]   local_block_store the cold-dir journal store (the
         *              SAME instance the decorator and drainer use)
         * @param[in]   remote_block_store the S3 tier (the SAME instance
         *              the drainer Puts through)
         * @param[in]   cold_store_mutex the ONE mutex shared with the
         *              publisher and the drainer over the LocalBlockStore
         * @param[in]   remote_store_mutex the ONE mutex serializing all
         *              S3BlockStore access (chain Gets vs drainer Puts;
         *              09-RESEARCH §6.3)
         * @param[in]   rehydrate_target the Phase-8 decorator (set()
         *              journals + writes hot - SC4's dedicated path)
         */
        ColdReadService( std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
                         std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store,
                         std::shared_ptr<std::mutex>                        cold_store_mutex,
                         std::shared_ptr<std::mutex>                        remote_store_mutex,
                         std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore>    rehydrate_target );

        ~ColdReadService() override;

        ColdReadService( const ColdReadService & )            = delete;
        ColdReadService &operator=( const ColdReadService & ) = delete;
        ColdReadService( ColdReadService && )                 = delete;
        ColdReadService &operator=( ColdReadService && )      = delete;

        /** @copydoc ColdReadFallback::Get() */
        ColdReadStatus Get( const CID &cid, sgns::common::Buffer &out ) override;

        /** @copydoc ColdReadFallback::HasLocalCold() */
        bool HasLocalCold( const CID &cid ) override;

        /** @copydoc ColdReadFallback::HasDisposed() */
        bool HasDisposed( const CID &cid ) override;

        /** @copydoc ColdReadFallback::EnqueueRewarm() */
        bool EnqueueRewarm( const CID &cid ) override;

        /** @copydoc ColdReadFallback::Rehydrate() */
        void Rehydrate( const CID &cid, const sgns::common::Buffer &bytes ) override;

        /**
         * @brief      Spawn the re-warm worker thread (idempotent;
         *             READ-02, v1.1 Phase 9 plan 09-02).
         * @note        Own std::thread + cv wake - never io_, never the
         *             pubsub asio context (Phase-8 D-10 pattern).
         */
        void Start();

        // D-12 (v1.1 Phase 9 plan 09-02): compile-time re-warm defaults
        // ONLY - nothing enters crdt_config.json (RESEARCH §7.3). Public:
        // these ARE the documented built-in defaults the SC2 evidence
        // banner cites.
        /// Re-warm budget: must complete before the peer's ~5 s first
        /// failed-root retry and matches CID_FAILURE_SUPPRESSION
        /// (graphsync_dagsyncer.hpp) so the retry window and the
        /// warm-completion window align; far below the requester's 20 s
        /// total budget (RESEARCH §7.3).
        static constexpr std::chrono::milliseconds kRewarmBudget{ 4000 };
        /// Queue bound: bounds re-warm memory; overflow drops + counts and
        /// the next request re-enqueues (D-07 makes drops cheap).
        static constexpr size_t kMaxRewarmQueueDepth{ 64 };
        /// Shutdown grace for an in-flight re-warm (Phase-8
        /// kShutdownGraceWindow parity; NOT a knob - D-12).
        static constexpr std::chrono::milliseconds kRewarmShutdownGraceWindow{ 5000 };

        /**
         * @brief      Stop the re-warm worker: flag -> notify -> bounded
         *             grace -> unconditional join (D-11 shape).
         * @note        Queued-but-unstarted re-warms are abandoned (the
         *             hot store simply stays cold; the next request
         *             re-enqueues).
         */
        void Stop();

    private:
        /**
         * @brief   Time-based rate limiter for cold-failure logging: at
         *          most one line per kColdFailureLogInterval, counting
         *          suppressed lines so the next line stays informative
         *          (BlockStoreBackedIpfsDatastore discipline).
         * @return  true when a line may be emitted now
         */
        bool ShouldLogColdFailure();

        /// Minimum spacing between cold-failure log lines (the chain runs
        /// on the CRDT DAG worker - a failing s3d must not spam it).
        static constexpr std::chrono::seconds kColdFailureLogInterval{ 1 };

        /// Per-CID in-flight re-warm state (D-07 coalescing: the map is
        /// dedup metadata, NOT promise infrastructure - nobody waits,
        /// so there are no futures or waiter lists, RESEARCH §8.2).
        struct RewarmState
        {
            std::chrono::steady_clock::time_point enqueued_at; ///< scheduling-time marker for the budget check
        };

        /// The single re-warm worker loop (event-driven cv wake).
        void RewarmWorkerLoop();

        /// Busy/idle marking for the Stop() grace wait (D-11 shape).
        void MarkRewarmBusy();
        void MarkRewarmIdle();

        std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store_;
        std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store_;
        std::shared_ptr<std::mutex>                        cold_store_mutex_;
        std::shared_ptr<std::mutex>                        remote_store_mutex_;
        std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore>    rehydrate_target_;

        // Rate-limited failure logging state (D-03/§3.4: cold failures
        // logged + counted, BlockStatus code only - never endpoint or
        // credential values).
        std::chrono::steady_clock::time_point last_cold_failure_log_{};
        size_t                                suppressed_cold_failures_{ 0 };

        // D-03 transport-failure counters (per tier; surfaced by a future
        // status getter alongside the Phase-8 drainer triple).
        std::atomic<size_t> local_transport_failures_{ 0 };
        std::atomic<size_t> remote_transport_failures_{ 0 };

        // Re-warm executor state (READ-02, plan 09-02). rewarm_mutex_
        // guards the coalescing map + bounded queue - never held across
        // any I/O, so the responder-thread enqueuer is never blocked.
        std::thread                          rewarm_thread_;
        std::atomic_bool                     rewarm_stop_flag_{ false };
        std::mutex                           rewarm_mutex_;
        std::condition_variable              rewarm_cv_;
        std::map<CID, RewarmState>           rewarm_in_flight_;
        std::deque<CID>                      rewarm_queue_;
        std::mutex                           rewarm_idle_mutex_;
        std::condition_variable              rewarm_idle_cv_;
        bool                                 rewarm_idle_{ true };
        std::atomic<size_t>                  rewarm_overflow_drops_{ 0 };
        std::atomic<size_t>                  rewarm_expired_drops_{ 0 };

        // The chain now runs on BOTH the CRDT DAG worker and the re-warm
        // worker - the rate-limiter state needs its own guard.
        std::mutex                           cold_failure_log_mutex_;

        sgns::base::Logger logger_ = sgns::base::createLogger( "ColdReadService" );
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_COLDREADSERVICE_HPP
