---
phase: 20-reclamation-proof-e2e-hardening
reviewed: 2026-10-24T20:21:27Z
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
  critical: 1
  warning: 2
  info: 4
  total: 7
status: issues_found
---

# Phase 20: Code Review Report

**Reviewed:** 2026-10-24T20:21:27Z
**Depth:** standard
**Files Reviewed:** 11
**Status:** issues_found

## Summary

The Phase 20 diff (40d423594^..HEAD) was reviewed at standard depth: the two new
GlobalDB surfaces (`CompactStorage`, `GetApproximateDiskUsageBytes`), the
`compact_threshold_mb` knob, the evaluator-carried estimate fields, the
TxPruner post-cycle compaction gate, the GeniusNode stop-path one-liner, and the
four test files (reclamation gate, trigger matrix, post-compaction identity,
seam-held race, mesh retention).

The engine-facing recipe itself is sound and verified against the vendored
RocksDB (`thirdparty/rocksdb`): pre-set `canceled` returns
`Status::Incomplete(kManualCompactionPaused)` (db_impl_compaction_flush.cc:973),
so the `IsManualCompactionPaused` → `std::errc::operation_canceled` mapping is
correct; `allow_write_stall=false` and `exclusive_manual_compaction=false` are
the safe defaults the comments claim; the 16×0xFF sentinel with
`INCLUDE_FILES | INCLUDE_MEMTABLES` spans every real key class in a
single-column-family store; the sentinel string provably outlives the borrowed
`Slice`. The measurement honesty split (estimate at erase time, measured delta
at compaction time) is consistently implemented, and `block_share_by_cid.at()`
in TxPruner.cpp:229 is provably non-throwing (both maps are keyed identically
from erasable reports, and groups are built only from `erasable_cids`).

The dominant defect is that the phase's shutdown-cancellation deliverable is
**dead code**: the stop-path one-liner sits inside a function that has no caller
anywhere in the tree, so the only production trigger of
`TxPruner::RequestCompactionCancel` never executes. Two further contract
defects: the queryable `PruneStatus::last_cycle` can never report the new
compaction fields (stale copy order), and the MB→bytes derivation wraps for
large-but-valid config values, silently defeating the write-amp guard the knob
validation claims to enforce.

## Critical Issues

### CR-01: Stop-path compaction cancellation is dead code — `ReleaseRuntimeMembersAfterIoStopped` has no caller

**File:** `src/account/GeniusNode.cpp:2466-2473` (function at `:2450`; live path `:2560-2641`)
**Issue:** The phase-20 stop-path deliverable

```cpp
if ( tx_pruner_ )
{
    // The io pool is already drained here, so an in-flight
    // compaction has RETURNED ...
    tx_pruner_->RequestCompactionCancel();
}
```

