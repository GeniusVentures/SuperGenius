---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 18
subsystem: security verification
tags: [ctest, junit, gtest, trust-state, burn-config, securecrdt, multi-account]

requires:
  - phase: 13-13
    provides: durable initial-burn sequencing and direct-store bypass regressions
  - phase: 13-14
    provides: runtime/admin initial-burn gates and observable activation failures
  - phase: 13-15
    provides: historical-authorizer burn readiness after policy evolution
  - phase: 13-16
    provides: exact full-domain PayEscrow burn arithmetic
  - phase: 13-17
    provides: current-member-bounded legacy SecureCrdt signatures
provides:
  - exact green focused evidence for CR-01 through CR-04
  - machine-checked 25-selected and 25-executed Phase 13 regression evidence with no failures, skips, or disabled tests
  - five additional consecutive node-scoped policy-lifetime passes
  - passing automated dispositions for every HIGH threat T13-G01 through T13-G09
affects: [phase-13-verification, v1.1-closure, security-evidence]

tech-stack:
  added: []
  patterns: [fail-fast focused-to-full gate, machine-checked CTest enumeration and JUnit accounting]

key-files:
  created:
    - .planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-18-SUMMARY.md
  modified: []

key-decisions:
  - "Phase 13 gap closure requires focused counterexamples, exact 25-test enumeration/JUnit accounting, and five post-gate lifetime repetitions in one fail-fast chain."

patterns-established:
  - "Release evidence must reject changed selection counts, enumerated disabled targets, and JUnit skipped/not-run cases rather than relying on ordinary CTest success alone."

requirements-completed: [BOOT-04, BURN-02, BURN-03, TEST-01]

duration: 7min
completed: 2026-08-13
---

# Phase 13 Plan 18: Exact Security Closure Gate Summary

**All four blocker counterexamples, all nine HIGH-threat mitigations, the unchanged 25-test Phase 13 gate, and five additional account-lifetime repetitions passed in one fail-fast execution.**

## Performance

- **Duration:** 7 min
- **Started:** 2026-08-13T15:13:51Z
- **Completed:** 2026-08-13T15:20:17Z
- **Tasks:** 1 verification-only task
- **Files modified:** 1 evidence summary plus plan-tracking metadata

## Accomplishments

- Built all five changed production-facing test binaries and passed every named CR-01 through CR-04 counterexample before the broad gate.
- Preserved the exact Phase 13 regex, enumerated exactly 25 tests, found no `(Disabled)` target, and mechanically parsed JUnit to prove `selected=25 executed=25 passed=25 failed=0 skipped=0 disabled=0`.
- Ran `policy_lifetime_multi_account_test` five more consecutive times after the machine-counted gate, with an individual PASS label for every invocation.
- Closed every T13-G01 through T13-G09 HIGH disposition with its named automated verification; no HIGH threat remains unresolved by this plan.

## Task Commits

This plan is verification-only and made no product or test-source change. Its evidence and state tracking are committed together in the plan metadata commit.

## Exact Automated Command

The following command was run unchanged in semantics and exited `0`:

