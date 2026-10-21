---
phase: 18
slug: txpruner-service-first-real-prune
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-10-21
---

# Phase 18 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test + CTest (C++17, existing infrastructure) |
| **Config file** | none — existing targets host the new files |
| **Quick run command** | `build/OSX/Release/test_bin/prune_eligibility_test --gtest_filter='TxPruner*'` + `ctest -R "crdt_test|globaldb_integration_test"` selections per task |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | quick ~30-90s · full suite per baseline (SC gates run it) |

---

## Sampling Rate

- **After every task commit:** task's quick filter(s), double-run for erase suites (fresh-DB discipline)
- **After every plan wave:** full CTest suite (existing suite stays green; default-off config erases nothing)
- **Before `/gsd:verify-work`:** full suite green + all containment gates in their new sanctioned-caller shape
- **Max feedback latency:** ~90 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 18-XX-XX | — | — | PRUNE-04 | — | real erase: four-step order, both levels gone, marker written, unspent UTXO set untouched | integration (real store) | quick filter | ❌ W0 | ⬜ pending |
| 18-XX-XX | — | — | PRUNE-04 | — | crash-any-step → fixed point; re-run no-op | seam matrix (extended interrupt hook) | quick filter ×2 | ❌ W0 | ⬜ pending |
| 18-XX-XX | — | — | PRUNE-05 | — | re-broadcast pruned root + restart head-exchange → zero fetch attempts, zero re-created elements | two-node fixture (real pubsub) | globaldb/crdt selection ×2 | ❌ W0 | ⬜ pending |
| 18-XX-XX | — | — | PRUNE-05 | — | resurrected entries re-erased by cycle idempotence (sweep-as-cycle) | integration | quick filter | ❌ W0 | ⬜ pending |
| 18-XX-XX | — | — | RECL-03 | — | per-cycle log: examined/erased counts, watermark advance | integration | quick filter | ❌ W0 | ⬜ pending |
| 18-XX-XX | — | — | RECL-04 | — | queryable status (watermark + last-cycle stats) via a testable status provider | unit (provider) + integration | quick filter | ❌ W0 | ⬜ pending |
| 18-XX-XX | — | — | PRUNE-04 (policy) | — | dev-mint: designated net {333,144} certifies burn-less via full consensus; non-designated validators REFUSE (both bypass legs scoped) | unit + two-node | mint-policy filters | ❌ W0 | ⬜ pending |

*Task IDs assigned by the planner. Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

From 18-RESEARCH.md:

- [ ] TxPruner service test file (fresh-DB; extends the evaluator-fixture family) — the SC-1/SC-2 harness
- [ ] Two-node real-pubsub fixture for SC-3 (re-broadcast + head-exchange re-delivery) — GlobalDBEraseTest::EraseNodeCollection analog
- [ ] Containment gates reshaped: EraseSurfaceGate + the 16/17 zero-production-caller inventories move to sanctioned-caller-set form (TxPruner + tests are the sanctioned callers; the marker PutRaw joins the production batch)
- [ ] Block-grouped K-budgeting design (stranding edge): group-or-defer + missing-block-as-erase-evidence — encoded in the plan's task actions, tested by a shared-block fixture
- [ ] Dev-mint policy tests: TestMintInputValidator's production twin (net-gated), both bypass legs scoped, non-designated-net refusal

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Live grow-then-prune demo on staging | PRUNE-04 | Needs the live net + seeded history (D-03 policy enables it) | Enable policy on staging Light node → dev-mint → spend → wait ≥1 cycle → verify tx gone at both levels, marker present, disk intact; the bytes-reclaimed claim stays Phase 20 |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 90s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
