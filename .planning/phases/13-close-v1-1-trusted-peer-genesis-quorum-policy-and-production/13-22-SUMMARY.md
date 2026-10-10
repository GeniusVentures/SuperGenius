---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 22
subsystem: security verification
tags: [ctest, junit, gtest, trust-state, startup, passive-policy, multi-account]

requires:
  - phase: 13-19
    provides: coherent locked trust-state reads and non-recursive commit verification
  - phase: 13-20
    provides: production-owned deterministic initial burn and actionable refresh outcomes
  - phase: 13-21
    provides: signer-free passive policy activation and safe callback teardown
provides:
  - exact five-case focused XML evidence for CR-05, CR-06, CR-07, and WR-06
  - structural proof that startup tests contain no direct activation hook and passive convergence needs exactly one operator-B approval
  - freshly linked exact 25-selected and 25-passed Phase 13 JUnit evidence with no failure, skip, or disabled result
  - five additional consecutive policy-lifetime passes and named mitigations for HIGH threats T13-G10 through T13-G16
affects: [phase-13-verification, v1.1-closure, security-evidence]

tech-stack:
  added: []
  patterns: [fresh-link focused-to-full gate, structural source guard, machine-checked CTest and JUnit accounting]

key-files:
  created:
    - .planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-22-SUMMARY.md
  modified: []

key-decisions:
  - "Final Phase 13 closure requires all 25 selected targets to be freshly linked before exact focused XML, structural source guards, broad JUnit accounting, and five post-gate lifetime repetitions run in one fail-fast chain."
  - "Passive policy evidence permits later failure-reporting and teardown submissions only after the balanced durable-convergence wait; the pre-convergence window contains exactly one operator-B approval and no activation nudge."

patterns-established:
  - "Release evidence couples exact focused case identity with declaration-level DISABLED_ rejection, source-structure guards, exact target enumeration, JUnit accounting, and repeated lifetime execution."

requirements-completed: [BOOT-04, POLICY-01, TEST-01, TPR-02, BURN-03]

duration: 9 min
completed: 2026-08-13
---

# Phase 13 Plan 22: Fresh-Linked Production Closure Gate Summary

**Five exact production/concurrency counterexamples, the unchanged 25-target Phase 13 gate, structural no-bypass proofs, and five additional policy-lifetime repetitions all passed from freshly linked release binaries.**

## Performance

- **Duration:** 9 min
- **Started:** 2026-08-13T20:03:45Z
- **Completed:** 2026-08-13T20:12:34Z
- **Tasks:** 1 verification-only task
- **Files modified:** 1 evidence summary plus plan-tracking metadata

## Accomplishments

- Freshly linked every one of the 25 targets selected by the established Phase 13 regex, including `policy_lifetime_multi_account_test`, before executing any focused or broad test.
- Mechanically proved the exact five fully-qualified focused cases executed and passed with zero failure, error, skip, not-run, disabled result, or `DISABLED_` counterpart.
- Proved the startup E2E source contains no direct activation-hook call and that passive policy convergence has exactly one operator-B approval with no pre-convergence administrative/proposal/activation nudge.
- Enumerated exactly 25 CTest targets with no disabled target, then proved `selected=25 executed=25 passed=25 failed=0 skipped=0 disabled=0` from JUnit.
- Passed five additional consecutive executions of the freshly linked policy-lifetime binary and mapped every HIGH threat T13-G10 through T13-G16 to named passing evidence.

## Task Commits

This plan is verification-only and made no product or test-source change. Its evidence and 13-22 tracking are committed together in the plan metadata commit.

## Exact Automated Command Record

The successful run executed the exact `<verify><automated>` fail-fast chain in `13-22-PLAN.md` (with XML `&amp;&amp;` decoded to shell `&&`) under `bash` with `set -euo pipefail`. The operational wrapper was:

```bash
bash -o pipefail -c 'bash /private/tmp/sgnus-plan-13-22-closure.sh 2>&1 | tee /private/tmp/sgnus-plan-13-22-rerun-2e900d06.log'
```

The script contained the plan command verbatim with readable line breaks. Its exact substantive invocations were:

