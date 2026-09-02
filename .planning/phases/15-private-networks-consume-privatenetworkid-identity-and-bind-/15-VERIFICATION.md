---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
verified: 2026-09-02T21:38:49Z
status: gaps_found
score: 29/37 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: none
  previous_score: none
  gaps_closed: []
  gaps_remaining: []
  regressions: []
gaps:
  - truth: "A same-PSK peer NOT in the NetworkRegistry membership is rejected at libp2p connection upgrade (gossip host and processing host) — phase-goal clause 'enforces that identity at libp2p connection upgrade'"
    status: failed
    reason: >-
      Owner-documented descope cluster, not an execution failure: plan 15-04 (vendored
      ipfs-pubsub allow-list gater + GossipPubSub surface) was skipped by owner order
      ("forget about 3rdparty directory"); plans 15-05 and 15-08 were executed owner-descoped
      for the membership halves. Verified in code: SetMembershipAllowList occurs 0 times in
      src/ and test/, and the installed vendored gater
      (/Users/henriqueklein/gnus/thirdparty/ipfs-pubsub/src/ipfs_pubsub/deny_list_connection_gater.hpp)
      has no allow-list API. Only credential-possession (pnet PSK) and Noise-only transport
      are enforced at connection level today; a correct-PSK peer outside NetworkRegistry
      membership can still mesh/connect. Owner direction (deferred-items.md §3): future
      enforcement is SuperGenius-side filtering consulting NetworkRegistry::GetCurrentPeers(),
      NOT libp2p gater injection.
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "StartPubSub private branch binds pnet only; no membership predicate consumed (network_registry_ has no runtime consumer inside GeniusNode)"
      - path: "src/processing/impl/processing_core_impl.cpp"
        issue: "Per-subtask host binds Noise-only + pnet + deny-only gater; no membership enforcement point"
      - path: "3rdparty vendored deny_list_connection_gater.hpp (not in repo)"
        issue: "Injectable membership allow-list predicate (15-04 artifact) does not exist; plan skipped by owner"
    missing:
      - "SuperGenius application-layer membership filter at message-handling/peer-evaluation points consulting NetworkRegistry::GetCurrentPeers(), fail-closed on empty membership (deferred-items.md §3 direction)"
      - "Tests for the filter against the existing two-node fixtures (test/src/account/private_network_registry_binding_test.cpp, test/src/pubsub_counts/pubsub_counts.cpp)"
  - truth: "15-04 must-haves: injectable allow-list on DenyListConnectionGater; deny-wins-over-allow; GossipPubSub exposes allow-list pre-Start; extended vendored library reinstalled"
    status: failed
    reason: >-
      Plan 15-04 SKIPPED by owner mid-wave-2; no 15-04-SUMMARY exists; ROADMAP checkbox
      intentionally left unchecked (verified). All four must-haves are unimplemented. If the
      owner's SuperGenius-side-filtering direction (deferred-items.md §3) makes the vendored
      gater extension permanently unnecessary, record an override instead of closing this gap
      with 3rdparty work.
    artifacts:
      - path: "3rdparty vendored gossip_pubsub + deny_list_connection_gater (outside repo)"
        issue: "SetMembershipAllowList surface absent by owner decision"
    missing:
      - "Owner decision at gap closure: implement SuperGenius-side filtering (replacement) or accept the deviation via override"
  - truth: "15-05 must-have: a private node feeds NetworkRegistry cached membership as the GossipPubSub allow-list (SetMembershipAllowList in StartPubSub private branch)"
    status: failed
    reason: "Descoped half of 15-05 truth 1; documented verbatim in 15-05-SUMMARY Descope Record. Construction half delivered and verified; binding half not implemented."
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "No SetMembershipAllowList call (grep-verified 0); network_registry_ membership is not consulted by the gossip host"
    missing:
      - "Application-layer membership consumption equivalent (per deferred-items.md §3)"
  - truth: "15-05 must-have: two same-PSK allow-listed peers mesh and exchange messages; a same-PSK peer NOT in the allow-list is rejected at connection upgrade (PrivateNetworkMembershipGating test)"
    status: partial
    reason: >-
      Same-PSK mesh + message exchange + wrong-PSK/deny-list rejection are proven by the
      unchanged PubsubCounts.PnetIsolationAndGaterBlocking (re-run green this verification),
      but the allow-list-qualified admission/rejection semantics and the runtime-admission
      (live widening) scenario are unimplemented and untestable without the allow-list API.
    artifacts:
      - path: "test/src/pubsub_counts/pubsub_counts.cpp"
        issue: "PrivateNetworkMembershipGating case not added (descoped); file untouched by 15-05"
    missing:
      - "Membership admission/rejection coverage once the application-layer filter exists"
  - truth: "15-08 must-have: the processing host binds the same enforcement as the gossip host INCLUDING the membership gater"
    status: partial
    reason: "Noise-only + pnet + caught-error contract implemented and test-proven (processing_core_gating 4/4); the membership-gater clause is owner-descoped (15-08-SUMMARY Descope Record; deferred-items.md §3)."
    artifacts:
      - path: "src/processing/impl/processing_core_impl.cpp"
        issue: "connection_gater_ is a fresh deny-only gater with no entries and no membership predicate — behaviorally permissive (documented known-stub)"
    missing:
      - "Membership enforcement point on the processing path once the application-layer filter exists, or removal of the inert gater"
  - truth: "Fail-closed identity posture: a provisioned private node cannot silently boot PUBLIC via a network_config.json write/read failure (CR-01 + WR-01, unfixed review findings)"
    status: failed
    reason: >-
      Code-verified unfixed (15-REVIEW.md CR-01 Critical + WR-01): WriteNetworkConfig's
      escaping loop handles only '\\' and '"' (src/account/GeniusNode.cpp:231-244), so a
      canonical swarm-key text containing literal newlines is written as invalid JSON; on
      reload LoadNetworkConfig's parse-error branch (GeniusNode.cpp:1599-1603) — and the
      missing-file branch — return settings with valid==true, so the node boots fully public,
      bypassing every identity validation added this phase. The reviewed fail-closed
      must-haves (malformed id, half-pair) hold only for well-formed JSON.
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "Writer emits control chars unescaped (225-246); parse-error and missing-file paths return valid==true (1588-1603)"
    missing:
      - "Escape/reject JSON string-unsafe chars in WriteNetworkConfig (incl. \\n, \\r, \\t, <0x20)"
      - "Fail closed (settings.valid=false with error log) on unparseable network_config.json per the 15-REVIEW.md CR-01/WR-01 fix sketches"
  - truth: "NetworkRegistry change-callback refresh thread does not busy-spin (WR-02, unfixed)"
    status: partial
    reason: >-
      Code-verified unfixed: refresh_pending_ is set (NetworkRegistry.cpp:417) and read in
      the wait predicate (:445) but never cleared — after the first change notification the
      refresh loop spins continuously (TryConfirm + GlobalDB scans). Not triggered in-node
      today (GeniusNode passes global_db=nullptr, 5-arg New) but exercised by
      network_registry_test CacheRefreshViaCrdtChangeCallback.
    artifacts:
      - path: "src/networkregistry/NetworkRegistry.cpp"
        issue: "refresh_pending_ never stored false; no drain-once or timeout re-wait"
    missing:
      - "Clear the flag after consuming the notification (re-set + timed wait if retry semantics wanted)"
  - truth: "Constructing a second NetworkRegistry for an already-registered network id must not brick the live registry (WR-03, unfixed)"
    status: partial
    reason: >-
      Code-verified unfixed: New maps SecureCrdtRegistry::Register's 'replaced' false to
      std::errc::file_exists (NetworkRegistry.cpp:365-367); the destructor's Unregister then
      removes the NEWLY inserted entry whose token matches, leaving the first live registry
      with no policy entry (subsequent writes fail UNREGISTERED_KEY).
    artifacts:
      - path: "src/networkregistry/NetworkRegistry.cpp"
        issue: "Duplicate-registration failure path destroys the replaced entry of the live registry"
    missing:
      - "Resolve-first non-destructive duplicate check (or restore the replaced entry on failure)"
  - truth: "network-registry/<id> CRDT elements receive the same SecureCrdt ingest verification as trusted-peer-registry/burn-config (WR-04, unfixed)"
    status: partial
    reason: >-
      Code-verified ordering: RegisterFilters() runs at GeniusNode.cpp:~1038 before
      NetworkRegistry::New at :1059, and CRDTDataFilter is accept_by_default — remote
      membership payloads/sig children are accepted into the datastore without the
      canonical-signer ingest filter. TryConfirm's ReadIfQuorum still gates cache
      application (not an authorization bypass) but unfiltered writes are a
      griefing/reset vector for pending bootstrap confirmations.
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "No RegisterFilters() re-run (idempotent) after successful NetworkRegistry::New"
    missing:
      - "Re-run secure_crdt_->RegisterFilters() after NetworkRegistry::New, or register the network-registry pattern's filter at construction"
