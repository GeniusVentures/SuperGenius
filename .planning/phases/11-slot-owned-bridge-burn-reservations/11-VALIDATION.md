---
phase: 11
slug: slot-owned-bridge-burn-reservations
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-07-28
---

# Phase 11 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest through repository `addtest(...)` |
| **Config file** | `test/src/blockchain/CMakeLists.txt`, `test/src/account/CMakeLists.txt` |
| **Quick run command** | `cmake --build build/OSX/Release --target consensus_burn_reservation_test -j2 && build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_brief=1` |
| **Full suite command** | `ctest --test-dir build/OSX/Release -R '(consensus_burn_reservation|consensus_vote_journal|consensus_finalization|consensus_pending_lifecycle|transaction_manager_pending_lifecycle|utxo_manager|consensus_certificate_store|certificate_compatibility|network_config_precedence)' --output-on-failure` |
| **Estimated runtime** | ~150 seconds |

---

## Sampling Rate

- **After every task commit:** Run the smallest affected focused GoogleTest target; reservation-store tasks use `consensus_burn_reservation_test`.
- **After every plan wave:** Run every target modified in that wave plus its direct Phase 10 regression targets.
- **Before `$gsd-verify-work`:** The full suite command above must be green.
- **Max feedback latency:** 180 seconds for the full focused gate; individual task samples should remain under 60 seconds.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 11-W0-01 | TBD | 0 | BURN-01..05 | T-11-01 / T-11-02 | Deterministic private-store, clock, restart, fault, and concurrency seams | harness | `cmake --build build/OSX/Release --target consensus_burn_reservation_test -j2` | ❌ W0 | ⬜ pending |
| 11-STORE | TBD | TBD | BURN-01, BURN-02, BURN-05 | T-11-01 | Reciprocal slot/outpoint records are strict, atomic, idempotent, and ABA-safe | unit | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Store*:*Generation*:*Release*'` | ❌ W0 | ⬜ pending |
| 11-ADMIT | TBD | TBD | BURN-01, BURN-02, BURN-03 | T-11-01 | Validation is pure and durable reservation precedes candidate visibility or voting | integration | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Admission*:*Contender*:*Cleanup*'` | ❌ W0 | ⬜ pending |
| 11-RESTART | TBD | TBD | BURN-01, BURN-05 | T-11-01 / T-11-03 | Restore/reconciliation finishes before observable consensus activity and uses canonical mint slots | integration | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Restart*:*Reconcile*:*Horizon*'` | ❌ W0 | ⬜ pending |
| 11-FINAL | TBD | TBD | BURN-03, BURN-04 | T-11-02 | Final-pending persists before handler; exact mint effects and reservation consumption commit atomically | integration | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Final*:*Application*:*SafetyError*'` | ❌ W0 | ⬜ pending |
| 11-RACE | TBD | TBD | BURN-02, BURN-03, BURN-05 | T-11-01 | Stale release/cleanup cannot defeat new admission, finality, or a recreated generation | concurrency | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Race*:*Stale*:*ABA*'` | ❌ W0 | ⬜ pending |
| 11-GATE | final | final | BURN-01..05 | all | Phase 11 behavior and Phase 10 compatibility remain green | regression | `ctest --test-dir build/OSX/Release -R '(consensus_burn_reservation|consensus_vote_journal|consensus_finalization|consensus_pending_lifecycle|transaction_manager_pending_lifecycle|utxo_manager|consensus_certificate_store|certificate_compatibility|network_config_precedence)' --output-on-failure` | mixed | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_burn_reservation_test.cpp` — focused real-RocksDB reservation harness with explicit clock, persistence faults, restart reconstruction, and predicate barriers for BURN-01 through BURN-05.
- [ ] `test/src/blockchain/CMakeLists.txt` — active `consensus_burn_reservation_test` target registered through existing `addtest(...)` conventions.
- [ ] Friend-only production seams required to pause atomic release/admission/finality stages and inject direct-store failures without changing public APIs.
- [ ] Exact nonzero GoogleTest list guards for every filtered command used by plans.

---

## Manual-Only Verifications

All Phase 11 behaviors have automated deterministic verification. The complete 11-node single-burn race is intentionally deferred to Phase 12 and is not a manual Phase 11 gate.

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verification or Wave 0 dependencies.
- [ ] Sampling continuity: no three consecutive tasks lack automated verification.
- [ ] Wave 0 provides every referenced missing test target and deterministic seam.
- [ ] Filtered test commands first prove a nonzero test list.
- [ ] No watch-mode flags, detached test threads, or wall-clock sleeps.
- [ ] Feedback latency remains below 180 seconds.
- [ ] Phase 10's exact eight-target compatibility gate remains green.
- [ ] `nyquist_compliant: true` is set after plan IDs and waves replace the TBD entries.

**Approval:** pending plan-checker verification
