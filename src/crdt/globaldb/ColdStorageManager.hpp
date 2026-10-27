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
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>

#include "ColdStorageStack.hpp"
#include "base/logger.hpp"
#include "globaldb.hpp"

namespace sgns::crdt
{
    class CrdtDatastore;      // GC dependency (attached post-construction)
    class GraphsyncDAGSyncer; // GC dependency (attached post-construction)

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

        /**
         * @brief       Retention-GC knobs (GC-01, v1.1 Phase 11).
         *
         * retention_age_ms is REQUIRED to activate the GC: 0 means the
         * GC actor is never constructed/run (absent = off = byte-identical
         * node behavior, D-02). The two cadence knobs follow the
         * 0-equals-default discipline and normalize to kDefaultGcIntervalMs
         * / kDefaultGraceMs at construction. dry_run parses here and only
         * suppresses deletes from 11-02 onward (every Phase-11 Wave-1 path
         * is observe-only by construction).
         */
        struct RetentionOptions
        {
            uint64_t retention_age_ms{ 0 }; ///< the age threshold (0 = GC off; NO default - D-04)
            bool     dry_run{ false };      ///< report candidates without deleting (11-02+)
            uint32_t gc_interval_ms{ 0 };   ///< pass cadence (0 = kDefaultGcIntervalMs)
            uint32_t grace_ms{ 0 };         ///< evicting->delete grace (0 = kDefaultGraceMs)
        };

        /// Default GC pass cadence (10 min) when gc_interval_ms == 0.
        static constexpr uint32_t kDefaultGcIntervalMs{ 600000 };
        /// Default evicting->delete grace (1 min) when grace_ms == 0.
        static constexpr uint32_t kDefaultGraceMs{ 60000 };
        /// SC5 (GC-01): the PINNED hot-store prefix holding GC's node-local
        /// epoch records - every key under it is pinned alongside heads
        /// (never a candidate, never deleted; public for test citation).
        static constexpr const char *kGcPinnedPrefix{ "/crdt/gc/" };
        /// SC5: the epoch watermark record's hot-store key (node-local,
        /// non-replicated; layout documented on WriteEpochWatermark).
        static constexpr const char *kGcEpochWatermarkKey{ "/crdt/gc/epoch-watermark" };
        /// DisposedRecord::reason code for retention-policy evictions
        /// (public for test citation; the only v1.1 reason).
        static constexpr uint8_t kEvictionReasonRetention{ 1 };

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
        /// D-12 staging-health threshold: bufferUsed above this fraction
        /// of a NON-ZERO bufferLimit reads staging unhealthy (public for
        /// test citation - Phase-9 D-12 constant precedent). bufferLimit
        /// == 0 means UNLIMITED (openapi verbatim) - the clause is
        /// vacuously true there.
        static constexpr double kStagingBufferFraction = 0.8;
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
         * @param[in]   retention_options retention-GC knobs (retention_age_ms
         *              == 0 = GC never runs - the D-02 absent=off posture;
         *              0 cadence knobs normalized here to the built-in
         *              defaults)
         */
        ColdStorageManager( std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
                            std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store,
                            std::shared_ptr<std::mutex>                        cold_store_mutex,
                            std::shared_ptr<std::mutex>                        remote_store_mutex,
                            BackpressureOptions                                options,
                            DurabilityOptions                                  durability_options = {},
                            std::shared_ptr<sgns::blockstore::S3AdminClient>    admin_client       = nullptr,
                            RetentionOptions                                   retention_options = {} );

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

        /**
         * @brief      D-13 on-demand staging-stats refresh: ONE bounded
         *             GetUploadStats plus predicate re-evaluation,
         *             serialized against the drainer tick's admin calls
         *             via admin_mutex_ (no poller thread).
         */
        void RefreshStagingStats();

        /**
         * @brief      TEST-ONLY mid-pass hook (the library's
         *             SetCrashPointForTesting precedent): invoked between
         *             the pass-start heads snapshot/walk and the
         *             heads-equality finalization - the seam the
         *             heads-change abort case uses to mutate heads inside
         *             the pass window. PRODUCTION CODE MUST NEVER CALL
         *             SetGcMidPassHookForTesting.
         * @param[in]  hook the callback (null clears)
         */
        void SetGcMidPassHookForTesting( std::function<void()> hook );

