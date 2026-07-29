---
phase: 11-slot-owned-bridge-burn-reservations
plan: 10
subsystem: blockchain-consensus
tags: [consensus, bridge, burn-reservation, durable-postcondition, transaction-manager]
requires:
  - phase: 11-07
    provides: atomic certified mint effects and reservation consumption
  - phase: 11-09
    provides: Phase 11 closure audit and exact regression gates
provides:
  - exact durable CONSUMED gate before certified mint completion and cleanup
  - retryable false-success handling bound to the authoritative winner across restart
  - irreconcilable finalized-handle payload failure classification with legacy compatibility
affects: [11-11, phase-12, bridge-mint-application]
tech-stack:
  added: []
  patterns: [advisory-handler-success, exact-durable-postcondition, terminal-certified-data-contradiction]
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.cpp
    - src/account/TransactionManager.cpp
    - test/src/blockchain/consensus_burn_reservation_test.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp
key-decisions:
  - "Applied and AlreadyApplied are advisory for resource-bearing certificates until an exact live-store reread proves the captured reservation identity is CONSUMED."
  - "Missing, unreadable, or exact FinalizedPendingApplication postconditions restore the exact process to Pending without completion, cleanup, or dependency wake."
  - "Finalized-handle payload contradictions are irreconcilable while the no-handle legacy fallback retains its established Applied compatibility result."
patterns-established:
  - "Resource success side effects remain behind one exact slot/outpoint/generation/certificate/proposal/winner CONSUMED comparison."
  - "Focused finalizer tests drive production application dispositions through durable process and reservation records without public test hooks."
requirements-completed:
  - BURN-03
  - BURN-04
duration: 26 min
completed: 2026-07-29
---

# Phase 11 Plan 10: Durable Certified Mint Completion Summary

**Certified mint completion, cleanup, and dependency wake now require the exact certificate-bound reservation to be durably `CONSUMED`, while invalid finalized payloads terminate safely instead of masquerading as success.**

## Performance

- **Duration:** 26 min
- **Started:** 2026-07-29T17:11:53Z
- **Completed:** 2026-07-29T17:37:44Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments

- Captured the immutable finalized burn identity before handler invocation and added a strict post-handler live-store reread before the only resource-bearing `MarkComplete` path.
- Kept false `Applied` and `AlreadyApplied` responses pending, exact-winner-bound, restart-retryable, and free of premature slot, cleanup, work-journal, or dependency-wake side effects.
- Classified finalized-handle decode, absent embedded transaction, deserialization, and transaction-hash contradictions as irreconcilable while preserving legacy no-handle behavior.
- Added deterministic integrated coverage for false success, restart retry, exact consumed completion, terminal payload protection, zero cleanup on contradiction, and legacy compatibility.

## Task Commits

Each task was committed atomically:

1. **Task 1: Gate resource application completion on exact durable consumption** - `3926e5c8` (fix)
2. **Task 2: Make finalized-handle payload failures irreconcilable and run closure gates** - `c63833a7` (fix)

## Files Created/Modified

- `src/blockchain/Consensus.cpp` - Exact durable reservation identity and `CONSUMED` postcondition before completion side effects.
- `src/account/TransactionManager.cpp` - Finalized-handle-aware standalone certificate fallback classification.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - False-success, restart, exact completion, cleanup, and dependency-wake evidence.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Malformed/hash-mismatch terminal protection and legacy compatibility evidence.

## Decisions Made

- Preserved the existing legacy certificate path outside the burn postcondition; only application handlers carrying a finalized reservation identity require durable consumption.
- Reused the existing terminal safety path for readable contradictions and the existing pending restoration path for operational/unavailable postcondition failures.
- Used the private focused `ProcessFinalizedCertificate` seam with real durable reservation/process records for account integration tests because the account fixture's default registry intentionally cannot form a one-validator public-slot bridge quorum.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The first dependency-wake fixture used a waiter in the finalized slot, so normal proposal cleanup removed it before the wake call. Moving the waiter to a distinct slot made the intended wake ordering observable without adding hooks.
- The account fixture's default validator registry cannot create a single-validator public bridge-slot certificate. The existing friend-only focused finalizer seam was used with real durable records, retaining production callback and terminal-state behavior without changing production APIs or registry policy.

## User Setup Required

None - no external service configuration required.

## Verification

- Task 1 exact discovery found all 3 required tests; the 4-test focused slice passed 4/4.
- Task 2 exact discovery found all 3 required tests; the focused account slice passed 3/3.
- Exact Phase 11 discovery reported 9 targets and the guarded suite passed 9/9 in 153.15 seconds.
- Exact Phase 10 compatibility discovery reported 8 targets and the guarded suite passed 8/8 in 109.25 seconds.
- Final focused rebuild/test passed 3/3 and `git diff --check` passed.

## Next Phase Readiness

- BURN-04 hollow-success closure is complete and the exact regression suites are green.
- Plan 11-11 can independently close the remaining post-`CONSUMED` contradiction terminal-state gap.

## Self-Check: PASSED

- Task commits `3926e5c8` and `c63833a7` exist.
- All four planned source/test files are present and committed.
- All task acceptance criteria and plan-level verification gates passed.
- No WR-03 handler replacement, mock RPC, Phase 12 race, or unrelated lifecycle source change was introduced.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-29*
