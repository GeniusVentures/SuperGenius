---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
verified: 2026-08-13T11:36:17Z
status: gaps_found
score: 28/32 must-haves verified
overrides_applied: 0
gaps:
  - truth: "D-13: initial BurnConfig v1/value 100 remains reachable and must become durable before later policy successors can advance"
    status: failed
    reason: "CR-01 is confirmed: policy successors are permitted while the node is WaitingForInitialBurn, but automatic burn v1 requires policy version 1 and ordinary burn proposals require economic readiness. A policy-v2 commit before burn v1 permanently strands the node."
    artifacts:
      - path: "src/account/TrustStartupController.cpp"
        issue: "CanApproveSuccessors() returns true in WaitingForInitialBurn."
      - path: "src/account/BurnConfig.cpp"
        issue: "OnTrustedPeerGenesisConfirmed() requires policy.version == 1, while ProposeBurnCandidate() rejects until IsEconomicallyReady()."
      - path: "src/trustedpeer/TrustStateStore.cpp"
        issue: "CommitPolicySuccessor() has no durable initial-burn readiness precondition."
    missing:
      - "Enforce TPR-genesis -> peer-confirmed burn-v1 -> policy-successor ordering at the durable transition boundary."
      - "Add a regression that attempts policy v2 before burn v1 and proves the invalid state cannot be committed."
  - truth: "D-14: every cryptographically verified persisted burn head remains economically authoritative after policy-threshold changes and restart"
    status: failed
    reason: "CR-02 is confirmed: TrustStateStore validates a burn proof against the historical authorizing policy, but BurnConfig::NewProduction rechecks proof count against the current policy burn threshold. Raising that threshold makes a valid historical burn head appear unready after restart."
    artifacts:
      - path: "src/account/BurnConfig.cpp"
        issue: "Lines 166-176 compare snapshot.burn_proof.size() with snapshot.policy.burn_threshold instead of trusting the verified authorizing policy result."
      - path: "src/trustedpeer/TrustStateStore.cpp"
        issue: "LoadAndVerify correctly validates burn records using burn.authorizing_policy_hash, exposing the mismatch in the consumer."
    missing:
      - "Publish every non-bootstrap burn record that LoadAndVerify has authenticated, or return an explicit peer-confirmed authorization kind."
      - "Add restart coverage where burn v1 was authorized by policy v1 and policy v2 changes burn_threshold."
  - truth: "D-11/D-15: quorum-approved BurnConfig drives correct PayEscrow output for the full uint64_t escrow domain"
    status: failed
    reason: "CR-03 is confirmed: PayEscrow multiplies two uint64_t values before division. For UINT64_MAX at 10,000 basis points, wrapped burn is 1,844,674,407,370,954 instead of 18,446,744,073,709,551,615."
    artifacts:
      - path: "src/account/TransactionManager.cpp"
        issue: "Line 837 computes (escrow_amount * burn_basis_points) / BASIS_POINTS_TOTAL with an overflowing uint64_t intermediate."
      - path: "test/src/account/burnconfig_policy_e2e_test.cpp"
        issue: "No UINT64_MAX boundary cases for 1, 100, and 10,000 basis points."
    missing:
      - "Use an overflow-safe wide or quotient/remainder calculation."
      - "Add exact maximum-value PayEscrow burn tests."
  - truth: "Roadmap SC6: the automated gate passes with no unmitigated HIGH security finding"
    status: failed
    reason: "The independent 25/25 gate and five lifetime repetitions pass, but CR-01 through CR-04 remain present. CR-04 additionally lets arbitrary valid keypairs persist unbounded legacy signature children because membership and count bounds are checked only later during quorum evaluation."
    artifacts:
      - path: ".planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-REVIEW.md"
        issue: "Four BLOCKER findings remain unresolved."
      - path: "src/securecrdt/SecureCrdt.cpp"
        issue: "AddSignature (186-228) and FilterSecureCrdtUpdate (644-658) verify cryptographic validity but not current signer membership or a bounded signature-child count before persistence."
    missing:
      - "Resolve CR-01 through CR-04 and add targeted regressions for each."
      - "For legacy SecureCrdt signatures, reject non-members locally/remotely and bound retained signer children to the authorized set."
