---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
verified: 2026-09-03T18:05:00Z
status: gaps_found
score: 8/9 prior gaps closed (gap-closure verified 7 + override 1; 1 partial) — 2 new critical gaps in the replacement enforcement
overrides_applied: 2
overrides:
  - must_have: "Enforces that identity at libp2p connection upgrade (membership allow-list at connection upgrade)"
    reason: "Owner direction 2026-09-02 (deferred-items.md §3, binding): membership enforcement is SuperGenius-side application-layer filtering (PubSubBroadcasterExt::OnMessage chokepoint + three processing-path gates + per-node wiring), NOT a libp2p gater allow-list; plan 15-04's vendored-gater approach is permanently off the table. pnet PSK + Noise-only transport + startup fail-closed remain the connection-level layers. Enforcement truths are scored against this REPLACEMENT posture, whose own defects (CR-G01/CR-G02 below) are reported as new gaps, not excused by this override."
    accepted_by: "henrique"
    accepted_at: "2026-09-03T17:55:00Z"
  - must_have: "15-04 must-haves: injectable allow-list on DenyListConnectionGater; deny-wins-over-allow; GossipPubSub exposes allow-list pre-Start; extended vendored library reinstalled"
    reason: "Plan 15-04 permanently skipped by owner order (no 3rdparty modifications); the four must-haves describe the vendored-gater surface that is permanently unnecessary under the app-layer replacement (implemented by 15-11/15-12/15-13). ROADMAP checkbox intentionally left unchecked for 15-04."
    accepted_by: "henrique"
    accepted_at: "2026-09-03T17:55:00Z"
re_verification:
  previous_status: gaps_found
  previous_score: 29/37
  gaps_closed:
    - "Gap 3 — private node installs registry-backed membership filter on its GlobalDB broadcaster + live cache refresh + teardown clear (15-12)"
    - "Gap 4 — same-PSK member replicates / honest same-PSK non-member denied at message-flow level, empty membership denies, runtime widening admits (15-11 flow tests)"
    - "Gap 5 — processing path (grid/results/queue channels) binds the same membership enforcement, wired per-node, test targets registered (15-13)"
    - "Gap 6 — fail-closed provisioning chain: WriteNetworkConfig full escaping + corrupt-config fatal load (15-10)"
    - "Gap 7 — NetworkRegistry refresh loop drains once, no busy-spin (WR-02, 15-09)"
    - "Gap 8 — duplicate NetworkRegistry::New fails non-destructively with address_in_use (WR-03, 15-09)"
    - "Gap 9 — network-registry/<id> CRDT branch receives SecureCrdt ingest filter via RegisterFilters re-run in New (WR-04, 15-09)"
    - "Gap 2 — closed by owner override (15-04 permanent skip; replacement delivered)"
  gaps_remaining:
    - "Gap 1 (PARTIAL) — the replacement posture is delivered and proven against HONEST same-PSK non-members at all four surfaces, but is NOT delivered against an ACTIVE adversary (CR-G01: forgeable authorization basis) and is not fail-closed throughout (CR-G02: ungated enrollment/startup windows)"
  regressions: []
