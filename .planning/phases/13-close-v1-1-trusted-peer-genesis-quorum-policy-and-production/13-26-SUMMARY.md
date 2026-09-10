---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 26
subsystem: security verification
tags: [ctest, googletest, junit, sanitizer-accounting, lifecycle, release-gate]

requires:
  - phase: 13-18
    provides: first exact focused and 25-target security closure gate
  - phase: 13-22
    provides: coherent-read, initial-burn, passive-policy, and callback-lifetime closure evidence
  - phase: 13-23
    provides: passive burn-successor convergence and corrected lifetime coverage
  - phase: 13-24
    provides: retained policy-quorum replay after controller reconstruction
  - phase: 13-25
    provides: serialized persisted-ready startup and same-path historical restart ownership
provides:
  - exact source/XML equality and 15/15 execution for all prior and CR-08/CR-09/CR-10 counterexamples
  - exact explicit 25-name CTest enumeration and JUnit equality with 25/25 passing
  - honest target-scoped sanitizer accounting and five consecutive passive-lifetime passes
  - named passing dispositions for every HIGH threat T13-G01 through T13-G25
affects: [phase-13-verification, milestone-v1.1-audit, trusted-peer-production-closure]

tech-stack:
  added: []
  patterns: [exact-name source-to-XML accounting, explicit CTest-to-JUnit set equality, target-scoped sanitizer eligibility]

key-files:
  created:
    - .planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-26-SUMMARY.md
  modified:
    - .planning/STATE.md
    - .planning/ROADMAP.md

key-decisions:
  - "Sanitizer status is PASS only when both exact lifecycle targets prove the same sanitizer in their own compile units and link/runtime; otherwise it is NOT_RUN."
  - "Final closure requires exact source, XML, CTest, and JUnit name-set equality, not count-only or regex-substitution evidence."
  - "The corrected passive receiver must pass five post-broad-gate repetitions without receiver administration or direct activation."

patterns-established:
  - "Release closure gate: freshly link every explicit target before focused and broad execution."
  - "Absence accounting: unavailable target-scoped sanitizer instrumentation is NOT_RUN, never PASS."

requirements-completed: [BOOT-04, POLICY-01, TEST-01, TPR-02, BURN-03]

duration: 11min
completed: 2026-08-14
---

# Phase 13 Plan 26: Exact Final Security Closure Gate Summary

**Freshly linked Release binaries passed the exact 15-counterexample and explicit 25-target gates, five passive-lifetime repetitions, and all T13-G01..G25 HIGH-threat dispositions without weakening production-path or accounting requirements.**

## Performance

- **Duration:** 11 min
- **Started:** 2026-08-14T17:51:27Z
- **Completed:** 2026-08-14T18:02:23Z
- **Tasks:** 2
- **Files modified:** 1 plan harness before execution, this summary, and final tracking metadata; no product source changed during the successful chain

## Accomplishments

- Rebuilt all 25 explicit Release targets, then proved exact unique declaration/XML equality for 15 focused counterexamples with 15 passes and zero failure, error, skip, not-run, disabled result, or disabled counterpart.
- Proved the unchanged Phase 13 CTest regex enumerated exactly the explicit 25-name set and that JUnit reported exactly the same 25 names with 25 passes and zero failure, skip, or disabled result.
- Proved passive-policy, passive-burn, reconstruction-no-write, and historical same-path/single-owner fixture shape before runtime execution.
- Recorded sanitizer eligibility honestly as `sanitizer_configured=0 sanitizer_status=NOT_RUN` because no tree/kind fully instrumented both exact lifecycle targets' own compile units and link/runtime.
- Passed the corrected passive burn lifetime case five additional consecutive labeled times after the broad gate.

## Task Commits

1. **Task 1: Correct and pass the historical restart structural guard** - `8506b51a` (docs)
2. **Pre-gate exact-case restoration required by Task 2** - `f681a975` (test)
3. **Task 2: Run and record the cumulative closure gate** - captured by this summary/tracking commit

## Verification Evidence

### Task 1 — Structural production-path guards

The comment/string-aware structural gate exited `0` and reported:

```text
prior_passive_guard=PASS passive_burn_guard=PASS reconstruction_no_write_guard=PASS historical_existing_path_guard=PASS
```

The gate mechanically proved:

