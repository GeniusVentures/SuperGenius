---
phase: 07-deferred-validation-and-pending-proposal-lifecycle
plan: 05
subsystem: transaction-manager
tags: [transaction-manager, pending, unconfirmed, replay-protection, consensus]

requires:
  - phase: 07-deferred-validation-and-pending-proposal-lifecycle
    provides: Pending dependency wakeups, retry, and TTL expiry
provides:
  - Missing predecessor certificate returns `Pending(Certificate(previous_hash))`
  - `TransactionStatus::UNCONFIRMED` for inconclusive local outgoing expiry
  - Remote embedded VERIFYING timeout removes temporary tracking
  - Existing FAILED semantics preserved for proven invalid transactions
affects: [transaction-manager, consensus, phase-07]

tech-stack:
  added: []
  patterns:
    - Structured replay-protection detail result
    - FAILED reserved for proven invalidity
    - UNCONFIRMED reserved for inconclusive local outgoing expiry

key-files:
  created: []
  modified:
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp

key-decisions:
  - "The existing boolean replay-protection API remains terminal for legacy callers; consensus handling uses `EvaluateTransactionReplayProtection()` to distinguish Pending from Reject."
  - "Local outgoing VERIFYING proposal expiry becomes UNCONFIRMED and does not trigger rollback/failure side effects."
  - "Remote embedded VERIFYING proposal expiry removes the temporary tracking entry instead of keeping it as UNCONFIRMED."
  - "No automatic resubmission policy was added for UNCONFIRMED."

patterns-established:
  - "Use `EvaluateTransactionReplayProtection()` when the caller needs structured Pending dependencies."
  - "Use `ChangeTransactionState(..., UNCONFIRMED)` only for inconclusive local outgoing expiry."

requirements-completed:
  - PEND-03
  - PEND-06
  - PEND-07
  - TXSTATE-01

duration: 12min
completed: 2026-06-16
---

# Phase 07 Plan 05: Transaction Pending Lifecycle Summary

**TransactionManager now distinguishes recoverable predecessor-certificate gaps from proven invalidity and inconclusive expiry**

## Performance

- **Duration:** 12 min
- **Completed:** 2026-06-16T21:50:15Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments

- Added `TransactionStatus::UNCONFIRMED`.
- Added structured replay protection via `EvaluateTransactionReplayProtection()`.
- Changed nonce consensus handling so a missing predecessor certificate returns local `ValidationResult::Pending({ Certificate(previous_hash) })`.
- Preserved the existing boolean `CheckTransactionReplayProtection()` behavior for callers that still need terminal true/false validation.
- Added a `ValidateTransactionForConsensus(tx, check_replay)` overload so consensus handling can consume the structured replay result once and avoid reclassifying Pending as Reject.
- Updated proposal timeout cleanup:
  - local outgoing VERIFYING -> UNCONFIRMED
  - remote embedded VERIFYING -> remove temporary tracking entry
  - CONFIRMED/missing/non-VERIFYING entries remain untouched
- Updated outgoing wait logic so UNCONFIRMED is observable as a terminal status.
- Added account lifecycle contract coverage for UNCONFIRMED and certificate dependency Pending.

## Task Commits

1. **Task 1/2 and Task 2/2: Transaction pending dependency and expiry semantics** - `b33ee080` (feat)

## Files Created/Modified

- `src/account/TransactionManager.hpp` - Adds UNCONFIRMED, replay detail result, friend test access, and updated declarations/comments.
- `src/account/TransactionManager.cpp` - Implements structured replay handling, Pending predecessor dependencies, UNCONFIRMED timeout semantics, and remote temp cleanup.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Adds focused contract checks for the new state and dependency surface.

## Decisions Made

- Kept automatic resubmission out of scope; UNCONFIRMED is a state signal only.
- Kept terminal validation Reject behavior marking proven-invalid local transactions FAILED.
- Did not serialize Pending/dependency metadata into protobuf messages; dependency metadata remains local consensus state.

## Deviations from Plan

### Coverage Note

The account lifecycle target now covers the new state/dependency contract surface, and the combined account/consensus lifecycle targets pass. Full end-to-end tx2-before-tx1 recovery still depends on broader TransactionManager object construction that this focused harness intentionally avoids to prevent GeniusNode/network startup.

---

**Total deviations:** 1 coverage limitation.
**Impact on plan:** Production behavior is implemented; deeper integration coverage should be added when the account fixture can construct TransactionManager/Blockchain without network-heavy startup.

## Issues Encountered

- No compile or runtime failures after implementation.
- The existing `evmrelay` submodule modification was pre-existing and left untouched.

## Verification

- `cmake --build build/OSX/Debug --target transaction_manager_pending_lifecycle_test consensus_pending_lifecycle_test` — passed.
- `ctest --test-dir build/OSX/Debug -R 'consensus_pending_lifecycle_test|transaction_manager_pending_lifecycle_test' --output-on-failure` — passed.
- `git diff --check` — passed.

## User Setup Required

None.

## Next Phase Readiness

Phase 7 execution plans are complete. Remaining work should be validation/audit-focused: add deeper integration tests when a light TransactionManager fixture is available, and review whether public callers should surface UNCONFIRMED distinctly in UI/API responses.

## Self-Check: PASSED

- Missing predecessor certificates are recoverable Pending dependencies.
- FAILED remains reserved for proven invalidity.
- Local inconclusive expiry becomes UNCONFIRMED.
- Remote temporary expiry removes tracking.
- No automatic resubmission was added.

---
*Phase: 07-deferred-validation-and-pending-proposal-lifecycle*
*Completed: 2026-06-16*
