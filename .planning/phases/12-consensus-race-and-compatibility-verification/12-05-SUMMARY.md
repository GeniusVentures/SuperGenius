---
phase: 12-consensus-race-and-compatibility-verification
plan: 05
subsystem: testing
tags: [ctest, googletest, bridge-race, consensus, verification, blocker]

requires:
  - phase: 12-consensus-race-and-compatibility-verification
    provides: Focused consensus/finality/vote-journal/burn-reservation/compatibility regressions and the mandatory 11-node race target (plans 01-04)
provides:
  - Fresh Phase 12 verification evidence at af382b03 (focused gate green, isolated race red)
  - Deterministic mint-destination blocker record with reproduction identity
  - Re-captured dynamic CTest inventory (98 entries) with missing-executable caveat
affects: [phase-12-closure, milestone-v2.0, follow-up-debugging]

tech-stack:
  added: []
  patterns:
    - "Stop-gate reporting: mandatory isolated race failure forbids full-suite characterization"

key-files:
  created: []
  modified:
    - .planning/phases/12-consensus-race-and-compatibility-verification/12-FULL-SUITE-REPORT.md

key-decisions:
  - "Honored the plan stop gate: after the isolated race failed deterministically twice, the unfiltered full suite was not executed and no passing characterization was made"
  - "No source or test code was modified under this reporting task; the mint-destination mismatch is recorded as a blocker for a scoped follow-up"

requirements-completed: []  # None — TEST-01..06 remain BLOCKED; closure criteria not met

duration: 37 min
completed: 2026-08-19
---

# Phase 12 Plan 05: Full-Suite Verification Re-Run Summary

**Re-ran Phase 12 verification at af382b03: focused gate green 7/7, but the mandatory 11-node race fails deterministically with a consensus-wide mint-destination mismatch — plan closure NOT achieved.**

## Performance

- **Duration:** 37 min
- **Started:** 2026-08-19T14:27:02Z
- **Completed:** 2026-08-19T15:04:10Z
- **Tasks:** 1 of 2 executed (Task 2 gated off by the plan's stop rule); report regenerated
- **Files modified:** 1 (`12-FULL-SUITE-REPORT.md`)

## Accomplishments

- Fresh focused Phase 12 gate: 7/7 pass in 94.43s at `af382b03` (finalization, finality race, vote journal, burn reservation, certificate store, compatibility, TransactionManager lifecycle)
- Proved the isolated `bridge_race_single_burn_test` failure is **deterministic**, not a flake: two consecutive isolated `-j1` runs failed identically
- Precisely characterized the failure: consensus converges correctly (one slot, 11 proposals, one vote per validator, one authority, winner-only confirmation, clean teardown), but the winning mint's output destination is node 1's **validator identity** instead of its SGNS address (`dest_match=0`, `owner_match=0`, zero destination balance) on all 11 nodes
- Re-captured the dynamic inventory: 98 configured tests (was 84), with 4 configured entries currently lacking built executables
- Honest BLOCKED disposition committed — no failure represented as success

## Task Commits

1. **Task 1: Run focused regressions and the isolated real race** — `98096d5f` (docs: report regenerated with fresh focused-gate pass and deterministic race-blocker evidence; covers Task 2 inventory capture and gated-off full suite)

**Plan metadata:** recorded below at close-out.

## Files Created/Modified

- `.planning/phases/12-consensus-race-and-compatibility-verification/12-FULL-SUITE-REPORT.md` — regenerated with fresh results, blocker disposition, requirement matrix, and follow-up blocker list

## Decisions Made

- Followed the plan's explicit stop gate: once the mandatory isolated race failed, the unfiltered full suite was not executed and the suite was not characterized. The prior unfiltered run (2026-07-31, 70/84 pass) remains the last full-suite data point.
- No source/test fixes attempted — out of scope for this reporting plan per its `files_modified` contract and the orchestrator's no-source-changes instruction.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Reconfigured stale CMake build tree**
- **Found during:** Task 1 (build step)
- **Issue:** `cmake --build build/OSX/Release --target consensus_finalization_test ...` failed with "No rule to make target" — generated Makefiles predated the rebase-era test registrations
- **Fix:** Ran `cmake -S build/OSX -B build/OSX/Release` (exit 0), then rebuilt; all 8 focused targets built successfully
- **Files modified:** build tree only (not versioned)
- **Verification:** Focused gate subsequently discovered and passed all 7 entries
- **Committed in:** `98096d5f` (noted in report reproduction identity)

---

**Total deviations:** 1 auto-fixed (blocking build configuration)
**Impact on plan:** Minimal — build-tree regeneration only; no scope change.

## Issues Encountered

- **`bridge_race_single_burn_test` deterministic failure (BLOCKER).** Two isolated runs at `af382b03` failed at `bridge_race_single_burn_test.cpp:201` (`application_converged`). All consensus invariants held; the mint application landed at the destination node's validator identity rather than its SGNS address on every node. The change range since the last passing run (`5fec4d6e..af382b03`) includes rebase-era certificate-lookup/mint fixes and the worktree `evmrelay` submodule sits at `62a9bbb1` while the superproject records `4787e582`. Root cause not established; no fix attempted under this plan. Recorded as follow-up blocker #1 in the report.
- **Full suite not run** — plan stop gate; D-16/D-17 unsatisfied this window.
- **4 configured tests without built executables** in `build/OSX/Release` (`bridge_event_identity_test`, `crdt_datastore_last_owner_test`, `public_chain_mint_validation_test`, `transaction_manager_certificate_fallback_test`); any future full run must build the complete inventory first to avoid `Not Run` entries.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 12 is **not** closable. TEST-01..06 remain BLOCKED.
- Required follow-up (scoped decision needed): (1) root-cause and fix the mint-destination mismatch in the bridge-race path, (2) decide and commit the correct `evmrelay` submodule pointer, (3) build the complete 98-test inventory, (4) re-run this plan's focused + isolated + unfiltered full-suite verification.

## Self-Check: FAILED

Must-have verification against the plan:

- [FAILED] D-14: the real 11-node race did **not** pass in isolated CTest execution (2 deterministic failures)
- [FAILED] D-16: no fresh unfiltered full-suite run exists (stop gate)
- [FAILED] D-17: prerequisite-backed full-inventory accounting incomplete (stop gate)
- [FAILED] Closure truth: zero-failure/zero-timeout/zero-crash state not demonstrated
- [PASSED] Artifact exists: `12-FULL-SUITE-REPORT.md` regenerated with reproducible identity and true disposition
- [PASSED] Commit exists: `98096d5f` (`git log --oneline | grep 98096d5f`)
- [PASSED] No failure represented as success anywhere in the report

This plan did **not** achieve closure. The SUMMARY is intentionally marked FAILED per the orchestrator's instruction for unmet must_haves.

---
*Phase: 12-consensus-race-and-compatibility-verification*
*Completed: 2026-08-19*
