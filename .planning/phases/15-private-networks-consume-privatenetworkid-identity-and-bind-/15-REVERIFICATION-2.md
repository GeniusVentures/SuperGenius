---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
verified: 2026-09-03T19:41:50Z
status: gaps_found
score: 9/10 must-haves verified (cycle-2 blockers/warnings all closed; 1 new critical fail-open on the failure path)
overrides_applied: 2
overrides:
  - must_have: "Enforces that identity at libp2p connection upgrade (membership allow-list at connection upgrade)"
    reason: "Owner direction 2026-09-02 (deferred-items.md §3, binding): membership enforcement is SuperGenius-side application-layer filtering (authenticated ingest gates at all four inbound surfaces), NOT a libp2p gater allow-list; plan 15-04's vendored-gater approach is permanently off the table. pnet PSK + Noise-only transport + startup fail-closed remain the connection-level layers. Enforcement truths are scored against this REPLACEMENT posture (now keypair-sealed-envelope authenticated after 15-14)."
    accepted_by: "henrique"
    accepted_at: "2026-09-03T17:55:00Z"
  - must_have: "15-04 must-haves: injectable allow-list on DenyListConnectionGater; deny-wins-over-allow; GossipPubSub exposes allow-list pre-Start; extended vendored library reinstalled"
    reason: "Plan 15-04 permanently skipped by owner order (no 3rdparty modifications); the four must-haves describe the vendored-gater surface that is permanently unnecessary under the app-layer replacement (delivered by 15-11..15-15). ROADMAP checkbox intentionally left unchecked for 15-04."
    accepted_by: "henrique"
    accepted_at: "2026-09-03T17:55:00Z"
re_verification:
  previous_status: gaps_found
  previous_score: "8/9 prior gaps closed; 2 new critical (CR-G01, CR-G02)"
  gaps_closed:
    - "CR-G01 — authenticated (keypair-sealed envelope) membership gates at all four surfaces + sign_messages=true at both production sites + forged-from/impostor proofs (15-14)"
    - "CR-G02a — filter+key passed into ProcessingNode::New, installed before Listen() and before CreateResultsChannel/ConnectToSubTaskQueue at both creation sites (15-15)"
    - "CR-G02b / G-WR-03 — bootstrap-membership interim filter installed in INITIALIZING_DATABASE strictly before the first AddListenTopic; registry-backed replace (15-15)"
    - "G-WR-01 — teardown removes the GlobalDB ingest element filter, token-guarded (15-16)"
    - "G-WR-02 — RegisterCrdtChangeCallback failure fails New with address_in_use (15-16)"
    - "G-WR-04 — SecureCrdtRegistry::RegisterIfAbsent atomic-detecting insert (15-16)"
    - "IN-01 — failed dynamic_pointer_cast no-ops now warn (15-15)"
  gaps_remaining:
    - "Fail-closed throughout is STILL not delivered end-to-end: CR-C2-01 — on NetworkRegistry::New (or BurnConfig/RegisterFilters) failure mid-boot, ShutdownNodePolicyServices CLEARS the interim deny-all filter on a still-live GlobalDB (GeniusNode.cpp:2376), restoring public pass-through ingest on a node parked in INITIALIZING_TRANSACTIONS with PubSub running and topics subscribed. The error log at :1129-1136 claims 'failing closed' while the gate is being opened."
  regressions: []
