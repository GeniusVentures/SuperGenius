---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-09-03T14:36:57Z
depth: standard
files_reviewed: 4
files_reviewed_list:
  - src/crdt/graphsync_dagsyncer.hpp
  - src/crdt/impl/graphsync_dagsyncer.cpp
  - test/src/blockchain/multi_node_finality_fault_test.cpp
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
findings:
  critical: 0
  warning: 5
  info: 9
  total: 14
status: issues_found
---

# Phase 12: Code Review Report (round 5 — round-4 gap-closure diff; supersedes commit 06887140)

**Reviewed:** 2026-09-03T14:36:57Z
**Depth:** standard
**Files Reviewed:** 4
**Status:** issues_found (0 critical)

## Summary

This round covers the four round-4 gap-closure commits against the current source of the
four in-scope files: 9f888b3b (ms-resolution blacklist clock + test seam in
`GraphsyncDAGSyncer`), e121f634 (`node.registry.reset()` in both lifecycle teardown
loops — the CR-03 fix), 0a9b76a8 (seam wiring with RAII guard in `RestartAtVote`), and
191e12f0 (post-restart certificate re-publication block). All four diffs were verified
line-by-line against the working tree; every commit's stated invariants were checked
against production source, not just the commit messages.

Verified correct:

- **9f888b3b (ms clock): production behavior is duration-identical when the override is
  unset.** Evidence: `GetCurrentTimestampMs` is referenced at exactly two production
  sites — `AddToBlackList` (`graphsync_dagsyncer.cpp:805`) and `IsOnBlackList`
  (`:875`) — and all four `BlacklistEntry.timestamp` touch points in the file
  (`:812`, `:820`, `:887`, `:892`) are inside those two functions, so no call-site was
  missed by the conversion and no mixed-unit comparison remains. The constants scaled
  exactly (5→5000, 30→30000, 10→10000, 1800→1800000); with the override at its default
  `0` (`:46`, `{ 0 }` initializer plus static zero-init) the short-circuit at `:828-832`
  is not taken and the formula path returns the same durations as before. The
  `cid_failures_` subsystem is untouched on the seconds clock: `GetCurrentTimestamp()`
  (seconds) is used only at `:1052`/`:1071` with `FAILURE_TIMEOUT = 180` seconds. The
  `:903` trace literal now correctly reads `{}ms`. The override is
  `std::atomic<uint64_t>` with `memory_order_relaxed` store/load — adequate for a
  duration knob with no ordering requirements, no torn reads, one definition (`:46`).
  Repo-wide grep finds **zero production callers** of
  `SetBlacklistBackoffTimeoutForTest`; the only call sites are the test's set/reset
  pair (`multi_node_finality_fault_test.cpp:2335`/`:2340`).
- **e121f634 (CR-03): RESOLVED.** Both teardown loops
  (`consensus_pending_lifecycle_test.cpp:1326-1337` and `:2028-2039`) now reset
  `manager` → `registry` → `db` → `account` before `pubsub->Stop()`. Re-checked for
  *remaining* GlobalDB co-owners in `MultiValidatorNode` (`:626-635`): the other
  members (`path`, `io`, `pubsub`) do not own the GlobalDB — ownership flows the other
  way (GlobalDB holds `io`/`pubsub`). The `scheduler`/`graphsync`/`generator`
  shared_ptrs are locals of `MakeMultiValidatorNode` and die at its return. At
  `pubsub->Stop()` the GlobalDB graphsync chain no longer co-owns the libp2p host; the
  invariant comments now match reality. WR-01/CR-03 are closed.
- **0a9b76a8 (seam wiring): correct.** The override is set at test-body scope
  (`:2335`) and the guard (`:2336-2342`) is declared immediately after, so it is
  destroyed after the three network blocks (reverse declaration order) and resets the
  override to 0 on every exit path including `ASSERT_*` returns. The collector tests
  fork+`execve` a fresh process (`:1581`), so children can never inherit a nonzero
  override; within this TU the collector tests also precede `RestartAtVote`, and even
  under shuffling the guard restore covers later tests. The override does apply to the
  `CRDTFixture`'s own idle DAGSyncer for the test's duration — harmless and within the
  "process-wide" contract documented at `graphsync_dagsyncer.hpp:122-133`.