gaps:
  - truth: "A same-PSK non-member cannot participate in gossip ingest, CRDT replication, or processing channels AGAINST AN ACTIVE ADVERSARY (the accepted replacement posture's own adversary model: PSK-holding non-member forging identity fields)"
    status: failed
    reason: >-
      All four membership gates authorize solely on unauthenticated wire-supplied identity:
      the protobuf-declared peer (bmsg.peer().id()) and the gossip transport from-field.
      Production gossip runs UNSIGNED: sign_messages = false at GeniusNode.cpp:1885 ("use no
      signing") and GlobalDbNetworkComposition.cpp:187. In the vendored gossip, publish()
      stamps from = local_peer_id_ unconditionally (thirdparty gossip_core.cpp:200-201), and
      the receive/forward path performs ZERO signature or from-authenticity verification
      (grep for verify across the vendored gossip tree: 0 hits; no SGNUS topic validator
      registered). A PSK-holding non-member publishing with from = <member PeerId> (and
      bmsg.peer().id() likewise) passes the broadcaster gate (pubsub_broadcaster_ext.cpp:159-191),
      the grid gate (processing_service.cpp:193-207), the results gate
      (processing_subtask_queue_accessor_impl.cpp:401-416), and the queue gate
      (processing_subtask_queue_channel_pubsub.cpp:100-117). The flow tests prove honest-sender
      denial only — every test fixture configures sign_messages = true (network_membership_filter_test.cpp:95,
      processing_service_test_base.cpp:189, pubsub_counts.cpp:54), which production does not.
      In-code security claims are incorrect under this configuration: pubsub_broadcaster_ext.cpp:163-164
      "Checking both defends against spoofing either field" (both fields are attacker-supplied
      and unsigned — checking both defends against nothing).
    artifacts:
      - path: "src/networkregistry/NetworkMembershipFilter.hpp"
        issue: "AuthorizeGossipSender trusts from_bytes with no authentication of the sender"
      - path: "src/crdt/globaldb/pubsub_broadcaster_ext.cpp"
        issue: "OnMessage dual-identity gate authorizes on two unauthenticated fields; comment claims spoof defense it does not provide (lines 159-191)"
      - path: "src/processing/processing_service.cpp"
        issue: "grid-channel gate authorizes on unauthenticated message->from (193-207)"
      - path: "src/processing/processing_subtask_queue_accessor_impl.cpp"
        issue: "results-channel gate authorizes on unauthenticated message->from (401-416)"
      - path: "src/processing/processing_subtask_queue_channel_pubsub.cpp"
        issue: "queue-channel gate authorizes on unauthenticated message->from (100-117)"
      - path: "src/account/GeniusNode.cpp:1885"
        issue: "production gossip configured sign_messages = false"
    missing:
      - "Authenticate the sender before consulting membership: enable gossip signing (sign_messages = true in StartPubSub and GlobalDbNetworkComposition) and verify in the gate — recover the PeerId from the message's embedded public key, check it matches from, verify the signature over the signable bytes; reject missing signature/key under a set filter (fail-closed), mirroring the empty-from denial"
      - "If signing cannot be enabled at this layer, correct the code comments and plan documents to state plainly that the gate is a policy filter over declared identity, enforceable only against well-behaved peers (remove the spoof-defense and 'no enrollment window' claims)"
  - truth: "Enforcement is fail-closed throughout — no ungated message windows in the processing-path and private-node-startup lifecycles"
    status: failed
    reason: >-
      CR-G02 (code-verified): ProcessingNode::New subscribes the queue channel inside
      Initialize (processingQueueChannel->Listen with the 2000ms default wait,
      processing_node.cpp:204, processing_node.hpp:59) and the results channel inside
      AttachTo (:211-233) — both BEFORE the filter is applied at the node-creation sites
      (processing_service.cpp:430-438 AcceptProcessingChannel and :811-819
      HandleNodeCreationTimeout, which run only after New returns). On EVERY node creation
      there is a multi-hundred-ms to multi-second window in which OnProcessingChannelMessage
      and OnResultChannelMessage run with an EMPTY filter (public pass-through): a same-PSK
      non-member timed into this window can grab queue ownership or push a poisoned queue
      snapshot/result. This contradicts the header contract at processing_service.hpp:71-74
      ("Applied to all existing processing nodes AND at node creation, so there is no
      enrollment window (T-15-13-06)") — the T-15-13-06 mitigation claim is not delivered.
      Additionally (G-WR-03): on private-node startup, tx_globaldb_ is created, started, and
      subscribes topics from INITIALIZING_DATABASE (GeniusNode.cpp:756-758) and
      INITIALIZING_BLOCKCHAIN (:852), while the broadcaster filter installs only at
      NetworkRegistry construction in INITIALIZING_TRANSACTIONS (:1121-1127) — a
      seconds-scale ungated CRDT ingest window on every boot, during which a non-member's
      deltas are merged and can be re-origined into other members via
      CrdtDatastore::RebroadcastHeads. (The grid channel is NOT affected: its filter is
      installed before StartProcessing -> Listen, GeniusNode.cpp:3561-3572.)
    artifacts:
      - path: "src/processing/processing_node.cpp"
        issue: "queue channel Listen (:204) and results channel AttachTo (:211-233) go live inside New before the creation sites install the filter"
      - path: "src/processing/processing_service.hpp"
        issue: "documented 'no enrollment window' contract (:71-74) is false for node creation"
      - path: "src/account/GeniusNode.cpp"
        issue: "GlobalDB listening from INITIALIZING_DATABASE (:756-758) vs filter install in INITIALIZING_TRANSACTIONS (:1121-1127) — startup ingest window"
    missing:
      - "Pass the filter into ProcessingNode::New before any subscription goes live (e.g. optional MembershipFilter parameter forwarded via SetMembershipFilter to the channel and accessor before Listen() and ConnectToSubTaskQueue()); snapshot the filter under m_membershipFilterMutex BEFORE calling ProcessingNode::New at both creation sites"
      - "Install a bootstrap-membership-backed filter on the broadcaster immediately after GlobalDB creation (MakeNetworkMembershipFilter over configured network_bootstrap_peers_), letting the registry-backed filter replace it at INITIALIZING_TRANSACTIONS — or document the startup window as an accepted deviation"
human_verification_recommended:
  - test: "End-to-end two-node private network job flow (carried forward from 15-VERIFICATION.md): provision two GeniusNodes with the same valid private_network_id + network_key + network_bootstrap_peers, publish a job from one, verify processing/replication under /chain/<id>/ keys, and verify a public node observes none of it"
    expected: "Private job data exists only under the scoped branch; both private nodes replicate it; the public node observes none of it; both private nodes reach READY with filters installed"
    why_human: "No automated suite exercises a live multi-process private network end to end (the flow tests use component-level GlobalDB/pubsub fixtures, not full GeniusNodes)"
  - test: "Owner decision on the CR-G01/CR-G02 disposition: fix (signing + gate verification + pre-subscription filter install) vs accept-and-relabel (policy filter over declared identity, honest-peers-only; windows accepted)"
    expected: "Recorded decision: gap-closure plan for the fix, or override entries accepting the residuals with corrected in-code claims"
    why_human: "Security-posture trade-off (enabling gossip signing changes wire format for all peers; window fixes touch node-creation ordering); only the owner can accept the residual exposure"
---

# Phase 15: Private Network Identity and libp2p Gating — RE-VERIFICATION Report

**Phase Goal:** SuperGenius consumes the license NFT's `privateNetworkId` as the self-certifying private-network identity and enforces that identity at libp2p connection upgrade, while isolating chain topics and CRDT keys by network.
**Verified:** 2026-09-03T18:05:00Z
**Status:** gaps_found
**Re-verification:** Yes — second pass, after gap-closure plans 15-09..15-13 (all executed, SUMMARYs present, commits verified in git log)

## Verdict in One Paragraph

The nine prior gaps are substantively closed at the mechanism level: the app-layer membership enforcement exists, is wired per-node at every gossip-ingest surface (broadcaster chokepoint + grid/results/queue channels), is fail-closed by default, is live-refreshing, and every closure has green, mutation-verified regression tests (23 suites green this verification). The owner override on the literal "connection upgrade" clause is recorded and the enforcement truths were scored against the replacement posture as directed. **However, the replacement posture itself is not fully delivered against an active adversary**: all four gates authorize on the unauthenticated gossip `from` field / protobuf-declared peer under production `sign_messages = false` (CR-G01 — a same-PSK non-member forging `from` passes every gate), and there are code-verified ungated windows at every processing-node creation and every private-node startup (CR-G02), contradicting the "no enrollment window" contract written into the code. Two new critical gaps are reported; the goal clause "enforces that identity" is therefore still not achieved end-to-end.

## Goal Achievement

| Goal clause | Status | Evidence |
|---|---|---|
| Consumes `privateNetworkId` as the self-certifying private-network identity | VERIFIED (regression) | Unchanged from first verification; network_config_private_network_test 7/7 green incl. the new fail-closed scenes; no regressions in the 23-suite battery |
| Enforces that identity at libp2p connection upgrade | PARTIAL (override + replacement scored) | Literal clause overridden per owner direction (deferred-items.md §3). Replacement (app-layer filter at message ingest) EXISTS and is WIRED at all four surfaces with fail-closed defaults — VERIFIED against honest peers (flow tests, mutation-verified). NOT delivered against an active adversary (CR-G01) and not fail-closed throughout (CR-G02 windows) — new critical gaps |
| Isolating chain topics and CRDT keys by network | VERIFIED (regression) | task_keys_scope_test, validator_registry_scope_test, escrow scenes green; no phase-15-gap-closure code touched the scope plumbing (diff-committed files confirm) |

## Prior Gap Accounting (the 9 gaps from 15-VERIFICATION.md)

| # | Prior gap | Status | Evidence (this verification) |
|---|---|---|---|
| 1 | Same-PSK non-member rejected at upgrade (goal clause 2) — replacement: cannot participate in gossip ingest / CRDT replication / processing channels, fail-closed throughout | **PARTIAL** | Mechanism: NetworkMembershipFilter.hpp (fail-closed: expired registry denies :69-77, empty membership denies :80-83, empty/malformed from denies :116-121); broadcaster OnMessage gate before DecodeBroadcast (pubsub_broadcaster_ext.cpp:159-191); three processing gates (AuthorizeGossipSender at entry of each handler); per-node wiring (GeniusNode.cpp:1121-1127 broadcaster, :3566-3570 processing service). Honest-peer denial test-proven (network_membership_filter_test 7/7, both new processing scenes; mutation-verified per SUMMARYs). BUT: active-adversary authorization forgeable (CR-G01) and ungated windows exist (CR-G02) — the posture as stated is not fully delivered |
| 2 | 15-04 must-haves (vendored gater allow-list surface) | **PASSED (override)** | Owner permanent skip (deferred-items.md §3; no 15-04-SUMMARY by design; ROADMAP checkbox intentionally unchecked — verified). Override recorded above; replacement delivered (see gap 1) |
| 3 | Private node feeds NetworkRegistry membership to gossip enforcement (descoped 15-05 half) | **CLOSED** | GeniusNode.cpp:1090-1098 — NetworkRegistry::New passes tx_globaldb_ as trailing global_db (live refresh); :1121-1127 — SetMembershipFilter(MakeNetworkMembershipFilter(network_registry_)) inside the `!private_network_id_.empty()` guard, before READY; :2319-2321 — ClearMembershipFilter before registry Unregister in ShutdownNodePolicyServices. private_network_registry_binding_test 4/4 incl. TeardownClearsBroadcasterMembershipFilter (non-vacuous, held broadcaster handle). Public node installs nothing (pinned by PublicNodeConstructsNoNetworkRegistry) |
| 4 | Same-PSK mesh/exchange + non-member rejected (PrivateNetworkMembershipGating) — replacement: message-flow-level proof | **CLOSED** (honest-sender scope) | network_membership_filter_test: UnauthorizedSamePskPeerCannotParticipateWhileMembersCan (both gated members, wrong-PSK transport control), MembershipWideningAdmitsNewPeerAtRuntime (per-message consultation), EmptyMembershipDeniesEverything — 7/7 green this verification; SUMMARY records mutation-verified non-vacuity (intruder admitted to membership makes the negative fail). Caveat: fixtures run sign_messages=true — active-adversary denial untested and false in production (CR-G01) |
| 5 | Processing host binds the same enforcement incl. membership (descoped 15-08 clause) | **CLOSED** (wiring + honest-sender) | Gates at all three handlers (processing_service.cpp:201, processing_subtask_queue_accessor_impl.cpp:411, processing_subtask_queue_channel_pubsub.cpp:112) with mutex-guarded storage; propagation chain ProcessingServiceImpl::SetMembershipFilter -> existing nodes (:126-139) + BOTH creation sites (:430-438, :811-819); ProcessingNode forwarding (:132-152); GeniusNode StartProcessing guarded install (:3566-3570) before StartProcessing(ScopedProcessingGridChannel()). Both test targets REGISTERED (ctest lists #86/#87; gate passed) and green: GridMessagesFromNonMemberPeersAreIgnored, MembershipFilterBlocksNonMemberQueueMessages (each re-run individually: PASSED). Caveat: CR-G02 enrollment window |
| 6 | Fail-closed identity posture for corrupt provisioning (CR-01 + WR-01) | **CLOSED** | WriteNetworkConfig full JSON escaping incl. \n \r \t \b \f + <0x20 as \u00XX (GeniusNode.cpp:233-271); parse-error branch sets settings.valid=false + "refusing to start" log (:1651-1658); missing-file branch unchanged public default with documented distinction (:1638-1644). SwarmKeyTextWithNewlinesRoundTrips + CorruptConfigFailsNodeStart re-run individually: PASSED. Residual IN-03 (id/peers emitted unescaped) now fails CLOSED (bricked boot, never public boot) — warning only |
| 7 | Refresh loop does not busy-spin (WR-02) | **CLOSED** | NetworkRegistry.cpp:506-521 — refresh_pending_.store(false) under refresh_mutex_ before unlock-then-TryConfirm (drain-once, no lost wakeup); refresh_attempts_ seam. RefreshLoopDrainsOnceWithoutSpinning re-run: PASSED |
| 8 | Duplicate NetworkRegistry::New must not brick the live registry (WR-03) | **CLOSED** | NetworkRegistry.cpp:375-386 — Resolve-first pre-check returns address_in_use BEFORE make_shared (:388)/Register; RegisterSignerSetSource failure remapped to address_in_use (:397-399). DuplicateNewDoesNotClobberLiveRegistry re-run: PASSED. Residual: concurrent-New TOCTOU (G-WR-04) — latent (single-threaded production wiring) |
| 9 | network-registry/<id> receives SecureCrdt ingest verification (WR-04) | **CLOSED** | NetworkRegistry.cpp:405-426 — RegisterFilters() re-run at the end of New (failure -> io_error), covering both production wiring orders; CRDTDataFilter re-registration replaces-and-succeeds. IngestFilterCoversLateRegisteredNetworkRegistryPattern re-run: PASSED (remote-originated unsigned element never lands; probe control proves delivery). Residual: G-WR-01 — teardown never removes the installed GlobalDB element filter (UnregisterElementFilter API exists, globaldb.cpp:689, but no teardown call site) |

**Prior-gap score: 8/9 closed (7 verified + 1 override); gap 1 partial.**

## New Findings Treatment (15-REVIEW-GAPS.md — independently code-verified this pass)

| Finding | My verification | Disposition |
|---|---|---|
| CR-G01 — all four gates authorize on unauthenticated `from`/declared peer | CONFIRMED at source: sign_messages=false (GeniusNode.cpp:1882-1885 "use no signing", GlobalDbNetworkComposition.cpp:187); vendored gossip publish stamps from=local_peer_id_ unconditionally (thirdparty gossip_core.cpp:200-210), zero verification calls in the whole vendored gossip tree, no SGNUS topic validator registered; gates read only bmsg.peer().id() + message->from; all three test fixtures use sign_messages=true | **NEW GAP (blocker)** — the replacement enforcement's adversary model ("PSK-holding non-member per registry is dropped", the filter's own doc) is not met against a forging peer |
| CR-G02 — enrollment window in ProcessingNode::New | CONFIRMED at source: Listen(2000ms default) inside Initialize (processing_node.cpp:204) and results-channel subscribe inside AttachTo (:211-233) both precede the filter application at the creation sites; processing_service.hpp:71-74 claims "no enrollment window (T-15-13-06)" | **NEW GAP (blocker)** — false security contract in code; seconds-scale ungated window per node creation |
| G-WR-01 — teardown leaves the GlobalDB ingest filter installed | CONFIRMED: no UnregisterElementFilter call on any teardown path (API exists, unused); re-opens the unsigned-base-element griefing vector on failure/retry paths where the GlobalDB outlives the policy stack | WARNING — defense-in-depth; not an authorization bypass (ReadIfQuorum still gates application) |
| G-WR-02 — RegisterCrdtChangeCallback failure degrades silently | CONFIRMED: NetworkRegistry.cpp:476-481 — registered==false -> warn + clear pattern + return WITHOUT the refresh thread, while New returns success (live widening silently dead on that node) | WARNING — wrong posture for the 15-12 live-refresh core feature |
| G-WR-03 — private-node startup ingest window | CONFIRMED via state machine: GlobalDB live from INITIALIZING_DATABASE (:744-758) + quorum topic (:852); filter installs at :1121-1127 (INITIALIZING_TRANSACTIONS) | Folded into NEW GAP 2 (fail-closed-throughout) |
| G-WR-04 — duplicate-New check-then-act TOCTOU | CONFIRMED structurally (Resolve pre-check is not atomic with Register) | WARNING — latent (production wiring single-threaded) |
| IN-01/02/03 | Spot-confirmed (silent no-op casts; unchecked public ctor deref; id/peers unescaped — now fail-closed) | INFO |
| WR-05 (prior review, entry pinning) | Unchanged, deferred, not worsened (per gap-delta review; consistent with the pinning caveat in 15-11-SUMMARY) | Deferred (accepted) |

## Observable Truths — Regression Check (previously verified 29)

Quick regression per re-verification protocol (existence + battery): all 23 targeted suites green this verification — network_config_private_network (7 scenes), private_network_registry_binding (4), network_registry (12), network_membership_filter (7), processing_service, processing_subtask_queue_channel_pubsub, processing_core_gating (4), pubsub_counts, peer_registry, task_keys_scope, validator_registry_scope, trustedpeerregistry (2), securecrdt (4), burnconfig (4), graphsync guards. Binaries fresh vs last source commit (16e743d5, Sep 3 11:04; binaries 11:01-11:11 — 15-13 rebuilt its test targets last). Zero regressions. Pre-existing no-genesis READY-stall failures NOT run, attributed to base per deferred-items.md §1 (documented A/B evidence).

## Required Artifacts (gap-closure delta)

| Artifact | Expected | Status | Details |
|---|---|---|---|
| src/networkregistry/NetworkMembershipFilter.hpp | MembershipFilter + MakeNetworkMembershipFilter + AuthorizeGossipSender, fail-closed | VERIFIED | 128 lines; all three exports; fail-closed on expired/empty/unparseable |
| src/crdt/globaldb/pubsub_broadcaster_ext.{hpp,cpp} | Set/Has/ClearMembershipFilter + OnMessage gate | VERIFIED | Mutex-guarded storage; gate before DecodeBroadcast; no networkregistry includes (layering) |
| src/networkregistry/NetworkRegistry.cpp | drain-once + duplicate pre-check + RegisterFilters re-run | VERIFIED | :375-386, :415-426, :506-521 |
| src/account/GeniusNode.cpp | config chain + broadcaster install + live refresh + teardown clear + processing wiring | VERIFIED | :233-271, :1651-1658, :1090-1127, :2319-2321, :3566-3570 |
| src/processing/* (4 class pairs) | gates + propagation | VERIFIED | All three handlers gated; both creation sites apply; CMake links networkregistry |
| test targets registered | processing_service_test + processing_subtask_queue_channel_pubsub_test | VERIFIED | ctest #86/#87 listed; both green |
| test/src/networkregistry/network_membership_filter_test.cpp | 4 unit + 3 flow cases | VERIFIED | 7/7 green this verification |
| test/src/networkregistry/network_registry_test.cpp | +3 lifecycle regressions | VERIFIED | 12 cases; the 3 new re-run individually green |
| test/src/account/network_config_private_network_test.cpp | +2 fail-closed scenes | VERIFIED | 7 scenes; the 2 new re-run individually green |
| test/src/account/private_network_registry_binding_test.cpp | +teardown scene + filter assertions | VERIFIED | 4 scenes green |

## Key Link Verification (gap-closure delta)

| From | To | Via | Status |
|---|---|---|---|
| GeniusNode INITIALIZING_TRANSACTIONS | PubSubBroadcasterExt::SetMembershipFilter | GetBroadcaster() after registry assignment (:1121-1127) | WIRED |
| NetworkRegistry::New | live cache refresh | tx_globaldb_ trailing global_db arg (:1098) | WIRED (silent-degradation warning G-WR-02) |
| ShutdownNodePolicyServices | ClearMembershipFilter | before registry Unregister (:2319-2331) | WIRED (test-proven non-vacuously) |
| GeniusNode StartProcessing | ProcessingServiceImpl::SetMembershipFilter | guarded install before StartProcessing (:3566-3572) | WIRED |
| ProcessingServiceImpl | ProcessingNode -> accessor + queue channel | propagation + both creation sites | WIRED (with CR-G02 pre-subscription window) |
| Each processing handler | AuthorizeGossipSender | entry gate on message->from | WIRED (authorization basis unauthenticated — CR-G01) |
| OnMessage (broadcaster) | membership predicate | dual-identity check pre-decode | WIRED (same CR-G01 caveat) |

## Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| 7 core gap-closure suites | `ctest -R "^(network_membership_filter\|network_registry\|network_config_private_network\|private_network_registry_binding\|processing_service_test\|processing_subtask_queue_channel_pubsub\|processing_core_gating)_test$"` | 7/7 Passed (112.7s) | PASS |
| 16 regression suites (incl. quorum trio, pubsub_counts, keys/validator scope) | `ctest -R "^pubsub_counts\|...trustedpeer\|securecrdt\|burnconfig..."` | 16/16 Passed (58.0s) | PASS |
| New processing gate scenes individually | direct gtest runs | both PASSED (9.2s / 7.8s) | PASS |
| New lifecycle/config/teardown scenes individually | direct gtest filters | all PASSED | PASS |
| Registration gate | `ctest -N -R "processing_service_test\|processing_subtask_queue_channel_pubsub"` | Total Tests: 2 (targets #86/#87) | PASS |
| Commit existence | `git log -1` 274baebb / 16e743d5 / ff983785 | all present | PASS |
| Binary freshness | test_bin mtimes vs last src commit | binaries newer | PASS |
| Production gossip signing | `grep sign_messages src/...` | false at both production sites | CONFIRMS CR-G01 |
| Vendored gossip verification | `grep -rn verify thirdparty/.../gossip/` | 0 hits | CONFIRMS CR-G01 |

## Probe Execution

SKIPPED — no probe scripts declared in any phase-15 plan (no `scripts/*/tests/probe-*.sh` in repo; verification uses ninja/ctest/grep, all executed above).

## Requirements Coverage (decision IDs; REQUIREMENTS.md has no phase-15 entries — unchanged accounting)

| ID | Status | Evidence |
|---|---|---|
| D-01, D-02, D-03, D-04, D-05, D-06, D-08, D-09, D-10 | SATISFIED (unchanged; suites green) | first-verification evidence carries; no regressions |
| D-07 / PNET-GATE | SATISFIED WITH RESIDUAL | replacement delivered + wired + honest-peer-tested (gaps 1/3/4 closures); active-adversary effectiveness open (CR-G01) + windows (CR-G02) |
| D-11 / PNET-PROC | SATISFIED WITH RESIDUAL | three gates + wiring + runnable proof (gap 5 closure); same CR-G01/CR-G02 residuals |
| PNET-CFG | SATISFIED | gap 6 closed; IN-03 residual is fail-closed (warning) |

## Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| src/crdt/globaldb/pubsub_broadcaster_ext.cpp | 163-164 | Incorrect security claim: "Checking both defends against spoofing either field" — both fields unauthenticated under sign_messages=false | Blocker (CR-G01 documentation half) | Misleads future maintainers; the spoof defense does not exist |
| src/processing/processing_service.hpp | 71-74 | Incorrect contract: "no enrollment window (T-15-13-06)" — windows verified at node creation and startup | Blocker (CR-G02 documentation half) | False guarantee in API docs |
| src/networkregistry/NetworkRegistry.cpp | 476-481 | Silent degradation: callback-registration failure leaves New succeeding with no live refresh | Warning (G-WR-02) | Live widening silently dead on affected nodes |
| src/networkregistry/NetworkRegistry.cpp | 375-400 | Check-then-act duplicate guard (TOCTOU under concurrency) | Warning (G-WR-04) | Latent (single-threaded production) |
| NetworkRegistry teardown / GlobalDB | — | Stale ingest filter never removed (UnregisterElementFilter unused) | Warning (G-WR-01) | Unsigned base elements accepted into network-registry/<id> post-teardown |
| src/account/GeniusNode.cpp | 274-293 | private_network_id / bootstrap peers emitted without escaping | Info (IN-03) | Post-WR-01-fix this fails closed (refuses start), never silent-public |
| src/processing/processing_node.cpp | 132-152 | Silent no-op on failed dynamic_pointer_cast in SetMembershipFilter | Info (IN-01) | Latent; deserves a warn log |

No TBD/FIXME/XXX debt markers in any phase-15 gap-closure file (grep-verified).

## Human Verification Required

See frontmatter `human_verification_recommended` (2 items): the carried-forward E2E two-node private-network job flow with a public-node control, and the owner decision on CR-G01/CR-G02 disposition (fix vs accept-and-relabel with corrected claims).

## Gaps Summary

1. **CR-G01 (blocker):** The accepted replacement control — app-layer membership gates — authorizes on the unauthenticated gossip `from` field and protobuf-declared peer. Under the production `sign_messages = false` configuration (both gossip construction sites), a same-PSK non-member forging `from=<member>` passes all four gates (gossip/CRDT ingest, grid, results, queue). The flow tests prove honest-sender denial only (their fixtures sign). Fix per the review sketch: enable signing and verify signature/key-vs-from inside the gate (reject unsigned under a set filter), or relabel the control honestly as a declared-identity policy filter for well-behaved peers and drop the spoof-defense claims.
2. **CR-G02 (blocker):** Ungated windows contradict the fail-closed-throughout posture: (a) every ProcessingNode::New subscribes its queue (2000ms Listen wait) and results channels before the creation sites install the filter — contradicting the header's "no enrollment window" contract; (b) every private-node boot runs its GlobalDB gossip ingest unfiltered from INITIALIZING_DATABASE until the NetworkRegistry installs the broadcaster filter in INITIALIZING_TRANSACTIONS.
3. **Warnings (not blockers):** stale GlobalDB ingest filter after teardown (G-WR-01), silent live-refresh degradation (G-WR-02), duplicate-New TOCTOU (G-WR-04), IN-01..03.

Everything the gap-closure wave set out to build is present, wired, and green: all nine prior gaps have their mechanisms in the codebase with runnable, mutation-verified proof, and the 29 previously verified truths show zero regressions across 23 suites. What remains is the effectiveness of the replacement enforcement itself against the adversary it was built for.

---

_Verified: 2026-09-03T18:05:00Z_
_Verifier: Claude (gsd-verifier)_