        /**
         * @brief      Attach the retention-GC dependencies (GC-01, v1.1
         *             Phase 11 - the §6.1 post-construction seam).
         *
         * The manager is constructed BEFORE CrdtDatastore exists (the
         * GlobalDB::Init order), so the GC's heads/walk/hot-store
         * dependencies attach here, between construction and Start().
         * Call between construction and Start(); reaching Start() with
         * retention configured but dependencies unattached is a
         * fail-closed programmer error (no GC thread, one error-level
         * line).
         * @param[in]  crdt_datastore the heads source (GetHeadList - the
         *             pass universe and the exactness re-check)
         * @param[in]  dag_syncer the DAG read path (GetNodeWithoutRequest
         *             only - hot-first then the Phase-9 cold chain)
         * @param[in]  hot_store the hot CRDT RocksDB face (the same
         *             handle GlobalDB owns; the /crdt/gc/ pinned namespace
         *             lives here)
         */
        void AttachGcDependencies( CrdtDatastore                     *crdt_datastore,
                                   GraphsyncDAGSyncer                 *dag_syncer,
                                   std::shared_ptr<storage::BufferStorage> hot_store );

        /**
         * @brief      ONE retention-GC pass (GC-01 - observe-only in
         *             Phase 11 Wave 1; deletion machinery lands in 11-02).
         *
         * The fixed tick order: (0) stop check; (1) RefreshStagingStats +
         * staging_healthy_ gate (unhealthy => inhibited tick, zero
         * candidates); (2) heads snapshot via GetHeadList; (3) FRESH full
         * reachability walk (every link traversed unconditionally - no
         * resolution skips, Pitfall 1); (4) eligibility over
         * GetDurableWithSpillTime under cold_store_mutex_ (journal is
         * truth, MI-01): age STRICTLY past retention (ties and
         * future-dated stamps read young), absent from the reachable set,
         * journal state EXACTLY kEvictionDurabilityGate (MI-02);
         * missing timestamps backfill to now and wait one full period;
         * (5) heads-equality re-check - ANY difference aborts the pass
         * with zero candidates (the §2.2 exactness lemma, under-eviction);
         * (6) gc status atomics updated, one per-pass summary line.
         *
         * Thread-safe to call from the GC thread (its normal home) or
         * directly from tests (the driven-pass seam).
         * @return     the pass's candidate CIDs (empty = inhibited, aborted,
         *             or genuinely zero candidates)
         */
        std::vector<sgns::blockstore::Cid> GcPassOnce();

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

        /// D-12 staging-health predicate: evaluates the conservative
        /// three-clause rule over the observed stats and writes the
        /// staging atomics (called from the drainer tick AND
        /// RefreshStagingStats - every field it touches is atomic).
        void UpdateStagingHealth( const sgns::blockstore::UploadStats &stats,
                                  bool                              admin_reachable );

        /// GC-01 (v1.1 Phase 11): the fresh full reachability walk from a
        /// heads snapshot - iterative deque work queue + visited set
        /// (the TraverseCIDsLinks mechanics ONLY; its isResolved skips
        /// are FORBIDDEN - under-counting reachability is the
        /// wrongful-eviction direction, Pitfall 1). Per-node reads via
        /// GetNodeWithoutRequest only (dagMutex_ held only inside
        /// GetNodeFromMerkleDAG - never across batches), stop-flag checks
        /// every 256-1024 nodes. Returns {reachable set, walk-exact}; a
        /// walk gap (a link missing from every tier) returns exact=false
        /// so the caller aborts the pass (reachability not computable
        /// => under-eviction, never a guess).
        std::pair<std::unordered_set<CID>, bool> WalkReachable(
            const CrdtHeads::CRDTHeadList &snapshot );

        /// GC-01: true when the two heads snapshots differ in ANY topic
        /// or CID (the §2.2 exactness lemma's abort predicate).
        static bool HeadsDiffer( const CrdtHeads::CRDTHeadList &lhs,
                                 const CrdtHeads::CRDTHeadList &rhs );

