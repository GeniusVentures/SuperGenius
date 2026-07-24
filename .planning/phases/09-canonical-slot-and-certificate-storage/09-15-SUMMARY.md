---
phase: 09-canonical-slot-and-certificate-storage
plan: 15
subsystem: crdt-lifecycle
tags: [crdt, shared-ptr, shutdown, reaper, callbacks, concurrency]

requires:
  - phase: 09-12
    provides: Truthful shared close barrier, synchronized drain snapshots, and post-close callback suppression
provides:
  - Bounded weak-to-strong ownership for every CRDT worker turn and CrdtSet callback
  - Factory-enforced deferred deletion on one process-owned non-worker reaper
  - Isolated final-owner callback regression covering Put and Delete wrappers
affects: [phase-10, phase-11, phase-12, crdt-shutdown]

tech-stack:
  added: []
  patterns:
    - Weak-owner promotion scoped to one operation and released before waits
    - Custom shared_ptr deleter that only queues close and deletion to a registered reaper
    - Future-only lifetime observation that does not retain or close the datastore

key-files:
  created:
    - test/src/crdt/crdt_datastore_last_owner_test.cpp
  modified:
    - src/crdt/crdt_datastore.hpp
    - src/crdt/impl/crdt_datastore.cpp
    - test/src/crdt/CMakeLists.txt

key-decisions:
  - "The factory-created shared_ptr is the sole ownership control block and its noexcept deleter only enqueues finalization."
  - "Close completion, destructor completion, and delete completion are observable through copied futures that never retain the datastore."
  - "Test collaborators are stopped and joined before mirror disconnection to avoid racing the broadcaster."

patterns-established:
  - "Bounded promotion: lock one weak pointer for one worker turn or complete callback body, release it, then wait through ShutdownControl."
  - "Deferred final release: actual strong-count zero queues raw allocation plus separately shared control; only the reaper closes and deletes."

requirements-completed: [CERT-02, CERT-03]

duration: 22 min
completed: 2026-07-24
---

# Phase 09 Plan 15: CRDT Callback Lifetime Safety Summary

**CRDT workers now release bounded smart-pointer promotions before waiting, while actual final-owner release closes and deletes the datastore on a dedicated non-worker reaper.**

## Performance

- **Duration:** 22 min
- **Started:** 2026-07-24T17:56:14Z
- **Completed:** 2026-07-24T18:18:23Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments

- Replaced destructor-driven shutdown with one registered process reaper that arbitrates requested close and final deletion through a shared lifecycle state.
- Scoped every DAG, handle-next, rebroadcast, Put, and Delete weak-pointer promotion so no owner survives a blocking wait or outlives its bounded callback/worker operation.
- Preserved the truthful Plan 09-12 close barrier, callback gate, worker joining, and complete pending/parked/head drain.
- Added an isolated subprocess test that resets the receiver's sole external owner inside a real Put callback, exercises the Delete wrapper, and proves eventual non-worker destructor/delete execution with no post-close callback.

## Task Commits

Each task was committed atomically:

1. **Task 1: Bound smart-pointer promotions and defer final destruction to a non-worker reaper** — `8b501946` (fix)
2. **Task 2: Prove final external release inside a real CrdtSet callback is safe** — `2d943658` (test)

## Files Created/Modified

- `src/crdt/crdt_datastore.hpp` — declares shared shutdown/deferred-delete state and the narrow lifetime observer.
- `src/crdt/impl/crdt_datastore.cpp` — implements bounded worker ownership, reaper arbitration, truthful close, and deferred destructor/delete.
- `test/src/crdt/CMakeLists.txt` — adds the isolated last-owner executable.
- `test/src/crdt/crdt_datastore_last_owner_test.cpp` — covers Put/Delete wrapper ownership, callback-local survival, eventual weak expiration, drain completion, and reaper deletion.

## Decisions Made

- The custom deleter never dereferences, waits on, or deletes the datastore; it only transfers the raw allocation and separately owned shutdown control to the reaper queue.
- Requested close temporarily retains a shared owner on the reaper, while zero-count final delete uses the same exactly-once lifecycle state without recreating ownership.
- Lifetime instrumentation exposes copied futures and atomic snapshots only; it cannot request close or provide a raw datastore access channel.

## Deviations from Plan

None - plan executed as specified.

## Issues Encountered

- The initial stress run intermittently crashed while test teardown disconnected the mirror broadcaster concurrently with the sender's rebroadcast worker. The macOS crash report identified `CRDTMirrorBroadcaster::Broadcast`; teardown was reordered to stop/join the sender before disconnecting collaborators, after which the exact 20-repeat run passed.

## Verification

- Exact one-test list guard passes.
- Isolated final-owner subprocess passes 20/20 repetitions with `--gtest_break_on_failure`.
- Complete `crdt_test` passes 27/27 tests.
- CTest selection passes 2/2: `crdt_test` and `crdt_datastore_last_owner_test`.
- Existing shutdown/dependency focused slice passes 2/2.
- No existing ASan or TSan CMake configuration was present.
- Static audit finds one factory `new CrdtDatastore`, no detached threads, no per-instance close coordinator, and no `use_count()` lifetime decisions.
- `git diff --check` passes.

## User Setup Required

None.

## Next Phase Readiness

- Final external owner release from a production callback is now deterministic and safe for subsequent certificate-storage work.
- Plan 09-16 can proceed; no remaining blocker was introduced by this plan.

## Self-Check: PASSED

- Both task commits are present.
- The isolated test source and all three modified integration files are present.
- All required focused, repeat, full-suite, and CTest gates pass.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-24*
