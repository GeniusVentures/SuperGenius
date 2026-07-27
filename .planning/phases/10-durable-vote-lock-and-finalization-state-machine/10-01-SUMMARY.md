---
phase: 10-durable-vote-lock-and-finalization-state-machine
plan: 01
subsystem: consensus-testing
tags: [consensus, rocksdb, crdt, concurrency, googletest]

requires:
  - phase: 09-canonical-slot-and-certificate-storage
    provides: Authoritative slot certificates, verified winner index, and owned CRDT shutdown
provides:
  - Active focused vote-journal and finalization GoogleTest targets
  - Same-path RocksDB restart fixture with real GlobalDB, registry, pubsub, and manager dependencies
  - Deterministic finalization barrier, ordered event trace, and independent lifecycle counters
affects: [phase-10, durable-vote-locks, finalization, conflict-handling]

tech-stack:
  added: []
  patterns:
    - Explicit system and steady clock samples for deterministic consensus tests
    - RAII callback/counter reset and worker release/join guards
    - Real same-path RocksDB reopen after manager and GlobalDB closure

key-files:
  created:
    - test/src/blockchain/consensus_vote_journal_test.cpp
    - test/src/blockchain/consensus_finalization_test.cpp
  modified:
    - test/src/blockchain/CMakeLists.txt

key-decisions:
  - "Wave 0 exercises the real CRDT/RocksDB dependency graph while reserving private test-access classes for later friend-only hooks."
  - "Concurrency fixtures use condition-variable predicates and RAII release/join ownership without timing sleeps or detached threads."

patterns-established:
  - "Persistent harness: close every tracked ConsensusManager, shut down GlobalDB, then reopen the exact database path directly."
  - "Finalization observation: independent atomic stage counters plus a mutex-protected ordered event trace expose interleavings without production APIs."

requirements-completed: [CERT-05, CERT-06, CERT-07, VOTE-01, VOTE-02, VOTE-03, VOTE-06, VOTE-07]

duration: 5 min
completed: 2026-07-27
---

# Phase 10 Plan 01: Durable Consensus Harnesses Summary

**Two focused GoogleTest binaries now provide real same-path RocksDB restart coverage and deterministic condition-variable finalization barriers for every later Phase 10 behavior plan.**

## Performance

- **Duration:** 5 min
- **Started:** 2026-07-27T15:14:02Z
- **Completed:** 2026-07-27T15:19:00Z
- **Tasks:** 1
- **Files modified:** 3

## Accomplishments

- Registered active `consensus_vote_journal_test` and `consensus_finalization_test` targets with the certificate/pending fixture dependency set.
- Added a real GlobalDB, validator registry, pubsub, and ConsensusManager harness that closes owners and reopens the exact RocksDB path to recover a durable marker.
- Added explicit clocks, RAII reset/release/join helpers, independent lifecycle counters, and ordered events for deterministic future finalization interleavings.
- Proved both required baseline tests are uniquely discoverable and pass without wall-clock sleeps, detached threads, or production source changes.

## Task Commits

Each task was committed atomically:

1. **Task 1: Register deterministic vote-journal and finalization test harnesses** — `b11e50bd` (test)

## Files Created/Modified

- `test/src/blockchain/CMakeLists.txt` — registers and links both focused Phase 10 test binaries.
- `test/src/blockchain/consensus_vote_journal_test.cpp` — provides real dependency construction, tracked manager closure, same-path RocksDB reopen, explicit clocks, counters, and RAII resets.
- `test/src/blockchain/consensus_finalization_test.cpp` — provides a condition-variable barrier, RAII worker ownership, ordered event trace, and independent finalization-stage counters.

## Decisions Made

- Kept all fault and observation scaffolding inside tests; later production-private seams can friend the named access classes without exposing public testing APIs.
- Used the existing `CRDTFixture` to allocate unique database/keypair paths and construct real GlobalDB/pubsub dependencies.
- Used predicate-based condition-variable waits and an RAII guard that always releases and joins its worker, including assertion-failure cleanup.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The existing Release build tree required one CMake regeneration before the newly registered targets were visible; regeneration completed without source changes.

## Verification

- Both focused targets build successfully with `-j2`.
- `PersistentDatabaseReopensCleanly` and `ConcurrentBarrierJoinsCleanly` each appear exactly once in `--gtest_list_tests`.
- Both focused binaries pass their complete suites (1/1 each).
- Static audit confirms neither new harness contains `sleep_for` or detached-thread use.
- `git diff --check` passes and the implementation commit contains only the three planned test files.

## User Setup Required

None.

## Next Phase Readiness

- Plan 10-02 can extend the vote-journal fixture with strict durable local records and exact-byte assertions.
- Plans 10-05 and 10-06 can extend the finalization fixture with handler barriers, stage counters, conflict evidence, and publication ordering.
- No blocker remains.

## Self-Check: PASSED

- The implementation commit exists and all three key files are present.
- Every task acceptance criterion and the plan-level focused verification command pass.
- No production file or public test API was changed.

---
*Phase: 10-durable-vote-lock-and-finalization-state-machine*
*Completed: 2026-07-27*