gaps:
  - truth: "Enforcement is fail-closed throughout — no code path on a private node restores public pass-through ingest while its GlobalDB is still live"
    status: failed
    reason: >-
      CR-C2-01 (code-verified this pass; unfixed — last commit is the cycle-2 review report 763dc93d,
      no code commits after it). Every policy-stack failure path in INITIALIZING_TRANSACTIONS —
      NetworkRegistry::New failure (GeniusNode.cpp:1127-1138, now MORE reachable because 15-16's
      G-WR-02 fix makes New fail on a duplicate change-callback pattern), BurnConfig failure
      (:1080-1084), RegisterFilters failure (:1090-1094) — calls ShutdownNodePolicyServices(),
      whose first broadcaster action is ClearMembershipFilter() (:2376). That clears the interim
      deny-all filter installed at INITIALIZING_DATABASE (:774-784) and restores PUBLIC pass-through
      ingest (OnMessage raw branch) on a GlobalDB that is never shut down on this path — the node
      merely parks in INITIALIZING_TRANSACTIONS with PubSub running and topics subscribed
      (AddListenTopic at :785 from INITIALIZING_DATABASE; quorum topic from INITIALIZING_BLOCKCHAIN).
      Exposure: any same-PSK peer (the accepted adversary model — no forging needed, raw unsealed
      publishes suffice) can push arbitrary CRDT deltas/CID announces into the stalled node's
      durable datastore for as long as it runs; pollution persists across restart. Compounding:
      the same handler resets secure_crdt_ (:2401) while tx_globaldb_ lives on, and the SecureCrdt
      element filters fail OPEN on weak_ptr expiry (SecureCrdt.cpp:686 returns nullopt = accept —
      cycle-2 WR-01), so the stalled node's network-registry/trust/burn-config branches also accept
      unsigned remote elements. Reachability is test-proven: PrivateNodeWithoutBootstrapMembershipFailsClosed
      (private_network_registry_binding_test.cpp:197) drives exactly this path and holds the stalled
      node alive for 15s in INITIALIZING_TRANSACTIONS. The full-shutdown path (:2461-2464) has the
      same clear followed by ShutdownNow() three lines later — a smaller window, and clearing is
      still more permissive than leaving the expired-registry/interim filter installed (an
      expired-registry predicate already deny-alls fail-closed). This is a regression relative to
      the pre-15-15 behavior (registry degrade kept the interim/registry filter installed) and
      contradicts the fail-closed posture the error log itself claims.
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: ":2376 ClearMembershipFilter() in ShutdownNodePolicyServices is unconditional — clears the interim deny-all gate on GlobalDBs that keep running on the :1084/:1094/:1137 failure paths; :2401 secure_crdt_.reset() while tx_globaldb_ lives exposes WR-01's fail-open element filters on the same stalled node"
      - path: "src/securecrdt/SecureCrdt.cpp"
        issue: ":679-687 element-filter lambda returns std::nullopt (ACCEPT) when weak_self expires — fail-open, contradicting the candidate filter's reject-on-expiry (:695-703)"
    missing:
      - "In ShutdownNodePolicyServices, do not clear to pass-through on paths where the GlobalDB keeps running: on a private node (!private_network_id_.empty()) replace the clear with an explicit deny-all SetMembershipFilter (or leave the interim MakeBootstrapMembershipFilter installed); keep/gate the clear for the full-destruction path (:2461) where ShutdownNow() follows"
      - "Mirror the candidate filter's expiry policy in SecureCrdt::RegisterFilters' element lambda: return std::vector<sgns::crdt::pb::Element>{} (reject) when weak_self has expired, so an expired policy owner fail-closes its branches on a live GlobalDB (cycle-2 WR-01 fix)"
      - "Add a regression: drive NetworkRegistry::New failure (empty bootstrap set) and assert the broadcaster still HasMembershipFilter() (or denies) on the stalled node — PrivateNodeWithoutBootstrapMembershipFailsClosed currently asserts only the state stall and null registry"
warnings:
  - "WR-C2-02 (grid caller-ordering contract): ProcessingServiceImpl::Listen subscribes the grid channel unconditionally and OnMessage consults the filter at message time — an undocumented ordering contract (SetMembershipFilter before StartProcessing). Production orders correctly (GeniusNode.cpp:3624-3630); the test processing_service_test.cpp:440/453 exercises the inverted order. Defense-in-depth or doc fix, not a production defect."
  - "WR-C2-03 (clear cannot propagate): processing_service.cpp:139/:161 guard propagation with the payload's truthiness (if (node && filter) / if (node && key_copy)) — an empty filter/null key updates the service snapshot but silently skips every existing ProcessingNode; asymmetric with every other gate surface where empty = clear. Latent (no production clear-through-service call today)."
  - "WR-C2-04 (SecureCrdtRegistry::Register re-lock race): SecureCrdtRegistry.hpp:121-137 — insert_or_assign after re-locking can destroy a concurrently-inserted entry while registry_mutex_ is held (destructor re-entrancy deadlock/UB). Latent (single-threaded production wiring) and pre-existing; RegisterIfAbsent covers the NetworkRegistry path."
  - "WR-C2-01 element-filter fail-open is folded into the blocker above (its exposure is dominated by the CR-C2-01 stalled-node path); the healthy-shutdown window (:2461 reset before :2464 ShutdownNow) remains a live-window instance of the same wrong expiry policy."
