---
phase: 13
slug: close-v1-1-trusted-peer-genesis-quorum-policy-and-production
status: ready
nyquist_compliant: true
wave_0_complete: true
created: 2026-08-11
revised: 2026-08-11
---

# Phase 13 — Validation Strategy

## Test Infrastructure

| Property | Value |
|----------|-------|
| Framework | GoogleTest via repository `addtest(...)`; CTest 3.31.4 |
| Quick command | `ctest --test-dir build/OSX/Release --output-on-failure -R 'securecrdt_candidate|genesis_manifest|quorum_policy|trust_state_store|trustedpeerregistry|burnconfig'` |
| Full phase gate | `ctest --test-dir build/OSX/Release --output-on-failure -R 'securecrdt|trustedpeer|burnconfig|account_management|node_startup|startup|multi_account'` |
| Repetition gate | Run `policy_lifetime_multi_account_test` five consecutive times after the full phase gate |

## Sampling Contract

- Every new test target is registered in the first task that invokes it, before that task's `<verify>` command. No later CMake task is required to make an earlier command runnable.
- Task feedback uses the smallest affected target. Startup/network E2E tests run only in the tasks that implement those paths.
- Full 2–5 minute suite plus five repetitions runs only at Plan 08's final plan/phase gate, not in task-level verification.
- All nine waves run the focused tests introduced or changed in that wave before dependent waves begin.

## Per-Task Verification Map

| Task ID | Wave | Requirement / behavior | Automated command | Registration timing |
|---|---:|---|---|---|
| 13-01-01 | 1 | D-02/D-04 canonical genesis | `genesis_manifest_test` | Same task, before verify |
| 13-01-02 | 1 | D-05..D-08 exact policy rules | `quorum_policy_test` | Same task, before verify |
| 13-03-01 | 1 | D-16 instance registry | `securecrdt_registry_test` | Existing target |
| 13-03-02 | 1 | Production owner migration | focused `trustedpeerregistry|burnconfig` CTest | Existing targets |
| 13-03-03 | 1 | Cross-node isolation | registry/quorum/propose CTest | Existing targets |
| 13-02-01 | 2 | D-14/D-15 durable verified store | `trust_state_store_test` | Same task, before verify |
| 13-02-02 | 2 | Persist-before-publish/race | filtered `trust_state_store_test` | Prior task |
| 13-04-01 | 3 | Candidate codec and exact bounds | filtered `securecrdt_candidate_test` | Same task, before verify |
| 13-04-02 | 3 | Shared local/remote auth gate | filtered `securecrdt_candidate_test` | Prior task |
| 13-04-03 | 3 | Coexistence/dedup/stale isolation | candidate and race CTest | Race target registered in same task |
| 13-05-01 | 4 | Explicit TPR activation/one winner | `operator_approval_test` + race test | Operator target registered in same task |
| 13-05-02 | 4 | Burn v1 and policy-bound successors | filtered `burnconfig_policy_e2e_test` | Same task, before verify |
| 13-05-03 | 4 | Combined activation gate | focused core CTest regex | All targets already registered |
| 13-06-01 | 5 | Reusable production network composition | build `crdt_globaldb globaldb_app securecrdt` | Production source registered in same task |
| 13-09-01 | 6 | Secret-safe genesis ceremony | `trust_genesis_tool_test` | Same task, before verify |
| 13-09-02 | 6 | Local admin/CLI explicit approval | filtered `trust_genesis_tool_test` | Prior task; executable wired before verify |
| 13-07-01 | 7 | Real network genesis into fresh running node | `trust_first_boot_e2e_test` | Same task, before verify |
| 13-07-02 | 7 | Restart/tamper authority | `trust_restart_test` + `trust_tamper_e2e_test` | Same task, before verify |
| 13-07-03 | 7 | Startup integration gate | focused startup CTest | All targets already registered |
| 13-10-01 | 7 | Ceremony/runbook/rollback boundary | documentation `rg` gate | Not applicable |
| 13-08-01 | 8 | Node-scoped lifetime | `policy_lifetime_multi_account_test` | Same task, before verify |
| 13-08-02 | 8 | Real PayEscrow live burn | burn + policy-lifetime binaries | Burn target exists; multiaccount target from prior task |
| 13-11-01 | 9 | Evidence-backed metadata | metadata `rg` gate | Not applicable |

## Wave Gates

| Wave | Plans | Gate |
|---:|---|---|
| 1 | 13-01, 13-03 | `ctest ... -R 'genesis_manifest|quorum_policy|securecrdt_registry|securecrdt_quorum|trustedpeerregistry|burnconfig'` |
| 2 | 13-02 | `trust_state_store_test` |
| 3 | 13-04 | `ctest ... -R 'securecrdt_candidate(_race)?'` |
| 4 | 13-05 | core trust-policy CTest regex |
| 5 | 13-06 | build composition and existing consumers |
| 6 | 13-09 | `trust_genesis_tool_test` plus `sgns-trust --help` |
| 7 | 13-07, 13-10 | startup E2E/restart/tamper CTest plus runbook grep |
| 8 | 13-08 | full phase regex plus five policy-lifetime repetitions |
| 9 | 13-11 | fast metadata/source assertion |

## Wave 0 Resolution

No separate Wave 0 plan is needed. Every missing target and production source is registered inside the first task that invokes it, and each such task lists both modified CMake files in `<files>` and `<read_first>`. This makes all 23 task commands runnable at their execution point while preserving task-local RED/GREEN flow.

## Manual-Only Verifications

| Behavior | Requirement | Why manual |
|---|---|---|
| Real participant public-key collection and identity verification | D-01, D-04, BOOT-01 | Trusted human channels cannot be automated. |
| Physical/off-host ephemeral-key destruction and backup handling | D-03, T13-02 | Filesystem media, backups, and operator custody exceed process-level tests. |

## Sign-Off

- [x] All 23 actual tasks and all nine waves are mapped.
- [x] Every task has a runnable automated command at its execution point.
- [x] Full/repeated feedback is reserved for the Plan 08 gate.
- [x] Every HIGH threat names an automated verification in its plan register.
- [x] Real production genesis networking is exercised by `trust_first_boot_e2e_test`.
