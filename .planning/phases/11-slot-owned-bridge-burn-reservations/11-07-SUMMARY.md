---
phase: 11-slot-owned-bridge-burn-reservations
plan: 07
subsystem: account-utxo
tags: [consensus, rocksdb, atomicity, bridge, utxo, idempotency]
requires:
  - phase: 11-06
    provides: shared-store finalized application contract and exact reservation identity
provides:
  - one physical batch for certified mint effects and reservation consumption
  - exact replay classification across retry and restart
  - certificate-derived synthetic bridge input materialization
affects: [11-08, 11-09, phase-12, bridge-mint-application]
tech-stack:
  added: []
  patterns: [shared atomic batch, pre-stage participant view, exact durable replay]
key-files:
  created: []
  modified:
    - src/blockchain/ConsensusStateStore.hpp
    - src/blockchain/ConsensusStateStore.cpp
    - src/account/UTXOManager.hpp
    - src/account/UTXOManager.cpp
    - src/account/TransactionManager.cpp
    - test/src/blockchain/consensus_burn_reservation_test.cpp
    - test/src/account/utxo_manager_test.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp
key-decisions:
  - "The finalized participant observes the durable pre-stage reservation while Consumed is staged invisibly in the same physical batch."
  - "Certificate-only synthetic bridge inputs may be materialized only from certified transaction facts under the exact finalized reservation handle."
  - "AlreadyApplied requires durable Consumed state plus exact application, output, input-consumption, and winner identity agreement."
patterns-established:
  - "Reservation state, application record, winning outputs, and physical bridge consumption commit or fail together."
  - "Retry and restart replay accept only a complete exact artifact set; partial or contradictory state is not recoverable."
requirements-completed:
  - BURN-03
  - BURN-04
duration: 19 min
completed: 2026-07-28
---

# Phase 11 Plan 07: Atomic Certified Burn Application Summary

**Certified bridge mint effects and reservation consumption now cross one shared-store batch boundary, with exact retry and restart replay semantics.**

## Performance

- **Duration:** 19 min
- **Started:** 2026-07-28T20:42:00Z
- **Completed:** 2026-07-28T21:00:35Z
- **Tasks:** 1
- **Files modified:** 8

## Accomplishments

- Replaced the split reservation/application commits with one physical batch containing winning outputs, application record, physical bridge input consumption, and the reservation's `Consumed` transition.
- Derived synthetic bridge input facts from the certified mint transaction and admitted their materialization only through the exact finalized reservation participant.
- Added exact retry, restart replay, competing-writer serialization, and contradictory-winner/artifact coverage.
- Preserved the store-first, persistence-second, UTXO-state-third lock order and added diagnostics for identity and artifact contradictions.

## Task Commits

1. **Task 1: Atomically consume certified bridge burns with mint effects** - `86b5f288` (feat)

## Files Created/Modified

- `src/blockchain/ConsensusStateStore.hpp` - Unlocked consumed-transition preparation and shared-batch participant contract.
- `src/blockchain/ConsensusStateStore.cpp` - Exact Pending/Consumed validation and invisible `Consumed` staging in the participant batch.
- `src/account/UTXOManager.hpp` - Certified bridge identity fields and shared-batch atomic mint interface.
- `src/account/UTXOManager.cpp` - Atomic output, application, input-consumption, and replay validation logic.
- `src/account/TransactionManager.cpp` - Certified transaction fact derivation and exact finalized-handle forwarding.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Deterministic atomic-consumption serialization coverage.
- `test/src/account/utxo_manager_test.cpp` - Failure, retry, restart replay, and conflicting-winner coverage.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - End-to-end reservation consumption and artifact assertions.

## Decisions Made

- Passed the participant the live durable pre-stage record even though `Consumed` is already staged in its batch, allowing first application and exact replay to be distinguished without a split commit.
- Restricted synthetic input materialization to the finalized participant and sourced its owner, amount, token, and outpoint from the certified mint transaction.
- Classified any missing or conflicting artifact after durable consumption as `state_not_recoverable`; only a complete exact artifact set is idempotent.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- Running the UTXO and TransactionManager binaries concurrently caused their CRDT-backed fixtures to collide on shared `unit_test_*` paths. The prescribed sequential gate avoids that unrelated test-harness collision and passed completely.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 11-08 can build on one durable burn-consumption boundary and exact replay classification.
- Plan 11-09 and Phase 12 can rely on certificate-derived immutable bridge input identity.
- No blockers; protected user-owned working-tree changes remain untouched.

## Self-Check: PASSED

- Task commit `86b5f288` exists and all eight modified files are present.
- Focused consensus tests passed 6/6, `utxo_manager_test` passed 29/29, and `transaction_manager_pending_lifecycle_test` passed 17/17 sequentially.
- Discovery guard found six focused tests; source audit and `git diff --check` passed.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-28*
