---
phase: 10
slug: trustedpeerregistry
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-07-24
---

# Phase 10 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest, driven via CTest (`addtest(...)` CMake macro, same as `securecrdt`/`multisig` test dirs) |
| **Config file** | `test/src/securecrdt/CMakeLists.txt` is the closest precedent; new `test/src/trustedpeer/CMakeLists.txt` follows the same shape |
| **Quick run command** | `ctest -R trustedpeer --output-on-failure` |
| **Full suite command** | `ctest --output-on-failure` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest -R trustedpeer`
- **After every plan wave:** Run `ctest` (full suite)
- **Before `/gsd:verify-work`:** Full suite must be green, plus a manual TPR-03 grep-inspection pass
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 10-01-01 | 01 | 0 | TPR-01 | — | Genesis node seeds initial trusted-peer set from hardcoded genesis config entry, no manual bootstrap | unit/e2e (single-node SecureCrdtTestNode) | `ctest -R trustedpeer_genesis` | ❌ W0 | ⬜ pending |
| 10-01-02 | 01 | 1 | TPR-02 | V4 | N-of-M quorum required for add/remove/replace; sub-quorum rejected, set unchanged | unit (propose/sign/confirm, positive+negative) | `ctest -R trustedpeer_quorum` | ❌ W0 | ⬜ pending |
| 10-01-03 | 01 | 1 | TPR-03 | — | Implementation is a pure ISignedCRDTData/SecureCrdt consumer, no parallel signature logic | code-inspection (grep-based) | `grep -rn "secp256k1\|VerifySignature\|VerifyPayloadSignature" src/trustedpeer/` returns zero direct crypto calls outside SecureCrdt delegation | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `test/src/trustedpeer/CMakeLists.txt` — new test target, modeled on `test/src/securecrdt/CMakeLists.txt`
- [ ] `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp` — covers TPR-01
- [ ] `test/src/trustedpeer/trustedpeerregistry_quorum_test.cpp` — covers TPR-02
- [ ] A genesis-ceremony test helper producing a valid `{signature, bootstrapper_address}` pair for fixtures, reusable across both new test files
- [ ] `src/trustedpeer/CMakeLists.txt` — new library target, linking `securecrdt`
- [ ] `src/CMakeLists.txt` and `test/src/CMakeLists.txt` — insert `add_subdirectory(trustedpeer)`

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| No parallel/duplicate signature-verification logic exists outside SecureCrdt | TPR-03 | Structural/architectural property, not a runtime behavior — best verified by code inspection | `grep -rn "secp256k1\|VerifySignature\|VerifyPayloadSignature" src/trustedpeer/` — confirm zero direct crypto calls; all verification must delegate through `SecureCrdt`/`multisig::` |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 30s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved (plan-checker PASS, 2026-07-24)
