---
phase: 17
slug: eligibility-policy-gating-dry-run
status: draft
nyquist_compliant: true
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
| 17-01-1 | 17-01 | 1 | PRUNE-01 (doc) | — | D-02-authorized REQUIREMENTS amendment (narrow, spent_epoch stays dead) | doc change | grep -c "Candidate B" .planning/REQUIREMENTS.md | ✅ | ⬜ pending |
| 17-01-2 | 17-01 | 1 | PRUNE-01 | T-17-0102/03 | spent_by_txid populated both ConsumeUTXOs branches; read accessor; 4 call sites | impl + build | cmake --build ... --target genius_transaction_objs genius_node | ✅ | ⬜ pending |
| 17-01-3 | 17-01 | 1 | PRUNE-01 | T-17-0102 | populate/synth-not-genuine/revert-reset/rewrite+restart round-trip | unit | test_bin/utxo_manager_test --gtest_filter='*Spender*:*SpentByTxid*' | ✅ | ⬜ pending |
| 17-02-1/2 | 17-02 | 1 | PRUNE-01, PRUNE-03 | T-17-0203 | GetKeyPriority passthrough chain + GetLocalDeltaKeys sibling (elements AND tombstones; precedent untouched) | impl + build | cmake --build ... --target crdt_globaldb | ✅ | ⬜ pending |
| 17-02-3 | 17-02 | 1 | PRUNE-01, PRUNE-03 | T-17-0201 | priority read on real store; missing-key==0 caveat asserted; delta keys cover both lists; garbage CID fails closed | unit (real store) | test_bin/crdt_test --gtest_filter='*PriorityPassthrough*:*DeltaKeys*' (4 tests named PriorityPassthroughReturnsPutPriority / PriorityPassthroughToleratesMissingKey / DeltaKeysCoverElementsAndTombstones / DeltaKeysFailClosedOnUnknownCid — the two prefixes make one filter run all four) | ❌ W0 (this plan creates it) | ⬜ pending |
| 17-03-1 | 17-03 | 2 | GATE-04 | T-17-0301/03 | one-line rung relaxation; binding checks verbatim; ladder order unchanged | impl + build + diff gate | git diff audit: exactly one guard line | ✅ | ⬜ pending |
| 17-03-2 | 17-03 | 2 | GATE-04 | T-17-0301/02 | pruned frontier resolves from cert; cold-start chain; no-cert ⇒ ""; rung3==rung4; registration regression | integration (CertificateFallback family + EraseTransactionEntry) | test_bin/transaction_manager_certificate_fallback_test --gtest_filter='*CertSlotPreviousHash*' | ✅ | ⬜ pending |
| 17-04-1 | 17-04 | 2 | PRUNE-01, GATE-01 | T-17-0401/02 | contracts: reason enum, result structs, PruningConfig fail-closed, Light-only construction, one allowlist home | impl + build | cmake --build ... --target genius_transaction_objs | ❌ W0 (this plan creates it) | ⬜ pending |
| 17-04-2 | 17-04 | 2 | PRUNE-01 | T-17-0403/05/06 | Evaluate: trailing-slash enumeration, genuine-spend, most-recent-spend window, existence-before-priority, zero-write grep gate | impl + build | grep gates on PruneEligibility.cpp | ❌ W0 | ⬜ pending |
| 17-04-3 | 17-04 | 2 | PRUNE-01, GATE-01, GATE-06 | T-17-0401..06 | SC-1 exact set (3-way discriminator + owner counterexample + split-time + synthesized); SC-4 Full/Archive refuse + 5 crafted keys; config parse/validate | integration (CRDTFixture family) | test_bin/prune_eligibility_test --gtest_filter='*ExactEligibleSet*:*FailClosed*:*PruningConfig*' | ❌ W0 (this plan creates it) | ⬜ pending |
| 17-05-1 | 17-05 | 3 | PRUNE-03 | T-17-0502/05 | marker proto + tri-state lookup + test-only writer (no production callers) | impl + build | grep gates (WritePrunedEntryForTest callers) | ✅ | ⬜ pending |
| 17-05-2 | 17-05 | 3 | PRUNE-03, GATE-01 | T-17-0501/03/04 | block gate: elements AND tombstones vs eligible set ∩ allowlist; parse fail ⇒ closed; spender-pruned window refinement | impl + build | grep gates (GetLocalDeltaKeys; PutRaw containment — exactly 1, inside WritePrunedEntryForTest) | ✅ | ⬜ pending |
| 17-05-3 | 17-05 | 3 | PRUNE-03 | T-17-0501..05 | SC-2: all-eligible multi-element block; live sibling; out-of-prefix key; tombstone-of-live-key; garbage CID; marker round-trip/NOT_FOUND/invariant-violation/spender-pruned | integration | test_bin/prune_eligibility_test --gtest_filter='*BlockGate*:*PruneMarker*' | ✅ (17-04 creates file) | ⬜ pending |
| 17-06-1 | 17-06 | 3 | GATE-06 | T-17-0601 | LoadSgnsConfig pruning block (D-C5 convention, delegates to PruningConfig::FromJSON, absent ⇒ no-op) | impl + build | cmake --build ... --target genius_node + guard-containment grep | ✅ | ⬜ pending |
| 17-06-2 | 17-06 | 3 | GATE-06, GATE-01 | T-17-0602..05 | timer wiring (gc_timer_ weak-self pattern, Light+enabled+dry_run gated, per-tick evaluator construction, one INFO summary, shutdown cancel, zero erase tokens) | impl + source gates | grep EraseEntry|EraseTransactionEntry|PutRaw in GeniusNode.cpp ⇒ 0 | ✅ | ⬜ pending |
| 17-06-3 | 17-06 | 3 | GATE-06 (SC-5) | T-17-0603 | full build + full ctest green (default config = today's behavior); four erase-inventory greps unchanged | full suite | ctest --test-dir build/OSX/Release --output-on-failure -j8 | ✅ | ⬜ pending |

*Task IDs assigned by the planner. Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

From 17-RESEARCH.md:

- [ ] 17-02 T1/T2 — `CrdtDatastore`→`GlobalDB` priority passthrough (`GetPriority` is public on CrdtSet but `set_` is private in CrdtDatastore — ~2-line exposure) — prerequisite for the window
- [ ] 17-02 T2 — Block-gate keys exposure (sibling keys-only variant; `GetLocalDeltaKeyValues` is elements-only and must cover `delta.tombstones()` keys too)
- [ ] 17-01 T1 — REQUIREMENTS.md out-of-scope row amendment (spent_by_txid population authorized by D-02 Candidate-B adoption) — planning-time doc change
- [ ] 17-04 T3 — Deterministic eligibility fixture: three-way window discriminator — recent-spend-recent-tx / ancient-tx-recent-spend (owner's counterexample) / ancient-spend-ancient-tx — plus genuine-vs-synthesized tombstone cases
- [ ] 17-01 T2/T3 — `spent_by_txid` population at all 4 `ConsumeUTXOs` call sites (spender in scope at :2637/:2657/:2702/:2871) with snapshot/load/revert symmetry preserved (:389-390, :1038-1043, :925-937)

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
