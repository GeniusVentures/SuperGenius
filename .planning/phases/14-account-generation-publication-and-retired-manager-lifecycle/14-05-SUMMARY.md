---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "05"
subsystem: transaction-manager lifecycle
tags: [cxx17, transaction-manager, lifecycle, public-api, regression-tests]
requires:
  - phase: 14-01
    provides: admission, drain, and retirement diagnostics
  - phase: 14-03
    provides: production manager caller migration
  - phase: 14-04
    provides: manager fixture caller inventory
provides:
  - final manager compatibility-seam removal
  - exact declaration and caller-target closure audits
  - friend-scoped fixture and bridge validator capabilities
affects: [14-06, 14-07, manager-caller-inventory]
tech-stack:
  added: []
  patterns: [final public-surface inventory, friend-scoped fixture capability, all-target caller build]
key-files:
  created: [14-05-SUMMARY.md]
  modified: [src/account/TransactionManager.hpp, src/account/TransactionManager.cpp, 14-01-PUBLIC-SURFACE.tsv, 14-manager-caller-inventory.tsv, 14-public-surface-audit.py, 14-manager-caller-audit.py]
key-decisions:
  - "The former enqueue compatibility shim is replaced only by a clearly named friend-only fixture capability."
  - "Manager setup/control and mutable public-chain validator access are no longer public; named owner and test friend capabilities retain the required access."
requirements-completed: [D-05, D-06, D-13, D-14, D-15]
metrics:
  duration: 22 min
  completed: 2026-08-20
  tasks_completed: 2
  files_modified: 9
---

# Phase 14 Plan 05: Final Manager Surface Closure Summary

The TransactionManager compatibility seam is removed, retired-safe lifecycle diagnostics are explicit, and every mapped manager caller target builds against the final surface.

## Accomplishments

- Migrated the final multi-account partition to explicit friend-only lifecycle test capabilities and marked all caller rows migrated.
- Removed both `EnqueueTransaction` compatibility overloads and replaced the multi-item fixture path with a protected `EnqueueForTest` capability.
- Added immutable `GetGeneration` diagnostics, moved lifecycle controls and mutable validator access behind precise friends, and validated the final public declaration inventory.
- Built the complete manifest target union: `bridge_race_fault_rpc_test`, `burnconfig_policy_e2e_test`, `genius_node`, `genius_node_test`, `migration`, `multi_account_test`, `policy_lifetime_multi_account_test`, and `transaction_manager_pending_lifecycle_test`.

## Task Commits

1. **Task 1: Migrate final multi-account manager callers** — `81f3f473` (test)
2. **Task 2: Remove manager shims, enforce the final surface, and build the caller-target union** — `da79a81d` (feat)

## Verification

- `14-manager-caller-audit.py --check-current` and `--check-all-migrated` — PASS.
- `14-public-surface-audit.py --check-final` and legacy-marker scan — PASS.
- Fresh build of every manifest target — PASS.
- Focused `multi_account_test` lifecycle suite — PASS (4/4); run with local socket permission required by the existing PubSub fixture.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 1 - Audit coverage] Audited all manager owner partitions, not only production rows.**
- **Found during:** Task 1
- **Issue:** `--check-current` compared only the 14-03 production partition, so fixture drift could be missed.
- **Fix:** The audit now compares current caller coverage for every owner partition while retaining source locations as inventory breadcrumbs.
- **Files modified:** `14-manager-caller-audit.py`
- **Commit:** `81f3f473`

2. **[Rule 2 - Missing critical closure] Replaced an untracked legacy fixture consumer with a named friend capability.**
- **Found during:** Task 2
- **Issue:** The pending-lifecycle fixture still invoked the temporary enqueue shim despite its manifest row being marked migrated.
- **Fix:** Removed both legacy overloads, added `EnqueueForTest` behind the existing friend, and adapted dependent fixture/bridge access through explicit named test capabilities.
- **Files modified:** `TransactionManager.hpp`, `TransactionManager.cpp`, pending-lifecycle, burn-config, and bridge-fault fixtures
- **Commit:** `da79a81d`

**Total deviations:** 2 auto-fixed. **Impact:** The final API closure now covers all manifest partitions without changing GeniusNode caller migration, bridge ownership, trust behavior, or global capability design.

## Known Stubs

None. Matches found in the changed manager source are pre-existing defaults and TODO comments outside this final-surface path.

## Self-Check: PASSED

- Required final-surface inventory, caller inventory, audit scripts, and manager implementation files exist.
- Task commits `81f3f473` and `da79a81d` exist in git history.
