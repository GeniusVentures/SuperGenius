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
| **Combined final-wave command** | `ctest --test-dir build/OSX/Release --timeout 300 -R '^(multi_node_finality_fault_test|consensus_pending_lifecycle_test|transaction_manager_certificate_fallback_test)$' --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | ≤300 seconds for the new multi-node suite |

---

## Sampling Rate

- **After 12-01 Task 1:** Build and run the new `multi_node_finality_fault_compatibility_smoke_test` once, together with its two predecessor targets, before harness construction.
- **After every task commit once the suite exists:** Build the affected target, then run the affected CTest target exactly once; do not also issue a wave-level invocation in that same feedback cycle.
- **After the final wave:** Use the one combined final-wave command above. It runs `multi_node_finality_fault_test`, `consensus_pending_lifecycle_test`, and `transaction_manager_certificate_fallback_test` exactly once each; do not precede it with a standalone multi-node CTest command.
- **Before `$gsd-verify-work`:** Full CTest suite must be green.
- **Bounded CTest feedback latency:** the single multi-node target is registered with `TIMEOUT ≤300` seconds. The serial final-wave combined command applies `--timeout 300` to each of its three targets, so its CTest execution is bounded at ≤900 seconds; compilation and the separate full-suite acceptance cycle are not represented by this bound.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 12-01-01 | 01 | 1 | TEST-06 | T-12-01, T-12-02, T-12-03 | Runnable compatibility smoke target proves a supported real-transport loss/reconnect route and exact public production `Blockchain` plus `TransactionManager` certificate-consumer composition before harness construction; then friend-only post-durability counters/barriers compile without direct authority, delivery, write, or Mint-completion APIs. | compile/regression | `cmake --build build/OSX/Release --target multi_node_finality_fault_compatibility_smoke_test consensus_pending_lifecycle_test transaction_manager_certificate_fallback_test --parallel 4 && ctest --test-dir build/OSX/Release -R '^(multi_node_finality_fault_compatibility_smoke_test|consensus_pending_lifecycle_test|transaction_manager_certificate_fallback_test)$' --output-on-failure` | ❌ created by task | ⬜ pending |
| 12-01-02 | 01 | 1 | TEST-06 | T-12-04 | Four-peer persistent real-route audit reaches PubSub, CRDT, RocksDB, consensus, and registered Mint ingress; static gate rejects shortcut calls. | integration/audit | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ created by task | ⬜ pending |
| 12-02-01 | 02 | 2 | TEST-01 | T-12-05 | Same-burn contention produces one canonical slot, immutable certificate, exact winner, and durable per-node Mint result. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ created by 12-01-02 | ⬜ pending |
| 12-02-02 | 02 | 2 | TEST-02, TEST-03 | T-12-06, T-12-07 | Late contender cannot create a usable second vote/certificate; passive recipient stays receive-only and recovers durably. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ created by 12-01-02 | ⬜ pending |
| 12-03-01 | 03 | 3 | TEST-04 | T-12-08 | Vote, accepted-certificate, and Mint/marker boundary restart subcases preserve the original vote and exact-once durable effects. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ created by 12-01-02 | ⬜ pending |
| 12-03-02 | 03 | 3 | TEST-05 | T-12-09, T-12-10, T-12-11 | Publisher loss proves persisted-before-notified bytes, outbound notification counts original=0/expected successor=1-or-more/all other peers=0, and durable D-10 recovery after the stopped publisher is restarted/reconnected. | integration | `ctest --test-dir build/OSX/Release --timeout 300 -R '^(multi_node_finality_fault_test|consensus_pending_lifecycle_test|transaction_manager_certificate_fallback_test)$' --output-on-failure` | ❌ created by 12-01-02 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] Planned: `test/src/blockchain/multi_node_finality_fault_test.cpp` — persistent four-peer fixture and five named scenarios; created by 12-01-02.
- [x] Planned: `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp` — independently runnable real-transport production-consumer composition and stop/restart/reconnect proof; created by 12-01-01.
- [x] Planned: `test/src/blockchain/CMakeLists.txt` — normal `addtest(multi_node_finality_fault_test ...)` registration with a timeout no greater than 300 seconds; created by 12-01-02.
- [x] Planned: friend-scoped, read-only counters and post-durability barriers at vote, certificate, and Mint boundaries; no transport mock or protocol-data injection; created by 12-01-01.

---

## Manual-Only Verifications

All phase behaviors have automated verification. The implementation must inspect the installed PubSub headers for a supported peer-disconnect operation; if none is available, the automated suite uses real stop/recreate plus `AddPeers` reconnection.

---

## Validation Sign-Off

- [x] All six planned tasks have `<automated>` verification, including the independently runnable compatibility smoke proof.
- [x] Sampling continuity: no 3 consecutive tasks without automated verify.
- [x] Wave 0 coverage is planned; execution remains pending (`wave_0_complete: false`).
- [x] No watch-mode flags.
- [x] CTest feedback latency is truthfully bounded: ≤300 seconds for the multi-node target and ≤900 seconds for the one serial three-target final-wave command; no CTest target is repeated within a feedback cycle.
- [x] `nyquist_compliant: true` set in frontmatter.

**Approval:** pending