---

# Phase 13: Trusted-Peer Genesis, Quorum Policy, and Production Integration Verification Report

**Phase Goal:** Running nodes establish a manually reviewed, authenticated trusted-peer genesis; persist and enforce versioned quorum policy as the restart authority; accept only current-peer-authenticated content-addressed CRDT candidates; apply explicit quorum-approved membership and burn successors; and preserve policy services and live burn behavior across account selection.

**Verified:** 2026-08-13T11:36:17Z  
**Status:** gaps_found  
**Re-verification:** No — initial verification

## Goal Achievement

The principal architecture exists, is substantive, is wired into production, and its declared 25-test gate passes. The phase still does **not** achieve its goal because four code-review BLOCKERs are independently confirmed in the live source. Passing tests do not override observable counterexamples in production code.

### Observable Truths

Roadmap criteria 1-5 are merged with the matching plan truths below; roadmap criterion 6 is retained as a separate, non-duplicative security truth. This yields 32 truths: all 31 plan truths plus the security-closure criterion.

| # | Source | Truth | Status | Code/executable evidence |
|---:|---|---|---|---|
| 1 | 13-01 | Canonical genesis identity is deterministic across peer order and rejects malformed/tampered input. | VERIFIED | `GenesisManifest.cpp:11-150`, `SGNS_TRUST_GENESIS_V1`, 256-peer cap; `genesis_manifest_test` 8/8 PASS. |
| 2 | 13-02 | Persisted genesis fingerprint and policy/burn heads are restart authority. | VERIFIED | `TrustStateStore::LoadAndVerify` reconstructs and verifies network-scoped chains; store test 9/9 PASS. The separate readiness projection defect is Truth 19. |
| 3 | 13-02 | Missing/corrupt/forked/failed candidates do not erase the durable LKG snapshot. | VERIFIED | Typed errors and head checks in `TrustStateStore.cpp:422-637,727-879`; store failure/race tests PASS. |
| 4 | 13-02 | Activation persists synchronously before in-memory publication. | VERIFIED | Store commits then reloads; TPR publishes at `TrustedPeerRegistry.cpp:548-554`, Burn at `BurnConfig.cpp:355-358`. |
| 5 | 13-03 | Every SecureCrdt owns an independent policy registry. | VERIFIED | Ordinary per-instance maps/mutexes in `SecureCrdtRegistry.hpp`; `SecureCrdt::Registry()` at `SecureCrdt.cpp:139-147`; isolation tests PASS. |
| 6 | 13-03 | Legacy SecureCrdt behavior remains operational and production owners use their node registry. | VERIFIED | TPR/Burn call `secure_crdt_->Registry()`; related SecureCrdt/TPR/Burn tests PASS. CR-04 is a security flaw in that retained legacy path, captured by Truth 32. |
| 7 | 13-04 | Proposal/approval is explicit; receipt/relay does not sign. | VERIFIED | Candidate callbacks carry records only; explicit `SubmitLocalApproval` paths in TPR/Burn; operator tests PASS. |
| 8 | 13-04 | Candidate ingress rejects malformed, oversized, wrong-context, and non-current-peer records before retention/notification. | VERIFIED | Shared `ValidateCandidateApproval` at `SecureCrdt.cpp:450-536` checks context, signer membership, signature, and caps before `Put`; candidate tests PASS. |
| 9 | 13-04 | Content/version/hash keys isolate candidate approvals. | VERIFIED | `CandidateKey`/`CandidateId` canonical codecs; race and cross-candidate tests PASS. |
| 10 | 13-04 | Candidate resources are bounded to 64 KiB, 32 candidates, 256 approvals, and 64 MiB. | VERIFIED | Exact constants in `SecureCrdtCandidate.hpp:28-31`; enforcement at `SecureCrdt.cpp:498-533`; boundary tests PASS. |
| 11 | 13-05 | TPR/Burn activation uses the durable current policy and policy-specific threshold. | VERIFIED | Activation reloads snapshots and store verifies proof with current policy thresholds at commit. |
| 12 | 13-05 | Production receipt does not auto-sign; non-genesis propose/approve is explicit. | VERIFIED | `Propose*`/`Approve*` are signing call paths; receive-only tests PASS. |
| 13 | 13-05 | Durable predecessor comparison permits one concurrent successor; stale losers do not overwrite cache. | VERIFIED | One store transition mutex and head comparison; `securecrdt_candidate_race_test` PASS. |
| 14 | 13-05 / Roadmap SC4 | Initial peers can complete deterministic burn v1/value 100 before later successors. | **FAILED** | CR-01: policy v2 may commit first, after which `OnTrustedPeerGenesisConfirmed` refuses burn v1 and ordinary burn proposals remain disabled. |
| 15 | 13-06 | Local trust tools reuse the production GlobalDB/CRDT transport without GeniusNode or a new topic. | VERIFIED | `GlobalDbNetworkComposition`, `sgns-trust`, and real first-boot cross-composition test; no account/private-key config in composition. |
| 16 | 13-06 | Composition owns and stops networking resources in dependency order. | VERIFIED (warning) | Substantive `Start`/`Stop` implementation; WR-03 remains a self-join edge-case warning. |
| 17 | 13-07 | Fresh startup networks in WaitingForTrustGenesis without active peers or economics. | VERIFIED | `TrustStartupController::Refresh` and first-boot E2E PASS. |
| 18 | 13-07 | Real tool/production transport reaches durable TPR, burn-v1 readiness, and success-only cleanup. | VERIFIED (warning) | First-boot E2E PASS; WR-02 notes pathname replacement can defeat actual-key cleanup. |
| 19 | 13-07 / Roadmap SC2 | A valid persisted snapshot is restart authority; trust JSON conflicts are diagnostic, network mismatch is fatal, and verified economic authority survives policy evolution. | **FAILED** | Config/network behavior passes at `TrustStartupController.cpp:58-94`, but CR-02 lets `BurnConfig::NewProduction` apply the current burn threshold to a historical proof and demote a verified burn head to unready. |
| 20 | 13-07 | Missing/older/fork replicated data alerts without replacing LKG. | VERIFIED | `ObserveReplicatedSnapshot` emits missing/rollback/fork codes without store mutation; tamper E2E PASS. |
| 21 | 13-08 / Roadmap SC5 | SelectAccount preserves policy objects, stores, registrations, caches, and callbacks. | VERIFIED | `SelectAccount` only calls account-bound teardown; policy teardown is separate; lifetime test and five repetitions PASS. |
| 22 | 13-08 | Replacement TransactionManager consumes the same confirmed provider and fails closed pre-ready. | VERIFIED | Provider injection at `GeniusNode.cpp:943-952`; checks at `TransactionManager.cpp:791-797`; lifetime tests PASS. |
| 23 | 13-08 / Roadmap SC4 | Confirmed burn successors change correct real PayEscrow output; stale/failed candidates do not. | **FAILED** | CR-03: `TransactionManager.cpp:837` overflows on large valid escrow amounts. Existing tests cover ordinary values only. |
| 24 | 13-09 / Roadmap SC1 | `sgns-trust genesis` canonicalizes/reviews, uses protected ephemeral signing, confirms via SecureCrdt, then cleanses/unlinks. | VERIFIED (warning) | Ceremony/tool tests 10/10 PASS; built help lists five local operations. WR-02 requires inode-stable cleanup hardening. |
| 25 | 13-09 | Local list/propose/approve operations are explicit and add no remote administration surface. | VERIFIED | `LocalTrustAdmin` and CLI wiring; no HTTP/RPC/new-topic implementation. WR-01 concerns swallowed activation failures. |
| 26 | 13-10 | Operators have an exact trusted-channel/fingerprint ceremony and non-production examples. | VERIFIED | `docs/trusted-peer-genesis.md` and example notice contain trusted-channel, fingerprint, 0600/stdin, non-production/non-authoritative instructions. |
| 27 | 13-10 | Runbook explains persisted authority, alerts, and whole-disk rollback boundary. | VERIFIED | Exact conflict/mismatch alerts and TPM/OS-keystore/off-host boundary at runbook lines 290-316. |
| 28 | 13-11 | Canonical milestone metadata changes only after the full/repeated green gate is recorded. | VERIFIED | `13-08-SUMMARY.md` records the exact 25/25 gate and five repetitions before `REQUIREMENTS.md` reconciliation. |
| 29 | 13-11 | MIG-05 retains approved signature-verification-only scope and whole-disk rollback is not claimed solved. | VERIFIED | `REQUIREMENTS.md`, `PROJECT.md`, and Phase 12 artifacts preserve both constraints. |
| 30 | 13-12 | Confirmed policy contains both thresholds and enforces bounds, strict majority, and two-thirds floors. | VERIFIED | Exact integer helpers and `quorum_policy_test` boundary matrix 8/8 PASS. |
| 31 | 13-12 | Policy successors require current.version+1, current hash linkage, and current-policy authorization. | VERIFIED | `ValidatePolicySuccessor` and self-authorization regressions PASS. |
| 32 | Roadmap SC6 / 13-11 evidence gate | Automated tests pass with no unmitigated HIGH security finding. | **FAILED** | 25/25 and five repetitions PASS, but CR-01..CR-04 remain present and `.planning/config.json` blocks on HIGH. |

