---
phase: 9
slug: securecrdt-layer
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-07-23
---

# Phase 9 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest, driven via CTest (`addtest(...)` CMake macro, same as `multisig`/`blockchain` test dirs) |
| **Config file** | `test/src/multisig/CMakeLists.txt` is the closest precedent (no GlobalDB dependency); new `test/src/securecrdt/CMakeLists.txt` follows the same shape |
| **Quick run command** | `ctest -R securecrdt` |
| **Full suite command** | `ctest` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest -R securecrdt`
- **After every plan wave:** Run `ctest` (full suite)
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 09-01-01 | 01 | 0 | SCRDT-01 | V5 | `ISignedCRDTData` interface compiles/links with a concrete test implementer | unit | `ctest -R securecrdt_interface_test` | ❌ W0 | ⬜ pending |
| 09-01-02 | 01 | 1 | SCRDT-02 | — | Registry resolves topic/key pattern → policy entry | unit | `ctest -R securecrdt_registry_test` | ❌ W0 | ⬜ pending |
| 09-01-03 | 01 | 1 | SCRDT-03 | V4 | Under-signed write rejected locally, never applied, before quorum reached | unit (single-node GlobalDB) | `ctest -R securecrdt_quorum_gate_test` | ❌ W0 | ⬜ pending |
| 09-01-04 | 01 | 1 | SCRDT-04 | V4/V6 | Propose+sign+quorum sequence via CRDT puts/filter callbacks only | unit/integration (single-node GlobalDB) | `ctest -R securecrdt_propose_sign_quorum_test` | ❌ W0 | ⬜ pending |
| 09-02-05 | 02 | 1 | SCRDT-03 | V5 | Local ProposeValue rejects malformed/invalid payload symmetrically with remote filter path (Warning 1) | unit (single-node GlobalDB) | `ctest -R securecrdt_quorum_gate_test` | ❌ W0 | ⬜ pending |
| 09-02-06 | 02 | 1 | SCRDT-04 | V4/V6 | ReadIfQuorum handoff contract (Verify/Apply) proven end-to-end (Warning 2) | integration (single-node GlobalDB) | `ctest -R securecrdt_quorum_contract_e2e_test` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `src/securecrdt/CMakeLists.txt`, header/impl files — new library target, linking `crdt_globaldb` + `multisig`
- [ ] `test/src/securecrdt/CMakeLists.txt` — new test target following `test/src/multisig/CMakeLists.txt` pattern, plus a single-node `GlobalDB` fixture pattern adapted from `test/src/crdt/globaldb_integration.cpp` for SCRDT-03/04's tests
- [ ] `src/CMakeLists.txt` and `test/src/CMakeLists.txt` — insert `add_subdirectory(securecrdt)`

---

## Manual-Only Verifications

*All phase behaviors have automated verification.*

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 30s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved
