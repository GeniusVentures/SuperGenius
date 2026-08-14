---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
verified: 2026-08-14T18:26:11Z
status: gaps_found
score: 28/32 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 29/32
  gaps_closed:
    - "CR-08: passive burn successors are discovered and activated before readiness publication, without successor auto-signing."
    - "CR-09: retained policy quorum is rediscovered after commit failure and controller/store reconstruction without another write or admin action."
    - "CR-10: persisted-ready re-entry is suppressed and the same-path historical restart constructs and starts one manager."
    - "WR-07: passive A/B-to-C PayEscrow and true same-path historical restart coverage are restored."
  gaps_remaining:
    - "CR-11: SelectAccount and public/asynchronous readers race account_ and transaction_manager_."
    - "CR-12: the controller signer address is fixed while its signing key follows the mutable selected account."
    - "CR-13: worker-triggered transient refresh failures are discarded without an event or bounded retry."
    - "CR-14: a worker callback can release the last controller owner and cause self-join/terminate or raw-pointer use-after-free."
  regressions: []
gaps:
  - truth: "D-16 and Roadmap SC5: account selection preserves safe node-scoped policy, manager, account, and callback ownership"
    status: failed
    reason: "CR-11: lifecycle serialization does not cover SelectAccount, GetAddress, GetTransactionManager, or catch-up callbacks; concurrent read/write of the same non-atomic shared_ptr objects is undefined behavior."
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "SelectAccount lines 2474-2522 mutates account_/transaction_manager_ without lifecycle_mutex_; GetAddress lines 3322-3330, GetTransactionManager lines 3368-3375, and catch-up callbacks lines 3580-3618 read them without the same ownership protocol."
      - path: "test/src/multiaccount/policy_lifetime_multi_account_test.cpp"
        issue: "Sequential account switching is covered, but no concurrent SelectAccount/public-query/catch-up stress case exists."
    missing:
      - "Serialize account/manager publication and snapshots or use atomic shared_ptr publication with a serialized switch state."
      - "Stop/join and reconstruct the catch-up watcher around account replacement; capture generation-bound snapshots in callbacks."
      - "Add a concurrent account-switch/query/catch-up regression and run it under TSan when target instrumentation exists."
  - truth: "D-13/D-16 and Roadmap SC4/SC5: a node-scoped trust signer remains cryptographically consistent across account selection and can complete initial burn"
    status: failed
    reason: "CR-12: TrustStartupController records the selected account's address once, but its sign callback dereferences the current account after SelectAccount; signatures are labeled with the old address and produced by the new key."
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "Lines 817-826 pass account_->GetAddress() as immutable signer identity while the callback dynamically calls self->account_->Sign()."
      - path: "src/account/TrustStartupController.cpp"
        issue: "The fixed address is propagated to TPR/BurnConfig and controls initial-burn membership/signature labeling."
    missing:
      - "Capture one immutable node-scoped trust identity and signing key together, or explicitly prohibit/authorize trust-identity changes."
      - "Add active-member account-switch tests before initial-burn readiness and after readiness."
  - truth: "D-08/D-12 and Roadmap SC3/SC4: retained policy and burn quorum converges without requiring another write after a transient discovery failure"
    status: failed
    reason: "CR-13: the refresh worker clears its request and ignores Refresh(); ListPendingPolicyCandidates/ListPendingBurnCandidates errors return without an event, requeue, or bounded backoff, so final-quorum data can remain stranded until an unrelated trigger."
    artifacts:
      - path: "src/account/TrustStartupController.cpp"
        issue: "Lines 170-184 discard the worker result; lines 266-270 and 377-380 return listing errors without observability or retry."
      - path: "test/src/startup/trust_first_boot_e2e_test.cpp"
        issue: "Commit-failure reconstruction is covered, but one-shot transient policy/burn listing failure with no subsequent write is not."
    missing:
      - "Classify refresh errors, emit typed events, and schedule bounded exponential-backoff retry for transient discovery/infrastructure failures."
      - "Add deterministic one-failure-then-success policy and burn listing tests with no new CRDT write."
  - truth: "D-16 and Roadmap SC6: controller callbacks and worker teardown are owner-safe with no unmitigated HIGH finding"
    status: failed
    reason: "CR-14: refresh_worker_ captures only instance.get(); synchronous state/event callbacks may drop the last shared owner on that worker, making the destructor join its current thread and terminate, or leaving the raw worker to access a destroyed object."
    artifacts:
      - path: "src/account/TrustStartupController.cpp"
        issue: "Raw worker capture at lines 170-184, synchronous callbacks at lines 515-531, and unconditional join at lines 190-200 form a reachable self-destruction path."
      - path: "src/account/TrustStartupController.hpp"
        issue: "Worker state is embedded in the controller rather than independently owned/drained."
    missing:
      - "Move worker queue/stop state to an independently owned object or executor and make callback-driven final release safe."
      - "Add a test that releases the last controller owner from a worker-delivered callback without terminate, detach, or post-destruction access."