- the passive policy path contains the single expected operator-B approval and no receiver admin/direct activation before durable observation;
- the passive burn case contains exactly operator A's proposal and operator B's approval, then only receiver-C durable `LoadAndVerify` and real `PayEscrow` observations inside the protected window;
- controller reconstruction performs no new write, administration, callback injection, or direct activation while rediscovering the durable policy;
- the historical test captures `historical_base_path`, reopens the same account with that exact path, and retains exact single-manager construction/start and callback-owner generation assertions;
- the fixture `CreateNode` helper's optional `existingBasePath` branch maps the supplied path into node storage, while fresh path generation and cleanup remain confined to the non-reuse branch.

### Task 2 — Fresh build and exact focused accounting

The chain explicitly rebuilt these 25 targets before execution:

`account_management_test`, `burnconfig_test`, `burnconfig_policy_e2e_test`, `securecrdt_interface_test`, `securecrdt_registry_test`, `securecrdt_candidate_test`, `securecrdt_candidate_race_test`, `securecrdt_quorum_gate_test`, `securecrdt_propose_sign_quorum_test`, `securecrdt_quorum_contract_e2e_test`, `genesis_manifest_test`, `trust_genesis_tool_test`, `quorum_policy_test`, `trust_state_store_test`, `operator_approval_test`, `trustedpeerregistry_genesis_test`, `trustedpeerregistry_quorum_test`, `trustedpeerregistry_threshold_floor_test`, `startup_wiring_test`, `trust_first_boot_e2e_test`, `trust_restart_test`, `trust_tamper_e2e_test`, `node_startup_test`, `multi_account_test`, and `policy_lifetime_multi_account_test`.

The exact 15 focused names were:

1. `TrustStateStoreTest.PolicySuccessorRejectedUntilInitialBurnPeerConfirmed`
2. `TrustStateStoreTest.BurnV2RejectedUntilInitialBurnPeerConfirmed`
3. `TrustStateStoreTest.LoadAndVerifySerializesPolicyAndBurnTransitionWithoutTornSnapshot`
4. `TrustStateStoreTest.CommitPathsUseUnlockedVerifierWithoutSelfDeadlock`
5. `TrustFirstBootE2ETest.PolicyV2BeforeInitialBurnCannotStrandStartup`
6. `TrustRestartTest.HistoricalBurnProofRemainsReadyAfterCurrentThresholdIncrease`
7. `BurnConfigPolicyE2ETest.PayEscrowUsesExactOverflowSafeBurnForUint64Maximum`
8. `SecureCrdtQuorumGateTest.LocalOutsiderSignatureNeverPersists`
9. `SecureCrdtQuorumGateTest.RemoteOutsiderSignatureNeverReplicatesAndRetentionBoundTracksAuthorizedSet`
10. `TrustFirstBootE2ETest.ProductionControllerInitiatesBurnAndRestartRecoversPersistedApprovals`
11. `TrustFirstBootE2ETest.PreloadedRefreshActivationFailuresAreReturnedAndEmitted`
12. `TrustFirstBootE2ETest.PassiveNodeDurablyActivatesPolicyWithoutExtraAdminCall`
13. `TrustFirstBootE2ETest.RetainedPolicyQuorumRetriesAfterControllerReconstructionWithoutNewWrite`
14. `PolicyLifetimeMultiAccountTest.PassiveBurnSuccessorChangesPayEscrowWithoutReceiverAdmin`
15. `MultiAccountTest.PersistedHistoricalTrustAndTransactionsRestartWithSingleManagerOwnership`

Machine accounting reported:

```text
focused_names_exact=PASS focused_declarations_exact=PASS focused_executed=15 focused_passed=15 focused_failed_or_skipped=0 focused_disabled=0
ctest_selected_names_exact=PASS selected=25 disabled=0
100% tests passed, 0 tests failed out of 25
junit_names_exact=PASS executed=25 passed=25 failed=0 skipped=0 disabled=0
```

The CTest run took 422.30 seconds. Selection required one unique `Total Tests: 25`, exact set equality with the explicit list above, and no disabled target; JUnit independently required the same exact set.

### Sanitizer accounting

The harness inspected only compile records belonging to `CMakeFiles/policy_lifetime_multi_account_test.dir/` and `CMakeFiles/multi_account_test.dir/`, then required matching target-specific link commands or sanitizer-runtime imports. No ASan or TSan tree/kind satisfied the complete two-target proof:

```text
sanitizer_configured=0 sanitizer_status=NOT_RUN
```

This is an honest availability result, not a sanitizer pass, and it did not substitute for the mandatory Release gate.