```bash
phase13_regex='genesis_manifest_test|quorum_policy_test|trust_state_store_test|operator_approval_test|trust_genesis_tool_test|trust_first_boot_e2e_test|trust_restart_test|trust_tamper_e2e_test|securecrdt|trustedpeer|burnconfig|account_management|node_startup|startup|multi_account'
expected_count=25
phase13_evidence_dir="$(mktemp -d)"

cmake --build build/OSX/Release --target account_management_test burnconfig_test burnconfig_policy_e2e_test securecrdt_interface_test securecrdt_registry_test securecrdt_candidate_test securecrdt_candidate_race_test securecrdt_quorum_gate_test securecrdt_propose_sign_quorum_test securecrdt_quorum_contract_e2e_test genesis_manifest_test trust_genesis_tool_test quorum_policy_test trust_state_store_test operator_approval_test trustedpeerregistry_genesis_test trustedpeerregistry_quorum_test trustedpeerregistry_threshold_floor_test startup_wiring_test trust_first_boot_e2e_test trust_restart_test trust_tamper_e2e_test node_startup_test multi_account_test policy_lifetime_multi_account_test -j8

build/OSX/Release/test_bin/trust_state_store_test --gtest_filter='TrustStateStoreTest.LoadAndVerifySerializesPolicyAndBurnTransitionWithoutTornSnapshot:TrustStateStoreTest.CommitPathsUseUnlockedVerifierWithoutSelfDeadlock' --gtest_output="xml:$phase13_evidence_dir/focused-trust-state.xml"

build/OSX/Release/test_bin/trust_first_boot_e2e_test --gtest_filter='TrustFirstBootE2ETest.ProductionControllerInitiatesBurnAndRestartRecoversPersistedApprovals:TrustFirstBootE2ETest.PreloadedRefreshActivationFailuresAreReturnedAndEmitted:TrustFirstBootE2ETest.PassiveNodeDurablyActivatesPolicyWithoutExtraAdminCall' --gtest_output="xml:$phase13_evidence_dir/focused-first-boot.xml"

python3 -c '<exact focused XML/name/status/declaration parser from 13-22-PLAN.md>' "$phase13_evidence_dir/focused-trust-state.xml" "$phase13_evidence_dir/focused-first-boot.xml" test/src/trustedpeer/trust_state_store_test.cpp test/src/startup/trust_first_boot_e2e_test.cpp

! rg --no-line-number 'OnTrustedPeerGenesisConfirmed\(|TryActivate(Burn|Policy)Candidate\(' test/src/startup/trust_first_boot_e2e_test.cpp | rg -v '^[[:space:]]*//'

python3 -c '<exact comment/string-aware brace and balanced-wait parser from revised 13-22-PLAN.md>' test/src/startup/trust_first_boot_e2e_test.cpp

ctest --test-dir build/OSX/Release -N -V -R "$phase13_regex" | tee "$phase13_evidence_dir/selection.txt"
test "$(grep -Ec '^Total Tests: [0-9]+$' "$phase13_evidence_dir/selection.txt")" -eq 1
test "$(sed -nE 's/^Total Tests: ([0-9]+)$/\1/p' "$phase13_evidence_dir/selection.txt")" -eq "$expected_count"
! grep -Eq '^[[:space:]]*Test[[:space:]]+#[0-9]+:.*\(Disabled\)[[:space:]]*$' "$phase13_evidence_dir/selection.txt"

ctest --test-dir build/OSX/Release --output-on-failure --output-junit "$phase13_evidence_dir/results.xml" -R "$phase13_regex"

python3 -c '<exact JUnit case/suite accounting parser from 13-22-PLAN.md>' "$phase13_evidence_dir/results.xml" "$expected_count"

for i in 1 2 3 4 5; do
  build/OSX/Release/test_bin/policy_lifetime_multi_account_test && printf 'lifetime_repeat_%s=PASS\n' "$i" || exit 1
done
```

The three abbreviated `python3 -c` payloads above are recorded in full, byte-for-byte, in the plan's automated command; the invocations, inputs, and asserted outputs shown here are the exact executed ones. The complete run exited `0`.

## Fresh-Link and Focused XML Evidence

