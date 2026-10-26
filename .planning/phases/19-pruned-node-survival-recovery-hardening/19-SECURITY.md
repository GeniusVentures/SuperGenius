---
phase: 19-pruned-node-survival-recovery-hardening
audited: 2026-10-26
auditor: gsd-security-auditor
asvs_level: 1
block_on: high
branch: gsd/phase-20-reclamation-proof-e2e-hardening
threats_found: 35
threats_closed: 35
threats_open: 0
threats_accepted: 9
status: secured
---

# Phase 19 Security Audit — Pruned-Node Survival & Recovery Hardening

Audit of the implemented phase-19 surface (commits `fda039d46^..1bc84068b`,
the 19-01..19-07 series, plus the review-fix commit `c6f4b17d4` and its docs
companion `36ce2d258`; span verified an ancestor of the audited branch HEAD
`2cfa9f226`). Every mitigation was verified against the source on disk —
documentation, SUMMARY "Threat Mitigations" claims, and the review report's
"FIXED" lines were not accepted as evidence; each mitigation (including all
three WR-01..03 review fixes) was re-located in the live tree.

**Surface audited:** the D-01 consumed-preserving clear
(`UTXOManager::ClearUnspentKeepConsumed`) and the checkpoint-rebuild guard
chain (preserve-clear swap, PRUNED marker consult, own-replay discriminator,
`ReadDurableSpendEvidence` + `CrosscheckRebuiltUtxos` belt); the D-02 typed
`PRE_BROADCAST_VALIDATION` error + READY-preserving TickOnce branch + bounded
gap-guarded SYNCING exit; the SC-5 `RetryDueFailedRoots` isResolved guard; the
D-03 tri-state `LookupTransaction` + GeniusNode passthrough + `tx` CLI; and
the survival/mesh/restart/tri-state proof suites.

**Live-tree caveat:** phase 20 stacks on top of 19 in this branch. Line
numbers below are from the current tree; where phase-20 evolved a shape, the
phase-19 mitigation property was re-verified in its evolved form.

## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| durable /utxo/ store ↔ in-memory UTXO registry | the preserve-clear and the rebuild belt cross it; lost or misread evidence here reopens the resurrection vector |
| checkpoint record ↔ rebuild replay | THE phase integrity boundary: an attacker-inducible mismatch (e.g. one corrupted /utxo/ record) must not re-authorize a spent output |
| replay path ↔ double-spend rule | the own-replay discriminator must never accept a FOREIGN spender |
| send-path state machine ↔ nonce store | the bounded SYNCING exit reads the durable anchor; touching pending_nonces_ creates nonce collisions |
| pre-Commit ↔ post-Commit failure classes | misclassification sends the TM to SYNCING on a local failure (wedge) or keeps it READY after publication (divergence) |
| retry scheduler ↔ sealed/pruned evidence | a guard placed after the counter (or skipped on error) burns doomed fetch attempts |
| restart window ↔ fetch machinery | any doomed pruned-CID fetch during restart is both a DoS vector and a SURV-01 failure |
| local query surface ↔ pruned-evidence markers | conflation (pruned reported as never-existed) hides state and destroys auditability |
| messenger surface ↔ pruned map | the serving leg must fail soft (empty response), never error-propagate or crash |
| requester ↔ pruned server | the refusal must be clean, immediate, and CID-scoped |
| fresh node ↔ route discovery | routes come from broadcasts; a missing serving route turns bootstrap into a retry-ladder DoS |

## Threat Register Verification

