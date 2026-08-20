---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "02"
subsystem: account lifecycle
tags: [cxx17, genius-node, account-generation, concurrency, caller-inventory]
requires: [manager-admission-gate]
provides: [generation-tagged-node-lifecycle, coherent-ready-publication, account-api-caller-manifest]
affects: [14-03, 14-06, 14-07, 14-08, 14-09, 14-10, 14-11, 14-12]
tech-stack:
  added: []
  patterns: [ready-retiring-pending bundles, generation-tagged event, checked lifecycle accessors, declaration-driven caller audit]
key-files:
  created: [.planning/phases/14-account-generation-publication-and-retired-manager-lifecycle/14-02-CALLER-INVENTORY.tsv, .planning/phases/14-account-generation-publication-and-retired-manager-lifecycle/14-node-api-caller-audit.py]
  modified: [src/account/GeniusNode.hpp, src/account/GeniusNode.cpp, test/src/multiaccount/multi_account_sync.cpp, test/src/account/account_management_test.cpp]
decisions:
  - "GeniusNode accepts a switch by allocating a generation, closes prior-manager admission, and keeps the generation unavailable until complete ready publication."
  - "Configured/bootstrap identity is stored separately from checked active-generation accessors; legacy accessor shims are explicitly temporary."
  - "The caller manifest is generated from public declarations and source expressions, then assigned to exactly one later migration partition."
metrics:
  duration: 14 min
  completed: 2026-08-20
  tasks_completed: 2
  files_modified: 6
---

# Phase 14 Plan 02: GeniusNode Lifecycle Core Summary

Generation-tagged account-switch acceptance, coherent ready publication, lifecycle-aware checked accessors, and a complete staged caller manifest for GeniusNode account APIs.

## Accomplishments

- Added account lifecycle, accepted-generation, ready/failure event, and processing-status value contracts.
- Closed the retiring manager admission boundary before accepted selection returns and delayed public account snapshots until ready publication.
- Added checked active address/processing accessors and a private configured identity accessor; legacy accessors remain marked for the later caller migrations.
- Added seven focused lifecycle regression names and strengthened snapshot presence checks.
- Generated a 191-row declaration/caller/target migration manifest and its deterministic audit tool.

## Verification

- `cmake --build build/OSX/Release --target multi_account_test account_management_test -j8` — PASS.
- Focused account acceptance/switching/configured-identity tests — PASS (3/3).
- Focused manager drain/stale-callback/processing-status tests — PASS (3/3).
- `MultiAccountTest.ConcurrentSelectAccountSnapshotsAndCatchupCallbacksStayGenerationConsistent` — PASS (29.2s, run with local socket permission).
- Caller audit generation and exact-owner/source-partition check — PASS.
- Structural lifecycle contract check (`CloseAdmission`, pending initialization, ready publication, configured identity) — PASS.

## Files Created/Modified

- `src/account/GeniusNode.hpp` — lifecycle contracts, owner bundles, checked accessors, and event declarations.
- `src/account/GeniusNode.cpp` — accepted-generation selection, pending-to-ready publication, generation/owner checks, and lifecycle-aware status mapping.
- `test/src/account/account_management_test.cpp` — acceptance/switching/configured-identity regressions.
- `test/src/multiaccount/multi_account_sync.cpp` — drain, stale callback, status, and coherent snapshot regressions.
- `14-02-CALLER-INVENTORY.tsv` — account-bound API declaration/caller/target ownership manifest.
- `14-node-api-caller-audit.py` — comment/string-aware manifest generator and ownership validator.

## Decisions Made

- A switch result is an `AccountSwitchAcceptance` with a monotonically allocated generation and target address; completion uses one generation-tagged event path.
- Only a complete account/manager/processing owner set may transition the node to published ready state.
- Later plans own caller migration; this plan records every discovered consumer without adapting those callers.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Ran network-backed snapshot verification outside the sandbox**
- **Found during:** Task 2 verification
- **Issue:** The sandbox denied the PubSub listener bind needed by the existing multi-account fixture.
- **Fix:** Re-ran the identical focused binary with local socket permission; it passed.
- **Files modified:** None
- **Verification:** `ConcurrentSelectAccountSnapshotsAndCatchupCallbacksStayGenerationConsistent` passed in 29.2 seconds.

**Total deviations:** 1 blocking environment adjustment. **Impact:** No production-code scope change.

## Known Stubs

- `src/account/GeniusNode.hpp` / `src/account/GeniusNode.cpp`: `PHASE14_TEMP_NODE_LEGACY_SHIM` preserves the old `GetProcessingStatus`/`GetAddress` shapes only until Plans 14-06 through 14-12 migrate every declared caller. The shims never publish pending or failed identity.

## Self-Check: PASSED

- All six plan output files exist.
- Task commits `9e2930ca` and `c1aabe5a` exist in git history.
