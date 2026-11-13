---
phase: 18
slug: txpruner-service-first-real-prune
status: active
nyquist_compliant: true
wave_0_complete: true
created: 2026-10-21
plans_assigned: 2026-10-21
---

# Phase 18 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test + CTest (C++17, existing infrastructure) |
| **Config file** | none — existing targets host the new files |
| **Quick run command** | `build/OSX/Release/test_bin/tx_pruner_test --gtest_filter='TxPruner*'` + `ctest -R "crdt_test|globaldb_integration_test"` selections per task |
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
| 18-06-T1 | 18-06 | 4 | PRUNE-04 | T-18-0601 | real erase: four-step order, both levels gone, marker written, unspent UTXO set untouched (byte-identity) + negative control + idempotence | integration (real store) | `tx_pruner_test --gtest_filter='TxPruner.RealPrune*:TxPruner.Idempotent*'` | ❌ W0 (18-06 creates) | ⬜ pending |
| 18-07-T1 | 18-07 | 5 | PRUNE-04 | T-18-0701 | crash-any-step → fixed point (both completeness directions); re-run no-op; optional true-kill | seam matrix (both seams) | `tx_pruner_test --gtest_filter='TxPruner.CrashFixedPoint*'` ×2 | ❌ W0 (extends 18-06 file) | ⬜ pending |
| 18-01-T2 | 18-01 | 1 | PRUNE-05 | T-18-0101/02 | re-broadcast pruned root + restart head-exchange → zero fetch attempts, zero re-created elements; negative control (unpruned still fetches) | two-node fixture (real pubsub) | `globaldb_integration_test --gtest_filter='*Redelivery*'` ×2 | ✅ (extend) | ⬜ pending |
| 18-06-T2 | 18-06 | 4 | PRUNE-05 | T-18-0403 | resurrected entries re-erased by cycle idempotence (sweep-as-cycle, D-02b); shared-block group-or-defer + BLOCK_ALREADY_ERASED completion (no stranding) | integration | `tx_pruner_test --gtest_filter='TxPruner.SweepReprunes*:TxPruner.SharedBlock*'` | ❌ W0 | ⬜ pending |
| 18-06-T3 | 18-06 | 4 | RECL-03 | T-18-0406 | per-cycle log: examined/erased counts, watermark advance, bytes-reclaimed explicit placeholder; generation tagging | integration (status + log) | `tx_pruner_test --gtest_filter='TxPruner.Observability*'` | ❌ W0 | ⬜ pending |
| 18-06-T3 | 18-06 | 4 | RECL-04 | T-18-0406 | queryable status via the testable provider: watermark monotonic (no advance on errored cycle), stats match actions, concurrent read safe | unit (provider) + integration | `tx_pruner_test --gtest_filter='TxPruner.PruneStatus*'` | ❌ W0 | ⬜ pending |
| 18-02-T2 | 18-02 | 1 | PRUNE-04 (policy) | T-18-0201..03 | dev-mint: designated net {333,144} certifies burn-less via FULL consensus; non-designated validators REFUSE (both bypass legs scoped — empty-chain-id leg explicitly); regardless of proposer | unit + multi-peer real consensus | `dev_mint_policy_test --gtest_filter='*EvidenceBypass*:*Validator*:*DesignatedNet*:*Refuses*'` | ❌ W0 (18-02 creates) | ⬜ pending |
| 18-03-T1 | 18-03 | 2 | PRUNE-04 | T-18-0301 | compose critical section closes WR-02 by construction (behavior-preserving; existing suites green unedited) | regression (existing suites) | `ctest -R "crdt_test|globaldb_integration|prune_eligibility"` | ✅ | ⬜ pending |
| 18-03-T2 | 18-03 | 2 | PRUNE-04 | T-18-0302/04 | marker joins the ONE batch (value XOR marker atomic — seam-interrupted proof); max_erases_per_cycle/settle_seconds fail-closed config | integration + config unit | `prune_eligibility_test` (new cases) | ✅ (extend) | ⬜ pending |
| 18-03-T3 | 18-03 | 2 | PRUNE-04 | T-18-0303 | BLOCK_ALREADY_ERASED completes otherwise-eligible erases (explicit evidence); corrupt-block stays fail-closed; one resolution home (no fork) | unit (gate verdicts) | `prune_eligibility_test` (gate cases) | ✅ (extend) | ⬜ pending |
| 18-06-T3 | 18-06 | 4 | PRUNE-04 (D-01) | T-18-0301 | concurrent PutElems cannot interleave with the compose (never-torn invariant); guards re-checked inside the lock | integration (race injection via seam) | `tx_pruner_test --gtest_filter='TxPruner.ComposeLock*'` | ❌ W0 | ⬜ pending |
| 18-06-T2 | 18-06 | 4 | PRUNE-04 (SC-4 gate) | T-18-0401 | tick gate refuses non-Light / non-READY / settle-not-elapsed; missing-set accessor covered | unit + integration | `tx_pruner_test --gtest_filter='TxPruner.TickGate*'` | ❌ W0 | ⬜ pending |
| 18-05-T1/T2 | 18-05 | 3 | PRUNE-04 (folded todo) | T-18-0501..03 | consensus-rejected local mint leaves no dangling UTXO_BRIDGE/reservation (reconciled delta or evidenced descope); idempotent; normal-tx FAILED untouched | integration (real reject path) | `transaction_manager_certificate_fallback_test --gtest_filter='*BurnCleanup*'` | ✅ (extend) | ⬜ pending |
| 18-07-T2 | 18-07 | 5 | PRUNE-04/05 | T-18-0702/03 | containment gates in sanctioned-caller-set shape: exactly TxPruner (+ tests) erase/marker callers; dry-run body token-clean; real cycle pinned to dry_run==false; mutation-verified | gate tests (source scan) | `crdt_test --gtest_filter='*EraseSurfaceGate*'` | ✅ (rework) | ⬜ pending |
| 18-07-T3 | 18-07 | 5 | all | T-18-0704 | full default suite green (SC-4 zero-change arm); format/tidy/stub/inventory sweep recorded | full suite | `ctest --test-dir build/OSX/Release --output-on-failure -j8` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

