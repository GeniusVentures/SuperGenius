---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
verified: 2026-09-04T14:23:04Z
status: human_needed
score: 10/10 must-haves verified (cycle-3 blocker CR-C2-01 closed and code/test-verified; sole remaining item is the carried-forward manual E2E)
overrides_applied: 2
overrides:
  - must_have: "Enforces that identity at libp2p connection upgrade (membership allow-list at connection upgrade)"
    reason: "Owner direction 2026-09-02 (deferred-items.md §3, binding): membership enforcement is SuperGenius-side application-layer filtering (authenticated ingest gates at all four inbound surfaces), NOT a libp2p gater allow-list; plan 15-04's vendored-gater approach is permanently off the table. pnet PSK + Noise-only transport + startup fail-closed remain the connection-level layers. Enforcement truths are scored against this REPLACEMENT posture (keypair-sealed-envelope authenticated after 15-14, fail-closed end-to-end after 15-17)."
    accepted_by: "henrique"
    accepted_at: "2026-09-03T17:55:00Z"
  - must_have: "15-04 must-haves: injectable allow-list on DenyListConnectionGater; deny-wins-over-allow; GossipPubSub exposes allow-list pre-Start; extended vendored library reinstalled"
    reason: "Plan 15-04 permanently skipped by owner order (no 3rdparty modifications); the four must-haves describe the vendored-gater surface that is permanently unnecessary under the app-layer replacement (delivered by 15-11..15-17). ROADMAP checkbox intentionally left unchecked for 15-04."
    accepted_by: "henrique"
    accepted_at: "2026-09-03T17:55:00Z"
re_verification:
  previous_status: gaps_found
  previous_score: 9/10
  gaps_closed:
    - "CR-C2-01 — deny-all-on-private teardown: ShutdownNodePolicyServices(bool global_db_shutdown_follows = false) installs MakeBootstrapMembershipFilter({}) on a private node whose GlobalDB stays live (GeniusNode.cpp:2385-2392); ClearMembershipFilter survives only in the else branch (:2395) reached by public nodes (no-op) and the destruction route (:2481 true -> ShutdownNow at :2484) (15-17 Task 1, commit 66e67ab9)"
    - "WR-C2-01 (folded) — SecureCrdt::RegisterFilters element-filter lambda returns std::vector<sgns::crdt::pb::Element>{} (REJECT) on weak_self expiry (SecureCrdt.cpp:686-692), mirroring the candidate filter (:708); no nullopt return remains in RegisterFilters (15-17 Task 2, commit ae662f0b)"
    - "Regression 1 — PrivateNodeWithoutBootstrapMembershipFailsClosed now asserts BroadcasterMembershipFilterInstalled on the stalled node (private_network_registry_binding_test.cpp:229-231); mutation evidence recorded in 15-17-SUMMARY with failure line matching current source (:229)"
    - "Regression 2 — ElementFilterRejectsOnSecureCrdtExpiry two-datastore behavioral proof (network_registry_test.cpp:878+): tpr_ pin release -> RegisterFilters -> owner expiry -> attacker's unsigned branch element rejected while control replicates; mutation evidence lines (:962/:965) match current source"
  gaps_remaining: []
  regressions: []
human_verification:
  - test: "End-to-end two-node private network job flow (carried forward from cycles 1-3): provision two GeniusNodes with the same valid private_network_id + network_key + network_bootstrap_peers, publish a job from one, verify processing/replication under /chain/<id>/ keys, and verify a public node observes none of it"
    expected: "Private job data exists only under the scoped branch; both private nodes replicate it; the public node observes none of it; both private nodes reach READY with filters installed"
    why_human: "No automated suite exercises a live multi-process GeniusNode private network end to end; full GeniusNode E2E is additionally blocked by the pre-existing phase-13 quorum-policy regression (blockchain_genesis_test/processing_nodes_test node-ready timeouts, first-bad 76f5fde28, tracked in .planning/todos/pending/genesis-e2e-suites-quorum-policy-regression.md). pubsub_counts_test PnetIsolationAndGaterBlocking provides the closest automated partial (4-host pnet isolation + public control) and is green."
