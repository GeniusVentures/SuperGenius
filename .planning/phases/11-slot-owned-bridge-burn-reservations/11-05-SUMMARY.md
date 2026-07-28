---
phase: 11-slot-owned-bridge-burn-reservations
plan: 05
subsystem: blockchain-consensus
tags: [consensus, bridge, finality, reservations, safety-error]
requires:
  - phase: 11-04
    provides: post-validation durable slot-owned burn admission
provides:
  - certificate-bound FinalizedPendingApplication before handler or cleanup
  - typed exact-winner application disposition and durable SafetyError
  - certificate-only protection and restart-safe exact-winner retry
affects: [11-06, 11-07, 11-08]
tech-stack:
  added: []
  patterns: [finality-before-application, typed application disposition, durable safety stop]
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/Blockchain.hpp
    - src/blockchain/impl/Blockchain.cpp
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - test/src/blockchain/consensus_burn_reservation_test.cpp
    - test/src/blockchain/consensus_finalization_test.cpp
key-decisions:
  - "Resource-bearing certificate handlers return Applied, AlreadyApplied, Retryable, or Irreconcilable while legacy subject handlers retain Check semantics."
  - "An irreconcilable exact-winner application marks the certificate-bound reservation SafetyError and completes recovery work without proposal cleanup."
  - "Certificate authority remains valid when reservation persistence fails; application and cleanup wait for a later exact transition retry."
patterns-established:
  - "Authoritative mint certificates synthesize or finalize burn protection before process markers, handler leases, and cleanup."
  - "Duplicate ingress shares one slot processing lease and SafetyError short-circuits all later application attempts."
requirements-completed:
  - BURN-03
  - BURN-04
duration: 27 min
completed: 2026-07-28
---

# Phase 11 Plan 05: Certificate-Bound Burn Finality Summary

**Authoritative mint certificates now make the exact burn durably unavailable before any winner application or proposal cleanup, with retryable recovery and permanent contradiction handling kept distinct.**

## Performance

- **Duration:** 27 min
- **Started:** 2026-07-28T19:50:00Z
- **Completed:** 2026-07-28T20:17:00Z
- **Tasks:** 1
- **Files modified:** 8

## Accomplishments

- Moved the exact certificate/proposal/winner reservation transition ahead of the certificate processing marker, handler lease, and cleanup, including certificate-only nodes with no candidate history.
- Added typed exact-winner application dispositions while preserving existing non-resource certificate handlers.
- Kept transient failure pending across manager reconstruction and converted irreconcilable application contradictions into durable reservation SafetyError without releasing or cleaning the burn.
- Added deterministic coverage for ordering, transition failure, restart retry, duplicate ingress, certificate-only synthesis, and stopped SafetyError retries.

## Task Commits

1. **Task 1: Persist finalized burn protection before exact-winner work** - `5f380806` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Typed application disposition and handler registration contract.
- `src/blockchain/Consensus.cpp` - Pre-handler burn finalization, exact-winner retry, SafetyError, and duplicate lease sequencing.
- `src/blockchain/Blockchain.hpp` - Typed application-handler forwarding declaration.
- `src/blockchain/impl/Blockchain.cpp` - Typed application-handler forwarding implementation.
- `src/account/TransactionManager.hpp` - Typed certificate application result declaration.
- `src/account/TransactionManager.cpp` - Mint application failure classification and typed registration.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Finality ordering, failure, restart, safety, and duplicate-ingress tests.
- `test/src/blockchain/consensus_finalization_test.cpp` - Canonical unrelated-slot fixture correction.

## Decisions Made

- Left normal certificate handlers on the established `Check` contract and added a separate typed handler path only for exact resource application.
- Treated invalid or conflicting durable mint application identity as irreconcilable; operational/unavailable failures remain retryable.
- Kept physical UTXO consumption out of certificate observation; Plan 11-07 will join reservation consumption to the existing atomic mint effects batch.

## Deviations from Plan

### Auto-fixed Issues

**1. Corrected the unrelated-certificate finalization fixture**
- **Found during:** Full `consensus_finalization_test` regression
- **Issue:** The fixture used the same account and hardcoded nonce for its supposedly unrelated certificate, so it resolved to the safety-stopped slot and correctly failed certificate creation.
- **Fix:** Parameterized the helper nonce and used nonce 8 for the unrelated slot.
- **Files modified:** `test/src/blockchain/consensus_finalization_test.cpp`
- **Verification:** Full finalization regression passed 8/8.
- **Commit:** `5f380806`

---

**Total deviations:** 1 auto-fixed (Rule 3 test-fixture correctness)
**Impact:** Restored the test's documented unrelated-slot intent without changing production semantics.

## Issues Encountered

None beyond the fixture correction above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Ready for Plan 11-06 to establish the shared-store application handle and serialization contract.
- Physical reservation consumption remains intentionally deferred to Plan 11-07.

## Self-Check: PASSED

- Task commit `5f380806` exists and all eight planned files are present.
- Guarded Final/SafetyError/CertificateOnly discovery found a nonzero slice and all 8 selected tests passed.
- Full `consensus_finalization_test` passed 8/8.
- Transition-failure coverage proved zero handler and cleanup calls; restart recovery retained the exact proposal/winner.
- `git diff --check` and no-sleep/no-detach guards passed.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-28*
