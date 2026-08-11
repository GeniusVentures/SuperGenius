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
| Full phase gate | `ctest --test-dir build/OSX/Release --output-on-failure -R 'genesis_manifest_test|quorum_policy_test|trust_state_store_test|operator_approval_test|trust_genesis_tool_test|trust_restart_test|trust_tamper_e2e_test|securecrdt|trustedpeer|burnconfig|account_management|node_startup|startup|multi_account'` |
| Repetition gate | Run `policy_lifetime_multi_account_test` five consecutive times after the full phase gate |

## Sampling Contract

- Every new test target is registered in the first task that invokes it, before that task's `<verify>` command. No later CMake task is required to make an earlier command runnable.
- Task feedback uses the smallest affected target. Startup/network E2E tests run only in the tasks that implement those paths.
- Full 2–5 minute suite plus five repetitions runs only at Plan 08's final plan/phase gate, not in task-level verification.
- All ten waves run the focused tests introduced or changed in that wave before dependent waves begin.

## Per-Task Verification Map

| Task ID | Wave | Requirement / behavior | Automated command | Registration timing |
|---|---:|---|---|---|
| 13-01-01 | 1 | D-02/D-04 canonical genesis | `genesis_manifest_test` | Same task, before verify |
| 13-03-01 | 1 | D-16 instance registry | `securecrdt_registry_test` | Existing target |
| 13-03-02 | 1 | Production owner migration | focused `trustedpeerregistry|burnconfig` CTest | Existing targets |
| 13-03-03 | 1 | Cross-node isolation | registry/quorum/propose CTest | Existing targets |
| 13-12-01 | 2 | D-05..D-08 exact policy rules | `quorum_policy_test` | Same task, before verify |
| 13-02-01 | 3 | D-14/D-15 durable verified store | `trust_state_store_test` | Same task, before verify |
| 13-02-02 | 3 | Persist-before-publish/race | filtered `trust_state_store_test` | Prior task |
| 13-04-01 | 4 | Candidate codec and exact bounds | filtered `securecrdt_candidate_test` | Same task, before verify |
| 13-04-02 | 4 | Shared local/remote auth gate | filtered `securecrdt_candidate_test` | Prior task |
| 13-04-03 | 4 | Coexistence/dedup/stale isolation | candidate and race CTest | Race target registered in same task |
| 13-05-01 | 5 | Explicit TPR activation/one winner | `operator_approval_test` + race test | Operator target registered in same task |
| 13-05-02 | 5 | Burn v1 and policy-bound successors | filtered `burnconfig_policy_e2e_test` | Same task, before verify |
| 13-05-03 | 5 | Combined activation gate | focused core CTest regex | All targets already registered |
| 13-06-01 | 6 | Reusable production network composition | build `crdt_globaldb globaldb_app securecrdt` | Production source registered in same task |
| 13-09-01 | 7 | Secret-safe genesis ceremony | `trust_genesis_tool_test` | Same task, before verify |
| 13-09-02 | 7 | Local admin/CLI explicit approval | filtered `trust_genesis_tool_test` | Prior task; executable wired before verify |
| 13-07-01 | 8 | Real network genesis into fresh running node | `trust_first_boot_e2e_test` | Same task, before verify |
| 13-07-02 | 8 | Restart/tamper authority | `trust_restart_test` + `trust_tamper_e2e_test` | Same task, before verify |
| 13-07-03 | 8 | Startup integration gate | focused startup CTest | All targets already registered |
| 13-10-01 | 8 | Ceremony/runbook/rollback boundary | documentation `rg` gate | Not applicable |
| 13-08-01 | 9 | Node-scoped lifetime | `policy_lifetime_multi_account_test` | Same task, before verify |
| 13-08-02 | 9 | Real PayEscrow live burn | burn + policy-lifetime binaries | Burn target exists; multiaccount target from prior task |
| 13-11-01 | 10 | Evidence-backed metadata | metadata `rg` gate | Not applicable |

## Wave Gates

