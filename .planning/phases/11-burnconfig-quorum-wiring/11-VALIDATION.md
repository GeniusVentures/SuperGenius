---
phase: 11
slug: burnconfig-quorum-wiring
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-07-24
---

# Phase 11 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest, driven via CTest (`addtest(...)` CMake macro, same as `trustedpeer`/`securecrdt` test dirs) |
| **Config file** | `test/src/trustedpeer/CMakeLists.txt` is the closest precedent (needs both a real SecureCrdt-backed single-node fixture AND a TrustedPeerRegistry instance); new `test/src/account/` additions or a new `test/src/burnconfig/CMakeLists.txt` follow the same shape |
| **Quick run command** | `ctest -R burnconfig --output-on-failure` |
| **Full suite command** | `ctest --output-on-failure` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest -R burnconfig` (and `ctest -R transaction_manager` if existing TransactionManager tests are touched)
- **After every plan wave:** Run `ctest` (full suite)
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 11-01-01 | 01 | 0 | BURN-01 | V4 | BurnConfig is a real quorum-signed CRDT value via SecureCrdt/TrustedPeerRegistry, not a hardcoded constant | unit/e2e (real SecureCrdt-backed single-node fixture) | `ctest -R burnconfig_genesis` | ❌ W0 | ⬜ pending |
| 11-01-02 | 01 | 1 | BURN-02 | — | TransactionManager caches the value, refreshes via CRDT-change callback, no CRDT read on PayEscrow path | unit | `ctest -R burnconfig_cache_refresh` | ❌ W0 | ⬜ pending |
| 11-01-03 | 01 | 1 | BURN-03 | — | Fresh genesis node burns exactly 1% (BURN_BASIS_POINTS=100) by default until quorum-signed update changes it | unit/e2e | `ctest -R burnconfig_genesis_default` | ❌ W0 | ⬜ pending |
| 11-01-04 | 01 | 1 | D-07 (security) | V4 | Constructing TrustedPeerRegistry/BurnConfig with a below-majority-floor threshold FAILS (rejects construction) | unit | `ctest -R burnconfig_threshold_floor` and `ctest -R trustedpeer_threshold_floor` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `src/account/BurnConfig.hpp`/`.cpp` (or `src/burnconfig/` if planner chooses a small standalone dir) — new BurnConfigPayload + wrapper
- [ ] New test file(s) covering BURN-01/02/03 and the D-07 majority-floor rejection tests, reusing `test/src/securecrdt/securecrdt_test_node.hpp`'s fixture
- [ ] CMake wiring for the new test target(s)
- [ ] `src/account/CMakeLists.txt` — confirm/add `trustedpeer` link to `sgns_genius_account`/`genius_node` targets (flagged by RESEARCH.md as needing verification)
- [ ] New `sgns_config.json` fields for both quorum thresholds (TrustedPeerRegistry's own + BurnConfig's separate one, per D-06)

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

**Approval:** approved (drafted from research findings, 2026-07-24)