| Threat ID | Category | Component | Disposition | Status | Evidence |
|-----------|----------|-----------|-------------|--------|----------|
| T-19-0101 | Tampering | ClearUnspentKeepConsumed | mitigate | CLOSED | `src/account/UTXOManager.cpp:535-576` — CONSUMED survivors kept in `utxo_outpoints_` and `address_outpoints_` (:550-557) with `spent_by_txid` intact; rollback snapshot discipline preserved. Disk+memory proof: `test/src/account/utxo_manager_test.cpp:866` (memory `GetOutPointSpender` :919-921, raw `/utxo/` prefix read with `spent_by_txid` bytes :925-935, reload leg :955-966), tombstone+genuine survival :969 |
| T-19-0102 | Tampering | ClearUnspentKeepConsumed | mitigate | CLOSED | `UTXOManager.cpp:549-558` — `local_reservations_.erase` for every address outpoint and `utxo_outpoints_.erase` for every non-CONSUMED entry (no stale READY survivor can re-mint); suite asserts READY/RESERVED gone + reservation-drop via `TryReserveOutpoint` outcome (19-01-SUMMARY); wipe negative control `KeepConsumedNegativeControlSetUTXOsStillWipesEverything` (:1034) proves `SetUTXOs` byte-identical |
| T-19-0103 | Tampering (integrity) | /utxo/ persistence | mitigate | CLOSED | `UTXOManager.cpp:564` — the clear persists ONLY through `StoreUTXOsLocked` (the sole store call in the function); no second `/utxo/` writer shape introduced anywhere in the span |
| T-19-01SC | Supply chain | package installs | accept | CLOSED (accepted) | Zero registry installs verified against the span diff `fda039d46^..36ce2d258`: no package/manifest files touched; the only build change is two test-target registrations in `test/src/account/CMakeLists.txt` (`addtest(account_messenger_test)`, `addtest(tx_lookup_tri_state_test)`) with no fetch/find_package/install |
| T-19-0201 | Tampering (nonce collision) | SyncNonce bounded exit | mitigate | CLOSED | Fallback body `src/transaction/TransactionManager.cpp:3724-3756` contains NO `ReleaseNonce`/`ReserveNextNonce`/`pending_nonces_` mutation — held reservations stay held; collision proof `test/src/account/transaction_manager_pending_lifecycle_test.cpp:1109-1148` (`SyncingExitFallsBackToDurableNonceAfterWindow`: `EXPECT_GT(ReserveNextNonce(), held_nonce)` with a reservation held across the fallback fire) |
| T-19-0202 | Tampering (misclassification) | TickOnce error site | mitigate | CLOSED | Typed `Error::PRE_BROADCAST_VALIDATION` (`TransactionManager.hpp:125`, category mapping `TransactionManager.cpp:53-54`); the three preflight sites (:2108 message_size-class, :2119, :2130) sit in the pre-publish loop BEFORE the first `crdt_transaction->Put` (:2183)/Commit; the three post-Commit sites keep bare `std::errc` codes (post-:2204) — genuinely SYNCING territory; negative control `PreBroadcastNegativeControlNonTypedFailureStillEntersSyncing` (test :1087) |
| T-19-0203 | DoS (wedged TM) | TickOnce/SyncNonce | mitigate | CLOSED | Half 1: `TransactionManager.cpp:583-624` — PRE_BROADCAST branch textually precedes `ChangeState(State::SYNCING)` (:629) and contains no ChangeState (TM stays READY); rollback + pop with the WR-03 cap below. Half 2: bounded exit :3724-3755 (60s default, `k_syncing_exit_timeout` hpp:864). Regression pair: `PreBroadcastFailureKeepsManagerReadyWithNonceRelease` (test :1046, nonce reuse :1072) + `SyncingExit*` (:1109, :1155) |
| T-19-0204 | Repudiation | fallback logging | mitigate | CLOSED | WARN at `TransactionManager.cpp:3743-3747` names the window (ms), the durable anchor value (or "none"), and the READY return; the behind-case WARN (:3752-3753) is equally loud — the recovery is observable, never silent |
| T-19-02SC | Supply chain | package installs | accept | CLOSED (accepted) | Zero registry installs in the span (see T-19-01SC evidence) |
| T-19-0301 | DoS (fetch storm via fail-open guard) | RetryDueFailedRoots | mitigate | CLOSED | `src/crdt/impl/crdt_datastore.cpp:1841-1869` — `isResolved` guard (:1847) textually precedes BOTH `DeleteCIDBlock` (:1863) and `EnqueueRootCID` (:1864, the counter site); error branch logs + continues (:1848-1853, never enqueue on unknown state); resolved branch terminal-skip log + `ClearFailedRootRetryLocked` (:1854-1861). Tests: `RetryDueFailedRootsSkipsPrunedRootWithZeroFetchAttempts` (`globaldb_erase_test.cpp:764`) + unpruned negative control `:799` |
| T-19-0302 | DoS | RetryStalledDeltas | accept (verify-only) | CLOSED (accepted) | Deliberately local-only path unchanged: `crdt_datastore.cpp` RetryStalledDeltas region uses `GetNodeWithoutRequest` only — zero fetch calls; pinned by `StalledDeltaPrunedBlockGivesUpLocallyWithZeroFetchAttempts` (`globaldb_erase_test.cpp:837`, zero counter movement + bounded give-up :886-893). Documented in 19-03-PLAN threat_model |
| T-19-0303 | Information disclosure / DoS | AccountMessenger tx leg | accept (verify-only) | CLOSED (accepted) | Production AccountMessenger untouched in the span (diff-stat confirms no `src/account/AccountMessenger.*` change); graceful empty signed BlockResponse pinned with presence+absence assertions by `PrunedTxRequestIsAnsweredWithEmptySignedBlockResponse` (`test/src/account/account_messenger_test.cpp:100`). Documented in 19-03-PLAN threat_model |
| T-19-0304 | Tampering (test vacuity) | all four test families | mitigate | CLOSED | Negative controls: unpruned failed root still retries with counter advance (`globaldb_erase_test.cpp:799`, `:826`); counter brackets reset before every window (:772-784, :860-891, ResetRootFetchAttemptsForTest discipline); PrunedSubject's prune non-vacuous via pre-erase CRDT-lookup precondition (19-03-SUMMARY) |
| T-19-03SC | Supply chain | package installs | accept | CLOSED (accepted) | Zero registry installs in the span |
| T-19-0401 | Tampering / EoP | checkpoint rebuild | mitigate | CLOSED | The D-01 chain, all three links located: (1) preserve-clear swap in the mismatch branch `TransactionManager.cpp:3377` (`ClearUnspentKeepConsumed` — the sole call site; WARN wording names preserved evidence :3368-3369); (2) PutUTXO existing-entry duplicate check by construction (comment :3371-3376); (3) the belt `CrosscheckRebuiltUtxos` (:3400 call gated on `mismatch_rebuild`; impl :3489-3564) consults the DURABLE record through owner-vended `ReadDurableSpendEvidence` (:3520) and reverts via the owner's `ConsumeUTXOs` with the record's spender (:3542-3545). Tests: `RebuildGuardNoResurrectionKeepsPreservedConsumeEvidence` (`tx_pruner_test.cpp:4058`, spent outpoint CONSUMED + raw-disk `spent_by_txid` proof :4085-4091) and the belt induction `:4161` (CONSUMED entry removed → PutUTXO re-creates READY → tampered durable record → crosscheck reverts, `reverted==1`, post-state CONSUMED with spender :4196-4204). Plan deviation recorded in 19-04-SUMMARY: the belt reads the durable record instead of the map-based `GetOutPointSpender` because a READY registry entry structurally cannot carry a spender — stronger than planned, same boundary |
| T-19-0402 | Tampering (bypass via false own-replay) | ParseTransferTransaction | mitigate | CLOSED | Discriminator `TransactionManager.cpp:2712-2732` — `own_replay` requires spender-of-record == this tx hash for EVERY consumed input (`all_of` with `GetOutPointSpender`); any other spender leaves `own_replay` false and the reject stands (:2739-2743 "Rejected double-spending transfer"); foreign arm of `RebuildGuardOwnReplayReappliesSpenderChangeWithoutDoubleSpendReject` drives a foreign certified spender through the CONFIRMED parse path and asserts error + no credited output (`tx_pruner_test.cpp:4146-4154`) |
| T-19-0403 | DoS (unbounded re-request loops) | missing-set insert | mitigate | CLOSED | `TransactionManager.cpp:3446-3482` — `PruneEligibility::LookupPrunedEntry` (:3457) consulted BEFORE the sole `missing_tx_hashes_.insert` in InitializeUTXOs (:3481, grep-verified single site); clean PRUNED verdict → INFO naming the hash + skip (:3465-3471); NOT_FOUND / VALUE_PRESENT_INVARIANT_VIOLATION / lookup error → WARN + the 18-landed bounded insert unchanged (never guess). Tests: `RebuildGuardMarkerConsultSkipsLocallyPrunedCreatingTx` (:4210, `HasMissingTxHashes()==false`) + unmarked negative control (:4228, `==true`) |
| T-19-0404 | Tampering (vacuous crosscheck) | InitializeUTXOs crosscheck | mitigate | CLOSED | The belt consult reads the record the clear preserved (disk-level assertion in the NoResurrection case, :4085-4091, proves the evidence path); the MANDATORY induction fires the belt branch for real — no exercised-by-construction downgrade (`tx_pruner_test.cpp:4157-4205`, belt arm asserts pre-state READY :4185-4187, divergence armed by `DeleteUTXO` + `PutUTXO` + `TamperUtxoRecordToConsumed`, post-state CONSUMED) |
| T-19-0405 | Repudiation | rebuild logging | mitigate | CLOSED | Every GATE-05 skip logs naming the pruned ref: lookup-error WARN (:3460-3463), PRUNED INFO (:3467-3469), invariant-violation WARN (:3474-3476); the belt WARN names outpoint, spender, and genuine-vs-tombstone (:3535-3541) plus a reverted-count summary (:3557-3559) |
| T-19-04SC | Supply chain | package installs | accept | CLOSED (accepted) | Zero registry installs in the span |
| T-19-0501 | Repudiation / Info disclosure | LookupTransaction | mitigate | CLOSED | `TransactionManager.cpp:4005-4096` — distinct PRUNED verdict with record payload (:4079-4080); hard errors from either found-source (:4030-4034) or the marker lookup (:4069-4075) propagate via `outcome::failure`, never downgraded to NOT_FOUND; invariant violations log ERROR and report the violation verdict, never a fabricated one (:4055-4062, :4086-4091). Test `PrunedAnswersWithRecordPayload` (`tx_lookup_tri_state_test.cpp:250`) asserts the record payload, not just the enum |
| T-19-0502 | Tampering (misorder race) | value/marker read order | mitigate | CLOSED | Value-first-then-marker ordering in source order (:4013-4036 both found-sources; marker consults only at :4047/:4068 after a miss), justified by the Phase-16 one-batch invariant (comment :4013-4017); a found value outranks any marker (:4038-4042); `InvariantViolationLoudWhileReturningFoundTx` (test :290) proves the found value outranks the marker |
| T-19-0503 | Tampering (logic at the boundary) | GeniusNode/NodeExample | mitigate | CLOSED | `src/account/GeniusNode.cpp:4700-4709` — one-line delegation with the sibling's not-ready gate (mirrors `GetTransactionStatusByTxId` :4692-4697); `cmd_tx` (`example/node_test/NodeExample.cpp:404-439`) maps verdict-to-text only ("history pruned" :426), comment pins invariant 3; registered in COMMANDS (:559); grep of `gRPCForSuperGenius/` and `evmrelay/` finds zero `LookupTransaction` occurrences — the D-03 RPC deferral honored |
| T-19-0504 | Tampering (duplicated vocabulary) | TM lookup | mitigate | CLOSED | `PrunedLookup` reused verbatim — `TransactionLookupResult` (hpp:89+) carries a `PrunedLookup` verdict, no second enum in TM; `LookupPrunedEntry` is the only marker read (call sites :4047/:4068, plus the 19-04 consult) |
| T-19-05SC | Supply chain | package installs | accept | CLOSED (accepted) | Zero registry installs in the span |
| T-19-0601 | DoS (restart fetch storm) | restart window | mitigate | CLOSED | Counter-bracketed zero-fetch across the ENTIRE restart window: `tx_pruner_test.cpp:1345` (reset before StartCore), `:4268` (delta 0 after the READY wait), per-leg brackets `:4379→4409`, `:4416→4441`, `:4449→4502` (tip/receive/outgoing legs); seam companion `:4332→4337` |
| T-19-0602 | Tampering (vacuous READY proof) | real-boot leg | mitigate | CLOSED | A real `tm_->StartCore()` call (`tx_pruner_test.cpp:1041`) with a bounded READY wait (:1049) and NO ForceManagerReady anywhere on the leg (`RestartMatrix_RealBootPrunedNodeReachesReadyWithZeroPrunedCidFetches` :4259); the seam path is a companion (`:4318`), never the only leg |
| T-19-0603 | Tampering (flaky wait) | READY wait | mitigate | CLOSED | Bounded `ASSERT_WAIT_FOR_CONDITION` with failure messages (:1049); the READY strategy shipped as (a) nonce responder with the measurement rationale recorded in the suite doc (:4252-4256 — strategy (b) pre-seed cannot fire on this fixture, CheckNonce read in full); double-run via `INSTANTIATE_TEST_SUITE_P( RestartMatrixDoubleRun, ...)` (:4505) |
| T-19-0604 | Tampering (nonce chain regression) | outgoing leg | mitigate | CLOSED | Outgoing nonce asserted against the durable chain (`EXPECT_EQ(dag.nonce(), kExpectedNextNonce)` :4481) and `previous_hash` by comparison with the ladder output (`EXPECT_EQ(dag.previous_hash(), ladder_hash)` :4482) — asserted, not re-derived; neither pruned tx's hash accepted (19-06-SUMMARY) |
| T-19-06SC | Supply chain | package installs | accept | CLOSED (accepted) | Zero registry installs; test-only plan |
| T-19-0701 | DoS (failover storm) | refusal path | mitigate | CLOSED | `PrunedPeerRefusalFailsOverToServingPeer` (`globaldb_erase_test.cpp:936`): fetch completes via the full node with ladder-drains-to-baseline assertion (:1020+); the CID-scoped-not-connection-blacklist assertion proves the light peer still serves fresh data afterward (:1024-1025+); attempt bounds recorded in 19-07-SUMMARY (<=8 RefusalFailover, <=4 PrunedMesh Phase B) |
| T-19-0702 | DoS (route starvation) | PrunedMesh wiring | mitigate | CLOSED | `FreshNodeBootstrapsPrunedHistoryServedByFullNode` (`globaldb_erase_test.cpp:1130`): the serving route is an asserted precondition (routes registered via `AddMultiCIDInfo` before the fetch window, :1000-1005 wiring discipline); ladder stays exactly at baseline — a starved ladder ride would leave entries; the fresh node's only route source is the full node |
| T-19-0703 | Tampering (guessed wire constant) | refusal status | mitigate | CLOSED | No status constant asserted anywhere in the family; behavior asserted (failover + CID-specific classification + storage-deterministic miss: `GetNodeFromMerkleDAG(entryA)` failing on the pruned node is the responder's own storage key); the observed transport behavior recorded in 19-07-SUMMARY key-decisions (A5 record), not hard-coded |
| T-19-0704 | Tampering (flaky multi-node green) | topology | mitigate | CLOSED | Double-run discipline, bounded `ASSERT_WAIT_FOR_CONDITION` windows with messages, deterministic teardown order (the shared EraseNodeCollection destructor path), fresh per-test temp dirs — all present in the suites; 8x sweeps + full ctest green recorded in 19-07-SUMMARY verification evidence |
| T-19-07SC | Supply chain | package installs | accept | CLOSED (accepted) | Zero registry installs; test-only plan |

## Accepted Risks

| Threat ID | Risk | Rationale | Bound |
|-----------|------|-----------|-------|
| T-19-01SC..T-19-07SC (7) | Package-registry supply chain | Zero registry installs across all seven plans — verified against the span diff (no package/manifest files; only two test-target CMake registrations with no fetch/install) | Re-verify per phase that adds dependencies |
| T-19-0302 | A stalled delta whose block was pruned gives up without fetching | Deliberately local-only design (`RetryStalledDeltas` uses `GetNodeWithoutRequest` only); fetching pruned blocks is the documented anti-pattern | Pinned by `StalledDeltaPrunedBlockGivesUpLocallyWithZeroFetchAttempts` — any future fetch addition fails CI |
| T-19-0303 | A pruned-tx request is answered with an empty response (no payload) | Designed graceful degradation of the messenger surface; the requester fails over through the mesh (SURV-02 peer half, T-19-0701) | Pinned by `PrunedTxRequestIsAnsweredWithEmptySignedBlockResponse` |

### Documented residuals (review Info findings — designed deferrals, not open threats)

| ID | Residual | Classification |
|----|----------|----------------|
| IN-01 | Resolved-root guard clears the retry ladder entry but leaves the `pending_jobs_` FAILED entry (`crdt_datastore.cpp:1859` clears only `ClearFailedRootRetryLocked`) — matches give-up semantics; future accounting may see phantom failed jobs | Informational deferral (fix scope was critical/warning) |
| IN-02 | Own-replay guard's `IsOutPointConsumed`/`GetOutPointSpender` sequence is per-call locked (TOCTOU window) and its soundness rests on `CheckTransactionAuthorization`/certificate gating binding `GetHash()` to content on every CONFIRMED path | Informational deferral — review confirmed the signature gating holds today; TickOnce's serialized strand makes the window unlikely |
| IN-03 | `TransactionLookupResult::verdict` has no default initializer; both current construction sites set it explicitly (`TransactionManager.cpp:4045`, `:4079`) | Informational deferral — hazard is a future construction site, not current code |
| IN-04 | Checkpoint-mismatch rebuild cannot restore unspent outputs whose creating tx was pruned locally (availability limit; pre-existing under the old wipe, checkpoint stores only the Merkle root) | Designed deferral — recorded as the known rebuild availability limit |
| IN-05 | `CrosscheckRebuiltUtxos` reverts with `UTXO_NORMAL`, clobbering `UTXO_BRIDGE` typing on bridge-typed outpoints (outpoint still ends CONSUMED; typing/amount metadata degraded) | Designed deferral — degradation noted in the belt's contract; outpoint is never left spendable |
| IN-06 | Blind 8-second sleep in the stalled-delta pruned-block test (`globaldb_erase_test.cpp:888`) — under extreme CI load the zero-fetch assertion could pass vacuously | Informational deferral — bounded-drive coverage added (:886-893); a pass-counter seam is the noted future improvement |

## Unregistered Flags

None. No SUMMARY in this phase carries a `## Threat Flags` section; each
summary's "Threat Mitigations (per plan threat_model)" table maps one-to-one
onto registered threat IDs from the plan-time register. The one notable
implementation deviation — 19-04's crosscheck consulting the DURABLE record
through the new owner-vended `UTXOManager::ReadDurableSpendEvidence` instead
of the planned map-based `GetOutPointSpender` consult — is documented in
19-04-SUMMARY with its rationale (a READY registry entry structurally cannot
carry a spender, making the planned consult vacuous) and maps onto the
existing T-19-0401/T-19-0404 boundary; it adds no new attack surface. The
19-06 READY-strategy change (responder instead of pre-seed) is test-harness
only.

## Review-Cycle Verification (19-REVIEW.md)

The review's three warnings are load-bearing for registered threats and were
re-verified in source, not taken from the report's "FIXED" lines (fix commit
`c6f4b17d4`):

