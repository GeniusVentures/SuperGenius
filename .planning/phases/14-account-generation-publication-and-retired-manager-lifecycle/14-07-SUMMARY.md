---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "07"
subsystem: account and blockchain lifecycle fixtures
tags: [cxx17, genius-node, lifecycle, fixture-audit, cmake]
requires:
  - phase: 14-06
    provides: checked ready-generation access and private configured bootstrap identity
provides:
  - target-resolved account/blockchain fixture caller partition
  - fixture-owned configured identity separated from checked active address access
  - fresh five-target fixture build evidence
affects: [14-08, 14-13, GeniusNode caller migration]
tech-stack:
  added: []
  patterns: [fixture-configured-bootstrap identity, checked active fixture access, owner-scoped caller audit]
key-files:
  created: [14-07-SUMMARY.md]
  modified: [test/src/account/account_management_test.cpp, test/src/account/network_config_precedence_test.cpp, test/src/account/node_type_derivation_test.cpp, test/src/blockchain/node_startup_test.cpp, test/src/blockchain/blockchain_genesis_test.cpp, 14-02-CALLER-INVENTORY.tsv, 14-node-api-caller-audit.py]
key-decisions:
  - "Fixture authorization before readiness derives identity from fixture-owned account material, while post-ready assertions use GetActiveAccountAddress."
  - "Plan 14-07 migration checks are limited to its five owned fixture sources, preserving later caller partitions for their assigned plans."
patterns-established:
  - "Fixtures make bootstrap identity explicit and require a checked active identity before consuming active-generation state."
requirements-completed: [D-01, D-02, D-03, D-04, D-11, D-16]
metrics:
  duration: 10 min
  completed: 2026-08-20T15:36:09Z
  tasks_completed: 2
  files_modified: 7
---

# Phase 14 Plan 07: Account and Blockchain Fixture Migration Summary

**Five account/blockchain fixtures now keep configured bootstrap identity separate from checked active generation access, with a target-resolved migration audit.**

## Performance

- **Duration:** 10 min
- **Started:** 2026-08-20T15:26:09Z
- **Completed:** 2026-08-20T15:36:09Z
- **Tasks:** 2/2
- **Files modified:** 7

## Accomplishments

- Locked the five account/blockchain fixture targets in the caller inventory and made the audit distinguish configured bootstrap setup from checked active access.
- Replaced pre-ready node-address reads with fixture-owned configured identities and made post-ready address assertions pass through `GetActiveAccountAddress`.
- Rebuilt `account_management_test`, `network_config_precedence_test`, `node_type_derivation_test`, `node_startup_test`, and `blockchain_genesis_test`; the prescribed lifecycle filter passed 3/3.

## Task Commits

1. **Task 1: Lock account and blockchain fixture ownership** — `0948e955` (chore)
2. **Task 2: Migrate account and blockchain fixtures without weakening lifecycle assertions** — `eb0b3a40` (test)

## Verification

- `14-node-api-caller-audit.py --check-current` — PASS.
- `14-node-api-caller-audit.py --check-owner 14-07 ...` for the five exact sources/targets — PASS.
- `14-node-api-caller-audit.py --check-migrated --owner 14-07` — PASS.
- Fresh CMake build of all five owner targets — PASS.
- Exact AccountManagement lifecycle filter — PASS (3/3).

## Files Created/Modified

- `14-node-api-caller-audit.py` — recognizes checked active aliases, preserves manifest disposition, records fixture bootstrap evidence, and scopes Plan 14-07 migration checks to its five fixtures.
- `14-02-CALLER-INVENTORY.tsv` — records classified, target-resolved configured-bootstrap and active-generation rows as migrated.
- Account/blockchain fixture sources — use fixture-owned bootstrap identity before readiness and checked active identity after readiness.

## Decisions Made

- Use independently constructed fixture account identity for pre-ready authorization rather than exposing configured identity through the node.
- Retain node compatibility shims for Plan 14-13; this partition uses checked alternatives without changing node lifecycle, bridge, trust, or timeout ownership.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Audit correctness] Distinguished fixture bootstrap and checked active access.**
- **Found during:** Task 1
- **Issue:** The audit collapsed configured-bootstrap rows into `active-generation` and could not identify callers already using checked active aliases.
- **Fix:** Corrected identity classification, retained concrete checked-alias expressions, and added explicit fixture bootstrap evidence.
- **Files modified:** `14-node-api-caller-audit.py`, `14-02-CALLER-INVENTORY.tsv`
- **Verification:** Current and owner audit commands passed.
- **Committed in:** `0948e955`, `eb0b3a40`

**2. [Rule 1 - Fixture build] Passed the generated private key as the required C-string.**
- **Found during:** Task 2 verification
- **Issue:** The fixture-owned identity helper passed `std::string` to the existing `NewFromPrivateKey` C-string parameter.
- **Fix:** Passed `key.c_str()`.
- **Files modified:** `test/src/blockchain/blockchain_genesis_test.cpp`
- **Verification:** The fresh five-target build passed.
- **Committed in:** `eb0b3a40`

**Total deviations:** 2 auto-fixed (2 Rule 1). **Impact:** Both fixes preserve the declared fixture partition and exact lifecycle contract without widening later caller, bridge, trust, cancellation, or global capability scope.

## Known Stubs

None. The scan found only intentional nullable test parameters and existing test assertions; no placeholder data path was introduced.

## Issues Encountered

The initial five-target build exposed the existing C-string signature expected by the fixture identity factory; it was corrected and the prescribed complete build was rerun successfully.

## User Setup Required

None.

## Next Phase Readiness

The account/blockchain fixture partition is migrated with the temporary node compatibility shims still intact for Plans 14-08 through 14-13.

## Self-Check: PASSED

- Found all five fixture sources and both caller audit artifacts.
- Found task commits `0948e955` and `eb0b3a40` in the phase branch history.
