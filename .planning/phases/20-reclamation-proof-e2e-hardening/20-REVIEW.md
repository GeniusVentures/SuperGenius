---
phase: 20-reclamation-proof-e2e-hardening
reviewed: 2026-10-26T12:16:43Z
depth: standard
files_reviewed: 11
files_reviewed_list:
  - src/account/GeniusNode.cpp
  - src/crdt/globaldb/globaldb.cpp
  - src/crdt/globaldb/globaldb.hpp
  - src/transaction/PruneEligibility.cpp
  - src/transaction/PruneEligibility.hpp
  - src/transaction/TxPruner.cpp
  - src/transaction/TxPruner.hpp
  - test/src/account/prune_eligibility_test.cpp
  - test/src/account/reclamation_gate_test.cpp
  - test/src/account/tx_pruner_test.cpp
  - test/src/crdt/globaldb_erase_test.cpp
findings:
  critical: 0
  warning: 3
  info: 6
  total: 9
status: issues_found
---

# Phase 20: Code Review Report

**Reviewed:** 2026-10-26T12:16:43Z
**Depth:** standard
**Files Reviewed:** 11
**Status:** issues_found

> **AUDIT TRAIL — POST-FIX RE-REVIEW for PR #401.** This report OVERWRITES the
> 2026-10-24 execution-cycle review (which found CR-01, WR-01, WR-02, 4 infos).
> Diff base `40d423594^..HEAD`. Scope of this pass: verify the three fixes hold
> in the live tree, then adversarially re-scan the compaction surface
> (cancellation correctness incl. live-path wiring, sentinel lifetime,
> single-homing), the measurement's honesty, the threshold validation, and the
> test suites' non-vacuity.

## Summary

The three execution-cycle fixes are verified present and substantively correct
(no blockers this pass), but each lands with residual gaps: the WR-01 status
re-copy is incomplete (a reachable branch still reports a compacted cycle as
un-compacted), the CR-01 live wiring was added *beside* the dead path instead
of replacing it (the dead shutdown function was extended with a
"verified ordering" comment for ordering that never executes), and none of the
three fixes has a regression test pinning it — contrary to the project's
"gates, not promises" doctrine. The compaction substrate itself is sound:
the recipe (`CompactRange` + `kForce` + cancellation pointer) is single-homed
in `GlobalDB::CompactStorage`, the size sentinel is lifetime-correct, the
`<< 20` threshold derivation has exactly one site and the wrap guard at the
config reader is arithmetically correct, and the measured-vs-estimated byte
split is honest (estimates feed only the trigger; the stamped number is always
a real before/after disk delta). The test suites are genuinely non-vacuous
(mutation checks, auto-trigger-silence asserts, anti-blindness pins on the
`before` measurement), and the new `reclamation_gate_test` is registered in
CMake/ctest.

### Prior-cycle fix verification

- **CR-01 (dead-path cancel → live wiring): VERIFIED.**
  `GeniusNode.cpp:2566-2574` now calls `tx_pruner_->RequestCompactionCancel()`
  inside `ShutdownForDestruction` — the only live shutdown route (the only
  `io_->stop()`/join site, `GeniusNode.cpp:2626-2646`) — after the prune-timer
  cancel and **before** the io drain. The wiring is sound beyond the obvious
  ordering: the atomic is passed **by address** into `CompactStorage`, so a
  compaction that starts between the gate-2 check and the cancel is still
  aborted mid-flight by the engine, and a pre-start cancel surfaces as
  `IsManualCompactionPaused` → `operation_canceled`, handled as the clean
  skip. The `TxPruner` instance outlives any in-flight `RunCycle` because the
  io threads are joined before member destruction. Residual issue: see WR-02
  below (the dead path was kept and extended).
- **WR-01 (last_cycle re-copy): VERIFIED PRESENT, but incomplete.**
  `TxPruner.cpp:397-403` re-copies `stats` into `status_m.last_cycle` after
  stamping `compacted`/`bytes_reclaimed` — on the success path only. See WR-01
  below for the un-re-copied branch.
- **WR-02 (threshold wrap guard): VERIFIED CORRECT.**
  `PruneEligibility.cpp:114-128` rejects `compact_threshold_mb >
  (2^64-1) >> 20`, so the single derivation site
  (`TxPruner.cpp:328`, `compact_threshold_mb << 20`) cannot wrap; the guard
  and the shift are mutually consistent and both are one-home. `FromJSON`
  remains the only production config entry. Not regression-tested — see WR-03.

## Warnings

### WR-01: `MaybeCompactAfterCycle` still desyncs `status.last_cycle` from the cycle's real outcome on non-success paths