---

# Phase 15: Private Network Identity and libp2p Gating — RE-VERIFICATION 3 Report (Cycle 4)

**Phase Goal:** SuperGenius consumes the license NFT's `privateNetworkId` as the self-certifying private-network identity and enforces that identity at libp2p connection upgrade, while isolating chain topics and CRDT keys by network.
**Verified:** 2026-09-04T14:23:04Z
**Status:** human_needed
**Re-verification:** Yes — fourth pass, after cycle-3 gap-closure plan 15-17 (commits 66e67ab9 + ae662f0b verified in git log; working tree clean for src/ and test/)
**Scored against:** the owner-sanctioned replacement posture (authenticated keypair-sealed-envelope membership gates at all four inbound surfaces + fail-closed end-to-end + no enrollment windows), per the standing overrides.

## Verdict in One Paragraph

The sole cycle-3 blocker CR-C2-01 is genuinely closed in code and proven by mutation-verified behavioral regressions: every policy-stack failure path now installs an explicit deny-all membership filter on a private node's still-live GlobalDB instead of clearing it, the clear survives only on the destruction route where `ShutdownNow()` follows three lines later, and the SecureCrdt element filters now reject on owner expiry exactly like the candidate filters. I re-verified the full ten-truth list from 15-REVERIFICATION-2 against the current codebase rather than trusting 15-17's SUMMARY: all ten truths are VERIFIED, the full 14-suite phase-15 battery passes on freshly rebuilt binaries (100%, 196s), and no other production `ClearMembershipFilter` call site exists outside the gated else branch. The fresh cycle-3 code review's one critical (CR-01, ValidatorRegistry::InitializeCache parse-only caching) is real but pre-existing — git archaeology shows the trust-on-load behavior predates phase 15 and phase-15's only touch of that file (15-07, d0ef6c89) rerouted key derivation without changing caching semantics — so it is a surfaced warning with a follow-up recommendation, not a phase-15 goal gap. What remains is the one carried-forward manual item: a live two-node private-network job-flow E2E with a public-node control, which stays human territory (and is currently blocked for full GeniusNode E2E by the phase-13 quorum-policy regression tracked in the pending todo).

## Goal Achievement

| Goal clause | Status | Evidence |
|---|---|---|
| Consumes `privateNetworkId` as the self-certifying private-network identity | VERIFIED | network_config_private_network_test green this verification (14/14 battery); identity-consumption chain untouched by 15-17 (diff d541fa4b..HEAD touches only the 5 declared files) |
| Enforces that identity (accepted posture: authenticated membership gates at all four inbound surfaces, fail-closed end-to-end, no enrollment windows) | VERIFIED | All four gates authenticate before authorizing (OpenGossipPayload at pubsub_broadcaster_ext.cpp:184, processing_service.cpp:282, processing_subtask_queue_accessor_impl.cpp:470, processing_subtask_queue_channel_pubsub.cpp:212); sign_messages=true at GeniusNode.cpp:1920 and GlobalDbNetworkComposition.cpp:200; pre-subscription installs intact; CR-C2-01 deny-all teardown closed this cycle (truth 7 below) |
| Isolating chain topics and CRDT keys by network | VERIFIED | task_keys_scope_test + validator_registry_scope_test green; scope plumbing untouched by 15-17 |

## Cycle-3 Gap Accounting (CR-C2-01 + folded WR-C2-01)