        /// GC-02 (D-13/D-14/D-15): ONE in-memory evicting mark - the
        /// GC-thread-only (single-writer) two-phase state. Carries
        /// everything the destructive step needs so NOTHING is re-derived
        /// after the grace: the pass-start heads snapshot it rests on
        /// (the D-14 delete-time exactness check compares CURRENT heads
        /// against THIS), the parsed delta row keys, the size, and the
        /// seal state - all captured BEFORE anything is deleted. Crashes
        /// vanish with the process (D-15): nothing was deleted, the next
        /// pass re-walks and re-marks from scratch.
        struct EvictingMark
        {
            EvictingMark() = delete; // Cid has no default constructor
            explicit EvictingMark( const sgns::blockstore::Cid &c ) : cid( c ) {}

            sgns::blockstore::Cid      cid;
            uint64_t                   marked_at_unix_ms{ 0 };
            uint64_t                   size_bytes{ 0 };
            bool                       was_sealed{ false };
            std::vector<std::string>   hot_row_keys;
                       uint64_t                   mark_tick{ 0 };
            CrdtHeads::CRDTHeadList    heads_snapshot;
        };

        /// GC-02: the evaluate half of a pass (steps 2-5b of the 11-01
        /// tick order + candidate marking). Returns the pass's
        /// candidates (empty = inhibited, aborted, or zero).
        std::vector<sgns::blockstore::Cid> EvaluatePass();

        /// GC-02 (D-14/D-15/D-16): process marks whose grace elapsed ON A
        /// LATER TICK - per mark, the delete-time heads-equality re-check
        /// against the MARK'S snapshot (ANY difference cancels: clear the
        /// mark, keep the bytes, the CID returns to normal), then unless
        /// dry_run the D-16 two-half eviction (library batch, then the
        /// idempotent hot cleanup + LRU invalidation).
        void ProcessDueMarks();

        /// GC-02 (§4.2): FIRST step of every pass - enumerate the
        /// disposed markers and, for each whose hot binary key is still
        /// present, complete HALF 2 only (the crash-between-halves
        /// recovery; the library half already committed). Row keys are
        /// recomputed from the REMOTE copy (the cold-dir bytes are gone).
        void FinishOrphanedHotCleanups();

        /// GC-02 (D-10): enforce both disposed-marker bounds and refresh
        /// gc_disposed_marker_count_.
        void TrimDisposedMarkers();

        /// GC-02 (D-16 HALF 2): ONE hot-store batch deleting the binary
        /// CID key, the text <cid>sealed key, and EXACTLY the given row
        /// keys (computed from the delta content, never enumerated),
        /// followed by DeleteCIDBlock LRU invalidation (A6: AFTER the
        /// deletes, so HasBlock never lies).
        void RunHotCleanup( const sgns::blockstore::Cid       &cid,
                            const std::vector<std::string> &row_keys );

        /// SC5 (GC-01): compute and store the per-topic epoch watermark
        /// record from the pass-start heads snapshot - min-head-height
        /// floor, max height, computed-at unix-ms - as ONE node-local,
        /// non-replicated record under the PINNED /crdt/gc/ hot-store
        /// prefix via the attached hot face (a DIRECT hot-store record -
        /// never through the CRDT datastore Put path, which would publish
        /// a block). NOTHING in v1.1 is deletion-gated on it.
        ///
        /// Record layout (internal, node-local, fixed): little-endian
        /// [u64 computed_at_unix_ms][u32 topic_count] then per topic
        /// [u16 name_len][name bytes][u64 min_height][u64 max_height].
        /// A height-read failure skips that head conservatively (the
        /// watermark is advisory bookkeeping in v1.1); a write failure is
        /// one rate-limited warn, never a pass abort.
        void WriteEpochWatermark( const CrdtHeads::CRDTHeadList &snapshot );

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

