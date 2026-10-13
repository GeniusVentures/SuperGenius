---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
verified: 2026-10-02T20:53:35Z
status: passed
score: 11/11 must-haves verified (cycle-5: UAT-1 gap closed by 15-18 with a machine-checked, mutation-verified E2E composition; no human items remain)
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
  previous_status: human_needed
  previous_score: 10/10
  gaps_closed:
    - "UAT-1 — the E2E two-node private-network job flow with public-node control is now covered by an automated test: NetworkMembershipFilterFlowTest.PrivateNetworkJobFlowReplicatesIsolatesAndDeniesOnTeardown (test/src/networkregistry/network_membership_filter_test.cpp:1218-1430, commits eafb89742 + d1ffbc415), joining all three UAT legs — real-data-path scoped job replication (TaskQueueImpl::EnqueueTask + SubTaskResultStorageImpl::AddSubTaskResult), data-level public-node isolation on a live public GlobalDB, and deny-all teardown on a still-live member GlobalDB. Verified green standalone (8657 ms) and in-suite; non-vacuity corroborated against current source (see report)."
  gaps_remaining: []
  regressions: []
---

# Phase 15: Private Network Identity and Binding — Verification Report (Cycle 5, post 15-18)

**Phase Goal:** Private networks consume privateNetworkId identity and bind it through every layer — distinct public private-network identity in network_config parsing, per-network registries/quorums, gossip + processing host bindings, job-scope keys/topics/escrow chain ids under /chain/<privateNetworkId>/, and fail-closed membership enforcement (deny-all ingest on teardown, data-level isolation from public nodes).
**Verified:** 2026-10-02T20:53:35Z
**Status:** passed
**Re-verification:** Yes — fifth pass. Cycle 4 (15-REVERIFICATION-3.md) verified 10/10 truths with one human item (UAT Test 1); the owner converted that item into the UAT-1 gap ("we should have an automated test for this"), closed by plan 15-18 (test-only; commits eafb89742, d1ffbc415).
**Scored against:** the owner-sanctioned replacement posture (authenticated keypair-sealed-envelope membership gates at all four inbound surfaces + fail-closed end-to-end + no enrollment windows), per the two standing overrides above.

## Verdict in One Paragraph

The sole remaining item — automated coverage of the E2E two-node private-network job flow — genuinely exists, is substantive, is wired to the real production data path, and passes on freshly built binaries: I read case (11) `PrivateNetworkJobFlowReplicatesIsolatesAndDeniesOnTeardown` in full (test/src/networkregistry/network_membership_filter_test.cpp:1218-1430) and confirmed all three UAT legs are implemented exactly as planned, with the asserted keys derived by the production `TaskKeys` functions (not hand-rolled strings) whose write sites I re-verified in TaskQueueImpl.cpp:57-76 and processing_subtask_result_storage_impl.cpp:25, and with the deny-all teardown using the same fail-closed `MakeBootstrapMembershipFilter({})` production shape (NetworkMembershipFilter.hpp:116-124). The case passed standalone under my own run (8657 ms), the full network_membership_filter suite passed (13 cases), the CMake-affected sibling network_registry_test passed, and after rebuilding every stale battery target I ran the full 15-suite phase-15 battery: 100% passed (353.97s). Non-vacuity is corroborated independently of the SUMMARY: the recorded mutation-failure messages match the exact assertion strings and key-derivation format in the current source, and the working tree is clean (mutations fully restored). The ten previously-verified truths show no regression — every load-bearing production call site re-grepped present (line numbers shifted by the post-cycle-4 branch-adaptation commits, mechanics unchanged), and the full battery is green. No human items remain: the prior manual-E2E item was re-scoped by owner decision into the automated test that now exists and is green. Residual advisory items are the delta-review warnings on the new test (WR-01 fatal-ASSERT/SIGABRT hazard on failure paths, WR-02 four structurally dead negative keys) plus the carried pre-existing hygiene findings — none affects a scored truth.

## Goal Achievement

