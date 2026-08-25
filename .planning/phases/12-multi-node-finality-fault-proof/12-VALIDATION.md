---
phase: 12
slug: multi-node-finality-fault-proof
status: draft
nyquist_compliant: false
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
| 12-01-01 | 01 | 1 | TEST-06 | T-12-01 | Four-peer harness starts real PubSub, CRDT, RocksDB, consensus, and Mint ingress without direct protocol delivery. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ W0 | ⬜ pending |
| 12-01-02 | 01 | 1 | TEST-01, TEST-02, TEST-03 | T-12-02 | Contention has one canonical certificate/winner; late contenders cannot add a usable vote; passive recipient makes zero authority writes. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ W0 | ⬜ pending |
| 12-02-01 | 02 | 2 | TEST-04 | T-12-03 | Restart at vote, certificate, and Mint durable boundaries preserves the exact vote and prevents duplicate UTXO/marker effects. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ W0 | ⬜ pending |
| 12-02-02 | 02 | 2 | TEST-05 | T-12-04 | A selected publisher persists before notification; publisher loss invokes only ordinary deterministic failover and never produces a conflicting slot record. | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `test/src/blockchain/multi_node_finality_fault_test.cpp` — persistent four-peer fixture and five named scenarios.
- [ ] `test/src/blockchain/CMakeLists.txt` — normal `addtest(multi_node_finality_fault_test ...)` registration with a timeout no greater than 300 seconds.
- [ ] Friend-scoped, read-only counters and post-durability barriers at vote, certificate, and Mint boundaries; no transport mock or protocol-data injection.

---

## Manual-Only Verifications

All phase behaviors have automated verification. The implementation must inspect the installed PubSub headers for a supported peer-disconnect operation; if none is available, the automated suite uses real stop/recreate plus `AddPeers` reconnection.

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies.
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify.
- [ ] Wave 0 covers all MISSING references.
- [ ] No watch-mode flags.
- [ ] Feedback latency ≤300 seconds.
- [ ] `nyquist_compliant: true` set in frontmatter.

**Approval:** pending
