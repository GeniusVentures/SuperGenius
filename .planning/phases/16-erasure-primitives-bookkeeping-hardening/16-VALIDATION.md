---
phase: 16
slug: erasure-primitives-bookkeeping-hardening
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-10-15
---

# Phase 16 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test + CTest (C++17, existing repo infrastructure) |
| **Config file** | none — existing targets; new test file links into them (see Wave 0) |
| **Quick run command** | `ctest --test-dir <build> -R "ErasePrimitives|GlobalDB" --output-on-failure` |
| **Full suite command** | `ctest --test-dir <build> --output-on-failure` |
| **Estimated runtime** | quick ~30-60s · full suite per repo baseline |

---

## Sampling Rate

- **After every task commit:** Run the quick command (erase-primitives + globaldb selections)
- **After every plan wave:** Run the full CTest suite (criterion 5: existing suite stays green)
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** ~60 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 16-XX-XX | — | — | PRUNE-02 | — | physical erase, no tombstone, no hooks | unit (fresh RocksDB) | quick command above | ❌ W0 | ⬜ pending |
| 16-XX-XX | — | — | GATE-03 | — | seal marker present pre+post erase; isResolved asserted | unit | quick command above | ❌ W0 | ⬜ pending |

*Task IDs assigned by the planner; each plan task maps its requirement + command here. Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

From 16-RESEARCH.md Validation Architecture (Wave 0 gaps):

- [ ] New erase-primitives unit fixture file — wires real `RocksdbDatastore` + `MerkleDagServiceImpl` from a temp DB handle (existing `CustomDagSyncer`/`InMemoryDatastore` fixture cannot prove one-batch atomicity or seal retention)
- [ ] GlobalDB-level integration test file (placement: `globaldb_integration_test` target recommended by research) for the single-entry erase API
- [ ] CMake link dependency for the new test file
- [ ] Grep-gate mechanism decision — no source-scan ctest precedent exists in the repo; planner picks the enforcement shape for the no-tombstone gate

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Zero production behavior change (pruner unwired) | PRUNE-02 | Absence of wiring is a review property | Verify no production call sites added; `removeNode` callers remain test-only |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 60s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