**Score:** 28/32 truths verified.

### Required Artifacts

`gsd-sdk query verify.artifacts` reports 23/23 plan-declared artifacts present and substantive. Manual Level 3 and behavior review produced the following functional classification.

| Artifact group | Expected | Status | Details |
|---|---|---|---|
| `CanonicalTrustCodec.*`, `GenesisManifest.*`, `QuorumPolicy.*` | Canonical identity, exact thresholds, successor validation | VERIFIED | Substantive implementations, CMake-wired, 16 focused tests PASS. |
| `TrustStateStore.*` | Synchronous verified genesis/policy/burn records and heads | VERIFIED | 908-line implementation; atomic batch/head writes, full-chain replay, 9 tests PASS. |
| `SecureCrdtRegistry.hpp`, `SecureCrdtCandidate.*`, `SecureCrdt.*` | Instance registry and authenticated bounded candidate transport | VERIFIED for candidate path; **SECURITY DEFECT** in legacy path | Candidate path checks current signers and caps. Legacy `AddSignature`/remote signature filter has CR-04. |
| `TrustedPeerRegistry.*`, `BurnConfig.*` | Production genesis/successor services and confirmed caches | **DEFECTIVE** | Wired and heavily tested, but CR-01 and CR-02 invalidate reachable sequencing/restart behavior. |
| `TrustStartupController.*`, `GeniusNode.*` | Fresh/restart state machine and node lifetime | **DEFECTIVE** | Production-wired; CR-01 permits successor approval in WaitingForInitialBurn and CR-02 can hold a valid restart unready. |
| `TransactionManager.*` | Fail-closed confirmed-burn PayEscrow | **DEFECTIVE** | Provider is wired, but CR-03 produces incorrect financial output for large valid escrows. |
| `GlobalDbNetworkComposition.*` | Reusable production transport | VERIFIED (warning) | Built/wired; WR-03 self-join edge case remains. |
| `GenesisCeremony.*`, `LocalTrustAdmin.*`, `main.cpp` | One-shot ceremony and local admin | VERIFIED (warnings) | Built CLI and 10 tests PASS; WR-01/WR-02 remain. |
| Runbook/config/metadata | Operator and traceability contract | VERIFIED | Docs submodule pinned at `2fa3896`; requirement and boundary text present. |
| Declared test artifacts | Behavioral proof | PARTIAL | All are wired and pass, but no tests detect CR-01..CR-04. |

