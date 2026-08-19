---
phase: 12-consensus-race-and-compatibility-verification
plan: 05
subsystem: testing
tags: [ctest, consensus, bridge-race, verification, flaky-test]

requires:
  - phase: 12-consensus-race-and-compatibility-verification
    provides: Plans 01–04 focused regressions and the mandatory isolated 11-node race
provides:
  - Run-4 full-suite report with focused-gate blocker evidence and root-cause analysis
affects: [phase-12 closure, TEST-01..06]

tech-stack:
  added: []
  patterns:
    - "Stop-gate discipline: focused-gate failure halts before full-suite characterization"

key-files:
  created: []
  modified:
    - .planning/phases/12-consensus-race-and-compatibility-verification/12-FULL-SUITE-REPORT.md

key-decisions:
  - "Flaky StructuredTraceUsesStableIdentityForExactReplay is a test-determinism defect (proposal_id relies on ms-timestamp uniqueness), recorded as evidence without patching per plan scope"

requirements-completed: []  # TEST-01..06 remain blocked — closure not achieved

# Metrics
duration: 11 min
completed: 2026-08-19
---

# Phase 12 Plan 05: Full-Suite Verification (Run 4) Summary

**Focused regression gate 6/7 with a nondeterministic replay-identity failure; stop gate fired before the isolated race and full suite — Phase 12 closure NOT achieved.**

## Performance

- **Duration:** 11 min (plan aborted at Task 1 stop gate; full suite not run)
- **Started:** 2026-08-19T20:12:02Z
- **Completed:** 2026-08-19T20:22:44Z
- **Tasks:** 1 of 2 executed (Task 2 blocked by Task 1 stop gate)
- **Files modified:** 1 (report only)

## Accomplishments

- Rebuilt/verified all 8 focused Phase 12 targets; build clean.
- Focused gate executed: 6 of 7 regressions pass; all targets discovered (`--no-tests=error` clean).
- Characterized the blocker precisely: `ConsensusFinalizationHarness.StructuredTraceUsesStableIdentityForExactReplay` fails ~1/3 of runs (isolated reruns: PASS/FAIL/PASS) because `CreateProposal` derives `proposal_id` from bytes whose only varying field is `CurrentTimeMs()`; same-millisecond competitor creation produces an identical `proposal_id`, failing `EXPECT_NE` at `consensus_finalization_test.cpp:691`.
- Run 3 fixes verified in place: `bridge_race_single_burn_test` has `TIMEOUT 900` + `RUN_SERIAL TRUE` in the generated CTestTestfile; bridge/Anvil fixtures rebuilt Aug 19 with a verified fork-RPC endpoint (availability recorded without URLs/credentials).

## Task Commits

1. **Task 1: Run focused regressions and the isolated real race** — `10e87088` (test) — focused gate run, blocker evidence recorded in 12-FULL-SUITE-REPORT.md; isolated race and full suite not run per stop gate.
2. **Task 2: Execute and account for the complete configured repository suite** — **NOT EXECUTED** (stop gate).

## Files Created/Modified

- `.planning/phases/12-consensus-race-and-compatibility-verification/12-FULL-SUITE-REPORT.md` — Run-4 report: build identity, commands, focused results, flaky-test root cause, requirement matrix, follow-ups.

## Decisions Made

- Applied the plan's stop gate literally: a focused-gate failure (even flaky) blocks characterizing the full suite as passing; evidence was gathered (3 isolated reruns) but no source or test code was modified, per the plan's report-only scope.

## Deviations from Plan

None — the plan was executed as written; the stop gate is part of the plan. (No auto-fixes applied; the flaky test is out of scope for this reporting plan.)

## Issues Encountered

- **BLOCKER (new in run 4):** `consensus_finalization_test` / `StructuredTraceUsesStableIdentityForExactReplay` is nondeterministic. Root cause: test assumes a distinct competitor `proposal_id`, but `ConsensusManager::CreateProposal` (`src/blockchain/Consensus.cpp:2222–2250`) builds the ID from proposal bytes differing only by millisecond timestamp; same-tick calls collide. Observed: gate run FAIL, isolated reruns PASS/FAIL/PASS. This violates the phase's no-scheduling-luck determinism requirement and must be fixed (distinct proposer/subject field in the competitor) before closure.
- Run-3 fixes (fixture rebuild + race `TIMEOUT 900`/`RUN_SERIAL`) are in place but their full-suite effect is unverified because the stop gate fired first.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- **Phase 12 closure remains BLOCKED.** Required: (1) fix the replay-identity test determinism (scoped follow-up decision), (2) re-run 12-05 fully: focused gate → isolated race → `ctest -N` inventory → one unfiltered full suite at `-j2`.
- TEST-01 through TEST-06 remain unmarked; the full-suite evidence leg is missing.

## Self-Check: FAILED

- FOUND: `.planning/phases/12-consensus-race-and-compatibility-verification/12-FULL-SUITE-REPORT.md` (run-4 report committed in `10e87088`).
- MISSING: full-suite execution evidence — stop gate fired at the focused gate; closure criteria (zero failures, zero unreviewed skips, complete TEST-01..06 matrix) not met.
- Closure NOT achieved; requirements TEST-01..06 NOT marked complete.

---
*Phase: 12-consensus-race-and-compatibility-verification*
*Completed: 2026-08-19 (run 4 — blocked)*
