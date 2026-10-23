---
phase: 19
slug: pruned-node-survival-recovery-hardening
status: ready
nyquist_compliant: true
wave_0_complete: false
created: 2026-10-23
---

# Phase 19 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test + CTest (existing) |
| **Config file** | none — existing targets host new files (one new target: `tx_lookup_tri_state_test`; one new target: `account_messenger_test`) |
| **Quick run command** | per-task filters: `tx_pruner_test --gtest_filter='*RebuildGuard*' / '*RestartMatrix*'`, `globaldb_integration_test` selections, `transaction_manager_pending_lifecycle_test` selections |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | quick ~30-90s · full suite ~19min baseline (18-07 measured 154 tests / 1151.89s) |

---

## Sampling Rate

- After every task commit: the task's quick filter, double-run for survival suites
- After every plan wave: full suite
- Before `/gsd:verify-work`: full suite green
- Max feedback latency: ~90s

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 19-01-T1 | 19-01 | 1 | GATE-05 | T-19-0101/0103 | owner-vended consumed-preserving clear; persists via snapshot only; SetUTXOs untouched | build gate | `cmake --build build/OSX/Release --target utxo_manager_test && grep -c ClearUnspentKeepConsumed src/account/UTXOManager.hpp src/account/UTXOManager.cpp` | ✅ (extend) | ⬜ pending |
| 19-01-T2 | 19-01 | 1 | GATE-05 | T-19-0101/0102 | spent_by_txid survives the clear in memory AND on disk; wipe semantics byte-identical | unit | `build/OSX/Release/test_bin/utxo_manager_test --gtest_filter='*KeepConsumed*'` ×2 + full binary | ❌ W0 | ⬜ pending |
| 19-02-T1 | 19-02 | 1 | SURV-01 | T-19-0202/0203 | typed pre-broadcast error; TickOnce branch precedes ChangeState(SYNCING); TM stays READY; nonce released | build + unit | `cmake --build ... --target transaction_manager_pending_lifecycle_test && ... --gtest_filter='*PreBroadcast*'` | ✅ (extend) | ⬜ pending |
| 19-02-T2 | 19-02 | 1 | SURV-01 | T-19-0201/0204 | bounded SYNCING exit anchored to durable nonce; zero pending_nonces_ mutation in diff | build + unit | `... --gtest_filter='*SyncingExit*'` | ✅ (extend) | ⬜ pending |
| 19-02-T3 | 19-02 | 1 | SURV-01 | T-19-0201/0203 | pre-broadcast ⇒ READY + reusable nonce (collision-proof: next reserved strictly greater); post-Commit control unchanged; todo closed | integration unit | `... --gtest_filter='*PreBroadcast*:*SyncingExit*'` ×2 + full binary + `ctest -R transaction_manager_pending_lifecycle` | ❌ W0 | ⬜ pending |
| 19-03-T1 | 19-03 | 1 | GATE-05, SURV-01 | T-19-0301 | RetryDueFailedRoots isResolved guard BEFORE counter; pruned root ⇒ delta 0; unpruned control advances | integration | `ctest -R globaldb_integration --output-on-failure` | ❌ W0 | ⬜ pending |
| 19-03-T2 | 19-03 | 1 | GATE-05, SURV-01 | T-19-0302/0304 | stalled delta local-only (zero fetches); cert PrunedSubject approves via embedded fallback, no re-fetch | integration | `ctest -R 'globaldb_integration|certificate_fallback'` (`*PrunedSubject*` filter) | ❌ W0 | ⬜ pending |
| 19-03-T3 | 19-03 | 1 | GATE-05 | T-19-0303 | AccountMessenger pruned request ⇒ empty signed BlockResponse, no crash/error propagation | unit (methods-wired) | `cmake --build ... --target account_messenger_test && ... && ctest -R account_messenger_test` | ❌ W0 | ⬜ pending |
| 19-04-T1 | 19-04 | 2 | GATE-05 | T-19-0403/0405 | mismatch branch preserve-clear; PRUNED consult ⇒ skip + INFO; unmarked path unchanged | integration | `build/OSX/Release/test_bin/tx_pruner_test --gtest_filter='*RebuildGuard*MarkerConsult*'` | ❌ W0 | ⬜ pending |
| 19-04-T2 | 19-04 | 2 | GATE-05 | T-19-0401/0402 | own-replay discriminator (spender-of-record == own hash; foreign still rejects); gap-fill crosscheck via accessors | integration | `... --gtest_filter='*RebuildGuard*'` | ✅ (extend) | ⬜ pending |
| 19-04-T3 | 19-04 | 2 | GATE-05 | T-19-0401..0404 | no resurrection (disk spent_by_txid proof); change outputs re-created; zero requests for pruned refs; negative control | integration ×2 | `... --gtest_filter='*RebuildGuard*'` ×2 + full binary | ❌ W0 | ⬜ pending |
| 19-05-T1 | 19-05 | 3 | SURV-02 | T-19-0501/0502/0504 | tri-state lookup; PrunedLookup reused; value-before-marker ordering; loud invariant report | build + unit | `cmake --build ... --target tx_lookup_tri_state_test && ... --gtest_filter='*TriState*'` | ✅ (extend) | ⬜ pending |
| 19-05-T2 | 19-05 | 3 | SURV-02 | T-19-0503 | GeniusNode passthrough + `tx <hash>` CLI ("history pruned" distinct); NO RPC mapping | build gate | `cmake --build ... --target node_test && grep -c "history pruned" example/node_test/NodeExample.cpp` | ✅ (extend) | ⬜ pending |
| 19-05-T3 | 19-05 | 3 | SURV-02 | T-19-0501/0502 | five tri-state cases incl. resurrected invariant-violation loud path | integration ×2 | `build/OSX/Release/test_bin/tx_lookup_tri_state_test` + `ctest -R tx_lookup_tri_state` | ❌ W0 | ⬜ pending |
| 19-06-T1 | 19-06 | 3 | SURV-01 | T-19-0601..0603 | real StartCore boot ⇒ bounded READY; zero pruned-CID fetches across restart; empty missing set | integration ×2 | `... tx_pruner_test --gtest_filter='*RestartMatrix*RealBoot*'` | ❌ W0 | ⬜ pending |
| 19-06-T2 | 19-06 | 3 | SURV-01 | T-19-0601/0604 | full matrix: tip sync, spend/receive, outgoing nonce + previous_hash == GATE-04 ladder | integration ×2 | `... --gtest_filter='*RestartMatrix*'` ×2 + full binary | ❌ W0 | ⬜ pending |
| 19-07-T1 | 19-07 | 2 | SURV-02 | T-19-0701/0703 | pruned-server refusal + requester failover; CID-scoped (not connection) accounting | integration ×2 | `build/OSX/Release/test_bin/globaldb_integration_test --gtest_filter='*RefusalFailover*'` + `ctest -R globaldb_integration` | ❌ W0 | ⬜ pending |
| 19-07-T2 | 19-07 | 2 | SURV-03 | T-19-0702/0704 | fresh light bootstraps pruned history via full node's broadcast-registered route; bounded attempts | integration ×2 | `... --gtest_filter='*PrunedMesh*'` + `ctest -R globaldb_integration` | ❌ W0 | ⬜ pending |

