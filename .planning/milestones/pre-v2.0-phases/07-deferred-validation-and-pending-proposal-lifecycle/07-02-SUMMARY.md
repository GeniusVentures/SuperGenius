---
phase: 07-deferred-validation-and-pending-proposal-lifecycle
plan: 02
subsystem: consensus
tags: [consensus, validation-result, pending, transaction-manager, gtest]

requires:
  - phase: 07-deferred-validation-and-pending-proposal-lifecycle
    provides: Wave 0 pending lifecycle test harnesses
provides:
  - Structured `ConsensusManager::ValidationResult` subject handler contract
  - Typed local `PendingDependencyKey::Certificate` dependency keys
  - Local pending metadata retention for dependencies and retry metadata
  - Subject handler branching that keeps Pending and Stalled local/no-vote outcomes
affects: [consensus, transaction-manager, blockchain-facade, phase-07]

tech-stack:
  added: []
  patterns:
    - Local-only structured validation result wrapping legacy `ConsensusManager::Check`
    - Friend-accessor focused tests for pending lifecycle internals

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/impl/Blockchain.cpp
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - test/src/blockchain/consensus_certificate_test.cpp

key-decisions:
  - "Subject handlers now return `outcome::result<ConsensusManager::ValidationResult>` while certificate handlers continue returning `ConsensusManager::Check`."
  - "Pending dependency and retry metadata are retained locally with pending proposals, but dependency indexing and capacity policy remain for later Phase 7 plans."

patterns-established:
  - "Use `ValidationResult::Approve/Reject/Stalled/Pending` for subject validation outcomes."
  - "Use `PendingDependencyKey::Certificate(hash)` for predecessor-certificate dependencies."

requirements-completed:
  - PEND-01
  - PEND-02
  - PEND-07

duration: 86min
completed: 2026-06-16
---

# Phase 07 Plan 02: Structured Validation Result Summary

**Local structured subject validation results with typed pending dependencies and no wire-schema change**

## Performance

- **Duration:** 86 min
- **Started:** 2026-06-16T19:42:31Z
- **Completed:** 2026-06-16T21:08:25Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments

- Added `ConsensusManager::ValidationResult` with `Approve`, `Reject`, `Stalled`, and `Pending` constructors.
- Added `ConsensusManager::PendingDependencyKey` with typed `Certificate` dependency identity and hash support.
- Converted subject handler registration paths in Consensus, Blockchain, and TransactionManager to the structured result contract.
- Updated proposal handling and resume handling to branch on `ValidationResult::check`, keeping `Pending` and `Stalled` local and before any approval vote path.
- Added focused tests for result construction, typed key hashing, local pending admission, pending removal, and retry approval through `ContinueProposalAfterSubject()`.

## Task Commits

1. **Task 1/2: Structured validation contract and handler routing** - `473ac90c` (feat)
2. **Task 2 coverage: Local pending retry path** - `ebb38225` (test)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Defines `ValidationResult`, `PendingDependencyKey`, hash support, subject handler signature, pending metadata maps, and test friend access.
- `src/blockchain/Consensus.cpp` - Stores pending dependency/retry metadata and branches proposal handling on structured outcomes.
- `src/blockchain/impl/Blockchain.cpp` - Adapts registry batch subject handler to return `ValidationResult`.
- `src/account/TransactionManager.hpp` - Updates nonce subject handler declaration to return `ValidationResult`.
- `src/account/TransactionManager.cpp` - Adapts nonce subject validation returns to structured results.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - Adds active coverage for structured results and local pending retry behavior.
- `test/src/blockchain/consensus_certificate_test.cpp` - Updates dormant subject-handler lambdas to the new signature so the file stays source-compatible.

## Decisions Made

- Kept pending metadata local to `ConsensusManager`; no protobuf or network message changes were added.
- Preserved the existing subject-hash pending queue for this plan, while storing dependency metadata for Plan 03 to index by typed keys.
- Left certificate handlers on the existing `Check` contract because this plan only changes subject validation.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Verification Coverage] Added active local pending retry-path coverage**
- **Found during:** Plan close-out acceptance review
- **Issue:** Constructor tests alone did not prove Pending stayed local before approval.
- **Fix:** Added an active `consensus_pending_lifecycle_test` case using friend access to assert pending admission emits no local vote and approval goes through `ContinueProposalAfterSubject()`.
- **Files modified:** `src/blockchain/Consensus.hpp`, `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
- **Verification:** `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure`
- **Committed in:** `ebb38225`

---

**Total deviations:** 1 coverage hardening.
**Impact on plan:** No scope expansion; the added test directly covers the plan's local-only Pending and retry-idempotency acceptance criteria.

## Issues Encountered

- The legacy `consensus_certificate_test` target is commented out in CMake, so it could not be built directly. Its subject-handler lambdas were still updated for source compatibility.
- A first attempt to instantiate a real `GeniusAccount` in the active pending lifecycle test failed in this test environment. The test was revised to use a deterministic validator id and dummy signer at the internal consensus transition boundary.

## Verification

- `cmake --build build/OSX/Debug --target consensus_pending_lifecycle_test` — passed.
- `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` — passed.
- `ctest --test-dir build/OSX/Debug -R transaction_manager_pending_lifecycle_test --output-on-failure` — passed.
- `git diff --check` for modified Phase 07 Plan 02 files — passed.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Ready for `07-03`: the structured result and local pending metadata are in place, and the next plan can replace the subject-hash-only pending queue with bounded typed dependency indexes and cleanup accounting.

## Self-Check: PASSED

- Key files exist on disk.
- Production and test commits exist.
- Focused CTest target passes with active coverage for structured outcomes and local pending retry behavior.
- No protobuf schema changes were introduced.

---
*Phase: 07-deferred-validation-and-pending-proposal-lifecycle*
*Completed: 2026-06-16*