human_verification_recommended:
  - test: "End-to-end two-node private network job flow (carried forward from 15-VERIFICATION.md / 15-REVERIFICATION.md): provision two GeniusNodes with the same valid private_network_id + network_key + network_bootstrap_peers, publish a job from one, verify processing/replication under /chain/<id>/ keys, and verify a public node observes none of it"
    expected: "Private job data exists only under the scoped branch; both private nodes replicate it; the public node observes none of it; both private nodes reach READY with filters installed"
    why_human: "No automated suite exercises a live multi-process private network end to end; full GeniusNode E2E is additionally blocked by the pre-existing no-genesis READY-stall base failures (deferred-items.md §1). The forged-from/impostor/creation-window/startup-window flow scenes provide the cheap automatable partial coverage (all green this verification)."
---

# Phase 15: Private Network Identity and libp2p Gating — RE-VERIFICATION 2 Report (Cycle 2)

**Phase Goal:** SuperGenius consumes the license NFT's `privateNetworkId` as the self-certifying private-network identity and enforces that identity at libp2p connection upgrade, while isolating chain topics and CRDT keys by network.
**Verified:** 2026-09-03T19:41:50Z
**Status:** gaps_found
**Re-verification:** Yes — third pass, after cycle-2 gap-closure plans 15-14/15-15/15-16 (all executed, SUMMARYs present, commits verified in git log)
**Scored against:** the owner-sanctioned replacement posture (authenticated keypair-sealed-envelope membership gates at all four inbound surfaces + no enrollment windows), per the standing overrides.

## Verdict in One Paragraph

Both cycle-2 blockers and all three cycle-1 warnings are substantively CLOSED and code-verified: the four gates now authenticate the sender (envelope key <-> PeerId(from) binding + signature) BEFORE consulting membership, sign_messages=true at both production sites, no ProcessingNode subscription can go live before its filter, the private-node boot window is covered by an interim bootstrap filter, and the registry lifecycle (filter teardown, fail-closed callback registration, atomic registration) has mutation-verified regression proof — 14 suites green this verification, zero regressions. **However, the fix for G-WR-02 interacted with the 15-15 interim gate to create a new fail-open path (CR-C2-01, cycle-2 review, confirmed unfixed in code at HEAD): when NetworkRegistry::New fails mid-boot, ShutdownNodePolicyServices CLEARS the interim deny-all filter on a GlobalDB that keeps running — a stalled private node in INITIALIZING_TRANSACTIONS with PubSub live and topics subscribed ingests PUBLIC pass-through, while its own log claims "failing closed."** On that same path secure_crdt_ is reset and the SecureCrdt element filters fail open (accept on weak_ptr expiry). The exposure is reachable by the exact adversary the posture targets (any same-PSK peer, raw unsealed publishes) and is driven by the existing test PrivateNodeWithoutBootstrapMembershipFailsClosed. One critical gap remains; the "fail-closed throughout" clause of the accepted posture is still not delivered end-to-end.

## Goal Achievement

| Goal clause | Status | Evidence |
|---|---|---|
| Consumes `privateNetworkId` as the self-certifying private-network identity | VERIFIED (regression) | network_config_private_network_test 7/7 green this verification (incl. PrivateNodeWithoutBootstrapMembershipFailsClosed + both 15-10 fail-closed scenes); no cycle-2 code touched the identity-consumption chain |
| Enforces that identity (accepted posture: authenticated membership gates at all four inbound surfaces, no enrollment windows) | PARTIAL — 1 critical residual | Gates authenticated and wired at all four surfaces (CR-G01 VERIFIED below); enrollment/startup windows closed (CR-G02 VERIFIED below); BUT fail-closed-throughout is broken on the policy-stack failure path (CR-C2-01: filter cleared on a still-live GlobalDB + element filters fail open) — new blocker |
| Isolating chain topics and CRDT keys by network | VERIFIED (regression) | task_keys_scope_test, validator_registry_scope_test green this verification; no cycle-2 code touched the scope plumbing (git diff d541fa4b..HEAD on src/ is empty — only docs commits after) |

## Cycle-2 Gap Accounting (CR-G01 / CR-G02 / G-WR-01 / G-WR-02 / G-WR-04)

