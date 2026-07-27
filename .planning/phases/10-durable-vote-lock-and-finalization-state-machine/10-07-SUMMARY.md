---
phase: 10-durable-vote-lock-and-finalization-state-machine
plan: 07
subsystem: consensus-safety
tags: [consensus, lifecycle, crdt, recovery, concurrency]

requires:
  - phase: 10-durable-vote-lock-and-finalization-state-machine
    provides: Durable vote, finalization, pending-application, and conflict-safety state machine from Plans 10-01 through 10-06
provides:
  - Owned ConsensusManager shutdown boundary that rejects new activity and drains callbacks, handlers, recovery, and timer work
  - Reentrant-safe CRDT certificate ingress deferred through the durable recovery journal
  - Deterministic shutdown and real-CRDT regression coverage without timing sleeps or detached workers
  - Full Phase 9/10 certificate, transaction-pending, UTXO, and configuration compatibility gate
affects: [phase-10-verification, consensus-lifecycle, certificate-recovery]

tech-stack:
  added: []
  patterns:
    - Scoped activity leases guard every asynchronous ConsensusManager entry point
    - Synchronous CRDT callbacks record durable work and defer authority reads until the outer merge commits
    - Condition-variable waits include shutdown and state predicates to prevent lost wakeups

key-files:
  created:
    - .planning/phases/10-durable-vote-lock-and-finalization-state-machine/10-07-SUMMARY.md
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_vote_journal_test.cpp
    - test/src/blockchain/consensus_finalization_test.cpp
    - test/src/blockchain/certificate_compatibility_test.cpp

key-decisions:
  - "Close is the manager lifetime boundary: it marks closing, wakes all waits, unregisters ingress, joins the timer, and drains active leases before returning."
  - "A CRDT new-element callback never reads or rewrites authority synchronously; it journals immediate recovery because the outer GraphSync merge is not yet visible."
  - "Corrupt slot authority remains a typed IntegrityError for a live reader and causes strict restoration to reject a restart before participation."

patterns-established:
  - "Lease-before-entry: asynchronous callbacks and finalization acquire an activity token that Close deterministically drains."
  - "Notify-with-predicate: every shutdown-sensitive wait observes both lifecycle state and the stop signal."

requirements-completed: [CERT-05, CERT-06, CERT-07, VOTE-01, VOTE-02, VOTE-03, VOTE-04, VOTE-05, VOTE-06, VOTE-07]

duration: 37 min
completed: 2026-07-27
---

# Phase 10 Plan 07: Lifecycle and Compatibility Closure Summary

**Consensus shutdown now owns and drains every callback path, while CRDT certificate ingress cannot reenter GraphSync persistence before its outer merge commits.**

## Performance

- **Duration:** 37 min
- **Started:** 2026-07-27T19:50:00Z
- **Completed:** 2026-07-27T20:27:00Z
- **Tasks:** 1
- **Files modified:** 5

## Accomplishments

- Added scoped lifecycle leases to pubsub, CRDT filters/callbacks, finalization, and the timer loop; `Close()` now rejects new work, wakes state waits, unregisters ingress, joins its owned timer, and waits for active operations to drain.
- Documented the manager lock order and retained signer, CRDT, pubsub, and registered handler execution outside the broad proposal/slot mutex.
- Replaced synchronous CRDT certificate finalization with durable immediate recovery scheduling, eliminating a nested `GlobalDB::Put`/GraphSync `WaitForJob` deadlock before the outer merge becomes visible.
- Added deterministic tests for close waking a publication-reservation waiter, close draining a blocked handler before destruction, and real-CRDT callback deferral followed by exactly-once recovery.
- Preserved certificate compatibility by checking typed live-read integrity errors and strict restart rejection for corrupt authority.

## Task Commits

