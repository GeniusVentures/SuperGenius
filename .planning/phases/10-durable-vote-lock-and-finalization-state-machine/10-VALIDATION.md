---
phase: 10
slug: durable-vote-lock-and-finalization-state-machine
status: ready
nyquist_compliant: true
wave_0_complete: false
created: 2026-07-27
updated: 2026-07-27
---

# Phase 10 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution of Plans 10-01 through 10-07.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest through the repository `addtest(...)` helper |
| **Config file** | `test/src/blockchain/CMakeLists.txt` |
| **Wave 0 build** | `cmake --build build/OSX/Release --target consensus_vote_journal_test consensus_finalization_test -j2` |
| **Quick run command** | `cmake --build build/OSX/Release --target consensus_vote_journal_test -j2 && build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1` |
| **Full suite command** | `ctest --test-dir build/OSX/Release -R '(consensus_vote_journal|consensus_finalization|consensus_pending_lifecycle|consensus_certificate_store|certificate_compatibility|network_config_precedence|transaction_manager_pending_lifecycle|utxo_manager)' --output-on-failure` |
| **Estimated runtime** | Focused journal tests under 10 seconds; CRDT-backed finalization tests under 30 seconds; closure suite sampled at wave completion |

---

## Sampling Rate and Continuity

- **Plan 10-01 / Wave 0:** Build and run both new harness binaries before any behavior plan begins.
- **After Plan 10-02:** Build and run `consensus_vote_journal_test` for the strict local store.
- **After Plan 10-03:** Run `consensus_vote_journal_test` and `network_config_precedence_test` for startup order, raw stored-envelope replay, and configuration propagation.
- **After Plan 10-04:** Run `consensus_vote_journal_test` and `consensus_pending_lifecycle_test` for fixed-window signing, generation-tagged `SigningPublishing`, exact durable publication/failure replay, and retirement.
- **After Plan 10-05:** Run finalization, pending lifecycle, TransactionManager pending lifecycle, and certificate-store targets for condition-waited publication-first versus finalization-first ordering, retry suppression, and application recovery.
- **After Plan 10-06:** Run `consensus_finalization_test` and `consensus_certificate_store_test` for all-ingress conflict handling.
- **After Plan 10-07:** Run the full suite command above, including `certificate_compatibility_test` and UTXO idempotency.
- **Continuity proof:** Every one of the seven plans contains one task with an `<automated>` command. There are zero consecutive implementation tasks without an automated sample, which is stricter than the maximum gap of two.
- **No watch mode:** Every command terminates and returns an exit status.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirements | Threat Refs | Secure behavior sampled | Automated command | File Exists / dependency | Status |
|---------|------|------|--------------|-------------|--------------------------|-------------------|--------------------|--------|
| 10-01-01 | 10-01 | 0 | CERT-05, CERT-06, CERT-07, VOTE-01, VOTE-02, VOTE-03, VOTE-06, VOTE-07 | T-10-01-01, T-10-01-02, T-10-01-03 | Real same-path persistence and barrier/counter harnesses cannot falsely pass on fresh storage or timing sleeps | `cmake --build build/OSX/Release --target consensus_vote_journal_test consensus_finalization_test -j2 && test "$(build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_list_tests | grep -Ec 'PersistentDatabaseReopensCleanly')" -eq 1 && test "$(build/OSX/Release/test_bin/consensus_finalization_test --gtest_list_tests | grep -Ec 'ConcurrentBarrierJoinsCleanly')" -eq 1 && build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1 && build/OSX/Release/test_bin/consensus_finalization_test --gtest_brief=1` | Creates both missing focused targets and reusable fixtures | ⬜ pending |
| 10-02-01 | 10-02 | 1 | VOTE-01, VOTE-02, VOTE-03, VOTE-07 | T-10-02-01, T-10-02-02, T-10-02-03, T-10-02-04 | Strict direct-RocksDB records preserve exact bytes, distinguish absence from corruption, and commit retirement/conflict safety transitions atomically | `cmake --build build/OSX/Release --target consensus_vote_journal_test -j2 && build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1` | Depends on 10-01 journal target | ⬜ pending |
| 10-03-01 | 10-03 | 2 | VOTE-03, VOTE-04 | T-10-03-01, T-10-03-02, T-10-03-03, T-10-03-04 | Startup fails before side effects; restored locks precede byte-identical raw replay with zero signer calls; missing handlers remain pending | `cmake --build build/OSX/Release --target consensus_vote_journal_test network_config_precedence_test -j2 && build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1 && build/OSX/Release/test_bin/network_config_precedence_test --gtest_brief=1` | Transitively depends on 10-01 through 10-02 | ⬜ pending |
| 10-04-01 | 10-04 | 3 | VOTE-01, VOTE-02, VOTE-04, VOTE-05, VOTE-07 | T-10-04-01, T-10-04-02, T-10-04-03, T-10-04-04 | Fixed-window generation freezes one candidate; `SigningPublishing` spans durable put and unlocked raw attempt; failed publish preserves exact replay bytes; retirement uses the live acceptance boundary | `cmake --build build/OSX/Release --target consensus_vote_journal_test consensus_pending_lifecycle_test -j2 && build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1 && build/OSX/Release/test_bin/consensus_pending_lifecycle_test --gtest_brief=1` | Uses 10-01 journal fixture and 10-03 raw publication helper | ⬜ pending |
| 10-05-01 | 10-05 | 4 | CERT-05, CERT-06, VOTE-06 | T-10-05-01, T-10-05-02, T-10-05-03, T-10-05-04 | Finalizer condition-waits for an exact active `SigningPublishing` or `PublishingReplay` generation, then reserves `Finalizing`; barriers prove both legal orders and finality/SafetyViolation suppress later replay; exact winner applies once | `cmake --build build/OSX/Release --target consensus_finalization_test consensus_pending_lifecycle_test transaction_manager_pending_lifecycle_test consensus_certificate_store_test -j2 && build/OSX/Release/test_bin/consensus_finalization_test --gtest_brief=1 && build/OSX/Release/test_bin/consensus_pending_lifecycle_test --gtest_brief=1 && build/OSX/Release/test_bin/transaction_manager_pending_lifecycle_test --gtest_brief=1 && build/OSX/Release/test_bin/consensus_certificate_store_test --gtest_brief=1` | Uses 10-01 finalization fixture and 10-04 shared lifecycle | ⬜ pending |
| 10-06-01 | 10-06 | 5 | CERT-07 | T-10-06-01, T-10-06-02, T-10-06-03, T-10-06-04, T-10-06-05 | Every valid conflict source records one canonical digest pair and durable per-slot safety stop without overwrite, second application, or rebroadcast | `cmake --build build/OSX/Release --target consensus_finalization_test consensus_certificate_store_test -j2 && build/OSX/Release/test_bin/consensus_finalization_test --gtest_brief=1 && build/OSX/Release/test_bin/consensus_certificate_store_test --gtest_brief=1` | Uses 10-01 finalization fixture and 10-05 finalizer | ⬜ pending |
| 10-07-01 | 10-07 | 6 | CERT-05, CERT-06, CERT-07, VOTE-01, VOTE-02, VOTE-03, VOTE-04, VOTE-05, VOTE-06, VOTE-07 | T-10-07-01, T-10-07-02, T-10-07-03, T-10-07-04 | Owned shutdown, lock order, full deterministic race matrix, Phase 9 compatibility, TransactionManager retry semantics, and UTXO idempotency close the phase | `cmake --build build/OSX/Release --target consensus_vote_journal_test consensus_finalization_test consensus_pending_lifecycle_test consensus_certificate_store_test certificate_compatibility_test network_config_precedence_test transaction_manager_pending_lifecycle_test utxo_manager_test -j2 && ctest --test-dir build/OSX/Release -R '(consensus_vote_journal|consensus_finalization|consensus_pending_lifecycle|consensus_certificate_store|certificate_compatibility|network_config_precedence|transaction_manager_pending_lifecycle|utxo_manager)' --output-on-failure` | Transitively depends on all Wave 0 fixtures and Plans 10-02 through 10-06 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Dependency Proof