- Evidence directory: `/var/folders/q4/j8ttdlss0xdf0hnkmskvc37r0000gn/T/tmp.84Zcg6CIkP`
- Full log: `/private/tmp/sgnus-plan-13-22-rerun-2e900d06.log`
- Focused store XML: `/var/folders/q4/j8ttdlss0xdf0hnkmskvc37r0000gn/T/tmp.84Zcg6CIkP/focused-trust-state.xml`
- Focused startup XML: `/var/folders/q4/j8ttdlss0xdf0hnkmskvc37r0000gn/T/tmp.84Zcg6CIkP/focused-first-boot.xml`
- The explicit 25-target build exited `0`; all named release executables were linked before focused execution.
- Mechanical parser output:

```text
focused_executed=5 focused_passed=5 focused_failed=0 focused_skipped=0 focused_disabled=0
```

Exact executed name set:

1. `TrustStateStoreTest.LoadAndVerifySerializesPolicyAndBurnTransitionWithoutTornSnapshot`
2. `TrustStateStoreTest.CommitPathsUseUnlockedVerifierWithoutSelfDeadlock`
3. `TrustFirstBootE2ETest.ProductionControllerInitiatesBurnAndRestartRecoversPersistedApprovals`
4. `TrustFirstBootE2ETest.PreloadedRefreshActivationFailuresAreReturnedAndEmitted`
5. `TrustFirstBootE2ETest.PassiveNodeDurablyActivatesPolicyWithoutExtraAdminCall`

The declaration scan found no disabled fixture or case counterpart for any of those five names. Both focused binaries and the combined parser exited `0`.

## Structural Production-Path Evidence

- Global direct-hook scan: `direct_startup_hook_calls=0`; there is no non-comment direct call to `OnTrustedPeerGenesisConfirmed`, `TryActivateBurnCandidate`, or `TryActivatePolicyCandidate` anywhere in `trust_first_boot_e2e_test.cpp`.
- Revised brace-aware passive-body parser output:

```text
passive_body_approve_total=1 operator_b_approve=1 pre_convergence_activation_nudges=0 passive_durable_wait=1
```

The guarded interval begins after the sole `operator_b_admin.Approve` and ends at the balanced `assertWaitForCondition(...)` containing `passive_store->LoadAndVerify`. It contains no admin receiver call, proposal, approval, submission, direct activation, or genesis-confirmation nudge. Later failure-reporting and teardown submissions are outside this convergence interval.

## Exact 25-Test Machine Evidence

- Selection file: `/var/folders/q4/j8ttdlss0xdf0hnkmskvc37r0000gn/T/tmp.84Zcg6CIkP/selection.txt`
- JUnit file: `/var/folders/q4/j8ttdlss0xdf0hnkmskvc37r0000gn/T/tmp.84Zcg6CIkP/results.xml`
- Regex: `genesis_manifest_test|quorum_policy_test|trust_state_store_test|operator_approval_test|trust_genesis_tool_test|trust_first_boot_e2e_test|trust_restart_test|trust_tamper_e2e_test|securecrdt|trustedpeer|burnconfig|account_management|node_startup|startup|multi_account`
- Enumeration: one `Total Tests: 25` line and `selection_total=25 disabled_targets=0`.
- CTest: `100% tests passed, 0 tests failed out of 25`; total real time `383.07 sec`; exit status `0`.
- Mechanical JUnit accounting: `selected=25 executed=25 passed=25 failed=0 skipped=0 disabled=0`; assertion exit status `0`.

CR-01 through CR-04 remained covered by the unchanged regex and all their established targets passed; this plan did not reopen their accepted implementation or test scope.

## Policy-Lifetime Repetition Evidence

Each post-gate invocation used the freshly linked `build/OSX/Release/test_bin/policy_lifetime_multi_account_test` and exited `0`:

```text
lifetime_repeat_1=PASS
lifetime_repeat_2=PASS
lifetime_repeat_3=PASS
lifetime_repeat_4=PASS
lifetime_repeat_5=PASS
```

## HIGH-Threat Dispositions