```bash
set -euo pipefail; phase13_regex='genesis_manifest_test|quorum_policy_test|trust_state_store_test|operator_approval_test|trust_genesis_tool_test|trust_first_boot_e2e_test|trust_restart_test|trust_tamper_e2e_test|securecrdt|trustedpeer|burnconfig|account_management|node_startup|startup|multi_account'; phase13_evidence_dir="$(mktemp -d)"; cmake --build build/OSX/Release --target trust_state_store_test trust_first_boot_e2e_test trust_restart_test burnconfig_policy_e2e_test securecrdt_quorum_gate_test -j8 && build/OSX/Release/test_bin/trust_state_store_test --gtest_filter='*PolicySuccessorRejectedUntilInitialBurnPeerConfirmed*:*BurnV2RejectedUntilInitialBurnPeerConfirmed*' && build/OSX/Release/test_bin/trust_first_boot_e2e_test --gtest_filter='*PolicyV2BeforeInitialBurnCannotStrandStartup*' && build/OSX/Release/test_bin/trust_restart_test --gtest_filter='*HistoricalBurnProofRemainsReadyAfterCurrentThresholdIncrease*' && build/OSX/Release/test_bin/burnconfig_policy_e2e_test --gtest_filter='*PayEscrowUsesExactOverflowSafeBurnForUint64Maximum*' && build/OSX/Release/test_bin/securecrdt_quorum_gate_test --gtest_filter='*OutsiderSignatureNever*:*RetentionBoundTracksAuthorizedSet*' && ctest --test-dir build/OSX/Release -N -V -R "$phase13_regex" | tee "$phase13_evidence_dir/selection.txt"; test "$(grep -Ec '^Total Tests: [0-9]+$' "$phase13_evidence_dir/selection.txt")" -eq 1; test "$(sed -nE 's/^Total Tests: ([0-9]+)$/\1/p' "$phase13_evidence_dir/selection.txt")" -eq 25; ! grep -Eq '^[[:space:]]*Test[[:space:]]+#[0-9]+:.*\(Disabled\)[[:space:]]*$' "$phase13_evidence_dir/selection.txt"; ctest --test-dir build/OSX/Release --output-on-failure --output-junit "$phase13_evidence_dir/results.xml" -R "$phase13_regex"; python3 -c 'import sys, xml.etree.ElementTree as E; root=E.parse(sys.argv[1]).getroot(); tag=lambda x:x.tag.rsplit("}",1)[-1]; suites=[x for x in root.iter() if tag(x)=="testsuite"]; cases=[x for x in root.iter() if tag(x)=="testcase"]; failed=sum(any(tag(y) in ("failure","error") for y in x) for x in cases); skipped=sum(any(tag(y)=="skipped" for y in x) or x.get("status","").lower() in ("disabled","notrun","skipped") for x in cases); disabled=max([int(x.get("disabled","0")) for x in suites] or [0]); passed=len(cases)-failed-skipped; print(f"selected=25 executed={len(cases)} passed={passed} failed={failed} skipped={skipped} disabled={disabled}"); assert len(cases)==25 and passed==25 and failed==0 and skipped==0 and disabled==0' "$phase13_evidence_dir/results.xml"; for i in 1 2 3 4 5; do build/OSX/Release/test_bin/policy_lifetime_multi_account_test && printf 'lifetime_repeat_%s=PASS\n' "$i" || exit 1; done
```

## Focused Blocker Evidence

| Segment | Exact focused invocation | Result | Exit status |
|---|---|---:|---:|
| Build | `cmake --build build/OSX/Release --target trust_state_store_test trust_first_boot_e2e_test trust_restart_test burnconfig_policy_e2e_test securecrdt_quorum_gate_test -j8` | Five targets built | 0 |
| CR-01 durable policy/burn bypasses | `trust_state_store_test --gtest_filter='*PolicySuccessorRejectedUntilInitialBurnPeerConfirmed*:*BurnV2RejectedUntilInitialBurnPeerConfirmed*'` | 2/2 PASS | 0 |
| CR-01 runtime first boot | `trust_first_boot_e2e_test --gtest_filter='*PolicyV2BeforeInitialBurnCannotStrandStartup*'` | 1/1 PASS | 0 |
| CR-02 historical restart | `trust_restart_test --gtest_filter='*HistoricalBurnProofRemainsReadyAfterCurrentThresholdIncrease*'` | 1/1 PASS | 0 |
| CR-03 exact arithmetic | `burnconfig_policy_e2e_test --gtest_filter='*PayEscrowUsesExactOverflowSafeBurnForUint64Maximum*'` | 1/1 PASS | 0 |
| CR-04 outsider/retention | `securecrdt_quorum_gate_test --gtest_filter='*OutsiderSignatureNever*:*RetentionBoundTracksAuthorizedSet*'` | 2/2 PASS | 0 |

The direct-store burn-v2 bypass regression ran before the broad gate; both direct-store tests exercised rejection, unchanged restart heads, exact initial-burn recovery, and successful later successor advancement.

## Exact 25-Test Machine Evidence

- Regex: `genesis_manifest_test|quorum_policy_test|trust_state_store_test|operator_approval_test|trust_genesis_tool_test|trust_first_boot_e2e_test|trust_restart_test|trust_tamper_e2e_test|securecrdt|trustedpeer|burnconfig|account_management|node_startup|startup|multi_account`
- Enumeration command: `ctest --test-dir build/OSX/Release -N -V -R "$phase13_regex"`
- Enumeration result: one `Total Tests` line, `Total Tests: 25`; disabled-target scan found no match; all assertions exited `0`.
- Execution command: `ctest --test-dir build/OSX/Release --output-on-failure --output-junit "$phase13_evidence_dir/results.xml" -R "$phase13_regex"`
- CTest result: 25/25 passed in 286.53 seconds; exit status `0`.
- JUnit parser result: `selected=25 executed=25 passed=25 failed=0 skipped=0 disabled=0`; assertion exit status `0`.

## Policy-Lifetime Repetition Evidence