deferred: # Items addressed in later phases — none: phase 15 is the last phase of the v1.1 milestone section; no later phase covers these
  []
human_verification_recommended:
  - test: "End-to-end two-node private network job flow: provision two GeniusNodes with the same valid private_network_id + network_key + network_bootstrap_peers, publish a job from one, verify it is processed/returned under /chain/<id>/ keys, and verify a public node cannot see the job, the scoped topics, or the scoped validator registry"
    expected: "Private job data exists only under the scoped branch; both private nodes replicate it; the public node observes none of it; both private nodes reach READY"
    why_human: "No automated suite exercises a live multi-node private network end to end; unit/integration suites cover placement, identifiers, and single-node startup only. Requires real multi-process nodes and observation of replicated state."
  - test: "Confirm the owner accepts the current enforcement posture (pnet + Noise-only + startup fail-closed) pending the SuperGenius-side membership filter"
    expected: "Owner decision recorded (override or gap-closure plan) for the D-07/PNET-GATE descope cluster"
    why_human: "Documented owner descope; only the owner can accept the residual same-PSK-unauthorized-peer exposure"
---

# Phase 15: Private Network Identity and libp2p Gating Verification Report

**Phase Goal:** SuperGenius consumes the license NFT's `privateNetworkId` as the self-certifying private-network identity and enforces that identity at libp2p connection upgrade, while isolating chain topics and CRDT keys by network.
**Verified:** 2026-09-02T21:38:49Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

