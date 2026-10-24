---
phase: 20
slug: reclamation-proof-e2e-hardening
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-10-24
---

# Phase 20 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test + CTest (existing) |
| **Config file** | none — existing targets + one new dedicated gate target |
| **Quick run command** | `tx_pruner_test --gtest_filter='*Reclaim*:*Compact*'` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | quick ~30-90s · gate target ≤600s (TIMEOUT) · full suite per baseline |

---

## Sampling Rate

- **After every task commit:** the task's quick filter, double-run for reclamation suites
- **After every plan wave:** full suite
- **Before `/gsd:verify-work`:** full suite green + the gate target green
- **Max feedback latency:** ~90s (gate target excluded — it runs at its own budget)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 20-XX | — | — | RECL-01 | — | CompactStorage surface (kForce, no write stall, cancel-atomic) + post-cycle threshold scheduling + bytes_reclaimed stamping at compaction | unit + integration | Reclaim filter | ❌ W0 | ⬜ pending |
| 20-XX | — | — | RECL-02 | — | dedicated gate: 200×~10KB → prune → one compaction → ≥60% of erased bytes reclaimed (INCLUDE_FILES\|INCLUDE_MEMTABLES, 0xFF high bound) | gate target (600s) | gate target | ❌ W0 | ⬜ pending |
| 20-XX | — | — | GATE-02 | — | non-tx values byte-identical AFTER compaction; full-node key-set equality across the light's prune+compact | integration ×2 | identity filter | ❌ W0 | ⬜ pending |
| 20-XX | — | — | GATE-02 | — | window-edge race: seam-held spend lands between evaluation and erase; skipped this cycle; picked up next; debug+release legs | integration ×2 | race filter | ❌ W0 | ⬜ pending |

*Task IDs assigned by the planner.*

---

## Wave 0 Requirements

From 20-RESEARCH.md:

- [ ] The compaction surface (GlobalDB::CompactStorage + GetApproximateDiskUsageBytes) — owner-vended, the one recipe home
- [ ] The dedicated gate target (addtest block; TIMEOUT 600; rides the full suite automatically)
- [ ] The SC-4 retention mesh (tx_pruner-side light + EraseNodeCollection-recipe full node)
- [ ] The measurement sentinels encoded once (0xFF high bound + INCLUDE_FILES|INCLUDE_MEMTABLES) — shared helper, not per-test copies

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Live staging disk observation | RECL-01/02 | Live net over days | The demo node's `du` of the DB dir trends down after cycles with compaction; GetPruneStatus reports bytes_reclaimed |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 90s (gate excluded)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
