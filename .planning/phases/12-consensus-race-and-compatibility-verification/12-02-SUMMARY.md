---
phase: 12-consensus-race-and-compatibility-verification
plan: 02
subsystem: consensus
tags: [consensus, finality, race, pubsub, crdt, recovery]

requires:
  - phase: 12-consensus-race-and-compatibility-verification
    provides: Private structured authority trace and friend-only test traversal from Plan 12-01
provides:
  - Deterministic authority-established pause after durable slot authority and before application work
  - Dedicated finality-race target covering PubSub, local, recovery, conflict, and restart behavior
affects: [12-04-eleven-node-race, consensus-finalization]

tech-stack:
  added: []
  patterns: [condition-variable race barriers, RAII worker ownership, same-datastore restart recovery]

key-files:
  created:
    - test/src/blockchain/consensus_finality_race_test.cpp
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/CMakeLists.txt

key-decisions:
  - "The authority-established control point runs outside locks after exact authority and finalized lifecycle installation, but before reservation, process, publication, application, or cleanup work."
  - "Late competitor traffic may remain observable, but authoritative bytes remain immutable and conflicting certificate establishment is rejected and deduplicated."

patterns-established:
  - "Cross-component finality races use predicate barriers and scoped joining workers, never sleeps or detached threads."
  - "External ingress assertions compare durable authority bytes and process/conflict state through real manager and store paths."

requirements-completed: [TEST-02, TEST-05]

duration: 8 min
completed: 2026-07-30
---

# Phase 12 Plan 02: Deterministic Finality Race Summary

**A lock-safe authority barrier and dedicated integration target prove that the certificate-before-application gap preserves one immutable winner and once-only application across live ingress and restart.**

## Performance

- **Duration:** 8 min
- **Started:** 2026-07-30T17:13:00Z
- **Completed:** 2026-07-30T17:21:00Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments

- Added an exact `authority-established` stage after slot/index authority and finalized lifecycle installation, before burn reservation, process, publication, handler, or cleanup work.
- Added a dedicated real-manager/real-RocksDB CTest target with a condition-variable barrier and RAII-owned worker.
- Proved PubSub gap visibility, immutable authority under competing traffic, once-only identical application through local/PubSub/recovery/restart, and canonical deduplicated conflict evidence.

## Task Commits

1. **Task 1: Add the dedicated finality-race harness and exact authority barrier** - `45440fe0` (test)
2. **Task 2: Prove the application gap and every certificate ingress contract** - `8f07ba34` (test)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Grants one narrowly scoped friend accessor to the dedicated race target.
- `src/blockchain/Consensus.cpp` - Emits the private deterministic stage at the exact post-authority/pre-application boundary.
- `test/src/blockchain/consensus_finality_race_test.cpp` - Real-store deterministic ingress, conflict, and restart tests.
- `test/src/blockchain/CMakeLists.txt` - Registers and links the dedicated CTest target.

## Decisions Made

- Reused the existing private finalization-stage observer instead of adding another event system or public test API.
- Asserted the safety property at the durable-authority boundary: candidate/vote traffic can be observed, but it cannot replace the exact stored authority or establish a competing certificate.

## Deviations from Plan

None - plan executed through the specified production handlers and recovery paths.

## Issues Encountered

- The generated build tree needed CMake regeneration before the new target was visible.
- The real manager accepts some late proposal/vote traffic for diagnostics; the regression therefore asserts the normative safety result—unchanged authority and rejected conflicting certificate—rather than requiring those observational calls themselves to fail.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The deterministic finality gap is closed and Plan 12-04 can use the established trace/race evidence in the genuine 11-node test.
- Existing finalization, burn-reservation, and certificate-store suites remain green.

## Verification

- `consensus_finality_race_test`: 4/4 passed, including all three required regression names.
- Existing regression CTest gate: 3/3 passed in 81.01 seconds.
- Dedicated CTest discovery: exactly one target.
- `git diff --check`: passed.

## Self-Check: PASSED

---
*Phase: 12-consensus-race-and-compatibility-verification*
*Completed: 2026-07-30*
