---
phase: 11-convergent-certificate-consumption-mint-recovery
plan: "01"
subsystem: consensus
tags: [c++17, consensus, crdt, certificate-recovery, tdd]

requires:
  - phase: 10-authoritative-slot-certificate-publication
    provides: "Durable canonical /cert/<slot> records and committed-readback certificate recovery"
provides:
  - "Retryable certificate work when its subject handler has not yet registered"
  - "Post-registration committed readback that dispatches an accepted certificate exactly once"
affects: [transaction-manager, certificate-consumption, mint-recovery]

tech-stack:
  added: []
  patterns:
    - "Certificate handlers trigger recovery only after releasing their registration mutex."
    - "Missing consumers retain the shared certificate work journal and active vote lock."

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp

key-decisions:
  - "Keep the existing certificate work journal as the sole retry boundary; no additional queue or finality state was introduced."
  - "Defer active-vote release until a matching certificate handler is available for durable recovery."

patterns-established:
  - "Consumer registration may resume durable certificate work after releasing its handler-map lock."

requirements-completed: [CERT-05]

duration: 6 min
completed: 2026-08-24
---

# Phase 11 Plan 01: Durable Certificate Handler Recovery Summary

**Durable canonical certificates now remain stalled until their registered consumer can replay committed data, then release the matching vote and finish exactly once.**

## Performance

- **Duration:** 6 min
- **Started:** 2026-08-24T12:57:17Z
- **Completed:** 2026-08-24T13:03:02Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- Added a deterministic certificate-before-handler lifecycle regression with no sleep-based synchronization.
- Retained missing-handler certificate work and its active vote lock rather than treating it as terminal.
- Reused the existing committed-readback recovery after handler registration, with no new certificate authority or journal.

## Task Commits

Each task was committed atomically:

1. **Task 1: Specify the durable certificate-before-handler replay lifecycle** - `0da7bc43` (test)
2. **Task 2: Retain no-handler certificate work and recover it after registration** - `dd6c443a` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.cpp` - Stalls unavailable-consumer work, defers vote release until a handler is present, and runs recovery after safe handler registration.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - Covers stalled-to-done recovery, one handler invocation, delayed vote release, and harmless replay.

## Decisions Made

- Reused `CRDTWorkJournal` and committed `/cert/<slot>` readback as the only acceptance/retry boundary.
- Performed post-registration recovery outside `certificate_handlers_mutex_` so recovery can safely read and dispatch handlers.
- Kept key, parse, quorum, and exact-binding validation in the existing recovery path before any vote release or dispatch.

## Verification

- RED: `ctest --test-dir build/OSX/Release -R '^consensus_pending_lifecycle_test$' --output-on-failure` failed as expected on the prior premature no-handler completion/unlock behavior.
- GREEN/final: `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test --parallel 4 && ctest --test-dir build/OSX/Release -R '^consensus_pending_lifecycle_test$' --output-on-failure` passed (20/20 tests).
- Confirmed the focused lifecycle test contains no sleep-based synchronization.

## TDD Gate Compliance

- RED commit present: `0da7bc43` (`test(11-01)`).
- GREEN commit present after RED: `dd6c443a` (`feat(11-01)`).
- No refactor commit was required.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Certificate recovery now remains live across the consensus-before-TransactionManager construction order.
- The shared committed-readback path is ready for downstream certificate consumption and Mint recovery work.

## Self-Check: PASSED

- `src/blockchain/Consensus.cpp` and `test/src/blockchain/consensus_pending_lifecycle_test.cpp` exist.
- Task commits `0da7bc43` and `dd6c443a` exist in git history.

---
*Phase: 11-convergent-certificate-consumption-mint-recovery*
*Completed: 2026-08-24*