| # | Item | Status | Evidence (code + tests, this verification) |
|---|---|---|---|
| 1 | CR-G01 — gates authorized on unauthenticated from/declared peer | **CLOSED** | src/base/gossip_auth.hpp (336 lines; magic + SealGossipPayload + OpenGossipPayload + DeriveGossipFromBytes; includes clean — only libp2p crypto/peer + std, no networkregistry/crdt). OpenGossipPayload BEFORE membership at all four gates: pubsub_broadcaster_ext.cpp:184, processing_service.cpp:282, processing_subtask_queue_accessor_impl.cpp:470, processing_subtask_queue_channel_pubsub.cpp:212. All 8 publish sites seal (broadcaster :426; grid :227/:604/:802; results :294/:613; queue :103/:153). sign_messages=true at GeniusNode.cpp:1920 and GlobalDbNetworkComposition.cpp:200 (no remaining false). Key wiring: gossip_signing_keypair_ retained, broadcaster SetGossipSigningKey in InitDatabase (:2174-2180) + processing service inside StartProcessing guard (:3626). Proofs green: GossipPayloadAuthDecisionTable.SealOpenForgeAndTamperCases, MemberImpostorEnvelopeIsDroppedByGatedIngest, UnsignedPayloadFromMemberIsDroppedUnderFilter, QueueChannelImpostorEnvelopeIgnored (all listed in --gtest_list_tests; suites 4/4 + 6/6 green). Envelope crypto soundness independently reviewed (15-REVIEW-CYCLE2 axis 2 — PeerId multihash derivation consistent, replay/relay under a different from fails the binding). Residual accepted: envelope not bound to topic/freshness (T-15-14-04, documented) |
| 2 | CR-G02a — creation window (queue/results subscribe before filter) | **CLOSED** | ProcessingNode::New takes trailing membershipFilter + gossipSigningKey (processing_node.hpp:72-73); queue-channel install at processing_node.cpp:225-231 BEFORE Listen at :291; accessor install at :249-255 BEFORE CreateResultsChannel/ConnectToSubTaskQueue at :303-305. Both creation sites snapshot under m_membershipFilterMutex and pass into New (processing_service.cpp:485-491+526-527 and :975-981+1021-1022); zero post-hoc node->SetMembershipFilter in the creation-site bodies; exactly 1 occurrence in the file (:141 — the preserved live-refresh loop). Contract rewritten truthfully (processing_service.hpp:77-97 "installed BEFORE any subscription goes live"). Mutation-verified scene CreationTimeFilterCoversSubscriptionWindow green (suite 6/6) |
| 3 | CR-G02b / G-WR-03 — startup window (GlobalDB ingest live before registry filter) | **CLOSED** (happy path; failure path scored under CR-C2-01) | MakeBootstrapMembershipFilter (empty set denies all) installed in INITIALIZING_DATABASE inside the !private_network_id_.empty() guard at GeniusNode.cpp:774-784, STRICTLY BEFORE AddListenTopic(:785); registry-backed replace unchanged at :1151-1155. BootstrapMembershipFilterSemantics.ConfigBackedFailClosedPredicate + StartupWindowFilterCoversGlobalDBIngest green. No wedge: registry replace is unconditional on the success path; SelectAccount reuses the already-filtered broadcaster |
| 4 | G-WR-01 — teardown leaves the GlobalDB ingest filter installed | **CLOSED** | SecureCrdt::UnregisterFiltersFor (SecureCrdt.cpp:711-720) uses the single shared IngestFilterPatternFor construction helper (:80; used by install :676 AND removal :720 — cannot drift). NetworkRegistry::Unregister removes it LAST (step 4, NetworkRegistry.cpp:729-735), token-guarded on UnregisterIf's removal result so a duplicate-New loser can never strip a live registry's filter. TeardownRemovesIngestFilter green (network_registry_test 18/18 this verification) |
| 5 | G-WR-02 — silent live-refresh degradation on callback-registration failure | **CLOSED** | RegisterCrdtChangeCallback returns bool (NetworkRegistry.cpp:487); New fails address_in_use on false (:407-424) with explicit instance->Unregister() cleanup (self-pin cycle fix). CallbackRegistrationFailureFailsNew green. NOTE: this closure is what widens CR-C2-01's reachability — see blocker |
| 6 | G-WR-04 — duplicate-New TOCTOU | **CLOSED** | SecureCrdtRegistry::RegisterIfAbsent (SecureCrdtRegistry.hpp:158+) — find-then-emplace under one continuous lock hold, no replace path, no destruction under the mutex; RegisterSignerSetSource uses it (NetworkRegistry.cpp:484). RegisterIfAbsentDoesNotReplaceLiveEntry green (mutation-verified per 15-16 SUMMARY) |

