---
phase: 19-pruned-node-survival-recovery-hardening
verified: 2026-10-24T00:55:00Z
status: passed
score: 5/5 must-haves verified
overrides_applied: 0
human_verification:
  - test: "Staging spot-check on the live demo node (node_example): restart a node with pruned history, run `tx <hash>` for a pruned hash, then spend/receive"
    expected: "`tx <pruned-hash>` prints the distinct 'history pruned' answer (not 'not found'); the node reaches READY; a spend and a receive complete normally after restart"
    why_human: "The CLI command registration/help/interactive boot path on the live example binary is a user flow; LookupTransaction itself is fully covered by the automated TriState suite, but the end-to-end interactive CLI is not a ctest target"
---

# Phase 19: Pruned-Node Survival & Recovery Hardening Verification Report

**Phase Goal:** A pruned light node is fully operational — every recovery path written under the "history never disappears" assumption is audited and hardened, and history queries answer "pruned" distinctly and cleanly to both local callers and peers.
**Verified:** 2026-10-24T00:55:00Z
**Status:** human_needed (all 5 roadmap truths VERIFIED; 1 human UAT item)
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (Roadmap Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | SC-1: Restart matrix green — prune → restart → READY with zero pruned-CID fetches → new-tip sync → spend/receive → next outgoing tx nonce/previous_hash chain correct | ✓ VERIFIED | Real `tm_->StartCore()` boot (test/src/account/tx_pruner_test.cpp:871, no ForceManagerReady on the leg); counter bracket across the whole restart window (:1175); Verifier executed `tx_pruner_test --gtest_filter='*RestartMatrix*'` → **4/4 PASS** (RealBoot READY in 2.5s; FullMatrix Round0/Round1: tip-sync tracked+UTXO+zero fetch, receive-spend CONSUMED with spender-of-record, nonce 4 on durable chain, previous_hash == ladder == decoded cert-slot `<addr>:3` embedded hash, ≠ both pruned hashes) |
| 2 | SC-2: Boot UTXO assembly + checkpoint-mismatch rebuild treat missing pruned tx as not relevant; boot completes; no spent-output resurrection | ✓ VERIFIED | Mismatch branch preserve-clear swap `ClearUnspentKeepConsumed` (TransactionManager.cpp:3377); PRUNED-marker consult before missing-set insert (:3456-3482, NOT_FOUND/violation/error keep bounded recovery); own-replay discriminator (:2712-2743); belt `CrosscheckRebuiltUtxos` (:3400, :3489) via exact-key `ReadDurableSpendEvidence` (UTXOManager.cpp:929-1000, WR-01 fix). Verifier executed `*RebuildGuard*` → **5/5 PASS** (no-resurrection with raw spent_by_txid disk proof, own-replay no false double-spend reject, tamper-fired belt revert, marker-consult zero-requests skip, unmarked bounded-recovery negative control) + `*KeepConsumed*` → **3/3 PASS** |
| 3 | SC-3: Local history queries return distinct "history pruned"; peers get immediate refusal with prompt failover — tested against a real pruned node | ✓ VERIFIED | `LookupTransaction` tri-state (TransactionManager.cpp:4005-4096): value-probes-then-marker, distinct PRUNED verdict + PrunedTransactionRecord payload, loud VALUE_PRESENT_INVARIANT_VIOLATION; `GeniusNode::LookupTransaction` one-line passthrough (GeniusNode.cpp:4681); CLI `tx <hash>` "history pruned" (NodeExample.cpp:426). Verifier executed tx_lookup_tri_state_test → **5/5 PASS**; `*RefusalFailover*` → **PASS** (pruned server's storage-deterministic miss asserted, per-CID accounting — IsOnBlackList false, failover completes, zero ladder rides, light still serves fresh data). INFO: the exact on-wire RS_NOT_FOUND/CID_NOT_FOUND constant is not directly test-observable (19-07 deviation 4); the refusal behavior is proven through its deterministic storage miss + CID-scoped classification consequences |
| 4 | SC-4: Fresh light bootstraps through a pruned mesh, served by the full node — E2E green | ✓ VERIFIED | `FreshNodeBootstrapsPrunedHistoryServedByFullNode` (globaldb_erase_test.cpp:1144): 4 real pubsub nodes, two lights prune, serving-topic route asserted wired before the fetch window, fresh node converges on A/L1_RETIRE/F_SERVE, ladder exactly at baseline, attempts in [1,4], post-join quiet window exactly 0 fetches, seals intact. Verifier executed → **PASS** |
| 5 | SC-5: Recovery-path audit treats pruned/resolved CIDs as terminal (root jobs, RetryDueFailedRoots, RetryStalledDeltas, work-journal recovery, missing_tx_hashes_); AccountMessenger correct after map deletion | ✓ VERIFIED | `isResolved` guard before both counter sites (crdt_datastore.cpp:1847 → DeleteCIDBlock :1863 / EnqueueRootCID :1864), terminal skip clears ladder entry, error branch re-arms fail-safe. Verifier executed `*RetryDueFailedRoots*:*StalledDeltaPruned*` → **3/3 PASS** (guard zero-delta + unresolved negative control + stalled-delta local-only zero-fetch bounded give-up); PrunedSubject cert-embedded fallback → **PASS**; account_messenger_test (empty signed BlockResponse over real loopback pubsub) → **PASS**. work-journal recovery/missing_tx_hashes_ bounded path is the 18-landed behavior, re-pinned here by the unmarked negative control |

**Score:** 5/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/account/UTXOManager.hpp/.cpp` | Preserve-clear API + impl (+ WR-01 exact-key evidence read) | ✓ VERIFIED | `ClearUnspentKeepConsumed` persists only via `StoreUTXOsLocked` (:535); `ReadDurableSpendEvidence` exact-key guard (:946-972) |
| `test/src/account/utxo_manager_test.cpp` | KeepConsumed suites (memory+disk+negative control) | ✓ VERIFIED | 3 tests, raw /utxo/ prefix disk assertions; executed 3/3 |
| `src/transaction/TransactionManager.hpp/.cpp` | Typed PRE_BROADCAST error, READY-preserving TickOnce branch, bounded gap-guarded SYNCING exit, rebuild guard, tri-state lookup | ✓ VERIFIED | Error enum hpp:125; TickOnce branch :590 BEFORE ChangeState(SYNCING) :629; WR-02 gap guard :3736-3755; WR-03 rollback cap :604-621; LookupTransaction :4005 |
| `test/src/account/transaction_manager_pending_lifecycle_test.cpp` | PreBroadcast + SyncingExit suites | ✓ VERIFIED | 4 tests incl. nonce-collision proof; executed 4/4 |
| `src/crdt/impl/crdt_datastore.cpp` (+ hpp ForTest seams) | RetryDueFailedRoots isResolved guard | ✓ VERIFIED | Guard :1847; guard/control/stalled tests executed 3/3 |
| `test/src/crdt/globaldb_erase_test.cpp` | Guard + StalledDelta + RefusalFailover + PrunedMesh suites | ✓ VERIFIED | 1290 lines; 5 phase-19 tests executed, all PASS |
| `test/src/account/transaction_manager_certificate_fallback_test.cpp` | PrunedSubject cert-fallback case | ✓ VERIFIED | Executed PASS |
| `test/src/account/account_messenger_test.cpp` | Pruned-serving over real pubsub (new target) | ✓ VERIFIED | Executed 1/1 PASS |
| `test/src/account/tx_pruner_test.cpp` | RebuildGuard + RestartMatrix families (real StartCore leg) | ✓ VERIFIED | 3095 lines; executed 9/9 (5 RebuildGuard + 4 RestartMatrix) |
| `src/account/GeniusNode.hpp/.cpp` | LookupTransaction passthrough | ✓ VERIFIED | GeniusNode.cpp:4681, GetTransactionStatus shape |
| `example/node_test/NodeExample.cpp` | `tx <hash>` CLI with distinct "history pruned" | ✓ VERIFIED | cmd_tx :404, verdict-to-text only, "history pruned" :426 |
| `test/src/account/tx_lookup_tri_state_test.cpp` | TriState suite (new target) | ✓ VERIFIED | 5 cases; executed 5/5 |
| `test/src/account/CMakeLists.txt` | New targets registered | ✓ VERIFIED | tx_lookup_tri_state_test + account_messenger_test; both build and run |

### Key Link Verification

All 18 key links across the 7 plans verified (gsd-sdk `verify.key-links` cannot parse the parenthesized `from` annotations — "Source file not found" is a tool path-parsing artifact; every link re-verified manually against source):

| Plan | From → To | Status | Evidence |
|------|-----------|--------|----------|
| 19-01 | ClearUnspentKeepConsumed → StoreUTXOsLocked | ✓ WIRED | UTXOManager.cpp:535 (single persistence path) |
| 19-01 | test → raw /utxo/ prefix spent_by | ✓ WIRED | ReadRawUtxoRecords helper + assertions |
| 19-02 | TickOnce error site → PRE_BROADCAST → RollbackTransactions before SYNCING | ✓ WIRED | :590 branch → :596 rollback; SYNCING at :629 |
| 19-02 | SyncNonce bounded exit → GetLocalConfirmedNonce | ✓ WIRED | :2078 anchor read; WR-02 gap guard added :3736 |
| 19-03 | RetryDueFailedRoots → isResolved before counter sites | ✓ WIRED | :1847 guard → :1863/:1864. NOTE: plan regex window (600 chars) exceeded by the intervening error/log block — semantic ordering verified directly |
| 19-03 | test → GetRootFetchAttemptsForTest zero-delta | ✓ WIRED | Asserted in guard test (:794-805) |
| 19-04 | mismatch branch → ClearUnspentKeepConsumed | ✓ WIRED | :3377 (only call site; zero SetUTXOs in InitializeUTXOs) |
| 19-04 | missing-set insert → LookupPrunedEntry | ✓ WIRED | :3457 consult; insert only on non-PRUNED (:3478-3482) |
| 19-04 | ParseTransferTransaction → GetOutPointSpender own-replay | ✓ WIRED | :2729 discriminator; foreign spender still rejects (:2741) |
| 19-05 | LookupTransaction → PrunedLookup vocabulary | ✓ WIRED | Verbatim reuse; value probes before marker (:4018-4036) |
| 19-05 | cmd_tx → GeniusNode::LookupTransaction | ✓ WIRED | NodeExample.cpp:410 |
| 19-06 | real-boot leg → StartCore | ✓ WIRED | tx_pruner_test.cpp:871; ForceManagerReady only on seam branch |
| 19-06 | nonce-chain assertion → GetOutgoingPreviousHash | ✓ WIRED | OutgoingPreviousHash forwarder; ladder + cert-slot belt asserted |
| 19-07 | PrunedMesh → serving-topic route before fetch window | ✓ WIRED | :1215 head assert + :1243 BroadcastHeadsForTopics before convergence wait |
| 19-07 | RefusalFailover → getNode route walk / RecordCIDFailure | ✓ WIRED | :1031-1044 ordered AddRoute + direct getNode, no blacklist, zero ladder rides |

### Data-Flow Trace (Level 4)

Not applicable in the stub-detection sense: all suites run real CRDT stacks, real RocksDB (raw-prefix disk reads), and real libp2p pubsub hosts — no mocks or static data sources anywhere in the phase's test surface. The tri-state lookup reads the live GlobalDB; the restart matrix reads the reopened on-disk store; the peer suites move real bytes over loopback pubsub. No HOLLOW/DISCONNECTED findings.

### Behavioral Spot-Checks (Verifier-Executed)

Binaries incrementally rebuilt from current sources (post-review-fix) before running:

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| 19-01 KeepConsumed suites | `utxo_manager_test --gtest_filter='*KeepConsumed*'` | 3/3 PASSED (160ms) | ✓ PASS |
| 19-02 PreBroadcast + SyncingExit | `transaction_manager_pending_lifecycle_test --gtest_filter='*PreBroadcast*:*SyncingExit*'` | 4/4 PASSED | ✓ PASS |
| 19-03 SC-5 guards + negative control | `globaldb_integration_test --gtest_filter='*RetryDueFailedRoots*:*StalledDeltaPruned*'` | 3/3 PASSED | ✓ PASS |
| 19-03 AccountMessenger | `account_messenger_test` (full) | 1/1 PASSED (599ms) | ✓ PASS |
| 19-03 PrunedSubject cert fallback | `transaction_manager_certificate_fallback_test --gtest_filter='*PrunedSubject*'` | 1/1 PASSED | ✓ PASS |
| 19-04 RebuildGuard family | `tx_pruner_test --gtest_filter='*RebuildGuard*'` | 5/5 PASSED | ✓ PASS |
| 19-05 TriState suite | `tx_lookup_tri_state_test` (full) | 5/5 PASSED | ✓ PASS |
| 19-06 RestartMatrix (SC-1 headline) | `tx_pruner_test --gtest_filter='*RestartMatrix*'` | 4/4 PASSED (RealBoot 2511ms; Round0 3355ms; Round1 3338ms) | ✓ PASS |
| 19-07 Peer refusal + Pruned mesh | `globaldb_integration_test --gtest_filter='*RefusalFailover*:*PrunedMesh*'` | 2/2 PASSED | ✓ PASS |

**28/28 tests green, executed in the verifier's own process.**

### Probe Execution

No `scripts/*/tests/probe-*.sh` probes exist or are declared for this phase; the behavioral spot-check table above is the executable evidence (C++ gtest targets, same contract: run, exit code, recorded output).

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| GATE-05 | 19-01, 19-03, 19-04 | Boot-time UTXO assembly / checkpoint rebuild treats missing pruned tx as not relevant; no resurrection | ✓ SATISFIED | Preserve-clear + marker consult + own-replay + belt; RebuildGuard 5/5 + KeepConsumed 3/3 executed. Strengthened v1.2 wording ("missing OR unresolvable — pruned, or never existed") covered: unmarked/never-existed refs keep the 18-landed bounded recovery (negative control `RebuildGuardMarkerConsultKeepsBoundedRecoveryForUnmarkedCreatingTx` PASS) |
| SURV-01 | 19-02, 19-03, 19-06 | Restart matrix on a pruned node — READY, zero pruned-CID fetches, new-tip sync, spend/receive, outgoing nonce chain | ✓ SATISFIED | RestartMatrix 4/4 executed (real StartCore, ~2.5s READY, zero fetches, nonce 4 + cert-slot previous_hash) |
| SURV-02 | 19-05 (local), 19-07 (peer) | Local "history pruned" distinct answer; peer refusal with prompt failover | ✓ SATISFIED | TriState 5/5 + RefusalFailover executed |
| SURV-03 | 19-07 | Fresh light bootstraps through pruned mesh, full node serves — E2E | ✓ SATISFIED | PrunedMesh executed on 4 real pubsub nodes |

Orphaned requirements: none — REQUIREMENTS.md maps exactly GATE-05, SURV-01/02/03 to Phase 19 and all four appear in plan frontmatters; live REQUIREMENTS.md marks all four Complete, consistent with the evidence.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none) | - | Zero TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER markers across all 17 phase-modified files | - | - |

Review-cycle findings (19-REVIEW.md: 0 critical / 3 warnings / 6 info):
- WR-01/02/03 — all three fixes verified present in current source (UTXOManager.cpp:946-972 exact-key; TransactionManager.cpp:3736-3755 gap-guarded exit; :604-621 + hpp:842 rollback cap). Commit c6f4b17d4 in history.
- IN-01..IN-06 — informational (pending_jobs_ FAILED residue, own-replay TOCTOU documentation, uninitialized-by-construction verdict with single mandated construction site, pruned-creating-tx rebuild limitation, bridge-type discriminator on belt revert, 8s test sleep). INFO items never trigger gates; documented in 19-REVIEW.md as deferred/informational.

### Human Verification Required

### 1. Staging spot-check: live demo node with pruned history

**Test:** Restart the demo node (node_example) whose history includes pruned transactions; run `tx <known-pruned-hash>` from the CLI; then perform a spend and a receive.
**Expected:** The `tx` command prints the distinct `history pruned` answer (never conflated with `not found`); the node reaches READY; spend/receive complete normally after the restart.
**Why human:** The interactive CLI path (command registration, help listing, live node boot) is a user flow on the example binary — not a ctest target. The underlying LookupTransaction engine and verdict mapping are fully covered by the automated TriState suite, so this is a confirmatory UAT, not a gap closure.

### Gaps Summary

No gaps. All five roadmap success criteria are verified in the codebase and re-proven by verifier-executed test runs (28/28 green) against binaries rebuilt from the current sources. All 18 key links are wired; zero debt markers; the three review warnings are fixed and the fixes hold in source. The single outstanding item is the confirmatory staging UAT above, which gates the status at `human_needed` per the verification contract.

---

_Verified: 2026-10-24T00:55:00Z_
_Verifier: Claude (gsd-verifier)_

## Human Verification Resolution (2026-10-24)

Owner approval after the live staging spot-check: the demo node restarted with pruned history, booted to READY, `tx <pruned-hash>` answered "history pruned" distinctly, and spend/receive completed. Record: 19-HUMAN-UAT.md (resolved, 1/1).
