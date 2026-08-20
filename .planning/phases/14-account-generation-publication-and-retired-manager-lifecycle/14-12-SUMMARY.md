---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "12"
subsystem: account-lifecycle caller migration
tags: [cxx17, cmake, geniusnode, lifecycle, processing, caller-audit]
requires:
  - phase: 14-11
    provides: bridge-race caller migration and the preceding ownership partitions
provides:
  - NodeExample and processing_multi callers migrated to checked active-generation APIs
  - processing_multi_test registered in the parent test CMake graph
  - final caller-union evidence for node_example and processing_multi_test
affects: [14-13-node-shim-removal, 14-15-phase-verification]
tech-stack:
  added: []
  patterns: [declaration-driven caller ownership, checked ready-generation result handling, temporary legacy-shim containment]
key-files:
  created: [14-12-SUMMARY.md]
  modified: [example/node_test/NodeExample.cpp, test/src/CMakeLists.txt, test/src/processing_multi/processing_multi_test.cpp, src/account/GeniusNode.hpp, src/account/GeniusNode.cpp, test/src/account/account_management_test.cpp, test/src/processing_nodes/child_tokens_test.cpp, test/src/processing_nodes/processing_nodes_test.cpp, 14-02-CALLER-INVENTORY.tsv, 14-node-api-caller-audit.py]
key-decisions:
  - "ProcessImage and escrow waits use checked ready-generation results; the raw escrow status API remains only the marked temporary Plan 14-13 shim."
  - "The existing processing_multi_test target is registered only through its parent CMake list, preserving its local target definition and fixture behavior."
requirements-completed: [D-03, D-04, D-11, D-16]
duration: 10min
completed: 2026-08-20
---

# Phase 14 Plan 12: Final Node Caller Targets Summary

**Checked lifecycle-bound NodeExample and processing-multi callers, parent CMake registration, and final owner-union evidence before node-shim removal.**

## Performance

- **Duration:** 10 min
- **Started:** 2026-08-20T19:55:50Z
- **Completed:** 2026-08-20T20:06:07Z
- **Tasks:** 2/2
- **Files modified:** 10 implementation/inventory files

## Accomplishments

- Assigned each omitted NodeExample and processing-multi account-bound expression exclusively to Plan 14-12 and retained both source/target pairs in the all-migrated union.
- Registered the existing `processing_multi_test` target with `add_subdirectory(processing_multi)` and rebuilt it with `node_example`.
- Added a checked escrow-release result API so switching and unavailable lifecycle states cannot be read as raw `INVALID` success; production callers use checked `ProcessImage` and active escrow waits.

## Task Commits

1. **Task 1: Lock omitted example and processing-multi ownership** — `3ccdf71c` (chore), `58528a64` (fix)
2. **Task 2: Wire and migrate both caller contracts** — `5f7db6a1` (feat)
3. **Lifecycle completeness follow-up** — `a0b26ada` (fix), `2622af17` (chore)

## Verification

- `cmake -S build/OSX -B build/OSX/Release ... -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` and target-help presence for `processing_multi_test` — PASS.
- Fresh builds: `node_example`, `processing_multi_test`, `account_management_test`, and `processing_nodes_test` — PASS.
- `14-node-api-caller-audit.py --check-current`, `--check-owner 14-12`, `--check-assigned`, `--check-migrated --owner 14-12`, and `--check-all-migrated` — PASS.
- All-migrated union contains `example/node_test/NodeExample.cpp`, `test/src/processing_multi/processing_multi_test.cpp`, `node_example`, and `processing_multi_test` — PASS.
- Focused lifecycle test `AccountManagement.SwitchInProgressRejectsAccountCallsAndOverlap` — PASS with the local listener permission; it asserts both `SWITCH_IN_PROGRESS` and `ACCOUNT_UNAVAILABLE` from `WaitForActiveEscrowRelease`.
- Focused lifecycle tests `SelectAccountReturnsGenerationBeforeReadyEvent` and `ConfiguredIdentityDoesNotPublishUnavailableGeneration` — PASS (2/2).
- Source/inventory guard for temporary raw escrow shim and checked `ProcessImage` rows — PASS.
- `ProcessingMultiTest.ProcessOne` — BLOCKED: fixture reports `ACCOUNT_UNAVAILABLE` before readiness, skips the test, then exits with status 139. No caller change was made.
- `ProcessingNodesTest.PostProcessing` and the bounded `node_example --terminal` smoke run both reached local node initialization but did not emit a terminal test/application result in the harness; no source change was made.

## Files Created/Modified

- `example/node_test/NodeExample.cpp` — unwraps checked lifecycle-bound balances, submissions, status, and control operations.
- `test/src/processing_multi/processing_multi_test.cpp` — uses checked active-generation helpers for processing fixture calls.
- `test/src/CMakeLists.txt` — registers the existing `processing_multi` target directory.
- `src/account/GeniusNode.hpp` and `src/account/GeniusNode.cpp` — expose `WaitForActiveEscrowRelease`; raw `WaitForEscrowRelease` is the explicitly tagged temporary shim only.
- `14-02-CALLER-INVENTORY.tsv` and `14-node-api-caller-audit.py` — provide exact source/target ownership, checked-call classification, and final-union validation.

## Decisions Made

- Checked escrow release is required at lifecycle-sensitive callers; `INVALID` from the legacy raw-status shape cannot represent switching or unavailable state.
- `ProcessImage` is an `active-ready` contract in the manifest for both Plan 14-12 sources.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Restricted caller-audit discovery to GeniusNode receivers.**
- **Found during:** Task 1
- **Issue:** Receiver-agnostic matching could inventory unrelated methods with the same names.
- **Fix:** Tightened audit receiver handling and regenerated the exact ownership rows.
- **Files modified:** `14-node-api-caller-audit.py`, `14-02-CALLER-INVENTORY.tsv`
- **Committed in:** `58528a64`

**2. [Rule 2 - Missing critical functionality] Added checked escrow-release lifecycle propagation.**
- **Found during:** Task 2 completion audit
- **Issue:** Lifecycle-sensitive escrow callers needed to distinguish switching/unavailable from a raw sentinel status.
- **Fix:** Added `WaitForActiveEscrowRelease`, migrated relevant callers, and audited the raw API as a temporary compatibility shim.
- **Files modified:** `src/account/GeniusNode.hpp`, `src/account/GeniusNode.cpp`, lifecycle tests, inventory, and audit script.
- **Committed in:** `a0b26ada`, `2622af17`

## Issues Encountered

- The sandbox initially denied the fixtures' local PubSub listener. Re-running the same focused lifecycle check with local socket permission passed.
- Processing integration fixture readiness is currently unstable outside this plan's caller migration scope; details are recorded in `deferred-items.md`.

## Known Stubs

None in Plan 14-12 implementation. The raw `WaitForEscrowRelease` method is an intentional, documented temporary compatibility shim scheduled for removal by Plan 14-13, not a UI/data stub.

## Next Phase Readiness

- The complete migrated source/target union is ready for Plan 14-13's node-shim removal audit.
- Do not alter the processing fixture readiness behavior as part of this plan; investigate the recorded runtime blocker separately.

## Self-Check: PASSED

- Summary exists and task commits `3ccdf71c`, `58528a64`, `5f7db6a1`, `a0b26ada`, and `2622af17` all resolve in Git history.

---
*Phase: 14-account-generation-publication-and-retired-manager-lifecycle*
*Completed: 2026-08-20*
