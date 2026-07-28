---
phase: 11-slot-owned-bridge-burn-reservations
plan: 02
subsystem: blockchain-persistence
tags: [rocksdb, protobuf, bridge, reservations, aba-safety]
requires:
  - phase: 11-01
    provides: deterministic real-RocksDB reservation test harness
  - phase: 10
    provides: strict direct-RocksDB consensus local-state store
provides:
  - versioned reciprocal burn reservation and outpoint-index records
  - strict create/join/finalize/safety/consume-stage/delete store transitions
  - random-generation ABA protection and corruption coverage
affects: [11-03, 11-04, 11-05, 11-06, 11-07, 11-08]
tech-stack:
  added: []
  patterns: [reciprocal strict records, one-lock RocksDB batches, expected-generation deletion]
key-files:
  created: []
  modified:
    - src/blockchain/impl/proto/ConsensusLocalState.proto
    - src/blockchain/ConsensusStateStore.hpp
    - src/blockchain/ConsensusStateStore.cpp
    - test/src/blockchain/consensus_burn_reservation_test.cpp
key-decisions:
  - "Burn reservations use reciprocal slot and hashed canonical-outpoint keys under /consensus/local/v2/burn and never enter replicated storage."
  - "Reservation generations are 256-bit lowercase hexadecimal values produced by the existing libp2p cryptographic random generator."
  - "Consumed state is validated and staged into a caller-owned batch so later mint application can preserve one physical commit boundary."
patterns-established:
  - "Strict reciprocal reads: a slot or outpoint half is never accepted without its exact generation-matched sibling."
  - "Finality monotonicity: FinalizedPendingApplication, Consumed, and SafetyError never transition back to Reserved or absent."
requirements-completed:
  - BURN-01
  - BURN-02
  - BURN-03
  - BURN-04
  - BURN-05
duration: 18 min
completed: 2026-07-28
---

# Phase 11 Plan 02: Durable Burn Reservation Store Summary

**Strict reciprocal RocksDB records now bind each canonical mint slot to one exact burn outpoint with cryptographic generation tokens and monotonic finality transitions.**

## Performance

- **Duration:** 18 min
- **Started:** 2026-07-28T19:05:30Z
- **Completed:** 2026-07-28T19:23:33Z
- **Tasks:** 1
- **Files modified:** 4

## Accomplishments

- Added versioned reservation and reciprocal outpoint-index protobufs with canonical slot, chain, burn, receipt-log index, generation, horizon, timestamps, and state-specific finality fields.
- Added strict typed store APIs for lookup, scan, create-or-join, finalization, safety error, caller-batch consumption staging, and generation-conditional deletion.
- Added deterministic coverage for atomic two-key failure, canonical corruption, missing/mismatched halves, idempotent horizon extension, illegal regressions, and recreate-after-delete ABA safety.

## Task Commits

1. **Task 1: Add strict reciprocal reservation records and transitions** - `ef5683f4` (feat)

## Files Created/Modified

- `src/blockchain/impl/proto/ConsensusLocalState.proto` - Versioned burn reservation lifecycle and reciprocal index schemas.
- `src/blockchain/ConsensusStateStore.hpp` - Typed public descriptor/results and reservation transition APIs.
- `src/blockchain/ConsensusStateStore.cpp` - Canonical validation, reciprocal reads/scans, one-lock mutations, CSPRNG generations, and ABA-safe deletion.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Real-RocksDB strict store, generation, transition, corruption, and atomic-failure tests.

## Decisions Made

- Used the SHA-256 canonical mint outpoint identity as the outpoint-index suffix, while retaining a distinct direct-local namespace and typed reciprocal value.
- Used libp2p's `BoostRandomGenerator` CSPRNG for fresh 256-bit generations rather than a restartable counter.
- Kept physical consumption as a caller-owned batch staging operation; the reservation store does not perform a separate consumption commit.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The strict durable authority is ready for Plan 11-03 startup restore and reconciliation.
- No blockers; protected user-owned working-tree changes remain untouched.

## Self-Check: PASSED

- All four key files exist and task commit `ef5683f4` is present.
- Guarded focused build and `*Store*:*Generation*:*Release*` run passed 8/8 tests.
- Reciprocal namespace source audit found no `GlobalDB::Put`, `Publish`, or `Broadcast` path.
- `git diff --check` passed.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-28*
