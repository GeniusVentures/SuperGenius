---
phase: 17
slug: eligibility-policy-gating-dry-run
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-10-16
---

# Phase 17 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test + CTest (C++17, existing repo infrastructure) |
| **Config file** | none — existing targets (`crdt_test`, `globaldb_integration_test`, account/transaction targets) host the new files |
| **Quick run command** | `build/OSX/Release/test_bin/crdt_test --gtest_filter='Prune*'` + account-side prune filters (per task) |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | quick ~30-60s · full suite per repo baseline (SC gates run it) |

---

## Sampling Rate

- **After every task commit:** quick filter(s) for the touched surface, double-run for eligibility suites (fresh-DB discipline from Phase 16)
- **After every plan wave:** full CTest suite (existing suite stays green — zero behavior change when config absent)
- **Before `/gsd:verify-work`:** full suite green + allowlist/fail-closed ctests green
- **Max feedback latency:** ~60 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 17-XX-XX | — | — | PRUNE-01 | — | exact-set eligibility incl. ancient-tx-recent-spend counterexample | unit (deterministic fixture) | quick filter above | ❌ W0 | ⬜ pending |
| 17-XX-XX | — | — | PRUNE-03 | — | multi-element block gate: any non-eligible sibling → block ineligible (elements AND tombstone keys) | unit | quick filter | ❌ W0 | ⬜ pending |
| 17-XX-XX | — | — | GATE-04 | — | cert-slot resolution with pruned own frontier; cold-start nonce chain correct | integration (CertificateFallbackTest family) | account-side test target | ✅ (family exists) | ⬜ pending |
| 17-XX-XX | — | — | GATE-01 | — | Light-only fail-closed + `/bc-<netid>/tx/` allowlist; out-of-prefix rejected | unit + ctest | ctest -R allowlist/fail-closed filters | ❌ W0 | ⬜ pending |
| 17-XX-XX | — | — | GATE-06 | — | config knobs default-off; absent keys = today's behavior; resolved values INFO-logged | unit | config parse test | ❌ W0 | ⬜ pending |

*Task IDs assigned by the planner. Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

From 17-RESEARCH.md:

- [ ] `CrdtDatastore`→`GlobalDB` priority passthrough (`GetPriority` is public on CrdtSet but `set_` is private in CrdtDatastore — ~2-line exposure) — prerequisite for the window
- [ ] Block-gate keys exposure (sibling keys-only variant; `GetLocalDeltaKeyValues` is elements-only and must cover `delta.tombstones()` keys too)
- [ ] REQUIREMENTS.md out-of-scope row amendment (spent_by_txid population authorized by D-02 Candidate-B adoption) — planning-time doc change
- [ ] Deterministic eligibility fixture: three-way window discriminator — recent-spend-recent-tx / ancient-tx-recent-spend (owner's counterexample) / ancient-spend-ancient-tx — plus genuine-vs-synthesized tombstone cases
- [ ] `spent_by_txid` population at all 4 `ConsumeUTXOs` call sites (spender in scope at :2637/:2657/:2702/:2871) with snapshot/load/revert symmetry preserved (:389-390, :1038-1043, :925-937)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| None expected | — | — | All phase behaviors have automated verification (evaluator is a pure function; dry-run produces no writes) |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 60s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