---

# Phase 13: Trusted-Peer Genesis, Quorum Policy, and Production Integration Verification Report

**Phase Goal:** Running nodes establish a manually reviewed, authenticated trusted-peer genesis; persist and enforce versioned quorum policy as the restart authority; accept only current-peer-authenticated content-addressed CRDT candidates; apply explicit quorum-approved membership and burn successors; and preserve policy services and live burn behavior across account selection.

**Verified:** 2026-08-14T18:26:11Z
**HEAD:** `2175df72dbbea5b76fa1e970a5bffd15263d345e`
**Status:** gaps_found
**Re-verification:** Yes — after Plans 13-23 through 13-26

## Goal Achievement

Plans 13-23 through 13-26 substantively close CR-08, CR-09, the original CR-10, and WR-07. A fresh 25-target build, exact 15-case source/XML run, and exact 25-target CTest/JUnit run pass at current HEAD. Those results do not override four independently traced HIGH production defects. CR-11 predates Phase 13's new mutex, but it remains phase-blocking because account selection is explicitly within the phase goal; CR-12 through CR-14 are Phase-13 wiring/worker defects. No later v1.1 phase exists to defer them.

### Observable Truths

| # | Source | Truth | Status | Evidence |
|---:|---|---|---|---|
| 1 | 13-01 | Canonical genesis identity is deterministic and rejects malformed/tampered input. | VERIFIED | Codec/manifest are substantive; fresh `genesis_manifest_test` passed in the exact gate. |
| 2 | 13-02 / SC2 | Persisted genesis, policy, and burn heads form one coherent durable authority. | VERIFIED | Locked `LoadAndVerify` and coherent-read cases are present and green. |
| 3 | 13-02 | Missing/corrupt/forked/failed candidates never erase durable LKG. | VERIFIED | Store/tamper/restart paths retain typed fail-closed behavior. |
| 4 | 13-02 | Activation persists synchronously before cache publication. | VERIFIED | TPR/Burn publish only after durable store commit/reload. |
| 5 | 13-03 | Every SecureCrdt owns an independent policy registry. | VERIFIED | Instance registry and owner-token teardown remain wired. |
| 6 | 13-03 | Legacy behavior remains operational and production owners use their node registry. | VERIFIED | Production owners resolve through their SecureCrdt registry. |
| 7 | 13-04 | Proposal/approval is explicit; receipt/relay never signs successors. | VERIFIED | Signing remains explicit except exact BootstrapOnly initial burn. |
| 8 | 13-04 | Ingress rejects malformed, oversized, wrong-context, and non-current-peer candidates. | VERIFIED | Shared authorization and bounds are production-wired. |
| 9 | 13-04 | Content/version/hash keys isolate candidates and approvals. | VERIFIED | Canonical candidate IDs and exact approval keys are used. |
| 10 | 13-04 | Candidate resources enforce the declared limits. | VERIFIED | Cap constants/enforcement remain covered by SecureCrdt targets. |
| 11 | 13-05 / SC3 / SC4 | Every receiving node durably applies retained quorum successors. | **FAILED** | CR-13 can silently consume the only refresh after final approval and never retry. |
| 12 | 13-05 | Non-genesis propose/approve is explicit and receipt does not auto-sign. | VERIFIED | Passive callbacks enqueue only foreign approvals; no successor signing hook. |
| 13 | 13-05 | Durable predecessor comparison permits one winner and rejects stale losers. | VERIFIED | Transition/CAS and race tests remain green. |
| 14 | 13-05 / SC4 | After durable TPR genesis, production peers automatically progress burn v1/value 100. | **FAILED** | CR-12 can strand an active member after account selection by signing under a key/address mismatch. |
| 15 | 13-06 | Trust tools reuse production GlobalDB/CRDT without GeniusNode or a new topic. | VERIFIED | Existing composition/topic path remains wired. |
| 16 | 13-06 | Composition owns/stops networking resources safely. | VERIFIED | Owned Start/Stop lifecycle remains substantive. |
| 17 | 13-07 | Fresh startup networks while economically restricted. | VERIFIED | Explicit waiting states and fail-closed readiness remain. |
| 18 | 13-07 | Real ceremony transport reaches durable TPR and success-only key cleanup. | VERIFIED | Ceremony/composition path is substantive and tested. |
| 19 | 13-07 / SC2 | Persisted state is stable restart authority; conflicts alert and corruption/mismatch fails closed. | VERIFIED | Original CR-10 is closed by state/epoch gating and same-path historical restart. |
| 20 | 13-07 | Missing/older/fork replicated data alerts without replacing LKG. | VERIFIED | ObserveReplicatedSnapshot preserves durable authority. |
| 21 | 13-08 / SC5 | SelectAccount safely preserves policy objects, stores, registrations, caches, managers, and callbacks. | **FAILED** | CR-11 races ownership; CR-12 changes the signing key behind the retained signer identity; CR-14 leaves callback-owner destruction unsafe. |
| 22 | 13-08 | Replacement TransactionManager consumes the same provider and fails closed pre-ready. | VERIFIED | Sequential replacement/provider identity is correct; concurrency safety is counted in Truth 21. |
| 23 | 13-08 / SC4 | Approved burn successors change real PayEscrow output; stale/failed data does not. | VERIFIED | Passive A/B-to-C case uses real C PayEscrow and durable provider. |
| 24 | 13-09 / SC1 | `sgns-trust genesis` reviews, signs ephemerally, confirms, and cleans up only after success. | VERIFIED | CLI/service/key handling are substantive. |
| 25 | 13-09 | Local list/propose/approve is explicit with no remote admin surface. | VERIFIED | LocalTrustAdmin only; no new RPC/topic. |
| 26 | 13-10 | Operators have an exact trusted-channel/fingerprint ceremony and labeled examples. | VERIFIED | Runbook and non-production labels exist. |
| 27 | 13-10 | Runbook explains persisted authority, alerts, and whole-disk rollback boundary. | VERIFIED | Accepted external-anchor limitation is explicit. |
| 28 | 13-11 | Canonical metadata changed only after recorded green evidence. | VERIFIED | Ordering exists; completion claims are now stale because source review found blockers. |
| 29 | 13-11 | MIG-05 narrow scope and whole-disk limitation remain intact. | VERIFIED | REQUIREMENTS/PROJECT preserve both boundaries. |
| 30 | 13-12 | Policy contains both thresholds and enforces exact bounds/floors. | VERIFIED | Exact validators/boundaries remain wired. |
| 31 | 13-12 | Successors require version+1, current-hash linkage, and current-policy authorization. | VERIFIED | Canonical/durable proof validation remains substantive. |
| 32 | Roadmap SC6 | Exact automated gate passes with no unmitigated HIGH finding. | **FAILED** | The gate is green, but CR-11 through CR-14 are reachable unmitigated HIGH defects. |

