---
phase: 20-reclamation-proof-e2e-hardening
plan: "04"
subsystem: test
tags: [txpruner, compaction-gate, byte-identity, spend-race, gate-02, sc-5, reclamation]

# Dependency graph
requires:
  - phase: 20-reclamation-proof-e2e-hardening plan 02
    provides: MaybeCompactAfterCycle (the D-01 conjuncts under test) + PruneStatus.bytes_reclaimed + PruneCycleStats.compacted + RequestCompactionCancel
  - phase: 20-reclamation-proof-e2e-hardening plan 01
    provides: GlobalDB::CompactStorage + GetApproximateDiskUsageBytes (the identity suite's rewrite + mutation check)
  - phase: 18-txpruner-service-first-real-prune plan 06
    provides: the RealPrune fixture + the six snapshot helpers (reused verbatim, never forked)
provides:
  - CompactionGate conjunct matrix — every D-01 conjunct machine-checked (below-threshold, erased==0, crossed-once + stamp retention, canceled-before-start + fresh retry, restart honesty) plus D-02 concurrent-read coherence during a compacting cycle
  - RealPrune_PostCompactionEverythingElseStaysByteIdentical — GATE-02/D-C5: the whole-DB rewrite proven non-mutating across a measurably-real compaction (post size < pre size asserted)
  - SpendRace_RevertedSpendSkipsThisCycleAndNextCyclePicksItUp — SC-5/D-04 both legs, deterministic seam-held interleaving, 8x release-sweep stable
affects: [20-05 mesh (the SC-4 full/archive retention legs run on this same file next wave)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "seam-choice-by-code-order: the deterministic hold must sit at the seam that PRECEDES the code under test — the landed RunCycle runs the per-candidate re-verify BEFORE that candidate's kInterruptBeforeErase seam, so the evaluation-to-erase window's entry seam (kInterruptAfterEvaluation) is the only hold from which a spend-state mutation is observable to the re-verify"
    - "two-step fixture revert: durable /utxo/ record rewrite + LoadUTXOs reload — the reload is load-bearing because IsOutPointGenuinelyConsumed reads the in-memory utxo_outpoints_ map, not the record"

key-files:
  created: []
  modified:
    - test/src/account/tx_pruner_test.cpp

key-decisions:
  - "The SC-5 hold fires at kInterruptAfterEvaluation (not the plan-named kInterruptBeforeErase): the landed per-candidate order is re-verify THEN seam, so a kInterruptBeforeErase hold on a single-candidate fixture fires after the only re-verify and the erase would proceed regardless — leg (a) would be unassertable (see deviation 1)"
  - "RevertUtxoRecordToUnspentByKey is assert-free by design: the cycle thread invokes it inside the interrupt hook, so failures report through a returned atomic flag instead of gtest fatal asserts on a worker thread"
  - "The crossed-threshold case proves stamp retention in BOTH forms (erased==0 follow-up AND erasing-but-sub-threshold follow-up) — the strongest stamped-at-compaction semantics proof"
  - "The canceled case adds a persistence pin (a later crossing on the SAME canceled instance still never compacts) because RequestCompactionCancel is never resettable — the retry leg therefore runs on a fresh never-canceled instance whose own crossing cycle compacts"

patterns-established:
  - "Never assert cross-instance bookkeeping: a fresh TxPruner reports no watermark AND no bytes_reclaimed (Pitfall 10 pinned as a TEST_F, not just a comment)"

requirements-completed: [RECL-01, GATE-02]

# Metrics
duration: 20min
completed: 2026-10-24
---

# Phase 20 Plan 04: Reclamation Proof Suites Summary

**The proof layer on the landed service: every D-01 compaction conjunct and the D-02 bytes_reclaimed honesty contract machine-checked conjunct-by-conjunct, GATE-02's non-tx byte-identity proven ACROSS a measurably-real whole-DB compaction (D-C5), and SC-5's window-edge spend race fault-tested deterministically with both legs asserted — 40/40 tests green twice consecutively.**

## Performance

- **Duration:** 20 min
- **Started:** 2026-10-24T18:19:06Z
- **Completed:** 2026-10-24T18:38:37Z
- **Tasks:** 3
- **Files modified:** 1 (test/src/account/tx_pruner_test.cpp only — no overlap with 20-03's files)

## Accomplishments
- **Task 1 (032321c73):** the CompactionGate conjunct matrix — six TEST_Fs, each naming its D-01 conjunct in the header comment, all asserts stats/status-driven (zero log capture): BelowThreshold (real erasing cycle under the default 16MB threshold → compacted=false, nullopt), ErasedZero (drained cycle at a 1MB threshold never compacts), CrossedThreshold (2x~1MB in ONE cycle → compacted=true once, stamp>0, RETAINED across both an erased==0 follow-up and an erasing-but-sub-threshold follow-up), CanceledBeforeStart (cancel before the cycle → never compacts, never stamps, persistence pin on the same instance; fresh never-canceled instance's crossing cycle compacts — the kept-watermark retry), FreshInstance (no stamp, no watermark — Pitfall 10), ConcurrentReadDuringCompactingCycle (reader thread polls GetPruneStatus through a seam-held cycle's erases + tail compaction; >200 coherent full-copy reads, stamp observed mid-poll, no deadlock).
- **Task 2 (560a67886):** RealPrune_PostCompactionEverythingElseStaysByteIdentical — RealPrune's verbatim shape with ~256KB candidates; the production `CompactStorage(nullptr)` runs INSIDE the test between the before/after snapshot pairs; the anti-vacuous mutation check (post < pre whole-DB size) proves the rewrite was real; all six snapshot families byte-identical after it (cert record, /seed/ value, /utxo/ prefix, heads + maxHeight, every seal marker, blocks); unspent sibling byte-identical on every level with marker NOT_FOUND; erased candidates stay value-gone + map-gone + PRUNED; auto-trigger silence pinned (default threshold: compacted=false, bytes_reclaimed nullopt — the rewrite is the test's direct call).
- **Task 3 (99e8d5d9e):** SpendRace_RevertedSpendSkipsThisCycleAndNextCyclePicksItUp — deterministic seam-held interleaving (blocking hook, atomics + 5s deadline that fails rather than hangs, return-false hold): while the cycle is held between evaluation and the per-tx erase, the target's spend state is reverted by the checker-B3 two-step (durable /utxo/ record → READY, then `LoadUTXOs` so the in-memory map the re-verify reads observes it). Leg (a): released cycle refuses at the under-lock re-verify — erased==0, skipped==1, value present, marker NOT_FOUND (asserted mid-hold AND after), skipped-only cycle still records stats. Leg (b): ConsumeOutput re-lands the genuine spend, the next cycle erases — erased==1, PRUNED, value gone. 8 consecutive release runs green.

## Task Commits

Each task was committed atomically:

1. **Task 1: CompactionGate conjunct matrix + bytes_reclaimed honesty suites** - `032321c73` (test)
2. **Task 2: GATE-02 post-compaction byte-identity suite (D-C5)** - `560a67886` (test)
3. **Task 3: SC-5 seam-held window-edge SpendRace suite (D-04, both legs)** - `99e8d5d9e` (test)

## Files Created/Modified
- `test/src/account/tx_pruner_test.cpp` — the three suite families (extend, never fork): 6 matrix TEST_Fs in the CompactionGate section, the identity TEST_F in the RealPrune section, the race TEST_F + the `RevertUtxoRecordToUnspentByKey` fixture helper (beside its `TamperUtxoRecordToConsumed` sibling) in the SpendRace section; the CompactionGate section header updated from "the conjunct matrix is 20-04's scope" to the landed reality.

## Decisions Made
- **The SC-5 hold seam** (see deviation 1 — the plan-level decision): `kInterruptAfterEvaluation`, because it is the only seam from which a spend-state mutation is observable to the re-verify on a single-candidate fixture. The test's header comment documents the code-order reasoning in place so future readers do not re-derive it.
- **Assert-free hook-side helper:** the two-step revert returns a bool through an atomic; gtest fatal asserts never run on the cycle thread.
- **Both retention forms** for the crossed-threshold stamp (the plan asked for the erased==0 form; the erasing-but-sub-threshold form was added as the stronger proof at trivial cost).
- **RECL-01 + GATE-02 marked complete:** the matrix is RECL-01's scheduling-policy breadth (the mechanism landed in 20-02), and the identity suite is GATE-02's entire carrier.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] The plan's SC-5 hold seam (kInterruptBeforeErase) cannot produce leg (a) on the landed code**
- **Found during:** Task 3 design (read of TxPruner.cpp before writing the test)
- **Issue:** The plan (action text + key_links) mandates the hold at `kInterruptBeforeErase` with "the cycle continues into the under-lock re-verify with the mutation visible". The landed RunCycle order is per-candidate: re-verify (`OutputsRemainGenuinelyConsumed`, TxPruner.cpp:201) fires BEFORE the `kInterruptBeforeErase` seam (:206) for the SAME candidate, and `EraseTransactionEntryPruned` contains no second spend check. A single-candidate hold at `kInterruptBeforeErase` therefore fires after the only re-verify — the mutation would be invisible and the erase would proceed (leg (a) unassertable: erased==1, skipped==0). A decoy-first-candidate shape would erase the decoy (erased==1), contradicting the plan's asserted erased==0.
- **Fix:** The hold fires at `kInterruptAfterEvaluation` — the entry seam of the exact evaluation-to-erase window D-04 names ("hold the cycle between evaluation and the per-tx erase"), the same mechanism shape as the plan-cited PruneStatus_ConcurrentReadDuringCycleIsCoherent precedent. Every asserted outcome is exactly the plan's: leg (a) erased==0/skipped==1/value-present/marker-NOT_FOUND, leg (b) erased==1/PRUNED/value-gone, checker-B3 two-step revert, 5s deadline, zero ordering-sleeps, 8x sweep.
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Committed in:** 99e8d5d9e

**2. [Rule 1 - Bug] Concurrent-read case seeded without the trailing driver put**
- **Found during:** Task 1 first run (erased==1, expected 2)
- **Issue:** big_b's delta block stayed the topic's LIVE head, so the compose's head guard refused its erase (a per-tx error) — the canary recipe's retiring driver put had been dropped.
- **Fix:** Added the trailing driver put (the canary's exact recipe); 7/7 green.
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Committed in:** 032321c73

**3. [Rule 3 - Blocking] Race cycle never reached evaluation on the first run**
- **Found during:** Task 3 first run (post-evaluation hold timeout)
- **Issue:** The off-thread poll used the plain `RunCycle` helper, which never drives the manager into the tick gate's READY state — every poll was gate-skipped.
- **Fix:** `ForceManagerReady()` before starting the cycle thread (the RunSettledCycle recipe).
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Committed in:** 99e8d5d9e

---

**Total deviations:** 3 auto-fixed (2x Rule 1, 1x Rule 3)
**Impact on plan:** Deviation 1 is a seam-constant correction forced by the landed code order — every asserted outcome, the mechanism shape, and both legs are the plan's. Deviations 2-3 are ordinary new-test authoring iteration. No scope creep; single declared file touched.

## Verification Evidence
- Task 1: `--gtest_filter='TxPrunerTest.CompactionGate*'` — 7/7 PASSED twice consecutively (10.4s / 10.3s, plus 10.5s post-format); `ctest -R tx_pruner_test` green (53.8s).
- Task 2: `--gtest_filter='TxPrunerTest.RealPrune*'` — 2/2 PASSED twice consecutively (2.4s / 2.4s, identity suite included); `ctest -R tx_pruner_test` green (54.9s).
- Task 3: `--gtest_filter='TxPrunerTest.SpendRace*'` — 8/8 consecutive runs PASSED (~1.1s each); `ctest -R tx_pruner_test` green (55.9s); post-format rerun green.
- Plan end: FULL binary twice consecutively — 40/40 PASSED (56.1s / 55.9s); final `ctest -R tx_pruner_test` green.
- Sleep audit: `grep -n sleep_for` — 7 sites, all pre-existing bounded waits plus the two new bounded ones (the hook's 5s deadline spin; the pre-cycle 10ms settle poll) — no ordering-sleep in the mutation/release interleaving path.
- clang-format (llvm@22 git-clang-format vs each task's base): clean after applying the tool's own fixes (Tasks 1 and 3; Task 2 was clean on first pass). clang-tidy: not applicable (addtest targets disable tidy; test-only plan).
- Post-commit checks: no file deletions in any task commit; no untracked files left behind.

## Timing-Leg Status (SC-5, per plan)
- **Release leg: RUN** — 8 consecutive SpendRace filter runs + full binary twice consecutively, all green, stable timing (~1.1s).
- **Debug leg: NOT RUN** — `build/OSX/Debug` does not exist (verified: only Release under build/OSX); configuring one builds vendored RocksDB, which the plan itself deems disproportionate. The interleaving is deterministic by construction (blocking hook + latch release, no timing windows), so the release determinism carries the proof; recorded here rather than claimed.

## Issues Encountered
None beyond the three deviations above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 20-05 (SC-4 mesh) runs on this same file next wave; the fixture's seam-clear dtor discipline, the seeding recipes, and the compaction-driving pattern (seed -> consume -> settled cycle -> direct CompactStorage through the production surface) are all proven here.

## Known Stubs
None — no placeholder logic landed.

## Threat Flags
None — the plan's register is mitigated as written: T-20-0401 (D-C5 vacuity) by the in-test compaction + mutation check, T-20-0402 (luck-passing race) by the deterministic blocking hook + both-legs stats/state asserts + 8x sweep, T-20-0403 (hung hooks) by the dtor-first clear + clear-on-every-exit pairs + the 5s deadline in every blocking hook, T-20-0404 (unproven conjuncts) by the one-TEST_F-per-conjunct matrix incl. the restart-honesty pin. No security-relevant surface beyond the register (test-only changes).

## Self-Check: PASSED

- test/src/account/tx_pruner_test.cpp exists on disk (git-clean after commits).
- Task commits verified: `032321c73`, `560a67886`, `99e8d5d9e` present on `gsd/phase-20-reclamation-proof-e2e-hardening`.

---
*Phase: 20-reclamation-proof-e2e-hardening*
*Completed: 2026-10-24*