---

## Wave 0 Requirements

All Wave-0 scaffolds are created by the plans themselves (each ❌ W0 row's suite lands with its task):

- [ ] `*KeepConsumed*` unit cases in utxo_manager_test.cpp (19-01-T2)
- [ ] `*PreBroadcast*` / `*SyncingExit*` suites + SetSyncingExitTimeoutForTest forwarder in transaction_manager_pending_lifecycle_test.cpp (19-02-T3)
- [ ] Failed-root guard test + `*StalledDeltaPruned*` in globaldb_erase_test.cpp (19-03-T1/T2); `*PrunedSubject*` in transaction_manager_certificate_fallback_test.cpp (19-03-T2); account_messenger_test target (19-03-T3)
- [ ] `*RebuildGuard*` family incl. MarkerConsult + mismatch induction (post-checkpoint progress) in tx_pruner_test.cpp (19-04-T3)
- [ ] tx_lookup_tri_state_test target + fixture copy (19-05-T3)
- [ ] `*RestartMatrix*` family incl. the REAL StartCore leg with pre-seeded local confirmed nonce (strategy b, measure first; fallback a) (19-06-T1/T2)
- [ ] `*RefusalFailover*` + `*PrunedMesh*` 3(+1)-node suites in globaldb_erase_test.cpp (19-07-T1/T2)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Live pruned-node spot check on staging | SURV-01/02 | Live net | Optional re-run of the phase-18 demo node post-restart: `tx <hash>` query on a pruned hash → "history pruned"; spend/receive still works |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 90s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending (plans committed 2026-10-23)
