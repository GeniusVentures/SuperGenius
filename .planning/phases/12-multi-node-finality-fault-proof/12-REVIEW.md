---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-09-03T17:53:09Z
depth: standard
files_reviewed: 2
files_reviewed_list:
  - test/src/blockchain/multi_node_finality_fault_test.cpp
  - src/crdt/impl/graphsync_dagsyncer.cpp
findings:
  critical: 0
  warning: 2
  info: 6
  total: 8
status: issues_found
---

# Phase 12: Code Review Report (round 6 — round-5 gap-closure diff; supersedes commit 1b9ce185)

**Reviewed:** 2026-09-03T17:53:09Z
**Depth:** standard
**Files Reviewed:** 2
**Status:** issues_found (0 critical)

## Summary

This round covers the two 12-21 gap-closure commits against the current source of the two
in-scope files: 97bb5f6d (Option A composed repair in the restart-mint block, +45/-0 test
insertions) and 0e99efa3 (the WR-06 exponent clamp in `getBackoffTimeout`, +12/-4). Both
diffs were verified line-by-line against the working tree, and every load-bearing claim in
the new test comments was checked against production source (`Consensus.cpp`,
`TransactionManager.cpp`), not just the commit messages.

Verified correct:

- **WR-06: RESOLVED.** Both former `1ULL << failures` sites now clamp via
  `const uint64_t exponent = failures < 16 ? failures : 16;`
  (`src/crdt/impl/graphsync_dagsyncer.cpp:849` and `:860`); a repo grep of the file finds
  exactly two shift sites remaining (`:850`, `:861`), both clamped, so no third site was
  missed. The clamp eliminates the UB at `failures >= 64` while preserving every reachable
  behavior: the ever-connected cap (5000 ms base, 30000 ms cap) is saturated from exponent
  3 upward and the never-connected cap (10000 ms base, 1800000 ms cap) from exponent 8
  upward, so clamping at 16 (above both) changes no reachable value, and the multiplies
  cannot overflow (`5000 * 2^16` and `10000 * 2^16` both fit comfortably in `uint64_t`).
  `IsOnBlackList`'s `entry.failures - 1` call site (`:893`) remains guarded by the
  `failures == 0` early break (`:887-890`). One residual nit in the fix comment is recorded
  as IN-10 below.
- **Round-4 seam: INTACT and unchanged.** `git diff 1b9ce185..HEAD` on
  `graphsync_dagsyncer.cpp` contains only the two exponent hunks; the override definition
  (`:46`), the short-circuit (`:828-832`), `SetBlacklistBackoffTimeoutForTest`
  (`:865-868`), and the header doc block (`graphsync_dagsyncer.hpp:122-133`) are
  byte-identical to the round-4 state. The test-side wiring (100 ms set at
  `multi_node_finality_fault_test.cpp:2335` plus the RAII guard at `:2336-2342`) is
  untouched by 97bb5f6d, whose diff is pure insertion after `:2488`.
- **97bb5f6d Insertion A (pre-restart retention wait, `:2507-2512`): predicate is sound.**
  The 20 s wait on `CheckCertificateForSlot` for second/third/passive runs while `first` is
  paused at the mint-effects barrier and still alive/routable. The ordering claim in the
  comment was verified in production source: the deterministic publisher path
  (`Consensus.cpp:2532-2551`) calls `SubmitCertificate`, which performs the
  `PutConvergentImmutable` CRDT broadcast on `consensus_datastore_topic_` (`:2127`) and the
  notification `Publish` on `consensus_messages_topic_` (`:2145-2147` → `Publish` at
  `:266-267`) before returning, while mint effects are applied strictly downstream
  (`TransactionManager.cpp:5513-5519`) with the barrier entered after the effects counter
  and before the bridge marker (`:5528` → barrier wait at `:2114-2124`). So both delivery
  channels fire before the pause, and recipients can converge within the window. The shape
  mirrors the proven block-2 wait (`:2430-2435`, same 20 s bound, three survivors, one
  barrier-paused peer). No deadlock: the barrier pauses only `first`'s TransactionManager
  thread; its io_context keeps servicing CID fetches.
- **97bb5f6d Insertion B (mesh-readiness gate, `:2529-2538`): logic and topic are correct.**
  The gate checks `getPeerCount(ConsensusTopic(peer->consensus)) >= 2` for every peer.
  `ConsensusTopic` returns `consensus_messages_topic_`, which is exactly the topic the
  re-publication uses (`SubmitCertificate` → `Publish` → `pubsub_->Publish(
  consensus_messages_topic_, ... )`, `Consensus.cpp:267`), so the gate measures the right
  mesh. The graph argument holds for a 4-node topic graph: min degree >= 2 forces
  connectivity (any disconnected 4-node graph has a component of size <= 2 and thus a node
  of degree <= 1). The API usage matches the two pre-existing call sites (`:580`, `:1106`).
  One honest caveat: the degree argument assumes the per-peer topic-peer views are
  symmetric, which gossipsub guarantees only in steady state; a transiently asymmetric view
  can delay but not defeat the gate (it retries for the full 10 s), and the unchanged 25 s
  recovery wait (`:2561-2566`) remains the backstop — so this cannot produce a silent
  false-pass.
