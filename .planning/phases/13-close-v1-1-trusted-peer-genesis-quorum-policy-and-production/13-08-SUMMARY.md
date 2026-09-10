---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: "08"
subsystem: node-policy-lifetime
tags: [burn-policy, multi-account, pay-escrow, trust-genesis, regression]

requires:
  - phase: 13-07
    provides: trusted startup, durable genesis, and fail-closed node readiness
provides:
  - node-scoped trust-policy services that survive account selection
  - fail-closed PayEscrow consumption of a retained confirmed-burn provider
  - real economic-output and repeated account-lifetime regression evidence
affects: [13-11, phase-13-verification, transaction-economics, account-management]

tech-stack:
  added: []
  patterns:
    - recreate account-bound consumers while retaining GlobalDB-scoped policy owners
    - expose confirmed economic policy through a shared provider published only after durable activation

key-files:
  created:
    - test/src/multiaccount/policy_lifetime_multi_account_test.cpp
  modified:
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - test/src/account/burnconfig_policy_e2e_test.cpp
    - test/src/account/account_management_test.cpp
    - test/testutil/genius_node_test_access.hpp

key-decisions:
  - "SelectAccount destroys only account-bound services; SecureCrdt, TPR, BurnConfig, stores, registrations, and the confirmed provider remain node-scoped."
  - "PayEscrow returns TRUST_POLICY_NOT_READY before persisting economic state when no durable confirmed burn policy is available."
  - "Legacy account fixtures execute the reviewed trust and burn genesis ceremony rather than weakening production quorum validation."

patterns-established:
  - "Replacement TransactionManagers receive the same retained confirmed-burn provider after every account switch."
  - "Economic integration tests assert exact PayEscrow burn output, not only policy getters."

requirements-completed: [BURN-01, BURN-02, BURN-03, TPR-01, TPR-02, SCRDT-04, TEST-01]

duration: 1h10m
completed: 2026-08-12
---

# Phase 13 Plan 08: Node Policy Lifetime and Live Burn Economics Summary

**Account selection now preserves node-owned trust policy, and real PayEscrow output changes only after a current-policy quorum reaches durable BurnConfig activation.**

## Performance

- **Duration:** 1h10m
- **Started:** 2026-08-12T17:25:17Z
- **Completed:** 2026-08-12T18:34:00Z
- **Tasks:** 2
- **Files modified:** 10

## Accomplishments

- Split account-bound teardown from node-policy lifetime so repeated `SelectAccount` calls replace the account and TransactionManager while preserving SecureCrdt, its registry, TrustStateStore, TrustedPeerRegistry, BurnConfig, confirmed-provider identity, and callback/filter registrations.
- Injected the retained confirmed-burn provider into every replacement TransactionManager and made PayEscrow fail closed with typed `TRUST_POLICY_NOT_READY` before economic state is constructed or persisted.
- Extended BurnConfig coverage through real PayEscrow calls: durable current-policy activation changes exact burn output, while under-signed, stale, outsider, wrong-predecessor, and failed-store candidates leave durable state and output unchanged.
- Added repeated multi-account coverage proving retained object identity, registration stability, live post-switch updates, exact economic output, and clean final teardown.
- Updated legacy account-management fixtures to perform explicit reviewed trust/burn genesis with self-trust test manifests, preserving the production majority-floor rejection.

## Task Commits

1. **Task 1: Split account teardown from node policy lifetime** - `34893d8d`, `cdbb620d6` (test, feat)
2. **Task 2: Prove live quorum-approved burn changes actual PayEscrow** - `dd21232e` (test)
3. **Regression fixture repair: Execute reviewed genesis in legacy account tests** - `0e644472` (test)

## Files Created/Modified

- `src/account/GeniusNode.hpp` / `src/account/GeniusNode.cpp` - Separate account-bound recreation from node-policy ownership and retain policy services across account changes.
- `src/account/TransactionManager.hpp` / `src/account/TransactionManager.cpp` - Consume the shared confirmed-burn provider and return typed not-ready failures.
- `test/src/multiaccount/policy_lifetime_multi_account_test.cpp` - Repeated identity, registration, callback, fail-closed, and live-economic assertions.
- `test/src/account/burnconfig_policy_e2e_test.cpp` - Exact PayEscrow burn results for accepted and rejected candidate paths.
- `test/src/account/account_management_test.cpp` / `test/testutil/genius_node_test_access.hpp` - Explicit reviewed genesis ceremony for legacy account integration fixtures.
- `test/src/account/CMakeLists.txt` / `test/src/multiaccount/CMakeLists.txt` - Register the new and extended regression targets.

