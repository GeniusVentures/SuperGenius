---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
verified: 2026-08-17T14:24:57Z
status: gaps_found
score: 29/32 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 28/32
  gaps_closed:
    - "Original CR-12: the controller now owns one immutable NodeTrustSigner address/key pair across SelectAccount."
    - "Original CR-13 narrow failure: transient policy/burn discovery errors now emit typed events and receive bounded retry."
    - "Original CR-14: the raw controller-owned worker/self-join path was replaced by independent weak-owner dispatch state."
  gaps_remaining:
    - "Latest CR-01: SelectAccount clears switching before complete manager publication and admits overlapping account generations."
    - "Latest CR-02: snapshotted old managers/public processing owners can still mutate after Stop/unpublish."
    - "Latest CR-03: provider/relayer/catch-up watcher publication and reset race outside lifecycle synchronization."
    - "Latest CR-04: a coalesced request arriving during an active successful refresh is erased and can strand retained quorum."
  regressions: []
gaps:
  - truth: "D-16 and Roadmap SC5: SelectAccount publishes and retires complete account-service generations safely"
    status: failed
    reason: "SelectAccount clears account_service_switching_ immediately after publishing the new account, before asynchronous blockchain and TransactionManager initialization publish the complete pair; overlapping switches and cross-generation blockchain callbacks remain reachable."
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "Lines 2557-2560 publish account_ and clear switching while transaction_manager_ is still null; blockchain callbacks at 701-747 carry neither the account generation nor expected Blockchain identity."
      - path: "test/src/multiaccount/multi_account_sync.cpp"
        issue: "Lines 693-700 skip every account/null-manager observation and wait for READY before starting another selection, so the claimed regression cannot detect the defect."
    missing:
      - "Keep switching true until the complete account/manager generation is published at transaction initialization."
      - "Generation- and Blockchain-identity-bind every start/retry/completion callback and reject stale completions."
      - "Test the account/non-null + manager/null interval and an overlapping second SelectAccount without waiting for READY."
  - truth: "D-16 and Roadmap SC5: retired account-service generations cannot accept public transaction or processing mutations"
    status: failed
    reason: "A public caller may retain a coherent strong snapshot, race SelectAccount, and call TransferFunds/MintFunds/EnqueueTransaction after Stop; stopped_ does not change READY and these mutators have no common stop/submission gate. GetProcessingStatus also races an ordinary processing_service_ reset."
    artifacts:
      - path: "src/account/TransactionManager.cpp"
        issue: "Stop lines 343-357 sets stopped_, but TransferFunds 571-593, MintFunds 596-726, and EnqueueTransaction 1120-1139 do not reject stopped_ under a shared mutation lock."
      - path: "src/account/GeniusNode.hpp"
        issue: "Inline GetProcessingStatus lines 535-540 dereferences processing_service_ without synchronization while account teardown resets it."
    missing:
      - "Add a generation operation lease or serialized revalidation that SelectAccount drains before Stop/deconfigure."
      - "Make every TransactionManager mutator reject stopped_ under the same submission lock, including immediately before reserve/enqueue."
      - "Publish/snapshot processing_service_ under lifecycle synchronization."
      - "Add deterministic snapshot/stop/reserve/enqueue and processing-service teardown regressions."
  - truth: "D-16 and Roadmap SC5: bridge provider, relayer, and catch-up watcher ownership is generation-safe"
    status: failed
    reason: "InitializeAndStartBridge directly assigns/reads rpc_endpoint_provider_, bridge_relayer_, and catchup_watcher_ while SelectAccount teardown directly resets/moves the same ordinary shared_ptr objects; a stale initializer can publish/start a watcher after teardown has passed its stop point."
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "Lines 3631-3652 and 3751-3780 publish/read bridge owners without lifecycle_mutex_; lines 1984-1985, 2017-2020, and 2527 reset/move them concurrently."
    missing:
      - "Construct bridge owners as locals, revalidate generation under lifecycle_mutex_, and atomically publish the complete owner set."
      - "Move every retired watcher under that lock and stop it outside the lock before destruction."
      - "Add real provider/watcher barriers around snapshot, publication, start, and teardown; run under TSan when available."
  - truth: "D-08/D-12 and Roadmap SC3/SC4: every retained quorum request is eventually reconsidered without another write"
    status: failed
    reason: "RequestDispatch sets coalesced_request when an attempt is active, but FinishDispatch unconditionally clears it and schedules no follow-up. A distinct candidate arriving after the active attempt copied its pending vector can remain queued forever after that attempt succeeds."
    artifacts:
      - path: "src/account/TrustStartupController.cpp"
        issue: "RequestDispatch lines 625-649 records the bit; FinishDispatch lines 652-666 always erases it; policy and burn vectors are snapshotted at lines 356 and 471 respectively."
      - path: "test/src/startup/trust_first_boot_e2e_test.cpp"
        issue: "The exhaustion/coalescing case proves duplicate absorption during a failing cycle, not a distinct late candidate arriving after vector snapshot in a successful active attempt."
    missing:
      - "Replace the lossy bit with a monotonic dirty/request generation or schedule one follow-up when work arrived after the attempt snapshot."
      - "Only mark dirty when enqueue inserts a new candidate ID."
      - "Add barrier-driven policy and burn cases that introduce a distinct candidate after vector copy and prove activation without another write."
