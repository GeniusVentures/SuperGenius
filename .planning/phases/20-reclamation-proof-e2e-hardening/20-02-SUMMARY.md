---
phase: 20-reclamation-proof-e2e-hardening
plan: "02"
subsystem: transaction
tags: [txpruner, compaction, reclamation, prune-status, rocksdb]

# Dependency graph
requires:
  - phase: 20-reclamation-proof-e2e-hardening plan 01
    provides: GlobalDB::CompactStorage(canceled) + GetApproximateDiskUsageBytes() + PruningConfig.compact_threshold_mb
provides:
  - TxPruner::MaybeCompactAfterCycle — the D-01 post-cycle compaction gate (erased>=1 AND !canceled AND watermark >= threshold; never fails the completed cycle; watermark kept on cancel/failure as the natural retry)
  - PruneStatus.bytes_reclaimed — the MEASURED last-compaction delta, stamped only at compaction success under status_mutex_ (nullopt until the first compaction since restart)
  - PruneCycleStats.compacted + compaction_duration_ms; TxPruner::RequestCompactionCancel (shutdown promptness)
  - Evaluation-carried erased-bytes estimate: PruneCandidateResult.value_bytes + BlockGateResult.block_bytes/sharing_tx_count (no second reads)
  - RECL-03 honest split: erase-time INFO counts-only, compaction completion INFO carries before/after/delta/duration
affects: [20-03 reclamation gate target, 20-04 identity suites + conjunct matrix, 20-05 mesh]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "baseline-at-last-compaction: the size baseline is a member lazily set at the first completed cycle (or after a compaction), so bytes_reclaimed measures the full accumulated span — never a per-cycle under-count"
    - "estimate-carry discipline: sizes ride reads evaluation already performs (value Buffer in hand, block parse in hand) — never surfaced as measured numbers"

key-files:
  created: []
  modified:
    - src/transaction/PruneEligibility.hpp
    - src/transaction/PruneEligibility.cpp
    - src/transaction/TxPruner.hpp
    - src/transaction/TxPruner.cpp
    - src/account/GeniusNode.cpp
    - test/src/account/tx_pruner_test.cpp

key-decisions:
  - "BlockGateResult.block_bytes counts the block's carried KEYS (elements + tombstones) from the parse the gate already performed — element VALUES stay per-candidate (value_bytes) and are not double-counted; no public raw-block-size getter exists above the wrappers and adding one would exceed the plan's surface"
  - "Post-compaction size-read failure (compaction ran, measurement failed): baseline KEPT so the next compaction measures the full span, bytes_reclaimed not stamped (never fabricated), watermark reset (the span was processed), stats.compacted=true with a WARN stating it plainly"
  - "status_m.last_cycle is copied at RecordCompletedCycle BEFORE the gate runs, so last_cycle.compacted stays false while bytes_reclaimed is stamped separately — per the plan's hook-point placement (between RecordCompletedCycle and return)"
  - "The canary sizes values via destination-address padding (one ~1MB low-entropy output per tx) — output-count scaling would be quadratic under PutUTXO's per-call address snapshot"

patterns-established:
  - "Member-scope cancellation atomic: the engine's compaction iterator reads it asynchronously, so it must outlive the in-flight CompactRange (a stack local is the Pitfall-4 bug)"

requirements-completed: [RECL-01]

# Metrics
duration: 23min
completed: 2026-10-24
---

# Phase 20 Plan 02: TxPruner Compaction Gate Summary

**D-01 post-cycle compaction gate with the honest-split reporting: threshold-crossed cycles compact through GlobalDB::CompactStorage on the pruner's own tail, PruneStatus.bytes_reclaimed stamps the measured delta at compaction only, and the shutdown path cancels promptly — proven end-to-end by a 1MB-threshold canary.**

## Performance

- **Duration:** 23 min
- **Started:** 2026-10-24T17:16:45Z
- **Completed:** 2026-10-24T17:39:57Z
- **Tasks:** 3
- **Files modified:** 6

