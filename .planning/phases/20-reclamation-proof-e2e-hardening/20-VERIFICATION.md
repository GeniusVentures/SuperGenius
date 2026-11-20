---
phase: 20-reclamation-proof-e2e-hardening
verified: 2026-10-24T21:07:15Z
status: passed
score: 10/10 must-haves verified
overrides_applied: 0
---

# Phase 20: Reclamation Proof & E2E Hardening — Verification Report

**Phase Goal:** The milestone's headline claim is proven in measured bytes — deleted data physically leaves the disk via scheduled compaction, non-transaction data is provably untouched, and the window-edge races are fault-tested.
**Verified:** 2026-10-24T21:07:15Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

Must-haves merged from ROADMAP Phase 20 Success Criteria (SC-1..SC-5) and the five PLAN frontmatters (deduplicated; plan truths add detail, roadmap SCs are the contract).

| #   | Truth                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | Status     | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1   | SC-1: A scheduled light-node-only `CompactRange` (`kForce`, off the hot path) runs after prune cycles, and measured bytes drop before-vs-after on a node with known spent history                                                                                                                                                                                                                                                                                                                                                                                  | ✓ VERIFIED | `TxPruner::MaybeCompactAfterCycle` called in the `RunCycle` tail between `RecordCompletedCycle` and return (src/transaction/TxPruner.cpp:252-254), D-01 conjuncts in order (:318-332); Light-only enforced at factory (TxPruner.cpp:72 refuses `node_type != NodeType::Light`); `kForce` in the one recipe home (src/crdt/globaldb/globaldb.cpp:929); off-hot-path (pruner's own post-cycle thread, `allow_write_stall=false` :930). Measured drop independently reproduced this verification: gate run printed `before=32134234 after=391110 reclaimed=31743124 fraction=1492%` |
| 2   | SC-2: The measured-bytes CI gate proves real reclamation — prune + compaction on known-size fixture must reclaim the expected byte delta (measured bytes, not key counts) or CI fails                                                                                                                                                                                                                                                                                                                                                                              | ✓ VERIFIED | `test/src/account/reclamation_gate_test.cpp:478-597`: 200 x ~10KB, `EXPECT_GE(reclaimed, 60% x erased_logical_bytes)` (:575), anti-vacuous before >= 80% pin (:552), measured-calibrated ceiling 20x+2MB (:591), du belt (:596); registered via `addtest` (test/src/account/CMakeLists.txt:474, ctest test #38, rides the full suite). **Re-run this verification on a freshly rebuilt binary: 2/2 PASSED in 13.2s** |
| 3   | SC-3: Non-transaction data is byte-identical across a prune run — certificates, SecureCRDT policy values, processing results, DAG heads, seal markers (ctest)                                                                                                                                                                                                                                                                                                                                                                                                    | ✓ VERIFIED | `RealPrune_ErasesEligibleAndLeavesEverythingElseByteIdentical` (erase-time, pre-existing :1501) + `RealPrune_PostCompactionEverythingElseStaysByteIdentical` (:1635-1757): production `CompactStorage(nullptr)` INSIDE the test between snapshot pairs (:1709), mutation check `EXPECT_LT(post, pre)` (:1715), byte-identity of /utxo/ prefix, cert record, seed values, head map + maxHeight, all seal markers, blocks, and the unspent sibling. **Re-run green this verification.** Caveat (see W-1): SecureCRDT-policy/processing-result classes are covered by same-storage-class stand-ins (`/cert/`-shaped + `/seed/` CRDT values), not literal policy records |
| 4   | SC-4: Full and archive nodes in the same mesh retain 100% of their transaction history across the light node's prune cycles (ctest)                                                                                                                                                                                                                                                                                                                                                                                                                                | ✓ VERIFIED | `MeshRetention_FullAndArchivePeersHoldIdenticalHistoryAcrossPruneAndCompactionCycles` (:3400-3745): two GlobalDB peers (`AddMeshPeer` :319, loopback advertise + one-direction dial + bounded waits), bounded pre-prune per-peer polls with non-empty baseline pins (:3478-3507), CHECKPOINT 1 erase retention + light-side mutation (:3536-3557), CHECKPOINT 2 compaction + size drop + peer identity (:3565-3580), ROUND 2 with union baselines and cumulative-retention pins (:3599-3719). **Re-run green this verification (4.4s)** |
| 5   | SC-5: Window-edge races fault-tested — spend arriving during an active cycle never erased (re-verified under lock before each erase), picked up next cycle                                                                                                                                                                                                                                                                                                                                                                                                        | ✓ VERIFIED | `SpendRace_RevertedSpendSkipsThisCycleAndNextCyclePicksItUp` (:3087-3218): deterministic seam-held hold at `kInterruptAfterEvaluation` (the documented, justified seam correction — the landed per-candidate order runs re-verify BEFORE `kInterruptBeforeErase`), two-step revert (durable record + `LoadUTXOs` reload), leg (a) erased==0/skipped==1/value present/marker NOT_FOUND asserted mid-hold AND post-release, leg (b) next cycle erased==1/PRUNED/value gone. **Re-run green this verification (1.1s).** Debug leg NOT run — documented deviation, see W-2 |
| 6   | 20-01: The compaction recipe (kForce, allow_write_stall=false, canceled passthrough, whole-DB bounds) and the whole-DB measurement (0xFF sentinel, files+memtables) live in exactly ONE home; both fail closed after ShutdownNow                                                                                                                                                                                                                                                                                                                                   | ✓ VERIFIED | globaldb.cpp:911-987; `grep -rn "CompactRange(" src/` → zero callers outside globaldb.cpp (machine-checked this verification); `kForce` count in globaldb.cpp == 1; `INCLUDE_MEMTABLES` count in src/ == 1; `ActiveDataStore()` first act with `Error::GLOBALDB_NOT_STARTED` (:913-918, :955-960); `IsManualCompactionPaused` → `std::errc::operation_canceled` distinct clean-skip (:943-948); sentinel string named before the borrowed Slice (:968-970) |
| 7   | 20-01: `pruning.compact_threshold_mb` parses with ~16MB safe default, degenerate 0 rejected at read time, resolved value INFO-logged; (review WR-02) wrap-range values rejected so `<< 20` cannot silently derive 0                                                                                                                                                                                                                                                                                                                                               | ✓ VERIFIED | PruneEligibility.hpp:282 (default 16), parse block PruneEligibility.cpp:114-128 with `kMaxCompactThresholdMb = max >> 20` guard (:122-123) and comment naming WR-02; four FromJSON cases C11-C14 (prune_eligibility_test.cpp:774-807) — **re-run 14/14 green**; GeniusNode resolved-values INFO carries `compact_threshold_mb={}` (GeniusNode.cpp:486-492) |
| 8   | 20-02: A threshold-crossed completed cycle triggers exactly one compaction through the surface before RunCycle returns; `bytes_reclaimed` nullopt until first compaction then the measured delta under `status_mutex_`; canceled/failed compaction never fails the cycle and the watermark is kept for retry; erase-time INFO counts-only, compaction completion INFO carries before/after/delta/duration                                                                                                                                                                                                                                                                                          | ✓ VERIFIED | TxPruner.cpp:313-413 (conjunct order, baseline-at-last-compaction lazy init, cancel-skip INFO :361, failure WARN watermark kept :368, success stamps under `status_mutex_` :396-403, one INFO :406-412); `bytes_reclaimed` optional on PruneStatus (TxPruner.hpp:147); watermark accumulation from evaluation-carried fields only on successful erases (:229); "not measured until Phase 20" and "Phase 20" tokens: 0 matches in src/ (re-checked). CompactionGate matrix 7 TEST_Fs (:2293-2715) — **re-run 7/7 green (10.4s)** covering below-threshold, erased==0, crossed-once + stamp retention (both forms), canceled-before-start + persistence + fresh-instance retry, fresh-instance honesty, concurrent-read coherence |
| 9   | 20-02 + review CR-01: shutdown cancellation is wired into the LIVE shutdown path (not dead code) — the member atomic outlives any in-flight CompactRange                                                                                                                                                                                                                                                                                                                                                                                                         | ✓ VERIFIED | `GeniusNode::ShutdownForDestruction` (GeniusNode.cpp:2532, called from `~GeniusNode` :2672) calls `tx_pruner_->RequestCompactionCancel()` at :2573 BEFORE `io_->stop()` (:2629) and thread joins (:2632-2645), with the CR-01 comment naming the dead-path history (:2566-2570). `compaction_canceled_` is a TxPruner MEMBER (TxPruner.hpp:414, "never reset"); `RequestCompactionCancel` sets it (TxPruner.cpp:496-502) |
| 10  | 20-03: The gate is provably non-vacuous — before >= 80% of erased logical bytes (not memtable/empty-range blind), markers PRUNED (prune really ran), auto-trigger provably silent (`bytes_reclaimed` nullopt pre-direct-call), du belt over db_path_ drops or stays flat                                                                                                                                                                                                                                                                                        | ✓ VERIFIED | reclamation_gate_test.cpp:525 (erased==200), :531-538 (every marker PRUNED), :543 (nullopt pin), :552 (80% pin), :556-557+595-596 (du belt). All layers present and passing in the fresh re-run; `sleep_for` count in the gate file: 0 (all waits bounded) |

**Score:** 10/10 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
| -------- | -------- | ------ | ------- |
| `src/crdt/globaldb/globaldb.hpp` | CompactStorage + GetApproximateDiskUsageBytes public contract | ✓ VERIFIED | :302, :319 with full Doxygen (fail-close, error mapping, one-home notes) |
| `src/crdt/globaldb/globaldb.cpp` | The one compaction-recipe home | ✓ VERIFIED | :911-987; kForce x1, CompactRange whole-DB overload, sentinel + flags encoded once |
| `src/transaction/PruneEligibility.hpp` | compact_threshold_mb member + FromJSON doc | ✓ VERIFIED | :282 member, :303-308 doc |
| `src/transaction/PruneEligibility.cpp` | Twin parse block + WR-02 guard | ✓ VERIFIED | :111-128 |
| `src/transaction/TxPruner.hpp` | compaction members, bytes_reclaimed, RequestCompactionCancel | ✓ VERIFIED | :147, :265, :399-414 |
| `src/transaction/TxPruner.cpp` | MaybeCompactAfterCycle + honest-split INFO | ✓ VERIFIED | :313-413, call at :253 |
| `src/account/GeniusNode.cpp` | INFO extension + LIVE stop-path cancel | ✓ VERIFIED | :486-492, :2571-2574 (in ShutdownForDestruction, reachable from ~GeniusNode :2672) |
| `test/src/crdt/globaldb_erase_test.cpp` | CompactStorageSmoke | ✓ VERIFIED | :1283-1321; **re-run 1/1 green** (idempotent double compaction, byte-identity of value + block) |
| `test/src/account/prune_eligibility_test.cpp` | C11-C14 FromJSON cases | ✓ VERIFIED | :774-807; **re-run 14/14 green** |
| `test/src/account/reclamation_gate_test.cpp` | Dedicated measured-bytes gate (min 200 lines) | ✓ VERIFIED | 597 lines; two suites; **full binary re-run 2/2 green** |
| `test/src/account/CMakeLists.txt` | addtest registration | ✓ VERIFIED | :474-489; ctest #38 discovered |
| `test/src/account/tx_pruner_test.cpp` | CompactionGate matrix + RealPrune_PostCompaction + SpendRace + MeshRetention | ✓ VERIFIED | 4509 lines; 11 phase-20 TEST_Fs; filters **re-run green: CompactionGate* 7/7, SpendRace* 1/1, MeshRetention+PostCompaction 2/2** |

### Key Link Verification

Note: `gsd-sdk query verify.key-links` reports "Source file not found" for three 20-01 links — a tooling artifact (it cannot parse the parenthesized `from` fields, e.g. `src/.../globaldb.cpp (CompactStorage)`; the one bare-path link verifies). Manual line-level verification below is authoritative.

| From | To | Via | Status | Details |
| ---- | -- | --- | ------ | ------- |
| globaldb.cpp CompactStorage | getDB()->CompactRange | whole-DB overload | ✓ WIRED | globaldb.cpp:938 `CompactRange( options, nullptr, nullptr )` via `ActiveDataStore()->getDB()` |
| globaldb.cpp GetApproximateDiskUsageBytes | getDB()->GetApproximateSizes | sentinel + flags | ✓ WIRED | globaldb.cpp:968-980 |
| PruneEligibility.cpp FromJSON | compact_threshold_mb | twin parse block | ✓ WIRED | PruneEligibility.cpp:114-128 |
| GeniusNode.cpp | pruning_config_.compact_threshold_mb | resolved-values INFO | ✓ WIRED | GeniusNode.cpp:486-492 |
| TxPruner.cpp RunCycle tail | GlobalDB::CompactStorage/GetApproximateDiskUsageBytes | MaybeCompactAfterCycle between RecordCompletedCycle and return | ✓ WIRED | TxPruner.cpp:252-254, :354, :341, :375 |
| GeniusNode live stop path | TxPruner::RequestCompactionCancel | beside prune-timer cancel, before io stop/join | ✓ WIRED | GeniusNode.cpp:2560-2574 (ShutdownForDestruction; CR-01 fix) |
| reclamation_gate_test.cpp | GlobalDB surface | production surface only, no test-side CompactRange | ✓ WIRED | `grep -rn "CompactRange(" test/` → 0; gate calls CompactStorage/GetApproximateDiskUsageBytes only |
| CMakeLists.txt | ctest / CI full suite | addtest block | ✓ WIRED | CMakeLists.txt:474; ctest -N shows test #38 |
| CompactionGate suites | PruneStatus.bytes_reclaimed / stats.compacted | stats/status asserts | ✓ WIRED | e.g. :2456-2485 (stamp + both retention forms) |
| identity suite | GlobalDB::CompactStorage | between snapshot pairs | ✓ WIRED | tx_pruner_test.cpp:1707-1716 |
| SpendRace suite | cycle interrupt seam | blocking deadline-bounded hook | ✓ WIRED | :3121-3139 at `kInterruptAfterEvaluation` (documented seam correction) |
| MeshRetention suite | EraseNodeCollection recipe / CompactStorage | AddMeshPeer + direct compaction between checkpoints | ✓ WIRED | :319-395 helper, :3567/:3708 compactions |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| -------- | ------------- | ------ | ------------------ | ------ |
| reclamation_gate_test | reclaimed (before - after) | GetApproximateDiskUsageBytes around CompactStorage | Yes — independently reproduced 32,134,234 → 391,110 bytes | ✓ FLOWING |
| TxPruner MaybeCompactAfterCycle | status_m.bytes_reclaimed | measured GetApproximateSizes delta, stamped only on success | Yes — canary + matrix assert has_value > 0 | ✓ FLOWING |
| MeshRetention peer baselines | PeerHistory (values/blocks/crdt/heads) | peers' own public query surface | Yes — non-empty baseline pins (:3499-3507) | ✓ FLOWING |

### Behavioral Spot-Checks

All binaries freshly rebuilt against HEAD (post review-fix commit e31fbe6de) before running — two tree binaries predated the fix commit, so targets were rebuilt to guarantee the results reflect current source.

| Behavior | Command | Result | Status |
| -------- | ------- | ------ | ------ |
| RECL-02 headline gate (full binary) | `build/OSX/Release/test_bin/reclamation_gate_test` | 2/2 PASSED (13.2s); `[reclamation-gate] erased_logical_bytes=2126474 before=32134234 after=391110 reclaimed=31743124 fraction_of_erased_logical=1492%` | ✓ PASS |
| Trigger conjunct matrix | `tx_pruner_test --gtest_filter='TxPrunerTest.CompactionGate*'` | 7/7 PASSED (10.4s) | ✓ PASS |
| SC-5 race both legs | `tx_pruner_test --gtest_filter='TxPrunerTest.SpendRace*'` | 1/1 PASSED (1.1s) | ✓ PASS |
| SC-4 mesh + GATE-02 post-compaction identity | `tx_pruner_test --gtest_filter='TxPrunerTest.MeshRetention*:TxPrunerTest.RealPrune_PostCompaction*'` | 2/2 PASSED (4.4s) | ✓ PASS |
| Config knob FromJSON cases | `prune_eligibility_test --gtest_filter='*PruningConfig*'` | 14/14 PASSED (0.5s) | ✓ PASS |
| Surface smoke | `globaldb_integration_test --gtest_filter='CompactStorageSmoke*'` | 1/1 PASSED (0.08s) | ✓ PASS |

### Probe Execution

Step 7c: SKIPPED — no `scripts/*/tests/probe-*.sh` convention in this repo; the phase's runnable evidence is the ctest targets above, which were executed directly.

### Requirements Coverage

Live REQUIREMENTS.md on this branch tracks v1.2 and marks all three phase IDs complete (verified: `[x]` at lines 162/176/177 and Complete in the traceability table). No orphaned requirements: ROADMAP maps exactly GATE-02, RECL-01, RECL-02 to Phase 20, all claimed by plans.

| Requirement | Source Plan | Description | Status | Evidence |
| ----------- | ---------- | ----------- | ------ | -------- |
| RECL-01 | 20-01, 20-02, 20-04 | Deleted bytes actually leave the disk — periodic light-node-only CompactRange (kForce) scheduled off the hot path | ✓ SATISFIED | Post-cycle gate on the pruner's own path (off DAG workers, post-cycle, allow_write_stall=false), Light-only factory, kForce one-home; measured 31.7MB reclaimed on 2.13MB logical (gate re-run); conjunct matrix green. "Periodic" realized as D-01's per-cycle byte-threshold schedule (locked decision) |
| RECL-02 | 20-03 | CI gate proves real reclamation — measured SST/du bytes before-vs-after (not key counts) | ✓ SATISFIED | Dedicated full-suite target (ctest #38, TIMEOUT 600) with measured-byte floor/ceiling + du belt + anti-vacuous pins; re-run green this verification |
| GATE-02 | 20-04, 20-05 | Non-transaction data provably untouched — byte-identical across a prune run (ctest) | ✓ SATISFIED | Post-compaction identity suite (strictly stronger than prune-only: identity across a real whole-DB rewrite with size-drop proof) + mesh peer identity across both erase and compaction, two rounds; re-run green. Class-coverage caveat W-1 below |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| src/account/GeniusNode.cpp | 3261 | `//TODO - Make it async...` | ℹ️ Info | Pre-existing (commit 4a72b7e22, 2026-05-20) — not phase debt, unrelated subsystem |
| test/src/account/tx_pruner_test.cpp | 2332, 2457 | `EXPECT_GT( compaction_duration_ms, 0u )` sub-ms flake risk (review IN-02, not fixed) | ⚠️ Warning | Latent CI flake on very fast storage; the compacted flag (also asserted) is the real carrier |
| test/src/account/reclamation_gate_test.cpp | 113-130 | `SumRegularFileBytes` swallows directory-iteration errors (review IN-03, not fixed) | ⚠️ Warning | du belt could pass vacuously on iteration error — the belt is explicitly never the carrier (floor is) |
| src/account/GeniusNode.cpp | 2450-2530 | `ReleaseRuntimeMembersAfterIoStopped` remains dead code containing a now-redundant copy of the cancel | ℹ️ Info | CR-01's core fix landed on the live path; the pre-existing dead function (dead since before phase 20) was neither removed nor wired — cosmetic debt only |

No TBD/FIXME/XXX markers in any phase-modified file. No stub implementations; no placeholder returns on any phase path.

### Review-Fix Verification (20-REVIEW.md cycle)

| Finding | Required Fix | Status | Evidence |
| ------- | ------------ | ------ | -------- |
| CR-01 (critical): stop-path cancel was dead code | Wire into live teardown | ✓ HOLDS IN SOURCE | `ShutdownForDestruction` (:2532, called by `~GeniusNode` :2672) cancels at :2573 before `io_->stop()` (:2629) / joins (:2632) — commit e31fbe6de |
| WR-01: `last_cycle` never carried compacted/duration | Re-copy stats under status mutex | ✓ HOLDS IN SOURCE | TxPruner.cpp:396-403 `status_m.last_cycle = stats` under `status_mutex_` in the success path. Residual W-3: the unmeasured-success path (:376-388) still does not re-copy, and no test pins `status.last_cycle.compacted` |
| WR-02: `<< 20` wrap recreates write-amp DoS | Reject wrapping range at parse | ✓ HOLDS IN SOURCE | PruneEligibility.cpp:118-127, `kMaxCompactThresholdMb = max >> 20`. Residual W-4: no FromJSON test case pins the huge-value rejection |

### Human Verification Required

None. This phase is entirely machine-checked test/infrastructure work; the verifier independently rebuilt and re-ran the core gates (headline gate, trigger matrix, race, mesh+identity, config, smoke) — all green on current source. No visual, UX, or external-service surfaces.

### Warnings and Documented Deviations (non-blocking)

- **W-1 (SC-3 class coverage):** The identity suites prove byte-identity for cert-shaped records, seed-class CRDT values, /utxo/ prefix, heads, seals, blocks, and an unspent sibling. Actual SecureCRDT policy records and processing-result records are not distinctly seeded — they share exactly the proven storage class (non-tx CRDT values outside the `/bc-<netid>/tx/` allowlist), and the erase is prefix-scoped while the rewrite is content-blind, so no per-class divergence mechanism exists. Accepted as satisfying GATE-02's intent.
- **W-2 (SC-5 debug leg):** The roadmap's "verified under debug and release timing" was executed release-only (8x sweep + determinism); no Debug tree exists and configuring one builds vendored RocksDB (deemed disproportionate). The 20-04 PLAN explicitly encoded this conditional and 20-04-SUMMARY recorded the not-run status — an honestly documented deviation, mitigated by the race being deterministic by construction (blocking hook + latch, no timing windows).
- **W-3:** WR-01's fix is not regression-pinned (no `EXPECT` on `GetPruneStatus().last_cycle.compacted`); the unmeasured-compaction edge path does not refresh `last_cycle`.
- **W-4:** WR-02's wrap-guard has no dedicated FromJSON test case (source guard verified by reading).
- **Ceiling recalibration (documented Rule-1 deviation, 20-03):** sanity ceiling `20x + 2MB` replaces the planned `2x + 1MB`, re-derived from measurement (CRDT stores each payload under four key families + block + UTXO records; stable 14.92x measured across runs) — the assertion SHAPE (floor + ceiling + pin + belt) is unchanged. Verified in code with the measured rationale at reclamation_gate_test.cpp:579-591.
- **Seam-anchor correction (documented Rule-1 deviation, 20-04):** the race hold fires at `kInterruptAfterEvaluation` instead of the plan-named `kInterruptBeforeErase` because the landed per-candidate order (re-verify at TxPruner.cpp:201 before the :206 seam) makes the plan's seam unable to produce leg (a). Every asserted outcome is the plan's.

### Gaps Summary

No gaps. All ten merged must-haves verified against source (line-level) and behavior (fresh rebuild + direct re-run of every phase-critical target, all green). The measured headline number was independently reproduced (31,743,124 bytes reclaimed across one production-surface compaction; 1492% of erased logical bytes — the 60% floor passes with 24.9x margin). All three code-review fixes (CR-01, WR-01, WR-02) hold in source. The residual items above are warning/info-level (documented deviations accepted at planning, unpinned test suggestions, and review Info findings), none of which undermine the phase goal. Requirements GATE-02, RECL-01, RECL-02 are satisfied and marked complete in the live REQUIREMENTS.md; no orphaned requirements remain for the milestone.

---

_Verified: 2026-10-24T21:07:15Z_
_Verifier: Claude (gsd-verifier)_