---

# Phase 13: Trusted-Peer Genesis, Quorum Policy, and Production Integration Verification Report

**Phase Goal:** Running nodes establish a manually reviewed, authenticated trusted-peer genesis; persist and enforce versioned quorum policy as the restart authority; accept only current-peer-authenticated content-addressed CRDT candidates; apply explicit quorum-approved membership and burn successors; and preserve policy services and live burn behavior across account selection.

**Verified:** 2026-08-17T14:24:57Z
**HEAD:** `563eaae7c7ad0bd86f57e6b9e13f7557c0bc6b6a`
**Status:** gaps_found
**Re-verification:** Yes — after Plans 13-27 through 13-29 and the latest deep review

## Goal Achievement

Plans 13-27 and 13-28 make real progress: the trusted-peer signing identity is now immutable across account selection; transient discovery errors are classified and retried; and the controller-owned raw worker/self-join path is gone. Plan 13-29's evidence artifacts are real and the relevant binaries still build at current HEAD. They do not prove the four interleavings above. Current source therefore still violates the account-lifetime, passive-convergence, and no-unmitigated-HIGH portions of the phase contract.

### Observable Truths

| # | Source | Truth | Status | Evidence |
|---:|---|---|---|---|
| 1 | 13-01 | Canonical genesis identity is deterministic and rejects malformed/tampered input. | VERIFIED | Canonical codec/manifest, fixed domain/bounds, and golden/tamper tests remain substantive and wired. |
| 2 | 13-02 / SC2 | Persisted genesis, policy, and burn heads form one coherent durable authority. | VERIFIED | Transition-locked `LoadAndVerify` and current record/head verification remain wired. |
| 3 | 13-02 | Missing/corrupt/forked/failed candidates never erase durable LKG. | VERIFIED | Typed store failures precede cache publication; persisted records remain authoritative. |
| 4 | 13-02 | Activation persists synchronously before cache publication. | VERIFIED | TPR/Burn activation commits and reloads before confirmed cache/provider publication. |
| 5 | 13-03 | Every SecureCrdt owns an independent policy registry. | VERIFIED | Instance registry ownership and owner-token unregister paths remain present. |
| 6 | 13-03 | Legacy behavior remains operational and production owners use their node registry. | VERIFIED | Production TPR/Burn registrations resolve through the owning SecureCrdt registry. |
| 7 | 13-04 | Proposal/approval is explicit; receipt/relay never signs successors. | VERIFIED | Successor signing remains local/operator-driven; only exact bootstrap burn behavior is automatic. |
| 8 | 13-04 | Ingress rejects malformed, oversized, wrong-context, and non-current-peer candidates. | VERIFIED | Shared local/remote authorization and bounds gates remain wired. |
| 9 | 13-04 | Content/version/hash keys isolate candidates and approvals. | VERIFIED | Canonical candidate IDs and exact approval keys remain substantive. |
| 10 | 13-04 | Candidate resources enforce declared limits. | VERIFIED | Candidate, approval, count, and retained-byte caps remain in production gates. |
| 11 | 13-05 / SC3 / SC4 | Every receiving node durably applies retained quorum successors. | **FAILED** | Latest CR-04 can erase the only late refresh request and strand a retained distinct policy/burn candidate. |
| 12 | 13-05 | Non-genesis propose/approve is explicit and receipt does not auto-sign. | VERIFIED | Passive callbacks activate only already authenticated quorum; no successor signing hook was added. |
| 13 | 13-05 | Durable predecessor comparison permits one winner and rejects stale losers. | VERIFIED | Store transition serialization and predecessor/hash/version checks remain intact. |
| 14 | 13-05 / SC4 | After durable TPR genesis, eligible production peers progress burn v1/value 100 under a stable trust identity. | VERIFIED | Original CR-12 is closed: `NodeTrustSigner` pins label and authority; before-readiness signature evidence is green. |
| 15 | 13-06 | Trust tools reuse production GlobalDB/CRDT without GeniusNode or a new topic. | VERIFIED | `GlobalDbNetworkComposition` and existing topic flow remain wired. |
| 16 | 13-06 | Composition owns/stops networking resources safely. | VERIFIED | Start/Stop composition ownership remains substantive. |
| 17 | 13-07 | Fresh startup networks while economically restricted. | VERIFIED | Waiting states and fail-closed economic provider remain wired. |
| 18 | 13-07 | Real ceremony transport reaches durable TPR and success-only cleanup. | VERIFIED | CLI -> production CRDT -> durable store flow remains substantive. |
| 19 | 13-07 / SC2 | Persisted state is stable restart authority; conflicts alert and corruption/mismatch fails closed. | VERIFIED | State/epoch gating and same-path restart evidence remain valid. |
| 20 | 13-07 | Missing/older/fork replicated data alerts without replacing LKG. | VERIFIED | Replicated observation cannot replace durable non-descendant heads. |
| 21 | 13-08 / SC5 | SelectAccount safely preserves complete policy/account/manager/bridge/callback ownership. | **FAILED** | Latest CR-01, CR-02, and CR-03 leave partial publication, post-stop mutation, unsynchronized processing owner, and bridge watcher races. |
| 22 | 13-08 | A successfully published replacement manager consumes the retained confirmed provider and fails closed before readiness. | VERIFIED | Construction injects the retained provider; availability checks return not-ready when snapshot has no manager. Complete publication safety is scored in Truth 21. |
| 23 | 13-08 / SC4 | Approved burn successors change real PayEscrow output; stale/failed data does not. | VERIFIED | Sequential real-PayEscrow data flow remains verified; eventual trigger loss is scored in Truth 11. |
| 24 | 13-09 / SC1 | `sgns-trust genesis` reviews, signs ephemerally, confirms, and cleans up only after success. | VERIFIED | CLI/service/key lifecycle remain substantive. |
| 25 | 13-09 | Local list/propose/approve is explicit with no remote admin surface. | VERIFIED | LocalTrustAdmin remains the only administration surface. |
| 26 | 13-10 | Operators have an exact trusted-channel/fingerprint ceremony and labeled examples. | VERIFIED | Runbook and explicit non-production labels remain present. |
| 27 | 13-10 | Runbook explains persisted authority, alerts, and whole-disk rollback boundary. | VERIFIED | External-anchor limitation remains explicit and accepted. |
| 28 | 13-11 | Canonical metadata changes only after recorded green evidence. | VERIFIED | Evidence ordering exists; its completion conclusion is superseded by this source verification. |
| 29 | 13-11 | MIG-05 narrow scope and whole-disk limitation remain intact. | VERIFIED | REQUIREMENTS/PROJECT preserve both decisions. |
| 30 | 13-12 | Policy contains both thresholds and enforces exact bounds/floors. | VERIFIED | 1..M, strict-majority membership, and two-thirds burn validation remain wired. |
| 31 | 13-12 | Successors require version+1, current-hash linkage, and current-policy authorization. | VERIFIED | Canonical/durable proof validation remains substantive. |
| 32 | Roadmap SC6 | Exact automated gates pass with no unmitigated HIGH finding. | **FAILED** | Automated gates pass, but latest CR-01 through CR-04 are independently reachable release blockers; sanitizer status is NOT_RUN. |

