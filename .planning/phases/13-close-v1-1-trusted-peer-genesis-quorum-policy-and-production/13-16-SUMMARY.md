---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 16
subsystem: account economics
tags: [cpp, uint64, overflow-safety, burn-config, pay-escrow, gtest]

requires:
  - phase: 13-08
    provides: node-scoped confirmed burn provider wired into every TransactionManager
provides:
  - exact floor(amount*basis_points/10000) PayEscrow arithmetic over the complete uint64_t domain
  - pre-fetch rejection of basis-point values above 10000
  - live UINT64_MAX economic-output regressions at 1, 100, and 10000 basis points
affects: [13-18, BurnConfig, TransactionManager, PayEscrow]

tech-stack:
  added: []
  patterns: [quotient-remainder percentage arithmetic, validate-before-fetch financial input gate]

key-files:
  created: []
  modified:
    - src/account/TransactionManager.cpp
    - test/src/account/burnconfig_policy_e2e_test.cpp

key-decisions:
  - "PayEscrow computes percentages with quotient/remainder decomposition after validating basis points, avoiding compiler-specific wide integers while preserving exact mathematical floor semantics."

patterns-established:
  - "Full-domain percentage: amount/total*rate + (amount%total*rate)/total after rate <= total validation."
  - "Invalid financial policy inputs return before reading escrow state or constructing payout outputs."

requirements-completed: [BURN-02, BURN-03, TEST-01]

duration: 6min
completed: 2026-08-13
---

# Phase 13 Plan 16: Exact Full-Domain Escrow Burn Arithmetic Summary

**PayEscrow now calculates exact basis-point burns for every uint64_t escrow amount and rejects invalid policy values before escrow lookup or payout persistence.**

## Performance

- **Duration:** 6 min
- **Started:** 2026-08-13T14:11:59Z
- **Completed:** 2026-08-13T14:18:02Z
- **Tasks:** 1 TDD task
- **Files modified:** 2

## Accomplishments

- Replaced overflow-prone multiplication with exact quotient/remainder arithmetic whose intermediates remain bounded throughout the complete valid input domain.
- Added basis-point range validation immediately after reading the confirmed or fallback value and before task validation, escrow fetch, CRDT topic mutation, or transaction construction.
- Proved live PayEscrow zero-address outputs for `UINT64_MAX`: `1,844,674,407,370,955` at 1 bp, `184,467,440,737,095,516` at 100 bp, and `UINT64_MAX` at 10,000 bp, with zero distributed outside the burn at 100%.
- Proved 10,001 bp returns `invalid_argument` while tracked transaction, outgoing transaction, and output-destination counts remain unchanged.

## Task Commits

1. **Task 1 RED: Maximum escrow arithmetic counterexample** - `d5810132` (test)
2. **Task 1 GREEN: Exact overflow-safe escrow burn arithmetic** - `feed11e3` (fix)

## Files Created/Modified

- `src/account/TransactionManager.cpp` - Validates the live/fallback basis-point range and computes exact full-domain burn output with quotient/remainder decomposition.
- `test/src/account/burnconfig_policy_e2e_test.cpp` - Exercises confirmed burn successors and exact real PayEscrow outputs at the uint64_t maximum, plus invalid-input no-persistence behavior.

## Decisions Made

- Used standard `uint64_t` quotient/remainder arithmetic instead of `unsigned __int128`: after validating `basis_points <= 10000`, both products and their sum are bounded, portable, and mathematically equal to `floor(amount*basis_points/10000)`.
- Placed validation before even ordinary task-result checks so malformed policy state cannot trigger escrow reads or any payout-side mutation.

## TDD Gate Compliance

- **RED (`d5810132`):** the test failed with the production counterexamples: 100 bp returned the 1 bp result, 10,000 bp burned only `1,844,674,407,370,954` and leaked value, and 10,001 bp enqueued a payout.
- **GREEN (`feed11e3`):** the focused maximum-domain and persist-before-cache cases passed after the production-only arithmetic fix.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The network-backed fixture could not bind its ephemeral local listener inside the restricted sandbox. The identical RED, focused GREEN, and complete-suite commands were rerun with local-listener permission; failures and passes then reflected the intended product assertions.

## Verification

- `cmake --build build/OSX/Release --target burnconfig_policy_e2e_test -j8` - PASS.
- Focused plan gate with `*PayEscrowUsesExactOverflowSafeBurnForUint64Maximum*:*PersistBeforeCache*` - PASS, 2/2 tests.
- Complete `burnconfig_policy_e2e_test --gtest_brief=1` - PASS, 6/6 tests.
- Static scan for `escrow_amount * burn_basis_points` - PASS, no match.
- `git diff --check` - PASS.

## Known Stubs

None. The changed implementation and test paths contain no new placeholder, TODO, empty-data, or mock-runtime behavior.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- CR-03 is closed with executable full-domain financial-output evidence.
- Plans 13-14, 13-15, 13-17, and 13-18 remain independently incomplete; this plan does not mark or advance them.

## Self-Check: PASSED

- Both modified implementation/test files and this summary exist.
- RED `d5810132` and GREEN `feed11e3` exist in repository history in the required order.
- Focused and complete-suite verification passed, and no tracked deletion or plan-created untracked artifact remains.
- Threat-surface scan found only the planned confirmed-policy-to-financial-output boundary; no new endpoint, authentication path, file-access pattern, schema, package, or network surface was introduced.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-13*