**File:** `src/transaction/TxPruner.cpp:353-389`
**Issue:** `RecordCompletedCycle` copies `stats` into `status_m.last_cycle`
*before* the compaction tail runs; the WR-01 fix re-copies only on the
success path (`:397-403`). In the "compaction ran but the size re-read
failed" branch (`:376-389`), `stats.compacted = true` is set and returned to
the caller, but the queryable status keeps `last_cycle.compacted == false`
and `last_cycle.compaction_duration_ms == 0` — the exact stale-field class
the prior WR-01 fixed, in the opposite direction: a gRPC `GetPruneStatus`
reader is told a compaction never ran when it did. The canceled and
`ROCKSDB_IO`-failure branches have the mirrored, milder form
(`compaction_duration_ms` is stamped on the returned stats at `:355` but
never reaches the status copy).
**Fix:** re-copy under `status_mutex_` in every post-`CompactStorage` branch
that mutates `stats`, or move the single `status_m.last_cycle = stats` copy
to after `MaybeCompactAfterCycle` returns in `RunCycle` (one copy, always
final):
```cpp
// RunCycle tail — single, final status sync:
RecordCompletedCycle( globaldb, stats, cycle_start ); // watermark + INFO only
MaybeCompactAfterCycle( globaldb, stats );
{
    const std::scoped_lock lock( status_mutex_ );
    status_m.last_cycle = stats;
}
```
(If the INFO line must print inside `RecordCompletedCycle`, keep the copy
there but re-sync in the `after.has_error()` branch of
`MaybeCompactAfterCycle` exactly as the success path already does.)

### WR-02: the dead shutdown path was extended instead of deleted — two documented cancel orderings, one of which never runs

**File:** `src/account/GeniusNode.cpp:2450-2530` (and `GeniusNode.hpp:1573-1578`)
**Issue:** `ReleaseRuntimeMembersAfterIoStopped` has **no caller anywhere in
the repo** (repo-wide grep: definition, declaration, and comments only —
including its own `tx_pruner_.reset()` / `tx_globaldb_.reset()` / member
teardown, which therefore never execute; members are released only by
implicit destruction). This phase *added* a `RequestCompactionCancel` block
plus a "the drain is the join; verified ordering" justification comment
(`:2466-2473`) **inside that dead function**, while the live CR-01 fix landed
in `ShutdownForDestruction`, whose comment itself states the dead function
"had no caller". Result: shutdown-critical code now documents the cancel
ordering in two places, one of which is unreachable, and the header still
advertises the dead function as "Releases the runtime object graph after all
node I/O threads have stopped". A future maintainer "fixing" shutdown by
wiring or copying from the dead path inherits comments describing ordering
that has never been exercised.
**Fix:** delete `ReleaseRuntimeMembersAfterIoStopped` (cpp + hpp declaration)
and its now-orphaned comments, leaving `ShutdownForDestruction` as the single
documented home of the cancel-before-drain statement. If the function is
intentionally retained for a planned restart API, say so explicitly in the
header instead of the current unconditional contract wording.

### WR-03: none of the three execution-cycle fixes has a regression gate

**File:** `test/src/account/tx_pruner_test.cpp`, `test/src/account/prune_eligibility_test.cpp`
**Issue:** The project's own bar is "a rule that matters is enforced by
ctest". Verified gaps:
- **WR-01 fix (status re-copy):** grep shows no test asserts
  `status.last_cycle.compacted` / `status.last_cycle.compaction_duration_ms`
  after a compacting cycle — `CompactionGate_ThresholdCrossed…` and
  `CompactionGate_CrossedThreshold…` assert only `stats.compacted` and
  `status.bytes_reclaimed`. Removing the re-copy at `TxPruner.cpp:402` today
  keeps every test green.
- **WR-02 fix (wrap guard):** the new config cases C11–C14
  (`prune_eligibility_test.cpp:774-806`) cover absent/explicit/zero/ill-typed
  but not the wrapping range the fix rejects — a `compact_threshold_mb` of
  `2^44` (accepted) and `2^44 + 1` (rejected → default 16) boundary case is a
  three-line `ParseConfig` test.
- **CR-01 fix (live wiring):** no test exercises `ShutdownForDestruction`
  cancelling an in-flight compaction (acknowledged harder — the fixture
  family deliberately avoids `GeniusNode`; the service-level cancel is
  well-covered by `CompactionGate_CanceledBeforeStart…`).
**Fix:** add the two cheap pins (boundary `ParseConfig` case; one
`EXPECT_TRUE( status.last_cycle.compacted )` +
`EXPECT_EQ( status.last_cycle.compaction_duration_ms, stats.compaction_duration_ms )`
in an existing compacting test). For the wiring, at minimum a comment in the
test suite naming CR-01 as uncovered-by-construction so the gap is not silent.

## Info

### IN-01: `PruneCycleStats::generation_superseded` can never be true in production

**File:** `src/transaction/TxPruner.hpp:104-110`, `src/account/GeniusNode.cpp:5128-5168`
**Issue:** The header contract says the wiring arm "checks before invoking the
cycle **and tags its own reporting**". `RunPruneCycle` performs the pre-cycle
supersession check (`:5133`) but never sets `generation_superseded` on the
returned stats and never re-checks after `RunCycle` returns, so the field —
surfaced through `GeniusNode::GetPruneStatus()` → gRPC — is permanently false,
including for cycles whose generation was superseded mid-flight (the window
the pre-check cannot close). The dry-run arm computes the equivalent flag for
its log clause (`:5070`), so the shape exists to copy.
**Fix:** after a successful `RunCycle`, compare
`SnapshotAccountServices().generation != snapshot.generation` and tag the
returned stats before reporting; or drop the field from `PruneCycleStats` so
the status cannot imply a detection that never happens.

