---
phase: 09-canonical-slot-and-certificate-storage
plan: 12
subsystem: crdt-replication-lifecycle
tags: [crdt-filter, dependency-retry, shutdown-barrier, concurrency]

requires:
  - phase: 09-08
    provides: Tri-state delta filtering and bounded dependency-root parking
  - phase: 09-09
    provides: Durable certificate dependency validation across CRDT replication
provides:
  - Namespace-aware mixed Reject and RetryDependency aggregation that preserves retained dependency barriers
  - Deterministic fail-closed handling for multiple retained retry dependencies
  - Worker-safe asynchronous close requests with one shared completion barrier
  - Synchronized shutdown snapshots and post-close admission/callback suppression
affects: [10-finalization-state-machine, crdt-replication, globaldb-shutdown]

tech-stack:
  added: []
  patterns:
    - Evaluate every matching delta filter against the immutable original delta and sanitize a separate result
    - Coalesce equal retry dependencies while stripping all retry namespaces on conflicting dependencies
    - Separate worker-safe close initiation from synchronous external close completion

key-files:
  created: []
  modified:
    - src/crdt/crdt_data_filter.hpp
    - src/crdt/impl/crdt_data_filter.cpp
    - src/crdt/crdt_datastore.hpp
    - src/crdt/impl/crdt_datastore.cpp
    - test/src/crdt/crdt_datastore_test.cpp

key-decisions:
  - "Reject has terminal authority only over its matching namespace; a retained retry namespace keeps the sanitized delta parked under its original dependency."
  - "Distinct retained dependency CIDs fail closed by removing every affected retry namespace instead of selecting a dependency by registration order."
  - "RequestClose returns one shared completion future, while CancelAndCloseNow is the external synchronous join boundary."
  - "Callback dispatch and the Closing transition share a recursive gate so a callback may request close without allowing another callback to start afterward."

patterns-established:
  - "Mixed filter aggregation: evaluate original delta -> sanitize terminal namespaces -> classify retained retry dependencies -> run legacy filters only without retry."
  - "CRDT close lifecycle: atomically enter Closing -> stop admission/dispatch -> owned coordinator joins workers and drains state -> fulfill shared barrier."

requirements-completed:
  - CERT-01
  - CERT-02
  - CERT-03

duration: 16 min
completed: 2026-07-24
---

# Phase 09 Plan 12: Mixed CRDT Decisions and Shutdown Completion Summary

**Mixed namespace rejection can no longer erase certificate dependency stalls, and worker-originated shutdown now exposes a truthful completion barrier after all CRDT work is joined and drained.**

## Performance

- **Duration:** 16 min
- **Started:** 2026-07-24T15:26:00Z
- **Completed:** 2026-07-24T15:41:37Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments

- Refactored complete-delta aggregation to evaluate all filters against the unchanged input, sanitize only rejected namespaces, preserve retained retry dependencies, and bypass legacy element filters while stalled.
- Added deterministic equal-dependency coalescing and distinct-dependency fail-closed behavior, with pure and datastore-level regressions proving rejected data cannot make stalled co-packaged data visible.
- Replaced detached worker-close handling with an owned coordinator, idempotent `RequestClose()` shared future, synchronous external `CancelAndCloseNow()`, and mutex-owned begin/end snapshots.
- Closed broadcast, queue, publication, and callback admission once shutdown begins, then proved the completion barrier observes finished worker futures and empty pending, parked, and head state.

## Task Commits

Each task was committed atomically:

1. **Task 1: Preserve retry semantics after rejected namespaces are sanitized** - `6debb627` (fix)
2. **Task 2: Synchronize shutdown snapshots and expose worker-safe completion** - `1af67849` (fix)

## Files Created/Modified

- `src/crdt/crdt_data_filter.hpp` - Documents retained retry behavior after namespace sanitation.
- `src/crdt/impl/crdt_data_filter.cpp` - Aggregates per-namespace decisions against the original delta and resolves retained dependencies deterministically.
- `src/crdt/crdt_datastore.hpp` - Declares shutdown snapshots, the shared completion API, coordinator ownership, and focused lifecycle test accessors.
- `src/crdt/impl/crdt_datastore.cpp` - Implements synchronized close initiation/completion, worker draining, and Closing-state admission/callback guards.
- `test/src/crdt/crdt_datastore_test.cpp` - Covers mixed Reject/Retry parking, conflicting dependencies, worker-originated close, idempotent barriers, and post-close invisibility.

## Decisions Made

- A Reject result is merge metadata after its namespace is removed; it does not override a retained RetryDependency from another namespace.
- Retry namespaces that name different dependencies are all sanitized and cannot park under an arbitrary first or last dependency.
- Worker callbacks use `RequestClose()` and hand the returned future to an external waiter; synchronous close never waits on its own worker frame.
- The close transition is serialized with callback dispatch so callbacks already running may request close, while subsequent callbacks cannot begin.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The first Task 2 compile exposed that the new const pending-head test accessor needed the existing pending-head mutex to be mutable. The mutex qualification was corrected and all prescribed tests then passed.

## Post-Merge Integration Fix

- Combined CTest exposed a nondeterministic assertion in `MixedRejectAndRetryDependencyParksRetainedNamespace`: merged values and callbacks are observable inside `MergeDataFromDelta`, while `RemoveParkedRootAfterSuccess` runs only after `ProcessJobIteration` returns to the scheduler.
- The production order is intentional—the root remains parked until the complete job iteration succeeds—so the test's value-only wait condition was structurally incomplete.
- Commit `e01d7ceb` makes the condition wait for merged values, both callbacks, and parked-root removal as one lifecycle completion predicate. It adds no delay or timing workaround.
- The exact two-test mixed-decision slice passed 20 consecutive repetitions after the fix, followed by the complete 27-test CRDT suite.

## Known Stubs

None introduced. Existing unrelated TODO comments in legacy tombstone filtering and IPLD content inspection remain unchanged and do not affect this plan.

## User Setup Required

None - no external service configuration required.

## Verification

- `cmake --build build/OSX/Release --target crdt_test -j2` — PASS.
- Exact nonzero list guard plus `DeltaFilterMixedRejectAndRetryDependencyPreservesRetry` and `MixedRejectAndRetryDependencyParksRetainedNamespace` — PASS, 2/2.
- Post-merge stress run of that exact mixed-decision slice — PASS, 20/20 consecutive repetitions.
- Exact nonzero list guard plus `DeltaFilterDependencyAttemptLimitAndShutdownDrainParkedRoots` and `WorkerInitiatedShutdownCompletesBeforeBarrierAndRunsNoPostCloseWork` — PASS, 2/2.
- Complete `crdt_test` — PASS, 27/27.
- Existing ThreadSanitizer configuration search — none exposed by the repository; deterministic mutex/barrier coverage used as planned.
- Detached-thread source assertion — PASS, no `.detach()` in `crdt_datastore.cpp`.
- `git diff --check 627ab8f7..HEAD` — PASS.

## Next Phase Readiness

- Mixed certificate namespace deltas remain dependency-gated after terminal sanitation.
- CRDT owners can now wait for a worker-originated close without a false early-completion signal.
- Plan 09-13 can execute; no blockers.

## Self-Check: PASSED

- All five modified plan files exist.
- Task commits `6debb627`, `1af67849`, and post-merge integration fix `e01d7ceb` are present.
- Both exact filtered slices and the complete 27-test CRDT suite pass.
- Shutdown source contains one synchronized snapshot helper and no detached close helper.
- Protected user-owned dirty and generated paths remain unstaged and untouched.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-24*