was added inside `GeniusNode::ReleaseRuntimeMembersAfterIoStopped()`. That
function is **never invoked** — a whole-tree grep (src, test, gRPCForSuperGenius,
evmrelay) finds only its declaration (GeniusNode.hpp:1578), its definition, and
a stale comment referencing it; git history shows it was re-added without a call
site in c0a665226 and has stayed dead since. Consequently the only production
call site of `TxPruner::RequestCompactionCancel()` is unreachable, and this
review's named availability surface is unprotected: the live shutdown path
(`ShutdownForDestruction`) cancels the prune timer, calls `io_->stop()`, and
joins the io threads (GeniusNode.cpp:2616-2636) **without ever setting the
cancel atomic**. A whole-DB `CompactRange` (`kForce`, no write stall, no
timeout) in flight when shutdown begins blocks the thread join for the
compaction's full duration with no way to abort it — exactly the promptness
problem the one-liner was supposed to mitigate. The comment's "verified
ordering" claim is unverifiable because the enclosing function never runs.
The stale comment at GeniusNode.cpp:5004-5008 ("tx_globaldb_ is reset only in
ReleaseRuntimeMembersAfterIoStopped(), after the io pool ... drained") repeats
the same fiction and should be corrected alongside.

Note there is no use-after-free here (the join precedes
`tx_globaldb_->ShutdownNow()` at :2640), so the impact is shutdown latency on
the compaction path plus an inert, misleading deliverable — but on the surface
the review was explicitly asked to scrutinize (cancellation correctness), the
shipped behavior contradicts the shipped code's own claims.

**Fix:** Wire the cancel into the live teardown and delete or wire the dead
function:

```cpp
// In GeniusNode::ShutdownForDestruction(), beside the prune-timer cancel
// (~line 2560), BEFORE io_->stop()/thread join so an in-flight CompactRange
// aborts promptly and a not-yet-started one skips cleanly:
if ( prune_dry_run_timer_ )
{
    boost::system::error_code ignored;
    prune_dry_run_timer_->cancel( ignored );
}
if ( tx_pruner_ )
{
    tx_pruner_->RequestCompactionCancel();
}
```

Either give `ReleaseRuntimeMembersAfterIoStopped` a real caller in the shutdown
sequence or remove it entirely (its body duplicates teardown that member
destruction already performs); also fix the :5004 comment to name the actual
release point. Add a grep-able assertion (test or CI check) that
`RequestCompactionCancel` has a reachable production call site, per the
project's "gates, not promises" rule.

## Warnings

### WR-01: `PruneStatus::last_cycle` never carries `compacted` / `compaction_duration_ms` — the status copy is taken before the compaction gate runs

**File:** `src/transaction/TxPruner.cpp:252-253, 279, 355, 386, 401`
**Issue:** `RunCycle` calls `RecordCompletedCycle( globaldb, stats, cycle_start )`
and only then `MaybeCompactAfterCycle( globaldb, stats )`. `RecordCompletedCycle`
copies the stats into the queryable status at TxPruner.cpp:279
(`status_m.last_cycle = stats;`) while `stats.compacted` is still `false` and
`stats.compaction_duration_ms` is still `0`. `MaybeCompactAfterCycle` then
stamps those fields on the local `stats` (and `status_m.bytes_reclaimed`
separately, under the mutex) but never refreshes `status_m.last_cycle`. Result:
`TxPruner::GetPruneStatus()` — surfaced through `GeniusNode::GetPruneStatus()`
(GeniusNode.cpp:4290) — reports `last_cycle.compacted == false` and
`last_cycle.compaction_duration_ms == 0` forever, even on cycles whose returned
stats say `compacted == true`. The header contract
("last_cycle: Counters of the last cycle that updated the status") is violated,
and no test covers the gap (the compaction suites assert the returned
`stats.compacted` and `status.bytes_reclaimed`, never
`status.last_cycle.compacted`).
**Fix:** In `MaybeCompactAfterCycle`, after stamping `stats`, refresh the status
copy under the same mutex in both the success and the unmeasured-compaction
paths:

```cpp
{
    const std::scoped_lock lock( status_mutex_ );
    status_m.bytes_reclaimed = reclaimed;
    status_m.last_cycle      = stats; // re-copy: compacted + duration landed after RecordCompletedCycle
}
```

(and add `EXPECT_TRUE( pruner->GetPruneStatus().last_cycle.compacted )` to the
`CompactionGate_ThresholdCrossed...` test).

### WR-02: `compact_threshold_mb << 20` wraps for validly-parsed huge values, silently recreating the write-amp DoS the knob rejects

**File:** `src/transaction/TxPruner.cpp:328` (derivation) and `src/transaction/PruneEligibility.cpp:114-118` (validation)
**Issue:** `FromJSON` accepts any positive `uint64` (`IsUint64() && > 0`), and
the single derivation site computes `config_m.compact_threshold_mb << 20`.
Unsigned shift is modulo 2^64: any value ≥ 2^44 (≈17.6e12 MB) silently wraps —
`compact_threshold_mb: 17592186044416` yields `threshold_bytes == 0`, so
`cumulative_erased_bytes_ < 0` is false and **every** erasing cycle triggers a
whole-DB compaction. That is exactly the "compact after every cycle — write-amp
denial of service on self (V5)" condition the read-time validation documents
itself as preventing (a degenerate 0 is rejected at parse, but the derived 0 is
not). The `pruning.compact_threshold_mb` knob is one of the review's named
security-sensitive validation surfaces; an operator-supplied absurd-but-JSON-valid
value defeats the guard with no log line.
**Fix:** Reject (or clamp with a WARN) at parse time in `PruningConfig::FromJSON`,
next to the existing zero check:

```cpp
// A value whose byte derivation would overflow silently wraps into a tiny
// threshold — the V5 write-amp case the zero-rejection guards. Fail the read.
constexpr uint64_t kMaxCompactThresholdMb = UINT64_MAX >> 20;
if ( pruning_object.HasMember( "compact_threshold_mb" ) && pruning_object["compact_threshold_mb"].IsUint64() )
{
    const uint64_t value = pruning_object["compact_threshold_mb"].GetUint64();
    if ( value > 0 && value <= kMaxCompactThresholdMb )
    {
        config.compact_threshold_mb = value;
    }
    else if ( value > kMaxCompactThresholdMb )
    {
        // log/record like the degenerate-0 case; keep the default
    }
}
```

(Keep one home for the bound: either the constant lives beside the shift in
TxPruner or the derivation exposes a checked helper — do not duplicate the
magic number.)

## Info

### IN-01: `operation_canceled` conflates pre-set cancel, mid-flight abort, and engine-side manual-compaction pause

**File:** `src/transaction/TxPruner.cpp:356-362`; `src/crdt/globaldb/globaldb.cpp:943-948`
**Issue:** `CompactStorage` maps every `IsManualCompactionPaused` status to
`std::errc::operation_canceled`, and TxPruner logs it as "skipped: canceled …
nothing was started". In the vendored engine that subcode is also returned for
mid-flight cancellations (partial work done) and for
`manual_compaction_paused_ > 0` raised by `DisableManualCompaction`
(db_impl_compaction_flush.cc:967-976, 2093, 2210) — cases where the "nothing
was started" wording is wrong. Behaviorally harmless (idempotent compaction,
watermark kept), but the log misleads an operator diagnosing shutdown latency.
**Fix:** Reword to "canceled or paused before completion", or distinguish by
checking `compaction_canceled_.load()` at the call site before logging.

