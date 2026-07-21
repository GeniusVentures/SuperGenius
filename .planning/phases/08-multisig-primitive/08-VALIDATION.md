---
phase: 8
slug: multisig-primitive
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-07-21
---

# Phase 8 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GTest + GMock, driven via CTest (`add_test` inside `addtest()`, `cmake/functions.cmake:8-25`) |
| **Config file** | No dedicated per-test config; target registration lives in each directory's `CMakeLists.txt` (e.g. `test/src/blockchain/CMakeLists.txt`) |
| **Quick run command** | `ctest -R multisig` |
| **Full suite command** | `ctest` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest -R multisig`
- **After every plan wave:** Run `ctest` (full suite)
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 08-01-01 | 01 | 0 | MSIG-01 | — | Valid signature over payload verifies true; tampered payload/signature verifies false | unit | `ctest -R multisig_verify_test` | ❌ W0 | ⬜ pending |
| 08-01-02 | 01 | 1 | MSIG-02 | — | N-of-M quorum boundary cases (N, N-1, all M) report correct has_quorum | unit | `ctest -R multisig_quorum_test` | ❌ W0 | ⬜ pending |
| 08-01-03 | 01 | 1 | MSIG-03 | — | Test targets build/link without `crdt_globaldb`/`genius_node`/`genius_node_test`/pubsub | build/link check | `cmake --build . --target multisig_verify_test multisig_quorum_test` (inspect link deps) | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `src/multisig/CMakeLists.txt`, `src/multisig/MultiSig.hpp` (+ optional `.cpp`) — new library target
- [ ] `test/src/multisig/CMakeLists.txt`, `test/src/multisig/multisig_verify_test.cpp`, `test/src/multisig/multisig_quorum_test.cpp` — new test targets, following the `addtest(...)` + `target_link_libraries(... sgns_genius_account)` pattern from `test/src/blockchain/CMakeLists.txt`'s no-node targets
- [ ] `src/CMakeLists.txt` — insert `add_subdirectory(multisig)`
- [ ] `test/src/CMakeLists.txt` — insert `add_subdirectory(multisig)`

---

## Manual-Only Verifications

*All phase behaviors have automated verification.*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