- **191e12f0 (re-publication block): as claimed.** The +22 lines are confined to the
  `restart-mint` block (`multi_node_finality_fault_test.cpp:2494-2515`); it uses the
  public `SubmitCertificate` (`Consensus.hpp:481`) with ASSERT-guarded results and adds
  no direct `PutConvergentImmutable` write site. The barrier-arm-without-release
  pattern it depends on is safe: `ConsensusManager::Close` sets `stop_timer_` before
  `fault_test_cv_.notify_all()` and the barrier predicate
  (`Consensus.cpp:1352`) includes `stop_timer_.load()`; `TransactionManager::Stop`
  sets `stopped_` before notifying and the mint-barrier predicate
  (`TransactionManager.cpp:2122`) includes `stopped_.load()` — so `RestartPeer` on an
  armed-but-unreleased barrier neither hangs nor destroys a blocked member mid-wait.

No new Critical findings. The open items below are carried forward (most in files
outside this round's scope, re-verified against current source) plus one latent
pre-existing defect newly surfaced inside the rewritten `getBackoffTimeout` (WR-06).

## Prior-Round Findings — Status Against Current Source

| Prior ID | Location | Status |
|----------|----------|--------|
| CR-01 | smoke test `ComponentPeer::Stop` order | **RESOLVED** (414ca5ea; out of file scope, unchanged since) |
| CR-02 | `~CRDTFixture` order | **RESOLVED** (414ca5ea; out of file scope, unchanged since) |
| CR-03 / WR-01 | lifecycle teardown loops miss `registry` | **RESOLVED** (e121f634) — verified complete this round, see Summary |
| WR-02 | `TransactionManager::Stop` / `ConsensusManager::Close` notify without paired mutex | **OPEN — DEFERRED BY GATE** — still present (`TransactionManager.cpp:313-323`, `Consensus.cpp:155-159`); repair deliberately withheld by the 12-15 authorization gate decision; recorded here as open/deferred, not re-raised as new. Src files out of this round's file scope |
| WR-03 | CMake `APPLE`-only preflight | **OPEN** — `test/src/blockchain/CMakeLists.txt:62-63` still hard-fails on non-Apple; out of file scope, re-verified for existence |
| WR-04 | fixed port 40001 + no serialization properties | **OPEN** — `base_crdt_test.cpp:102`; `consensus_pending_lifecycle_test` still has no `RUN_SERIAL`/`RESOURCE_LOCK`; out of file scope, re-verified |
| WR-05 | non-fatal transport-start failures in `StartPeer` | **OPEN** — in file scope this round, see WR-05 below |
| IN-01 | dead `fcntl` on closed fds in collector child | **OPEN** — in file scope, now at `:1554-1555` (shifted +1 by the 0a9b76a8 include) |
| IN-02 | fixture reap failures swallowed | **OPEN** — `base_crdt_test.cpp:66-80`; out of file scope, re-verified |
| IN-03 | stale "(10 seconds)" comment on 5000 ms constant | **OPEN** — `TransactionManager.hpp:50-51`; out of file scope, re-verified |
| IN-04 | dead `GNUS_FULL_NODES_TOPIC_LEGACY` | **OPEN** — `TransactionManager.hpp:49`, still unreferenced; re-verified |
| IN-05 | unsynchronized `Configure*` writes | **OPEN** — `Consensus.cpp:469-510` still writes members without `timer_mutex_`; re-verified |
| IN-06 | pipe-descriptor leak when second `pipe()` fails | **OPEN** — in file scope, now at `:1542-1543` |
| IN-07 | keypair failure in fixture ctor hard-crashes | **OPEN** — `base_crdt_test.cpp:84`; out of file scope, re-verified |

## Critical Issues

None this round.

## Warnings

### WR-02 (carried, deferred by 12-15 gate): notify without paired mutex on shutdown paths

**File:** `src/account/TransactionManager.cpp:313-323`; `src/blockchain/Consensus.cpp:155-159` (src, out of file scope)
**Issue:** `Stop()`/`Close()` call `cv_.notify_all()`/`fault_test_cv_.notify_all()` without holding the mutex paired with those condition variables — a lost-wakeup hazard per the standard. The 12-15 gate decision withheld the repair; this entry records it as open/deferred so the report stands alone.
**Fix:** (when re-scoped) lock the paired mutex (even an empty critical section) before each `notify_all()`, or use a timed wait on the consumer side.

### WR-03 (carried): `APPLE`-only CMake preflight hard-fails all other POSIX platforms

**File:** `test/src/blockchain/CMakeLists.txt:62-63` (out of file scope, re-verified)
**Issue:** `if(NOT APPLE OR WIN32) message(FATAL_ERROR ...)` rejects Linux despite the per-primitive `P12_HAVE_*` checks that follow.
**Fix:** Gate on the actual primitive probes (`fork`/`setsid`/`kill`/`waitpid`/`socket`), not on `APPLE`.

### WR-04 (carried): fixed listen port 40001 shared by every `CRDTFixture` user; `consensus_pending_lifecycle_test` still unprotected

**File:** `test/testutil/storage/base_crdt_test.cpp:102` (out of file scope, re-verified)
**Issue:** The fixture still binds fixed port 40001. The multi-node targets hold `RUN_SERIAL TRUE` + `RESOURCE_LOCK phase12_real_socket_ports`, but `consensus_pending_lifecycle_test` (`CMakeLists.txt:38`) still has neither; overlapping ctest runs contend for 40001, and the loser's `BOOST_ASSERT_MSG` is a no-op in NDEBUG builds. The sibling fixed-port scheme `54001+index` (this round's in-scope file, `:657`) is the same class across repeated runs.
**Fix:** Bind port 0 and read back the ephemeral port, or give every CRDTFixture-based target `RUN_SERIAL TRUE` / a shared `RESOURCE_LOCK crdt_fixture_port`.