| Goal clause | Status | Evidence |
|---|---|---|
| Distinct public private-network identity in network_config parsing | VERIFIED | network_config_private_network_test green (15-suite battery, fresh binaries); identity chain untouched by 15-18 (git show --stat: only 2 test files) |
| Per-network registries/quorums | VERIFIED | network_registry_test (16 cases), securecrdt_quorum_gate_test, trustedpeerregistry_quorum_test, peer_registry_test green; RegisterIfAbsent + SharedPattern construction sites re-grepped |
| Gossip + processing host bindings | VERIFIED | Four authenticated OpenGossipPayload gate sites present (pubsub_broadcaster_ext.cpp:184, processing_service.cpp:304, processing_subtask_queue_accessor_impl.cpp:565, processing_subtask_queue_channel_pubsub.cpp:212); sign_messages=true at GeniusNode.cpp:1877 + GlobalDbNetworkComposition.cpp:191; processing_core_gating_test green including the post-cycle-4 crypto-provider binding fix (bf30c1a86) |
| Job-scope keys/topics/escrow chain ids under /chain/<privateNetworkId>/ | VERIFIED | task_keys_scope_test + validator_registry_scope_test green; ScopedChainId (TransactionManager.cpp:1930) consumed by escrow at :1109 with byte-identical public default; NEW: case (11) proves the scoped key set replicates through the REAL data path (leg a) |
| Fail-closed membership enforcement (deny-all ingest on teardown, data-level isolation from public nodes) | VERIFIED | Deny-all branch at GeniusNode.cpp:2378 with sole production ClearMembershipFilter at :2385 (gated else; destruction route :2534 passes true); interim filter :761 strictly before AddListenTopic :767; NEW: case (11) legs (b)+(c) prove data-level public isolation and deny-all-on-teardown on live GlobalDBs |

## Gap Accounting (UAT-1, the only cycle-4 leftover)

| # | Item | Status | Evidence (code + tests, this verification) |
|---|---|---|---|
| 1 | UAT-1: automated E2E two-node private-network job flow with public-node control | **CLOSED** | Case (11) exists (Level 1), is substantive (Level 2 — 233-line scene read in full, three legs, DI-aliasing assert, bounded waits, wired teardown), is wired (Level 3 — CMake processing_service+SGProcessingProto linked; includes at :48-51; TaskQueueImpl/SubTaskResultStorageImpl/broadcaster/filter APIs all verified against production source), and flows real data (Level 4 — keys derived by production TaskKeys from real EnqueueTask/AddSubTaskResult writes; replication to a separate GlobalDB asserted). Behavioral: standalone green 8657 ms; full suite 13 cases green 33.8-34.0s; sibling green; full 15-suite battery green. Non-vacuity corroborated (below) |

## Observable Truths

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | UAT-1 (15-18 truth a): two same-PSK gated+sealed members replicate a private job's full key set under /chain/<id>/ through the real data path | VERIFIED | Leg (a) at :1297-1339: TaskQueueImpl::New(db, ScopedTopic, kFlowNetworkId) + EnqueueTask + SubTaskResultStorageImpl::AddSubTaskResult on B; four bounded positive waits on A for TaskKey/SubTaskKey/ClaimableTaskKey/SubTaskResultKey — all derived via production TaskKeys; write sites verified at TaskQueueImpl.cpp:57/66/72/76 and storage impl :25. Ran green under my own execution |
| 2 | UAT-1 (15-18 truth b): public control with its own LIVE GlobalDB, unscoped-topic listener, dialed both directions, never holds scoped or public-scope job keys | VERIFIED | Leg (b) at :1341-1364: publicControl built via MakeNode (real GlobalDB + graphsync, SWARM_KEY_OUTSIDE); both-direction dials :1285-1288; transport EXPECT_FALSE both ways :1344-1347; 4s multi-key negative window over 10 keys via AssertKeysNeverPresentWithin (:588-607). 6 of 10 keys are live assertions (4 scoped written keys + TaskKey(task1) + SubTaskResultKey("",result1)); 4 list-key entries are structurally dead (review WR-02, advisory — see Anti-Patterns) |
| 3 | UAT-1 (15-18 truth c): MakeBootstrapMembershipFilter({}) on a still-live member stops further scoped writes; filter persists | VERIFIED | Leg (c) at :1366-1387: deny-all swap :1370 (production shape), HasMembershipFilter EXPECT_TRUE before :1371 and after :1387 the window; new task2/result2 committed via the same queue_b/storage_b :1374-1376; negative window on A :1378-1383. MakeBootstrapMembershipFilter empty-set deny verified at source (NetworkMembershipFilter.hpp:116-124) |
| 4 | Unauthenticated senders denied at all four gates; both production gossip sites sign (CR-G01, regression) | VERIFIED | Four OpenGossipPayload sites re-grepped this pass; sign_messages=true at GeniusNode.cpp:1877, GlobalDbNetworkComposition.cpp:191; gossip/impostor/unsigned suites green in battery |
| 5 | No ProcessingNode subscription live before its filter (CR-G02a, regression) | VERIFIED | processing_node.cpp installs channel filter :236 and accessor filter :261, both before Listen/CreateResultsChannel/ConnectToSubTaskQueue; processing suites green |
| 6 | Private node's GlobalDB ingest gated from first subscription (CR-G02b, regression) | VERIFIED | Interim MakeBootstrapMembershipFilter(network_bootstrap_peers_) at GeniusNode.cpp:761 strictly before AddListenTopic :767 |
| 7 | Fail-closed throughout: no path restores public pass-through on a live private GlobalDB (CR-C2-01, regression) | VERIFIED | Deny-all branch :2378; whole-src sweep: exactly ONE production ClearMembershipFilter (GeniusNode.cpp:2385, gated else); destruction route :2534 passes true with ShutdownNow following; membership suites green |
| 8 | Public nodes keep byte-identical raw behavior | VERIFIED | Interim install guarded by !private_network_id_.empty() (:761 context); public-node scenes green across battery; new case pins the public shape (EXPECT_FALSE HasMembershipFilter :1280) |
| 9 | privateNetworkId identity consumption + fail-closed provisioning intact (regression) | VERIFIED | network_config_private_network_test green; config chain untouched by 15-18 |
| 10 | Chain topics and CRDT keys isolated by network (regression) | VERIFIED | task_keys_scope_test + validator_registry_scope_test green; ScopedChainId + escrow SetChainIdOverride(:1109) re-grepped present |
| 11 | SecureCrdt element/candidate filters reject on owner expiry; teardown removes installed ingest filter (WR-C2-01/G-WR-01 fold, regression) | VERIFIED | SecureCrdt.cpp RegisterFilters uses shared IngestFilterPatternFor (helper consolidated at SecureCrdt.cpp:82; install :692, removal :743 — one construction site invariant preserved across the module move); 2 residual `return std::nullopt` are outside RegisterFilters; securecrdt + networkregistry suites green |

