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
 *  - flush-gated durability advancement (DUR-01, v1.1 Phase 10): every
 *    drainer cycle follows DrainOnce with AdvanceDurabilityOnce - POST
 *    /objects/flush plus a pendingObjects==0 observation advances the
 *    pre-flush UploadedAccepted snapshot to UploadedDurable on the s3d
 *    admin facade's evidence, never on accepted-is-durable assumption
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
#include <sgns-blockstore/S3AdminClient.hpp>
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
         * @brief       Flush-cadence knobs for the flush-gated durability
         *              advancement (DUR-01, v1.1 Phase 10).
         *
         * 0 means "built-in default" and is normalized at construction
         * to kDefaultFlushMinIntervalMs / kDefaultFlushMaxIntervalMs
         * (the BackpressureOptions 0-equals-default discipline). The
         * hybrid cadence (D-01): a flush fires when the floor elapsed
         * AND the cycle is quiescent, OR when the max-interval backstop
         * elapsed regardless of quiescence.
         */
        struct DurabilityOptions
        {
            uint32_t flush_min_interval_ms{ 0 }; ///< floor between flush attempts
            uint32_t flush_max_interval_ms{ 0 }; ///< backstop: flush at least this often
        };

        /// @brief The eviction-safety durability gate (D-10): a local
        ///        block's bytes are evictable ONLY when its journal
        ///        record is UploadedDurable. Compile-time constant - no
        ///        config knob can lower it (never accept-is-durable).
        static constexpr sgns::blockstore::UploadState kEvictionDurabilityGate =
            sgns::blockstore::UploadState::UploadedDurable;

        /// Default flush floor (30 s) when flush_min_interval_ms == 0.
        static constexpr std::chrono::milliseconds kDefaultFlushMinIntervalMs{ 30000 };
        /// Default flush backstop (5 min) when flush_max_interval_ms == 0.
        static constexpr std::chrono::milliseconds kDefaultFlushMaxIntervalMs{ 300000 };
        /// Read budget for a cadence-tick admin Flush call (s3d has NO
        /// server-side deadline - this is the only bound; Pitfall 4).
        static constexpr std::chrono::milliseconds kCadenceFlushReadTimeoutMs{ 30000 };
        /// Shorter read budget for the Stop() final-flush attempt (D-04)
        /// - shutdown must stay bounded.
        static constexpr std::chrono::milliseconds kFinalFlushBudgetMs{ 2000 };
        /// Cap of the flush-failure backoff (compile-time, NOT a knob).
        static constexpr std::chrono::milliseconds kFlushBackoffCapMs{ 60000 };
        /// Shutdown grace window for in-flight cycle work (D-11; NOT a
        /// knob - public for the bounded-Stop test citation).
        static constexpr std::chrono::milliseconds kShutdownGraceWindow{ 5000 };

        /**
         * @brief       Construct the drainer over the shared stores.
         * @param[in]   local_block_store the cold-store journal (the SAME
         *              instance the decorator writes through)
         * @param[in]   remote_block_store the S3 tier receiving drained
         *              blocks (createOnly idempotency)
         * @param[in]   cold_store_mutex the ONE mutex shared with the
         *              decorator (library single-threaded-use contract)
         * @param[in]   remote_store_mutex the ONE mutex serializing ALL
         *              S3BlockStore access (shared with the Phase-9
         *              cold-read chain; taken per-Put-item in the drainer's
         *              remote phase - 09-RESEARCH §6.3)
         * @param[in]   options NORMALIZED backpressure knobs (0s resolved
         *              to built-in defaults before this call)
         * @param[in]   durability_options flush-cadence knobs (0s
         *              normalized here to the built-in defaults)
         * @param[in]   admin_client the s3d admin facade (nullptr = no
         *              flush-gated advancement at all - the Phase-8
         *              5-argument posture, drain-to-Accepted only)
         */
        ColdStorageManager( std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
                            std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store,
                            std::shared_ptr<std::mutex>                        cold_store_mutex,
                            std::shared_ptr<std::mutex>                        remote_store_mutex,
                            BackpressureOptions                                options,
                            DurabilityOptions                                  durability_options = {},
                            std::shared_ptr<sgns::blockstore::S3AdminClient>    admin_client       = nullptr );

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
         * @brief      Tell the manager whether the admin credential was
         *             configured (planner ruling 1 / 10-RESEARCH §5.3).
         *
         * The manager NEVER sees the password value - only its presence,
         * so Start() can emit the one-time missing-credential WARN. The
         * stack calls this right after construction; default is present
         * (no WARN) for direct constructions that carry no admin client
         * posture.
         * @param[in]  present true when SGNS_BLOCKSTORE_S3_ADMIN_PASSWORD
         *             resolved to a non-empty value
         */
        void SetAdminPasswordPresent( bool present );

        /**
         * @brief      D-08 status read from atomics - no journal
         *             scan, no lock. Since Phase 10 the triple carries
         *             the flush-gated durability evidence (accepted /
         *             durable counts and the last-flush observation).
         * @return     { journal depth + watermark + health, accepted /
         *             durable counts, last-flush evidence }
         */
        ColdStorageStatus GetStatus() const;

        /**
         * @brief      ONE flush-gated durability advancement attempt
         *             (DUR-01 SC1; runs on the drainer thread after
         *             DrainOnce, before MarkIdle).
         *
         * Split-phase lock discipline (the DrainOnce shape): the
         * Accepted snapshot is taken under cold_store_mutex_, the admin
         * Flush + GetUploadStats calls run UNLOCKED (a new connection -
         * remote_store_mutex_ is not involved), and the marks re-lock.
         * A record is marked Durable ONLY on observed evidence - flush
         * succeeded AND pendingObjects == 0 - marking EXACTLY the
         * pre-flush snapshot (D-03). Every other outcome leaves records
         * Accepted and engages the separate flush backoff.
         *
         * @param[in]  quiescent_this_cycle DrainOnce's own quiescence
         *             signal (empty enumeration or all-succeeded cycle)
         */
        void AdvanceDurabilityOnce( bool quiescent_this_cycle );

        /**
         * @brief      Per-CID upload-state pass-through (D-09) under
         *             cold_store_mutex_ (library single-threaded-use
         *             contract against the publisher).
         * @param[in]  cid the block whose journal state to read
         * @return     the library UploadState result (no record reads
         *             as the library's NotFound failure)
         */
        sgns::blockstore::result<sgns::blockstore::UploadState, sgns::blockstore::BlockStatus>
        GetUploadState( const sgns::blockstore::Cid &cid );

    private:
        /// Backoff multiplier per failed cycle (D-03; compile-time, D-05).
        static constexpr uint32_t kBackoffFactor = 2;
        /// Rate limit for the held-watermark heartbeat line (D-09).
        static constexpr std::chrono::milliseconds kHeartbeatPeriod{ 60000 };

        /// The per-poll drain body (split-phase, D-01). Returns the
        /// cycle's quiescence signal (DUR-01): true when the pending
        /// enumeration was empty OR every item succeeded.
        bool DrainOnce();

        /// A cycle with any failure (outer or per-item) backs off (D-03).
        void RecordFailedCycle();

        /// First success resets the delay to the floor and clears the
        /// failure streak / health flag (D-03).
        void BackoffReset();

        /// Flush-failure backoff twin (D-07, SEPARATE state - Pitfall 6):
        /// a failed flush cycle never poisons the drain backoff and vice
        /// versa. Doubles flush_delay_ capped at kFlushBackoffCapMs.
        void RecordFlushFailedCycle( const std::string &context );

        /// Flush-success twin: clears the flush failure streak and
        /// returns flush_delay_ to the cadence floor.
        void BackoffResetFlush();

        /// A completed observation whose evidence was NOT met
        /// (pendingObjects != 0): records valid-but-not-ok evidence and
        /// resets nothing - records stay Accepted for the next cycle.
        void RecordFlushNoEvidence( uint64_t observed_pending );

        /// Update the pending-count atomic, watermark transitions, and the
        /// held-watermark heartbeat (D-02/D-07/D-08/D-09).
        void UpdateWatermark( size_t pending_after );

        /// Mark the drainer busy/idle for the Stop() grace wait (D-11).
        void MarkBusy();
        void MarkIdle();

        std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store_;
        std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store_;
        std::shared_ptr<std::mutex>                        cold_store_mutex_;
        /// READ-01 (v1.1 Phase 9): serializes remote Puts against the
        /// cold-read chain's S3 Gets (single-threaded-use contract).
        std::shared_ptr<std::mutex>                        remote_store_mutex_;
        BackpressureOptions                                options_;

        /// DUR-01 (v1.1 Phase 10): the s3d admin facade driving the
        /// flush-gated advancement (nullptr = no advancement).
        std::shared_ptr<sgns::blockstore::S3AdminClient>   admin_client_;
        /// NORMALIZED flush-cadence knobs (0s resolved in the ctor).
        DurabilityOptions                                  durability_;

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

        // Flush-cadence state (drainer-thread-only, DUR-01): SEPARATE
        // from the drain backoff above (Pitfall 6 - neither poisons the
        // other). Clocks seed to construction time so the floor gates
        // even the first attempt.
        std::chrono::steady_clock::time_point  last_flush_attempt_{};
        std::chrono::steady_clock::time_point  last_advancement_{};
        uint32_t                               flush_failure_streak_{ 0 };
        std::chrono::milliseconds              flush_delay_{ 0 };
        /// Whether the admin credential was configured (presence only -
        /// NEVER the value; drives the one-time Start() WARN).
        bool                                   admin_password_present_{ true };
        /// D-04: Stop() is idempotent and the destructor calls it too -
        /// the guard makes the final-flush attempt exactly-once.
        std::atomic<bool>                      final_flush_done_{ false };

        // Status atomics (D-08): read lock-free by GetStatus.
        std::atomic<size_t>              pending_count_{ 0 };
        std::atomic<ColdStorageWatermark> watermark_{ ColdStorageWatermark::None };
        std::atomic<bool>                healthy_{ false };
        // Flush-gated durability evidence (D-08/D-09, v1.1 Phase 10).
        std::atomic<size_t>              accepted_count_{ 0 };
        std::atomic<size_t>              durable_count_{ 0 };
        std::atomic<bool>                last_flush_valid_{ false };
        std::atomic<bool>                last_flush_ok_{ false };
        std::atomic<uint64_t>            last_flush_unix_ms_{ 0 };
        std::atomic<uint64_t>            last_flush_pending_objects_{ 0 };
        /// D-07/Pitfall 6: mirror of flush_failure_streak_ > 0 for the
        /// lock-free status getter (the streak itself is drainer-only).
        std::atomic<bool>                flush_backoff_active_{ false };

        sgns::base::Logger logger_ = sgns::base::createLogger( "ColdStorageManager" );
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_COLDSTORAGEMANAGER_HPP