### WR-05 (carried, in file scope): transport-start failures are non-fatal in `StartPeer`

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1007`
**Issue:** `EXPECT_FALSE( peer.pubsub->Start( port, ... ).get() );` is non-fatal; on a bind failure the helper falls through to `GetHost()` graphsync wiring and `ConnectPeers`, converting a clean failure into a far-from-root-cause null-host dereference.
**Fix:**
```cpp
const auto start_result = peer.pubsub->Start( port, { peer.pubsub->GetLocalAddress() } ).get();
if ( start_result )
{
    ADD_FAILURE() << "pubsub start failed: " << start_result.message();
    return peer; // caller's ASSERT_TRUE( peer.consensus ) aborts cleanly
}
```

### WR-06 (new this round; pre-existing latent): `1ULL << failures` is UB once a peer accumulates 64 failures

**File:** `src/crdt/impl/graphsync_dagsyncer.cpp:843` and `:853`
**Issue:** `getBackoffTimeout` computes `base_ms * ( 1ULL << failures )`. `failures` grows without bound — `AddToBlackList` increments it (`:817`), only `RecordSuccessfulConnection` resets it to 0, and `blacklist_` entries are never erased — so a chronically unreachable-but-routable peer (re-blacklisted after each expiry) reaches `failures >= 64`, where `1ULL << failures` is undefined behavior (on x86-64 the shift count masks to `failures % 64`, silently collapsing the backoff to the base; other targets may do anything). The overflowed multiply is defined (unsigned wrap) but also silently wrong; only `failures >= 64` is UB. This predates 9f888b3b (the seconds version had the same expression), but the function was rewritten this round without addressing it.
**Fix:** Clamp the exponent — the caps are reached by 2^6 (ever-connected) and 2^8 (never-connected), so any clamp ≥ 16 preserves behavior:
```cpp
const uint64_t exponent = failures < 16 ? failures : 16;
uint64_t timeout = base_ms * ( 1ULL << exponent );
return std::min( timeout, max_ms );
```

## Info

### IN-01 (carried, in file scope): dead `fcntl(F_SETFD)` on already-closed descriptors

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1554-1555`
**Issue:** The collector child closes `data_pipe[0]`/`control_pipe[0]` (`:1552-1553`) then calls `::fcntl(..., F_SETFD, FD_CLOEXEC)` on them — guaranteed `EBADF` no-ops.
**Fix:** Delete the two `fcntl` calls.

