---
phase: 09-canonical-slot-and-certificate-storage
plan: 14
subsystem: account-storage
tags: [bridge, mint, utxo, rocksdb, atomicity, restart]

requires:
  - phase: 09-10
    provides: Durable catch-up outcomes and fail-closed bridge replay reads
provides:
  - Atomic mint effects and durable burn-keyed application records
  - Linearizable ordinary UTXO persistence across concurrent mint application
  - Verified duplicate replay and restart recovery at every commit boundary
affects: [phase-10, phase-11, phase-12]

tech-stack:
  added: []
  patterns:
    - Shared persistence gate around UTXO snapshots and raw database batches
    - Burn-keyed application record as the sole durable mint-completion authority
    - Copy-stage-commit-publish mutation for atomic in-memory and RocksDB state

key-files:
  created: []
  modified:
    - src/account/proto/SGTransaction.proto
    - src/account/UTXOManager.hpp
    - src/account/UTXOManager.cpp
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - test/account/UTXOManagerTest.cpp
    - test/account/TransactionManagerPendingLifecycleTest.cpp
    - test/account/BridgeEventIdentityTest.cpp

key-decisions:
  - "A canonical burn-keyed application record, written with all mint effects, is the only durable completion authority."
  - "Ordinary UTXO persistence and mint application share one persistence gate so no stale snapshot can overwrite an atomic mint."
  - "AlreadyApplied is returned only after the stored application identity, winner, outputs, and consumed bridge input all verify."

patterns-established:
  - "Atomic mint application: validate, stage copied state, commit one raw batch, then publish live maps."
  - "Restart recovery: distinguish no-commit, committed-batch, and fully published memory boundaries using durable records."

requirements-completed: [SLOT-03, SLOT-04]

duration: 21 min
completed: 2026-07-24
---

# Phase 09 Plan 14: Atomic Mint Application Summary

**Mint confirmation now commits produced outputs, consumed bridge input, owner snapshots, and a canonical application record as one durable unit, with replay and restart verification at every failure boundary.**

## Performance

- **Duration:** 21 min
- **Started:** 2026-07-24T17:32:00Z
- **Completed:** 2026-07-24T17:53:25Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments

- Replaced the legacy logical execution marker with a versioned, burn-keyed application record that is committed in the same RocksDB batch as every mint effect.
- Serialized atomic mint application with ordinary UTXO persistence and checkpoint operations, preventing stale snapshots from overwriting committed mint state.
- Added strict duplicate verification and restart coverage for pre-commit failure, post-batch publication failure, and already-committed replay.
- Persisted UTXO type information so bridge-input consumption remains verifiable after restart while legacy entries retain the normal type default.

## Task Commits

Each task was committed atomically:

1. **Task 1: Make mint effects atomic and durable** — `a02ee765` (fix)
2. **Task 2: Prove mint replay and restart recovery** — `f5a300c1` (test)

Verification-driven hardening commits:

- `2142648a` — preserve invalid burn-hash coverage in the adapted marker-absence helper
- `05af8b1a` — reject incomplete, non-canonical, or unordered application records

## Files Created/Modified

- `src/account/proto/SGTransaction.proto` — adds persisted UTXO type metadata and the canonical bridge application record.
- `src/account/UTXOManager.hpp` — exposes application results/readback and private stage/read seams for focused fault tests.
- `src/account/UTXOManager.cpp` — implements gated, copied-state mint staging and one-batch durable application.
- `src/account/TransactionManager.hpp` — defines the atomic mint adapter and application synchronization.
- `src/account/TransactionManager.cpp` — delegates mint publication to atomic storage and verifies durable replay state.
- `test/account/UTXOManagerTest.cpp` — covers fault boundaries and overlapping ordinary persistence.
- `test/account/TransactionManagerPendingLifecycleTest.cpp` — covers duplicate delivery and manager/account reconstruction over the same database.
- `test/account/BridgeEventIdentityTest.cpp` — asserts the legacy execution marker is absent without parsing deliberately invalid identities.

## Decisions Made

- The application record is keyed only by canonical external event identity and version, so candidate-controlled output fields cannot create multiple completion authorities.
- Persistence synchronization begins before snapshot reads and remains held through batch commit; the in-memory UTXO lock is then acquired in one canonical order.
- A naked consumed bridge UTXO is not recoverable completion evidence. Only a fully verified application record yields `AlreadyApplied`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Adapted legacy-marker assertion parsed deliberately invalid burn identities**

- **Found during:** Full focused-suite verification
- **Issue:** The marker-absence helper constructed a canonical application key through the production parser, causing an existing invalid-identity test to throw before reaching its assertion.
- **Fix:** Made the helper validate lowercase hexadecimal syntax directly and treat invalid identities as incapable of producing an application key.
- **Files modified:** `test/account/BridgeEventIdentityTest.cpp`
- **Verification:** Full three-suite CTest selection passes.
- **Committed in:** `2142648a`

**2. [Rule 2 - Missing Critical] Application readback accepted structurally incomplete records**

- **Found during:** Final implementation audit
- **Issue:** Durable replay verification needed explicit rejection of empty outputs, non-canonical encodings, and unordered indexes.
- **Fix:** Added canonical reserialization and ordered, nonempty output validation before any record can prove prior application.
- **Files modified:** `src/account/UTXOManager.cpp`
- **Verification:** UTXO and transaction-manager suites pass.
- **Committed in:** `05af8b1a`

---

**Total deviations:** 2 auto-fixed (1 bug, 1 missing critical validation)
**Impact on plan:** Both fixes strengthen the requested fault and replay guarantees without expanding product scope.

## Issues Encountered

- The first complete focused-suite run exposed the invalid-identity helper exception described above; the helper was corrected and the entire selection reran successfully.
- Reconstructing blockchain-facing objects against the same global database emits duplicate certificate-filter registration warnings in the restart fixture, but all restart assertions and focused suites pass.

## Verification

- Focused pre-commit, post-batch, overlap, duplicate, and restart tests pass.
- `ctest --test-dir build/OSX/Release -R '(transaction_manager_pending_lifecycle|utxo_manager|bridge_event_identity)' --output-on-failure` passes 3/3.
- `git diff --check` passes.
- Persistence audit confirms gated ordinary stores, atomic raw batches, and gated checkpoint writes.
- Production search confirms the legacy `/bridge/executed` marker is absent.

## User Setup Required

None.

## Next Phase Readiness

- Plan 09-15 can build on a single durable authority for mint completion and a persistence boundary that survives concurrent ordinary stores.
- Remaining Phase 09 gap-closure plans are not blocked by this implementation.

## Self-Check: PASSED

- All four implementation/test commits are present.
- All eight planned source and test files are present.
- The complete focused CTest selection passes.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-24*
