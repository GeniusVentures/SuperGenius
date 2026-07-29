---
phase: 11
slug: slot-owned-bridge-burn-reservations
status: planned
nyquist_compliant: true
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
| **Full suite command** | See **Complete Gate Commands** below; the exact-count guard is mandatory. |
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
| 11-W0-01 | 11-01 Task 1 | 0 | BURN-01..05 | T-11-01-01..03 | Deterministic private-store, clock, restart, fault, and concurrency seams | harness | `cmake --build build/OSX/Release --target consensus_burn_reservation_test -j2` | ❌ W0 | ⬜ pending |
| 11-STORE | 11-02 Task 1 | 1 | BURN-01, BURN-02, BURN-03, BURN-04, BURN-05 | T-11-02-01..04 | Reciprocal slot/outpoint records are strict, atomic, idempotent, finality-monotonic, and ABA-safe | unit | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Store*:*Generation*:*Release*'` | ❌ W0 | ⬜ pending |
| 11-RESTART | 11-03 Task 1 | 2 | BURN-01, BURN-04, BURN-05 | T-11-03-01..04 | Restore/reconciliation finishes before observable consensus activity and uses canonical mint slots | integration | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Restart*:*Reconcile*:*Startup*:*Horizon*'` | ❌ W0 | ⬜ pending |
| 11-ADMIT | 11-04 Task 1 | 3 | BURN-01, BURN-02, BURN-03, BURN-05 | T-11-04-01..04 | Validation is pure and durable reservation precedes candidate visibility or voting | integration | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Admission*:*Contender*:*Cleanup*'` | ❌ W0 | ⬜ pending |
| 11-FINAL | 11-05 Task 1 | 4 | BURN-03, BURN-04 | T-11-05-01..04 | Final-pending persists before handler and exact-winner failure states remain protected | integration | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Final*:*SafetyError*:*CertificateOnly*'` | ❌ W0 | ⬜ pending |
| 11-CONTRACT | 11-06 Task 1 | 5 | BURN-03, BURN-04 | T-11-06-01..04 | One live shared store, immutable exact-identity handle, same datastore object, one serialization gate, and fixed lock order | integration | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*SharedStore*:*ApplicationHandle*:*DatastoreIdentity*:*SerializationGate*'` | ❌ W0 | ⬜ pending |
| 11-ATOMIC | 11-07 Task 1 | 6 | BURN-03, BURN-04 | T-11-07-01..04 | Exact mint effects and reservation consumption commit atomically while competing writers remain behind the shared gate | integration/concurrency | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Application*:*Atomic*:*Consumed*:*CompetingWriter*'` | ❌ W0 | ⬜ pending |
| 11-RACE | 11-08 Task 1 | 7 | BURN-02, BURN-03, BURN-05 | T-11-08-01..04 | Stale release/cleanup cannot defeat new admission, finality, a recreated generation, or shutdown ownership | concurrency | `build/OSX/Release/test_bin/consensus_burn_reservation_test --gtest_filter='*Race*:*Stale*:*ABA*:*Horizon*:*Abandon*:*Shutdown*'` | ❌ W0 | ⬜ pending |
| 11-GATE | 11-09 Task 1 | 8 | BURN-01..05 | T-11-09-01..04 | Requirement/decision/threat evidence and Phase 10 compatibility remain green under exact-count discovery | regression/audit | `ctest -N` exact-count guards (9 Phase 11, 8 Phase 10), then both anchored `ctest` gates; see Plan 11-09 Task 1 | mixed | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_burn_reservation_test.cpp` — focused real-RocksDB reservation harness with explicit clock, persistence faults, restart reconstruction, and predicate barriers for BURN-01 through BURN-05.
- [ ] `test/src/blockchain/CMakeLists.txt` — active `consensus_burn_reservation_test` target registered through existing `addtest(...)` conventions.
- [ ] Friend-only production seams required to pause atomic release/admission/finality stages and inject direct-store failures without changing public APIs.
- [ ] Exact nonzero GoogleTest list guards for every filtered command used by plans.

## Complete Gate Commands

- **Phase 11 (exactly 9):** `test "$(ctest --test-dir build/OSX/Release -N -R '^(consensus_burn_reservation_test|consensus_vote_journal_test|consensus_finalization_test|consensus_pending_lifecycle_test|transaction_manager_pending_lifecycle_test|utxo_manager_test|consensus_certificate_store_test|certificate_compatibility_test|network_config_precedence_test)$' | sed -n 's/^Total Tests: //p')" -eq 9 && ctest --test-dir build/OSX/Release -R '^(consensus_burn_reservation_test|consensus_vote_journal_test|consensus_finalization_test|consensus_pending_lifecycle_test|transaction_manager_pending_lifecycle_test|utxo_manager_test|consensus_certificate_store_test|certificate_compatibility_test|network_config_precedence_test)$' --no-tests=error --output-on-failure`
- **Phase 10 compatibility (exactly 8):** `test "$(ctest --test-dir build/OSX/Release -N -R '^(consensus_vote_journal_test|consensus_finalization_test|consensus_pending_lifecycle_test|consensus_certificate_store_test|certificate_compatibility_test|network_config_precedence_test|transaction_manager_pending_lifecycle_test|utxo_manager_test)$' | sed -n 's/^Total Tests: //p')" -eq 8 && ctest --test-dir build/OSX/Release -R '^(consensus_vote_journal_test|consensus_finalization_test|consensus_pending_lifecycle_test|consensus_certificate_store_test|certificate_compatibility_test|network_config_precedence_test|transaction_manager_pending_lifecycle_test|utxo_manager_test)$' --no-tests=error --output-on-failure`

---

## Manual-Only Verifications

All Phase 11 behaviors have automated deterministic verification. The complete 11-node single-burn race is intentionally deferred to Phase 12 and is not a manual Phase 11 gate.

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verification or Wave 0 dependencies.
- [x] Sampling continuity: no three consecutive tasks lack automated verification.
- [x] Wave 0 provides every referenced missing test target and deterministic seam.
- [x] Filtered test commands first prove a nonzero test list.
- [x] No watch-mode flags, detached test threads, or wall-clock sleeps are permitted by plans.
- [x] Feedback latency remains below 180 seconds by focused sampling design.
- [x] Plan 11-06 establishes the one-shared-store/application-handle contract before Plan 11-07 adds UTXO batch participation.
- [x] Plan 11-08 owns abandonment and lifecycle races without the broad regression audit.
- [x] Phase 10's exact eight-target compatibility gate is isolated in audit-only Plan 11-09.
- [x] `nyquist_compliant: true` is set after plan IDs and waves replace the TBD entries.

**Approval:** ready for plan-checker verification