Each invocation used `build/OSX/Release/test_bin/policy_lifetime_multi_account_test`, ran after the 25-test JUnit assertions, and exited `0`:

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
| T13-G01 — durable policy/burn successor bypass | MITIGATED | `PolicySuccessorRejectedUntilInitialBurnPeerConfirmed` and `BurnV2RejectedUntilInitialBurnPeerConfirmed` passed in the focused store segment and `trust_state_store_test` passed in the 25-test gate. |
| T13-G02 — initial trust-state sequence denial of service | MITIGATED | The same two store counterexamples passed their rejection, reopen-preservation, peer-confirmed burn-v1 recovery, and later-successor assertions. |
| T13-G03 — runtime/controller privilege bypass | MITIGATED | `PolicyV2BeforeInitialBurnCannotStrandStartup` passed focused; `trust_first_boot_e2e_test` passed in the 25-test gate. |
| T13-G04 — activation failure repudiation | MITIGATED | `AdminActivationFailureIsReturnedWhileUnderQuorumRemainsPending` ran within the passing `trust_genesis_tool_test` broad-gate target; startup activation diagnostics also remained covered by the passing `trust_first_boot_e2e_test`. |
| T13-G05 — historical-authorizer readiness loss | MITIGATED | `HistoricalBurnProofRemainsReadyAfterCurrentThresholdIncrease` passed focused; `trust_restart_test` passed in the 25-test gate. |
| T13-G06 — overflowing PayEscrow accounting | MITIGATED | `PayEscrowUsesExactOverflowSafeBurnForUint64Maximum` passed focused and within the passing `burnconfig_policy_e2e_test` broad-gate target. |
| T13-G07 — invalid arithmetic input persistence | MITIGATED | The invalid-basis/no-persistence case in `PayEscrowUsesExactOverflowSafeBurnForUint64Maximum` passed, and the full burn/account targets passed. |
| T13-G08 — outsider legacy signature spoofing/elevation | MITIGATED | `LocalOutsiderSignatureNeverPersists` and `RemoteOutsiderSignatureNeverReplicatesAndRetentionBoundTracksAuthorizedSet` passed focused; all SecureCrdt targets passed. |
| T13-G09 — unbounded retained signature children | MITIGATED | `RemoteOutsiderSignatureNeverReplicatesAndRetentionBoundTracksAuthorizedSet` passed its signer-set shrink/retention assertions; all SecureCrdt targets passed. |

**Unresolved HIGH dispositions:** None.

## Files Created/Modified

- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-18-SUMMARY.md` — Exact focused, enumeration, JUnit, repetition, and HIGH-threat closure evidence.
- `.planning/STATE.md` — Plan position, progress, metric, and session tracking only.
- `.planning/ROADMAP.md` — Marks Plan 13-18 executed and refreshes the Phase 13 plan count/status only.

## Decisions Made

- Retained the established Phase 13 regex byte-for-byte and required independent enumeration plus JUnit assertions before accepting the five repetition results.
- Treated the named focused regressions and their full production-facing binaries as complementary evidence; neither substitutes for the other.

## Deviations from Plan

None - the successful closure run executed the plan's exact automated command without product-source or test-source changes.

## Issues Encountered

- An earlier attempt exposed a stale assertion in `InvalidAndUnderSignedCandidatesLeavePayEscrowUnchanged`, which still expected the pre-13-14 error contract for an authenticated below-quorum candidate. It was corrected outside this plan in commit `f3184b60`; the isolated test and full burn-config binary were verified before this plan restarted from the beginning. The complete recorded closure command then passed without retries or changes.
- Network-backed tests require local ephemeral listener permission in the managed environment; the exact successful chain ran with that permission.

## Known Stubs

None. This verification-only plan created no product data path or runtime implementation.

## Threat Flags

None. This plan introduced no endpoint, authentication path, file-access pattern, schema, package, dependency, or network surface. T-13-SC remains accepted: no package installation or dependency change occurred.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- CR-01, CR-02, CR-03, and CR-04 have exact passing counterexamples.
- The unchanged Phase 13 security/regression gate is machine-proven at 25 selected, 25 executed, and 25 passed with zero failure, skip, or disabled counts.
- All nine HIGH threats have passing named mitigations, and the five additional account-lifetime repetitions are green.

## Self-Check: PASSED

- This summary exists and contains the exact regex, focused commands/statuses, machine counts, five labeled repetitions, and all T13-G01 through T13-G09 mappings.
- The five required binaries built and every focused segment exited `0`.
- Enumeration, disabled scan, CTest execution, JUnit parsing, and all five repetitions exited `0` in one fail-fast chain.
- The two protected pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-13*
