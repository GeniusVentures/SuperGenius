---
phase: 11-slot-owned-bridge-burn-reservations
plan: 01
subsystem: testing
tags: [googletest, rocksdb, consensus, concurrency, restart]

requires:
  - phase: 10-durable-vote-lock-and-finalization-state-machine
    provides: Direct RocksDB consensus state and deterministic finalization test patterns
provides:
  - Focused consensus_burn_reservation_test target
  - Same-path real-storage restart harness
  - Predicate barrier, scoped worker, hook reset, fixed clocks, raw inspection, and lifecycle counters
affects: [11-02, 11-03, 11-04, 11-05, 11-06, 11-07, 11-08, 11-09]

tech-stack:
  added: []
  patterns: [same-path RocksDB reopen, RAII barrier release and join, exact nonzero test discovery]

key-files:
  created:
    - test/src/blockchain/consensus_burn_reservation_test.cpp
  modified:
    - test/src/blockchain/CMakeLists.txt

key-decisions:
  - "Wave 0 exercises an existing strict local consensus record so restart durability is real without introducing reservation production behavior early."
  - "Future Phase 11 production hooks remain private and friend-only; the harness adds no public test setters."

patterns-established:
  - "Reservation tests use fixed clocks, predicate barriers, and RAII worker cleanup instead of wall-clock sleeps."
  - "Restart tests close every GlobalDB/datastore owner before reopening the identical path."

requirements-completed:
  - BURN-01
  - BURN-02
  - BURN-03
  - BURN-04
  - BURN-05

duration: 32 min
completed: 2026-07-28
---

# Phase 11 Plan 01: Deterministic Burn-Reservation Harness Summary

**A focused real-RocksDB restart and concurrency harness now provides deterministic scaffolding for every Phase 11 reservation lifecycle plan.**

## Performance

- **Duration:** 32 min
- **Started:** 2026-07-28T18:31:00Z
- **Completed:** 2026-07-28T19:03:34Z
- **Tasks:** 1
- **Files modified:** 2

## Accomplishments

- Registered the active `consensus_burn_reservation_test` target with the Phase 10 consensus fixture dependencies.
- Proved a strict local consensus record survives closure and reconstruction over the exact same RocksDB path.
- Added fixed clocks, raw record inspection, scoped hook cleanup, lifecycle counters, named future behavior groups, and a predicate barrier whose worker is always released and joined.
- Kept reservation production behavior, CRDT reservation publication, mock RPC, and the Phase 12 multi-node race out of Wave 0.

## Task Commits

Each task was committed atomically:

1. **Task 1: Register the focused burn-reservation harness** - `26a490c8` (test)

## Files Created/Modified

- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Deterministic persistent-storage and concurrency harness.
- `test/src/blockchain/CMakeLists.txt` - Active focused test target and required links.

## Decisions Made

- Used `ConsensusStateStore::CertificateProcessingRecord` as the harmless supported local record for the Wave 0 same-path restart proof.
- Reserved the test access class name without exposing or modifying a production API; later plans can add only private friendship where needed.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- Normal CMake regeneration initially found an incomplete local libp2p install. The existing libp2p build was completed and installed into the already configured third-party Release prefix; no dependency declarations or project source behavior changed. CMake regeneration and the prescribed build/test gate then passed.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Ready for Plan 11-02 to add the strict reciprocal durable reservation store using this focused target.
- No Phase 12 or mock-RPC infrastructure was introduced.

## Self-Check: PASSED

- Created test source and modified CMake registration exist.
- Task commit `26a490c8` exists.
- Exact discovery guard found one `PersistentDatabaseAndBarrierReopenCleanly` test.
- Focused binary passed 1/1 tests; `git diff --check` and forbidden-pattern scans passed.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-28*
