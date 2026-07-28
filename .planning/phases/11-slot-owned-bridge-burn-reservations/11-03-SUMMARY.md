---
phase: 11-slot-owned-bridge-burn-reservations
plan: 03
subsystem: blockchain-consensus
tags: [rocksdb, consensus, restart, bridge, reservations]
requires:
  - phase: 11-02
    provides: strict reciprocal durable burn reservation records and transitions
  - phase: 10
    provides: fail-closed local-state restoration and durable vote horizons
provides:
  - canonical nonce/mint resolver available before strict consensus restoration
  - pre-side-effect reservation, certificate, and vote reconciliation
  - certificate-only synthesis of FinalizedPendingApplication burn protection
affects: [11-04, 11-05, 11-06, 11-07, 11-08]
tech-stack:
  added: []
  patterns: [insert-only handler registration, startup fail-closed reconciliation, certificate-only protection]
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/impl/Blockchain.cpp
    - test/src/blockchain/consensus_burn_reservation_test.cpp
    - test/src/blockchain/consensus_vote_journal_test.cpp
key-decisions:
  - "ConsensusManager::New guarantees the built-in nonce/mint resolver before restoration, while Blockchain::New installs it explicitly before manager construction."
  - "Slot-key handler registration is insert-only; replacement requires explicit unregistration."
  - "Authoritative mint certificates synthesize missing local FinalizedPendingApplication reservations before handler recovery."
patterns-established:
  - "Reserved records survive restart without ephemeral candidates, and active vote horizons extend rather than release protection."
  - "Every reciprocal, slot, certificate, and finality contradiction fails before subscribe/filter/timer/replay side effects."
requirements-completed:
  - BURN-01
  - BURN-04
  - BURN-05
duration: 17 min
completed: 2026-07-28
---

# Phase 11 Plan 03: Startup Burn-Reservation Reconciliation Summary

**Consensus startup now restores canonical burn protection before live activity, synthesizing certificate-backed finality and preserving candidate/vote horizons without ephemeral state.**

## Performance

- **Duration:** 17 min
- **Started:** 2026-07-28T19:23:40Z
- **Completed:** 2026-07-28T19:40:32Z
- **Tasks:** 1
- **Files modified:** 5

## Accomplishments

- Installed a built-in canonical nonce/mint slot resolver before `RestoreLocalState`, independent of `TransactionManager`, account UTXO state, or prior static-map residue.
- Strict-scanned reciprocal reservations with votes, processes, conflicts, safety state, and certificates before subscription, filters, timers, recovery, or vote replay.
- Reconciled exact burn descriptors and finality identity, synthesized certificate-only `FinalizedPendingApplication` protection, and extended Reserved horizons from durable active votes.
- Added deterministic restart, registration, certificate-only, vote-horizon, and corrupt-startup coverage plus the Phase 10 query-failure regression.

## Task Commits

1. **Task 1: Make canonical mint slot resolution a startup prerequisite and reconcile reservations** - `f43af6dc` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Insert-only slot resolver registration and built-in installation contract.
- `src/blockchain/Consensus.cpp` - Canonical mint decoding plus strict reservation/certificate/vote startup reconciliation.
- `src/blockchain/impl/Blockchain.cpp` - Resolver installation before consensus construction.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Fresh-registration, restart, certificate-only, horizon, and corrupt-startup tests.
- `test/src/blockchain/consensus_vote_journal_test.cpp` - Reservation scan failure included in the typed startup-query regression.

## Decisions Made

- Kept the built-in resolver at the consensus construction boundary so direct `ConsensusManager::New` callers and full `Blockchain::New` startup share the same fresh-process guarantee.
- Used exact embedded mint chain/hash/receipt-log data for reconciliation; generic nonce fallback applies only when the nonce subject is not a mint-v2 transaction.
- Missing candidates and handlers remain normal restart states; durable contradictions remain terminal.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The certificate-only fixture initially lacked sufficient bridge slot-hash quorum data. The deterministic one-validator registry fixture was configured with one-member public groups and signed all three slot hashes; production behavior was unchanged.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Ready for Plan 11-04 to establish post-validation, pre-candidate durable admission.
- No blockers; protected user-owned working-tree changes remain untouched.

## Self-Check: PASSED

- Task commit `f43af6dc` exists and all five planned files are present.
- Exact discovery guard found five restart/reconciliation/startup/horizon tests; all 5 passed.
- Full `consensus_vote_journal_test` regression passed 31/31.
- `git diff --check` and no-sleep/no-detach test guards passed.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-28*