**Score:** 29/32 truths verified.

### Roadmap Success Criteria

| SC | Status | Evidence |
|---|---|---|
| SC1 — one-shot reviewed genesis ceremony | VERIFIED | Canonical CLI/service/key-cleanup production path is wired. |
| SC2 — persisted state authoritative and restart-safe | VERIFIED | Coherent durable store, mismatch/corruption failure, and LKG preservation remain intact. |
| SC3 — current peers/current policy authorize first durable candidate winner | **FAILED** | Authorization is correct when evaluated, but latest CR-04 can lose the only trigger for an already retained winner. |
| SC4 — restricted genesis then live approved membership/burn changes | **FAILED** | Genesis restriction and signer pinning work; latest CR-04 can indefinitely strand later retained policy/burn quorum. |
| SC5 — node-scoped services/live burn across SelectAccount | **FAILED** | Latest CR-01 through CR-03 violate complete generation, retired-manager, processing owner, and bridge watcher lifetime safety. |
| SC6 — automated race/tamper/restart/economic gate with no HIGH finding | **FAILED** | Exact evidence is green, but four unmitigated HIGH/release blockers remain. |

### Required Artifacts

| Artifact group | Expected | Status | Details |
|---|---|---|---|
| Canonical codec, manifest, policy, trust store | Deterministic durable trust authority | VERIFIED | Exists, substantive, wired, and represented in focused/broad evidence. |
| SecureCrdt candidate transport | Current-peer-authenticated bounded CRDT flow | VERIFIED | Canonical content addressing, authorization, exact signatures, and caps are wired. |
| `TrustedPeerRegistry.*` / `BurnConfig.*` | Explicit signing and durable activation | PARTIAL | Verification/commit paths work; dispatcher CR-04 can fail to revisit a late retained candidate. |
| `TrustStartupController.*` | Restart-safe, owner-safe, live refresh orchestration | DEFECTIVE | Original raw-worker and transient-error gaps close, but CR-04 loses dirty work and WR-01 weakens prompt timer drain. |
| `GeniusNode.*` / `TransactionManager.*` | Complete, leased account generations and safe bridge owners | DEFECTIVE | CR-01 through CR-03 remain reachable. |
| CLI/composition/runbook | Production operator boundary | VERIFIED | Substantive and linked to production CRDT flow. |
| Phase tests/evidence | Exact cumulative behavior proof | PARTIAL | Exact counts pass; tests omit the reviewed interleavings. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| Genesis manifest | trust store/startup | canonical fingerprint and verified durable snapshot | WIRED | Deterministic identity becomes restart authority. |
| Candidate approval | TPR/Burn store commit | current-policy verification and durable predecessor CAS | WIRED | Correct when activation is attempted. |
| Trust signer label | signing key | immutable `NodeTrustSigner` pair | WIRED | Original CR-12 closed; no mutable selected-account lookup. |
| SelectAccount unpublish | complete manager publication | switching/generation protocol | **NOT WIRED** | CR-01 clears switching at account-only publication. |
| Public transaction snapshot | manager Stop | operation lease/final stopped recheck | **NOT WIRED** | CR-02 permits mutation after retirement. |
| Bridge initializer | bridge owner publication/teardown | lifecycle-locked generation commit | **NOT WIRED** | CR-03 uses unsynchronized shared_ptr members. |
| Late callback request | fresh pending-vector pass | dirty generation/follow-up dispatch | **NOT WIRED** | CR-04 clears the request without another attempt. |

