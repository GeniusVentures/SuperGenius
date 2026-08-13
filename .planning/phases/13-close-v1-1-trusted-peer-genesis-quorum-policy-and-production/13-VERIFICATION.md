---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
verified: 2026-08-13T15:40:06Z
status: gaps_found
score: 28/32 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 28/32
  gaps_closed:
    - "CR-01: durable initial-burn sequencing bypass"
    - "CR-02: historical burn authority lost after threshold evolution"
    - "CR-03: overflowing PayEscrow burn arithmetic"
    - "CR-04: unauthorized unbounded legacy SecureCrdt signature children"
  gaps_remaining:
    - "Production startup does not initiate or retry deterministic burn v1 after TPR genesis."
    - "Passive nodes do not activate replicated policy candidates at quorum."
    - "LoadAndVerify can observe a torn multi-record transition and report valid state as corrupt."
    - "The no-unmitigated-HIGH security gate is not satisfied."
  regressions:
    - "Previously accepted production first-boot evidence calls BurnConfig::OnTrustedPeerGenesisConfirmed directly from the test."
    - "Previously accepted policy activation evidence calls LocalTrustAdmin::Approve again after quorum solely to trigger activation."
gaps:
  - truth: "D-13 / Roadmap SC4: after durable TPR genesis, production nodes automatically initiate/retry deterministic BurnConfig v1/value 100 and can reach economic readiness"
    status: failed
    reason: "CR-05 confirmed: no production caller invokes OnTrustedPeerGenesisConfirmed; Refresh only waits or retries in-memory IDs, while ordinary burn proposal rejects before readiness. The E2E invokes the missing hook directly."
    artifacts:
      - path: "src/account/TrustStartupController.cpp"
        issue: "Genesis callback only calls Refresh; Refresh never creates/re-discovers the deterministic initial-burn candidate."
      - path: "src/account/BurnConfig.cpp"
        issue: "OnTrustedPeerGenesisConfirmed exists but has no caller under src; ProposeBurnCandidate returns NOT_CONFIRMED while unready."
      - path: "test/src/startup/trust_first_boot_e2e_test.cpp"
        issue: "Lines 211 and 315 call OnTrustedPeerGenesisConfirmed directly, bypassing production orchestration."
    missing:
      - "Idempotently initiate/retry burn v1 from the production controller when a verified BootstrapOnly snapshot is observed."
      - "Add a production-composition test that reaches burn readiness without direct BurnConfig access, including restart recovery."
  - truth: "D-05/D-08/D-12 / Roadmap SC3: the first current-policy quorum-confirmed policy candidate becomes durable on every receiving node without an extra local administrative action"
    status: failed
    reason: "CR-06 confirmed: TrustStartupController registers callbacks only for trusted-peer-genesis and burn-config. Replicated trusted-peer approvals have no callback, so passive nodes never call TryActivatePolicyCandidate."
    artifacts:
      - path: "src/account/TrustStartupController.cpp"
        issue: "No trusted-peer policy-domain candidate callback is registered or unregistered."
      - path: "test/src/startup/trust_first_boot_e2e_test.cpp"
        issue: "After the second approval, line 247 calls admin.Approve again solely to trigger local activation; no passive-node persistence is tested."
    missing:
      - "Register a signer-free trusted-peer callback that attempts policy activation, reports actionable failures, and refreshes after success."
      - "Add a multi-node regression where a passive receiver persists the quorum winner without an additional local admin call."
  - truth: "D-02/D-14/D-15 / Roadmap SC2: LoadAndVerify always evaluates one coherent durable trust snapshot during concurrent valid policy/burn advancement"
    status: failed
    reason: "CR-07 confirmed: public LoadAndVerify takes neither transition_mutex_ nor a RocksDB snapshot and performs independent reads of policy head/history and burn head/history. A valid policy-v2 then burn-v2 transition can therefore be misreported as INVALID_BURN_PROOF and elevated to TRUST_LOCAL_STATE_CORRUPT."
    artifacts:
      - path: "src/trustedpeer/TrustStateStore.cpp"
        issue: "LoadAndVerify at line 436 is unlocked; writers lock only in commit methods and call the same unlocked reader."
      - path: "src/storage/rocksdb/rocksdb.cpp"
        issue: "Each get uses the adapter's ordinary ReadOptions independently; no snapshot is established for the verification sequence."
      - path: "test/src/trustedpeer/trust_state_store_test.cpp"
        issue: "Concurrent-writer tests serialize writers but no deterministic reader/writer interleaving test covers a coherent read view."
    missing:
      - "Use the transition mutex for the entire public read with an internal unlocked helper for commit paths, or use one RocksDB snapshot."
      - "Add a deterministic torn-read regression proving the reader returns a complete old/new snapshot, never corruption."
  - truth: "Roadmap SC6: the automated gate passes with no unmitigated HIGH security finding"
    status: failed
    reason: "The exact 25-test gate and five lifetime repeats are green, and CR-01..CR-04 are closed, but CR-05..CR-07 are independently confirmed production blockers. Security enforcement is configured to block on HIGH. WR-06 also remains an actionable warning."
    artifacts:
      - path: ".planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-REVIEW.md"
        issue: "Current deep review records three critical findings and one warning."
      - path: ".planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-18-SUMMARY.md"
        issue: "The 25/25 evidence covers CR-01..CR-04 gap tests, not the missing production calls or coherent-read concurrency."
    missing:
      - "Close CR-05, CR-06, CR-07, and WR-06 with production-facing regressions, then rerun the exact gate."
