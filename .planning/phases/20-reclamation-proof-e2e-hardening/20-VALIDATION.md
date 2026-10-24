---
phase: 20
slug: reclamation-proof-e2e-hardening
status: ready
nyquist_compliant: true
wave_0_complete: true
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
| **Quick run command** | `build/OSX/Release/test_bin/tx_pruner_test --gtest_filter='TxPrunerTest.CompactionGate*:TxPrunerTest.RealPrune*:TxPrunerTest.SpendRace*:TxPrunerTest.MeshRetention*'` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | quick ~30-90s · gate target ≤600s (TIMEOUT) · full suite per baseline |

---

## Sampling Rate

- After every task commit: the task's quick filter, double-run for reclamation/race/mesh suites (8x sweep for SpendRace and MeshRetention)
- After every plan wave: full suite
- Before `/gsd:verify-work`: full suite green + the reclamation_gate target green
- Max feedback latency: ~90s (gate target excluded — it runs at its own budget)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists? | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 20-01-T1 | 20-01 | 1 | RECL-01 | T-20-0102/03/04 | CompactStorage surface (kForce, no write stall, cancel-atomic passthrough, fail-closed post-shutdown) + sentinel/flags size measurement encoded ONCE + smoke (value byte-identical across rewrite) | unit+smoke | `globaldb_integration_test --gtest_filter='*Compact*'` + full `-R globaldb_integration` + grep zero other CompactRange callers in src/ | partial (extend) | ⬜ pending |
| 20-01-T2 | 20-01 | 1 | RECL-01 | T-20-0101 | pruning.compact_threshold_mb: safe 16MB default, degenerate-0 rejected at parse (write-amp DoS knob unexpressible), resolved values INFO-logged | unit | `prune_eligibility_test --gtest_filter='*PruningConfig*'` | exists (extend) | ⬜ pending |
| 20-02-T1 | 20-02 | 2 | RECL-01 | T-20-0204 | evaluation-carried size fields (no second fetch path; estimate never surfaced as measured) | unit (regression) | `ctest -R "prune_eligibility\|tx_pruner_test"` | exists (regression) | ⬜ pending |
| 20-02-T2 | 20-02 | 2 | RECL-01 | T-20-0201/02/03 | post-cycle gate conjuncts (erased≥1 ∧ !canceled ∧ watermark≥threshold), stamp-at-compaction only, watermark-kept retry, honest-split INFO | unit (regression, default threshold = zero behavior change) | full tx_pruner_test green ×2 + grep "not measured until Phase 20" == 0 | exists (regression) | ⬜ pending |
| 20-02-T3 | 20-02 | 2 | RECL-01 | T-20-0203 | shutdown cancel wiring (member atomic, io-drain-is-join) + threshold-crossed canary (compacted + bytes_reclaimed>0) | integration | `tx_pruner_test --gtest_filter='TxPrunerTest.CompactionGate*'` | exists (extend) | ⬜ pending |
| 20-03-T1 | 20-03 | 3 | RECL-02 | T-20-0301/02 | dedicated target scaffold: 5×~10KB → prune → compact → measurable drop, sibling control, TIMEOUT 600, full-suite ride | integration (smoke) | `ctest -R reclamation_gate` | ❌ W0 (created here) | ⬜ pending |
| 20-03-T2 | 20-03 | 3 | RECL-02 | T-20-0301/02/03 | 200×~10KB → ONE compaction → ≥60% of erased logical bytes + sanity ceiling + anti-vacuous before-pin (≥80%) + du belt + auto-trigger-silence (nullopt pre-call) | gate target (600s) | `ctest -R reclamation_gate` (full binary double-run) | created in 20-03-T1 | ⬜ pending |
| 20-04-T1 | 20-04 | 3 | RECL-01 | T-20-0404 | conjunct matrix: below-threshold/erased==0/canceled never compact; crossed fires once; kept-watermark retry; restart honesty; concurrent-read coherence during compacting cycle | integration (matrix) | `tx_pruner_test --gtest_filter='TxPrunerTest.CompactionGate*'` ×2 | exists (extend) | ⬜ pending |
| 20-04-T2 | 20-04 | 3 | GATE-02 | T-20-0401 | non-tx values byte-identical AFTER a measurably-real compaction (certs, policy/seed, processing results, heads, seals, sibling); D-C5 anti-vacuity | integration | `tx_pruner_test --gtest_filter='TxPrunerTest.RealPrune*'` ×2 | exists (extend) | ⬜ pending |
| 20-04-T3 | 20-04 | 3 | GATE-02 (SC-5) | T-20-0402/03 | seam-held spend during cycle: skipped THIS cycle (erased==0, skipped==1, value+marker untouched), picked up NEXT cycle (erased==1, PRUNED); no sleeps; 8x sweep; debug/release legs reported | integration ×2 legs | `tx_pruner_test --gtest_filter='TxPrunerTest.SpendRace*'` ×8 | exists (extend) | ⬜ pending |
| 20-05-T1 | 20-05 | 4 | GATE-02 (SC-4) | T-20-0501/02/04 | mesh peers provably hold 100% pre-prune; byte-identical across the light's erase; light-side mutation check | integration (real pubsub) | `tx_pruner_test --gtest_filter='TxPrunerTest.MeshRetention*'` ×2 | exists (extend) | ⬜ pending |
| 20-05-T2 | 20-05 | 4 | GATE-02 (SC-4) | T-20-0502/03 | peers byte-identical across the light's compaction (same baselines); light markers/sibling correct; 8x sweep | integration | MeshRetention ×8 + full binary + `ctest -R tx_pruner_test` | extends 20-05-T1 | ⬜ pending |

---

## Wave 0 Requirements

All Wave 0 gaps from 20-RESEARCH.md are covered by the plans:

- [x] GlobalDB::CompactStorage + GetApproximateDiskUsageBytes — Plan 20-01 Task 1
- [x] PruningConfig::compact_threshold_mb (parse, degenerate-0 reject, resolved-values INFO) — Plan 20-01 Task 2
- [x] PruneStatus.bytes_reclaimed + PruneCycleStats.compacted (+ duration) + RECL-03/Doxygen wording retirement — Plan 20-02 Task 2
- [x] TxPruner trigger members + MaybeCompactAfterCycle tail + erased-bytes estimate — Plan 20-02 Tasks 1-2
- [x] Shutdown wiring (cancel atomic; io-drain ordering verified unchanged) — Plan 20-02 Task 3
- [x] test/src/account/reclamation_gate_test.cpp + addtest registration — Plan 20-03
- [x] tx_pruner_test extensions (identity, race, trigger) — Plan 20-04
- [x] SC-4 mesh leg (hosted on the tx_pruner fixture per Open Q3) — Plan 20-05
- [x] Framework install: none needed (GTest integrated)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Live staging disk observation | RECL-01/02 | Live net over days | The demo node's `du` of the DB dir trends down after cycles with compaction; GetPruneStatus reports bytes_reclaimed |
| SC-5 debug-timing leg | GATE-02 (SC-5) | No Debug-configured tree exists (build/OSX/Release only); a cold Debug configure builds vendored RocksDB (disproportionate) | Attempt `cmake -S . -B build/OSX/Debug` + target-scoped build of tx_pruner_test if cheap; otherwise record not-run with reason in 20-04-SUMMARY — the seam-held interleaving is deterministic by construction and the release leg carries the 8x sweep |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify (regression-only tasks — 20-02-T1/T2 — are followed within one wave by their positive suites in 20-04)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 90s (gate excluded)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planner-signed 2026-10-24 (per-task map filled; checker to countersign at verify-work)