**Cycle-2 gap score: 5/5 closed.** IN-01 (silent no-op casts) also closed (4 warn logs in processing_node.cpp). Residual dispositions unchanged: WR-05 deferred per 15-11; IN-02/IN-03 optional hardening per ROADMAP note.

## Observable Truths (scored against the accepted posture)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | CR-G01 closed: under a set filter, a message whose sender identity cannot be authenticated (missing envelope / key-PeerId mismatch / invalid signature) is denied at all four gates even when from names a member; members cannot impersonate each other; both production gossip sites sign | VERIFIED | Code + tests above (row 1 of cycle-2 accounting) |
| 2 | CR-G02a closed: no ProcessingNode subscription goes live before its filter is installed | VERIFIED | Code + tests above (row 2) |
| 3 | CR-G02b closed: private node's GlobalDB ingest is membership-gated from its first topic subscription | VERIFIED | Interim filter before first AddListenTopic (row 3); the failure-path clearing of this same filter is scored under truth 7 |
| 4 | G-WR-01 closed: teardown removes the ingest element filter it installed | VERIFIED | Row 4 |
| 5 | G-WR-02 closed: callback-registration failure fails New (no silent degradation) | VERIFIED | Row 5 |
| 6 | G-WR-04 closed: registration is atomic-detecting (no live-entry replacement) | VERIFIED | Row 6 |
| 7 | Fail-closed throughout: no code path on a private node restores public pass-through ingest while its GlobalDB is still live | **FAILED (BLOCKER — CR-C2-01)** | GeniusNode.cpp:2376 unconditional ClearMembershipFilter in ShutdownNodePolicyServices, reached from the :1084/:1094/:1137 failure paths that leave tx_globaldb_ running indefinitely (stalled INITIALIZING_TRANSACTIONS node, PubSub live, topics subscribed); log at :1129-1136 claims "failing closed". Compounded by :2401 secure_crdt_.reset() exposing SecureCrdt.cpp:686's fail-open element filters (nullopt = accept on weak_ptr expiry; candidate filter at :695-703 rejects — contradictory policies). Test-reachable: PrivateNodeWithoutBootstrapMembershipFailsClosed drives the path and holds the stalled node 15s. Unfixed at HEAD (763dc93d is docs-only; no code commits after the review) |
| 8 | Public nodes keep byte-identical raw behavior (no filter -> raw publish/receive) | VERIFIED | Interim install guarded by !private_network_id_.empty() (:774); every gate/publish takes the raw branch with no filter; guard suites green (pubsub_counts, processing_core_gating, network_config scenes) |
| 9 | Goal clause 1 regression: privateNetworkId identity consumption + fail-closed provisioning intact | VERIFIED | network_config_private_network_test 7/7; WriteNetworkConfig escaping + corrupt-config fatal load unchanged (verified in REVERIFICATION; no cycle-2 diff touches them) |
| 10 | Goal clause 3 regression: chain topics and CRDT keys isolated by network | VERIFIED | task_keys_scope_test, validator_registry_scope_test green; zero source changes after d541fa4b (docs-only commits) |

**Score: 9/10 truths verified.** Truth 7 is the sole failure and the only blocker.

## CR-C2-01 Treatment (the new critical finding)

**Confirmed at source, this pass, unfixed.** Verification chain:

