/**
 * @file       TxPruner.hpp
 * @brief      PRUNE-04 real prune-cycle service: the first production eraser.
 * @details    Owns the idempotent, crash-consistent erase CYCLE and nothing
 *             else: tick gate (Light node, TransactionManager READY, empty
 *             missing set, settle window elapsed — D-C2), a per-cycle
 *             PruneEligibility::Evaluate() reused VERBATIM (the window
 *             arithmetic's one home — never re-implemented here), a
 *             block-grouped K-bounded erase budget taken whole or deferred
 *             whole (D-04 / Pitfall 1 — same-block siblings are never
 *             stranded behind an exhausted K), a FRESH per-outpoint
 *             spent-ness re-verify immediately before every erase (D-01),
 *             the atomic per-tx erase driver
 *             TransactionManager::EraseTransactionEntryPruned (value XOR
 *             marker in one commit, 18-03), and the per-cycle observability
 *             contract (RECL-03 log + RECL-04 queryable PruneStatus).
 *
 *             Deliberately NOT a GeniusNode member or lambda (the god-class
 *             drift the milestone research forbids): the service depends on
 *             the transaction/crdt/account-service CONTRACTS only, and every
 *             CRDT/service dependency is passed PER CYCLE (D-C4) — the
 *             instance holds only config, the node role, the mutex-guarded
 *             PruneStatus, and the settle clock, so it is generation-safe to
 *             keep alive across Phase-13 account switches. There is no
 *             persistent cursor and no cached evaluator: erased transactions
 *             vanish from enumeration, so the next evaluation simply stops
 *             proposing them.
 * @date       2026-10-21
 */

#ifndef _SGNS_TX_PRUNER_HPP_
#define _SGNS_TX_PRUNER_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>

#include "account/NodeType.hpp"
#include "base/logger.hpp"
#include "outcome/outcome.hpp"
#include "transaction/PruneEligibility.hpp"

namespace sgns
{
    namespace crdt
    {
        class GlobalDB;
    }

    class TransactionManager;
    class UTXOManager;

    /**
     * @brief       Counters of ONE prune cycle (RECL-03; counts only).
     * @details     Per-tx EVIDENCE (reasons, spend heights, block verdicts)
     *              lives in the evaluator's PruneEvaluationResult — the stats
     *              carry the cycle-level arithmetic an operator needs in the
     *              per-cycle INFO line and PruneStatus. Reclaimed BYTES are
     *              never carried here: deletes alone reclaim nothing, and the
     *              measured delta is stamped at compaction time on
     *              PruneStatus::bytes_reclaimed (D-02 — the honest split).
     */
    struct PruneCycleStats
    {
        /// Candidates the evaluator enumerated this cycle (eligible + ineligible).
        uint64_t examined = 0;

        /// Candidates the evaluator marked eligible (PRUNE-01 verdicts).
        uint64_t eligible = 0;

        /// Transactions physically erased through the atomic pruned-erase
        /// driver this cycle (never more than max_erases_per_cycle).
        uint64_t erased = 0;

        /// Erasable block groups DEFERRED whole because the remaining K
        /// budget could not fit the group (D-04 group-or-defer — the
        /// stranding edge, Pitfall 1). Deferred groups retry next cycle.
        uint64_t held_blocks = 0;

        /// Candidates inside taken groups whose FRESH spent-ness re-verify
        /// refused the erase (a spend reverted between evaluation and erase,
        /// or the value could not be re-fetched) — skipped, never erased.
        uint64_t skipped = 0;

        /// Per-tx erase failures inside taken groups (recorded and skipped
        /// past — the cycle continues; the tx stays enumerated for a later
        /// cycle). Per-tx failures are NOT hard errors: the cycle still ran
        /// to completion.
        uint64_t errors = 0;

        /// Wall-clock duration of the cycle in milliseconds.
        uint64_t duration_ms = 0;

        /// The account-generation tag the caller passed for this cycle
        /// (D-C4). Informational: the service cannot observe generations.
        uint64_t generation = 0;

        /// CALLER-OWNED (D-C4): true when the caller detected that the
        /// account generation moved past @ref generation while this cycle
        /// described it. RunCycle itself CANNOT detect supersession (it
        /// never touches the snapshot lifecycle) — the wiring arm checks
        /// before invoking the cycle and tags its own reporting; the flag
        /// stays false as produced by the service.
        bool generation_superseded = false;

        /// True when this cycle's post-cycle compaction gate actually ran a
        /// compaction to completion (D-01). A canceled, skipped, or failed
        /// compaction leaves it false — the completed erase cycle itself
        /// never fails because of compaction.
        bool compacted = false;

        /// Wall-clock duration of the compaction call in milliseconds
        /// (zero when no compaction ran).
        uint64_t compaction_duration_ms = 0;
    };

