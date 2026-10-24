/**
 * @file       ColdStorageManager.hpp
 * @brief      SuperGenius-owned drainer service for the cold-storage
 *             journal (SPILL-02, v1.1 Phase 8).
 *
 * GATED TU HEADER: compiled (and includable) ONLY under
 * SGNS_COLD_STORAGE - it carries sgns-blockstore types. globaldb.hpp and
 * globaldb.cpp reach this class EXCLUSIVELY through the ColdStorageStack
 * bridges (D-10 - no library type crosses the ungated boundary).
 *
 * The library ships a synchronous one-shot drain body (DrainPendingUploads)
 * with a single-threaded-use contract; THIS class owns everything D-01..D-12
 * requires on top, mirroring the twice-proven backup-loop lifecycle pattern
 * in globaldb.cpp (own std::thread, cv poll loop, explicit Stop + join -
 * never io_, never the pubsub asio context):
 *  - poll cadence with exponential backoff (factor 2, cap, reset on first
 *    success; D-03) - partial-success cycles count as failed cycles while
 *    their successes still advance (OQ3)
 *  - dual record-count watermark (warn/critical, D-02) with ALWAYS-ON
 *    transition + heartbeat logging (D-07/D-09)
 *  - atomics-backed status getter (D-08) - never a journal scan
 *  - split-phase DrainOnce: snapshot/fetch and journal-mark phases hold the
 *    SHARED cold-store mutex, the remote Put phase runs UNLOCKED so writes
 *    are never blocked by remote S3 state (D-01)
 *  - ops-triggered drains enqueue (flag + notify), never run inline (SC2)
 *  - the FIRST poll is the crash-restart reconciliation (D-12) -
 *    idempotent re-drain via S3 createOnly convergence
 *  - Stop: flag -> notify -> bounded grace wait -> unconditional join
 *    (D-11; the drainer thread is NEVER separated from its owner)
 */

#ifndef SUPERGENIUS_CRDT_COLDSTORAGEMANAGER_HPP
#define SUPERGENIUS_CRDT_COLDSTORAGEMANAGER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>

#include "ColdStorageStack.hpp"
#include "base/logger.hpp"
#include "globaldb.hpp"

namespace sgns::crdt
{
    class ColdStorageManager final
    {
    public:
        using BackpressureOptions = GlobalDB::ColdStorageOptions::BackpressureOptions;

        /**
         * @brief       Construct the drainer over the shared stores.
         * @param[in]   local_block_store the cold-store journal (the SAME
         *              instance the decorator writes through)
         * @param[in]   remote_block_store the S3 tier receiving drained
         *              blocks (createOnly idempotency)
         * @param[in]   cold_store_mutex the ONE mutex shared with the
         *              decorator (library single-threaded-use contract)
         * @param[in]   options NORMALIZED backpressure knobs (0s resolved
         *              to built-in defaults before this call)
         */
        ColdStorageManager( std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
                            std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store,
                            std::shared_ptr<std::mutex>                        cold_store_mutex,
                            BackpressureOptions                                options );

        ~ColdStorageManager();

        ColdStorageManager( const ColdStorageManager & )            = delete;
        ColdStorageManager &operator=( const ColdStorageManager & ) = delete;
        ColdStorageManager( ColdStorageManager && )                 = delete;
        ColdStorageManager &operator=( ColdStorageManager && )      = delete;

        /** @brief Spawn the drainer thread; first action is the
         *         crash-restart reconciliation poll (D-12). Idempotent. */
        void Start();

        /** @brief Stop with bounded grace then unconditional join (D-11). */
        void Stop();

        /**
         * @brief      Enqueue an ops-triggered drain: flag + notify ONLY -
         *             the drain body never runs on the caller's thread.
         */
        void RequestDrain();

        /**
         * @brief      D-08 status triple read from atomics - no journal
         *             scan, no lock.
         * @return     { pending record count, watermark, drainer health }
         */
        ColdStorageStatus GetStatus() const;

    private:
        /// Backoff multiplier per failed cycle (D-03; compile-time, D-05).
        static constexpr uint32_t kBackoffFactor = 2;
        /// Shutdown grace window for in-flight cycle work (D-11; NOT a
        /// knob - D-05).
        static constexpr std::chrono::milliseconds kShutdownGraceWindow{ 5000 };
        /// Rate limit for the held-watermark heartbeat line (D-09).
        static constexpr std::chrono::milliseconds kHeartbeatPeriod{ 60000 };

        /// The per-poll drain body (split-phase, D-01).
        void DrainOnce();

        /// A cycle with any failure (outer or per-item) backs off (D-03).
        void RecordFailedCycle();

        /// First success resets the delay to the floor and clears the
        /// failure streak / health flag (D-03).
        void BackoffReset();

        /// Update the pending-count atomic, watermark transitions, and the
        /// held-watermark heartbeat (D-02/D-07/D-08/D-09).
        void UpdateWatermark( size_t pending_after );

        /// Mark the drainer busy/idle for the Stop() grace wait (D-11).
        void MarkBusy();
        void MarkIdle();

        std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store_;
        std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store_;
        std::shared_ptr<std::mutex>                        cold_store_mutex_;
        BackpressureOptions                                options_;

        std::thread              thread_;
        std::atomic_bool         stop_flag_{ false };
        std::atomic_bool         drain_requested_{ false };
        std::mutex               wait_mutex_;
        std::condition_variable  wait_cv_;
        std::mutex               idle_mutex_;
        std::condition_variable  idle_cv_;
        bool                     idle_{ true };

        // Drainer-thread-only state (no synchronization needed): current
        // backoff delay seeded to the drain-interval floor, failure
        // streak, and the heartbeat clock.
        std::chrono::milliseconds              current_delay_{ 0 };
        uint32_t                               failure_streak_{ 0 };
        std::chrono::steady_clock::time_point  last_heartbeat_{};

        // Status atomics (D-08): read lock-free by GetStatus.
        std::atomic<size_t>              pending_count_{ 0 };
        std::atomic<ColdStorageWatermark> watermark_{ ColdStorageWatermark::None };
        std::atomic<bool>                healthy_{ false };

        sgns::base::Logger logger_ = sgns::base::createLogger( "ColdStorageManager" );
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_COLDSTORAGEMANAGER_HPP
