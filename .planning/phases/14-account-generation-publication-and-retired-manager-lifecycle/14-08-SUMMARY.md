---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "08"
subsystem: processing and transaction-sync lifecycle callers
tags: [cxx17, genius-node, lifecycle, processing, transaction-sync, caller-audit]
requires:
  - phase: 14-07
    provides: checked active-generation fixture access and owner-scoped caller audit
provides:
  - checked active-ready access for seven processing and transaction-sync targets
  - partition-scoped migrated caller audit including migration_sync_test
affects: [14-09, 14-13, GeniusNode caller migration]
tech-stack:
  added: []
  patterns: [checked active address before legacy shim, configured fixture identity before readiness, target-resolved caller audit]
key-files:
  created: [14-08-SUMMARY.md, deferred-items.md]
  modified: [14-node-api-caller-audit.py, 14-02-CALLER-INVENTORY.tsv, processing_nodes_test.cpp, full_node_test.cpp, child_tokens_test.cpp, genius_node_bootstrap_reconnect_test.cpp, transaction_sync_test.cpp, transaction_crash_test.cpp, migration_sync_test.cpp]
key-decisions:
  - "Processing and transaction-sync fixtures prove active readiness before consuming temporary balance and wait shims."
  - "migration_sync_test keeps fixture-derived configured identity for pre-ready authorization and uses checked active balance only after readiness."
requirements-completed: [D-03, D-04, D-11, D-16]
metrics:
  duration: 13 min
  completed: 2026-08-20T15:51:02Z
  tasks_completed: 2
  files_modified: 10
---

# Phase 14 Plan 08: Processing and Transaction-Sync Caller Migration Summary

**Seven processing and transaction-sync targets now assert ready-generation access before consuming compatibility shims, while migration fixtures retain separate pre-ready configured identity.**

## Performance

- **Duration:** 13 min
- **Completed:** 2026-08-20T15:51:02Z
- **Tasks:** 2/2
- **Files modified:** 10

## Accomplishments

- Scoped the node API audit's migrated check to the seven exact Plan 14-08 sources, leaving later processing partitions untouched.
- Migrated address, balance, count, wait, mint, transfer, and direct transaction submission consumers to preserve explicit lifecycle failure rather than interpreting legacy zero, empty, or INVALID values.
- Kept `migration_sync_test` bootstrap authorization fixture-owned, and validated its balance only after ready-generation access succeeds.
- Added `migration_sync_test` to the owner manifest and built every mapped target freshly.

## Task Commits

1. **Task 1: Lock processing and transaction-sync caller rows and target mapping** — `3e538cf9` (chore)
2. **Task 2: Migrate processing and transaction-sync callers** — `060292d9` (test)

## Verification

- `14-node-api-caller-audit.py --check-current` — PASS.
- `14-node-api-caller-audit.py --check-owner 14-08 ...` for the seven exact sources and targets — PASS.
- `14-node-api-caller-audit.py --check-migrated --owner 14-08` — PASS.
- Fresh CMake build of `processing_nodes_test`, `full_node_test`, `child_tokens_test`, `genius_node_bootstrap_reconnect_test`, `transaction_sync_test`, `transaction_crash_test`, and `migration_sync_test` — PASS.
- Focused `migration_sync_test` filter — BLOCKED before assertions by pre-existing node `Network initialization error`; recorded in `deferred-items.md`.

## Decisions Made

- Keep temporary node shims in place, but guard every use in this caller partition with a checked active account result.
- Do not treat fixture bootstrap identity as an active generation; use it only to configure pre-ready full-node authorization.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Audit correctness] Scoped migrated verification to the seven Plan 14-08 sources.**
- **Found during:** Task 1
- **Issue:** `--check-migrated --owner 14-08` also evaluated later staged processing sources and could not prove this plan's partition independently.
- **Fix:** Added the exact seven-source set and filtered the Plan 14-08 migrated audit to that set.
- **Files modified:** `14-node-api-caller-audit.py`
- **Verification:** Current and owner audit commands passed.
- **Commit:** `3e538cf9`

**2. [Rule 1 - Fixture path type] Used Boost filesystem path for configured migration identity.**
- **Found during:** Task 2 build
- **Issue:** `GeniusAccount::NewFromPrivateKey` accepts `boost::filesystem::path`, not `std::filesystem::path`.
- **Fix:** Passed a Boost path for the isolated configured-identity fixture directory.
- **Files modified:** `migration_sync_test.cpp`
- **Verification:** Fresh `migration_sync_test` target build passed.
- **Commit:** `060292d9`

**Total deviations:** 2 auto-fixed (2 Rule 1). **Impact:** Both preserve the approved caller-only migration boundary.

## Known Stubs

None. The only pre-existing TODO concerns numeric configuration conversion and is unrelated to lifecycle caller migration.

## Deferred Issues

- The focused migration runtime filter cannot reach assertions in this environment because node initialization fails with `Network initialization error`; see `deferred-items.md`.

## Self-Check: PASSED

- Found all seven migrated source files, the owner audit, inventory, and deferred-item record.
- Found task commits `3e538cf9` and `060292d9` in branch history.
