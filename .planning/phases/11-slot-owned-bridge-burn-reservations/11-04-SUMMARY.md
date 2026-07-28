---
phase: 11-slot-owned-bridge-burn-reservations
plan: 04
subsystem: blockchain-consensus
tags: [consensus, bridge, reservations, admission, pending-lifecycle]
requires:
  - phase: 11-03
    provides: startup reconciliation and canonical mint slot resolution
  - phase: 11-02
    provides: durable reciprocal burn reservation records
provides:
  - post-validation durable resource admission before candidate visibility
  - canonical mint resource descriptors independent of candidate identity
  - proposal-local pending expiry without fabricated finality
affects: [11-05, 11-06, 11-07, 11-08]
tech-stack:
  added: []
  patterns: [post-validation admission barrier, slot-owned resources, proposal-local cleanup]
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
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp
key-decisions:
  - "Canonical resource descriptors exclude candidate identity; consensus owns durable reservation persistence."
  - "The per-slot admission barrier spans descriptor extraction and store admission through candidate activation, and finalization waits for it."
  - "Pending TTL cleanup removes proposal-local state without invoking finality or releasing slot-owned reservations."
patterns-established:
  - "Every initial and retry Approve path durably creates or joins the burn reservation before candidate visibility."
  - "Same-slot contenders share one durable reservation generation while candidate selection remains independent."
requirements-completed:
  - BURN-01
  - BURN-02
  - BURN-03
  - BURN-05
duration: 17 min
completed: 2026-07-28
---

# Phase 11 Plan 04: Post-Validation Burn Admission Summary

**Approved bridge mints now durably join their canonical slot-owned burn reservation before becoming active candidates, while proposal-local failure and expiry cannot release that shared protection.**

## Performance

- **Duration:** 17 min
- **Started:** 2026-07-28T19:45:02Z
- **Completed:** 2026-07-28T20:02:07Z
- **Tasks:** 1
- **Files modified:** 9

## Accomplishments

- Added a post-validation admission barrier to every initial, scheduled retry, and dependency-resume Approve path, persisting or joining the canonical burn before candidate insertion.
- Added strict mint-v2 descriptor extraction that cross-checks the embedded input, DAG uncle hash, receipt-log index, and canonical slot without candidate-controlled identity fields.
- Removed mint-v2 ownership from local UTXO reserve/rollback and proposal cleanup paths while retaining ordinary transfer, escrow, and migration behavior.
- Made pending expiry purely proposal-local and added deterministic coverage for ordering, write failure, contender generation sharing, and cleanup invariants.

## Task Commits

1. **Task 1: Admit canonical bridge burns after validation and before candidate activation** - `36a44ec7` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Resource-admission API, per-slot barrier, and reservation query contract.
- `src/blockchain/Consensus.cpp` - Durable pre-activation admission, finalizer coordination, and proposal-local expiry.
- `src/blockchain/Blockchain.hpp` - Public forwarding API for resource admission and reservation lookup.
- `src/blockchain/impl/Blockchain.cpp` - Consensus forwarding implementations.
- `src/account/TransactionManager.hpp` - Canonical resource descriptor declaration.
- `src/account/TransactionManager.cpp` - Descriptor registration/extraction and consensus-owned bridge reservation behavior.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Admission ordering, failure, contender, and cleanup tests.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - Pending expiry without false finality regression.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Descriptor and durable bridge-state coverage with ordinary behavior preserved.

## Decisions Made

- Resource admission keys only the immutable chain id, burn transaction hash, and receipt-log index; proposal identity, nonce, validator, and destination do not create separate reservations.
- The admission in-flight marker remains held until candidate activation, closing the store-to-memory visibility gap and making finalization wait for a coherent state.
- Store or descriptor failure rejects activation and removes only orphaned ephemeral tracking; it never rolls back a slot-owned durable record.

## Deviations from Plan

### Auto-fixed Issues

**1. Canonicalized the pending-lifecycle validator fixture**
- **Found during:** Full prescribed regression gate
- **Issue:** The fixture used a non-canonical validator id, so the Phase 11-03 built-in ordinary nonce slot resolver rejected it before the pending lifecycle assertions could run.
- **Fix:** Replaced it with the canonical 128-character account-id form used by consensus.
- **Files modified:** `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
- **Verification:** Full `consensus_pending_lifecycle_test` passed 7/7.

---

**Total deviations:** 1 auto-fixed (Rule 3 correctness fixture)
**Impact on plan:** Required only to exercise the intended canonical resolver; no production scope expansion.

## Issues Encountered

None beyond the canonical test-fixture correction above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Ready for Plan 11-05 to build on durable pre-candidate reservation ownership.
- No blockers; protected user-owned working-tree changes remain untouched.

## Self-Check: PASSED

- Task commit `36a44ec7` exists and all nine planned files are present.
- The nonzero discovery guard found five Admission/Contender/Cleanup cases; all 5 passed.
- Full `consensus_pending_lifecycle_test` passed 7/7 and full `transaction_manager_pending_lifecycle_test` passed 15/15.
- Remaining reserve/rollback calls are ordinary transfer/escrow or legacy migration paths; `git diff --check` passed.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-28*
