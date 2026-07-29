---
phase: 07-deferred-validation-and-pending-proposal-lifecycle
plan: 01
subsystem: testing
tags: [consensus, transaction-manager, pending-lifecycle, gtest, cmake]

requires:
  - phase: 07-deferred-validation-and-pending-proposal-lifecycle
    provides: Phase 7 context, research, validation strategy, and executable plan set
provides:
  - Focused consensus pending lifecycle GTest target
  - Focused TransactionManager pending lifecycle GTest target
  - CMake registrations for both Wave 0 pending lifecycle harnesses
affects: [consensus, transaction-manager, phase-07]

tech-stack:
  added: []
  patterns:
    - Dedicated pending lifecycle harnesses with smoke tests and future behavior slots
    - CRDTFixture-based TransactionManager test shape without GeniusNode network startup

key-files:
  created:
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp
  modified:
    - test/src/blockchain/CMakeLists.txt
    - test/src/account/CMakeLists.txt

key-decisions:
  - "Wave 0 test targets are intentionally lightweight smoke harnesses so later implementation plans can add behavior cases without changing CMake wiring."
  - "The TransactionManager pending lifecycle harness follows the existing `test::CRDTFixture` pattern and avoids GeniusNode network startup."

patterns-established:
  - "Consensus pending lifecycle tests live in `test/src/blockchain/consensus_pending_lifecycle_test.cpp` and reserve named coverage for D-01 through D-12 and D-16."
  - "TransactionManager pending lifecycle tests live in `test/src/account/transaction_manager_pending_lifecycle_test.cpp` and reserve named coverage for D-13 through D-16."

requirements-completed:
  - PEND-01
  - PEND-02
  - PEND-03
  - PEND-04
  - PEND-05
  - PEND-06
  - PEND-07
  - TXSTATE-01

duration: 33min
completed: 2026-06-16
---

# Phase 07 Plan 01: Wave 0 Pending Lifecycle Test Harness Summary

**Dedicated GTest/CMake harnesses for consensus pending proposals and TransactionManager inconclusive expiry behavior**

## Performance

- **Duration:** 33 min
- **Started:** 2026-06-16T17:05:30Z
- **Completed:** 2026-06-16T18:38:49Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments

- Added `consensus_pending_lifecycle_test` as the focused future target for PEND-01 through PEND-07.
- Added `transaction_manager_pending_lifecycle_test` as the focused future target for TXSTATE-01 and transaction cleanup behavior.
- Registered both test targets in their local CMake files and verified CTest discovery/execution.

## Task Commits

1. **Task 1: Create consensus pending lifecycle test target** - `83ddfa11` (test)
2. **Task 2: Create transaction manager pending lifecycle test target** - `dd9765f7` (test)
3. **Task 2 fix: Correct CRDTFixture namespace** - `66f3b260` (fix)

## Files Created/Modified

- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - Consensus pending lifecycle smoke harness with named future behavior slots for D-01 through D-12 and D-16.
- `test/src/blockchain/CMakeLists.txt` - Registers and links `consensus_pending_lifecycle_test`.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - TransactionManager pending lifecycle smoke harness using `test::CRDTFixture`.
- `test/src/account/CMakeLists.txt` - Registers and links `transaction_manager_pending_lifecycle_test` with the existing account test force-load pattern.

## Decisions Made

- Kept Wave 0 tests as compile-ready smoke harnesses because the actual pending lifecycle APIs are introduced in later plans.
- Used `test::CRDTFixture` for the TransactionManager harness to match the certificate fallback test pattern and avoid GeniusNode network startup.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Build correctness] Corrected TransactionManager harness fixture namespace**
- **Found during:** Task 2 verification
- **Issue:** The generated harness inherited from `sgns::test::CRDTFixture`, but the fixture is declared in namespace `test`.
- **Fix:** Changed the base class to `test::CRDTFixture`.
- **Files modified:** `test/src/account/transaction_manager_pending_lifecycle_test.cpp`
- **Verification:** Rebuilt `transaction_manager_pending_lifecycle_test` and reran CTest successfully.
- **Committed in:** `66f3b260`

---

**Total deviations:** 1 auto-fixed build correction.
**Impact on plan:** No scope expansion. The fix aligned the new harness with the existing account test convention.

## Issues Encountered

The first `ctest --test-dir build/OSX/Debug -R transaction_manager_pending_lifecycle_test --output-on-failure` found the CTest registration but could not run because the binary had not built yet. Building the target exposed the namespace issue above. After the fix, the target built and passed.

## Verification

- `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` — passed.
- `cmake --build build/OSX/Debug --target transaction_manager_pending_lifecycle_test` — passed.
- `ctest --test-dir build/OSX/Debug -R transaction_manager_pending_lifecycle_test --output-on-failure` — passed.
- `grep -v '^#' test/src/blockchain/CMakeLists.txt | grep -c 'consensus_pending_lifecycle_test'` — returned `1`.
- `grep -v '^#' test/src/account/CMakeLists.txt | grep -c 'transaction_manager_pending_lifecycle_test'` — returned `1`.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Ready for `07-02`: the consensus and TransactionManager pending lifecycle test targets are in place for structured validation result tests and later retry/TTL behavior cases.

## Self-Check: PASSED

- Key files exist on disk.
- Production commits exist for both tasks.
- Acceptance criteria were re-run after the namespace fix.
- Plan-level verification commands pass.

---
*Phase: 07-deferred-validation-and-pending-proposal-lifecycle*
*Completed: 2026-06-16*