### Data-Flow Trace (Level 4)

| Artifact | Data | Source | Produces real data | Status |
|---|---|---|---|---|
| Genesis/startup | reviewed manifest and approvals | CLI -> CRDT -> store | Yes | FLOWING |
| Policy/burn successor | candidate + exact approvals | CRDT -> verifier -> store -> caches | Yes when dispatched | PARTIAL — CR-04 can strand retained input |
| Transaction burn | confirmed basis points | store -> BurnConfig provider -> TransactionManager -> PayEscrow | Yes | FLOWING for a live manager |
| Selected account generation | account + manager + callbacks | SelectAccount -> async blockchain -> manager publication | Partial tuple is externally observable | **HOLLOW GENERATION (CR-01)** |
| Retired transaction snapshot | strong manager/account owners | public snapshot -> Stop -> mutator | Old data remains mutable | **STALE-WRITABLE (CR-02)** |
| Bridge owners | provider/relayer/watcher | async bridge init -> member publication | Real, but generation commit is unsynchronized | **RACY (CR-03)** |

### Plan 13-29 Evidence Verification

| Evidence | Independent result | Status |
|---|---|---|
| Structural guards | `/tmp/phase13-29-full-task1.log` contains all 9 required PASS groups: prior passive, passive burn, reconstruction no-write, historical path, race, signer, recovery no-write, exhaustion, callback-last-owner. | PASS |
| Current affected build | Fresh current-HEAD build of `trust_first_boot_e2e_test`, `multi_account_test`, and `policy_lifetime_multi_account_test` exited 0 in 3.3s. | PASS |
| Focused XML | Independently parsed the seven focused XML files: 22 cases, 22 unique, zero failure/error/skip children. Product/test sources are unchanged from the evidence commit. | PASS |
| Broad CTest/JUnit | Independently parsed `results.xml`: 25 cases, 25 unique, zero failure/error/skip; selection/log markers show exact-name equality. | PASS |
| Sanitizers | Both sanitizer eligibility/result files are empty; recorded `sanitizer_configured=0 sanitizer_status=NOT_RUN`. | NOT_RUN |
| Lifetime repetitions | Raw task log contains exactly five `passive_lifetime_repeat_N=PASS` markers. | PASS |
| Source-defect countercheck | Direct reachability trace for latest CR-01..CR-04 and WR-01/WR-02. | FAIL — tests do not override reachable defects |

