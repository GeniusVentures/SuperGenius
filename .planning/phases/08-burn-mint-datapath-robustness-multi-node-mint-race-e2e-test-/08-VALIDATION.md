---
phase: 8
slug: burn-mint-datapath-robustness-multi-node-mint-race-e2e-test
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-07-16
---

# Phase 8 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test (GTest/GMock) via CMake `addtest()`; libFuzzer targets via new `addfuzztarget()` |
| **Config file** | `cmake/functions.cmake`, per-suite `CMakeLists.txt` |
| **Quick run command** | targeted test binary from `build/.../test_bin/` (per plan) |
| **Full suite command** | `ctest` in the platform build dir |
| **Estimated runtime** | race suite is heavyweight (11 nodes + Anvil); quick runs use unit-level targets |

---

## Sampling Rate

- **After every task commit:** Run the targeted test binary for the touched suite
- **After every plan wave:** Run the affected suite binaries (race suite gated on Foundry availability)
- **Before `/gsd:verify-work`:** Full suite must be green; fuzz smoke (60s/target) must not crash
- **Max feedback latency:** minutes for the race suite; seconds for fuzz smoke and mock-transport units

---

## Per-Task Verification Map

*To be filled by the planner — one row per task, mapping to D-01..D-16 from CONTEXT.md.*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | | | | | | | | | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] New race-suite directory + CMake target compiles and links against existing testutil/anvil fixture helpers
- [ ] `-DSGNS_FUZZING=ON` configure path succeeds with Clang (fuzz targets build empty-corpus)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Deep fuzz runs (>60s) | D-14 | Long-running, local-only per CONTEXT.md | Run each fuzzer locally with corpus dir for ≥30 min; triage any crashes |
| Race suite at scale (>11 nodes) | D-05 | Resource-dependent | Raise kNodeCount locally, observe stability |