### IN-02: `EXPECT_GT( stats.compaction_duration_ms, 0u )` can flake on fast storage

**File:** `test/src/account/tx_pruner_test.cpp:2332, 2457`
**Issue:** `ElapsedMs` truncates `steady_clock` to whole milliseconds; a
compaction that completes in under 1 ms reports 0 and fails the strict `>`.
The compaction suites are otherwise stats/status-driven and deterministic; this
one assertion adds storage-speed sensitivity to two of them.
**Fix:** `EXPECT_GE( stats.compaction_duration_ms, 0u )` is vacuous — assert
only `EXPECT_TRUE( stats.compacted )` (already present) and drop the duration
bound, or use a microsecond count if a lower bound is genuinely wanted.

### IN-03: `SumRegularFileBytes` swallows directory-iteration errors, letting the du belt pass vacuously

**File:** `test/src/account/reclamation_gate_test.cpp:113-130`
**Issue:** `recursive_directory_iterator( root, ec )` with `ec` set at
construction (or during advance) exits the loop with `total` partially counted
or 0; the function returns it silently. The du belt
(`EXPECT_LE( du_after, du_before + 1MB )`) then passes without measuring
anything — the exact "vacuous pass" shape the file's own header doc promises to
avoid.
**Fix:** Record and report the error (e.g., return `std::optional<uint64_t>` or
assert `!ec` in the caller), so a broken iteration fails the belt loudly.

### IN-04: The `CompactStorage` const-pointer contract is engine-dependent, not absolute

**File:** `src/crdt/globaldb/globaldb.cpp:934-935`; `src/crdt/globaldb/globaldb.hpp:271-276`
**Issue:** The header documents the caller-owned atomic as "never written
here", bridged via `const_cast`. That is true of GlobalDB, but the vendored
engine's `DisableManualCompaction()` "may overwrite the user-provided canceled
variable" by writing `true` through the aliased reference
(db_impl_compaction_flush.cc:2784-2793, ManualCompactionState::canceled is a
reference to the caller's atomic). With this recipe the write path is
unreachable (it requires `options.change_level`, never set), and the pruner's
member is non-const, so there is no UB today — but the const promise documents
more than GlobalDB can guarantee. Also note the pruner's "never resets" stance
interacts benignly with such a write (same value).
**Fix:** Either soften the contract wording ("never written by GlobalDB; the
vendored engine may set it true in other CompactRangeOptions combinations —
none used here") or drop the const parameter and take
`std::atomic<bool> *canceled` so the signature tells the truth.

---

_Reviewed: 2026-10-24T20:21:27Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
