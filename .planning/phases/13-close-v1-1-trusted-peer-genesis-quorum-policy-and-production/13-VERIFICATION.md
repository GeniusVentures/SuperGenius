---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
verified: 2026-08-13T20:29:40Z
status: gaps_found
score: 29/32 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 28/32
  gaps_closed:
    - "CR-05: production startup now initiates and restart-recovers deterministic initial burn."
    - "CR-06: live replicated policy callbacks now activate quorum on passive receivers."
    - "CR-07: LoadAndVerify now holds one transition view across policy and burn verification."
    - "WR-06: Refresh now returns and emits actionable genesis/policy/burn activation failures."
  gaps_remaining:
    - "CR-08: burn-ready Refresh returns before passive burn-successor discovery/activation."
    - "CR-09: controller reconstruction never rediscovers retained policy candidates."
    - "CR-10: persisted-ready startup can re-enter INITIALIZING_TRANSACTIONS and start two managers."
  regressions:
    - "WR-07: release fixtures replace historical-storage restart with fresh storage and exercise local rather than passive burn activation."
gaps:
  - truth: "D-05/D-08/D-12 and Roadmap SC3/SC4: every receiving production node durably applies quorum-confirmed policy and burn successors"
    status: failed
    reason: "CR-08: Refresh returns immediately whenever the current burn is ready, so all later passive burn candidates are unreachable."
    artifacts:
      - path: "src/account/TrustStartupController.cpp"
        issue: "Lines 321-325 return before burn discovery/activation at lines 327-442."
      - path: "test/src/multiaccount/policy_lifetime_multi_account_test.cpp"
        issue: "Lines 285-290 propose burn locally on the observed node and bypass passive receipt."
    missing:
      - "Process retained/callback burn successors before the ready-state return without auto-signing them."
      - "Add a three-peer regression where a passive receiver commits burn v2 after two other explicit approvals."
  - truth: "D-05/D-08/D-12 and Roadmap SC2/SC3: retained quorum policy candidates remain restart-discoverable and converge without another write"
    status: failed
    reason: "CR-09: Refresh consumes only an in-memory callback queue; production never calls ListPendingPolicyCandidates after controller reconstruction."
    artifacts:
      - path: "src/account/TrustStartupController.cpp"
        issue: "Lines 260-319 process only pending_burn_candidates_; no persistent policy listing is merged."
      - path: "src/trustedpeer/TrustedPeerRegistry.cpp"
        issue: "ListPendingPolicyCandidates exists at lines 459-472 but its only src caller is LocalTrustAdmin."
      - path: "test/src/startup/trust_first_boot_e2e_test.cpp"
        issue: "Lines 877-957 destroy the failed passive controller but never reconstruct it and retry the retained failed policy quorum."
    missing:
      - "List, merge, deduplicate, and deterministically retry pending policy candidates during startup/Refresh."
      - "Add a commit-failure then controller-reconstruction test with no new registry write or admin action."
  - truth: "D-14 and Roadmap SC2: a valid persisted-ready restart initializes one stable TransactionManager and preserves its callbacks"
    status: failed
    reason: "CR-10: synchronous ConfirmedReady during TrustStartupController::New posts a same-state transaction initialization while the original transition is still executing."
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "Lines 630-635 have no transition re-entry guard; lines 843-862 post INITIALIZING_TRANSACTIONS; lines 946-966 replace/start a manager on each entry."
      - path: "src/account/TrustStartupController.cpp"
        issue: "Line 180 synchronously Refreshes; lines 321-324 emit ConfirmedReady for persisted-ready state."
      - path: "src/account/TransactionManager.cpp"
        issue: "Constructor path registers shared callbacks at lines 157-275; the per-instance StartCore guard at lines 398-418 cannot prevent two instances, and displaced destruction unregisters shared callbacks at lines 306-340."
      - path: "test/src/multiaccount/multi_account_sync.cpp"
        issue: "Lines 389-405 now construct a fresh-storage client instead of reopening the historical store that exercised the lifecycle."
    missing:
      - "Serialize/idempotently gate GeniusNode state transitions and suppress ConfirmedReady re-entry while INITIALIZING_TRANSACTIONS is in progress."
      - "Stop the existing manager before any intentional replacement and owner-scope callback teardown."
      - "Restore a persisted trust plus historical transaction database restart test asserting one manager construction/start and stable callbacks; run under ASan/TSan."
  - truth: "Roadmap SC6: the automated gate passes with no unmitigated HIGH security finding"
    status: failed
    reason: "The exact gate is green, but CR-08, CR-09, and CR-10 are reachable unmitigated HIGH production defects and security_block_on is high."
    artifacts:
      - path: ".planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-REVIEW.md"
        issue: "Current deep review records three critical findings and WR-07."
      - path: ".planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-22-SUMMARY.md"
        issue: "The exact five focused cases, 25-target gate, and lifetime repetition omit the three reachable paths."
    missing:
      - "Close CR-08, CR-09, CR-10 and WR-07 with production-facing regressions, then rerun the exact five-case, 25/25, and five-repeat gate."