---

# Phase 13: Trusted-Peer Genesis, Quorum Policy, and Production Integration Verification Report

**Phase Goal:** Running nodes establish a manually reviewed, authenticated trusted-peer genesis; persist and enforce versioned quorum policy as the restart authority; accept only current-peer-authenticated content-addressed CRDT candidates; apply explicit quorum-approved membership and burn successors; and preserve policy services and live burn behavior across account selection.

**Verified:** 2026-08-13T15:40:06Z
**Status:** gaps_found  
**Re-verification:** Yes — after Plans 13-13 through 13-18 closed the prior four blockers

## Goal Achievement

Plans 13-13 through 13-17 substantively close CR-01 through CR-04, and the independent focused rerun passed all named counterexamples. The phase goal still fails because the current production composition cannot begin initial burn automatically, cannot activate policy quorum on passive nodes, and can classify a valid concurrent durable transition as corruption. The green 25-test gate does not exercise those missing paths.

### Observable Truths

The contract remains the 32 truths established in the initial verification: 31 non-duplicative Plan 13-01..13-12 truths plus Roadmap SC6. Gap-plan truths refine and test those same obligations rather than increasing the phase score denominator.

| # | Source | Truth | Status | Current code/executable evidence |
|---:|---|---|---|---|
| 1 | 13-01 | Canonical genesis identity is deterministic across peer order and rejects malformed/tampered input. | VERIFIED | Canonical codec/manifest are substantive; `genesis_manifest_test` remains in the 25/25 gate. |
| 2 | 13-02 / SC2 | Persisted genesis, policy, and burn heads are one coherent restart authority. | **FAILED** | CR-07: unlocked, unsnapshotted `LoadAndVerify()` can combine an old policy view with a new burn head and return `INVALID_BURN_PROOF`. |
| 3 | 13-02 | Missing/corrupt/forked/failed candidates do not erase durable LKG. | VERIFIED | Typed load/commit errors and tamper/restart tests remain green. |
| 4 | 13-02 | Activation persists synchronously before cache publication. | VERIFIED | Store commits/reloads before TPR/Burn publication; failure regressions pass. |
| 5 | 13-03 | Every SecureCrdt owns an independent policy registry. | VERIFIED | Instance-owned registry and isolation tests are present and wired. |
| 6 | 13-03 | Legacy behavior remains operational and production owners use their node registry. | VERIFIED | TPR/Burn use `secure_crdt_->Registry()`; CR-04 current-member bound is now closed. |
| 7 | 13-04 | Proposal/approval is explicit; receipt/relay never signs. | VERIFIED | Only explicit propose/approve paths call signing callbacks. |
| 8 | 13-04 | Candidate ingress rejects malformed, oversized, wrong-context, and non-current-peer records before retention/notification. | VERIFIED | Shared candidate validator enforces context, membership, signature, and caps. |
| 9 | 13-04 | Content/version/hash keys isolate candidate approvals. | VERIFIED | Canonical candidate IDs and race/cross-candidate tests remain green. |
| 10 | 13-04 | Candidate resources are bounded to 64 KiB, 32 candidates, 256 approvals, and 64 MiB. | VERIFIED | Exact constants and enforcement remain in the candidate path. |
| 11 | 13-05 / SC3 | Current-policy quorum activates the durable TPR/Burn winner on receiving production nodes. | **FAILED** | CR-06: no `trusted-peer` callback exists; live rerun logged “no pattern matches found” for replicated policy approvals. |
| 12 | 13-05 | Non-genesis propose/approve is explicit and receipt does not auto-sign. | VERIFIED | Production/local APIs retain explicit signing boundaries. |
| 13 | 13-05 | Durable predecessor comparison permits one concurrent successor; stale losers do not overwrite cache. | VERIFIED | Transition mutex and direct-store race coverage pass; CR-07 is the separate reader-coherence defect. |
| 14 | 13-05 / SC4 | After durable TPR genesis, production initial peers automatically progress deterministic burn v1/value 100 to readiness. | **FAILED** | CR-05: `OnTrustedPeerGenesisConfirmed` has no production caller; the E2E calls it directly. |
| 15 | 13-06 | Trust tools reuse production GlobalDB/CRDT without GeniusNode or a new topic. | VERIFIED | Reusable production composition is built and used. |
| 16 | 13-06 | Composition owns and stops networking resources in dependency order. | VERIFIED | Substantive Start/Stop implementation and integration tests are present. |
| 17 | 13-07 | Fresh startup networks while waiting, with no active peers/economics. | VERIFIED | Controller fresh-state gating and first-boot checks pass. |
| 18 | 13-07 | Real ceremony transport reaches durable TPR and success-only key cleanup. | VERIFIED | Production GlobalDB ceremony path persists TPR and deletes only after confirmation; the subsequent burn step is Truth 14. |
| 19 | 13-07 | Persisted trust overrides conflicting JSON; network mismatch/corruption fails closed. | VERIFIED | Config conflict/network behavior and historical-authorizer readiness pass; CR-07 is counted in Truth 2's coherent authority contract. |
| 20 | 13-07 | Missing/older/fork replicated data alerts without replacing LKG. | VERIFIED | `ObserveReplicatedSnapshot` and tamper coverage preserve durable state. |
| 21 | 13-08 / SC5 | SelectAccount preserves policy objects, stores, registrations, caches, and callbacks. | VERIFIED | Node/account teardown split and five repeated lifetime runs pass. |
| 22 | 13-08 | Replacement TransactionManager consumes the same provider and fails closed pre-ready. | VERIFIED | Provider injection and `TRUST_POLICY_NOT_READY` path are wired. |
| 23 | 13-08 / SC4 | Approved Burn successors change correct real PayEscrow output; stale/failed candidates do not. | VERIFIED | CR-03 closed with exact quotient/remainder arithmetic and maximum-domain test. |
| 24 | 13-09 / SC1 | `sgns-trust genesis` reviews, signs ephemerally, confirms via SecureCrdt, and cleans up on durable success. | VERIFIED | Ceremony implementation, tests, and CLI are substantive and wired. |
| 25 | 13-09 | Local list/propose/approve is explicit with no remote admin surface. | VERIFIED | LocalTrustAdmin/CLI only; no new RPC or topic. |
| 26 | 13-10 | Operators have an exact trusted-channel/fingerprint ceremony and non-production examples. | VERIFIED | Runbook and example labels are pinned and present. |
| 27 | 13-10 | Runbook explains persisted authority, alerts, and whole-disk rollback boundary. | VERIFIED | Required alerts and external-anchor limitation are explicit. |
| 28 | 13-11 | Canonical metadata changed only after recorded green evidence. | VERIFIED | Plan 13-08 evidence preceded reconciliation; metadata is mechanically present, although current completion claims are now stale. |
| 29 | 13-11 | MIG-05 narrow scope and whole-disk limitation remain intact. | VERIFIED | REQUIREMENTS/PROJECT preserve both constraints. |
| 30 | 13-12 | Policy contains both thresholds and enforces exact bounds/floors. | VERIFIED | Exact integer validators and boundary vectors remain green. |
| 31 | 13-12 | Policy successors require version+1, current hash linkage, and current-policy authorization. | VERIFIED | Structural and direct-store proof validation are implemented. |
| 32 | Roadmap SC6 | Automated unit/race/tamper/restart/cross-node/economic/lifetime gate passes with no unmitigated HIGH. | **FAILED** | 25/25 plus five repeats are green, but CR-05..CR-07 remain unmitigated production blockers. |