**Score: 11/11 truths verified.** No regressions in previously-verified truths; the one cycle-4 leftover is closed.

## Non-Vacuity Corroboration (independent of 15-18-SUMMARY)

| Check | Result |
|---|---|
| Mutation-A failure messages vs current source | SUMMARY records four timeouts "scoped task/subtask/claimable/subtask result key did not replicate to the other member (timeout: 25000ms)" — byte-matches the assertWaitForCondition message strings at :1315/:1323/:1331/:1339 in the current file |
| Mutation-B failure message vs current source | SUMMARY records "post-teardown scoped write on the deny-all member replicated although it must be denied: /chain/0x6d...7839/processing_1/tasks/pnet_job_task_2" — matches the `what` argument at :1383 + the helper's message format at :598-599, and the key shape matches TaskKeys::TaskKey derivation (ScopePrefix "/chain/<id>" + TaskListKey + "/" + taskId, TaskKeys.hpp:118-126) |
| Mutations restored | `git status` clean at verification start; 15-18 commits touch exactly the 2 declared files (git show --stat) |
| Fresh-binary proof under my own runs | Standalone case PASS (8657 ms); full suite PASS; battery PASS after rebuilding stale targets |

## Required Artifacts (15-18 delta)

| Artifact | Expected | Status | Details |
|---|---|---|---|
| test/src/networkregistry/network_membership_filter_test.cpp | case (11) + AssertKeysNeverPresentWithin helper | VERIFIED | Case :1218-1430 (233 lines, three legs exactly per plan); helper :588-607 (multi-key grace loop, 100ms ticks, per-tick ASSERT + final EXPECT); builders MakeJobFlowTask/SubTask/Result :435-465; includes :23,48-51; using namespace sgns::processing :62; file-header doc line names the case |
| test/src/networkregistry/CMakeLists.txt | job-path link deps | VERIFIED | processing_service + SGProcessingProto appended to NETWORKREGISTRY_TEST_NODE_LIBS (:10-11); both targets link the list; sibling suite built and green |

## Key Link Verification (15-18)