### IN-02: `kInterruptAfterStatus` abort does not skip the compaction tail

**File:** `src/transaction/TxPruner.cpp:246-254, 291-294`
**Issue:** A step-4 hook abort returns early only from the INFO-line
remainder of `RecordCompletedCycle`; `RunCycle` then falls through into
`MaybeCompactAfterCycle` unconditionally. The seam doc ("a @c true return
aborts the cycle AT that point") over-promises. Benign today — the
crash-matrix fixture that arms step 4 (`tx_pruner_test.cpp:3770`, expects
`cycle_completes=true`) uses the default 16MB threshold so no compaction can
fire — but a future threshold-crossing fixture arming step 4 would silently
trigger a real whole-DB `kForce` compaction the abort promised to prevent.
**Fix:** either gate `MaybeCompactAfterCycle` behind a non-aborted status
(i.e. have `RecordCompletedCycle`'s step-4 abort propagate a skip to the
tail), or narrow the seam doc to "aborts the INFO summary only; the
compaction tail still runs".

### IN-03: `std::numeric_limits` used without including `<limits>`

**File:** `src/transaction/PruneEligibility.cpp:122`
**Issue:** `kMaxCompactThresholdMb` (the WR-02 guard) relies on
`std::numeric_limits`, but the TU's include list (`:12-19`) has no `<limits>`
— it compiles only through transitive includes, which is exactly the
fragility the project's include discipline forbids.
**Fix:** add `#include <limits>` to the block with `<set>`.

### IN-04: sub-millisecond compaction would fail `EXPECT_GT(duration_ms, 0u)` on truncation

**File:** `test/src/account/tx_pruner_test.cpp:2332, 2457`
**Issue:** `compaction_duration_ms` comes from
`duration_cast<milliseconds>` (truncating). On a fast/tmpfs CI disk a ~2MB
memtable flush + rewrite could genuinely complete in under 1 ms, truncating
to 0 and failing these two asserts spuriously. The rest of the gate asserts
(`compacted`, `bytes_reclaimed > 0`) are robust.
**Fix:** assert the *mechanism* rather than wall time — e.g.
`EXPECT_TRUE(stats.compacted)` only, or record the duration in
microseconds/microsecond-resolution before comparing.

### IN-05: `reclamation_gate_test.cpp` carries a verbatim copy of the tx_pruner_test family scaffolding

**File:** `test/src/account/reclamation_gate_test.cpp:203-360`
**Issue:** `MakeTransfer`/`SeedTransaction`/`PutSeedValue`/`TrackOutputs`/
`ConsumeOutput`/`MakePruner`/`RunSettledCycle` are near-verbatim copies from
`tx_pruner_test.cpp` (the file header owns the decision: "owns its own copy
of the family helpers and includes no sibling test"). The hard invariant
names repeated test scaffolding as duplication to consolidate; a third
consumer (or a fixture-recipe fix, e.g. the `padded_owner` consume shape
already needed in both) now has two divergent homes to edit.
**Fix:** when the next suite in this family appears, lift the helpers into a
shared `testutil` fixture header in the same move (not worth it for two
copies alone, per the no-coincidental-abstraction rule — but the trigger is
met on the third).

### IN-06: whole-DB `kForce` compaction runs synchronously on an io-pool thread

**File:** `src/transaction/TxPruner.cpp:354`, `src/account/GeniusNode.cpp:4963-4981`
**Issue:** `MaybeCompactAfterCycle` executes inside the asio prune-tick
handler, so a large store's whole-DB bottommost compaction pins one io thread
for its full duration; only the shutdown cancel can cut it short, and no
admission control (size cap, off-thread execution) exists. Perf/liveness is
out of v1 review scope and the cadence (10-minute timer, ≥16MB threshold)
bounds the frequency — recorded so the accepted trade-off is not silent.
**Fix (if ever needed):** run the compaction on a detached worker with the
same cancel atomic and have `RunCycle` treat it as fire-and-forget; not
required for this phase.

## Structural Findings (fallow)

None provided by the workflow for this pass (`<structural_findings>` block
absent). The single-home claims in this report were verified by direct grep:
`CompactRange` appears only in `src/crdt/globaldb/globaldb.cpp`; the
`compact_threshold_mb << 20` derivation only in `src/transaction/TxPruner.cpp:328`;
`TxPruner::New` / `CompactStorage` / `GetApproximateDiskUsageBytes` have no
production callers outside the intended wiring.

---

_Reviewed: 2026-10-26T12:16:43Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
_Re-review basis: diff 40d423594^..HEAD (post-fix state for PR #401; supersedes the 2026-10-24 report formerly at this path)_