**Score:** 28/32 truths verified.

### Required Artifacts

`gsd-sdk query verify.artifacts` reports every declared artifact across Plans 13-01..13-18 present and substantive. Manual wiring/data-flow inspection changes the functional result for the following groups.

| Artifact group | Expected | Status | Details |
|---|---|---|---|
| Canonical codec, manifest, quorum policy | Deterministic identity and exact successor policy | VERIFIED | Built, production-linked, and covered by focused tests. |
| `TrustStateStore.*` | Crash-safe coherent restart/transition authority | **DEFECTIVE** | Atomic writes are correct, but public multi-record verification has no lock/snapshot (CR-07). |
| Candidate SecureCrdt and registry | Authenticated bounded candidate/legacy transport | VERIFIED | Candidate path is bounded; CR-04 outsider/retention closure is substantive and focused tests pass. |
| TPR/Burn activation services | Durable current-policy activation | **PARTIAL** | APIs work when called, but production omits initial-burn initiation and passive policy activation (CR-05/06). |
| `TrustStartupController.*` / GeniusNode | Production orchestration | **DEFECTIVE** | Registers genesis and burn callbacks only; no initial-burn hook or policy callback. |
| TransactionManager | Fail-closed exact live economics | VERIFIED | CR-03 arithmetic closure and provider wiring pass. |
| GlobalDB composition / CLI / runbook | Production transport and operator boundary | VERIFIED | Substantive, linked, and behaviorally exercised. |
| Declared tests | Goal evidence | PARTIAL | All gate tests pass, but two E2E calls bypass missing production behavior and no torn-read test exists. |