- **"Insertions only" claims verified.** The 97bb5f6d diff is exactly +45/-0 confined to
  the restart-mint block; no pre-existing bound was changed or relaxed (25 s mint-barrier
  wait `:2485-2487`, 25 s recovery `:2561-2566`, and 20 s reopen wait `:2574-2579` are all
  byte-identical), no barrier was touched, `PutConvergentImmutable` appears nowhere in the
  test (CERT-02 holds), and `SubmitCertificate` has exactly one call site (`:2559`,
  CERT-05 holds). The re-publication block re-enters `EnterFinalityFaultBarrier(
  certificate_persisted_barrier_ )` (`Consensus.cpp:2140`) only on the recreated peer's
  fresh (unarmed) manager, so it cannot hang; and the mint barrier armed on the old
  `first` is released by `TransactionManager::Stop` setting `stopped_` before notifying
  (barrier predicate includes `stopped_.load()`), so `RestartPeer` on the armed-but-
  unreleased barrier remains safe as established in the round-5 review.

No new Critical or Warning findings. The two Warnings below are carried forward (WR-02
deliberately deferred by the 12-15 gate; WR-05 re-verified in this round's in-scope test
file). The Info items are carried in-scope items plus two new low-severity observations.

## Prior-Round Findings — Status Against Current Source

| Prior ID | Location | Status |
|----------|----------|--------|
| CR-01/CR-02 | smoke test / fixture teardown order | **RESOLVED** (414ca5ea; unchanged since) |
| CR-03 / WR-01 | lifecycle teardown loops miss `registry` | **RESOLVED** (e121f634) |
| WR-02 | notify without paired mutex on shutdown | **OPEN — DEFERRED BY GATE** (re-verified present this round: `TransactionManager.cpp:313-322`, `Consensus.cpp:155-159`); see WR-02 below |
| WR-03 | `APPLE`-only CMake preflight | Recorded in superseded review; file out of scope, not re-verified this round |
| WR-04 | fixed port 40001 / missing `RESOURCE_LOCK` | Recorded in superseded review; files out of scope, not re-verified this round |
| WR-05 | non-fatal transport-start failures in `StartPeer` | **OPEN** — in file scope, re-verified at `:1007`; see WR-05 below |
| WR-06 | `1ULL << failures` UB at 64 failures | **RESOLVED** (0e99efa3) — see Summary |
| IN-01 | dead `fcntl` on closed fds in collector child | **OPEN** — in file scope, re-verified at `:1554-1555` |
| IN-02 | fixture reap failures swallowed | Out of scope, not re-verified this round |
| IN-03 | stale "(10 seconds)" comment | Out of scope, not re-verified this round |
| IN-04 | dead legacy topic constant | Out of scope, not re-verified this round |
| IN-05 | unsynchronized `Configure*` writes | Out of scope, not re-verified this round |
| IN-06 | pipe-descriptor leak on second `pipe()` failure | **OPEN** — in file scope, re-verified at `:1542-1543` |
| IN-07 | keypair failure hard-crashes fixture ctor | Out of scope, not re-verified this round |
| IN-08 | dead constants `TIMEOUT_SECONDS` / `MAX_FAILURES` | **OPEN — DECLINED THIS ROUND** (explicitly declined in 0e99efa3; still present at `graphsync_dagsyncer.hpp:142-143`) |
| IN-09 | write-only observer diagnostic state | **OPEN** — in file scope, re-verified (no readers found for `successful_diagnosis_`/snapshot `ready_`/`invalid_reason_`) |

## Critical Issues

None this round.

## Warnings

### WR-02 (carried, deferred by 12-15 gate): notify without paired mutex on shutdown paths

**File:** `src/account/TransactionManager.cpp:313-322` (notifies at `:320-321`); `src/blockchain/Consensus.cpp:155-159` (notifies at `:158-159`) — src, out of this round's file scope; existence re-verified this round
**Issue:** `Stop()`/`Close()` call `cv_.notify_all()`/`fault_test_cv_.notify_all()` without holding the mutex paired with those condition variables — a lost-wakeup hazard per the standard. The 12-15 gate decision withheld the repair; this entry records it as open/deferred so the report stands alone.
**Fix:** (when re-scoped) lock the paired mutex (even an empty critical section) before each `notify_all()`, or use a timed wait on the consumer side.