**Score:** 28/32 truths verified.

### Roadmap Success Criteria

| SC | Status | Evidence |
|---|---|---|
| SC1 — one-shot reviewed genesis ceremony | VERIFIED | Canonical CLI/service/key cleanup are wired. |
| SC2 — persisted state is authoritative and restart-safe | VERIFIED | Same-path restart and tamper paths pass; CR-13 does not replace LKG. |
| SC3 — current policy authorizes the first durable winner | **FAILED** | Authorization is correct when attempted, but CR-13 can strand retained quorum after one transient listing error. |
| SC4 — restricted genesis then live approved membership/burn changes | **FAILED** | CR-12 can strand initial burn after account selection; CR-13 can strand later passive convergence. |
| SC5 — node-scoped policy lifetime across SelectAccount | **FAILED** | CR-11 and CR-12 violate safe owner/signer lifetime despite sequential tests. |
| SC6 — exact automated gate with no unmitigated HIGH | **FAILED** | CR-11 through CR-14 remain reachable HIGH findings. |

### Required Artifacts

| Artifact group | Expected | Status | Details |
|---|---|---|---|
| Canonical codec, manifest, policy, store | Deterministic, durable trust authority | VERIFIED | Exists, substantive, linked, and freshly tested. |
| SecureCrdt candidate transport | Current-peer authenticated bounded CRDT flow | VERIFIED | Exact key/signature/bounds path remains. |
| `TrustedPeerRegistry.*` / `BurnConfig.*` | Explicit signing and durable activation | PARTIAL | Core validation/commit works; CR-12 signer identity and CR-13 retry liveness break production operation. |
| `TrustStartupController.*` | Restart-safe serialized orchestration | DEFECTIVE | CR-13 discards worker errors; CR-14 has raw-pointer/self-destruction lifetime. |
| `GeniusNode.*` / `TransactionManager.*` | Safe account/manager replacement | DEFECTIVE | Original duplicate-manager bug is closed, but CR-11 races ownership and CR-12 mutates the signer key. |
| CLI/composition/runbook | Production operator boundary | VERIFIED | Substantive and linked. |
| Phase tests | Exact regression evidence | PARTIAL | Required gates pass, but no CR-11–14 counterexamples exist. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| Refresh | retained policy listing/activation | list, sort, dedupe, activate | WIRED | Lines 266-342; closes CR-09 reconstruction. |
| Refresh | retained burn listing/activation | list before readiness | WIRED | Lines 377-455; closes CR-08 without successor signing. |
| Trust ready callback | transaction initialization | source/epoch gate | WIRED | Lines 866-920; closes original CR-10. |
| SelectAccount | account/manager readers | shared ownership protocol | **NOT WIRED** | CR-11: writers/readers use no common lock/atomic publication. |
| Controller signer address | signing key | immutable identity/key pair | **NOT WIRED** | CR-12: fixed label, mutable selected-account key. |
| Worker Refresh error | event + bounded retry | result inspection/backoff | **NOT WIRED** | CR-13: result is discarded. |
| Controller owner | refresh worker lifetime | shared/drained ownership | **UNSAFE** | CR-14: raw pointer plus possible self-join/UAF. |

