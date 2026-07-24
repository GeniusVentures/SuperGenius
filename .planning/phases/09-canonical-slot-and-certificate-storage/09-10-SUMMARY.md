---
phase: 09-canonical-slot-and-certificate-storage
plan: 10
subsystem: bridge
tags: [catch-up, durability, utxo, replay-protection, storage-errors]

requires:
  - phase: 09-06
    provides: Transactional receipt-resolution and chunk staging
  - phase: 09-09
    provides: Typed certificate storage read classification
provides:
  - Typed durable state for canonical external burns
  - Fail-closed MintFunds replay reads before mutation
  - Explicit Processed, AlreadyHandled, and Retry catch-up outcomes
  - Transactional publication barrier for callback failures
affects: [bridge-catchup, transaction-manager, phase-11-burn-reservations]

tech-stack:
  added: []
  patterns:
    - Exact NOT_FOUND-only absence classification
    - Durable publication outcomes gate cursor and dedup commits

key-files:
  created: []
  modified:
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - src/watcher/impl/bridge_catchup_watcher.hpp
    - src/watcher/impl/bridge_catchup_watcher.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp
    - test/src/account/bridge_event_identity_test.cpp
    - test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp
    - test/src/startup/startup_wiring_test.cpp

key-decisions:
  - "Only an exact datastore NOT_FOUND permits bridge burn state evaluation to continue to the UTXO registry."
  - "Catch-up commits a chunk only when every callback returns Processed or durable AlreadyHandled."

patterns-established:
  - "Bridge burn state: persisted execution and consumed outpoints are AlreadyHandled; reservations are Retry."
  - "Catch-up publication: callback exceptions are Retry and preserve both cursor and dedup state."

requirements-completed: [SLOT-03, SLOT-04]

duration: 16 min
completed: 2026-07-24
---

# Phase 09 Plan 10: Durable Catch-up Publication Summary

**Typed bridge burn reads now fail closed before mint mutation, while explicit callback outcomes prevent transient catch-up failures from advancing the scan cursor**

## Performance

- **Duration:** 16 min
- **Started:** 2026-07-24T14:46:57Z
- **Completed:** 2026-07-24T15:02:45Z
- **Tasks:** 2
- **Files modified:** 10

## Accomplishments

- Added a friend-only durable bridge burn state seam that distinguishes available, reserved, and already-handled burns while preserving exact storage errors.
- Made `MintFunds` return before UTXO, reservation, persistence, or queue mutation on corruption, I/O, and every non-`NOT_FOUND` executed-record read error.
- Replaced the boolean catch-up callback with `Processed`, `AlreadyHandled`, and `Retry`, and made retry/exception outcomes preserve the failed chunk cursor and uncommitted dedup tuples.
- Migrated and built all four callback-bearing translation units, with direct tests of the private production `GeniusNode` classifier.

## Task Commits

Each task was committed atomically:

1. **Task 1: Establish typed durable bridge state and make MintFunds fail closed** - `fa4c5921` (fix)
2. **Task 2: Make every BurnProcessor caller use durable Processed, AlreadyHandled, or Retry outcomes** - `b39c1790` (fix)

## Files Created/Modified

- `src/account/TransactionManager.hpp` - Defines typed bridge burn state and the private executed-record reader contract.
- `src/account/TransactionManager.cpp` - Classifies durable burn state and gates `MintFunds` before mutation.
- `src/account/GeniusNode.hpp` - Defines the private classifier facts and friend-only test access.
- `src/account/GeniusNode.cpp` - Uses durable state before submission and after failed submission races.
- `src/watcher/impl/bridge_catchup_watcher.hpp` - Defines explicit callback outcomes.
- `src/watcher/impl/bridge_catchup_watcher.cpp` - Stops publication before dedup/cursor commit on retry or exception.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Covers fail-closed storage reads and durable state distinctions.
- `test/src/account/bridge_event_identity_test.cpp` - Covers publication retry, exceptions, partial publication, and production classification.
- `test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp` - Migrates the standalone counting callback.
- `test/src/startup/startup_wiring_test.cpp` - Migrates the startup cancellation callback.

## Decisions Made

- A reserved outpoint is transient state and therefore returns `Retry`; it is never durable duplicate proof.
- `AlreadyHandled` requires either a persisted executed-burn record or a consumed outpoint.
- A failed submission is re-read durably: only a newly proven `AlreadyHandled` state permits the chunk to advance.

## Verification

- Built `transaction_manager_pending_lifecycle_test`; the exact three-test list guard found 3 tests and all 3 passed.
- Built `genius_node_test`, `bridge_event_identity_test`, `bridge_anvil_catchup_e2e_test`, and `startup_wiring_test`.
- The exact five-test publication list guard found 5 tests and all 5 passed.
- Full `bridge_event_identity_test` passed 19 tests.
- Full `transaction_manager_pending_lifecycle_test` passed 11 tests.
- The callback source audit returned exactly the four inventoried files.
- `git diff --check` passed, and the unrelated `GeniusNode.cpp` logger-level hunk remains unstaged and uncommitted.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The first Task 1 commit attempt was blocked by sandbox permission and paused. The user explicitly resumed the plan, after which the exact verified commit succeeded without changing task scope.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Catch-up publication now has durable retry semantics and is ready for Phase 09 re-verification.
- Plans 09-11 through 09-13 remain to close the other Phase 09 review findings.

## Self-Check: PASSED

- Summary and all key source files exist.
- Task commits `fa4c5921` and `b39c1790` are present in git history.
- Required focused and full regression suites passed.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-24*
