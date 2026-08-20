---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "04"
subsystem: transaction-manager regression fixtures
tags: [cxx17, transaction-manager, lifecycle, regression-tests, cmake]
requires:
  - phase: 14-01
    provides: manager admission, retirement diagnostics, and test-only lifecycle capability
  - phase: 14-03
    provides: checked production manager caller migration and caller-audit ownership rules
provides:
  - current-source manager caller audit for the account, pending, consensus, and bridge-fault fixture partition
  - migrated fixture inventory rows with four concrete target mappings
  - active-lifecycle assertion before bridge mock-validator fault injection
affects: [14-05, manager-caller-inventory, regression-fixtures]
tech-stack:
  added: []
  patterns: [owner-scoped caller discovery, target-resolved fixture audit, active-manager test injection]
key-files:
  created: [14-04-SUMMARY.md]
  modified: [14-manager-caller-audit.py, 14-manager-caller-inventory.tsv, test/src/bridge_race/bridge_race_fault_rpc_test.cpp]
key-decisions:
  - "The bridge fault fixture verifies a checked manager result is ACTIVE before using its test-only validator factory."
  - "The caller audit distinguishes current direct manager calls from source targets that intentionally contain no direct manager expression."
patterns-established:
  - "Fixture partitions compare their owned rows against fresh discovery and resolve every required target from compile_commands.json."
requirements-completed: [D-06, D-13, D-14, D-15]
metrics:
  duration: 8 min
  completed: 2026-08-20
  tasks_completed: 2
  files_modified: 3
---

# Phase 14 Plan 04: Manager regression fixture migration Summary

**The account, pending-lifecycle, consensus, and bridge-fault fixture partition now has fresh source/target coverage, migrated manager rows, and an active-lifecycle guard for mock validator injection.**

## Performance

- **Duration:** 8 min
- **Started:** 2026-08-20T14:32:05Z
- **Completed:** 2026-08-20T14:40:05Z
- **Tasks:** 2/2
- **Files modified:** 3

## Accomplishments

- Extended the owner-scoped audit to record the bridge fixture's checked manager acquisition and validator access, compare 14-04 rows to current discovery, and resolve all four requested build targets.
- Marked every 14-04 manager fixture row migrated after reconciling exact source lines and target ownership.
- Required the bridge fault fixture's acquired manager to be `ACTIVE` before its test-only mock-validator injection, preserving the existing bridge race and fault behavior.
- Freshly built `burnconfig_policy_e2e_test`, `transaction_manager_pending_lifecycle_test`, `consensus_subject_test`, and `bridge_race_fault_rpc_test`.

## Task Commits

1. **Task 1: Lock the account/consensus/bridge fixture caller partition** — `0526aa72` (chore)
2. **Task 2: Migrate account, pending, consensus, and bridge-fault fixtures** — `28f78416` (test)

## Verification

- `14-manager-caller-audit.py --check-current ...` — PASS.
- `14-manager-caller-audit.py --check-owner 14-04 ... --required-targets burnconfig_policy_e2e_test,transaction_manager_pending_lifecycle_test,consensus_subject_test,bridge_race_fault_rpc_test` — PASS.
- `14-manager-caller-audit.py --check-migrated --owner 14-04 ...` — PASS.
- `cmake --build build/OSX/Release --target burnconfig_policy_e2e_test transaction_manager_pending_lifecycle_test consensus_subject_test bridge_race_fault_rpc_test -j8` — PASS.

## Files Created/Modified

- `14-manager-caller-audit.py` — validates fresh owned rows and resolves required fixture targets even where a fixture deliberately has no direct manager expression.
- `14-manager-caller-inventory.tsv` — records bridge checked-manager/validator access and marks the full 14-04 partition migrated.
- `test/src/bridge_race/bridge_race_fault_rpc_test.cpp` — asserts the checked manager is active before test-only validator fault injection.

## Decisions Made

- Preserve test-only bridge fault injection, but gate its mutable validator setup on the checked manager result and `ACTIVE` lifecycle.
- Treat the consensus fixture as target coverage with no direct manager expression rather than inventing a compatibility call solely for the manifest.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Audit coverage bug] Recorded and verified bridge manager-access expressions.**
- **Found during:** Task 1
- **Issue:** The caller audit allowed the bridge fixture but did not discover its checked manager acquisition or public-chain validator access, so the required target could not be represented by the manifest.
- **Fix:** Added bridge-only manager-access discovery and current owner-row comparison; required targets are resolved directly from `compile_commands.json` so fixtures without direct manager expressions remain covered.
- **Files modified:** `14-manager-caller-audit.py`, `14-manager-caller-inventory.tsv`
- **Verification:** Current/owner audit checks passed.
- **Committed in:** `0526aa72`

**2. [Rule 3 - Verification invocation] Supplied the audit's required compile-command input.**
- **Found during:** Task 1 verification
- **Issue:** The plan's owner-check invocation omitted the script's mandatory `--compile-commands` argument.
- **Fix:** Ran the declared owner check with the configured `build/OSX/Release/compile_commands.json` input.
- **Files modified:** None
- **Verification:** Owner check passed after the required input was supplied.
- **Committed in:** `0526aa72`

---

**Total deviations:** 2 auto-fixed (1 audit coverage bug, 1 blocking verification invocation).
**Impact on plan:** Both fixes are confined to the declared fixture inventory/audit partition; no legacy compatibility, bridge ownership, trust, or global authority surface changed.

## Known Stubs

None. The modified implementation and audit files contain no placeholder or unwired data path.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Plan 14-05 can now migrate the remaining multi-account fixture partition using the same current-source and target-resolved audit contract.

## Self-Check: PASSED

- Required audit, inventory, fixture, and summary files exist.
- Task commits `0526aa72` and `28f78416` exist in git history.
- No task commit deleted tracked files.

---
*Phase: 14-account-generation-publication-and-retired-manager-lifecycle*
*Completed: 2026-08-20*
