---
phase: 10-durable-vote-lock-and-finalization-state-machine
plan: 05
subsystem: consensus-finalization
tags: [consensus, finality, recovery, processing-lease, linearization]

requires:
  - phase: 10-durable-vote-lock-and-finalization-state-machine
    provides: Generation-tagged publication reservations and durable vote replay from Plan 10-04
provides:
  - One authoritative FinalizeSlot path for local, PubSub, CRDT, recovery, and certificate processing
  - Durable Pending, Processing, and Complete winner-application lifecycle bound to certificate identity
  - Condition-variable ordering between publication reservations and finalization reservations
  - Completion-before-cleanup semantics with exact-winner retry and idempotent replay
affects: [phase-10, certificate-conflicts, consensus-recovery, transaction-application]

tech-stack:
  added: []
  patterns:
    - Persist authoritative certificate state before local application markers and effects
    - Reserve under the shared slot mutex, then perform storage, transport, and handlers outside it
    - Treat exact occupied authority as timeless and idempotent while validating empty-slot observations live

key-files:
  created:
    - .planning/phases/10-durable-vote-lock-and-finalization-state-machine/10-05-SUMMARY.md
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_finalization_test.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - test/src/blockchain/consensus_certificate_store_test.cpp

key-decisions:
  - "All certificate ingress sources delegate to one FinalizeSlot result contract; adapters do not apply or clean independently."
  - "A Finalizing reservation binds generation, proposal, deterministic certificate digest, and winner before authoritative persistence."
  - "The exact Pending marker is durable before handler execution, and Complete is durable before any proposal cleanup callback."
  - "Synchronous CRDT callbacks observe an in-flight matching Finalizing reservation without recursively joining it."

patterns-established:
  - "Finality-first processing: authoritative pair, exact marker, one processing lease, handler, Complete, then cleanup."
  - "Linearized races: publication owners notify before a finalizer reserves; a reserved finalizer suppresses later signing and replay."

requirements-completed: [CERT-05, CERT-06, VOTE-06]

duration: 41 min
completed: 2026-07-27
---

# Phase 10 Plan 05: Durable Finalization State Machine Summary

**Every certificate source now converges on one authoritative, crash-recoverable finalization path that persists exact winner work before application and commits completion before cleanup.**

## Performance

- **Duration:** 41 min
- **Started:** 2026-07-27T16:49:00Z
- **Completed:** 2026-07-27T17:30:00Z
- **Tasks:** 1
- **Files modified:** 5

## Accomplishments

- Unified local submission, PubSub delivery, CRDT callbacks, startup recovery, and quorum certificate processing behind `FinalizeSlot` and a typed source-independent result.
- Added shared-slot `Finalizing` reservations that wait for active publication/replay owners, bind the exact certificate identity, and prevent later signing or vote replay.
- Persisted the authoritative slot/index pair before the exact Pending application marker, with reread-based handling for ambiguous writes and generation-safe rollback only when absence is confirmed.
- Added one in-process processing lease per slot, stale Processing recovery, exact marker validation, handler retry semantics, durable Complete-before-cleanup ordering, and winner dependency wakeup.
- Preserved TransactionManager's atomic mint idempotency contract: exact `AlreadyApplied` remains handler success and UTXO production files remain unchanged.
- Added deterministic publication-first and finalization-first barriers, all-source convergence, missing-handler wake, queryability, single-handler, and Complete-before-cleanup coverage.

## Task Commits

Each task was committed atomically:

1. **Task 1: Implement unified durable slot finalization and winner application** — `795f6705` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` — defines delivery/result types, finalization reservations, exact identity bindings, and processing leases.
- `src/blockchain/Consensus.cpp` — implements unified ingress, authority-first persistence, condition-waited linearization, recoverable processing, and completion-first cleanup.
- `test/src/blockchain/consensus_finalization_test.cpp` — covers all-source convergence, missing-handler recovery, exact winner application, cleanup order, and both legal race orderings.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — preserves non-aggregator state and verifies TTL cleanup fires exactly once.
- `test/src/blockchain/consensus_certificate_store_test.cpp` — adapts strict preflight and startup authority coverage to the unified finalizer.

## Decisions Made

- The CRDT slot/index pair remains authoritative; the local process marker is a separate recoverable commit and is synthesized on startup when absent.
- Exact occupied certificate replay bypasses live horizon checks, while an empty authoritative slot requires current first-observation validation.
- Only the newly persisted local winner is published. Duplicate and replicated sources neither rebroadcast nor perform source-specific cleanup.
- Application attempts are serialized per slot without holding the proposal mutex across storage or handler execution.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Prevented synchronous CRDT callback self-deadlock**
- **Found during:** Local authoritative persistence testing
- **Issue:** The CRDT `Put` callback can run synchronously and re-enter finalization while the originating slot is already `Finalizing`.
- **Fix:** Matching in-flight callbacks defer legacy work instead of joining their own reservation; the originating path retires the work journal only after Complete.
- **Verification:** Concurrent Local/PubSub/CRDT/Recovery finalization completes and invokes one handler.

**2. [Rule 1 - Bug] Removed duplicate TTL cleanup callback**
- **Found during:** Pending-lifecycle regression verification
- **Issue:** TTL expiry fired cleanup directly and then called slot cleanup, which fired the same callback again.
- **Fix:** TTL expiry now delegates callback execution solely to `ClearProposalSlot` after successful removal.
- **Verification:** The bounded pending lifecycle case passes with exactly one cleanup callback.

**3. [Rule 3 - Blocking] Preserved certificate-store error compatibility under the typed finalizer**
- **Found during:** Full certificate-store verification
- **Issue:** The internal `StorageFailure` result intentionally collapses storage categories, but the existing public submission API distinguishes integrity from operational preflight failures.
- **Fix:** The Local adapter preserves its public preflight error contract before entering the source-independent finalizer; the startup fixture now uses valid canonical v2 authority required by strict restoration.
- **Verification:** All 23 certificate-store tests pass.

---

**Total deviations:** 3 auto-fixed issues (2 bugs, 1 blocking compatibility adjustment).
**Impact on plan:** All fixes preserve the requested authority and ordering model; no UTXO production code changed.

## Issues Encountered

- The broad header change caused expected static-library rebuilds; all focused and phase-wide regression commands completed successfully.

## Verification

- Exact Plan 10-05 command passes: finalization 5/5, pending lifecycle 7/7, TransactionManager pending lifecycle 15/15, and certificate store 23/23.
- Phase 10 CTest regex passes 8/8 targets, including vote journal, certificate compatibility, network configuration precedence, and UTXO manager.
- Static audit confirms Local, PubSub, CRDT, Recovery, and `ProcessCertificates` converge on `FinalizeSlot` with no adapter-owned cleanup path.
- Deterministic barriers prove publication-first notification precedes finalization reservation and finalization-first suppresses later signing before persistence.
- `git diff --check` passes; `UTXOManager.hpp/.cpp` and TransactionManager production files are unchanged.

## User Setup Required

None.

## Next Phase Readiness

- The unified typed conflict result and shared SafetyViolation lifecycle are ready for Plan 10-06 all-ingress conflict evidence.
- Authoritative replay, marker recovery, and application retry behavior are stable across restart and duplicate delivery.
- No blocker remains for Plan 10-06.

## Self-Check: PASSED

- Implementation commit `795f6705` exists and all five implementation/test files are present.
- Exact focused verification and the eight-target Phase 10 regression gate pass.
- Protected pre-existing dirty paths remain unstaged and uncommitted.

---
*Phase: 10-durable-vote-lock-and-finalization-state-machine*
*Completed: 2026-07-27*
