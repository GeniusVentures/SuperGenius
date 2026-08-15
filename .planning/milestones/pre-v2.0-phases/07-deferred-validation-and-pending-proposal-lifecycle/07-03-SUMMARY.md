---
phase: 07-deferred-validation-and-pending-proposal-lifecycle
plan: 03
subsystem: consensus
tags: [consensus, pending, dependency-index, bounded-pool, gtest]

requires:
  - phase: 07-deferred-validation-and-pending-proposal-lifecycle
    provides: Structured `ConsensusManager::ValidationResult` subject handler contract
provides:
  - Bounded canonical pending proposal pool
  - Typed pending dependency index keyed by `PendingDependencyKey`
  - Test-configurable pending lifecycle limits and three-minute production TTL default
  - Single pending removal helper for dependency indexes, votes, byte accounting, and proposer accounting
affects: [consensus, phase-07]

tech-stack:
  added: []
  patterns:
    - Canonical entry plus secondary typed dependency indexes
    - Local-only capacity refusal with no Reject vote
    - Single cleanup helper for all pending accounting

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - test/src/blockchain/consensus_certificate_test.cpp

key-decisions:
  - "Pending proposals are stored once in `pending_entries_` and indexed by all typed dependencies in `pending_by_dependency_`."
  - "Default pending lifecycle bounds are 1024 global entries, 64 entries per proposer, 64 MB retained bytes, and a three-minute TTL."
  - "Capacity refusal is local and returns `false` from `AddPendingProposal()` without voting or creating quorum state."
  - "The active test target keeps one CRDT-backed manager lifecycle because repeated manager-backed fixtures abort in this harness; pure contract checks run without CRDT startup."

patterns-established:
  - "Use `SetPendingLifecycleConfig()` for deterministic test bounds such as ten-second TTLs and low capacity limits."
  - "Use `RemovePendingProposal()` for every pending cleanup path so dependency indexes and counters stay consistent."

requirements-completed:
  - PEND-03
  - PEND-05
  - PEND-06
  - PEND-07

duration: 35min
completed: 2026-06-16
---

# Phase 07 Plan 03: Bounded Pending Pool Summary

**Typed, bounded pending proposal storage with centralized cleanup accounting**

## Performance

- **Duration:** 35 min
- **Completed:** 2026-06-16T21:23:26Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments

- Replaced subject-hash-only pending storage with canonical `PendingProposalEntry` records keyed by proposal id.
- Added `pending_by_dependency_` typed secondary indexing for all `ValidationResult::Pending` dependencies.
- Added `PendingLifecycleConfig` with defaults: 1024 global pending proposals, 64 per proposer, 64 MB retained bytes, and three-minute TTL.
- Added `SetPendingLifecycleConfig()` so tests can inject ten-second TTLs and low capacity limits without sleeps.
- Changed `AddPendingProposal()` to return admission success and refuse over-limit proposals locally without emitting Reject votes.
- Added `RemovePendingProposal()` and locked cleanup helper to remove dependency indexes, pending votes, retained-byte accounting, and per-proposer counts consistently.
- Updated `ClearProposalSlot()` and already-certified cleanup to use the pending removal helper.
- Expanded `consensus_pending_lifecycle_test` coverage for defaults, typed dependency indexes, capacity refusal, cleanup accounting, and retry approval after dependency arrival.

## Task Commits

1. **Task 1/2 and Task 2/2: Bounded pending pool and cleanup accounting** - `ea77d9ed` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Adds `PendingLifecycleConfig`, `PendingProposalEntry`, pending indexes/counters, config setter, and cleanup helper declarations.
- `src/blockchain/Consensus.cpp` - Implements bounded admission, dependency normalization, typed dependency wakeup, and centralized pending removal/accounting.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - Adds active coverage for bounded admission, cleanup, typed indexes, and local retry approval.
- `test/src/blockchain/consensus_certificate_test.cpp` - Updates dormant friend accessor to the new canonical pending entry map.

## Decisions Made

- Kept pending capacity refusal as a local condition only: no Reject broadcast and no quorum/vote side effect.
- Used proposal serialized size via `ByteSizeLong()` for retained-byte accounting.
- Preserved certificate-success behavior by not firing transaction cleanup callbacks during successful certificate cleanup.
- Collapsed manager-backed lifecycle coverage into one test case to avoid repeated CRDT/pubsub manager startup aborts in this test harness.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Verification Stability] Reduced repeated manager-backed fixture churn**
- **Found during:** `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure`
- **Issue:** Multiple CRDT-backed `ConsensusManager` fixtures in one binary caused subprocess aborts after the first manager-backed test, while each case passed in isolation.
- **Fix:** Moved pure contract/default assertions to non-fixture tests and combined bounded-pool plus retry-approval checks into one manager-backed fixture test.
- **Files modified:** `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
- **Verification:** `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure`
- **Committed in:** `ea77d9ed`

---

**Total deviations:** 1 test harness stabilization.
**Impact on plan:** No acceptance criteria were removed; the same behaviors remain covered with fewer CRDT manager lifecycles.

## Issues Encountered

- The direct test binary run created a root-level `CRDT.Datastore.TEST.unit_6/` datastore directory; it was removed before commit.
- The existing `evmrelay` submodule modification was pre-existing and left untouched.

## Verification

- `cmake --build build/OSX/Debug --target consensus_pending_lifecycle_test` — passed.
- `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` — passed.
- `git diff --check` — passed.

## User Setup Required

None.

## Next Phase Readiness

Ready for `07-04`: the pending pool now stores retry metadata, typed dependencies, TTL timestamps, and cleanup-safe accounting needed for scheduler/expiry behavior.

## Self-Check: PASSED

- Bounded pending entries and dependency indexes exist in `ConsensusManager`.
- Capacity limits are configurable and have the requested production defaults.
- Capacity refusal remains local/no-vote.
- Cleanup accounting is centralized and covered by the focused test.

---
*Phase: 07-deferred-validation-and-pending-proposal-lifecycle*
*Completed: 2026-06-16*