    /**
     * @brief       Queryable pruning status (RECL-04).
     * @details     A COPY is returned under the status mutex — concurrent
     *              reads are safe while a cycle runs.
     */
    struct PruneStatus
    {
        /// The maxHeight (GlobalDB::GetCRDTHeadList) of the LAST cycle that
        /// ran to completion without hard error — monotonic (never
        /// regresses). K exhaustion, skips, and per-tx errors never block
        /// advancement; aborted/interrupted cycles never advance it.
        /// nullopt until the first completed cycle.
        std::optional<uint64_t> watermark;

        /// Counters of the last cycle that updated the status (gate-skipped
        /// ticks and interrupted cycles leave the previous value in place).
        PruneCycleStats last_cycle;

        /// MEASURED disk bytes reclaimed by the last successful compaction
        /// (D-02): baseline-at-last-compaction size minus post-compaction
        /// size, stamped ONLY at compaction success under the status mutex.
        /// nullopt until the first compaction since restart — deletes alone
        /// reclaim nothing, so this is never stamped at erase time and never
        /// a fabricated number.
        std::optional<uint64_t> bytes_reclaimed;
    };

    /**
     * @brief       The real prune-cycle service (PRUNE-04).
     * @details     Timer-driven ONLY (D-C6): never invoked from CRDT element
     *              callbacks (they fire pre-batch-commit). Construction
     *              fails closed unless the node role is Light (GATE-01 —
     *              the PruneEligibility::New role-gate mirror; full/archive
     *              never erase history); the PruningConfig default-disabled
     *              consent gate is the wiring's duty (SC-5) and is not
     *              re-checked here.
     */
    class TxPruner
    {
    public:
        /**
         * @brief       Fail-closed factory (the construction gate).
         * @details     Mirrors PruneEligibility::New: rejects any role other
         *              than NodeType::Light with std::errc::operation_not_permitted
         *              before any state exists (GATE-01).
         * @param[in]   node_type Deployment role of the constructing node.
         * @param[in]   config Validated pruning policy (the caller —
         *              GeniusNode — owns the consent gate).
         * @return      The service, or outcome::failure for a non-Light role.
         */
        static outcome::result<std::shared_ptr<TxPruner>> New( NodeType node_type, const PruningConfig &config );

        /**
         * @brief       Runs ONE erase cycle over the passed dependencies.
         * @details     Cycle order (research Pattern 1): (1) tick gate —
         *              TransactionManager::GetState() == State::READY,
         *              !HasMissingTxHashes(), and the settle window elapsed
         *              (the clock starts when the gate FIRST fully holds,
         *              skips until settle_seconds after it, and restarts
         *              whenever the gate re-opens — a cycle cannot fire
         *              during initial sync, D-C2); a gate-skipped tick
         *              returns zeroed stats and touches no status. (2) A
         *              per-cycle PruneEligibility is constructed from the
         *              PASSED deps and Evaluate()d VERBATIM — never under
         *              any lock, and never re-implemented (the window
         *              arithmetic's one home). (3) Eligible candidates are
         *              grouped by delta-block CID; a group whose block
         *              report is erasable is taken WHOLE when it fits the
         *              remaining K budget, else DEFERRED whole (held_blocks)
         *              — never a partial group (Pitfall 1). (4) Per tx
         *              inside a taken group: a FRESH spent-ness re-verify
         *              (IsOutPointGenuinelyConsumed per outpoint) runs
         *              immediately before the erase; a reverted spend (or an
         *              unreadable value) skips the tx — never aborts the
         *              cycle; then the atomic
         *              EraseTransactionEntryPruned commits value + marker in
         *              one batch (18-03), with a per-tx failure recorded and
         *              skipped past (log-and-continue is sanctioned ONLY for
         *              this per-tx scan contract). (5) The status is updated
         *              and ONE INFO summary emitted (RECL-03).
         *
         *              GENERATION CONTRACT (D-C4): the supersession check is
         *              the CALLER's pre-erase duty — the wiring arm checks
         *              the generation is still current BEFORE invoking this
         *              cycle (this function is the only erase path, so the
         *              check precedes every erase). @p generation is carried
         *              onto the returned stats and the INFO line as the tag.
         *              Dependencies are never cached: the caller passes a
         *              consistent per-tick snapshot and holds it for the
         *              whole cycle.
         * @param[in]   globaldb Committed-state reader (watermark height).
         * @param[in]   transaction_manager Tick-gate inputs, re-verify
         *              fetch, and the pruned-erase driver.
         * @param[in]   utxo_manager Fresh spent-ness re-verify.
         * @param[in]   generation The caller's account-generation tag.
         * @return      The cycle counters, or outcome::failure: an
         *              invalid_argument-class Evaluate failure (and a
         *              non-Light evaluator construction) is a PERMANENT-stop
         *              signal — see IsPermanentStop — while any other error
         *              is transient (retry next cycle). Failures leave the
         *              status untouched.
         */
        outcome::result<PruneCycleStats> RunCycle( crdt::GlobalDB     &globaldb,
                                                   TransactionManager &transaction_manager,
                                                   UTXOManager        &utxo_manager,
                                                   uint64_t            generation );