1. **Task 1: Prove shutdown, concurrency, configuration, and compatibility closure** — `53c5aba8` (fix)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` — defines the shared activity state, shutdown serialization, and manager lock-order contract.
- `src/blockchain/Consensus.cpp` — implements activity leasing, deterministic close/drain behavior, predicate-based wakeup, and deferred CRDT recovery.
- `test/src/blockchain/consensus_vote_journal_test.cpp` — proves a real CRDT write returns without nested persistence and recovery applies the committed certificate once.
- `test/src/blockchain/consensus_finalization_test.cpp` — proves publication waiters wake on close and blocked handlers drain before destruction.
- `test/src/blockchain/certificate_compatibility_test.cpp` — retains typed integrity lookup assertions while aligning restart checks with strict restoration.

## Decisions Made

- Shutdown is serialized and idempotent; after closing begins no asynchronous entry point can acquire a new activity lease.
- The timer thread uses the manager only while protected by shared activity state, avoiding both a final strong-owner self-join and post-destruction access.
- CRDT ingress validates the canonical key, marks durable recovery work seen/stalled with zero delay, and lets recovery read committed authority later.
- Live lookup semantics and startup semantics are complementary: corruption is a typed integrity failure when read and a fail-closed startup condition on restart.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Removed reentrant GraphSync persistence from CRDT callbacks**
- **Issue:** The real CRDT regression hung in `CertificateReceived -> FinalizeSlot -> GlobalDB::Put -> WaitForJob` while the outer CRDT worker was still committing the callback-triggering merge.
- **Fix:** Journaled immediate durable recovery from the callback and deferred finalization until committed authority is readable.
- **Verification:** The new real-CRDT regression and the full vote-journal suite pass without a hang.

**2. [Rule 1 - Bug] Closed a shutdown lost-wakeup race**
- **Issue:** `Close()` could notify after a finalizer checked the stop flag but before its predicate-less publication-reservation wait began.
- **Fix:** Made the wait predicate observe both shutdown and reservation lifecycle transitions.
- **Verification:** The exact shutdown test passed 10 consecutive runs and the full finalization suite passed.

**3. [Rule 3 - Blocking Test Correction] Aligned corrupt-authority compatibility fixtures with strict restoration**
- **Issue:** Two legacy fixtures expected manager construction to succeed after inserting corrupt slot authority, contradicting the existing fail-closed startup contract.
- **Fix:** Preserved the live `IntegrityError` assertions, then asserted that restart is rejected before participation.
- **Verification:** All 17 certificate compatibility tests pass in the exact gate.

---

**Total deviations:** 3 auto-fixed issues (2 runtime concurrency defects, 1 blocking compatibility-test correction).
**Impact on plan:** Production changes stayed inside `Consensus.hpp/.cpp`; no TransactionManager, UTXOManager, bridge-burn, or multi-node race behavior changed.

## Issues Encountered

- The first gate exposed a CRDT callback deadlock; a live process sample identified nested GraphSync `WaitForJob` calls on the CRDT processing worker.
- A later gate exposed the publication-wait lost wakeup; a live sample showed `Close()` draining the finalizer while the finalizer waited on `slot_cv_`.
- One certificate-store case failed once during the initial interrupted run but passed immediately in isolation and passed in the clean full rerun.

## Verification

- Exact eight-target build and CTest regex: **8/8 passed**, 0 failed, 105.30 seconds.
- `CloseWakesFinalizerWaitingForPublicationReservation`: **10/10 repeated runs passed**.
- Real-CRDT callback deferral, full vote-journal, full finalization, and compatibility suites pass.
- Test audit finds no `sleep_for` or detached thread in the three focused consensus lifecycle suites.
- `git diff --check` passes; protected pre-existing dirty paths remained unstaged and unmodified.

## User Setup Required

None.

## Next Phase Readiness

- Phase 10's durable vote/finality implementation has a deterministic owned lifetime and its complete named compatibility gate is green.
- The milestone can proceed to Phase 10 verification; Phase 11 burn ownership and Phase 12 multi-node race work remain intentionally untouched.

## Self-Check: PASSED

- Implementation commit `53c5aba8` exists and contains only the five scoped production/test files.
- The exact plan verification command passes all eight suites.
- Protected user-owned changes remain outside the implementation commit.

---
*Phase: 10-durable-vote-lock-and-finalization-state-machine*
*Completed: 2026-07-27*
