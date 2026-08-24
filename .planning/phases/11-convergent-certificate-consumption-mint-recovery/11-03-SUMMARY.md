---
phase: 11-convergent-certificate-consumption-mint-recovery
plan: "03"
subsystem: transaction-manager
tags: [c++17, consensus-certificate, mint-v2, rocksdb, recovery, tdd]

requires:
  - phase: 11-02
    provides: exact certificate-first Mint winner selection and binding
provides:
  - durable Mint V2 completion ordering of effects, marker, then terminal tracking
  - retryable marker-write failures through the existing certificate work journal
  - real callback-to-readback recovery coverage for registered TransactionManager handlers
affects: [mint-finality, certificate-recovery, phase-12-fault-proof]

tech-stack:
  added: []
  patterns:
    - certificate work remains retryable while Mint V2 persistence is incomplete
    - friend-only test access reaches the owned ConsensusManager without production getters

key-files:
  created: []
  modified:
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - src/blockchain/Blockchain.hpp
    - src/blockchain/Consensus.hpp
    - test/src/account/transaction_manager_certificate_fallback_test.cpp

key-decisions:
  - "Use the existing certificate work journal as the sole retry mechanism; no Mint or finality journal was added."
  - "For Mint V2, keep transaction tracking VERIFYING until idempotent effects and the existing bridge marker both persist."
  - "Expose only friend-scoped test access to the fixture-owned ConsensusManager; production APIs remain unchanged."

patterns-established:
  - "Certified Mint V2 completion is durable UTXOs, durable bridge marker, then CONFIRMED."

requirements-completed: [MINT-01, MINT-02]

duration: 8 min
completed: 2026-08-24
---

# Phase 11 Plan 03: Mint Marker Recovery Summary

**Certified Mint V2 effects now persist idempotently before the bridge marker and become terminal only after certificate-journal recovery can prove both durable.**

## Performance

- **Duration:** 8 min
- **Started:** 2026-08-24T13:21:35Z
- **Completed:** 2026-08-24T13:30:27Z
- **Tasks:** 2/2
- **Files modified:** 5

## Accomplishments

- Added a deterministic marker-only failure regression that uses `CertificateReceived`, committed `/cert/<slot>` readback, and the handler registered by `TransactionManager::New`.
- Made Mint V2 output and bridge-input persistence errors propagate through the existing certificate work journal instead of permitting terminal confirmation.
- Enforced the durable completion order: idempotent UTXO effects, existing `/bridge/executed/<chain>:<source>` marker, then `CONFIRMED` tracking.

## Task Commits

1. **Task 1: Specify marker-only failure recovery through the real certificate ingress path** — `040c2d2d` (`test`)
2. **Task 2: Make Mint V2 terminal confirmation follow durable effects and marker** — `07231d16` (`feat`)

## Files Created/Modified

- `src/account/TransactionManager.hpp` — Declares friend-only marker persistence and deterministic failure seam.
- `src/account/TransactionManager.cpp` — Propagates Mint V2 persistence errors and orders effects, marker, and terminal confirmation.
- `src/blockchain/Blockchain.hpp` and `src/blockchain/Consensus.hpp` — Grant narrow fixture access to the owned manager and journal hooks.
- `test/src/account/transaction_manager_certificate_fallback_test.cpp` — Proves callback-to-durable-readback recovery completes exactly once after marker writes recover.

## Decisions Made

- Reused the existing `CRDTWorkJournal`; `MarkDone` removes completed work rather than introducing a new completed-state record.
- Preserved a retryable `VERIFYING` tracked state while a certified Mint is partially durable.
- Kept the marker failure seam private and reachable only through `CertificateFallbackTestAccess`.

## TDD Gate Compliance

- RED commit present: `040c2d2d` (`test(11-03)`) — the absent friend-only access and marker seam caused the focused target to fail as expected.
- GREEN commit present after RED: `07231d16` (`feat(11-03)`) — focused tests passed after durable ordering and error propagation were implemented.
- No refactor commit was required.

## Verification

- `cmake --build build/OSX/Release --target transaction_manager_certificate_fallback_test --parallel 1` passed.
- `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test --parallel 4` passed.
- `ctest --test-dir build/OSX/Release -R '^transaction_manager_certificate_fallback_test$' --output-on-failure` passed (1/1, 25.64s).
- `ctest --test-dir build/OSX/Release -R '^consensus_pending_lifecycle_test$' --output-on-failure` passed (1/1, 23.57s).
- The requested combined CTest run started both targets, but the executor's 30-second command cell ended after the fallback target passed; individual focused CTest runs completed successfully.
- Static checks confirm `ParseTransaction` precedes `PersistBridgeExecutedMarker`, which precedes terminal `CONFIRMED`, and the new end-to-end test has no direct `OnConsensusCertificate` assertion.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Mint completion now repairs an absent marker after durable UTXO effects without duplicating the Mint outcome.
- Phase 12 can exercise this certificate-consumption path under multi-node delivery and restart faults.

## Self-Check: PASSED

- All five planned source/test files exist.
- Task commits `040c2d2d` and `07231d16` exist in git history.