### Data-Flow Trace (Level 4)

| Artifact | Data | Source | Produces real data | Status |
|---|---|---|---|---|
| Genesis/startup | reviewed manifest and approvals | CLI -> CRDT -> store | Yes | FLOWING |
| Passive burn successor | A proposal + B approval | CRDT -> C Refresh -> store/provider -> PayEscrow | Yes | FLOWING |
| Retained policy successor | retained approvals after reconstruction | CRDT listing -> activation -> store | Yes | FLOWING |
| Selected-account signer | fixed signer label + current account key | controller + mutable `account_` | Mismatched after switch | **CORRUPTIBLE (CR-12)** |
| Refresh retry | transient listing error | worker ignores `Refresh()` result | No event/retry | **DISCONNECTED (CR-13)** |

### Behavioral Spot-Checks

| Behavior | Command/evidence | Result | Status |
|---|---|---|---|
| Fresh exact build | explicit 25 Release targets, `-j8` | exit 0 | PASS |
| Exact focused counterexamples | seven binaries, exact 15 filters, XML parse | 15 executed, 15 passed, 0 failed/skipped | PASS |
| Restored CR-01/HIGH semantics | `PolicyV2BeforeInitialBurnCannotStrandStartup` source and fresh run | rejects pre-burn write/sign, later succeeds | PASS |
| CTest enumeration | unchanged Phase 13 regex, `ctest -N -V` | exact 25 names, no disabled target | PASS |
| CTest/JUnit | unchanged regex with `--output-junit` | 25/25, zero failure/skip/disabled, 391.16 s | PASS |
| Sanitizers | target-scoped compile/link discovery contract | no proven pair | NOT_RUN (not PASS) |
| Passive lifetime repetition | five exact post-gate invocations of `PolicyLifetimeMultiAccountTest.PassiveBurnSuccessorChangesPayEscrowWithoutReceiverAdmin` | repeats 1-5 each exited 0 | PASS |
| CR-11–14 reachability | direct source/ownership trace | all four reachable; no covering test | FAIL |

### Probe Execution

No probe script or probe-based criterion is declared for Phase 13.

### Requirements Coverage

All 15 Phase 13 roadmap IDs appear in plan frontmatter; none is orphaned.

| Requirement | Status | Evidence / blocker |
|---|---|---|
| BOOT-01 | SATISFIED | Ceremony/runbook and non-production labels exist. |
| BOOT-02 | SATISFIED | Canonical authenticated manifest binds all fields. |
| BOOT-03 | SATISFIED | Production genesis persists before readiness. |
| BOOT-04 | SATISFIED | Verified persisted state remains restart/LKG authority. |
| POLICY-01 | **BLOCKED** | CR-13 can strand a valid retained current-policy-authorized successor after transient discovery failure. |
| VALID-01 | SATISFIED | Peer/candidate bounds and threshold floors are enforced. |
| TEST-01 | **BLOCKED** | Exact gates omit CR-11 concurrent ownership, CR-12 active-signer switch, CR-13 transient listing retry, and CR-14 callback final-release paths. |
| SCRDT-04 | SATISFIED | Transport remains CRDT-only. |
| TPR-01 | SATISFIED | Reviewed authenticated production TPR genesis exists. |
| TPR-02 | **BLOCKED** | CR-12 invalidates selected-account signing identity and CR-13 can prevent retained current-set quorum activation. |
| BURN-01 | SATISFIED | Burn remains a quorum-signed CRDT value. |
| BURN-02 | **BLOCKED** | CR-11 makes account/manager/provider ownership unsafe across SelectAccount. |
| BURN-03 | **BLOCKED** | CR-12 can strand burn v1; CR-13 can strand later quorum-signed updates. |
| MIG-05 | SATISFIED | Narrow signature-verification-only migration remains intact. |
| MIG-06 | SATISFIED | Existing ValidatorRegistry behavior remains in the green gate. |