---

# Phase 13: Trusted-Peer Genesis, Quorum Policy, and Production Integration Verification Report

**Phase Goal:** Running nodes establish a manually reviewed, authenticated trusted-peer genesis; persist and enforce versioned quorum policy as the restart authority; accept only current-peer-authenticated content-addressed CRDT candidates; apply explicit quorum-approved membership and burn successors; and preserve policy services and live burn behavior across account selection.

**Verified:** 2026-08-13T20:29:40Z
**HEAD:** `63fa3afe083d0ceabdc880d68a904c3e2040d8f8`
**Status:** gaps_found  
**Re-verification:** Yes — after Plans 13-19 through 13-22

## Goal Achievement

The prior CR-05, CR-06, CR-07, and WR-06 implementations are substantive. Independent source inspection and a fresh 2/2 coherent-store run confirm their narrow closures. Phase 13 still fails goal-backward verification because successor convergence and persisted-ready startup have three reachable holes not exercised by the green release gate.

The score retains the original 32-truth contract: 31 non-duplicative truths from Plans 13-01 through 13-12 plus Roadmap SC6. Gap-plan truths refine those obligations rather than inflating the denominator.

### Observable Truths

| # | Source | Truth | Status | Current code/executable evidence |
|---:|---|---|---|---|
| 1 | 13-01 | Canonical genesis identity is deterministic across peer order and rejects malformed/tampered input. | VERIFIED | Canonical codec/manifest are substantive and wired; gate includes `genesis_manifest_test`. |
| 2 | 13-02 / SC2 | Persisted genesis, policy, and burn heads form one coherent durable authority. | VERIFIED | CR-07 closed: public `LoadAndVerify()` locks `transition_mutex_` for `LoadAndVerifyUnlocked()` at `TrustStateStore.cpp:442-445`; independent exact cases passed 2/2. |
| 3 | 13-02 | Missing/corrupt/forked/failed candidates never erase durable LKG. | VERIFIED | Typed load/commit paths and restart/tamper binaries are present and green. |
| 4 | 13-02 | Activation persists synchronously before cache publication. | VERIFIED | Store commits and reloads before TPR/Burn publication; failure paths remain explicit. |
| 5 | 13-03 | Every SecureCrdt owns an independent policy registry. | VERIFIED | Instance registry and owner-token teardown are production wired. |
| 6 | 13-03 | Legacy behavior remains operational and production owners use their node registry. | VERIFIED | TPR/Burn resolve through `secure_crdt_->Registry()`; current-member legacy bounds remain implemented. |
| 7 | 13-04 | Proposal/approval is explicit; receipt/relay never signs. | VERIFIED | Signing remains confined to explicit propose/approve and exact initial-burn bootstrap behavior. |
| 8 | 13-04 | Candidate ingress rejects malformed, oversized, wrong-context, and non-current-peer records before retention/notification. | VERIFIED | Shared validation, exact key binding, signer membership, and caps are substantive. |
| 9 | 13-04 | Content/version/hash keys isolate candidate approvals. | VERIFIED | Canonical candidate IDs and per-candidate approval reads are wired. |
| 10 | 13-04 | Candidate resources are bounded to 64 KiB, 32 candidates, 256 approvals, and 64 MiB. | VERIFIED | Exact constants/enforcement remain in candidate code and tests. |
| 11 | 13-05 / SC3 / SC4 | Current-policy quorum activates the durable TPR/Burn winner on every receiving production node. | **FAILED** | CR-08 makes burn successors unreachable after readiness (`TrustStartupController.cpp:321-325` before `:327-442`); CR-09 loses retained policy quorum across reconstruction because production never calls `ListPendingPolicyCandidates()`. |
| 12 | 13-05 | Non-genesis propose/approve is explicit and receipt does not auto-sign. | VERIFIED | Passive callback calls activation only; local admin owns signing. |
| 13 | 13-05 | Durable predecessor comparison permits one concurrent successor and stale losers do not replace cache. | VERIFIED | Transition mutex, exact durable head checks, and race coverage remain intact. |
| 14 | 13-05 / SC4 | After durable TPR genesis, production peers automatically progress deterministic burn v1/value 100. | VERIFIED | CR-05 closed at `TrustStartupController.cpp:328-398`; restart discovers initial-burn candidates and focused production case passed. |
| 15 | 13-06 | Trust tools reuse production GlobalDB/CRDT without GeniusNode or a new topic. | VERIFIED | Composition and CLI use the existing production topic. |
| 16 | 13-06 | Composition owns/stops networking resources in dependency-safe order. | VERIFIED | Start/Stop ownership is substantive and integrated. |
| 17 | 13-07 | Fresh startup networks while waiting with no active peers/economics. | VERIFIED | Explicit waiting states and fail-closed readiness remain wired. |
| 18 | 13-07 | Real ceremony transport reaches durable TPR and success-only key cleanup. | VERIFIED | Production composition and ceremony service are substantive. |
| 19 | 13-07 / SC2 | Valid persisted state is stable restart authority; conflicts alert and network mismatch/corruption fails closed. | **FAILED** | Trust bytes verify correctly, but CR-10 makes persisted-ready startup re-enter transaction initialization and construct/start two managers; displaced teardown can remove callbacks required by the replacement. |
| 20 | 13-07 | Missing/older/fork replicated data alerts without replacing LKG. | VERIFIED | `ObserveReplicatedSnapshot` retains durable authority and emits typed events. |
| 21 | 13-08 / SC5 | SelectAccount preserves policy objects, stores, registrations, caches, and callbacks. | VERIFIED | Account-only teardown retains node policy objects; address/provider identity assertions exist. WR-07 weakens coverage but the source link is intact. |
| 22 | 13-08 | Replacement TransactionManager consumes the same provider and fails closed pre-ready. | VERIFIED | Provider injection and `TRUST_POLICY_NOT_READY` are wired; CR-10 is a startup re-entry defect, counted in Truth 19. |
| 23 | 13-08 / SC4 | Approved Burn successors change real PayEscrow output; stale/failed candidates do not. | VERIFIED | Explicit local successor path and exact arithmetic work; passive receiving-node convergence is counted in Truth 11. |
| 24 | 13-09 / SC1 | `sgns-trust genesis` reviews, signs ephemerally, confirms via SecureCrdt, and cleans up only after durable success. | VERIFIED | Ceremony implementation, local CLI, and key-handling tests are substantive. |
| 25 | 13-09 | Local list/propose/approve is explicit with no remote admin surface. | VERIFIED | LocalTrustAdmin/CLI only; no RPC or new topic. |
| 26 | 13-10 | Operators have an exact trusted-channel/fingerprint ceremony and non-production examples. | VERIFIED | Runbook and example labels exist at the pinned docs revision. |
| 27 | 13-10 | Runbook explains persisted authority, alerts, and whole-disk rollback boundary. | VERIFIED | Required alerts and accepted external-anchor limitation are explicit. |
| 28 | 13-11 | Canonical metadata changed only after recorded green evidence. | VERIFIED | Metadata ordering exists, although the completion claims are now stale because later source review found blockers. |
| 29 | 13-11 | MIG-05 narrow scope and whole-disk limitation remain intact. | VERIFIED | REQUIREMENTS/PROJECT preserve both boundaries. |
| 30 | 13-12 | Policy contains both thresholds and enforces exact bounds/floors. | VERIFIED | Exact integer validators and boundary tests remain wired. |
| 31 | 13-12 | Policy successors require version+1, current hash linkage, and current-policy authorization. | VERIFIED | Structural and durable proof validation are implemented. |
| 32 | Roadmap SC6 | Exact automated gate passes with no unmitigated HIGH security finding. | **FAILED** | Actual artifacts show focused 5/5, broad 25/25, and five repetitions, but CR-08..CR-10 remain reachable HIGH blockers. |