All Wave-0 scaffolds are assigned to plans (original list from 18-RESEARCH.md → delivering task):

- [x] TxPruner service test file — created by **18-06-T1** (fixture + RealPrune + Idempotent)
- [x] Two-node real-pubsub fixture for SC-3 — **18-01-T2** (EraseNodeCollection analog, extends globaldb_erase_test.cpp in the globaldb_integration_test target)
- [x] Containment gates reshaped (sanctioned-caller-set; marker PutRaw joins the production batch) — **18-07-T2**
- [x] Block-grouped K-budgeting (group-or-defer + missing-block-as-evidence) — structural in **18-04-T1**, gate verdict in **18-03-T3**, tested by **18-06-T2** (shared-block fixture)
- [x] Dev-mint policy tests (production twin, both bypass legs scoped, non-designated refusal) — **18-02-T2**
- [x] Compose-lock exposure (D-01) — **18-03-T1**; proof suite **18-06-T3**
- [x] Missing-set accessor + production pruned-erase driver — **18-03-T3**
- [x] PruningConfig new keys fail-closed — **18-03-T2**
- [x] Fetch-attempt observable seam — **18-01-T1**
- [x] Cycle-level interrupt seam — **18-04-T1** (service), exercised in **18-07-T1**

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Live grow-then-prune demo on staging | PRUNE-04 | Needs the live net + seeded history (D-03 policy enables it) | Enable policy on staging Light node → dev-mint → spend → wait ≥1 cycle → verify tx gone at both levels, marker present, disk intact; the bytes-reclaimed claim stays Phase 20 |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 90s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending (plans assigned 2026-10-21; quick-filter commands named per task)
