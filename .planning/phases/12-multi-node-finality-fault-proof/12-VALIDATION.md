---
phase: 12
slug: multi-node-finality-fault-proof
status: planned
nyquist_compliant: true
wave_0_complete: false
created: 2026-08-25
---

# Phase 12 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest + CTest through the project `addtest(...)` helper |
| **Config file** | `test/src/blockchain/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | ≤300 seconds for the new multi-node suite |

---

## Sampling Rate

- **After every task commit:** Build the affected target, then run `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` once the target exists.
- **After every plan wave:** Run the new target together with `consensus_pending_lifecycle_test` and `transaction_manager_certificate_fallback_test`.
- **Before `$gsd-verify-work`:** Full CTest suite must be green.
- **Max feedback latency:** 300 seconds.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 12-01-01 | 01 | 1 | TEST-06 | T-12-01, T-12-02, T-12-03 | Compatibility gate proves a supported real-transport loss/reconnect route and production Mint-consumer composition before harness construction; then friend-only post-durability counters/barriers compile without direct authority, delivery, write, or Mint-completion APIs. | compile/regression | `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test transaction_manager_certificate_fallback_test --parallel 4 && ctest --test-dir build/OSX/Release -R '^(consensus_pending_lifecycle_test|transaction_manager_certificate_fallback_test)$' --output-on-failure` | ✅ existing | ⬜ pending |
| 12-01-02 | 01 | 1 | TEST-06 | T-12-04 | Four-peer persistent real-route audit reaches PubSub, CRDT, RocksDB, consensus, and registered Mint ingress; static gate rejects shortcut calls. | integration/audit | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ created by task | ⬜ pending |
| 12-02-01 | 02 | 2 | TEST-01 | T-12-05 | Same-burn contention produces one canonical slot, immutable certificate, exact winner, and durable per-node Mint result. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ created by 12-01-02 | ⬜ pending |
| 12-02-02 | 02 | 2 | TEST-02, TEST-03 | T-12-06, T-12-07 | Late contender cannot create a usable second vote/certificate; passive recipient stays receive-only and recovers durably. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ created by 12-01-02 | ⬜ pending |
| 12-03-01 | 03 | 3 | TEST-04 | T-12-08 | Vote, accepted-certificate, and Mint/marker boundary restart subcases preserve the original vote and exact-once durable effects. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ created by 12-01-02 | ⬜ pending |
| 12-03-02 | 03 | 3 | TEST-05 | T-12-09, T-12-10, T-12-11 | Publisher loss proves persisted-before-notified bytes, protocol-derived later-round successor counter crossing, and durable D-10 recovery after the stopped publisher is restarted/reconnected. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ created by 12-01-02 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] Planned: `test/src/blockchain/multi_node_finality_fault_test.cpp` — persistent four-peer fixture and five named scenarios; created by 12-01-02.
- [x] Planned: `test/src/blockchain/CMakeLists.txt` — normal `addtest(multi_node_finality_fault_test ...)` registration with a timeout no greater than 300 seconds; created by 12-01-02.
- [x] Planned: friend-scoped, read-only counters and post-durability barriers at vote, certificate, and Mint boundaries; no transport mock or protocol-data injection; created by 12-01-01.

---

## Manual-Only Verifications

All phase behaviors have automated verification. The implementation must inspect the installed PubSub headers for a supported peer-disconnect operation; if none is available, the automated suite uses real stop/recreate plus `AddPeers` reconnection.

---

## Validation Sign-Off

- [x] All six planned tasks have `<automated>` verification, including the explicit pre-existing-target compatibility proof.
- [x] Sampling continuity: no 3 consecutive tasks without automated verify.
- [x] Wave 0 coverage is planned; execution remains pending (`wave_0_complete: false`).
- [x] No watch-mode flags.
- [x] Feedback latency ≤300 seconds.
- [x] `nyquist_compliant: true` set in frontmatter.

**Approval:** pending