### Key Link Verification

All 20 plan-declared key links were found by `gsd-sdk query verify.key-links`; manual review confirms the calls are real rather than import-only.

| From | To | Status | Details |
|---|---|---|---|
| `GenesisManifest.cpp` | canonical codec/hash | WIRED | Canonical bytes feed SHA-256 fingerprint. |
| `TrustStateStore.cpp` | RocksDB batch + canonical decoders | WIRED | Atomic record/head batches, then verified reload. |
| TPR/Burn | owning `SecureCrdt::Registry()` | WIRED | Instance-local registration/unregistration. |
| `SecureCrdt.cpp` | authorization snapshot + multisig | WIRED | Candidate validation uses live policy and exact core signature. |
| TPR/Burn activation | `TrustStateStore` | WIRED, behavior gaps | Commit-before-publish is present; CR-01/CR-02 affect policy sequencing/readiness. |
| `GlobalDbNetworkComposition` | production GlobalDB | WIRED | Existing topic and production `GlobalDB::New`. |
| GeniusNode/controller | store, SecureCrdt, TransactionManager | WIRED, behavior gaps | Startup/lifetime integration is live; CR-01..CR-03 remain. |
| Genesis tool | TPR + production composition | WIRED | Ceremony submits reviewed approval and waits for durable confirmation. |
| Runbook | actual CLI | WIRED | Commands match built `sgns-trust --help`. |
| Quorum policy | codec + exact floor helpers | WIRED | Both policy-specific validators are production call sites. |