## Decisions Made

- Kept trust-policy owners at node/GlobalDB scope because account selection changes the signing/transaction consumer, not the network's durable trust authority.
- Used a shared confirmed provider as the TransactionManager boundary, avoiding CRDT access in PayEscrow and ensuring publication occurs only after verified durable activation.
- Repaired tests with explicit ceremony helpers rather than accepting empty trusted-peer sets or relaxing the majority-floor invariant.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Legacy account fixtures attempted unsafe zero-peer startup**
- **Found during:** Exact HIGH-threat regression gate
- **Issue:** `account_management_test` wrote no trusted peers and waited for READY. Phase 13 correctly rejected its zero-peer quorum configuration, causing the old fixture to wait indefinitely.
- **Fix:** Added deterministic self-trust test configuration and friend-access ceremony helpers that submit the canonical configured genesis approval, wait for durable trust confirmation, confirm initial burn, and then wait for READY.
- **Files modified:** `test/src/account/account_management_test.cpp`, `test/testutil/genius_node_test_access.hpp`
- **Verification:** All 5 account-management cases pass, and the same suite passes inside the exact plan gate.
- **Committed in:** `0e644472`

---

**Total deviations:** 1 auto-fixed bug in a legacy integration fixture.
**Impact on plan:** Production validation remains fail closed; the repair makes tests exercise the intended reviewed ceremony.

## Issues Encountered

- Initial focused tests could not bind local TCP sockets inside the filesystem sandbox (`Operation not permitted`). They were rerun with approved local socket access and passed; no code or test expectation was changed for that environment constraint.
- An interrupted executor left an orphaned `account_management_test` process. The exact PID was identified and terminated before clean reruns; no project files were affected.

## Verification

- Focused `account_management_test --gtest_color=no` - PASS, 5/5 tests in 63.917 seconds.
- Exact plan HIGH-threat command - PASS with exit status 0:

```sh
ctest --test-dir build/OSX/Release --output-on-failure -R 'genesis_manifest_test|quorum_policy_test|trust_state_store_test|operator_approval_test|trust_genesis_tool_test|trust_first_boot_e2e_test|trust_restart_test|trust_tamper_e2e_test|securecrdt|trustedpeer|burnconfig|account_management|node_startup|startup|multi_account' && for i in 1 2 3 4 5; do build/OSX/Release/test_bin/policy_lifetime_multi_account_test || exit 1; done
```

- CTest selection - PASS, 25/25: account management, BurnConfig unit/e2e, SecureCrdt interface/registry/candidate/race/quorum/contract, genesis manifest, trust tool, quorum policy, trust store, operator approval, TrustedPeerRegistry genesis/quorum/floor, startup wiring, first boot, restart, tamper, node startup, multi-account, and policy lifetime.
- CTest elapsed time - 276.34 seconds with zero failed or skipped selected tests.
- Five additional consecutive `policy_lifetime_multi_account_test` invocations - PASS, 5/5; each asserted retained policy identity and post-switch behavior.
- Static acceptance checks - PASS: no `ResetQuorumMembers` call remains in GeniusNode account selection; tests contain real PayEscrow calls and typed `TRUST_POLICY_NOT_READY` assertions; no skip, disabled case, or fixed sleep exists in the new test bodies.
- `git diff --check` - PASS.

## Known Stubs

None.

## User Setup Required

None.

## Next Phase Readiness

- Plan 13-11 can consume the complete exact-gate evidence and reconcile the milestone audit, requirements, roadmap, validation map, and security closure.
- No blocker remains from Plan 13-08.

## Self-Check: PASSED

- All implementation and regression commits exist on the phase branch.
- The exact named HIGH-threat gate passed with 25/25 selected tests plus five consecutive lifetime repetitions.
- Both TDD artifacts use real PayEscrow behavior, preserve production fail-closed invariants, and contain no accepted skip or flake.
- No task-related uncommitted file remains; the two pre-existing unrelated untracked paths were left untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-12*
