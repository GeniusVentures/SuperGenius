---
phase: 12-consensus-race-and-compatibility-verification
plan: 05
subsystem: testing
tags: [ctest, e2e, bridge, anvil, consensus, verification]

requires:
  - phase: 12-consensus-race-and-compatibility-verification
    provides: focused regression targets from plans 12-01..12-04
provides:
  - Reproducible Phase 12 full-suite audit artifact (12-FULL-SUITE-REPORT.md)
  - Confirmation that the evmrelay 4787e582 destination-byte-order fix resolves the run-2 race blocker
  - Precisely recorded external-prerequisite blockers for scoped follow-up
affects: [phase-12-closure, milestone-v2.0-completion]

tech-stack:
  added: []
  patterns:
    - "Dynamic ctest inventory + single unfiltered full-suite invocation as closure gate"
    - "Direct external-prerequisite verification (anvil fork probe) as failure root-cause evidence"

key-files:
  created: []
  modified:
    - .planning/phases/12-consensus-race-and-compatibility-verification/12-FULL-SUITE-REPORT.md

key-decisions:
  - "Fixture/setup failures are recorded as blockers, never as reviewed skips, per plan rules"
  - "External prerequisite regression (drpc free-plan Sepolia withdrawal) root-caused by direct Anvil invocation, not patched (plan forbids source changes)"

requirements-completed: []

duration: 46 min
completed: 2026-08-19
---

# Phase 12 Plan 05: Full-Suite Verification Summary

**Focused gate 7/7 and isolated 11-node race PASS after evmrelay fix, but the unfiltered 98-test suite recorded 7 failures (6 drpc prerequisite-regression fixture failures + 1 race timeout) — Phase 12 closure NOT achieved.**

## Performance

- **Duration:** 46 min
- **Started:** 2026-08-19T15:24:32Z
- **Completed:** 2026-08-19T16:10:05Z
- **Tasks:** 2
- **Files modified:** 1

## Accomplishments

- Verified the `evmrelay` update to `4787e582` (`fix(eth): preserve bridge destination byte order`) resolves the run-2 deterministic blocker: `bridge_race_single_burn_test` passed isolated `-j1` in 271.33s with correct mint destination and clean 11-node teardown.
- Focused Phase 12 gate passed 7/7 (90.46s) with all owning targets rebuilt against the updated submodule.
- Built the 4 previously unbuilt test executables and executed one unfiltered `ctest -j2` run over the full 98-test dynamic inventory: 91 passed, 7 failed, zero Not Run, zero crashes.
- Root-caused the 6 bridge/Anvil fixture failures to an external prerequisite regression — `sepolia.drpc.org` withdrew Sepolia from its free plan (HTTP 400, code 35), verified by direct Anvil invocation and serial re-run — and recorded them as blockers, not skips.

## Task Commits

Each task was committed atomically:

1. **Task 1: Focused regressions and isolated real race** - `4ffc55df` (test)
2. **Task 2: Complete configured repository suite** - `aca0d1ea` (test)

**Plan metadata:** recorded below (docs commit)

## Files Created/Modified

- `.planning/phases/12-consensus-race-and-compatibility-verification/12-FULL-SUITE-REPORT.md` - Reproducible build identity, 98-test dynamic inventory, focused/isolated/full-suite results, failure root causes, reviewed prerequisite skips, TEST-01..06 requirement matrix, and closure decision

## Decisions Made

- Recorded the six Anvil-fixture failures as blockers rather than reviewed skips: the plan explicitly states a fixture/setup failure is not an acceptable skip, and this plan's scope forbids source changes (fork URLs live in test sources).
- Recorded the full-suite race timeout (500.04s under `-j2` load, vs 271.33s isolated pass) as a blocking state requiring a scoped follow-up decision (timeout budget vs CTest resource-lock serialization).

## Deviations from Plan

None - plan executed exactly as written. No source code was modified; failures were recorded as the plan's stop/report rules require.

## Issues Encountered

- **Run-2 blocker resolved:** the mint-destination mismatch was fixed by the `evmrelay` submodule update (`62a9bbb1` → `4787e582`); both the focused gate and the isolated race pass.
- **New external prerequisite regression:** `sepolia.drpc.org` free plan no longer serves Sepolia. Six tests (`bridge_anvil_e2e_test`, `bridge_anvil_catchup_e2e_test`, `bridge_race_batch_test`, `bridge_race_fault_rpc_test`, `bridge_race_fault_kill_test`, `bridge_race_fault_partition_test`) fail deterministically at fixture setup. Requires a scoped follow-up to repoint fork URLs, then an unfiltered re-run.
- **Race timeout under `-j2` full-suite load:** the 11-node race exceeded its configured 500s timeout when overlapped with other tests. Requires a scoped follow-up on timeout budget and/or Anvil-test serialization.
- **Reviewed prerequisite skips (acceptable):** `bridge_e2e_test` (`RUN_E2E_BRIDGE` absent), `bridge_rlpx_e2e_test` (`RUN_E2E_RLPX` absent), `bridge_sepolia_e2e_test` (live signing unavailable; `DISABLED_` body) — each verified directly this window.

## User Setup Required

None - no external service configuration required by this plan. (Follow-up work will need a working public Sepolia fork endpoint configured in the six bridge/Anvil fixtures.)

## Next Phase Readiness

- Phase 12 closure remains **BLOCKED**: TEST-01 and TEST-06 lack a clean full-suite result; TEST-02..05 pass in both focused and full-suite evidence.
- Follow-up scope: (1) repoint the six fixtures' fork URLs to a working endpoint, (2) decide race timeout/serialization, (3) re-run the unfiltered suite per Plan 12-05.

## Self-Check: FAILED

Plan closure was not achieved — the plan's acceptance criteria require zero failures, zero timeouts, zero `Not Run`, and zero unreviewed skips in the unfiltered full-suite run; the run recorded 6 fixture failures and 1 timeout. The failure is external to the repository code (public RPC provider policy change) plus a load-sensitive timeout, both recorded precisely in `12-FULL-SUITE-REPORT.md` with reproduction evidence.

Verified artifacts and commits:
- FOUND: `.planning/phases/12-consensus-race-and-compatibility-verification/12-FULL-SUITE-REPORT.md`
- FOUND: commit `4ffc55df` (Task 1), `aca0d1ea` (Task 2)

---
*Phase: 12-consensus-race-and-compatibility-verification*
*Completed: 2026-08-19*