        /**
         * @brief       Returns a copy of the current status (RECL-04).
         * @details     Thread-safe: taken under the status mutex, so a
         *              concurrent reader never observes a half-updated
         *              cycle.
         * @return      The watermark + last-cycle counters.
         */
        PruneStatus GetPruneStatus() const;

        /**
         * @brief       Whether a RunCycle error is a permanent-stop class.
         * @details     The 17-06 fail-closed error split re-hosted as ONE
         *              classification home: std::errc::invalid_argument
         *              (Evaluate's fail-closed no-window contract) and
         *              std::errc::operation_not_permitted (a non-Light role
         *              at either factory) are broken policy states no retry
         *              can fix — the caller stops the cycle instead of
         *              spinning on it every 10 minutes. Every other error is
         *              transient (skip this tick, keep the cadence).
         * @param[in]   error A RunCycle failure code.
         * @return      true when the caller must stop instead of reschedule.
         */
        static bool IsPermanentStop( const std::error_code &error );

        /**
         * @brief       Requests cancellation of the post-cycle compaction.
         * @details     Sets the member cancellation atomic the compaction
         *              gate checks BEFORE starting a compaction and the
         *              engine honors mid-flight (the shutdown promptness
         *              path). The atomic is an INSTANCE MEMBER so it
         *              outlives any in-flight CompactRange call — a stack
         *              local would dangle under the engine's asynchronous
         *              reads. Idempotent; never resets (a released pruner
         *              never compacts again).
         */
        void RequestCompactionCancel();

        /// Interrupt seam step: after Evaluate(), before grouping.
        static constexpr int kInterruptAfterEvaluation = 1;
        /// Interrupt seam step: immediately before EACH per-tx erase call
        /// (the first erase included — "between erases" plus the pre-first
        /// position; mid-compose steps are covered by GlobalDB's own seam).
        static constexpr int kInterruptBeforeErase = 2;
        /// Interrupt seam step: after the erase loop, before the status update.
        static constexpr int kInterruptBeforeStatus = 3;
        /// Interrupt seam step: after the status update, before the INFO
        /// summary (the status IS updated at this point).
        static constexpr int kInterruptAfterStatus = 4;

        /**
         * @brief       Test-only interruption seam for the cycle.
         * @param[in]   hook invoked at the steps above; a @c true return
         *              aborts the cycle AT that point. An aborted cycle is
         *              NOT a completed cycle: the status (including the
         *              watermark) is left untouched for steps 1-3 — the
         *              crash-matrix positions 18-06 asserts against. Pass
         *              nullptr to clear.
         * @note        No production caller may invoke this method. Fixture
         *              discipline: a hook armed by a failing case must never
         *              leak further — clear it in the fixture destructor
         *              (the GlobalDB::SetEraseInterruptHookForTest shape).
         */
        static void SetCycleInterruptHookForTest( std::function<bool( int step )> hook );

    private:
        TxPruner( NodeType node_type, PruningConfig config );

        /**
         * @brief       Completion tail shared by every non-aborted cycle.
         * @details     Watermark capture (the monotonic maxHeight of a
         *              cycle that ran to completion without hard error),
         *              the status update under the mutex, and the ONE
         *              RECL-03 INFO summary. The kInterruptAfterStatus seam
         *              fires between the status update and the INFO line.
         * @param[in]   globaldb Head-list reader for the watermark.
         * @param[in,out] stats The cycle counters (duration stamped here,
         *              copied into the status).
         * @param[in]   cycle_start The cycle's start timestamp.
         */
        void RecordCompletedCycle( crdt::GlobalDB                       &globaldb,
                                   PruneCycleStats                      &stats,
                                   std::chrono::steady_clock::time_point cycle_start );

