---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "11"
subsystem: bridge-race lifecycle caller migration
tags: [cxx17, genius-node, lifecycle, bridge-race, caller-audit]
requires:
  - phase: 14-10
    provides: checked bridge E2E caller patterns and a partitioned node API caller audit
provides:
  - five-target bridge-race shared-fixture ownership coverage
  - checked active-generation guards for bridge-race addresses and balances
  - fixture-owned configured bootstrap identities before node readiness
affects: [14-12, 14-13, GeniusNode compatibility-shim removal]
tech-stack:
  added: []
  patterns: [shared-header target expansion, checked active balance guard, configured fixture bootstrap identity]
key-files:
  created: [14-11-SUMMARY.md]
  modified: [bridge_race_fixture.hpp, bridge_race_single_burn_test.cpp, bridge_race_batch_test.cpp, bridge_race_fault_rpc_test.cpp, bridge_race_fault_kill_test.cpp, bridge_race_fault_partition_test.cpp, 14-02-CALLER-INVENTORY.tsv, 14-node-api-caller-audit.py]
key-decisions:
  - "The shared bridge-race fixture is audited against all five consuming CMake targets rather than a generic header consumer."
  - "Pre-ready bridge-race genesis authorization derives from a fixture-owned configured account identity; post-ready address and balance reads require an active generation."
patterns-established:
  - "A shared fixture's lifecycle API rows enumerate every concrete test target that includes it."
  - "Race release loops check readiness without adding waits or changing their back-to-back endpoint configuration semantics."
requirements-completed: [D-03, D-04, D-11, D-16]
metrics:
  duration: 9 min
  completed: 2026-08-20
---

# Phase 14 Plan 11: Bridge-Race Caller Migration Summary

**All five bridge-race targets now gate lifecycle-sensitive reads on a checked active generation while retaining their exact race and fault-injection flow.**

## Performance

- **Duration:** 9 min
- **Started:** 2026-08-20T19:12:27Z
- **Completed:** 2026-08-20T19:21:40Z
- **Tasks:** 2/2
- **Files modified:** 8

## Accomplishments

- Expanded the shared `bridge_race_fixture.hpp` call row from `header-consumer` to all five concrete bridge-race CMake targets.
- Moved race address and balance use behind checked active-generation helpers, and kept the fault-RPC manager result explicitly checked.
- Replaced pre-ready node-address reads used for genesis authorization with independently constructed fixture-owned configured identities.
- Preserved the no-wait, back-to-back RPC endpoint release loops, every race/fault target, CMake ownership, and the temporary node legacy shims.

## Task Commits

1. **Task 1: Lock bridge-race translation-unit and shared-fixture owner rows** — `58407022` (chore)
2. **Task 2: Migrate bridge-race callers without changing ownership or fault behavior** — `347c8683` (test)

## Verification

- `14-node-api-caller-audit.py --check-current` — PASS.
- `14-node-api-caller-audit.py --check-owner 14-11` for the five sources plus the shared fixture and five required targets — PASS.
- `14-node-api-caller-audit.py --check-migrated --owner 14-11` — PASS.
- Fresh CMake build of `bridge_race_single_burn_test`, `bridge_race_batch_test`, `bridge_race_fault_rpc_test`, `bridge_race_fault_kill_test`, and `bridge_race_fault_partition_test` — PASS.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 1 - Audit classification] Classified the fixture-owned configured identity as bootstrap-only.**
   - **Found during:** Task 2
   - **Issue:** The declaration-based audit sees `GeniusAccount::GetAddress()` in the shared fixture but cannot infer its receiver type; it would have labelled the configured pre-ready identity as an active-ready node address.
   - **Fix:** Added a bridge-fixture-specific classification so that row remains `configured-bootstrap`, while the checked `GeniusNode::GetActiveAccountAddress()` row remains `active-ready`.
   - **Files modified:** `14-node-api-caller-audit.py`, `14-02-CALLER-INVENTORY.tsv`
   - **Verification:** Current, owner, and migrated audits passed.
   - **Commit:** `347c8683`

**Total deviations:** 1 auto-fixed (Rule 1). **Impact:** The audit now proves the intended bootstrap-versus-active lifecycle boundary without changing bridge behavior.

## Known Stubs

None. The fixture's `__CREATION_BLOCK__` token is an implemented per-run configuration substitution, and the audit's `header-consumer` wording describes the retired generic mapping rather than a runtime placeholder.

## Self-Check: PASSED

- Required bridge-race fixture, five target sources, caller inventory, audit script, and Summary exist.
- Task commits `58407022` and `347c8683` resolve to commits in git history.