**Score:** 29/32 truths verified.

### Roadmap Success Criteria

| SC | Status | Evidence |
|---|---|---|
| SC1 — one-shot reviewed genesis ceremony | VERIFIED | CLI/service/canonical identity/key cleanup are present and wired. |
| SC2 — persisted state is authoritative and restart-safe | **FAILED** | CR-10 breaks stable persisted-ready service initialization; CR-09 also loses retained candidate retry across controller restart. |
| SC3 — current peers/current policy authorize first durable winner | **FAILED** | Authorization is correct when called, but CR-09 prevents retained policy quorum replay. |
| SC4 — restricted genesis then live approved membership/burn changes | **FAILED** | Initial burn works; CR-08 prevents passive post-ready burn successor activation. |
| SC5 — node-scoped policy lifetime across SelectAccount | VERIFIED | Policy objects/provider survive account selection; existing source wiring is intact. |
| SC6 — exact automated gate with no unmitigated HIGH | **FAILED** | Test accounting is green but three source-reachable HIGH defects remain. |

### Required Artifacts

`gsd-sdk query verify.artifacts` reports every declared artifact across all 22 plans present and substantive. Manual Level 3/4 verification changes the functional status of these groups:

| Artifact group | Expected | Status | Details |
|---|---|---|---|
| Canonical codec, manifest, policy | Deterministic identities and exact policy validation | VERIFIED | Exists, substantive, production-linked, focused tests included. |
| `TrustStateStore.*` | Atomic coherent durable authority | VERIFIED | CR-07 lock/helper split is correct and independently spot-checked. |
| SecureCrdt candidate/legacy transport | Current-peer authenticated bounded CRDT flow | VERIFIED | Exact keys, membership, signatures, caps, and no new transport. |
| `TrustedPeerRegistry.*` | Durable policy activation and restart listing | **PARTIAL** | Listing API exists, but controller does not consume it after reconstruction (CR-09). |
| `BurnConfig.*` | Initial and successor burn activation | **PARTIAL** | Initial burn is production-wired; post-ready passive successors are blocked by controller return (CR-08). |
| `TrustStartupController.*` | Serialized restart-safe orchestration | **DEFECTIVE** | Live callbacks work, but retained policy replay and ready burn processing are incomplete. |
| `GeniusNode.*` / `TransactionManager.*` | One stable manager per startup/account transition | **DEFECTIVE** | Persisted-ready callback can post a duplicate initialization (CR-10). |
| CLI/composition/runbook | Production transport and operator boundary | VERIFIED | Substantive and linked. |
| Phase test fixtures | Release evidence | **PARTIAL** | WR-07 masks persisted historical restart and passive burn successor paths. |