| Finding | Threat impact | Verified in code |
|---------|---------------|------------------|
| WR-01 prefix-scan sibling-key decode in `ReadDurableSpendEvidence` | Hardens T-19-0401/0404 (the belt's evidence read) | `src/account/UTXOManager.cpp:941-972` — exact-key equality on the first hit (`first.first.toString() != expected_key` → `std::nullopt`, :967-972) with the sibling-key hazard documented in the comment (:941-945). Note: no dedicated ":1 vs :10" sibling regression test accompanies the fix (the review prescribed the code fix only); the belt-induction test exercises the exact-key-present path |
| WR-02 bounded exit firing while genuinely behind (stale-nonce publish) | Hardens T-19-0201/0203 | `TransactionManager.cpp:3730-3754` — `proposed_behind` guard (:3736-3739); exit fires only when NOT behind (:3740), stays in SYNCING with a distinct WARN when behind (:3752-3753), and re-arms the window (:3754); log wording corrected to match behavior (state change only, :3743-3747) |
| WR-03 unbounded pre-broadcast rollback re-failure loop | Hardens T-19-0203 | `TransactionManager.cpp:599-619` — `kMaxRollbackFailures = 3`, consecutive-failure counter (`pre_broadcast_rollback_failures_`, hpp:842), loud ERROR per attempt naming the count, force-pop of the batch at the cap (:614-618) so later queue entries are not starved; counter reset on success (:621) |

## Audit Trail

- Auditor: gsd-security-auditor (forced stance: every mitigation assumed absent until a source match proved otherwise)
- Branch/HEAD audited: `gsd/phase-20-reclamation-proof-e2e-hardening` @ `2cfa9f226` (clean tree); phase-19 span `fda039d46^..1bc84068b` (27 commits) plus review-fix `c6f4b17d4` + docs `36ce2d258` confirmed ancestors of HEAD
- Method: plan threat_model blocks (7 plans, 35 threats) extracted; each `mitigate` disposition verified by locating the declared mechanism in the cited implementation file (26/26); each test-backed mitigation verified by locating the named suite/case; the 9 `accept` dispositions verified (7 supply-chain accepts against the span diff; 2 verify-only accepts against pinned tests + untouched production files); 0 `transfer` dispositions
- The audit-guard fail-open concern named at the planning gate is covered by T-19-0301's error-branch verification (logs + continues, never enqueues on unknown state)
- Implementation files were read-only throughout this audit; the only file created is this SECURITY.md
- Result: 35/35 closed (26 mitigated with source evidence, 9 accepted and logged), 0 open — phase may ship
