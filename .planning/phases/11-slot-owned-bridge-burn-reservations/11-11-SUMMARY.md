---
phase: 11-slot-owned-bridge-burn-reservations
plan: 11
subsystem: blockchain-consensus
tags: [consensus, bridge, burn-reservation, terminal-safety, restart-recovery]
requires:
  - phase: 11-10
    provides: exact durable CONSUMED completion gate and irreconcilable finalized payload classification
  - phase: 11-07
    provides: atomic certified mint effects and reservation consumption
provides:
  - explicit consumed-derived terminal safety state retaining physical burn consumption
  - exact identity-bound monotonic contradiction transition with reciprocal protection
  - restart and duplicate-delivery suppression for consumed artifact contradictions
affects: [phase-12, bridge-mint-recovery, consensus-safety-audit]
tech-stack:
  added: []
  patterns: [consumed-derived-terminal-state, terminal-before-recovery-scheduling, exact-idempotent-safety-transition]
key-files:
  created: []
  modified:
    - src/blockchain/impl/proto/ConsensusLocalState.proto
    - src/blockchain/ConsensusStateStore.cpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_burn_reservation_test.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp
    - test/src/account/utxo_manager_test.cpp
key-decisions:
  - "Post-consumption contradictions use CONSUMED_SAFETY_ERROR instead of overwriting the durable fact of physical consumption with ordinary SafetyError."
  - "Both terminal reservation states retain their reciprocal outpoint index and exact finality identity; only same-diagnostic replay is idempotent."
  - "Startup installs terminal safety lifecycle before certificate recovery selection, while duplicate ingress short-circuits before handler lookup or cleanup."
patterns-established:
  - "Irreconcilable application state advances FinalizedPendingApplication to SafetyError and Consumed to ConsumedSafetyError under the same exact-identity API."
  - "Terminal reservation recovery leaves process evidence durable but excludes it from handler scheduling and resource-bearing side effects."
requirements-completed:
  - BURN-03
  - BURN-04
duration: 30 min
completed: 2026-07-29
---

# Phase 11 Plan 11: Consumed Terminal Safety Recovery Summary

**Exact-winner artifact contradictions discovered after physical burn consumption now persist a consumed-derived terminal state and remain non-releasable and non-retryable across restart.**

## Performance

- **Duration:** 30 min
- **Started:** 2026-07-29T17:43:04Z
- **Completed:** 2026-07-29T18:13:08Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments

- Added strict `CONSUMED_SAFETY_ERROR` schema and validation semantics without changing the clean v2 local-state version policy.
- Made terminal safety transitions atomic, reciprocal-preserving, identity-bound, diagnostic-bound, and failure-preserving for both pre- and post-consumption contradictions.
- Restored terminal lifecycle before recovery scheduling and short-circuited later handler registration, timer reconciliation, and duplicate certificate ingress without completion, cleanup, dependency wake, release, or remint.
- Extended UTXO and production TransactionManager coverage for missing application records, missing winner outputs, conflicting output order, and consumed-input inconsistency.

## Task Commits

Each task was committed atomically:

1. **Task 1: Add a strict consumed-terminal safety transition** - `4409ffba` (feat)
2. **Task 2: Drive consumed artifact corruption through restart recovery and stop retries** - `d21832a6` (fix)

## Files Created/Modified

- `src/blockchain/impl/proto/ConsensusLocalState.proto` - Explicit consumed-derived terminal reservation enum.
- `src/blockchain/ConsensusStateStore.cpp` - Strict validation and atomic exact-identity terminal transition.
- `src/blockchain/Consensus.cpp` - Terminal-aware startup, finalization, and restored-work suppression.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Store transition, reopen, restart, duplicate-ingress, no-cleanup, and no-release evidence.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Production callback mapping from consumed artifact contradiction to irreconcilable.
- `test/src/account/utxo_manager_test.cpp` - Missing/conflicting post-consumption artifact detection with reservation protection.

## Decisions Made

- Preserved the physical-consumption fact with a distinct terminal enum rather than weakening `CONSUMED` or mapping it back to pre-consumption safety.
- Kept the reciprocal outpoint record indefinitely for terminal states so neither another winner nor a recreated lifecycle can claim the burn.
- Reused the existing exact finality identity API and process evidence; recovery suppression is derived from the durable terminal reservation instead of deleting or falsely completing the process.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added the generated bridge-application protobuf include to the expanded UTXO test**

- **Found during:** Task 2 three-target build
- **Issue:** The new raw artifact-corruption helper referenced `SGTransaction::BridgeApplicationRecord` without directly including its generated header.
- **Fix:** Added `account/proto/SGTransaction.pb.h` to the focused UTXO test.
- **Files modified:** `test/src/account/utxo_manager_test.cpp`
- **Verification:** The target rebuilt successfully; the focused UTXO test and both guarded closures passed.
- **Committed in:** `d21832a6`

---

**Total deviations:** 1 auto-fixed (1 blocking issue). **Impact:** Test-only include correction; no production or scope change.

## Issues Encountered

- The protobuf enum change required regeneration and a broad dependent C++ rebuild; it completed cleanly.
- CRDT-backed test output is verbose, but all focused and guarded targets completed deterministically and sequentially.

## User Setup Required

None - no external service configuration required.

## Verification

- Task 1 exact discovery found all 3 required store tests; the focused slice passed 4/4.
- Task 2 exact discovery found the required consensus and TransactionManager tests once each; the UTXO, TransactionManager, and consensus focused slices passed 1/1 each.
- Exact Phase 11 discovery reported 9 targets and the guarded suite passed 9/9 in 152.49 seconds.
- Exact Phase 10 compatibility discovery reported 8 targets and the guarded suite passed 8/8 in 110.82 seconds.
- Source terminal-branch audit and `git diff --check` passed.

## Next Phase Readiness

- Phase 11's durable completion and post-consumption terminal-safety gaps are closed.
- Phase 12 can rely on non-releasable consumed-terminal protection while exercising the full consensus race and compatibility matrix.
- No blockers; protected user-owned dirty paths remain untouched.

## Self-Check: PASSED

- Task commits `4409ffba` and `d21832a6` exist.
- All six planned modified files are present and committed.
- All task acceptance criteria and plan-level focused, 9-target, and 8-target verification gates passed.
- No WR-03 handler replacement, mock RPC, Phase 12 race, migration path, or unrelated production source change was introduced.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-29*