- [ ] `test/src/blockchain/consensus_vote_journal_test.cpp` is intentionally missing before execution and is created by Task 10-01-01 with same-path RocksDB reopen, explicit clocks, raw-byte/signature counters, and startup-order hooks.
- [ ] `test/src/blockchain/consensus_finalization_test.cpp` is intentionally missing before execution and is created by Task 10-01-01 with multi-ingress counters, handler barriers, persistence-stage observers, cleanup ordering, and conflict/publication hooks.
- [ ] `test/src/blockchain/CMakeLists.txt` receives active targets for both missing files in Task 10-01-01.
- [ ] Plans 10-02 through 10-07 are transitively blocked on 10-01 (`10-02 -> 10-01`, `10-03 -> 10-02`, ..., `10-07 -> 10-06`), so no command references a missing focused binary before Wave 0 creates it.
- [ ] Existing extension targets (`consensus_pending_lifecycle_test`, `consensus_certificate_store_test`, `network_config_precedence_test`, `transaction_manager_pending_lifecycle_test`, `utxo_manager_test`, and `certificate_compatibility_test`) already exist and are only sampled in dependency-ordered later waves.
- [ ] Friend-only clocks, fault observers, handler blockers, and evidence readers follow existing test-access patterns; no public production failure-injection API is required.

---

## Requirement and Threat Coverage

- **VOTE-01/02/03/07:** Store and restart safety are sampled in 10-02/10-03, with exact durable publication and retirement in 10-04.
- **VOTE-04/05:** Configuration and fixed candidate selection are sampled in 10-03/10-04.
- **CERT-05/06 and VOTE-06:** condition-waited `SigningPublishing`/`Finalizing` linearization in both orders, retry suppression, authoritative persistence recovery, exact-winner processing, and local-vote override are sampled in 10-05.
- **CERT-07:** All-ingress evidence and SafetyViolation restoration are sampled in 10-06.
- **Phase-wide closure:** 10-07 samples all ten Phase 10 requirements and its own lifetime/compatibility threat register.
- Every threat reference in the map names an ID present in that plan's `<threat_model>`; no synthetic or legacy threat IDs remain.

---

## Manual-Only Verifications

All Phase 10 behaviors have automated verification. The 11-node single-burn race remains intentionally deferred to Phase 12.

---

## Validation Sign-Off

- [x] All seven actual tasks have terminating `<automated>` verification commands.
- [x] Sampling continuity has zero unsampled consecutive tasks.
- [x] Wave 0 creates every missing focused target before a dependent plan can execute.
- [x] Requirement references and threat IDs match the owning plans.
- [x] Startup raw replay is owned and tested in 10-03 before 10-04 consumes its helper.
- [x] Both publication-first and finalization-first orderings, failed-publication replay suppression, rollback, and persisted-finality recovery are explicitly sampled in 10-05.
- [x] No watch-mode flags are present.
- [x] Focused feedback targets remain under the research target of 30 seconds; the larger closure suite runs only at Wave 6.
- [x] `nyquist_compliant: true` is set in frontmatter.

**Approval:** approved for execution; Wave 0 remains pending until Task 10-01-01 passes.