### IN-02 (carried, out of file scope): fixture reap failures swallowed

**File:** `test/testutil/storage/base_crdt_test.cpp:66-80`
**Issue:** A throwing `fs::remove_all` during the construction-time reap only prints to stderr; construction then reopens the stale database the reap was meant to delete.
**Fix:** Fail the fixture loudly (throw / `GTEST_FAIL`) or retry once before proceeding.

### IN-03 (carried, out of file scope): stale comment on `NONCE_REQUEST_TIMEOUT_MS`

**File:** `src/account/TransactionManager.hpp:50-51`
**Issue:** Constant is `5000` ms; comment says "(10 seconds)". Re-verified unchanged.
**Fix:** Update the comment to "(5 seconds)".

### IN-04 (carried, out of file scope): dead legacy topic constant

**File:** `src/account/TransactionManager.hpp:49`
**Issue:** `GNUS_FULL_NODES_TOPIC_LEGACY` still has no references beyond its declaration. Re-verified.
**Fix:** Remove it, or wire it into the legacy-topic fallback if intended.

### IN-05 (carried, out of file scope): round-duration config writes unsynchronized with timer thread

**File:** `src/blockchain/Consensus.cpp:469-510`
**Issue:** `ConfigureTimestampWindow`/`ConfigureRoundDuration`/`ConfigureRoundSkew`/`ConfigureCertificateDelay` write the duration members without `timer_mutex_` while the round timer reads them under the mutex. Latent data race (no current production callers). Re-verified.
**Fix:** Take `std::lock_guard lock( timer_mutex_ )` in the setters, or make the durations atomics.

### IN-06 (carried, in file scope): pipe-descriptor leak when the second `pipe()` fails

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1542-1543`
**Issue:** `if ( ::pipe( data_pipe ) != 0 || ::pipe( control_pipe ) != 0 ) return Invalid(...)` — if `control_pipe` fails after `data_pipe` succeeded, both `data_pipe` fds leak for the process lifetime.
**Fix:** Close both pipe pairs (checking each fd is nonzero) before returning `Invalid(...)`.

### IN-07 (carried, out of file scope): keypair failure in fixture constructor hard-crashes

**File:** `test/testutil/storage/base_crdt_test.cpp:84`
**Issue:** `KeyPairFileStorage( ... ).GetKeyPair().value()` throws/terminates on error instead of producing a gtest failure. Re-verified.
**Fix:** Check the result and fail the fixture with a diagnostic before `.value()`.

### IN-08 (new, pre-existing): dead constants `TIMEOUT_SECONDS` and `MAX_FAILURES`

**File:** `src/crdt/graphsync_dagsyncer.hpp:142-143`
**Issue:** Neither constant has any reference in `src/` or `test/` (repo-wide grep). They sit directly above the blacklist machinery that this round touched, inviting the false belief that a failure cap exists — relevant to WR-06, where the absence of a cap is precisely the problem.
**Fix:** Delete both constants, or implement the cap they imply (see WR-06).

### IN-09 (new, pre-existing): write-only diagnostic state in the publisher-readiness observers

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:527-533, 698-699, 806, 985`
**Issue:** `PublisherReadinessSnapshot::ready_`/`successful_diagnosis_` are written in `MarkReady()` but never read; `PublisherReadinessObserver::invalid_reason_` is assigned in the constructor's catch block but never read. Dead stores that suggest an intended-but-missing diagnostic output.
**Fix:** Either consume them in a diagnostic line or remove the members.

---

_Reviewed: 2026-09-03T14:36:57Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
