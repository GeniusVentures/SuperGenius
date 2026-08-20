---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "09"
subsystem: multi-account lifecycle caller migration
tags: [cxx17, genius-node, multi-account, lifecycle, caller-audit]
requires:
  - phase: 14-08
    provides: scoped checked-generation migration and partition-aware caller audit
provides:
  - multi-account callers guarded by checked active-generation results
  - source-scoped manifest and fresh-target verification for multi_account_test and policy_lifetime_multi_account_test
affects: [14-13, GeniusNode caller migration]
tech-stack:
  added: []
  patterns: [checked active-address fixture guard, source-scoped migration audit]
key-files:
  created: [14-09-SUMMARY.md]
  modified: [14-node-api-caller-audit.py, 14-02-CALLER-INVENTORY.tsv, multi_account_sync.cpp, policy_lifetime_multi_account_test.cpp]
key-decisions:
  - "Multi-account migration proof is constrained to its two owned sources so later partitions cannot satisfy or invalidate it."
  - "Concurrent lifecycle readers assert SWITCH_IN_PROGRESS or ACCOUNT_UNAVAILABLE instead of accepting legacy empty/default values."
requirements-completed: [D-03, D-04, D-11, D-16]
metrics:
  duration: 3h 6m
  completed: 2026-08-20
  tasks_completed: 2
  files_modified: 4
---

# Phase 14 Plan 09: Multi-Account Caller Migration Summary

**Multi-account fixtures now assert checked active-generation outcomes through switch, unavailable, and ready boundaries, with exact source/target audit proof.**

## Performance

- **Duration:** 3h 6m
- **Started:** 2026-08-20T15:53:51Z
- **Completed:** 2026-08-20T18:59:38Z
- **Tasks:** 2/2
- **Files modified:** 4

## Accomplishments

- Locked all multi-account caller rows to Plan 14-09 and both actual CMake targets.
- Scoped 14-09 migration verification to its two source files, keeping later caller partitions independent.
- Added checked active-address and lifecycle-result assertions for concurrent readers, stale-callback observation, processing status, and policy account-switch assertions.
- Marked all scoped manifest rows migrated and freshly built both multi-account targets.

## Verification

- `14-node-api-caller-audit.py --check-current` — PASS.
- `14-node-api-caller-audit.py --check-owner 14-09 ...` — PASS.
- `14-node-api-caller-audit.py --check-migrated --owner 14-09` — PASS.
- `cmake --build build/OSX/Release --target multi_account_test policy_lifetime_multi_account_test -j8` — PASS.
- Exact five-case `multi_account_test` lifecycle filter — PASS with local listener permission. The sandbox-only run was blocked before assertions by its local socket policy.

## Task Commits

1. **Task 1: Lock the multi-account node API caller partition** — `7ce6e166` (chore)
2. **Task 2: Migrate multi-account callers and preserve lifecycle test intent** — `da78c5b2` (test)

## Files Created/Modified

- `test/src/multiaccount/multi_account_sync.cpp` — checked active-generation helpers and deterministic lifecycle assertions.
- `test/src/multiaccount/policy_lifetime_multi_account_test.cpp` — checked active address assertions across policy-account switches.
- `14-02-CALLER-INVENTORY.tsv` — complete migrated 14-09 caller rows.
- `14-node-api-caller-audit.py` — source-scoped 14-09 migrated-audit partition.

## Decisions Made

- Preserve temporary node compatibility shims; the fixture callers explicitly test checked active-generation outcomes instead of treating shim defaults as readiness.
- Keep pre-ready fixture identity separate from active-ready access; no bridge, policy/trust ownership, global authority, cancellation, or shim-removal scope was expanded.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Audit correctness] Scoped Plan 14-09 migrated verification to its owned sources.**
- **Found during:** Task 1
- **Issue:** The generic owner check did not explicitly constrain 14-09 completion to its two-source partition.
- **Fix:** Added `PLAN_14_09_SOURCES` and applied it to the migrated audit path.
- **Files modified:** `14-node-api-caller-audit.py`
- **Verification:** Current and owner audit commands passed.
- **Committed in:** `7ce6e166`

**Total deviations:** 1 auto-fixed (Rule 1). **Impact:** Improves caller-migration evidence without changing production ownership or lifecycle behavior.

## Known Stubs

None.

## Issues Encountered

- The sandbox blocks the fixture's local PubSub listener before assertions. The identical focused test filter passed when granted local listener permission, matching the established Phase 14 test environment behavior.

## User Setup Required

None.

## Next Phase Readiness

- The multi-account partition is migrated and independently target-verified.
- Phase 14-08's `migration_sync_test` runtime `Network initialization error` remains a separate deferred environment issue and does not affect this plan's focused lifecycle proof.

## Self-Check: PASSED

- Found all four modified plan files and `14-09-SUMMARY.md`.
- Found task commits `7ce6e166` and `da78c5b2` in branch history.