### Five post-gate lifetime repetitions

The freshly linked `PolicyLifetimeMultiAccountTest.PassiveBurnSuccessorChangesPayEscrowWithoutReceiverAdmin` case passed five consecutive exact-filter executions after broad green:

```text
passive_lifetime_repeat_1=PASS
passive_lifetime_repeat_2=PASS
passive_lifetime_repeat_3=PASS
passive_lifetime_repeat_4=PASS
passive_lifetime_repeat_5=PASS
```

## HIGH Threat Dispositions

| Threat | Disposition | Named passing evidence |
|---|---|---|
| T13-G01 — durable policy/burn successor bypass | MITIGATED | Both focused TrustStateStore successor-rejection cases and broad `trust_state_store_test`. |
| T13-G02 — initial trust-state sequence denial of service | MITIGATED | The same two focused store cases preserve recoverable sequencing through peer-confirmed initial burn. |
| T13-G03 — runtime/controller privilege bypass | MITIGATED | `PolicyV2BeforeInitialBurnCannotStrandStartup` and broad `trust_first_boot_e2e_test`. |
| T13-G04 — activation failure repudiation | MITIGATED | Broad `trust_genesis_tool_test` and `trust_first_boot_e2e_test`, including typed activation visibility. |
| T13-G05 — historical-authorizer readiness loss | MITIGATED | `HistoricalBurnProofRemainsReadyAfterCurrentThresholdIncrease` and broad `trust_restart_test`. |
| T13-G06 — overflowing PayEscrow accounting | MITIGATED | `PayEscrowUsesExactOverflowSafeBurnForUint64Maximum` focused and broad. |
| T13-G07 — invalid arithmetic input persistence | MITIGATED | The same PayEscrow case's invalid-basis/no-persistence assertions and broad burn/account targets. |
| T13-G08 — outsider legacy signature elevation | MITIGATED | Both exact outsider focused cases and all selected SecureCrdt targets. |
| T13-G09 — unbounded retained signature children | MITIGATED | Remote-outsider retention-bound focused case and all selected SecureCrdt targets. |
| T13-G10 — torn coherent trust reads | MITIGATED | `LoadAndVerifySerializesPolicyAndBurnTransitionWithoutTornSnapshot` focused and broad. |
| T13-G11 — lock-owning commit self-deadlock | MITIGATED | `CommitPathsUseUnlockedVerifierWithoutSelfDeadlock` focused and broad. |
| T13-G12 — initial-burn startup denial of service | MITIGATED | `ProductionControllerInitiatesBurnAndRestartRecoversPersistedApprovals` focused and broad. |
| T13-G13 — automatic signing privilege boundary | MITIGATED | The production-controller case plus the clean global direct-hook rejection. |
| T13-G14 — refresh failure repudiation/denial of service | MITIGATED | `PreloadedRefreshActivationFailuresAreReturnedAndEmitted` focused and broad. |
| T13-G15 — passive policy tampering/elevation | MITIGATED | `PassiveNodeDurablyActivatesPolicyWithoutExtraAdminCall` plus the no-admin/direct-activation structural guard. |
| T13-G16 — callback result/lifetime denial of service | MITIGATED | Passive-policy lifetime sections in the focused and broad startup target. |
| T13-G17 — passive burn activation tampering/elevation | MITIGATED | `PassiveBurnSuccessorChangesPayEscrowWithoutReceiverAdmin` and its exact signer/no-receiver-action guard. |
| T13-G18 — post-ready burn divergence/denial of service | MITIGATED | The same case's durable head, provider, and real `PayEscrow` convergence plus five repeats. |
| T13-G19 — passive lifetime evidence repudiation | MITIGATED | Exact A/B decisions, zero receiver admin/direct activation, focused XML equality, and five repetitions. |
| T13-G20 — reconstruction policy retry denial of service | MITIGATED | `RetainedPolicyQuorumRetriesAfterControllerReconstructionWithoutNewWrite` focused and broad. |
| T13-G21 — retained policy activation tampering/elevation | MITIGATED | The same case's current-policy quorum/CAS assertions and reconstruction no-write guard. |
| T13-G22 — retry failure repudiation | MITIGATED | Typed failure, controller-local suppression, and authoritative rediscovery sections of the same case. |
| T13-G23 — persisted-ready re-entry denial of service | MITIGATED | `PersistedHistoricalTrustAndTransactionsRestartWithSingleManagerOwnership` exact construction/start assertions. |
| T13-G24 — manager callback ownership tampering/denial of service | MITIGATED | The same case's owner-generation and functional callback assertions plus broad `multi_account_test`. |
| T13-G25 — historical restart evidence repudiation | MITIGATED | Same-path historical focused case, helper branch guard, and separate retained fresh-client broad coverage. |

