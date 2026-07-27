---
phase: 10
slug: durable-vote-lock-and-finalization-state-machine
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-07-27
---

# Phase 10 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest through the repository `addtest(...)` helper |
| **Config file** | `test/src/blockchain/CMakeLists.txt` |
| **Quick run command** | `cmake --build build/OSX/Release --target consensus_vote_journal_test -j2 && build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1` |
| **Full suite command** | `ctest --test-dir build/OSX/Release -R '(consensus_vote_journal|consensus_finalization|consensus_pending_lifecycle|consensus_certificate_store|network_config_precedence|transaction_manager_pending_lifecycle|utxo_manager)' --output-on-failure` |
| **Estimated runtime** | focused journal tests under 10 seconds; CRDT-backed finalization tests under 30 seconds |

---

## Sampling Rate

- **After every state-store or vote-journal task commit:** Build and run `consensus_vote_journal_test`.
- **After every candidate-selection task commit:** Run `consensus_pending_lifecycle_test` plus `consensus_vote_journal_test`.
- **After every finalization or conflict task commit:** Run `consensus_finalization_test` plus `consensus_certificate_store_test`.
- **After every plan wave:** Run the full focused CTest regex above.
- **Before `$gsd-verify-work`:** Run the focused suite plus `certificate_compatibility_test`, `transaction_manager_pending_lifecycle_test`, and `utxo_manager_test`.
- **Max feedback latency:** No more than two implementation tasks without an automated focused run; target 30 seconds for focused feedback.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 10-01-01 | 01 | 0 | VOTE-01, VOTE-02, VOTE-03 | T-10-01 | Corrupt or contradictory vote state fails before consensus side effects | RocksDB/factory integration | `cmake --build build/OSX/Release --target consensus_vote_journal_test -j2` | ❌ W0 | ⬜ pending |
| 10-01-02 | 01 | 1 | VOTE-01, VOTE-02, VOTE-03, VOTE-07 | T-10-02 | Exact signed vote and envelope persist before publication and replay byte-identically | unit/integration | `build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 10-02-01 | 02 | 1 | VOTE-04, VOTE-05 | T-10-03 | One fixed deadline freezes one deterministic candidate and cannot trigger a revote | deterministic state/timer | `build/OSX/Release/test_bin/consensus_pending_lifecycle_test --gtest_brief=1` | ✅ extend | ⬜ pending |
| 10-02-02 | 02 | 1 | VOTE-01, VOTE-07 | T-10-04 | Vote retirement uses the same live certificate-acceptance horizon and is durable before reuse | boundary integration | `build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 10-03-01 | 03 | 2 | CERT-05, CERT-06, VOTE-06 | T-10-05 | Every ingress path converges on one finalization operation and cannot apply twice | CRDT/RocksDB integration | `cmake --build build/OSX/Release --target consensus_finalization_test -j2` | ❌ W0 | ⬜ pending |
| 10-03-02 | 03 | 2 | CERT-05, CERT-06 | T-10-06 | Handler failure preserves finality and pending state; restart retries the exact winner | crash-recovery integration | `build/OSX/Release/test_bin/consensus_finalization_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 10-04-01 | 04 | 3 | CERT-07 | T-10-07 | A valid conflict preserves the original winner and creates one digest-pair evidence record | CRDT filter/integration | `build/OSX/Release/test_bin/consensus_finalization_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 10-04-02 | 04 | 3 | CERT-07 | T-10-08 | Restart restores the safety stop; conflicts are never applied or normally rebroadcast | restart/concurrency | `ctest --test-dir build/OSX/Release -R '(consensus_finalization|consensus_certificate_store)' --output-on-failure` | ❌ W0 | ⬜ pending |
| 10-05-01 | 05 | 3 | CERT-05..07, VOTE-01..07 | T-10-09 | Configuration, shutdown, concurrency, and compatibility paths preserve all phase invariants | regression suite | `ctest --test-dir build/OSX/Release -R '(consensus_vote_journal|consensus_finalization|consensus_pending_lifecycle|consensus_certificate_store|network_config_precedence|transaction_manager_pending_lifecycle|utxo_manager)' --output-on-failure` | ✅/extend | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_vote_journal_test.cpp` — deterministic persistence, restart, exact replay, corruption, retirement, and side-effect-order fixtures for VOTE-01 through VOTE-03 and VOTE-07.
- [ ] `test/src/blockchain/consensus_finalization_test.cpp` — multi-ingress, application retry, conflict evidence, and safety-stop fixtures for CERT-05 through CERT-07 and VOTE-06.
- [ ] `test/src/blockchain/CMakeLists.txt` — active `addtest(...)` entries for both new focused targets.
- [ ] Friend-only clock, fault-observer, handler-blocker, and evidence-reader seams following `ConsensusManagerTestAccess`; no production failure-injection API.

---

## Manual-Only Verifications

All Phase 10 behaviors have automated verification. The 11-node single-burn race is intentionally deferred to Phase 12.

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency under 30 seconds for focused runs
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
