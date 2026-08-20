---
phase: 8
slug: canonical-slot-certificate-binding
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-08-20
---

# Phase 8 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest via CTest |
| **Config file** | `test/src/blockchain/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` |
| **Full suite command** | `ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | Existing focused tests should complete within minutes; establish the observed time during Wave 0. |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure`
- **After every plan wave:** Run `ctest --test-dir build --output-on-failure`
- **Before `$gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** One focused CTest invocation per task

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 08-01-01 | 01 | 1 | SLOT-01, SLOT-02 | T-08-01 | Same verified Mint facts produce the same slot; changing every required fact changes it; proposer and nonce do not. | unit | `ctest --test-dir build -R consensus_slot_key_test --output-on-failure` | ✅ extend | ⬜ pending |
| 08-02-01 | 02 | 1 | SLOT-03 | T-08-02 | A certificate/proposal/slot binding mismatch returns before cleanup, finalization notification, handler dispatch, or mint-capable effects. | component | `ctest --test-dir build -R consensus_pending_lifecycle_test --output-on-failure` | ✅ extend | ⬜ pending |
| 08-02-02 | 02 | 1 | SLOT-03 | T-08-03 | Canonical-slot and expected future slot-key derivation are deterministic while current subject-hash persistence remains unchanged until Phase 10. | unit | `ctest --test-dir build -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` | ✅ extend | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_slot_key_test.cpp` — add envelope-independence and required-fact mutation controls for `MintTransactionV2` slots.
- [ ] `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — add test-access observation for rejected binding side effects, or use an already linked certificate fixture with equivalent production ingress coverage.
- [ ] Confirm the focused CTest regex resolves in the configured build directory; adjust only the test target names if necessary.

---

## Manual-Only Verifications

All Phase 8 behaviors have automated source and component-test targets. Protocol-level multi-node publication, failover, restart, and mint-effect verification remain the explicit scope of Phases 9-12.

---

## Validation Sign-Off

- [ ] All tasks have automated verification or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all missing references
- [ ] No watch-mode flags
- [ ] Focused CTest command is confirmed in the build environment
- [ ] `nyquist_compliant: true` set in frontmatter after the plan and focused commands are confirmed

**Approval:** pending
