---
phase: 11
slug: convergent-certificate-consumption-mint-recovery
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-08-24
---

# Phase 11 — Validation Strategy

> Per-phase validation contract for convergent certificate consumption and Mint recovery.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest / CTest (C++17) |
| **Config file** | `CMakeLists.txt` and target CMake files |
| **Quick run command** | `ctest --test-dir build/OSX/Release -R '^(consensus_pending_lifecycle_test|transaction_manager_certificate_fallback_test)$' --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | ~30 seconds focused; full suite varies by build host |

---

## Sampling Rate

- **After every task commit:** Build affected targets, then run the focused CTest command.
- **After every plan wave:** Run the focused CTest command; run the full suite when the build host permits.
- **Before `$gsd-verify-work`:** Focused suite and all relevant new regression cases must be green.
- **Max feedback latency:** 60 seconds after an incremental build is available.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 11-01-01 | 01 | 1 | CERT-05 | T-11-01 | A durable accepted certificate with no handler remains stalled; handler registration replays it once through durable readback. | integration | `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test --parallel 4 && ctest --test-dir build/OSX/Release -R '^consensus_pending_lifecycle_test$' --output-on-failure` | ✅ | ⬜ pending |
| 11-02-01 | 02 | 2 | CERT-05, MINT-01 | T-11-02 | Certificate-first consumption chooses only the exact CRDT transaction, then the exact embedded transaction only on CRDT absence; same-slot losers cannot confirm. | integration | `cmake --build build/OSX/Release --target transaction_manager_certificate_fallback_test --parallel 4 && ctest --test-dir build/OSX/Release -R '^transaction_manager_certificate_fallback_test$' --output-on-failure` | ✅ | ⬜ pending |
| 11-03-01 | 03 | 3 | MINT-01, MINT-02 | T-11-03 | Named `SetBridgeExecutedMarkerWriteFailureForTest` fails only the existing marker after winning UTXOs persist; the actual ConsensusManager callback → durable readback → registered TransactionManager handler keeps journal work stalled/nonterminal, then recovery marks marker and completes exactly once. | integration | `cmake --build build/OSX/Release --target transaction_manager_certificate_fallback_test consensus_pending_lifecycle_test --parallel 4 && ctest --test-dir build/OSX/Release -R '^(transaction_manager_certificate_fallback_test|consensus_pending_lifecycle_test)$' --output-on-failure` | ✅ | ⬜ pending |

---

## Threat Model Coverage

| Ref | Threat | Mitigation evidence |
|-----|--------|---------------------|
| T-11-01 | A valid certificate arrives before the transaction handler exists and its durable work is silently completed. | Preserve the existing certificate-work journal as stalled until handler registration triggers durable recovery. |
| T-11-02 | A same-slot competing Mint is substituted during certificate-first recovery. | Require CRDT transaction hash equality and Phase 10 exact certificate-to-transaction binding before confirmation; fallback is only the verified embedded winner. |
| T-11-03 | Crash or local storage error records bridge completion before Mint effects, suppressing retry. | Persist idempotent UTXOs first, propagate failures, then persist `/bridge/executed`; only then set terminal confirmed state. |

---

## Wave 0 Requirements

Existing infrastructure covers all phase requirements. The phase extends these existing focused targets and fixtures:

- `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
- `test/src/account/transaction_manager_certificate_fallback_test.cpp`

No new framework, test runner, or shared fixture bootstrap is required.

---

## Manual-Only Verifications

All phase behaviors have automated verification. A multi-process crash simulation is represented by deterministic replay after durable UTXO effects with the existing marker absent or failing.

---

## Validation Sign-Off

- [x] All tasks have automated verification.
- [x] Sampling continuity: each task has focused automated verification.
- [x] Wave 0 is unnecessary; existing targets cover all requirements.
- [x] No watch-mode flags.
- [x] Focused feedback latency target is under 60 seconds after incremental build.
- [x] `nyquist_compliant: true` set in frontmatter.

**Approval:** pending