The goal decomposes into three clauses:

| Goal clause | Status | Evidence |
|---|---|---|
| Consumes `privateNetworkId` as the self-certifying private-network identity | VERIFIED | 15-01: `private_network_id` parsed/validated (0x-hex-32B, all-zero reject, D-01 pair rule) at GeniusNode.cpp:1642-1702, retained :1950-1954, fail-closed at InitNetwork :1920; 5-scene suite green |
| Enforces that identity at libp2p connection upgrade | FAILED (owner-documented descope) | Only credential-possession is enforced at upgrade: pnet PSK on both hosts (dial+accept) + Noise-only transport. Identity/membership-based decision has NO enforcement point: `SetMembershipAllowList` = 0 occurrences in src/ and test/; installed vendored gater has no allow-list API (15-04 skipped; 15-05/15-08 descoped). Owner direction: future SuperGenius-side filtering (deferred-items.md §3) |
| Isolating chain topics and CRDT keys by network | VERIFIED | 15-06: /chain/<id>/ branch at all 15 TaskKeys sites (12 explicit + 3 LockKey-by-design), results/ keys, escrow path, scoped processing+grid channels (6 sites), scoped chain id → genius validator; 15-07: ValidatorRegistry key/topic/cid instance-scoped, three-way disjoint, public byte-stable |

### Observable Truths (consolidated across the 8 plans)