        // SC4 staging health (D-12/D-13): the predicate evaluates on the
        // drainer tick and on demand; every status-read field is atomic.
        std::atomic<uint64_t>                  staging_pending_objects_{ 0 };
        std::atomic<uint64_t>                  staging_buffer_used_{ 0 };
        std::atomic<uint64_t>                  staging_buffer_limit_{ 0 };
        std::atomic<uint64_t>                  staging_failed_uploads_{ 0 };
        std::atomic<bool>                      staging_healthy_{ false };
        /// Last-observed failedUploads (the flat-delta clause's
        /// baseline; atomic - refresh may run off the drainer thread).
        std::atomic<uint64_t>                  previous_failed_uploads_{ 0 };
        std::atomic<bool>                      admin_reachable_last_tick_{ false };
        /// D-13: serializes admin-client use between the drainer tick's
        /// calls and RefreshStagingStats (the only contended object).
        std::mutex                             admin_mutex_;

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

        //
        // Retention GC (GC-01, v1.1 Phase 11): D-06 - the GC runs on its
        // OWN std::thread with its own stop-flag/cv (never io_, never the
        // pubsub asio context), mirroring the drainer lifecycle shape.
        // The Stop() join slots AFTER the drainer join and the Phase-10
        // final flush.
        //
        /// NORMALIZED retention knobs (0s resolved in the ctor).
        RetentionOptions retention_;
        /// True when retention_age_ms > 0 (ctor-fixed, immutable).
        bool gc_enabled_{ false };
        /// Mirrors retention_.dry_run (ctor-fixed, immutable).
        bool gc_dry_run_{ false };
        /// §6.1 post-construction dependencies (raw: the GlobalDB owns
        /// them and outlives Stop(); never dereferenced before attach).
        CrdtDatastore                     *gc_crdt_datastore_ = nullptr;
        GraphsyncDAGSyncer                *gc_dag_syncer_ = nullptr;
        std::shared_ptr<storage::BufferStorage> gc_hot_store_;
        std::thread              gc_thread_;
        std::atomic_bool         gc_stop_flag_{ false };
        std::mutex               gc_wait_mutex_;
        std::condition_variable  gc_cv_;
        /// TEST-ONLY mid-pass hook (null in production; see the setter).
        std::function<void()>    gc_mid_pass_hook_;
        /// Serializes gc_mid_pass_hook_ swaps against a running pass.
        std::mutex               gc_hook_mutex_;
        /// Rate-limit state for the inhibited/aborted gc: lines (atomic:
        /// GcPassOnce may run on the GC thread or a test thread).
        std::atomic<uint32_t>    gc_inhibit_streak_{ 0 };
        std::atomic<uint32_t>    gc_abort_streak_{ 0 };

        // GC-02 (v1.1 Phase 11): the in-memory evicting marks + tick
        // counter (GC-thread-only / single-writer - GcPassOnce never
        // overlaps itself) and the per-pass reclaimed accumulators.
        std::vector<EvictingMark> evicting_marks_;
        uint64_t                  gc_tick_counter_{ 0 };
        size_t                    pass_reclaimed_count_{ 0 };
        uint64_t                  pass_reclaimed_bytes_{ 0 };
        /// The last completed evaluation's heads snapshot (the SC5
        /// watermark input; GC-thread-only like the marks).
        CrdtHeads::CRDTHeadList    last_pass_snapshot_;

        // GC status atomics (D-08 discipline: read lock-free by
        // GetStatus). The evicting/reclaimed/disposed fields stay zero
        // until 11-02's destructive half lands.
        std::atomic<bool>        gc_inhibited_staging_{ false };
        std::atomic<size_t>      gc_evicting_count_{ 0 };
        std::atomic<uint64_t>    gc_last_pass_unix_ms_{ 0 };
        std::atomic<size_t>      gc_last_pass_candidates_{ 0 };
        std::atomic<size_t>      gc_last_pass_reclaimed_count_{ 0 };
        std::atomic<uint64_t>    gc_last_pass_reclaimed_bytes_{ 0 };
        std::atomic<size_t>      gc_disposed_marker_count_{ 0 };

        sgns::base::Logger logger_ = sgns::base::createLogger( "ColdStorageManager" );
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_COLDSTORAGEMANAGER_HPP