### Key Link Verification

| From | To | Status | Evidence |
|---|---|---|---|
| Manifest/policy models | canonical codec/hash/threshold helpers | WIRED | Production calls and focused tests exist. |
| TPR/Burn candidate activation | TrustStateStore | WIRED | Commit-before-publish and current-policy proof paths are present. |
| TrustStateStore verification | one consistent RocksDB view | **NOT WIRED** | Independent `get`/`query` calls; no public read lock or RocksDB snapshot. |
| Genesis callback/controller | deterministic initial burn hook | **NOT WIRED** | Zero `OnTrustedPeerGenesisConfirmed` call sites under `src/`. |
| Policy candidate receipt | `TryActivatePolicyCandidate` | **NOT WIRED** | No `trusted-peer` controller callback; production callbacks cover genesis and burn only. |
| Burn confirmed provider | TransactionManager PayEscrow | WIRED | Ready/value read and exact arithmetic are production call paths. |
| GeniusNode account selection | retained policy services/provider | WIRED | Account-only teardown preserves identities. |

### Data-Flow Trace (Level 4)

| Artifact | Data | Source | Produces real data | Status |
|---|---|---|---|---|
| Genesis ceremony/controller | reviewed manifest and bootstrap approval | CLI -> production GlobalDB -> genesis callback -> store | Yes | FLOWING |
| Initial burn | deterministic burn-v1 approval | Required controller hook -> BurnConfig -> CRDT -> store | No production source | **DISCONNECTED (CR-05)** |
| Policy successors | replicated policy approvals | GlobalDB -> required policy callback -> store/cache | Approvals flow; activation callback absent | **HOLLOW (CR-06)** |
| Restart authority | genesis/policy/burn records | RocksDB multi-key reads -> verified snapshot | Real records, inconsistent possible view | **TORN (CR-07)** |
| Live economics | confirmed basis points | store activation -> shared provider -> PayEscrow | Yes, exact full-domain math | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| CR-01 durable sequencing | focused `trust_state_store_test` two named cases | 2/2 PASS | PASS |
| CR-01 runtime gate | focused `PolicyV2BeforeInitialBurnCannotStrandStartup` | 1/1 PASS, but directly invokes missing burn hook | PASS WITH COVERAGE GAP |
| CR-02 historical authority | focused restart case | 1/1 PASS | PASS |
| CR-03 exact arithmetic | focused maximum-domain PayEscrow case | 1/1 PASS | PASS |
| CR-04 legacy outsider/bounds | focused SecureCrdt cases | 2/2 PASS | PASS |
| Full Phase 13 gate | exact `13-VALIDATION.md` regex | Plan 13-18: selected=25 executed=25 passed=25 failed=0 skipped=0 disabled=0 | PASS, insufficient coverage |
| Account lifetime | five consecutive post-gate runs | lifetime_repeat_1..5=PASS | PASS |
| Initial burn production caller | `rg -n OnTrustedPeerGenesisConfirmed src` | Definition/declaration only; no caller | **FAIL** |
| Passive policy callback | callback registration inspection and live focused log | No trusted-peer callback; replicated approvals report no matching callback pattern | **FAIL** |

### Probe Execution

No probe scripts or probe-based criteria are declared for this phase. Step 7c is not applicable.

### Requirements Coverage