| Wave | Plans | Gate |
|---:|---|---|
| 1 | 13-01, 13-03 | `ctest ... -R 'genesis_manifest|securecrdt_registry|securecrdt_quorum|trustedpeerregistry|burnconfig'` |
| 2 | 13-12 | `ctest ... -R 'genesis_manifest|quorum_policy|trustedpeerregistry_threshold'` |
| 3 | 13-02 | `trust_state_store_test` |
| 4 | 13-04 | `ctest ... -R 'securecrdt_candidate(_race)?'` |
| 5 | 13-05 | core trust-policy CTest regex |
| 6 | 13-06 | build composition and existing consumers |
| 7 | 13-09 | `trust_genesis_tool_test` plus `sgns-trust --help` |
| 8 | 13-07, 13-10 | startup E2E/restart/tamper CTest plus runbook grep |
| 9 | 13-08 | exact full HIGH-threat phase regex plus five policy-lifetime repetitions |
| 10 | 13-11 | fast metadata/source assertion |

## Wave 0 Resolution

No separate Wave 0 plan is needed. Every missing target and production source is registered inside the first task that invokes it, and each such task lists both modified CMake files in `<files>` and `<read_first>`. This makes all 23 task commands runnable at their execution point while preserving task-local RED/GREEN flow.

## Multi-Source Coverage Audit

