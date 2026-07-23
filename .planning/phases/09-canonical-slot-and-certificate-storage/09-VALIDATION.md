---
phase: 09
slug: canonical-slot-and-certificate-storage
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-07-23
---

# Phase 9 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest via CMake `addtest` |
| **Config file** | Root/test CMake files; no new framework |
| **Quick run command** | `build/OSX/Release/test_bin/<focused-target> --gtest_brief=1` |
| **Full suite command** | `ctest --test-dir build/OSX/Release -R '(consensus_slot_key|consensus_certificate_store|bridge_relayer|bridge_event_identity|public_chain_mint_validation|certificate_compatibility)' --output-on-failure` |
| **Estimated runtime** | Focused unit targets should remain under 30 seconds; CRDT integration targets may be slower |

## Sampling Rate

- **After every task commit:** Build and run the focused target named by the task.
- **After every plan wave:** Run the phase-filtered CTest command.
- **Before `$gsd-verify-work`:** The phase-filtered suite and existing nonce/UTXO compatibility targets must be green.
- **Max feedback latency:** Prefer 30 seconds for unit work; use focused CRDT filters when integration fixtures exceed that.

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 09-01-01 | 01 | 1 | SLOT-01, SLOT-02 | T-09-01 | Noncanonical slot input fails closed | unit | `cmake --build build/OSX/Release --target consensus_slot_key_test -j2 && build/OSX/Release/test_bin/consensus_slot_key_test --gtest_brief=1` | ✅ | ⬜ pending |
| 09-01-02 | 01 | 1 | SLOT-03, SLOT-04 | T-09-01 | Same burn tuple always shares one digest | unit | `cmake --build build/OSX/Release --target consensus_slot_key_test -j2 && build/OSX/Release/test_bin/consensus_slot_key_test --gtest_brief=1` | ✅ | ⬜ pending |
| 09-02-01 | 02 | 2 | SLOT-03, SLOT-04 | T-09-02 | Multiple receipt logs retain distinct immutable ordinals | unit | `cmake --build build/OSX/Release --target bridge_event_identity_test -j2 && build/OSX/Release/test_bin/bridge_event_identity_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 09-02-02 | 02 | 2 | SLOT-03 | T-09-02 | No mint path defaults event index to zero | unit/integration | `cmake --build build/OSX/Release --target bridge_event_identity_test -j2 && build/OSX/Release/test_bin/bridge_event_identity_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 09-02-03 | 02 | 2 | SLOT-04 | T-09-03 | Validators verify the exact indexed event facts | integration | `cmake --build build/OSX/Release --target public_chain_mint_validation_test -j2 && build/OSX/Release/test_bin/public_chain_mint_validation_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 09-03-01 | 03 | 2 | CERT-02, CERT-03 | T-09-04 | Replicated certificate/index pair is validated atomically | unit | `cmake --build build/OSX/Release --target crdt_atomic_transaction_test consensus_certificate_store_test -j2 && build/OSX/Release/test_bin/crdt_atomic_transaction_test --gtest_brief=1 && build/OSX/Release/test_bin/consensus_certificate_store_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 09-03-02 | 03 | 2 | CERT-01, CERT-02, CERT-03 | T-09-04, T-09-05 | Exact replay is idempotent; differing occupied-slot write conflicts | integration | `cmake --build build/OSX/Release --target consensus_certificate_store_test -j2 && build/OSX/Release/test_bin/consensus_certificate_store_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 09-03-03 | 03 | 2 | COMP-02 | T-09-06 | Legacy state blocks startup before side effects | integration | `cmake --build build/OSX/Release --target consensus_certificate_store_test -j2 && build/OSX/Release/test_bin/consensus_certificate_store_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 09-04-01 | 04 | 3 | CERT-04 | T-09-05 | Slot and hash lookups verify every link | integration | `cmake --build build/OSX/Release --target certificate_compatibility_test -j2 && build/OSX/Release/test_bin/certificate_compatibility_test --gtest_brief=1` | ❌ W0 | ⬜ pending |
| 09-04-02 | 04 | 3 | SLOT-01, COMP-01 | T-09-05 | Full-subject lookup sees winner; hash consumers remain winner-only | integration | `cmake --build build/OSX/Release --target certificate_compatibility_test transaction_manager_pending_lifecycle_test -j2 && build/OSX/Release/test_bin/certificate_compatibility_test --gtest_brief=1 && build/OSX/Release/test_bin/transaction_manager_pending_lifecycle_test --gtest_brief=1` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_certificate_store_test.cpp` and CMake target — CRDT-backed certificate pair, conflict, and legacy startup fixtures.
- [ ] `test/src/account/bridge_event_identity_test.cpp` and CMake target — receipt ordinal and mandatory mint API propagation.
- [ ] `test/src/account/public_chain_mint_validation_test.cpp` and CMake target — deterministic multi-log receipt validation.
- [ ] `test/src/blockchain/certificate_compatibility_test.cpp` and CMake target — slot lookup and transaction-index compatibility.

Wave 0 scaffolds are created at the start of their owning plans before implementation tasks depend on them.

## Manual-Only Verifications

All Phase 9 behaviors have automated verification. The 11-node race and restart matrix remain Phase 12 acceptance work.

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency target documented
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planning-approved 2026-07-23; execution results pending