All 15 Phase 13 roadmap requirement IDs appear in plan frontmatter. No requirement is orphaned.

| Requirement | Status | Evidence / blocker |
|---|---|---|
| BOOT-01 | SATISFIED | Trusted-channel ceremony/runbook and example labels exist. |
| BOOT-02 | SATISFIED | Canonical authenticated manifest binds every required field. |
| BOOT-03 | SATISFIED | TPR genesis flows through production SecureCrdt and persists before policy/economic enablement. |
| BOOT-04 | **BLOCKED** | CR-07 can misclassify valid concurrent durable state as corruption; restart/refresh authority is not consistently readable. |
| POLICY-01 | **BLOCKED** | Durable policy format/current authorizer are correct, but CR-06 prevents passive nodes from applying quorum-confirmed policy. |
| VALID-01 | SATISFIED | Peer/candidate bounds and exact policy floors are enforced. |
| TEST-01 | **BLOCKED** | Exact gate is green but bypasses CR-05/06 and has no CR-07 coherent-read regression. |
| SCRDT-04 | SATISFIED | Propose/sign/quorum uses CRDT only; no new transport/RPC. |
| TPR-01 | SATISFIED | Reviewed authenticated production TPR genesis is implemented. |
| TPR-02 | **BLOCKED** | Current-set N-of-M validation exists, but passive receivers do not durably activate the quorum winner. |
| BURN-01 | SATISFIED | Burn values are current-policy quorum-signed CRDT records. |
| BURN-02 | SATISFIED | Node-scoped confirmed provider survives account selection and updates replacement managers. |
| BURN-03 | **BLOCKED** | Exact default/value behavior works once invoked, but normal production startup never creates/retries burn v1. |
| MIG-05 | SATISFIED | Approved signature-verification-only migration remains intact. |
| MIG-06 | SATISFIED | Existing ValidatorRegistry behavior remains green under the approved scope. |

**Requirement accounting:** 10 satisfied, 5 blocked (`BOOT-04`, `POLICY-01`, `TEST-01`, `TPR-02`, `BURN-03`).

### Anti-Patterns and Review Findings

| Finding | Classification | Verification | Impact |
|---|---|---|---|
| CR-01 | CLOSED | Durable guards and focused direct-store regressions pass. | No remaining gap. |
| CR-02 | CLOSED | Restart consumes verified historical `PeerQuorum` classification. | No remaining gap. |
| CR-03 | CLOSED | Overflow-safe exact quotient/remainder arithmetic and max-domain case pass. | No remaining gap. |
| CR-04 | CLOSED | Current-member local/remote gates, pruning, and bound pass. | No remaining gap. |
| CR-05 | BLOCKER | CONFIRMED | Production can remain forever in WaitingForInitialBurn. |
| CR-06 | BLOCKER | CONFIRMED | Passive nodes retain an old policy after replicated quorum. |
| CR-07 | BLOCKER | CONFIRMED | Valid concurrent state can be reported as local corruption. |
| WR-06 | WARNING | CONFIRMED | Refresh discards genesis activation result and pending-burn activation errors. |
| Debt-marker scan | INFO | No `TBD`, `FIXME`, or `XXX` in the reviewed Phase 13 production files. | No separate debt-marker blocker. |

Disconfirmation pass:

- Partial requirement: `TPR-02` validates quorum correctly only when activation is explicitly called; passive production reception does not call it.
- Misleading test: `PolicyV2BeforeInitialBurnCannotStrandStartup` directly calls `OnTrustedPeerGenesisConfirmed` and later calls `admin.Approve` again after quorum, masking CR-05/06.
- Uncovered error path: `Refresh()` suppresses actionable activation errors for pre-existing genesis/burn candidates (WR-06).

### Human Verification Required

None. The blocking gaps are directly observable in production call sites, RocksDB read semantics, focused-test logs, and test-only bypasses. No visual, subjective, or external-service decision is needed.

### Deferred Items

None. `roadmap.analyze` reports no later phase in the v1.1 milestone. CR-05..CR-07 and WR-06 are Phase 13 closure work.

### Gaps Summary

The prior four blockers are genuinely closed, but Phase 13 still cannot ship. Its positive-path gate is green because tests manually trigger two behaviors that production never triggers, and its durable reader is not isolated from concurrent transitions. With `.planning/config.json` set to block on HIGH, CR-05 through CR-07 keep Roadmap SC6 false and block five requirement IDs.

---

_Verified: 2026-08-13T15:40:06Z_
_Verifier: the agent (gsd-verifier)_
