---
phase: 11-slot-owned-bridge-burn-reservations
plan: 06
subsystem: blockchain-consensus
tags: [consensus, rocksdb, shared-ownership, atomicity, bridge]
requires:
  - phase: 11-05
    provides: certificate-bound FinalizedPendingApplication reservation identity
provides:
  - one shared ConsensusStateStore authority retained by consensus and carried to mint application
  - exact shared-object datastore identity rejection at the finalized batch boundary
  - one store-gated finalized participant contract with explicit lock ordering
affects: [11-07, 11-08, bridge-mint-application]
tech-stack:
  added: []
  patterns: [immutable authority handle, shared-object identity, insert-only resource handler, store-first lock order]
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/ConsensusStateStore.hpp
    - src/blockchain/ConsensusStateStore.cpp
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - test/src/blockchain/consensus_burn_reservation_test.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp
key-decisions:
  - "Finalized mint application receives an immutable value handle containing the exact live shared ConsensusStateStore and complete certificate-bound reservation identity."
  - "Datastore participation requires shared_ptr object identity; another wrapper over the same RocksDB database is rejected before participant mutation."
  - "Typed resource-application registration is insert-only, while ordinary nonce certificates continue through the unchanged legacy handler signature."
patterns-established:
  - "The finalized batch gate owns the store mutex across exact reread, identity validation, participant staging, and participant-owned commit."
  - "The total participant order is ConsensusStateStore gate, UTXO persistence lock, then UTXO state lock, with outer consensus and registry locks excluded."
requirements-completed:
  - BURN-03
  - BURN-04
duration: 18 min
completed: 2026-07-28
---

# Phase 11 Plan 06: Shared-Store Finalized Application Contract Summary

**Winning mint application now carries the exact live consensus reservation store through an immutable handle and enters one identity-checked serialization gate.**

## Performance

- **Duration:** 18 min
- **Started:** 2026-07-28T20:18:30Z
- **Completed:** 2026-07-28T20:36:40Z
- **Tasks:** 1
- **Files modified:** 8

## Accomplishments

- Replaced unique reservation-store ownership with one live shared authority and carried its exact instance plus slot, outpoint, generation, certificate digest, proposal, and winner to TransactionManager mint application.
- Added a narrow finalized batch gate that rejects same-database/different-shared-object participation before mutation and serializes participant work against every existing reservation writer.
- Made typed application-handler registration once-only, retained weak TransactionManager ownership, and preserved legacy non-resource certificate behavior.
- Added deterministic shared-instance, overwrite, expired-owner, datastore-identity, and paused-participant serialization coverage without sleeps.

## Task Commits

1. **Task 1: Establish one shared reservation store and immutable application contract** - `fbb95eec` (feat)
2. **Task 1 acceptance closure: Enforce UTXO shared-datastore identity at handoff** - `f7c27ae1` (fix)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Immutable finalized handle, typed callback contract, and shared store ownership.
- `src/blockchain/Consensus.cpp` - Shared store construction, insert-only registration, and exact handle creation at finality.
- `src/blockchain/ConsensusStateStore.hpp` - Finalized identity, typed identity error, participant gate, and lock-order contract.
- `src/blockchain/ConsensusStateStore.cpp` - Shared-object identity check and one-lock exact reread/participant batch boundary.
- `src/account/TransactionManager.hpp` - Finalized handle plumbing toward mint application and friend-only observation seam.
- `src/account/TransactionManager.cpp` - Weak resource callback, ordinary legacy callback, end-to-end handle forwarding, and UTXO datastore admission through the exact gate.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Shared authority, once-only registration, identity mismatch, and serialization tests.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Weak-owner expiry and exact handle arrival at mint application.

## Decisions Made

- Used shared-object identity rather than path or underlying database equivalence because the shared wrapper is the node's lock authority.
- Kept the resource application callback separate from the existing certificate handler; mint resources receive the required handle while ordinary subjects retain the established two-argument behavior.
- Left physical UTXO effects and reservation `Consumed` staging to Plan 11-07; this plan establishes only the ownership, identity, and serialization boundary.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The first end-to-end test attempt tried to build a bridge certificate with the account fixture's default registry, which lacks the focused bridge quorum configuration. The test was corrected to invoke the production callback registered through Blockchain, directly proving the intended Consensus-to-TransactionManager handle handoff without changing production behavior.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Ready for Plan 11-07 to stage winning outputs, bridge input consumption, application record, and reservation `Consumed` state through this exact shared-store gate.
- No blockers; protected user-owned working-tree changes remain untouched.

## Self-Check: PASSED

- Task commits `fbb95eec` and `f7c27ae1` exist and all eight modified files are present.
- Guarded `SharedStore|ApplicationHandle|DatastoreIdentity|SerializationGate` slice passed 4/4.
- Full `transaction_manager_pending_lifecycle_test` passed 17/17.
- Ownership/source guards, no-sleep/no-detach guard, and `git diff --check` passed.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-28*