| From | To | Via | Status |
|---|---|---|---|
| case (11) | src/processing/impl/TaskQueueImpl.cpp | TaskQueueImpl::New(db, ScopedTopic, id) + EnqueueTask committing SubTaskKey/TaskKey/ClaimableTaskKey on the scoped topic | WIRED — test :1301-1304; production writes at TaskQueueImpl.cpp:57/66/72, commit on processing_topic_ :76 |
| case (11) | src/processing/impl/processing_subtask_result_storage_impl.cpp | SubTaskResultStorageImpl(db, scoped_topic, id) + AddSubTaskResult | WIRED — test :1306-1307; production write of SubTaskResultKey(m_private_network_id, subtaskid) at :25 |
| case (11) | src/networkregistry/NetworkMembershipFilter.hpp | MakeBootstrapMembershipFilter( {} ) deny-all on still-live member | WIRED — test :1370; production fail-closed empty-set semantics at :116-124 |
| case (11) | src/crdt/globaldb/pubsub_broadcaster_ext.hpp | SetMembershipFilter / SetGossipSigningKey / HasMembershipFilter on both members; nothing on publicControl | WIRED — test :1266-1280; full API present in header (:128/:134/:140/:161/:167) |

## Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|---|---|---|---|---|
| case (11) leg (a) | pnetA->db contents (asserted keys) | TaskQueueImpl::EnqueueTask + SubTaskResultStorageImpl::AddSubTaskResult on pnetB->db, replicated over real gossip+graphsync | Yes — real proto payloads, real CRDT transactions, separate on-disk GlobalDBs (per-node unique basePath) | FLOWING |
| case (11) leg (b) | publicControl->db contents | None expected (negative window) | Yes — control owns a live GlobalDB on the unscoped topic with both-direction dials; window ticks real Get() probes | FLOWING (negative proof) |
| case (11) leg (c) | pnetA->db post-swap contents | Same queue_b/storage_b publishers on B | Yes — task2/result2 are real commits whose arrival is probed and must not occur | FLOWING (negative proof) |

## Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Case registration | binary --gtest_list_tests | NetworkMembershipFilterFlowTest.PrivateNetworkJobFlowReplicatesIsolatesAndDeniesOnTeardown listed; 13 cases total | PASS |
| New case standalone | binary --gtest_filter=...PrivateNetworkJobFlow... | PASSED (8657 ms) | PASS |
| Full membership suite | ctest -R ^network_membership_filter_test$ | 1/1 Passed, 33.81-34.03s (13 cases) | PASS |
| CMake-affected sibling | ctest -R ^network_registry_test$ | 1/1 Passed, 5.52-5.59s (16 cases) | PASS |
| Stale-target rebuild | make -j8 (13 battery targets) | 100% built, no errors | PASS |
| Full phase-15 battery | ctest -R (15 suites) | 100% passed, 0 failed (353.97s) | PASS |
| Sole production clear-site invariant | grep -rn ClearMembershipFilter src/ (excl. broadcaster impl) | exactly 1: GeniusNode.cpp:2385 (gated else) | PASS |
| sign_messages production sites | grep both sites | = true at GeniusNode.cpp:1877, GlobalDbNetworkComposition.cpp:191 | PASS |
| 15-18 commits | git show --stat eafb89742 d1ffbc415 | exactly the 2 declared test files; no production code touched | PASS |
| Working tree | git status | clean (mutations restored) | PASS |

## Probe Execution

SKIPPED — no probe scripts declared in any phase-15 plan and none under scripts/ (verified: no `scripts/*/tests/probe-*.sh` exists). Verification uses make/ctest/grep, all executed above.

## Requirements Coverage

REQUIREMENTS.md has no phase-15 entries (0 matches for phase 15; known, documented gap since 2026-09 — per instruction, not failed on). Decision-ID accounting from 15-CONTEXT.md, updated with the 15-18 strengthening:

| ID | Status | Evidence |
|---|---|---|
| D-01 | SATISFIED | network_config_private_network_test green; config chain untouched by 15-18 |
| D-02 | SATISFIED (strengthened) | case (11): id drives scoped topic + /chain/<id>/ keys; network_key drives pnet — both-direction cross-PSK dials prove isolation at the data level |
| D-03 | SATISFIED | Prior-cycle verified (registry stores non-secret metadata only; teardown warn logs public id only) |
| D-04 | SATISFIED | peer_registry_test green (post-cycle-4 adaptation c651a16ce rebuilt and green this cycle) |
| D-05 / D-06 | SATISFIED | trustedpeerregistry_quorum_test + network_registry_test green; RegisterIfAbsent atomic registration intact (NetworkRegistry.cpp:484) |
| D-07 / PNET-GATE | SATISFIED (under override) | Authenticated gates at four surfaces + interim startup filter + fail-closed teardown + NEW end-to-end machine-checked composition (case 11) |
| D-08 | SATISFIED (strengthened) | Real-data-path scoped job replication machine-checked; task_keys_scope_test green; public scope byte-identical (empty-scope overloads return public forms) |
| D-09 | SATISFIED | validator_registry_scope_test green |
| D-10 | SATISFIED | Historical process constraint; phase-13 quorum regression tracked separately, phase-15 suites avoid those fixtures by design |
| D-11 | SATISFIED | processing_core_gating_test green incl. post-cycle-4 explicit crypto-provider binding (bf30c1a86); pubsub_counts_test green (4-host pnet isolation + gater blocking) |
| PNET-CFG / PNET-NETREG / PNET-PROC / PNET-REG/VAL/SCOPE | SATISFIED | Corresponding suites green in the 15-suite battery |