| Source | ID | Feature / requirement | Plan(s) | Status | Notes |
|---|---|---|---|---|---|
| GOAL | — | Manually reviewed authenticated genesis, persisted policy authority, authenticated candidates, explicit successors, and node-scoped live economics | 13-01, 13-12, 13-02..13-11 | COVERED | End-to-end outcome spans all ten waves. |
| REQ | BOOT-01 | Manual ceremony, verification, ephemeral-key handling, and example labeling | 13-09, 13-10 | COVERED | Tool behavior plus operator runbook. |
| REQ | BOOT-02 | Canonical authenticated genesis identity | 13-01, 13-09 | COVERED | Canonical codec/fingerprint consumed by ceremony. |
| REQ | BOOT-03 | Production SecureCrdt genesis and persist-before-enable | 13-02, 13-05, 13-06, 13-07, 13-09 | COVERED | Store, activation, network composition, startup, and tool path. |
| REQ | BOOT-04 | Persisted restart authority and rollback/fork/corruption safety | 13-02, 13-07, 13-10 | COVERED | Store invariants, startup behavior, and boundary documentation. |
| REQ | POLICY-01 | Versioned signed thresholds authorized by current policy | 13-12, 13-02, 13-04, 13-05, 13-07 | COVERED | Contract precedes persistence and activation. |
| REQ | VALID-01 | Complete peer and threshold bounds/floors | 13-01, 13-12 | COVERED | Manifest validation plus exact policy formulas. |
| REQ | TEST-01 | Unit, race, tamper, restart, network, economic, and account-switch proof | 13-02..13-09, 13-11, 13-12 | COVERED | Exact named tests are in the final HIGH-threat gate. |
| REQ | SCRDT-04 | CRDT-only propose/sign/quorum transport | 13-03, 13-04, 13-05, 13-06, 13-07, 13-09 | COVERED | No new RPC/topic/proposal protocol. |
| REQ | TPR-01 | Production genesis trusted-peer state | 13-01, 13-05, 13-06, 13-07, 13-08, 13-09 | COVERED | Restricted until confirmed. |
| REQ | TPR-02 | Current-set quorum membership changes | 13-12, 13-04, 13-05, 13-08, 13-09 | COVERED | Current policy exclusively authorizes successor. |
| REQ | BURN-01 | Quorum-signed BurnConfig state | 13-12, 13-04, 13-05, 13-07, 13-08, 13-09 | COVERED | Includes deterministic v1 and later explicit successors. |
| REQ | BURN-02 | Cached live burn value used by TransactionManager | 13-03, 13-05, 13-08 | COVERED | Node-scoped provider survives account selection. |
| REQ | BURN-03 | Genesis value 100 and live PayEscrow behavior | 13-01, 13-05, 13-07, 13-08 | COVERED | Fingerprint binding through real economic output. |
| REQ | MIG-05 | Preserve approved signature-verification-only scope | 13-11 | COVERED | Evidence-gated metadata only. |
| REQ | MIG-06 | Preserve ValidatorRegistry behavior/tests | 13-11 | COVERED | Completion metadata requires prior green evidence. |
| CONTEXT | D-01 | Trusted-channel manual trust boundary | 13-09, 13-10 | COVERED | No enrollment workflow added. |
| CONTEXT | D-02 | Persist canonical genesis fingerprint | 13-01, 13-02 | COVERED | Genesis identity and durable anchor are serialized. |
| CONTEXT | D-03 | One-shot bootstrap command and key cleanup | 13-06, 13-07, 13-09, 13-10 | COVERED | Real production composition and success-only cleanup. |
| CONTEXT | D-04 | Validate/canonicalize peers and display review identity | 13-01, 13-09, 13-10 | COVERED | Golden vectors and operator presentation. |
| CONTEXT | D-05 | Persist both thresholds as authoritative policy | 13-12, 13-02, 13-05 | COVERED | Split Plan 12 owns the contract. |
| CONTEXT | D-06 | Strict-majority membership floor | 13-12, 13-05 | COVERED | Exact integer boundary vectors. |
| CONTEXT | D-07 | Two-thirds burn floor | 13-12, 13-05 | COVERED | Exact integer boundary vectors. |
| CONTEXT | D-08 | Current policy authorizes exact successor | 13-12, 13-02, 13-05 | COVERED | Candidate signer set cannot self-authorize. |
| CONTEXT | D-09 | Direct candidates over existing CRDT | 13-04, 13-05, 13-06, 13-09 | COVERED | Existing topic only. |
| CONTEXT | D-10 | Only current peers introduce retained candidates | 13-04, 13-07 | COVERED | Shared local/remote pre-retention gate. |
| CONTEXT | D-11 | Explicit local exact-byte approval | 13-04, 13-05, 13-08, 13-09 | COVERED | Receive path never signs. |
| CONTEXT | D-12 | Content-addressed candidates and one winner | 13-04, 13-05 | COVERED | Bounded coexistence plus durable stale loser. |
| CONTEXT | D-13 | Restricted first boot and deterministic burn genesis | 13-05, 13-07, 13-08 | COVERED | Economics remains fail-closed until both heads persist. |
| CONTEXT | D-14 | Persisted restart authority | 13-02, 13-07, 13-10 | COVERED | JSON conflicts alert; network mismatch fails. |
| CONTEXT | D-15 | Reject rollback/fork and persist before publish | 13-02, 13-05, 13-07, 13-08, 13-10 | COVERED | Whole-disk/all-anchor limitation remains explicit. |
| CONTEXT | D-16 | Node-scoped policy lifetime | 13-03, 13-08 | COVERED | Instance registry and account-switch proof. |
| RESEARCH | — | Canonical genesis and versioned policy boundaries | 13-01, 13-12 | COVERED | Serialized to keep each plan within file budget. |
| RESEARCH | — | Signed bounded candidate approval records | 13-04, 13-05 | COVERED | Exact path/bytes/auth/caps and activation. |
| RESEARCH | — | Synchronous TrustStateStore and persist-before-publish | 13-02, 13-05, 13-07, 13-08 | COVERED | Durable head is restart/cache authority. |
| RESEARCH | — | Reusable production GlobalDB composition | 13-06, 13-07, 13-09 | COVERED | Tool and E2E share production transport. |
| RESEARCH | — | Startup state machine and node-scoped ownership | 13-03, 13-07, 13-08 | COVERED | Fresh/restart/lifetime cases included. |
| RESEARCH | — | Ephemeral secret protection and software rollback boundary | 13-02, 13-09, 13-10, 13-11 | COVERED | No overclaim about physical erase or whole-disk restore. |
| RESEARCH | — | No external package installation | 13-01..13-12 | COVERED | Every plan uses repository-resident dependencies; T-13-SC remains represented. |

Deferred bridge timing and bridge/RPC work from `13-CONTEXT.md` remain excluded and do not appear in any plan.

## Manual-Only Verifications

| Behavior | Requirement | Why manual |
|---|---|---|
| Real participant public-key collection and identity verification | D-01, D-04, BOOT-01 | Trusted human channels cannot be automated. |
| Physical/off-host ephemeral-key destruction and backup handling | D-03, T13-02 | Filesystem media, backups, and operator custody exceed process-level tests. |

## Sign-Off

- [x] All 23 actual tasks and all ten waves are mapped.
- [x] Every task has a runnable automated command at its execution point.
- [x] Full/repeated feedback is reserved for the Plan 08 gate.
- [x] Every HIGH threat names an automated verification in its plan register.
- [x] Real production genesis networking is exercised by `trust_first_boot_e2e_test`.
