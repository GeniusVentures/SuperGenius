---
phase: 11-slot-owned-bridge-burn-reservations
plan: 08
subsystem: consensus-lifecycle
tags: [consensus, bridge, reservation, abandonment, aba, shutdown]
requires:
  - phase: 11-07
    provides: atomic certified burn application and reservation consumption
provides:
  - timer-owned strict-horizon reservation reconciliation
  - generation-and-horizon conditional reciprocal deletion
  - admission, finality, ABA, and shutdown race closure
affects: [11-09, phase-12, bridge-consensus-safety]
tech-stack:
  added: []
  patterns: [owned reconciliation loop, strict horizon, expected-generation delete, lifecycle drain]
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/ConsensusStateStore.hpp
    - src/blockchain/ConsensusStateStore.cpp
    - test/src/blockchain/consensus_burn_reservation_test.cpp
key-decisions:
  - "Reservation reconciliation is one owned consensus-timer activity serialized through an explicit Reconciling slot lifecycle."
  - "Deletion requires exact certificate NotFound, strict passage beyond candidate and active-vote horizons, and matching generation plus persisted candidate horizon."
  - "Shutdown cancellation restores the prior lifecycle and drains the reconciliation activity before any durable deletion."
patterns-established:
  - "Admission and authoritative finality wait behind reconciliation, while stale store work loses to generation, horizon, or state changes."
  - "Terminal SafetyViolation remains intact when the original authoritative winner retries application."
requirements-completed:
  - BURN-02
  - BURN-03
  - BURN-05
duration: 51 min
completed: 2026-07-28
---

# Phase 11 Plan 08: Safe Burn Abandonment and Race Closure Summary

**Only provably abandoned uncertified burn generations now release, with strict horizon, finality, ABA, and shutdown protection.**

## Performance

- **Duration:** 51 min
- **Started:** 2026-07-28T21:01:30Z
- **Completed:** 2026-07-28T21:52:23Z
- **Tasks:** 1
- **Files modified:** 5

## Accomplishments

- Added reservation reconciliation to the owned consensus timer with an activity lease and explicit `Reconciling` lifecycle, without detached or per-slot workers.
- Required strict `now > horizon` passage, exact typed certificate absence, durable active-vote retirement, and a final certificate recheck before deletion.
- Strengthened reciprocal deletion with expected generation and persisted candidate-horizon matching so admission renewal, finality, and recreated generations defeat stale cleanup.
- Added deterministic equality, strict-passage, lookup uncertainty, admission extension, finality race, ABA, vote horizon, and paused-shutdown coverage.

## Task Commits

1. **Task 1: Reconcile and conditionally abandon only provably dead generations** - `a247382d` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Reconciliation lifecycle, owned method, and friend-only deterministic test seams.
- `src/blockchain/Consensus.cpp` - Timer reconciliation, strict evidence checks, whole-slot cleanup, finality serialization, and shutdown cancellation.
- `src/blockchain/ConsensusStateStore.hpp` - Expected candidate-horizon input for conditional deletion.
- `src/blockchain/ConsensusStateStore.cpp` - Under-gate generation, state, reciprocal, and horizon recheck before one-batch deletion.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Deterministic abandonment, vote, race, ABA, uncertainty, and shutdown matrix.

## Decisions Made

- Reused the existing resource-admission barrier while a slot is `Reconciling`, preventing candidate visibility or first-observation finality from crossing the release decision.
- Treated certificate lookup errors other than exact `NotFound` as durable uncertainty and retained the reservation.
- Preserved `SafetyViolation` during exact authoritative-winner retry after conflict evidence, rather than allowing retry serialization to regress the terminal participation state.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The first test compile needed the existing `storage/database_error.hpp` declaration for the certificate-read fault seam; adding the direct include resolved it.
- The first finalization regression run exposed that authoritative retry serialization could overwrite `SafetyViolation`. The lifecycle transition was corrected to preserve terminal safety state, and the complete finalization suite then passed.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 11-09 can audit all BURN requirements against deterministic release, restart, finality, and shutdown evidence.
- Phase 12 can rely on one bounded local reservation lifecycle for the complete network race proof.
- No blockers; protected user-owned working-tree changes remain untouched.

## Self-Check: PASSED

- Task commit `a247382d` exists and all five modified files are present.
- Guarded abandonment/race filter passed 6/6; vote journal passed 31/31; pending lifecycle passed 7/7; finalization passed 8/8.
- Nonzero discovery, no-sleep/no-detach source guards, and `git diff --check` passed.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-28*