| # | Plan | Truth | Status | Evidence |
|---|------|-------|--------|----------|
| 1 | 15-01 | Release build bound to dev_pnets vendored install; pnet pubsub test passes | VERIFIED | CMakeCache `THIRDPARTY_DIR:PATH=/Users/henriqueklein/gnus/thirdparty` (path moved from /gnus/3rdparty by owner mid-phase — documented in deferred-items.md §1 env note; libp2p pin b28eed2 verified at the new path); `pubsub_counts_test` PASSED this verification incl. PnetIsolationAndGaterBlocking |
| 2 | 15-01 | Valid private_network_id → retained + logged (public id only) | VERIFIED | GeniusNode.cpp:1950-1954 (log at :1954 prints id only); test `ValidIdentityPairIsRetainedAndDistinctFromKey` PASSED |
| 3 | 15-01 | Absent id → public node, unchanged | VERIFIED | Guard `!private_network_id_.empty()` (GeniusNode.cpp:1055); test `AbsentKeysKeepPublicNodeBehavior` PASSED |
| 4 | 15-01 | Malformed id fails load with error log, no start | VERIFIED | 66-char 0x-hex + all-zero checks :1665-1687; `settings.valid=false` → InitNetwork aborts :1920; test `MalformedIdentityFailsNodeStart` PASSED. Caveat: only for parseable JSON — see gap 6 (CR-01/WR-01) |
| 5 | 15-01 | Half-provisioned pair fails load naming the missing sibling key | VERIFIED | :1693-1702 (key names only, never the key value); test `HalfProvisionedIdentityPairFailsNodeStart` PASSED |
| 6 | 15-02 | PeerRegistry abstraction, cached-state-only resolution | VERIFIED | src/peerregistry/PeerRegistry.hpp: three pure-virtuals + re-entrancy contract doc + MakeRegistrySignerSetSource |
| 7 | 15-02 | TPR implements PeerRegistry, zero logic change, single global root | VERIFIED | TrustedPeerRegistry.hpp:92 public inheritance; forwarding-only CurrentSignerSet/BaseKey; regex line byte-identical (SecureCrdtRegistry.hpp:124); no per-network TPR instances |
| 8 | 15-02 | SecureCrdtRegistryEntry explicit registry association; key resolution derives signer set from it | VERIFIED | SecureCrdtRegistry.hpp:97 `peer_registry` member + :601 `entry.peer_registry = shared_from_this()`; peer_registry_test PASSED (4 cases) |
| 9 | 15-02 | Existing securecrdt/trustedpeer tests pass unchanged | VERIFIED | This verification: `ctest -R "trustedpeer\|securecrdt\|burnconfig"` → 12/12 PASSED (35.5s) |
| 10 | 15-03 | NetworkRegistry per privateNetworkId on SecureCRDT (no bespoke protocol) | VERIFIED | src/networkregistry/ (602-line cpp); ISignedCRDTData payload; per-network base key `network-registry/<id>` |
| 11 | 15-03 | Bootstrap confirms only at TPR majority; under-signed never confirms | VERIFIED | StrictMajorityQuorumFloor + double ValidateQuorumThreshold (cpp:298,330-347); test `BootstrapUnderTprMajorityConfirms` PASSED |
| 12 | 15-03 | Post-confirm self-governance; single peer can never admit itself | VERIFIED | Tests `SelfGovernanceAfterConfirm`, `SinglePeerCannotAdmitItself` PASSED (dual-identity payload: PeerIds for membership + 128-hex signers — documented in-scope discretion deviation) |
| 13 | 15-03 | Serialized records contain only non-secret metadata | VERIFIED | Payload fields exactly network_peers/network_signers/pnet_key_version/pnet_key_fingerprint; grep for secret-named fields: only doc comments; test `NoRawKeyMaterialInRecords` PASSED |
| 14 | 15-03 | Signer-set resolution cached-only, never re-enters ReadIfQuorum | VERIFIED | ResolveSignerSet body (cpp): shared_lock + cached returns only, no ReadIfQuorum |
| 15 | 15-04 | Injectable allow-list on vendored gater | FAILED | Owner-skipped plan (no SUMMARY; ROADMAP checkbox unchecked by design); API absent from vendored tree |
| 16 | 15-04 | Deny wins over allow | FAILED | Not implemented (skipped) |
| 17 | 15-04 | GossipPubSub exposes allow-list before Start | FAILED | Not implemented (skipped) |
| 18 | 15-04 | Extended vendored library reinstalled | FAILED | Not performed (skipped); vendored tree untouched |
| 19 | 15-05 | Private node constructs NetworkRegistry + feeds membership as gossip allow-list | FAILED (partial) | Construction VERIFIED: GeniusNode.cpp:1055-1083 (guarded, fail-closed via ShutdownNodePolicyServices, public-id-only logging); teardown :2262-2277; binding half NOT implemented (descope) |
| 20 | 15-05 | Same-PSK allow-listed peers mesh + exchange | PARTIAL | Same-PSK mesh/exchange proven by unchanged PnetIsolationAndGaterBlocking (PASSED); allow-list qualifier has no meaning without the API |
| 21 | 15-05 | Same-PSK peer NOT in allow-list rejected at upgrade | FAILED | Descoped; no enforcement point exists (gap 1) |
| 22 | 15-05 | Empty membership fails closed (replaced semantics: startup-level) | VERIFIED | NetworkRegistry::New floor rejects empty set → INITIALIZING_TRANSACTIONS fails, never READY; test `PrivateNodeWithoutBootstrapMembershipFailsClosed` PASSED |
| 23 | 15-05 | Public node: no NetworkRegistry, pubsub unchanged | VERIFIED | Test `PublicNodeConstructsNoNetworkRegistry` PASSED; StartPubSub public branch untouched |
| 24 | 15-06 | TaskKeys optional scope; empty = byte-identical public keys | VERIFIED | Tests `PublicBuildersAreByteStableAcrossForms`, `PublicQueueWithDefaultedScopeKeepsExactPublicKeys` PASSED; zero-arg builders retained |
| 25 | 15-06 | Private scope → /chain/<id>/ branch; distinct ids disjoint | VERIFIED | ScopePrefix via HierarchicalKey("chain").ChildString (TaskKeys.hpp:36-73); tests `ScopePrefixIsChainBranch`, `DistinctPrivateIdsNeverShareAKeyTree` PASSED |
| 26 | 15-06 | Every real key site consumes the scope (15 TaskKeys sites, 3 results sites, escrow path) | VERIFIED | grep TaskQueueImpl.cpp: exactly 15 TaskKeys:: sites — 12 pass network_scope_, 3 LockKey scope-agnostic by design (embeds the already-scoped task key); storage: 3 SubTaskResultKey sites (cpp:25,32,43); escrow: ScopedKeyPath at GeniusNode.cpp:2985; data-path placement tests PASSED |
| 27 | 15-06 | Scoped processing + grid channels at all consumption sites; public strings exact | VERIFIED | AddListenTopic(ScopedProcessingChannel()) at :731,:2799; TaskQueueImpl::New :2082; storage :2089; DHTInit `ScopedProcessingGridChannel() + GetNetAndVersionAppendix()` :2661 (scope first, appendix last); StartProcessing :3495 |
| 28 | 15-06 | Scoped escrow chain id → genius input validator; public byte-identical | VERIFIED | HoldEscrow trailing network_scope (TransactionManager.cpp:795,813-815); SetChainIdOverride (EscrowTransaction.hpp:118-130); SelectInputValidator scoped-prefix branch (TransactionManager.cpp:1384-1391); escrow test scene PASSED |
| 29 | 15-06 | No process-global network setter in new code | VERIFIED | SetNetworkId only at GeniusNode.cpp:400 — pre-existing sgns_config.json net_id handling (introduced 4a72b7e2, 2026-05-20), unrelated to private-network scope; 0 occurrences in TaskKeys.hpp/TransactionManager.cpp |
| 30 | 15-07 | ValidatorRegistry identifiers instance-scoped | VERIFIED | ScopedIdentifier + registry_key_/validator_topic_/registry_cid_key_ members (ValidatorRegistry.hpp:718-731); internal cpp uses read members (only ctor-init + MigrateCids statics remain) |
| 31 | 15-07 | Public instance byte-identical to previous constants | VERIFIED | static_asserts (test:55-57) + `PublicScopeByteStable` PASSED; static constexpr bodies retained |
| 32 | 15-07 | Disjoint scopes — consensus never merges | VERIFIED | Test `DisjointScopes` PASSED; Blockchain support sites derive from instance accessors (deviation-fix verified) |
| 33 | 15-07 | ValidatorRegistry does not authorize NetworkRegistry updates | VERIFIED | No SecureCRDT/NetworkRegistry authorization coupling in the diff; consensus-only |
| 34 | 15-08 | Processing host binds identical enforcement (Noise-only, pnet, membership gater) | PARTIAL | Noise-only + gater-override + conditional pnet VERIFIED (cpp:86-96, both branches); membership gater descoped — connection_gater_ is an inert deny-only instance |
| 35 | 15-08 | Processing host never offers Plaintext | VERIFIED | grep Plaintext = 0 in processing_core_impl.cpp; useSecurityAdaptors<Noise> in both branches |
| 36 | 15-08 | Invalid network key → caught error, never uncaught/half-configured | VERIFIED | try/catch (cpp:182-188) → Error::PNET_INITIALIZATION_ERROR via shared failure tail; test `InvalidKeyFailsEagerly` PASSED |
| 37 | 15-08 | Public node processing path unchanged (defaulted args) | VERIFIED | Trailing defaulted network_key (hpp:114,134); test `DefaultArgumentsKeepPublicConstruction` PASSED |