### Review Reconciliation

The identifiers below distinguish the prior verification's original CR-11..CR-14 from the latest review's renumbered CR-01..CR-04.

| Finding | Disposition | Current evidence |
|---|---|---|
| Original CR-11 — account/shared_ptr/catch-up ownership | **PARTIAL, NOT CLOSED** | Coherent snapshots and stale callback checks were added, but latest CR-01..CR-03 expose incomplete publication, missing operation leases, processing owner race, and bridge owner race. |
| Original CR-12 — fixed signer label with mutable account key | CLOSED | `NodeTrustSigner` stores address and strong authority together; selection does not mutate it; both signer tests pass. |
| Original CR-13 — transient listing error silently discarded | NARROW GAP CLOSED, LIVENESS NOT CLOSED | Typed bounded retry exists, but latest CR-04 loses a distinct request that arrives mid-attempt. |
| Original CR-14 — raw worker self-join/UAF | CLOSED WITH WARNING | Independent dispatch state and weak controller close the raw worker path; WR-01 still permits delayed post-destruction timer work. |
| Latest CR-01 — partial generation/overlapping selection | CONFIRMED BLOCKER | `account_service_switching_ = false` at GeniusNode 2560 precedes manager publication at 1028; blockchain callbacks have no generation/identity binding. |
| Latest CR-02 — post-stop mutation/processing owner race | CONFIRMED BLOCKER | Transfer/Mint/Enqueue lack stopped/generation lease; inline processing getter races reset. |
| Latest CR-03 — bridge owner shared_ptr race/stale watcher | CONFIRMED BLOCKER | Initializer and teardown access the same ordinary owner members without one mutex/protocol. |
| Latest CR-04 — coalesced refresh erased | CONFIRMED BLOCKER | `FinishDispatch` clears `coalesced_request` unconditionally and schedules no fresh pass. |
| WR-01 — timer cancel before async_wait arm | CONFIRMED WARNING | Timer is published under mutex at 783-787 and `async_wait` is armed only after unlocking at 789; destructor can cancel in between. |
| WR-02 — CR-11 test blind spot | CONFIRMED WARNING | Test skips null-account/null-manager tuples at 696 and uses direct private helper injection rather than a real watcher callback; selector waits for READY. |

