---
phase: 07-deferred-validation-and-pending-proposal-lifecycle
plan: 04
subsystem: consensus
tags: [consensus, pending, dependency-wakeup, retry, ttl, cleanup]

requires:
  - phase: 07-deferred-validation-and-pending-proposal-lifecycle
    provides: Bounded canonical pending proposal pool
provides:
  - Typed dependency wakeups for pending proposals
  - Scheduled pending retry cadence
  - Dependency-triggered retry throttling
  - TTL expiry cleanup for pending consensus state
  - Thin Blockchain facade for typed pending dependency resume
affects: [consensus, blockchain-facade, phase-07]

tech-stack:
  added: []
  patterns:
    - Typed dependency wakeup through `PendingDependencyKey`
    - Retry validation outside `proposals_mutex_`
    - TTL expiry callback ownership separate from certificate success cleanup

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/Blockchain.hpp
    - src/blockchain/impl/Blockchain.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp

key-decisions:
  - "Certificate arrival wakes `PendingDependencyKey::Certificate(subject_hash)` only after certificate validation/finalization succeeds."
  - "Scheduled retry uses the configured 1s, 2s, 5s, then 10s cadence by default."
  - "Dependency-triggered retries preserve `last_retry_at` across re-pends so repeated dependency events respect the minimum retry interval."
  - "Proposal cleanup callbacks are TTL/timeout behavior and no longer fire on successful certificate cleanup."

patterns-established:
  - "Use `WakePendingDependency()` for typed dependency resume instead of raw subject-hash resume when the dependency type matters."
  - "Use `ProcessDuePendingRetries()` and `ExpirePendingProposals()` from the manager-owned timer loop and friend tests."

requirements-completed:
  - PEND-03
  - PEND-04
  - PEND-05
  - PEND-06
  - PEND-07

duration: 28min
completed: 2026-06-16
---

# Phase 07 Plan 04: Pending Wakeup, Retry, And Expiry Summary

**Pending proposals now recover on typed dependency arrival, retry on a bounded schedule, and expire cleanly after TTL**

## Performance

- **Duration:** 28 min
- **Completed:** 2026-06-16T21:38:27Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments

- Added public `ConsensusManager::WakePendingDependency()` and a thin `Blockchain::TryResumePendingDependency()` facade.
- Wired `CertificateReceived()` to wake `PendingDependencyKey::Certificate(subject_hash)` after successful certificate validation/finalization.
- Added scheduled retry processing to the existing manager timer loop.
- Added configurable retry cadence with defaults of 1s, 2s, 5s, then 10s.
- Added dependency-triggered retry throttling via `min_dependency_retry_interval`.
- Added TTL expiry processing that removes pending entries through the centralized cleanup helper and fires cleanup callbacks exactly once.
- Reused the normal subject-validation-to-approval path for retries so local voting remains idempotent.
- Removed cleanup callback firing from successful certificate processing so cleanup callbacks are reserved for timeout/TTL behavior.
- Expanded the focused pending lifecycle test for wrong-key wakeups, multi-dependency re-pending, scheduled backoff, dependency throttle, and TTL cleanup.

## Task Commits

1. **Task 1/2 and Task 2/2: Typed wakeups, scheduled retry, and TTL expiry** - `cb549cb0` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Adds wakeup API, retry config, retry counters, and scheduler/expiry helper declarations.
- `src/blockchain/Consensus.cpp` - Implements typed wakeups, due retry processing, TTL expiry, retry helper routing, certificate-triggered wakeups, and certificate cleanup callback separation.
- `src/blockchain/Blockchain.hpp` - Declares typed dependency resume facade.
- `src/blockchain/impl/Blockchain.cpp` - Delegates typed dependency resume to `ConsensusManager`.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - Adds focused coverage for 07-04 behaviors.

## Decisions Made

- Kept retry processing outside `proposals_mutex_`; helper methods collect candidates under lock and invoke subject validation after releasing it.
- Preserved the old `ResumeProposalHandling(hash)` path for callers that still use subject-hash resume, while adding typed wakeup for certificate dependency correctness.
- Used test friend access to force retry due times and expiry times deterministically instead of sleeping.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 4 - Behavioral Consistency] Removed cleanup callbacks from certificate success**
- **Found during:** 07-04 diff review
- **Issue:** `ProcessCertificates()` still fired proposal cleanup callbacks on already-certified/successful certificate cleanup, conflicting with the Phase 7 rule that callbacks represent timeout/TTL cleanup.
- **Fix:** Removed those callback calls and left `ClearProposalSlot()` as the certificate-success cleanup path.
- **Files modified:** `src/blockchain/Consensus.cpp`
- **Verification:** `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure`
- **Committed in:** `cb549cb0`

---

**Total deviations:** 1 behavioral correction.
**Impact on plan:** Positive; aligns certificate cleanup with the plan's TTL-only cleanup callback rule.

## Issues Encountered

- The active test target still uses one CRDT-backed manager fixture to avoid repeated manager lifecycle aborts in this harness; new 07-04 behavior was added inside that fixture.
- Dummy signatures in the test harness still produce expected signature-verification log noise when self-votes are submitted; assertions inspect local slot vote state, not external signature acceptance.

## Verification

- `cmake --build build/OSX/Debug --target consensus_pending_lifecycle_test` — passed.
- `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` — passed.
- `git diff --check` — passed.

## User Setup Required

None.

## Next Phase Readiness

Ready for `07-05`: pending proposals now expire locally and fire cleanup callbacks exactly once, so TransactionManager can classify expired/inconclusive transactions separately from proven invalid cases.

## Self-Check: PASSED

- Certificate dependencies wake by predecessor certificate hash, not the pending proposal's own hash.
- Multiple-dependency proposals can re-pend on remaining dependency keys.
- Scheduled retry cadence and dependency throttle are configurable and covered.
- TTL expiry cleans pending entries, indexes, counters, and callbacks.

---
*Phase: 07-deferred-validation-and-pending-proposal-lifecycle*
*Completed: 2026-06-16*