1. `git log` — HEAD is 763dc93d "docs(15): add cycle-2 code review report"; the last source commit is d541fa4b (test 15-15). No fix commits exist.
2. GeniusNode.cpp:2363-2377 — `ShutdownNodePolicyServices` resets trust_startup_controller_, then unconditionally `tx_globaldb_->GetBroadcaster()->ClearMembershipFilter()` (:2376).
3. The three callers that leave the node alive: :1084 (BurnConfig failure), :1094 (RegisterFilters failure), :1137 (NetworkRegistry::New failure) — each logs, calls the shutdown hook, and `return`s; nothing stops PubSub or shuts down the GlobalDB (ShutdownNow exists only on the full-destruction path, :2464).
4. The cleared filter is the interim deny-all installed at :774-784 (the registry-backed filter at :1151 installs only after New succeeds). With the filter cleared, `PubSubBroadcasterExt::OnMessage` takes the raw no-filter branch — public pass-through — on a GlobalDB still subscribed to the processing channel (:785) and the quorum topic (INITIALIZING_BLOCKCHAIN).
5. Reachability: empty `network_bootstrap_peers` (PrivateNodeWithoutBootstrapMembershipFailsClosed), duplicate change-callback pattern (made failing by 15-16's G-WR-02 closure — an intentional fix with an unintended interaction), RegisterFilters failure. Any same-PSK peer then merges raw unsealed CRDT deltas into the stalled node's durable datastore indefinitely.
6. Compounding: the same hook resets `secure_crdt_` (:2401) while `tx_globaldb_` lives, and every SecureCrdt element filter (network-registry/<id>, trust, burn-config branches) then fails OPEN (`return std::nullopt` = accept, SecureCrdt.cpp:686) — so even branch-level ingest filtering is gone on the stalled node.

**Why blocker, not warning:** the accepted posture's own contract is fail-closed membership enforcement against same-PSK peers; this path hands that adversary an unauthenticated, durable write primitive into a private node's datastore, on a path the codebase itself labels "failing closed." It is a small, well-scoped fix (deny-all-on-private instead of clear, plus the element-filter reject-on-expiry) — consistent with, not contradicting, the owner's FIX-BOTH direction.

## Cycle-2 Warnings Treatment

| Warning | My verification | Disposition |
|---|---|---|
| WR-C2-01 — SecureCrdt element filters fail open on weak_ptr expiry | CONFIRMED: SecureCrdt.cpp:679-687 returns nullopt (accept) on expiry vs candidate filter :695-703 returns {} (reject) — contradictory policies; permanently exposed on the CR-C2-01 stalled path (secure_crdt_.reset() :2401, GlobalDB lives); also a live window on the healthy shutdown path (:2461 before :2464) | Folded into the blocker (its dominant exposure is the stalled node); standalone fix is the same reject-on-expiry change — listed under gaps.missing |
| WR-C2-02 — grid gating relies on undocumented caller ordering | CONFIRMED: Listen() subscribes unconditionally (processing_service.cpp:168+); production site ordered correctly (GeniusNode.cpp:3624-3630 before StartProcessing :3630); the test itself exercises the inverted order | WARNING — defense-in-depth/doc fix; not a production defect today |
| WR-C2-03 — service-level setters cannot propagate a clear | CONFIRMED: `if ( node && filter )` (:139) / `if ( node && key_copy )` (:161) skip existing nodes on empty payloads | WARNING — latent (no production clear-through-service call) |
| WR-C2-04 — SecureCrdtRegistry::Register re-lock race | CONFIRMED structurally: SecureCrdtRegistry.hpp:121-137 extract-under-lock, then re-lock + insert_or_assign can destroy a racing entry under the mutex (destructor re-entrancy) | WARNING — latent (single-threaded production) + pre-existing; NetworkRegistry path safe via RegisterIfAbsent |

## Required Artifacts (cycle-2 delta)

| Artifact | Expected | Status | Details |
|---|---|---|---|
| src/base/gossip_auth.hpp | Seal/Open/DeriveGossipPayload header-only authenticator, no networkregistry/crdt deps | VERIFIED | 336 lines; all exports; includes verified clean (libp2p crypto/peer + std only) |
| src/crdt/globaldb/pubsub_broadcaster_ext.cpp | OnMessage authenticate-before-authorize + Broadcast seal-or-fail-closed + corrected comment | VERIFIED | OpenGossipPayload :184 (before membership, before DecodeBroadcast); seal :426; "spoofing either field" claim gone; fail-closed no-key branch |
| src/processing/* (3 gates + node + service) | authenticated gates, sealed publishes, filter+key into New pre-subscription | VERIFIED | Gates :282/:470/:212; 7 sealed publishes; New params :72-73; ordering 225<291, 249<303; creation sites pass both |
| src/networkregistry/NetworkMembershipFilter.hpp | MakeBootstrapMembershipFilter fail-closed predicate | VERIFIED | Empty set denies all (unit-proven) |
| src/account/GeniusNode.cpp | interim filter before first AddListenTopic; sign_messages=true; keypair retention + wiring | VERIFIED | :774-784 < :785; :1920; :2174-2180; :3624-3626 — see CR-C2-01 for the one defective interaction (:2376) |
| src/securecrdt/SecureCrdt.{hpp,cpp} | UnregisterFiltersFor + shared pattern helper | VERIFIED | :711-720; IngestFilterPatternFor :80 single construction site |
| src/securecrdt/SecureCrdtRegistry.hpp | RegisterIfAbsent + bool UnregisterIf | VERIFIED | :158+; no destruction under lock |
| src/networkregistry/NetworkRegistry.cpp | fail-closed callback registration; token-guarded teardown; explicit failure-path Unregister | VERIFIED | :407-424, :487; :695-736 |
| Test scenes (9 new across 4 files) | decision table, impostor/unsigned/startup/creation-window + 3 lifecycle regressions | VERIFIED | All names present in --gtest_list_tests; all suites green this verification |

## Key Link Verification (cycle-2 delta)

| From | To | Via | Status |
|---|---|---|---|
| All four gate handlers | OpenGossipPayload | authenticate BEFORE AuthorizeGossipSender, inner payload to protobuf parse | WIRED |
| All 8 publish sites | SealGossipPayload | snapshot filter+key, derive from, seal-or-fail-closed | WIRED |
| GeniusNode StartPubSub keypair | broadcaster + processing service SetGossipSigningKey | retained gossip_signing_keypair_ copy | WIRED (:2178, :3626) |
| Both processing creation sites | ProcessingNode::New(filter, key) | snapshot under m_membershipFilterMutex pre-New | WIRED (:526-527, :1021-1022) |
| INITIALIZING_DATABASE | broadcaster SetMembershipFilter(MakeBootstrapMembershipFilter) | before first AddListenTopic | WIRED (:778 < :785) |
| NetworkRegistry::Unregister | SecureCrdt::UnregisterFiltersFor | token-guarded, step 4 last | WIRED |
| NetworkRegistry::New | RegisterIfAbsent / callback bool | atomic insert / fail address_in_use | WIRED |
| Policy-stack failure paths | ShutdownNodePolicyServices -> ClearMembershipFilter | intended teardown, actual public pass-through on live GlobalDB | **DEFECTIVE WIRING (CR-C2-01)** |

## Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Core membership/registry/binding/config battery | `ctest -R "^(network_membership_filter_test\|network_registry_test\|private_network_registry_binding_test\|network_config_private_network_test)$"` | 4/4 Passed (104.8s) | PASS |
| Processing + scope battery | `ctest -R "^(processing_service_test\|processing_subtask_queue_channel_pubsub_test\|processing_core_gating_test\|pubsub_counts_test\|task_keys_scope_test\|validator_registry_scope_test)$"` | 6/6 Passed (74.9s) | PASS |
| SecureCrdt/TPR/BurnConfig guards (15-16 touched SecureCrdt*) | `ctest -R "^(securecrdt_registry_test\|securecrdt_quorum_gate_test\|burnconfig_test\|trustedpeerregistry_quorum_test)$"` | 4/4 Passed (14.9s) | PASS |
| New scene registration | `--gtest_list_tests` on membership-filter + queue-channel binaries | all 9 cycle-2 scenes listed | PASS |
| Binary freshness | src/test diff d541fa4b..HEAD = 0 lines; no source file newer than binaries | binaries reflect final source | PASS |
| Commit existence | 732845e8, 54e39f86, 1fe13c6b, 3d6ed29d, 1138acbc, 8fb515fa, 43a542d6, d541fa4b | all in git log | PASS |
| CR-C2-01 fix present? | `git log` after 763dc93d (docs) + grep ClearMembershipFilter :2376 | no code commits; clear still unconditional | CONFIRMS BLOCKER |
| sign_messages at production sites | grep both sites | `= true` at GeniusNode.cpp:1920, GlobalDbNetworkComposition.cpp:200 | PASS |

## Probe Execution

SKIPPED — no probe scripts declared in any phase-15 plan (no `scripts/*/tests/probe-*.sh` in repo; verification uses ninja/ctest/grep, all executed above).

## Requirements Coverage (decision IDs; REQUIREMENTS.md has no phase-15 entries — unchanged accounting)

| ID | Status | Evidence |
|---|---|---|
| D-01..D-05, D-08, D-09, D-10 | SATISFIED (unchanged; suites green, zero regressions) | first-verification evidence carries |
| D-06 / PNET-NETREG | SATISFIED | 15-16 closures verified (filter teardown, fail-closed New, atomic registration); network_registry_test 18/18 |
| D-07 / PNET-GATE | SATISFIED WITH RESIDUAL | authenticated gates + interim startup filter delivered and flow-proven (CR-G01, CR-G02 closed); residual = CR-C2-01 failure-path public pass-through |
| D-11 / PNET-PROC | SATISFIED WITH RESIDUAL | three gates authenticated, pre-subscription install at both creation sites; residual = CR-C2-01 (same broadcaster) + WR-C2-02 ordering contract |
| PNET-CFG | SATISFIED | unchanged from REVERIFICATION |

## Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| src/account/GeniusNode.cpp | 2376 | Unconditional ClearMembershipFilter on failure paths with a still-live GlobalDB; log claims "failing closed" | Blocker (CR-C2-01) | Public pass-through CRDT ingest on a stalled private node; durable pollution |
| src/securecrdt/SecureCrdt.cpp | 679-687 | Element filters fail OPEN (nullopt=accept) on weak_ptr expiry — contradicts candidate filter's reject :695-703 | Blocker-adjacent (WR-C2-01, folded) | Unsigned remote elements accepted on policy branches after secure_crdt_.reset() on a live GlobalDB |
| src/processing/processing_service.cpp | 168+ | Grid subscription not gated by an enforced/documented ordering contract | Warning (WR-C2-02) | Ungated window only if a future caller inverts the order |
| src/processing/processing_service.cpp | 139, 161 | Truthiness-guarded propagation cannot clear filters on existing nodes | Warning (WR-C2-03) | Latent asymmetry; a future clear-through-service silently no-ops |
| src/securecrdt/SecureCrdtRegistry.hpp | 121-137 | Register re-lock insert_or_assign can destroy a racing entry under the mutex | Warning (WR-C2-04) | Latent deadlock/UB under concurrency; pre-existing |

No TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER debt markers in any phase-15 modified file (grep-verified this pass).

## Human Verification Required

See frontmatter `human_verification_recommended` (1 item): the carried-forward E2E two-node private-network job flow with a public-node control — still recommended-manual, still blocked for full GeniusNode E2E by the pre-existing no-genesis READY-stall base failures (deferred-items.md §1). The owner CR-G01/CR-G02 disposition decision from REVERIFICATION is resolved (FIX-BOTH, delivered); the CR-C2-01 fix does not need a new posture decision — the deny-all-on-private fix IS the already-decided posture, so it is reported as a gap for a closure plan, not as an escalation.

## Gaps Summary

1. **CR-C2-01 (blocker):** the fail-closed story has one defective edge — every policy-stack failure path in INITIALIZING_TRANSACTIONS clears the interim deny-all filter on a GlobalDB that keeps running, leaving a stalled private node with public pass-through ingest (and, after secure_crdt_.reset(), fail-open SecureCrdt element filters) while logging "failing closed." Reachable by the posture's own adversary model (same-PSK peer, raw unsealed publishes); test-reachable today. Fix is small and scoped: deny-all-on-private instead of clear in ShutdownNodePolicyServices (+ reject-on-expiry in the SecureCrdt element lambda), with a stalled-node-filter regression added to PrivateNodeWithoutBootstrapMembershipFailsClosed.
2. **Warnings (not blockers):** grid-ordering contract undocumented (WR-C2-02), clear-propagation asymmetry (WR-C2-03), Register re-lock race (WR-C2-04) — all latent or defense-in-depth; none affects current production wiring.

Everything cycle 2 set out to build is present, wired, and green: CR-G01, CR-G02 (a and b), G-WR-01, G-WR-02, and G-WR-04 are all closed with code-verified mechanisms and mutation-verified proofs, with zero regressions across the 14 suites run this verification. What remains is one fail-open interaction between two of those closures — the kind of defect the next (small) gap-closure wave exists to fix.

---

_Verified: 2026-09-03T19:41:50Z_
_Verifier: Claude (gsd-verifier)_