### Threat Accounting

| Threat set | Status | Evidence |
|---|---|---|
| T13-01..T13-12 and T13-G01..G25 | MITIGATED | Canonical identity, store sequencing, current-policy authorization, ingress bounds, durable-first activation, arithmetic, passive activation, reconstruction, and restart evidence remain substantive. |
| T13-G26 / T13-G27 — account ownership and stale async mutation | **UNMITIGATED** | Latest CR-01, CR-02, and CR-03 falsify the claimed complete generation boundary. |
| T13-G28 — signer label/key binding | MITIGATED | Immutable pinned trust signer and cryptographic before/after-switch tests. |
| T13-G29 / T13-G30 — policy/burn discovery liveness | **PARTIAL / UNMITIGATED** | One transient error retries, but latest CR-04 can lose a distinct late trigger. |
| T13-G31 — bounded retry scheduler | PARTIAL | Seven-attempt bounded failure cycle is verified; successful-cycle coalesced dirty work is not preserved. |
| T13-G32 — worker owner lifetime | MITIGATED WITH WR-01 | Raw worker/self-join is gone; timer arm/cancel drain race remains warning-level. |
| T13-11-HOST — whole-disk/all-anchor rollback | ACCEPTED BOUNDARY | Explicitly documented as requiring TPM/OS-keystore monotonic state or authenticated off-host checkpoints. |
| T-13-SC — package supply chain | ACCEPTED | No package/dependency installation in Phase 13. |

### Requirements Coverage

All 15 Phase 13 requirement IDs appear in plan frontmatter; none is orphaned.

| Requirement | Status | Evidence / blocker |
|---|---|---|
| BOOT-01 | SATISFIED | Manual trusted-channel ceremony and non-production labels exist. |
| BOOT-02 | SATISFIED | Canonical manifest binds all required fields and fingerprint. |
| BOOT-03 | SATISFIED | Production genesis persists before policy/economic readiness. |
| BOOT-04 | SATISFIED | Verified persisted state remains restart/LKG authority within the accepted software boundary. |
| POLICY-01 | **BLOCKED** | CR-04 can strand a retained current-policy-authorized successor. |
| VALID-01 | SATISFIED | Signer/candidate bounds and exact threshold floors remain enforced. |
| TEST-01 | **BLOCKED** | Exact gate omits latest CR-01..CR-04 counterexamples; WR-02 makes the account regression accept the partial tuple. |
| SCRDT-04 | SATISFIED | Candidate/approval transport remains CRDT-only. |
| TPR-01 | SATISFIED | Reviewed authenticated TPR genesis is production-wired. |
| TPR-02 | **BLOCKED** | Current-policy verification is correct, but CR-04 can prevent retained membership quorum from activating. |
| BURN-01 | SATISFIED | Burn remains a quorum-signed versioned CRDT value. |
| BURN-02 | **BLOCKED** | CR-01..CR-03 violate safe manager/provider/callback lifetime across SelectAccount. |
| BURN-03 | **BLOCKED** | Genesis default is correct; CR-04 can strand a later quorum-approved live update and account lifetime remains unsafe. |
| MIG-05 | SATISFIED | Approved signature-verification-only scope remains intact. |
| MIG-06 | SATISFIED | Existing ValidatorRegistry behavior remains in the green broad gate. |

