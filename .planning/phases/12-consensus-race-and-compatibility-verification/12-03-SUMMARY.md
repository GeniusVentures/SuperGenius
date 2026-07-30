---
phase: 12-consensus-race-and-compatibility-verification
plan: 03
subsystem: consensus-testing
tags: [consensus, durability, restart, certificate-index, compatibility]

requires:
  - phase: 10-durable-vote-lock-and-finalization-state-machine
    provides: Durable per-validator vote journal and controlled candidate deadline
  - phase: 11-slot-owned-bridge-burn-reservations
    provides: Slot-indexed authoritative certificates and real transaction consumer paths
provides:
  - True same-database manager reconstruction proof for one-vote-per-slot locking
  - Controlled-clock proof of candidate replacement before and after publication
  - Sleep-free certificate-store correctness coverage with typed immutable corruption handling
  - Verified previous-nonce and producer-UTXO consumers through the authoritative slot index
affects: [12-04-eleven-node-race, phase-12-verification]

tech-stack:
  added: []
  patterns: [true owner destruction before restart, controlled deadline driving, typed non-log diagnostics]

key-files:
  created: []
  modified:
    - test/src/blockchain/consensus_vote_journal_test.cpp
    - test/src/blockchain/consensus_certificate_store_test.cpp
    - test/src/blockchain/certificate_compatibility_test.cpp

key-decisions:
  - "Restart coverage proves destruction with weak ownership and compares the replacement manager's replay against exact durable envelope bytes."
  - "Certificate corruption diagnostics assert the typed error category and nonempty status message while authority immutability is proven byte-for-byte."
  - "The existing TransactionManager replay-protection and GeniusInputValidator witness tests remain the authoritative real-consumer compatibility gates."

patterns-established:
  - "Deadline correctness advances the explicit controlled clock and deadline processor; it never relies on elapsed wall-clock sleeps."
  - "Corrupt index reads are injected at the storage seam, then the real reader proves the original authority remains resolvable."

requirements-completed: [TEST-03, TEST-04, TEST-05, TEST-06]

duration: 7 min
completed: 2026-07-30
---

# Phase 12 Plan 03: Durable Restart and Compatibility Verification Summary

**True manager reconstruction preserves exact vote identity, controlled deadlines cannot authorize a second target, and malformed certificate indexes leave real consumer authority unchanged.**

## Performance

- **Duration:** 7 min
- **Started:** 2026-07-30T16:58:29Z
- **Completed:** 2026-07-30T17:05:46Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments

- Drove a real vote through persistence and publication, destroyed its manager, reconstructed over the same database, and proved exact replay without signer invocation or same-slot replacement.
- Proved a strictly better candidate wins before the controlled deadline while an even better post-publication candidate cannot change signer count, publication tuple, or durable journal bytes.
- Removed every `sleep_for` correctness dependency from the certificate-store suite and retained immediate observable predicates and synchronous invariants.
- Added typed malformed-index coverage that preserves authoritative certificate and index bytes, exposes a useful error status, and restores the same canonical lookup result.
- Confirmed the real previous-nonce and producer-UTXO consumers retain their approve/pending/reject semantics through slot-index-backed lookup.

## Task Commits

1. **Task 1: Prove reconstructed vote locks and deadline ordering** - `02287456` (test)
2. **Task 2: Close index corruption and real-consumer compatibility coverage** - `115f5604` (test)

## Files Created/Modified

- `test/src/blockchain/consensus_vote_journal_test.cpp` - True reconstruction, exact replay, competing-proposal rejection, and pre/post-publication controlled deadline proofs.
- `test/src/blockchain/consensus_certificate_store_test.cpp` - Predicate-driven registry readiness, synchronous duplicate/error invariants, and typed diagnostic assertions without correctness sleeps.
- `test/src/blockchain/certificate_compatibility_test.cpp` - Malformed index injection with byte-stable authority and successful canonical lookup restoration.

## Decisions Made

- Used a weak pointer to prove the original manager's ownership graph is destroyed before replacement construction; no in-memory vote state is copied.
- Compared serialized vote and envelope bytes plus canonical proposal identity instead of timing, log text, or callback scheduling.
- Preserved the v2.0 clean-state compatibility boundary: legacy/malformed namespaces remain rejected while canonical slot and transaction-hash lookup behavior stays unchanged.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Durable restart, deadline ordering, typed index corruption, and real-consumer compatibility gates are ready for the 11-node race and final full-suite closure.
- Plan 12-02 remains the earliest incomplete Phase 12 plan.

## Verification

- Exact restart/deadline discovery and execution: 2/2 passed.
- `consensus_certificate_store_test`: 24/24 passed.
- `certificate_compatibility_test`: 18/18 passed.
- Exact TransactionManager consumer regressions: 2/2 passed.
- Certificate-store `sleep_for` scan: none found.
- `git diff --check`: passed.

## Self-Check: PASSED

---
*Phase: 12-consensus-race-and-compatibility-verification*
*Completed: 2026-07-30*