### Data-Flow Trace (Level 4)

| Artifact | Data | Source and path | Produces real data | Status |
|---|---|---|---|---|
| Genesis ceremony | Reviewed manifest/fingerprint/signature | CLI input -> canonical manifest -> exact candidate -> SecureCrdt -> TPR -> store | Yes | FLOWING, WR-02 cleanup warning |
| Candidate inbox | Approval records | local/remote CRDT -> shared validator -> GlobalDB -> callback | Yes, authenticated and bounded | FLOWING |
| TPR policy cache | Confirmed policy snapshot | approval quorum -> store commit/reload -> `PublishSnapshot` | Yes | FLOWING, CR-01 sequencing gap |
| Burn confirmed provider | Burn record/proof | store load/activation -> `PublishConfirmedBurn` -> atomic provider | Yes | **HOLLOW ON SOME RESTARTS** due CR-02 |
| PayEscrow | basis points | retained provider -> `TransactionManager::PayEscrow` | Yes | **INCORRECT AT LARGE VALUES** due CR-03 |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Canonical genesis | `genesis_manifest_test --gtest_brief=1` | 8/8 PASS | PASS |
| Exact policy | `quorum_policy_test --gtest_brief=1` | 8/8 PASS | PASS |
| Durable state | `trust_state_store_test --gtest_brief=1` | 9/9 PASS | PASS |
| Full named phase gate | Exact `13-VALIDATION.md` CTest regex | 25/25 PASS in 265.22s | PASS, insufficient coverage |
| Repeated account lifetime | `policy_lifetime_multi_account_test` x5 | 5/5 PASS | PASS |
| CLI surface | `sgns-trust --help` | Exactly genesis/list/propose-policy/propose-burn/approve | PASS |
| Escrow overflow counterexample | BigInt reproduction of line 837 at UINT64_MAX and 10,000 bp | Wrapped burn 1,844,674,407,370,954 vs correct 18,446,744,073,709,551,615 | **FAIL** |

### Probe Execution

No probe scripts or probe-based criteria are declared for this phase. Step 7c is not applicable.

### Requirements Coverage

All 15 requirement IDs in the roadmap appear in at least one plan. No Phase 13 requirement is orphaned.

