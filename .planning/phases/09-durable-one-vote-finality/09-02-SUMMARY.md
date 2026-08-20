---
phase: 09-durable-one-vote-finality
plan: 02
subsystem: consensus
tags: [c++17, rocksdb, crdt, certificates, gtest]
requires:
  - phase: 09-durable-one-vote-finality
    provides: durable per-slot active-vote records and deterministic vote recovery
provides:
  - post-commit legacy-certificate readback before same-slot active-vote release
  - read-only accepted-certificate slot fence preventing restart re-votes
  - deterministic regressions for callback, removal, and restart race boundaries
affects: [10-authoritative-slot-certificate-publication, 11-convergent-certificate-consumption-and-mint-recovery]
tech-stack:
  added: []
  patterns:
    - CRDT pre-commit callbacks only create stalled retry work
    - direct local vote deletion follows accepted legacy-value readback and validation
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
key-decisions:
  - Certificate callback receipt is never treated as durable acceptance.
  - Same-slot release accepts a different certified proposal only after committed legacy-key readback and validation.
  - Legacy certificate scanning is read-only and remains non-authoritative until Phase 10.
patterns-established:
  - Preserve stalled certificate work whenever readback, validation, slot matching, or local removal cannot prove release safety.
  - Fence candidate admission and active-vote recovery with an accepted legacy certificate scan.
requirements-completed: [VOTE-04]
metrics:
  duration: 42m
  completed: 2026-08-20
  tasks_completed: 2
  files_modified: 3
---

# Phase 9 Plan 02: Durable Certificate Release Summary

Durable active-vote locks now release only after a committed, validated legacy certificate readback proves finality for the same canonical slot.

## Accomplishments

- Converted the pre-commit CRDT certificate callback into stalled-work notification only.
- Added post-commit `db_->Get` recovery that rechecks legacy binding and certificate approval before synchronously deleting the matching local vote record.
- Added a read-only accepted-slot scan that prevents a reconstructed validator from re-voting a finalized slot.
- Covered missing/racing readback, different-slot finality, keyless PubSub, removal failure, later retry success, and restart no-revote behavior with `MemorySecureStorage`.

## Task Commits

1. **Task 1: Implement durable same-slot active-vote release and finalized-slot read seam** — `5036b619` (`feat`)
2. **Task 2: Harden release and restart paths against adversarial certificate and deadline races** — `d88f1447` (`test`)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` — private accepted-certificate query and durable-release contracts.
- `src/blockchain/Consensus.cpp` — stalled-only callback, validated post-commit recovery, direct local deletion, and no-revote scan.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — deterministic adversarial lifecycle coverage.

## Decisions Made

- `/cert/<subject-hash>` remains the current read source; no `/cert/<slot>` authority or CRDT write path was added.
- A local removal failure keeps both the exact active record and stalled work, allowing only a later successful readback/release to clear volatile state.
- `HandleCertificate` remains keyless volatile handling and cannot remove the direct local record.

## Verification

- Release build completed for `consensus_pending_lifecycle_test` and `consensus_slot_key_test`.
- Direct focused runners passed: `consensus_pending_lifecycle_test` 14/14 and `consensus_slot_key_test` 6/6.
- `git diff --check` passed.
- The CTest wrapper repeatedly stopped emitting output after starting the lifecycle target in this runner, while that exact binary completed cleanly when invoked directly; no CTest pass result is claimed.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 1 - Bug] Created a journal entry before marking a callback stalled**
   - **Found during:** Task 1 focused lifecycle test.
   - **Issue:** `MarkStalled` intentionally updates only an existing journal entry, so receipt-only work was not retained.
   - **Fix:** Mark the entry seen, then stalled with a zero lease in the pre-commit callback.
   - **Files modified:** `src/blockchain/Consensus.cpp`
   - **Verification:** Post-callback and failed-readback assertions retain stalled work and the active record.
   - **Committed in:** `5036b619`

2. **[Rule 1 - Bug] Updated old ingress assertions for the new callback boundary**
   - **Found during:** Task 1 focused lifecycle test.
   - **Issue:** Existing tests expected a certificate handler to run during the CRDT callback, contradicting the durable post-commit ordering.
   - **Fix:** Assert callback receipt has no handler/finality effects.
   - **Files modified:** `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
   - **Verification:** Focused lifecycle runner passes 14/14.
   - **Committed in:** `5036b619`

## Known Stubs

None.

## Next Phase Readiness

Phase 10 can replace the isolated read-only legacy accepted-slot seam with authoritative certificate publication without changing Phase 9's local lock-release ordering.

## Self-Check: PASSED

- Implementation and test artifacts exist.
- Task commits `5036b619` and `d88f1447` are present in history.