Orphaned requirements: none (REQUIREMENTS.md maps no IDs to phase 15).

## Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| test/src/networkregistry/network_membership_filter_test.cpp | 1301-1304, 1374-1375 | Delta-review WR-01: fatal ASSERTs (ASSERT_TRUE(queue_b), ASSERT_FALSE EnqueueTask has_error) run while io_thread is joinable — a failed precondition would SIGABRT the binary and discard subsequent cases' results (harness robustness on failure paths only; green runs unaffected) | Warning | Recommend the review's RAII join guard or hoisting TaskQueueImpl::New above the thread start in a follow-up; does not affect truth validity |
| test/src/networkregistry/network_membership_filter_test.cpp | 1354-1355, 1359, 1362 | Delta-review WR-02: four of the ten leg-(b) negative keys (TaskListKey/ClaimableListKey scoped and public forms) are never written by any production path — structurally dead assertions inflating apparent coverage; the case comment slightly overstates "ANY /chain/<id>/ key" (window spot-checks rather than sweeps the prefix) | Warning | The 6 live keys (4 scoped written keys + TaskKey(task1) + SubTaskResultKey("",result1)) prove the isolation truth; recommend dropping the dead entries or adding a QueryKeyValues prefix sweep in a follow-up |
| test/src/networkregistry/network_membership_filter_test.cpp | 457-465, 1228-1231 | Delta-review IN-01: result ids (pnet_job_result_1/2) do not reference real subtask ids — modeled job has results mapping to no subtask (self-consistent but misleading) | Info | Cosmetic; rename or pass sub ids in a follow-up |
| test/src/networkregistry/CMakeLists.txt | 10-11 | Delta-review IN-02: shared lib list links processing_service + SGProcessingProto into network_registry_test which does not use them | Info | Established directory idiom; build-graph cost only |
| src/blockchain/ValidatorRegistry.cpp | ~1990-2026 | Carried cycle-3/4 review CR-01: InitializeCache trusts persisted registry bytes (parse-only, no VerifyUpdate) — PRE-EXISTING, out of phase scope | Warning | Follow-up hardening todo recommended (pairs with reject-on-expiry mirroring in Blockchain/ValidatorRegistry/TransactionManager element filters — carried review WR-01) |
| src/account/GeniusNode.cpp | ~3117 region | Pre-existing TODO (async job-data posting), predates phase 15 | Info | Outside phase-15 work; no TBD/FIXME/XXX in any 15-18-modified region (grep clean) |

## Human Verification Required

None. The sole carried item (manual E2E two-node private-network job flow) was re-scoped by owner decision into an automated-test requirement (UAT-1); that requirement is now delivered, stable, and mutation-verified non-vacuous. All phase-goal clauses are machine-checked by the green 15-suite battery. For context only: a full multi-process GeniusNode E2E remains untestable due to the pre-existing phase-13 quorum-policy regression (blockchain_genesis_test/processing_nodes_test; tracked in .planning/todos/pending/genesis-e2e-suites-quorum-policy-regression.md) — out of phase-15 scope, deliberately avoided by this phase's suites.

## Gaps Summary

No gaps. UAT-1 is closed with a code-verified, green, non-vacuous automated composition; all eleven scored truths are VERIFIED; the full 15-suite battery passes on freshly rebuilt binaries; no regression in any previously-verified truth. Residual items are advisory follow-ups (delta-review WR-01/WR-02/IN-01/IN-02 on the new test, carried pre-existing hardening warnings) — recommended for a future hygiene pass, not phase-15 gap-closure plans. Bookkeeping note for the orchestrator: 15-HUMAN-UAT.md Test 1 can now be recorded as covered by network_membership_filter_test case (11).

---

_Verified: 2026-10-02T20:53:35Z_
_Verifier: Claude (gsd-verifier)_