**Requirement accounting:** 10 satisfied, 5 blocked (`POLICY-01`, `TEST-01`, `TPR-02`, `BURN-02`, `BURN-03`).

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---:|---|---|---|
| `GeniusNode.cpp` | 2560 | Premature lifecycle-guard clear | BLOCKER | Publishes account/null-manager and permits overlapping generation work. |
| `TransactionManager.cpp` | 571-709, 1120-1139 | Mutators ignore stopped state | BLOCKER | Retired manager can reserve/enqueue work that will never process. |
| `GeniusNode.cpp` | 3631-3780 | Unsynchronized shared owner publication | BLOCKER | C++ data race and stale live watcher. |
| `TrustStartupController.cpp` | 652-666 | Dirty/coalesced work erased | BLOCKER | Retained quorum may never be reconsidered. |
| `TrustStartupController.cpp` | 783-793 | Timer publication/arm gap | WARNING | Cancellation can miss an unarmed wait and delay dispatch drain. |
| Phase-modified legacy files | various | Pre-existing unreferenced TODO comments | INFO | Blame predates Phase 13 and none is in the four trust-goal closure paths; no new marker was introduced by Plans 13-27/28. |

### Behavioral Spot-Checks

| Behavior | Command/evidence | Result | Status |
|---|---|---|---|
| Current affected build | `cmake --build ... --target trust_first_boot_e2e_test multi_account_test policy_lifetime_multi_account_test -j8` | exit 0 | PASS |
| Current test declarations | each affected binary `--gtest_list_tests` | all nine startup, two selected account, and three lifetime declarations active | PASS |
| Historical focused gate | independently parsed seven XML files | 22/22 unique, zero failed/skipped | PASS |
| Historical broad gate | independently parsed JUnit | 25/25 unique, zero failed/skipped | PASS |
| Release-blocker reachability | direct source trace | latest CR-01..CR-04 reachable | FAIL |

### Probe Execution

No probe script or probe-based criterion is declared for Phase 13.

### Human Verification Required

None. The blocking failures are observable source-level concurrency/liveness defects and require implementation plus deterministic regression coverage, not subjective UAT.

### Deferred Items

None. `roadmap.analyze` reports no later v1.1 phase to which any blocker can be conservatively deferred. The whole-disk/all-anchor limitation is an accepted boundary, not a deferred implementation gap.

### Gaps Summary

Four release blockers remain:

1. Keep account switching active until complete pair publication and bind blockchain callbacks to one generation/Blockchain identity.
2. Introduce generation-scoped operation leases/final stopped checks and synchronize processing-service ownership.
3. Atomically publish/retire provider, relayer, and catch-up watcher owners under lifecycle synchronization.
4. Preserve mid-attempt dirty work with a request generation/follow-up pass and add distinct late-candidate barriers.

After those changes, rerun a fresh exact source/XML 26-case gate (the existing 22 plus four new blocker counterexamples, or a larger exact set if split), exact 25-target CTest/JUnit accounting, target-proven sanitizers when available, and the five lifetime repetitions. WR-01 should be fixed and covered with a real steady-timer arm/cancel barrier; WR-02 must be strengthened so it cannot skip the partial tuple or bypass the production watcher path.

---

_Verified: 2026-08-17T14:24:57Z_
_Verifier: the agent (gsd-verifier)_