### Key Link Verification

| From | To | Status | Details |
|---|---|---|---|
| `LoadAndVerify` | `LoadAndVerifyUnlocked` | WIRED | Public reader owns `transition_mutex_`; commit paths use the helper without re-locking. |
| Trust controller | initial BurnConfig initiation/retry | WIRED | `OnTrustedPeerGenesisConfirmed`, listing, activation, and errors are linked for BootstrapOnly. |
| Live policy callback | durable policy activation | WIRED | Remote callback queues the ID and `Refresh` calls `TryActivatePolicyCandidate`. |
| Controller reconstruction | retained policy listing | **NOT WIRED** | `ListPendingPolicyCandidates` has no production orchestration caller. |
| Ready burn callback | successor burn activation | **NOT WIRED** | ready return precedes discovery and `TryActivateBurnCandidate`. |
| Persisted-ready state callback | transaction initialization | **UNSAFE** | synchronous `ConfirmedReady` posts the state already in progress; no idempotence guard. |
| Confirmed provider | PayEscrow | WIRED | Exact provider-backed quotient/remainder arithmetic is the production path. |
| SelectAccount | retained policy services/provider | WIRED | Account-only teardown preserves node-scoped identities. |

### Data-Flow Trace (Level 4)

| Artifact | Data | Source | Produces real data | Status |
|---|---|---|---|---|
| Genesis startup | reviewed manifest/approval | CLI -> GlobalDB -> callback -> store | Yes | FLOWING |
| Initial burn | deterministic v1/value 100 approvals | controller -> CRDT -> store -> provider | Yes | FLOWING |
| Burn successors | replicated quorum approvals | callback queue -> Refresh -> required activation | Queue receives data; ready return stops it | **HOLLOW (CR-08)** |
| Policy successors after restart | retained quorum approvals | CRDT listing -> required merge -> activation | Real retained data; listing never called | **DISCONNECTED (CR-09)** |
| Persisted-ready runtime | verified snapshot -> controller state -> manager | real snapshot synchronously emits readiness | Flows twice into initialization | **DUPLICATED (CR-10)** |
| Live economics | confirmed basis points | store/provider -> PayEscrow | Yes, exact full-domain math | FLOWING |