**Score:** 29/37 truths fully verified; 2 partial (#20, #34); 6 failed (#15-18 = 15-04 skip, #19, #21 = descope cluster)

**Owner-decision accounting (per verification instructions):** all 8 non-verified items trace to the recorded owner decisions — 15-04 skip (deferred-items.md §3 owner order; ROADMAP checkbox intentionally unchecked) and the 15-05/15-08 membership descopes (verbatim Descope Records in both SUMMARYs). They are reported here as gaps for the gap-closure cycle, exactly as directed, not as executor failures. The delivered substitutes in that space are real and verified: pnet PSK binding on both hosts, Noise-only security everywhere, fail-closed startup (empty bootstrap membership → never READY), NetworkRegistry construction/teardown in GeniusNode.

**Review findings weighting (owner decision #6):** 15-REVIEW.md's CR-01 (Critical) + WR-01 undermine the fail-closed must-have posture for one input class (unparseable config / newline-bearing swarm key via WriteNetworkConfig round-trip) — code-verified still unfixed and listed as gap 6. WR-02/WR-03/WR-04 (NetworkRegistry robustness/defense-in-depth) are code-verified unfixed and listed as gaps 7-9. WR-05 and IN-01..IN-04 are accepted as informational (pre-existing conventions or latent-only).

### Deferred Items

None — Phase 15 is the last phase of the current milestone roadmap section; no later phase addresses these gaps (Step 9b: no matches).

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| src/account/GeniusNode.hpp | private_network_id members + NetworkSettings fields | VERIFIED | :1031-1038, :1130-1137; network_registry_ after trusted_peer_registry_ (:954-959) |
| src/account/GeniusNode.cpp | parse/validate/retain/emit | VERIFIED | 34 occurrences; read :1642, validate :1665-1702, retain :1950, emit :248-259 |
| test/src/account/network_config_private_network_test.cpp | ≥80 lines config tests | VERIFIED | 241 lines, 5 scenes, suite PASSED |
| src/peerregistry/PeerRegistry.hpp | PeerRegistry interface | VERIFIED | 3 pure-virtuals + adapter helper |
| src/securecrdt/SecureCrdtRegistry.hpp | per-entry peer_registry association | VERIFIED | :97; regex contract untouched |
| test/src/peerregistry/peer_registry_test.cpp | ≥60 lines | VERIFIED | 173 lines, 4 cases, PASSED |
| src/networkregistry/NetworkRegistry.{hpp,cpp} | ≥150-line cpp | VERIFIED | 602-line cpp; lifecycle + double floor + cached-only resolution |
| test/src/networkregistry/network_registry_test.cpp | ≥120 lines | VERIFIED | 464 lines, 6 cases (5 plan-named + codec/refresh), PASSED |
| 3rdparty vendored gater + gossip surface (15-04) | SetMembershipAllowList surface | MISSING | Owner-skipped plan — reported as gap, not accidental omission |
| test/src/account/private_network_registry_binding_test.cpp | 15-05 replacement scenes | VERIFIED | 211 lines, 3 scenes, PASSED |
| src/processing/impl/TaskKeys.hpp | ScopePrefix et al. | VERIFIED | All 4 helpers + scoped overloads |
| src/processing/impl/TaskQueueImpl.cpp | network_scope_ at all sites | VERIFIED | Member + 12/15 explicit (3 LockKey by design) |
| src/processing/impl/processing_subtask_result_storage_impl.cpp | SubTaskResultKey | VERIFIED | All 3 former results/%s sites |
| src/account/TransactionManager.{hpp,cpp} | ScopedChainId | VERIFIED | :802 decl, :1352 def, HoldEscrow :795/:815 |
| src/account/EscrowTransaction.hpp | SetChainIdOverride + GetChainId | VERIFIED | :118-130, default byte-identical |
| test/src/processing/task_keys_scope_test.cpp | ≥120 lines | VERIFIED | 286 lines, 10 cases, PASSED |
| src/blockchain/ValidatorRegistry.{hpp,cpp} | instance-scoped identifiers | VERIFIED | Members + accessors + ScopedIdentifier; statics retained |
| src/blockchain/impl/Blockchain.cpp + Blockchain.hpp | scope threading | VERIFIED | network_scope stored + forwarded to single creation site |
| test/src/blockchain/validator_registry_scope_test.cpp | ≥60 lines | VERIFIED | 139 lines, 4 cases + static_asserts, PASSED |
| src/processing/impl/processing_core_impl.{hpp,cpp} | gated injector + error enum | VERIFIED | GatedHostContext + MakeGatedHostInjector; PNET_INITIALIZATION_ERROR; Noise+gater+pnet bindings |
| test/src/processing/processing_core_gating_test.cpp | ≥60 lines | VERIFIED | 210 lines, 4 cases, PASSED |

### Key Link Verification

| From | To | Via | Status |
|---|---|---|---|
| GeniusNode.cpp LoadNetworkConfig | NetworkSettings.private_network_id | `read("private_network_id")` beside network_key (:1641-1642) | WIRED |
| GeniusNode.cpp InitNetwork | private_network_id_ | retention + public-id log (:1950-1954) | WIRED |
| TrustedPeerRegistry.hpp | PeerRegistry.hpp | public inheritance + overrides (:92) | WIRED |
| SecureCrdtRegistry.hpp | PeerRegistry | peer_registry shared_ptr member (:97) | WIRED |
| NetworkRegistry.cpp | TrustedPeerRegistry | bootstrap authority + majority floor | WIRED |
| NetworkRegistry.cpp | SecureCrdt.hpp | propose/sign/ReadIfQuorum (not on resolve path) | WIRED |
| NetworkRegistry.cpp | QuorumThresholdValidation | ValidateQuorumThreshold ×2+ (:330-347) | WIRED |
| GeniusNode StartPubSub | GossipPubSub::SetMembershipAllowList | pnet constructor only (:1850) | NOT WIRED — descoped (gap) |
| GeniusNode INITIALIZING_TRANSACTIONS | NetworkRegistry::New | :1059, guarded, fail-closed | WIRED |
| TaskKeys.hpp | hierarchical_key.hpp | ChildString (:73) | WIRED |
| TaskQueueImpl 15 sites | scoped overloads | network_scope_ member | WIRED (12 explicit + 3 by design) |
| GeniusNode AddListenTopic ×2 | ScopedProcessingChannel | :731, :2799 | WIRED |
| GeniusNode DHTInit/StartProcessing | ScopedProcessingGridChannel | :2661, :3495 | WIRED |
| GeniusNode ProcessImage | TaskKeys::ScopedKeyPath | :2985 + task.set_escrow_path | WIRED |
| TransactionManager HoldEscrow | ScopedChainId via SetChainIdOverride | :813-815 | WIRED |
| GeniusNode INITIALIZING_BLOCKCHAIN | Blockchain::New(private_network_id_) | :795-798 | WIRED |
| Blockchain.cpp | ValidatorRegistry::New(network_scope) | :124-163 | WIRED |
| GeniusNode InitProcessingModules | ProcessingCoreImpl::New(network_key_) | :2086 | WIRED (membership predicate descoped) |
| processing_core_impl.cpp | DenyListConnectionGater | di::bind override (:87,:95) | WIRED |

### Data-Flow Trace (Level 4)

Not a UI phase; the equivalent check is scope/identity flow from config to data-path sites: `private_network_id_` (config) → NetworkRegistry base key, TaskQueueImpl keys, storage keys, escrow path, chain id, channels, DHT CID, ValidatorRegistry identifiers — each traced to a consuming site above (all FLOWING). One intentional dead-end: `network_registry_->GetCurrentPeers()` is logged at startup (:1082) but has no enforcement consumer inside GeniusNode (descope; gap 1).

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| 8 phase-15 suites | `ctest -R "network_config_private_network\|network_registry_test\|peer_registry_test\|task_keys_scope_test\|validator_registry_scope_test\|processing_core_gating_test\|private_network_registry_binding\|pubsub_counts"` | 8/8 PASSED (59.9s) | PASS |
| Quorum-trio regression | `ctest -R "trustedpeer\|securecrdt\|burnconfig"` | 12/12 PASSED (35.5s) | PASS |
| Build bound to dev_pnets install | `grep THIRDPARTY_DIR:PATH CMakeCache.txt` | /Users/henriqueklein/gnus/thirdparty (moved path; libp2p b28eed2 verified) | PASS |
| D-10 merge executed | `git merge-base --is-ancestor 2c05a8fe HEAD` | true (also dc7b40f1) | PASS |
| 15-04 descope greps | `grep -rn SetMembershipAllowList src/ test/` | 0 occurrences; vendored header has 0 | PASS (confirms descope, not partial work) |
| No Plaintext on processing host | `grep -c Plaintext processing_core_impl.cpp` | 0 | PASS |
| No secret fields in registry payload | `grep network_key\|password\|secret NetworkRegistry.hpp` | 3 doc-comment matches only | PASS |
| Binary freshness vs source | test_bin timestamps (Sep 2 18:10) > last src commit (17:56); working tree modifies only docs | build current | PASS |

Pre-existing failures NOT counted (deferred-items.md §1, A/B-verified by 15-01/15-06/15-07): no-genesis READY stall affecting network_config_precedence, node_type_derivation, blockchain_genesis (2 scenes), migration_sync (3 scenes), processing_nodes, full_node, node_initialization_progress, genius_node_bootstrap_reconnect. Not run here (13+ min); attributed per documented evidence.

### Probe Execution

SKIPPED — no probe scripts declared (no `scripts/*/tests/probe-*.sh` in repo; plan verification uses grep/ninja/ctest, all executed above).

### Requirements Coverage

REQUIREMENTS.md contains no PNET-* or D-* entries (phase requirements are TBD-formal, derived from 15-CONTEXT.md decisions + research codes, cited per plan). Accounting by plan-cited IDs:

| Requirement | Source Plan(s) | Status | Evidence |
|---|---|---|---|
| D-01 | 15-01 | SATISFIED | network_config.json source of identity+key; pair rule enforced |
| D-02 | 15-01, 15-06 | SATISFIED | distinct public id vs secret; id drives paths/topics/chain-ids |
| D-03 | 15-03 | SATISFIED | secret-free payload, test-proven sentinel exclusion |
| D-04 | 15-02 | SATISFIED | PeerRegistry abstraction + per-key association |
| D-05 | 15-02, 15-03 | SATISFIED | TPR sole global root; additive only |
| D-06 | 15-03, 15-05 | SATISFIED | registry + GeniusNode wiring, TPR-majority bootstrap, self-governance |
| D-07 | 15-04, 15-05 | DESCOPE GAP | membership allow-list consulted at upgrade NOT implemented; only pnet possession layer exists |
| D-08 | 15-06 | SATISFIED | explicit scope at every job-derived site; public scope 0 unchanged |
| D-09 | 15-07 | SATISFIED | per-scope validator state/quorum, disjoint, byte-stable public (note IN-03: consensus topic + /cert/ keys still public — review informational) |
| D-10 | 15-01 | SATISFIED | blocking gate executed; merge-closeout-first (2c05a8fe verified ancestor); encoding 0x-hex-32B recorded verbatim |
| D-11 | 15-08 | PARTIAL | both hosts Noise-only + pnet; membership clause descoped (15-08-SUMMARY requirements-completed deliberately empty) |
| PNET-CFG | 15-01 | SATISFIED | (caveat: CR-01/WR-01 fail-open path open) |
| PNET-REG | 15-02 | SATISFIED | |
| PNET-NETREG | 15-03, 15-05 | SATISFIED | |
| PNET-GATE | 15-04, 15-05 | DESCOPE GAP | see gaps 1-5 |
| PNET-SCOPE | 15-06 | SATISFIED | |
| PNET-VAL | 15-07 | SATISFIED | |
| PNET-PROC | 15-08 | PARTIAL | Noise+pnet+error contract done; membership descoped |

No orphaned requirements: every plan-cited ID is accounted for above.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| src/account/GeniusNode.cpp | 2976 | Pre-existing `//TODO - Make it async...` (introduced 4a72b7e2, 2026-05-20 — NOT phase-15 debt) | Info | None new; phase-15 files contain zero TBD/FIXME/XXX |
| src/processing/impl/processing_core_impl.cpp | 125 | Inert `connection_gater_` (deny-only, no entries, no membership predicate) | Warning | Documented owner-descope known-stub; bound into composition but behaviorally permissive |
| src/account/GeniusNode.cpp | 1599-1603 | Parse-error path returns valid==true (fail-open) | Blocker (review CR-01/WR-01) | Private-provisioned node can boot public |
| src/account/GeniusNode.cpp | 231-244 | Escaping loop misses control chars | Blocker (review CR-01) | Newline-bearing swarm keys → invalid JSON → gap above |
| src/networkregistry/NetworkRegistry.cpp | 417/445 | refresh_pending_ never cleared | Warning (review WR-02) | Busy-spin thread when global_db wired (library API; tests hit it) |
| src/networkregistry/NetworkRegistry.cpp | 365-367 | file_exists failure path unregisters live entry | Warning (review WR-03) | Duplicate construction bricks existing registry |
| src/account/GeniusNode.cpp | 1038 vs 1059 | RegisterFilters before NetworkRegistry::New | Warning (review WR-04) | network-registry keys ingested without SecureCrdt filter |

### Human Verification Recommended

(See frontmatter `human_verification_recommended`.) The two items: (1) live two-node private-network end-to-end job flow with a public-node control — no automated multi-node private-network scene exists; (2) owner acceptance of the current enforcement posture pending the SuperGenius-side membership filter.

### Override Suggestion (for the owner)

Gaps 2-5 (the 15-04 skip + 15-05/15-08 membership descopes) look permanent under the owner's SuperGenius-side-filtering direction. If the owner wants to close the phase WITHOUT a libp2p gater, add to this file's frontmatter:

```yaml
overrides:
  - must_have: "Enforces that identity at libp2p connection upgrade (membership allow-list at connection upgrade)"
    reason: "Owner direction 2026-09-02 (deferred-items.md §3): membership filtering moves to the SuperGenius application layer; 15-04 vendored gater permanently off the table. pnet PSK + Noise-only + startup fail-closed remain as the connection-level enforcement."
    accepted_by: "henrique"
    accepted_at: "{ISO timestamp}"
```

Otherwise the gap-closure plan should implement the application-layer filter per deferred-items.md §3.

### Gaps Summary

Two gap clusters, both pre-documented by the owner:

1. **Enforcement-at-upgrade cluster (goal clause 2):** the phase goal's "enforces that identity at libp2p connection upgrade" is not met at the identity/membership level. 15-04 was skipped and 15-05/15-08 were executed descoped; what IS enforced at connection level is PSK possession (pnet, both hosts, dial+accept) and Noise-only transport, plus a startup-level fail-closed (empty membership → never READY). Gap closure = SuperGenius-side membership filtering consulting `NetworkRegistry::GetCurrentPeers()`, fail-closed, tested against the existing two-node fixtures (frontmatter gaps 1-5).
2. **Fail-closed robustness cluster (review findings, code-verified unfixed):** CR-01+WR-01 (WriteNetworkConfig control-char escaping + fail-open parse path — a private node can silently boot public), WR-02 (refresh busy-spin), WR-03 (duplicate-New clobbers live registry), WR-04 (no ingest filter for network-registry keys) (frontmatter gaps 6-9).

Everything else the phase set out to do is in the codebase and green: config identity with fail-closed validation, PeerRegistry abstraction, NetworkRegistry trust lifecycle, full job-scope propagation (keys/topics/channels/escrow/chain-ids), per-scope ValidatorRegistry — 29/37 must-haves verified with 20/20 targeted test suites passing this verification.

---

_Verified: 2026-09-02T21:38:49Z_
_Verifier: Claude (gsd-verifier)_