| # | Item | Status | Evidence (code + tests, this verification) |
|---|---|---|---|
| 1 | CR-C2-01 — failure-path teardown restores public pass-through on a live GlobalDB | **CLOSED** | GeniusNode.hpp:1327 `ShutdownNodePolicyServices( bool global_db_shutdown_follows = false )`; GeniusNode.cpp:2363 definition, :2385 branch guard `if (!private_network_id_.empty() && !global_db_shutdown_follows)` -> :2387-2388 `SetMembershipFilter(MakeBootstrapMembershipFilter({}))` + :2389-2391 warn (logs PUBLIC private_network_id_ only, D-03); else branch :2395 `ClearMembershipFilter()` (public no-op; destruction route). All three failure-path callers (:1084 BurnConfig, :1094 RegisterFilters, :1137 NetworkRegistry::New) use the default; :2481 destruction route passes `/*global_db_shutdown_follows=*/ true` with `tx_globaldb_->ShutdownNow()` at :2484. Disproven "would deny-all anyway" comment gone (grep count 0). Whole-src grep: exactly ONE production ClearMembershipFilter call site (the gated else). Stalled-node regression :229-231 asserts the filter remains installed; mutation evidence in 15-17-SUMMARY records the reverted-branch failure at exactly :229 (line matches current source). Suite green (14/14 battery) |
| 2 | WR-C2-01 — SecureCrdt element filters fail OPEN on weak_ptr expiry | **CLOSED** | SecureCrdt.cpp:686-692: expired-owner branch returns `std::vector<sgns::crdt::pb::Element>{}` (REJECT) with the WR-C2-01/CR-C2-01 comment; candidate filter :708 same policy; no `return std::nullopt;` remains inside RegisterFilters (:666-715, read in full). Behavioral proof ElementFilterRejectsOnSecureCrdtExpiry (network_registry_test.cpp:878-970): bare entry registered, tpr_ pin released BEFORE the RegisterFilters re-run (:893-894 < :900), attacker datastore connected, owner expired (:929), unsigned branch element rejected (control replicates, branch Get has_error :960-966); mutation evidence records revert failures at :962/:965 (lines match current source) — which also proves weak_self genuinely expired (a live owner would crash on entry.make_instance's null std::function, not silently land). 16/16 cases green |

## Observable Truths (full re-verification of the 15-REVERIFICATION-2 list)

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | CR-G01 closed: unauthenticated senders denied at all four gates even when from names a member; no member impersonation; both production gossip sites sign | VERIFIED (regression) | Gate sites re-grepped this pass (four OpenGossipPayload sites listed above); sign_messages=true at both production sites; gossip/impostor/unsigned suites green in the 14/14 battery |
| 2 | CR-G02a closed: no ProcessingNode subscription goes live before its filter | VERIFIED (regression) | processing_node.cpp installs :227/:231 (queue channel) and :251/:255 (accessor) BEFORE Listen :291 and CreateResultsChannel/ConnectToSubTaskQueue :303-305; trailing New params :72-73 intact; processing suites green |
| 3 | CR-G02b closed: private node's GlobalDB ingest is membership-gated from its first topic subscription | VERIFIED (regression) | Interim MakeBootstrapMembershipFilter(network_bootstrap_peers_) at GeniusNode.cpp:774-784 strictly before AddListenTopic(:785) (read this pass); startup-window scenes green |
| 4 | G-WR-01 closed: teardown removes the ingest element filter it installed | VERIFIED (regression) | Shared IngestFilterPatternFor helper (:80) used by install (:676) and UnregisterFiltersFor (:717+); TeardownRemovesIngestFilter green |
| 5 | G-WR-02 closed: callback-registration failure fails New | VERIFIED (regression) | RegisterCrdtChangeCallback returns bool (NetworkRegistry.cpp:487), checked at :407; CallbackRegistrationFailureFailsNew green |
| 6 | G-WR-04 closed: registration is atomic-detecting | VERIFIED (regression) | RegisterIfAbsent (SecureCrdtRegistry.hpp:158) used at NetworkRegistry.cpp:484; securecrdt_registry_test green |
| 7 | Fail-closed throughout: no code path on a private node restores public pass-through ingest while its GlobalDB is still live | **VERIFIED (flipped this cycle)** | Deny-all-on-private teardown branch + gated destruction-route clear (row 1 above); element filters reject on expiry (row 2); whole-src ClearMembershipFilter sweep shows the single gated call site; the account-switch path (ShutdownAccountBoundServices :2291+) stops blockchain_ but never touches the broadcaster membership filter, so the CR-01 window does NOT reopen public pass-through on private nodes; PrivateNodeWithoutBootstrapMembershipFailsClosed + TeardownClearsBroadcasterMembershipFilter both green (deny-all on the stalled path, clear on the destruction path) |
| 8 | Public nodes keep byte-identical raw behavior (no filter -> raw publish/receive) | VERIFIED | Interim install guarded by !private_network_id_.empty() (:774); teardown clear branch is a no-op on a node that never installed a filter; public-node scenes green across the battery |
| 9 | Goal clause 1 regression: privateNetworkId identity consumption + fail-closed provisioning intact | VERIFIED | network_config_private_network_test green (14/14 battery); 15-17 diff did not touch the config chain |
| 10 | Goal clause 3 regression: chain topics and CRDT keys isolated by network | VERIFIED | task_keys_scope_test + validator_registry_scope_test green; 15-17 diff did not touch scope plumbing |

**Score: 10/10 truths verified.** Truth 7 flipped FAILED -> VERIFIED this cycle; no regressions in any previously-verified truth.

## Cycle-3 Code Review Treatment (15-REVIEW.md, 1 critical + 8 warnings + 6 info)

| Finding | My verification | Disposition |
|---|---|---|
| CR-01 — ValidatorRegistry::InitializeCache trusts persisted registry bytes (parse-only, no VerifyUpdate) | CONFIRMED as a real defect in src/blockchain/ValidatorRegistry.cpp, but PRE-EXISTING and outside phase-15 scope: git log shows InitializeCache created in f06854122 ("Feat: Adding cache registry", pre-phase-15); the only phase-15 commit touching the file (d0ef6c89, 15-07) rerouted registry_key_/topic derivation through ScopedIdentifier members and did not change caching semantics (verified via git show hunks). The exposure window (Blockchain::Stop unregisters element filters while GlobalDB lives during SelectAccount) also predates the phase. On private nodes the broadcaster membership filter remains installed through that window (ShutdownAccountBoundServices does not touch it — verified), so truth 7 is not implicated; the residual is member-authenticated forged registry updates on private nets and any-peer on public nets — a consensus-layer concern independent of the phase goal | WARNING — file a follow-up todo / carry into the next milestone's hardening work (fix shape in the review: VerifyUpdate in InitializeCache + defense-in-depth in RegistryUpdateReceived). NOT a phase-15 gap; no later phase exists to defer it to (phase 15 is last in this roadmap section) |
| Review WR-01 — reject-on-expiry not applied to Blockchain/ValidatorRegistry/TransactionManager element filters (still nullopt = accept on expiry) | CONFIRMED structurally; reviewer notes branches appear unreachable today (owners deterministically unregister in Stop()/Close()). Same policy class as WR-C2-01 but in files outside the 15-17 scope (which fixed the phase's policy-stack SecureCrdt filters) | WARNING — recommend the same reject-on-expiry mirroring in a follow-up (pairs naturally with the CR-01 fix) |
| Review WR-02..WR-08 (NodeState formatter gaps, example UPnP config, GrabTask lock leak, escrow strand on enqueue failure, 8x copy-pasted seal/publish block, TPR null deref, negative reconnect intervals) | Quality/robustness items in phase-15-adjacent files; none affects the ten scored truths (formatter and example-config items predate or ride on pre-existing infra) | WARNING/INFO — follow-up hygiene; WR-06 (seal/publish duplication) is the drift risk most worth folding into future work |
| Carried WR-C2-02/03/04 (grid ordering contract, clear-propagation asymmetry, Register re-lock race) | Unchanged dispositions from cycle 2 (latent, defense-in-depth); no production defect introduced by 15-17 | WARNING — unchanged, documented |

## Required Artifacts (15-17 delta, all verified this pass)

| Artifact | Expected | Status | Details |
|---|---|---|---|
| src/account/GeniusNode.hpp | ShutdownNodePolicyServices(bool global_db_shutdown_follows = false) declaration | VERIFIED | :1327 + Doxygen @param :1320 |
| src/account/GeniusNode.cpp | deny-all-on-private teardown branch + rewritten contract comment | VERIFIED | Guard :2385, MakeBootstrapMembershipFilter({}) :2388, warn :2389-2391 (public id only), clear :2395 (sole production site), true call :2481 -> ShutdownNow :2484 |
| src/securecrdt/SecureCrdt.cpp | element-filter reject-on-expiry | VERIFIED | :686-692 empty-vector return + comment; no nullopt in RegisterFilters |
| test/src/account/private_network_registry_binding_test.cpp | stalled-node filter regression | VERIFIED | :223-231 EXPECT inside PrivateNodeWithoutBootstrapMembershipFailsClosed, names CR-C2-01; TeardownClearsBroadcasterMembershipFilter (:251+) unchanged and green |
| test/src/networkregistry/network_registry_test.cpp | ElementFilterRejectsOnSecureCrdtExpiry behavioral proof | VERIFIED | :878-970; no MakeRegistry in the scene; pin-release order correct; binary lists exactly 16 cases (3 + 13) |

## Key Link Verification (15-17 delta)

| From | To | Via | Status |
|---|---|---|---|
| GeniusNode failure paths (:1084/:1094/:1137) | SetMembershipFilter(MakeBootstrapMembershipFilter({})) | ShutdownNodePolicyServices() default on a private node | WIRED |
| GeniusNode::ShutdownForDestruction (:2481) | ClearMembershipFilter | ShutdownNodePolicyServices(true) immediately before ShutdownNow (:2484) | WIRED |
| SecureCrdt::RegisterFilters element lambda | expired-owner policy | !weak_self.lock() -> empty vector (reject) | WIRED |
| Regression tests | test accessors | BroadcasterMembershipFilterInstalled / two-datastore attacker pattern | WIRED (both scenes exercise the real paths; mutation-verified per 15-17-SUMMARY with line-corroborated failure output) |

## Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Binary freshness | ninja -C build/OSX/Release genius_node securecrdt networkregistry network_registry_test private_network_registry_binding_test | 19/19 OK (only the known pre-existing -Wswitch warning at GeniusNode.cpp:4865) | PASS |
| Full 14-suite phase-15 battery | ctest -R "^(network_membership_filter_test\|network_registry_test\|private_network_registry_binding_test\|network_config_private_network_test\|securecrdt_registry_test\|securecrdt_quorum_gate_test\|burnconfig_test\|trustedpeerregistry_quorum_test\|processing_service_test\|processing_subtask_queue_channel_pubsub_test\|processing_core_gating_test\|pubsub_counts_test\|task_keys_scope_test\|validator_registry_scope_test)$" | 100% passed, 0 failed (196.46s) | PASS |
| New scene registration | network_registry_test --gtest_list_tests | 16 cases (NetworkMembershipPayloadTest 3 + NetworkRegistryTest 13); ElementFilterRejectsOnSecureCrdtExpiry listed | PASS |
| Sole clear-site invariant | grep -rn ClearMembershipFilter src/ (excl. broadcaster impl) | exactly 1: GeniusNode.cpp:2395 (gated else) | PASS |
| sign_messages production sites | grep both sites | = true at GeniusNode.cpp:1920, GlobalDbNetworkComposition.cpp:200 | PASS |
| Commits | git log | 66e67ab9, ae662f0b present; HEAD-area docs commits only after | PASS |
| Mutation evidence corroboration | SUMMARY failure lines vs current source | binding :229 and registry :962/:965 match exactly | PASS |

## Probe Execution

SKIPPED — no probe scripts declared in any phase-15 plan (no `scripts/*/tests/probe-*.sh` in repo; verification uses ninja/ctest/grep, all executed above).

## Requirements Coverage

REQUIREMENTS.md has no phase-15 entries (160 lines, no phase-15 section) — decision-ID accounting unchanged from prior cycles:

| ID | Status | Evidence |
|---|---|---|
| D-01..D-05, D-08, D-09, D-10 | SATISFIED | suites green across cycles; untouched by 15-17 |
| D-06 / PNET-NETREG | SATISFIED | registry lifecycle closures intact (truths 4-6); network_registry_test 16/16 |
| D-07 / PNET-GATE | SATISFIED | authenticated gates + interim startup filter + fail-closed teardown now end-to-end (truth 7 flipped) |
| D-11 / PNET-PROC | SATISFIED | three processing gates authenticated, pre-subscription installs intact; WR-C2-02 ordering contract remains a documented latent warning |
| PNET-CFG | SATISFIED | unchanged; network_config_private_network_test green |
| PNET-REG/VAL/SCOPE | SATISFIED | scope + membership suites green |

Orphaned requirements: none (REQUIREMENTS.md maps no IDs to phase 15).

## Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| src/blockchain/ValidatorRegistry.cpp | 1990-2026 | InitializeCache trusts persisted registry bytes without signature re-verification (review CR-01) | Warning (pre-existing, out of phase scope) | Forged registry can poison consensus weighting after an unfiltered window; recommend follow-up todo |
| src/blockchain/impl/Blockchain.cpp:98-117, ValidatorRegistry.cpp:1192-1203, TransactionManager.cpp:222-248 | - | Element filters return nullopt (ACCEPT) on owner expiry — reject-on-expiry not mirrored (review WR-01) | Warning (unreachable today per reviewer) | Latent fail-open on consensus-authority paths |
| src/account/GeniusNode.cpp | 3117 | Pre-existing TODO (async job-data posting) | Info | Dates to old commit 4a72b7e22, outside phase-15 work; no TBD/FIXME/XXX in any 15-17-modified region |
| Review WR-02..WR-08, IN-01..06 | - | Quality/robustness items (formatter gaps, example config, lock leak, escrow strand, duplication, null deref, negative intervals, logging nits) | Warning/Info | Follow-up hygiene; none affects the scored truths |

## Human Verification Required

### 1. End-to-end two-node private network job flow (carried forward, cycles 1-3)

**Test:** Provision two GeniusNodes with the same valid private_network_id + network_key + network_bootstrap_peers, publish a job from one, verify processing/replication under /chain/<id>/ keys, and verify a public node observes none of it.
**Expected:** Private job data exists only under the scoped branch; both private nodes replicate it; the public node observes none of it; both private nodes reach READY with filters installed.
**Why human:** No automated suite exercises a live multi-process GeniusNode private network end to end. Closest automated coverage is pubsub_counts_test PnetIsolationAndGaterBlocking (4 hosts: two same-PSK mesh, outside-PSK and gater-blocked controls) — green this verification. Full GeniusNode E2E is additionally blocked by the pre-existing phase-13 quorum-policy regression (blockchain_genesis_test/processing_nodes_test node-ready timeouts; first-bad 76f5fde28; tracked in .planning/todos/pending/genesis-e2e-suites-quorum-policy-regression.md — bisect-attributed to phase 13, not phase 15).

## Gaps Summary

No gaps. The cycle-3 blocker CR-C2-01 and its folded warning WR-C2-01 are closed with code-verified mechanisms and mutation-verified behavioral regressions; all ten scored truths are VERIFIED; the 14-suite battery is green on freshly rebuilt binaries; no regression in any previously-verified truth. Residual items are (a) the one carried-forward manual E2E above — the reason this report is human_needed rather than passed — and (b) review findings CR-01/WR-01..08 which are pre-existing or adjacent-scope defects surfaced by the cycle-3 review, recommended for a follow-up todo/milestone hardening wave, not phase-15 gap-closure plans.

---

_Verified: 2026-09-04T14:23:04Z_
_Verifier: Claude (gsd-verifier)_