| Requirement | Status | Evidence / blocker |
|---|---|---|
| BOOT-01 | SATISFIED | Runbook and protected ceremony exist and are tested. |
| BOOT-02 | SATISFIED | Canonical manifest binds all required fields and fingerprint. |
| BOOT-03 | SATISFIED | Production first-boot E2E confirms and persists TPR before economic readiness. |
| BOOT-04 | **BLOCKED** | Store rollback/fork behavior works, but CR-02 breaks valid persisted burn readiness after policy evolution. |
| POLICY-01 | SATISFIED | Versioned policy successor is signed/authorized by current durable policy. |
| VALID-01 | SATISFIED | Peer bounds and exact membership/burn floors are implemented/tested. |
| TEST-01 | **BLOCKED** | Declared gate passes but lacks regressions for all four confirmed blockers. |
| SCRDT-04 | SATISFIED | Candidate/propose/sign/quorum uses CRDT only; no new RPC/transport. CR-04 still blocks security closure. |
| TPR-01 | SATISFIED | Reviewed authenticated production genesis is implemented. |
| TPR-02 | SATISFIED | Current-set configurable N-of-M policy successors are implemented. |
| BURN-01 | SATISFIED | Burn basis points are stored/updated as quorum-signed CRDT policy data. |
| BURN-02 | **BLOCKED** | Node-scoped provider survives account selection, but CR-02 can publish it as unready after a valid restart. |
| BURN-03 | **BLOCKED** | Normal default/value tests pass, but CR-01 can strand initial readiness and CR-03 violates correct default burn behavior for large valid escrow amounts. |
| MIG-05 | SATISFIED | Approved signature-verification-only scope is preserved; Phase 12 verification confirms the exact callee/link. |
| MIG-06 | SATISFIED | Existing ValidatorRegistry behavior remains green under the approved Phase 12 scope. |

**Requirement accounting:** 11 satisfied, 4 blocked.

### Code Review and Anti-Patterns

| Finding | Classification | Verification result | Impact |
|---|---|---|---|
| CR-01 | BLOCKER | CONFIRMED | Permanent economic-startup deadlock is reachable. |
| CR-02 | BLOCKER | CONFIRMED | Valid historical burn authority is lost on restart after threshold increase. |
| CR-03 | BLOCKER | CONFIRMED | Large escrows are materially under-burned. |
| CR-04 | BLOCKER | CONFIRMED | Outsider legacy signatures can consume unbounded replicated storage. |
| WR-01 | WARNING | CONFIRMED | CLI/startup callbacks cast activation errors to void. |
| WR-02 | WARNING | CONFIRMED | Pathname replacement can unlink the wrong file after ceremony confirmation. |
| WR-03 | WARNING | CONFIRMED | `Stop()` can attempt to join its own I/O thread. |
| WR-04 | WARNING | CONFIRMED | Partial fixture setup can cause teardown null dereference. |
| WR-05 | WARNING | CONFIRMED | Partial filter registration is not rolled back. |
| Phase-file debt scan | WARNING | No unreferenced `TBD`/`FIXME`/`XXX`; several pre-existing TODOs exist in broad modified files | No debt-marker gate beyond the confirmed findings. |

Disconfirmation pass:

- Partially met requirement: BURN-02 has correct node lifetime but incorrect restart readiness under historical authorization.
- Misleading passing test: `burnconfig_policy_e2e_test` proves normal-value economics but does not exercise overflowing escrow amounts or burn-proof authorization after a policy threshold change.
- Uncovered error path: policy-v2 activation while burn v1 is still bootstrap-only has no regression and creates an unrecoverable normal-API state.

### Human Verification Required

None. The blocking gaps are directly observable in source and arithmetic/state-machine counterexamples. Visual, external-service, or subjective UAT is not required to decide this phase verdict.

### Deferred Items

None. `roadmap.analyze` reports no later phase in the v1.1 milestone; all four blockers belong to Phase 13 and cannot be deferred.

### Gaps Summary

The phase built the intended trust architecture and most positive-path behavior, but it cannot close v1.1 yet. The existing test suite is green while omitting four security/correctness counterexamples. Per `.planning/config.json`, security enforcement is enabled and blocks on HIGH; roadmap criterion 6 also explicitly requires no unmitigated HIGH finding. CR-01 through CR-04 therefore prevent a pass independently of the 25/25 test result.

---

_Verified: 2026-08-13T11:36:17Z_  
_Verifier: the agent (gsd-verifier)_