        /**
         * @brief       Post-cycle compaction gate (D-01 — the RunCycle tail).
         * @details     Runs BETWEEN RecordCompletedCycle and the RunCycle
         *              return, so the completed cycle's status is already
         *              recorded and the wiring's reschedule (which fires
         *              only after RunCycle returns) is still held off — the
         *              compaction never overlaps the next tick by
         *              construction. Gate conjuncts, evaluated in order,
         *              each returning with the stats untouched on false:
         *              (1) this cycle erased at least one transaction;
         *              (2) the shutdown cancellation atomic is clear;
         *              (3) the cumulative erased-bytes watermark (an
         *              in-memory estimate assembled from the evaluator's
         *              carried sizes) has crossed
         *              compact_threshold_mb. A canceled or failed
         *              compaction NEVER fails the completed cycle — the
         *              watermark is kept so the next completed cycle
         *              retries naturally. On success, the measured
         *              before/after disk-usage delta stamps
         *              PruneStatus::bytes_reclaimed under the status mutex
         *              (D-02), the watermark and baseline reset, and ONE
         *              INFO line carries the measured numbers.
         * @param[in,out] globaldb The compaction + size-measurement surface.
         * @param[in,out] stats The cycle counters (compacted +
         *              compaction_duration_ms stamped here on success).
         */
        void MaybeCompactAfterCycle( crdt::GlobalDB &globaldb, PruneCycleStats &stats );

        /**
         * @brief       Tick gate + settle-window clock (D-C2).
         * @details     Called FIRST in every cycle, WITHOUT any other lock
         *              held. When the gate does not fully hold the settle
         *              clock resets (restart-on-reopen); the cycle is
         *              refused until the gate has held settle_seconds.
         * @param[in]   transaction_manager State + missing-set inputs.
         * @param[in]   now The cycle's start timestamp.
         * @return      true when the cycle may proceed.
         */
        bool TickGateSettled( TransactionManager &transaction_manager, std::chrono::steady_clock::time_point now );

        /**
         * @brief       FRESH per-outpoint spent-ness re-verify (D-01).
         * @details     Re-fetches the candidate's transaction by its path
         *              and re-checks IsOutPointGenuinelyConsumed for every
         *              produced outpoint — each call is atomic under the
         *              UTXO lock internally — immediately before the erase
         *              call (T-18-0402: a spend that reverted between
         *              evaluation and erase must refuse the erase). A skip
         *              reason is WARN-logged per transaction. This is NOT a
         *              re-evaluation: no window arithmetic and no spender
         *              resolution are recomputed — Evaluate remains their
         *              only home.
         * @param[in]   globaldb Value re-fetch reader.
         * @param[in]   utxo_manager Genuine-spend discriminator.
         * @param[in]   candidate The eligible candidate about to be erased.
         * @return      true when every output is still genuinely consumed.
         */
        bool OutputsRemainGenuinelyConsumed( crdt::GlobalDB             &globaldb,
                                             UTXOManager                &utxo_manager,
                                             const PruneCandidateResult &candidate ) const;

        /// @brief Fires the test seam at @p step; true = abort the cycle.
        static bool InterruptRequested( int step );

        /// Validated policy snapshot (budget K, settle window, finality window).
        PruningConfig config_m;

        /// Deployment role captured at construction (passed to the per-cycle evaluator).
        NodeType node_type_m;

        /// Guards status_m and gate_held_since_ (GetPruneStatus is const).
        mutable std::mutex status_mutex_;

        /// Queryable status (RECL-04) — watermark + last completed cycle.
        PruneStatus status_m;

        /// When the tick gate FIRST fully held (D-C2 settle clock); resets
        /// whenever the gate re-opens.
        std::optional<std::chrono::steady_clock::time_point> gate_held_since_;

        /// Cumulative erased-bytes watermark (D-01): the evaluation-carried
        /// estimate (value bytes + per-tx block share) summed over successful
        /// erases, reset ONLY by a successful compaction. IN-MEMORY ONLY —
        /// never persisted, never a /prune/ cursor; a restart rebuilds it
        /// from zero (worst case one extra early compaction). Never surfaced
        /// as a measured number.
        uint64_t cumulative_erased_bytes_ = 0;

        /// Whole-DB size at the LAST compaction (the reclamation baseline).
        /// Lazily initialized at the first completed cycle after restart or
        /// after a compaction, BEFORE any compaction of the span has run —
        /// bytes_reclaimed therefore measures the full accumulated span
        /// since the last compaction, never a per-cycle under-count. A
        /// failed size read stores nothing and skips compaction this cycle.
        std::optional<uint64_t> compaction_baseline_bytes_;

        /// Cancellation flag threaded into GlobalDB::CompactStorage. MEMBER
        /// scope is load-bearing: the engine's compaction iterator reads it
        /// asynchronously, so it must outlive any in-flight compaction call
        /// (a stack local at the call site would dangle). Set only through
        /// RequestCompactionCancel (the shutdown path) and never reset.
        std::atomic<bool> compaction_canceled_{ false };

        /// Test-only cycle interruption seam (nullptr in production).
        static std::function<bool( int step )> cycle_interrupt_hook_for_test_;

        base::Logger logger_ = base::createLogger( "TxPruner" );
    };
} // namespace sgns

#endif