### Behavioral Spot-Checks and Gate Accounting

| Behavior | Evidence/command | Result | Status |
|---|---|---|---|
| Coherent trust read and commit liveness | Fresh run of exact two `trust_state_store_test` cases | 2 executed, 2 passed in 429 ms | PASS |
| Exact five closure cases | Independently parsed retained GoogleTest XML files | 5 executed, 5 passed, zero failure/skip/disabled | PASS for named cases |
| Exact Phase 13 gate | Independently parsed `selection.txt` and `results.xml` | selected=25, executed=25, passed=25, failed=0, skipped=0, disabled=0 | PASS for selected targets |
| Lifetime repetition record | Independently inspected full retained run log | `lifetime_repeat_1` through `_5` all PASS | PASS for fixture path |
| Passive burn successor | Source order: ready return at 321 precedes listing at 365/activation at 398 | structurally unreachable after v1 readiness | **FAIL** |
| Retained policy replay | Repository call trace for `ListPendingPolicyCandidates` | only declaration/definition and LocalTrustAdmin caller | **FAIL** |
| Persisted-ready single manager | controller/GeniusNode/manager call graph | two manager constructions/starts are reachable | **FAIL** |

The retained evidence files are actual XML/log artifacts, not accepted SUMMARY narration. They prove the selected executions, but no green result is used to override the source defects.

### Probe Execution

No probe script or probe-based criterion is declared for Phase 13. Step 7c is not applicable.

### Requirements Coverage

All 15 Phase 13 roadmap requirement IDs appear in plan frontmatter; no Phase 13 requirement is orphaned.

| Requirement | Status | Evidence / blocker |
|---|---|---|
| BOOT-01 | SATISFIED | Manual ceremony/runbook and non-production labels exist. |
| BOOT-02 | SATISFIED | Canonical authenticated manifest binds all required fields. |
| BOOT-03 | SATISFIED | Production SecureCrdt genesis persists before economic readiness. |
| BOOT-04 | **BLOCKED** | CR-09 prevents retained policy retry after reconstruction; CR-10 makes persisted-ready startup lifecycle unsafe. |
| POLICY-01 | **BLOCKED** | Policy format/current authorizer are correct, but CR-09 prevents retained quorum from becoming current after restart. |
| VALID-01 | SATISFIED | Peer/candidate bounds and exact policy floors are enforced. |
| TEST-01 | **BLOCKED** | Exact gate omits CR-08, CR-09 reconstruction, and CR-10 historical persisted-ready manager ownership. |
| SCRDT-04 | SATISFIED | All proposal/sign/quorum transport remains CRDT-only. |
| TPR-01 | SATISFIED | Reviewed authenticated production TPR genesis is implemented. |
| TPR-02 | **BLOCKED** | N-of-M validation exists, but retained quorum is not replayed on reconstructed passive receivers. |
| BURN-01 | SATISFIED | Burn remains a trusted-peer-quorum-signed CRDT value. |
| BURN-02 | SATISFIED | Confirmed provider is node-scoped and retained across account selection. |
| BURN-03 | **BLOCKED** | Default v1/value 100 works, but passive nodes do not apply later quorum-signed burn updates (CR-08). |
| MIG-05 | SATISFIED | Approved signature-verification-only migration remains intact. |
| MIG-06 | SATISFIED | Existing ValidatorRegistry behavior remains in the gate. |

**Requirement accounting:** 10 satisfied, 5 blocked (`BOOT-04`, `POLICY-01`, `TEST-01`, `TPR-02`, `BURN-03`).

### Review Findings and Anti-Patterns