## Accomplishments
- **Task 1 (1f2c568a2):** evaluation-carried estimate inputs — `PruneCandidateResult.value_bytes` (stored-value size from the enumeration read), `BlockGateResult.block_bytes` (carried-key total from the block parse) + `sharing_tx_count` (share divisor from the report in hand); the already-erased branch now reuses the counted divisor instead of a duplicate `any_of` predicate. Zero extra reads (grep: GetLocalDeltaKeys count unchanged at 1, GetLocalDeltaKeyValues at 0), zero behavior change, all suites green.
- **Task 2 (82cfea568):** `MaybeCompactAfterCycle` in the RunCycle tail between `RecordCompletedCycle` and `return stats` — D-01 conjuncts in order (erased>=1, !canceled, watermark >= `compact_threshold_mb << 20` with MB the single config representation); baseline-at-last-compaction lazy init (Open-Q-2 resolution encoded); cancel-skip is one INFO, other failures WARN with the watermark KEPT (the still-crossed threshold is the retry trigger); success stamps `bytes_reclaimed` under `status_mutex_`, resets watermark + baseline, sets `compacted`/`compaction_duration_ms`, emits ONE INFO with before/after/delta/duration/generation. RECL-03 clause retired — the erase-time INFO is counts-only and all "Phase 20" tokens are gone from src/transaction (grep gate: 0). `RequestCompactionCancel` sets the MEMBER atomic. Default 16MB threshold = zero compactions on existing fixtures: tx_pruner_test green TWICE consecutively (the no-behavior-change regression proof).
- **Task 3 (749b92207):** GeniusNode stop path calls `RequestCompactionCancel` (exactly one call site, guarded, before `tx_pruner_.reset()` — the io drain is the join). The `CompactionGate_ThresholdCrossed` canary: two ~1MB spent-beyond-window values cross a 1MB threshold inside ONE settled cycle → erased==2, compacted==true, duration recorded, `bytes_reclaimed` has_value > 0, erased values PRUNED (non-vacuous) — green twice consecutively (1.2-1.3s each). MakePruner/MakeTransfer/ConsumeOutput gained backward-compatible trailing parameters (no existing call site changed); the Observability suite now also pins the honest nullopt under the default threshold.

## Task Commits

Each task was committed atomically:

1. **Task 1: Evaluation-carried erased-bytes estimate (PruneEligibility size fields)** - `1f2c568a2` (feat)
2. **Task 2: TxPruner compaction gate + PruneStatus.bytes_reclaimed + honest-split reporting** - `82cfea568` (feat)
3. **Task 3: GeniusNode stop-path cancel + the threshold-crossed canary test** - `749b92207` (feat)

## Files Created/Modified
- `src/transaction/PruneEligibility.hpp` — value_bytes / block_bytes / sharing_tx_count fields with estimate-only Doxygen (research A5)
- `src/transaction/PruneEligibility.cpp` — size carry at the two existing read points; divisor reuse in the already-erased branch; "Phase 20" token retired from the knob comment
- `src/transaction/TxPruner.hpp` — PruneCycleStats.compacted/compaction_duration_ms, PruneStatus.bytes_reclaimed, RequestCompactionCancel, MaybeCompactAfterCycle, the three compaction members (watermark/baseline/member atomic); retiring Doxygen hooks replaced with the real contract
- `src/transaction/TxPruner.cpp` — share map + watermark accumulation on successful erases, the gate call in the tail, MaybeCompactAfterCycle body, RequestCompactionCancel, counts-only RECL-03 INFO
- `src/account/GeniusNode.cpp` — the one-line stop-path cancel with the WHY comment
- `test/src/account/tx_pruner_test.cpp` — the canary, helper extensions, honest-split pins, refreshed header/observability docs