| Threat | Disposition | Named passing evidence |
|---|---|---|
| T13-G10 — torn coherent trust reads | MITIGATED | `TrustStateStoreTest.LoadAndVerifySerializesPolicyAndBurnTransitionWithoutTornSnapshot` passed focused and `trust_state_store_test` passed in the 25-target gate. |
| T13-G11 — lock-owning commit self-deadlock | MITIGATED | `TrustStateStoreTest.CommitPathsUseUnlockedVerifierWithoutSelfDeadlock` passed focused and `trust_state_store_test` passed broadly. |
| T13-G12 — initial-burn startup denial of service | MITIGATED | `TrustFirstBootE2ETest.ProductionControllerInitiatesBurnAndRestartRecoversPersistedApprovals` passed focused and `trust_first_boot_e2e_test` passed broadly. |
| T13-G13 — automatic signing privilege boundary | MITIGATED | Non-member no-sign and exact deterministic approval assertions passed inside `ProductionControllerInitiatesBurnAndRestartRecoversPersistedApprovals`; the direct-hook scan was clean. |
| T13-G14 — refresh failure repudiation/denial of service | MITIGATED | `TrustFirstBootE2ETest.PreloadedRefreshActivationFailuresAreReturnedAndEmitted` passed focused and within the broad startup target. |
| T13-G15 — passive policy tampering/elevation | MITIGATED | `TrustFirstBootE2ETest.PassiveNodeDurablyActivatesPolicyWithoutExtraAdminCall` passed focused; the structural guard proved one operator-B approval and no pre-convergence activation nudge. |
| T13-G16 — callback result/lifetime repudiation/denial of service | MITIGATED | Pending, forced-error, and post-destruction sections of `PassiveNodeDurablyActivatesPolicyWithoutExtraAdminCall` passed, and all five lifetime repetitions passed. |

**Unresolved HIGH dispositions:** None.

T-13-SC remains accepted at LOW: this plan installed no package and changed no dependency.

## Files Created/Modified

- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-22-SUMMARY.md` — Exact fresh-link, focused XML, structural guard, CTest/JUnit, lifetime, and HIGH-threat closure evidence.
- `.planning/STATE.md` — Plan position, progress, metric, and session tracking only.
- `.planning/ROADMAP.md` — Marks only Plan 13-22 complete and refreshes Phase 13 progress/status.

## Decisions Made

- Retained the established Phase 13 regex and expected count of 25 without semantic or target-count drift.
- Required the exact five focused case names and structural no-bypass proofs before accepting broad success.
- Counted later failure-reporting/teardown submissions outside the balanced passive durable-convergence window, as specified by the corrected Plan 13-22 guard.

## Deviations from Plan

None - the successful closure run executed the revised plan chain without product-source or test-source changes.

## Issues Encountered

- Earlier closure attempts exposed a structural guard-window defect and fresh-link compatibility failures. Those were corrected before this successful restart in prerequisite commits `5773e64a`, `cb6b9946`, and `2e900d06`. The complete recorded chain at `2e900d06` passed from the beginning without an inline change or retry.
- Network-backed tests require local ephemeral listener permission in the managed environment; the exact successful chain ran with that permission.

## Known Stubs

None. This verification-only plan created no product path, runtime implementation, mock data source, or placeholder.

## Threat Flags

None. This plan introduced no endpoint, authentication path, schema, package, dependency, file-access pattern, transport, topic, or new network surface.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- CR-05, CR-06, CR-07, and WR-06 now have exact passing production-facing evidence; BOOT-04, POLICY-01, TEST-01, TPR-02, and BURN-03 are no longer blocked by them.
- The Phase 13 release/security gate is machine-proven at 25 selected, 25 executed, and 25 passed with zero failure, skip, or disabled result.
- Every HIGH threat T13-G10 through T13-G16 has a named green mitigation and no unresolved HIGH disposition remains.

## Self-Check: PASSED

- This summary exists and records the explicit 25-target build, exact focused commands/names/counts/XML paths, disabled-counterpart proof, direct-hook and passive-body guard counts, exact regex/enumeration/JUnit counts, five repetition labels, and T13-G10 through T13-G16 mappings.
- The successful chain exited `0`; all generated evidence files exist and the full log contains every asserted marker.
- No product source, test source, dependency, requirement, project, prior plan, or prior summary was modified by Plan 13-22.
- The two protected pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-13*