| Finding | Disposition | Exact evidence | Impact |
|---|---|---|---|
| CR-01 | CLOSED | Durable BootstrapOnly/PeerQuorum guards remain at `TrustStateStore.cpp:775-860`. | No remaining gap. |
| CR-02 | CLOSED | Restart consumes verified historical `PeerQuorum`; no current-threshold recount. | No remaining gap. |
| CR-03 | CLOSED | Exact quotient/remainder arithmetic at `TransactionManager.cpp:842-845`. | No remaining gap. |
| CR-04 | CLOSED | Current-member legacy signer gate and retention bound remain in `SecureCrdt.cpp`. | No remaining gap. |
| CR-05 | CLOSED | Production controller invokes initial-burn initiation/discovery/activation. | Narrow closure verified. |
| CR-06 | CLOSED | Live remote policy callback queues and activates without signing. | Narrow closure verified; restart replay is CR-09. |
| CR-07 | CLOSED | Locked public reader plus unlocked helper; exact cases freshly passed 2/2. | No remaining gap. |
| WR-06 | CLOSED | Genesis/policy/burn errors emit `TRUST_ACTIVATION_FAILED` and return typed errors. | No remaining gap. |
| CR-08 | **BLOCKER** | `Refresh` lines 321-325 make lines 327-442 unreachable after readiness. | Passive burn policy divergence. |
| CR-09 | **BLOCKER** | No production `ListPendingPolicyCandidates` caller. | Retained quorum can be stranded after restart/fault. |
| CR-10 | **BLOCKER** | Synchronous ready callback plus unguarded posted same-state transition constructs two managers. | Duplicate startup work, callback loss, reachable crash/corruption risk. |
| WR-07 | WARNING | Fresh-storage replacement at `multi_account_sync.cpp:389-405`; local burn proposal at policy-lifetime `:285-290`. | Green gate overstates restart/passive coverage. |
| Pre-existing TODO | WARNING | `multi_account_sync.cpp:538`, blame `fb1b89f90`, comments out a transaction-status assertion. | Not introduced by Phase 13 and outside the three blocker paths, but weakens this modified fixture. |
| Debt-marker scan | CLEAN | No `TBD`, `FIXME`, or `XXX` in reviewed Phase 13 production files. | No separate debt-marker blocker. |

Disconfirmation pass:

- Partial requirement: `TPR-02` correctly verifies current-set signatures when activation is called, but a reconstructed passive receiver never calls it for retained data.
- Misleading test: `policy_lifetime_multi_account_test` proves a local `ProposeBurn`, not passive burn convergence; broad green status does not cover CR-08.
- Uncovered error path: persisted-ready `ConfirmedReady` re-entry into transaction initialization has no single-manager ownership assertion and was removed from the historical-storage fixture.

### Human Verification Required

None. These gaps are deterministically visible in source ordering, repository-wide call sites, state-transition wiring, fixture history, and retained machine evidence. The LLDB crash reported while fixture work was in progress was not independently reproduced, but CR-10 does not depend on accepting that crash report: the duplicate-manager call chain is directly reachable in code.

### Deferred Items

None. `roadmap.analyze` reports no later phase in the v1.1 milestone. CR-08, CR-09, CR-10, and WR-07 are Phase 13 closure work.

### Exact Next Gap Closure List

1. Move passive burn candidate discovery/activation before the ready return; prove remote A+B approvals alone advance passive C to burn v2 and change C's PayEscrow output.
2. Merge `ListPendingPolicyCandidates()` into every startup/Refresh retry cycle; prove retained quorum survives injected commit failure plus controller reconstruction with no new write/admin action.
3. Make GeniusNode state transitions serialized and idempotent; suppress same-state `INITIALIZING_TRANSACTIONS`, stop-before-replace, and owner-scope manager callbacks.
4. Restore persisted historical trust/transaction storage in a restart fixture and assert exactly one TransactionManager construction/start plus stable GlobalDB/account/blockchain callbacks; run it under ASan/TSan.
5. Keep the existing exact five focused cases, then add the three new counterexamples, rerun exact 25/25 accounting (or intentionally update the expected count), and repeat the corrected passive/lifetime fixture five times.

### Gaps Summary

Phase 13 remains blocked. Initial burn recovery, live passive policy activation, coherent durable reads, and activation-error propagation are real improvements, but successor processing is incomplete after readiness/reconstruction and persisted-ready GeniusNode startup is re-entrant. With `security_enforcement=true` and `security_block_on=high`, CR-08 through CR-10 make Roadmap SC6 false regardless of the green selected gate.

---

_Verified: 2026-08-13T20:29:40Z_
_Verifier: the agent (gsd-verifier)_