No HIGH threat remains unresolved. CR-08, CR-09, CR-10, and both halves of WR-07 now have production-facing named evidence in the single exact closure chain.

## Files Created/Modified

- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-26-PLAN.md` - Corrected the historical structural assertion to inspect the test and `CreateNode` helper at their actual ownership boundaries.
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-26-SUMMARY.md` - Records exact build, focused, broad, sanitizer, repetition, and threat evidence.
- `.planning/STATE.md` and `.planning/ROADMAP.md` - Final plan/phase tracking after the complete green chain.

## Decisions Made

- Count-only success is insufficient: focused source declarations, focused XML, CTest enumeration, and JUnit results must each equal their explicit expected name set.
- Sanitizer evidence is target-scoped across both lifecycle targets. Configuration elsewhere in a tree, toolchain, dependency, or fuzz target cannot qualify the pair.
- The historical restart guard separately verifies test-level same-path reuse/ownership and helper-level `existingBasePath` mapping, matching where those responsibilities actually live.

## Deviations from Plan

### Authorized Pre-execution Corrections

**1. [Rule 3 - Blocking verification harness] Corrected an impossible historical assertion**
- **Found during:** Task 1
- **Issue:** The original guard required the helper identifier `existingBasePath` inside the extracted historical test body, although that identifier correctly belongs to `CreateNode`.
- **Fix:** Separately proved the historical test's same `historical_base_path` reuse and exact manager-owner assertions, then proved the comment/string-aware `CreateNode` helper maps optional `existingBasePath` into node storage rather than generating a fresh base.
- **Files modified:** `13-26-PLAN.md` only
- **Verification:** Both embedded automated blocks passed `bash -n`; the corrected Task 1 gate exited `0`.
- **Committed in:** `8506b51a`

**2. [Rule 3 - Blocking exact gate] Restored the required initial-burn ordering regression declaration**
- **Found during:** the first Task 2 attempt
- **Issue:** `TrustFirstBootE2ETest.PolicyV2BeforeInitialBurnCannotStrandStartup` was absent, so exact source/XML accounting correctly stopped at 14/15.
- **Fix:** Restored that exact test-only case before the final verification-only rerun.
- **Files modified:** `test/src/startup/trust_first_boot_e2e_test.cpp`
- **Verification:** The rerun reported exact unique source/XML equality and 15/15 passes, followed by exact 25/25 broad green.
- **Committed in:** `f681a975`

---

**Total deviations:** 2 authorized blocking corrections (both completed before the successful full rerun).
**Impact on plan:** The corrections restored the intended mechanical gate; they did not weaken security, production-path, exact-count, lifetime, or sanitizer requirements. No product source or test was edited during the successful rerun.

## Issues Encountered

- Listener-backed tests require local port binding, so the complete chain ran with approved listener access.
- Earlier orchestrator supplemental approval-review timeouts occurred before execution and were not counted as product failures; the executor's complete chain passed.

## Known Stubs

None. This plan adds only verification documentation and tracking; all evidence comes from freshly linked production-path tests.

## Threat Flags

None. The successful execution changed no product code, endpoint, authentication path, schema, storage boundary, package, topic, or transport.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- Phase 13's third gap wave is fully green: CR-08, CR-09, CR-10, and WR-07 are covered by exact production-facing counterexamples.
- All 26 Phase 13 plans can now be tracked complete and the v1.1 milestone can proceed to final audit/verification.
- Sanitizer execution remains honestly unavailable for the exact two-target pair in the discovered build trees; this is recorded as `NOT_RUN`, not an unresolved HIGH or a substituted pass.

## Self-Check: PASSED

- This summary and the corrected plan harness exist.
- Harness commit `8506b51a` and prerequisite exact-case repair commit `f681a975` exist in repository history.
- The final rerun log records exact focused 15/15, CTest/JUnit 25/25, target-scoped sanitizer `NOT_RUN`, and five labeled lifetime passes at exit `0`.
- No product source/test edit occurred during the successful chain, and the two protected pre-existing untracked paths remain untouched.
- Every HIGH threat T13-G01 through T13-G25 has a named passing disposition with none unresolved.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-14*