## Decisions Made
- **block_bytes = carried keys only** (see frontmatter): values ride value_bytes; no double count; no new GlobalDB surface (the plan's "no extra Gets" constraint makes the parse the only honest size source).
- **Unmeasured-success edge**: compaction succeeded but post-read failed → keep baseline, stamp nothing, reset watermark, compacted=true + WARN (Rule 2 error handling; documented, never fabricated).
- **status_m.last_cycle.compacted stays false** (copied pre-gate per the plan's mandated hook position); the per-cycle compaction observable is the RETURNED stats + the stamped bytes_reclaimed.
- **RECL-01 marked complete** in this plan's requirements-completed: the scheduled trigger (this plan), the substrate (20-01), and Light-only enforcement (factory gate + TickGate test) are all landed; 20-04 carries the matrix breadth, not the mechanism.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Task 2's grep gate required retiring "Phase 20" tokens outside Task 2's file list**
- **Found during:** Task 2 verification
- **Issue:** The checker-fixed gate `grep -rn "Phase 20" src/transaction/ | wc -l` == 0 also matches two pre-existing comments in PruneEligibility.{hpp,cpp} ("(Phase 20 D-01)", "Phase 20 compaction threshold knob") — files Task 2 did not declare.
- **Fix:** Reworded both comments (D-01 reference retained; no semantic change) inside Task 2's commit so the gate passes as specified.
- **Files modified:** src/transaction/PruneEligibility.hpp, src/transaction/PruneEligibility.cpp
- **Committed in:** 82cfea568

**2. [Rule 1 - Bug] Stale test-file docs claimed PruneStatus carries NO bytes field**
- **Found during:** Task 3
- **Issue:** The tx_pruner_test.cpp file header and the Observability test doc asserted the pre-20-02 world ("deliberately NO bytes-reclaimed field"), now false after Task 2.
- **Fix:** Rewrote both docs to the honest-split contract and extended the Observability test with the default-threshold pins (compacted=false, compaction_duration_ms=0, bytes_reclaimed nullopt) so the suite keeps proving the honest state.
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Committed in:** 749b92207

---

**Total deviations:** 2 auto-fixed (1× Rule 3, 1× Rule 1)
**Impact on plan:** Both were plan-mandated consistency fixes inside the phase's declared file set. No scope creep.

## Verification Evidence
- `cmake --build build/OSX/Release --target prune_eligibility_test tx_pruner_test` — clean (Task 1); `ctest -R "prune_eligibility|tx_pruner_test"` — 2/2 passed.
- Task 2: `ctest -R "tx_pruner_test|tx_lookup_tri_state|prune_eligibility"` — 3/3 passed; tx_pruner_test green a SECOND consecutive time (44.3s); `grep -rn "Phase 20" src/transaction/ | wc -l` → 0; `grep -rn "not measured until Phase 20" src/` → 0; `grep -n "MaybeCompactAfterCycle" TxPruner.cpp` → call (:253) + definition (:313); compaction_canceled_ declared in the TxPruner.hpp members region (:414).
- Task 3: `genius_node` + `tx_pruner_test` targets built clean; `--gtest_filter='TxPrunerTest.CompactionGate*'` passed TWICE consecutively (1312ms / 1238ms, plus 1249ms post-format); full `ctest -R tx_pruner_test` green (44.6s); `grep -n "RequestCompactionCancel" src/account/GeniusNode.cpp` → exactly one call site (:2472).
- clang-format (llvm@22 git-clang-format vs each task's HEAD) — clean on all 6 files after applying the tool's own fixes.
- clang-tidy (llvm@22, single-arch-filtered compile db): TxPruner.cpp — ZERO findings in the whole file; PruneEligibility.cpp — zero findings on any new line (remaining warnings are the pre-existing naming/pro-bounds classes on sibling code); GeniusNode.cpp — zero findings on the new lines (the two findings at :2490 are the pre-existing migration_mutex_ lock_guard block, out of scope). Test target: tidy not applicable (addtest disables it).
- No file deletions in any task commit; no untracked files left behind.

## Issues Encountered
None beyond the two deviations above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 20-03 (reclamation gate target) can assert on the production pair end-to-end: `CompactStorage` + `GetApproximateDiskUsageBytes` + the gate's stamped `bytes_reclaimed`; the canary is the shape reference.
- 20-04's conjunct matrix has every observable it needs: stats.compacted/compaction_duration_ms, status.bytes_reclaimed, the kept-watermark retry behavior, and the cancel-skip INFO path.

## Known Stubs
None — no placeholder logic landed.

## Threat Flags
None — no security-relevant surface beyond the plan's threat model (T-20-0201..0204 all mitigated as planned; the estimate drift T-20-0204 stays accept-documented on the fields).

## Self-Check: PASSED

- All 6 modified files exist on disk (git-clean after commits).
- Task commits verified: `1f2c568a2`, `82cfea568`, `749b92207` present on `gsd/phase-20-reclamation-proof-e2e-hardening`.

---
*Phase: 20-reclamation-proof-e2e-hardening*
*Completed: 2026-10-24*