### WR-05 (carried, in file scope): transport-start failures are non-fatal in `StartPeer`

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1007`
**Issue:** `EXPECT_FALSE( peer.pubsub->Start( port, { peer.pubsub->GetLocalAddress() } ).get() );` is non-fatal; on a bind failure the helper falls through to `GetHost()` graphsync wiring and `ConnectPeers`, converting a clean failure into a far-from-root-cause null-host dereference. Re-verified unchanged by this round's diff (insertions start at `:2489`).
**Fix:**
```cpp
const auto start_result = peer.pubsub->Start( port, { peer.pubsub->GetLocalAddress() } ).get();
if ( start_result )
{
    ADD_FAILURE() << "pubsub start failed: " << start_result.message();
    return peer; // caller's ASSERT_TRUE( peer.consensus ) aborts cleanly
}
```

## Info

### IN-01 (carried, in file scope): dead `fcntl(F_SETFD)` on already-closed descriptors

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1554-1555`
**Issue:** The collector child closes `data_pipe[0]`/`control_pipe[0]` (`:1552-1553`) then calls `::fcntl(..., F_SETFD, FD_CLOEXEC)` on them — guaranteed `EBADF` no-ops.
**Fix:** Delete the two `fcntl` calls.

### IN-06 (carried, in file scope): pipe-descriptor leak when the second `pipe()` fails

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1542-1543`
**Issue:** `if ( ::pipe( data_pipe ) != 0 || ::pipe( control_pipe ) != 0 ) return Invalid(...)` — if `control_pipe` fails after `data_pipe` succeeded, both `data_pipe` fds leak for the process lifetime.
**Fix:** Close both pipe pairs (checking each fd is nonzero) before returning `Invalid(...)`.

### IN-08 (carried, declined this round): dead constants `TIMEOUT_SECONDS` and `MAX_FAILURES`

**File:** `src/crdt/impl/graphsync_dagsyncer.hpp:142-143`
**Issue:** Neither constant has any reference in `src/` or `test/`. They sit directly above the blacklist machinery touched by WR-06, inviting the false belief that a failure cap exists — relevant because the absence of a cap is precisely what made WR-06 possible. The 0e99efa3 commit message explicitly declines removing them this round ("round-5 production budget is the clamp alone"); recorded as open-declined, not re-raised.
**Fix:** Delete both constants in a later docs/cleanup round, or implement the cap they imply.

### IN-09 (carried, in file scope): write-only diagnostic state in the publisher-readiness observers

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:530-532, 698-699, 806, 985`
**Issue:** `PublisherReadinessSnapshot::ready_`/`successful_diagnosis_` are written in `MarkReady()` but never read (grep confirms zero readers); `PublisherReadinessObserver::invalid_reason_` is assigned in the constructor's catch block but never read. Dead stores that suggest an intended-but-missing diagnostic output. (The observer's own `ready_` IS read in `EmitTerminal` — only the snapshot class's members and `invalid_reason_` are dead.)
**Fix:** Either consume them in a diagnostic line or remove the members.

### IN-10 (new this round): WR-06 fix comment misstates the ever-connected saturation exponent

**File:** `src/crdt/impl/graphsync_dagsyncer.cpp:846` (comment block `:842-848`; same claim repeated in the 0e99efa3 commit message)
**Issue:** The comment says "the caps are reached by 2^6 and 2^8". The never-connected figure is correct (10000 * 2^8 = 2,560,000 >= 1,800,000; 2^7 is below), but the ever-connected cap is reached at 2^3 (5000 * 8 = 40000 >= 30000; 5000 * 4 = 20000 < 30000), not 2^6. The fix itself is unaffected — the clamp at 16 sits above both 3 and 8, so every reachable value is preserved — but the wrong figure could mislead future tuning of `base_ms`/`max_ms` (e.g., someone raising the cap assuming headroom through exponent 5 that does not exist).
**Fix:** Correct the comment to "caps are reached by 2^3 (ever-connected) and 2^8 (never-connected)".

### IN-11 (new this round, pre-existing): `Stop()` dereferences `graphsync_` without the null guard its sibling uses

**File:** `src/crdt/impl/graphsync_dagsyncer.cpp:1252-1258`
**Issue:** `GraphsyncDAGSyncer::Stop()` calls `graphsync_->stop()` unconditionally, while `StopSync()` (`:500-511`) null-checks both `graphsync_` and `host_`, and `StartSync()`/`RequestNode()` treat a null `graphsync_` as a reportable error rather than a crash. A `GraphsyncDAGSyncer` constructed with a null `graphsync` can therefore fail cleanly at start yet segfault at stop. No current caller constructs it that way, so this is a latent robustness inconsistency, not a live defect.
**Fix:** Guard as in `StopSync`: `if ( graphsync_ != nullptr ) { graphsync_->stop(); }`.

---

_Reviewed: 2026-09-03T17:53:09Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