**Requirement accounting:** 10 satisfied, 5 blocked (`POLICY-01`, `TEST-01`, `TPR-02`, `BURN-02`, `BURN-03`).

### Review Findings and Anti-Patterns

| Finding | Disposition | Exact evidence | Impact |
|---|---|---|---|
| CR-08 | CLOSED | Burn listing/activation at controller lines 377-453 precedes readiness line 455; signing is BootstrapOnly/current-member at 347-352. | Passive burn convergence restored. |
| CR-09 | CLOSED | Policy listing/merge/activation at 266-342; exact reconstruction no-write window at test lines 1375-1398. | Retained commit-failure recovery restored. |
| CR-10 | CLOSED (original path) | State/source/epoch gating at GeniusNode 866-920 and stop-before-replace at 989-1031. | Persisted-ready duplicate manager removed. |
| WR-07 | CLOSED | Passive C real PayEscrow test and same `historical_base_path` restart assertions are present and fresh-green. | Required coverage restored. |
| CR-11 | **BLOCKER** | Unsynchronized writes at 2474-2522 and reads at 3322-3375/3580-3618; watcher stops only at full destruction 2127-2132. | Data race, wrong-account mutation, UAF/process corruption. |
| CR-12 | **BLOCKER** | Fixed address at 821 and mutable-key callback at 822-826. | Invalid initial-burn/policy signatures after account switch. |
| CR-13 | **BLOCKER** | Worker discards result at controller 170-184; listing errors return at 266-270/377-380. | Retained quorum liveness and observability failure. |
| CR-14 | **BLOCKER** | Raw worker pointer 170-184, synchronous callbacks 515-531, self-join destructor 190-200. | Reachable terminate or UAF. |
| TODOs | WARNING | Existing TODOs at GeniusNode 2678 and multi-account fixture 684 are not `TBD`/`FIXME`/`XXX` and are outside these roots. | No separate completion blocker. |
| Debt markers | CLEAN | No `TBD`, `FIXME`, or `XXX` in reviewed Phase 13 production/test files. | No debt-marker blocker. |

Disconfirmation pass:

- Partial requirement: TPR-02 verifies signatures when activation runs, but CR-12 can produce a signature under the wrong key and CR-13 can prevent activation from being retried.
- Misleading green test: sequential `SelectAccount` tests cannot detect CR-11's concurrent shared-pointer race or CR-12's active signer mismatch.
- Uncovered error path: worker-triggered listing failure and callback-triggered final owner release have no deterministic regression.

### Human Verification Required

None. All gaps are deterministically established from source ordering, C++ ownership rules, callback call graphs, test-source omissions, and executable gates.

### Deferred Items

None. `roadmap.analyze` reports Phase 13 as the only v1.1 phase and no later phase can absorb CR-11 through CR-14.

### Exact Next Gap Closure List

1. Unify `SelectAccount`, account/manager publication, public snapshots, catch-up watcher, and asynchronous callbacks under one generation-aware ownership protocol; add concurrent stress/TSan coverage.
2. Make the trust signer an immutable node-scoped address/key pair (or explicitly forbid/authorize identity changes), then test active-member switching before and after initial-burn readiness.
3. Inspect worker `Refresh()` results; emit typed events and bounded backoff retries for transient policy/burn discovery failures; prove recovery without a new write.
4. Replace the controller worker's raw-pointer lifetime with independent owned/drained worker state; prove callbacks may release the last controller owner safely.
5. Add the four counterexamples to the exact focused gate, intentionally update exact name accounting, retain 25-target CTest/JUnit equality, and report sanitizer absence only as `NOT_RUN`.

### Gaps Summary

Phase 13 remains blocked. CR-08, CR-09, the original CR-10, and WR-07 are genuinely closed and all requested Release gates are green. The phase nevertheless promises safe node-scoped trust and account lifetime and forbids unmitigated HIGH findings. CR-11 through CR-14 violate those contracts through reachable concurrency, signer-integrity, retry-liveness, and callback-owner failures; green tests cannot make Roadmap SC3-SC6 true.

---

_Verified: 2026-08-14T18:26:11Z_
_Verifier: the agent (gsd-verifier)_
