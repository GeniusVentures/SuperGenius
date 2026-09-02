---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-09-02T00:00:00Z
depth: standard
files_reviewed: 11
files_reviewed_list:
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
  - src/blockchain/Blockchain.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Consensus.hpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
  - test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp
  - test/src/blockchain/multi_node_finality_fault_runner.cpp
  - test/src/blockchain/multi_node_finality_fault_test.cpp
  - test/testutil/storage/base_crdt_test.cpp
findings:
  critical: 2
  warning: 4
  info: 5
  total: 11
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-09-02T00:00:00Z
**Depth:** standard
**Files Reviewed:** 11
**Status:** issues_found

## Summary

This gap-closure round focuses on the three recent changes: the CRDTFixture run-unique
db paths and construction-time reaping (1564d4b7), the no-quorum certificate rejection
warning in `ConsensusManager::ValidateCertificate` (040fac6a), and the `Peer::Stop`
teardown reorder (b9ad7d2b).

Verified correct:

- **1564d4b7 (fixture paths):** the `pid_fixture-id` suffix is unique per process and per
  fixture-in-process; the reap targets exactly `keypair_path_`/`db_path_` before
  `KeyPairFileStorage`/`GlobalDB::New` open them, so a stale directory from a killed run
  (or pid reuse) can no longer be silently reopened. No over-broad sweep of `basePath`.
- **040fac6a (warn logging):** observability-only as claimed — no control-flow, return
  value, or signature change; both ternary arms produce `std::string`; network-sourced
  fields (`registry_cid`, slot key) are passed as runtime args, not format strings.
- **b9ad7d2b (Peer::Stop reorder) in `multi_node_finality_fault_test.cpp`:** the new
  order (`db.reset()`/`account.reset()` before `pubsub->Stop()`, `io.reset()` last)
  satisfies the stated asio invariant. The account's UTXO manager co-owns the rocksdb
  via `shared_ptr` (`UTXOManager.hpp:473`), so `db.reset()` before `account.reset()` is
  safe, and the parked-barrier thread keeps the old TransactionManager alive through the
  `weak_ptr` lock in the certificate handler (`TransactionManager.cpp:128-149`), so
  `transactions.reset()` cannot destroy the object under a waiting thread.
- The two prior-round Critical findings are confirmed fixed in current source and are
  not re-reported: `CreateProposalState` now takes `proposals_mutex_`
  (`Consensus.cpp:3043-3045`), and the conflicting-transaction lookup checks
  `tx_processed_m.end()` with a value-scan fallback (`TransactionManager.cpp:4007-4020`).

The dominant new problem class: **the teardown invariant established by b9ad7d2b was not
propagated**. Two in-scope teardown sites (the compatibility smoke test and
`CRDTFixture` itself) still call `GossipPubSub::Stop()` while the GlobalDB graphsync
chain still co-owns the libp2p host — the exact SIGSEGV mechanism the 12-14 plan
documented — and the multi-validator tests in `consensus_pending_lifecycle_test.cpp`
have the same shape.

## Critical Issues

### CR-01: Compatibility smoke test retains the pre-12-14 teardown order (`pubsub->Stop()` before `db.reset()`)

**File:** `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp:265-279`
**Issue:** `ComponentPeer::Stop()` executes `pubsub->Stop()` (line 274) and only then
`db.reset()` (line 275). This target builds the identical component topology that
b9ad7d2b fixed in `multi_node_finality_fault_test.cpp`: `graphsync::Network` is wired
from `peer.pubsub->GetHost()` (line 313) and handed to `GlobalDB::New` (lines 315-321),
so the GlobalDB chain co-owns the libp2p host. `ConnectPeers` (lines 395, 401) creates
real cross-peer TCP connections, so leftover `TcpConnection`s exist at teardown.
Per the 12-14 root cause, `StopImpl` destroys `m_host`/`m_context` while the external
co-owner keeps the host object alive; the later `~BasicHost` (member order:
`transport_manager_` before `network_`) then deregisters those connections from the
freed reactor — the teardown SIGSEGV crash pattern. The fix was applied to only one of
the two sibling teardowns.
**Fix:** Mirror the `Peer::Stop` ordering from `multi_node_finality_fault_test.cpp:377-411`:
```cpp
void Stop() noexcept
{
    if ( transactions ) transactions->Stop();
    transactions.reset();
    if ( blockchain ) (void) blockchain->Stop();
    consensus.reset();
    blockchain.reset();
    if ( io ) io->stop();
    if ( io_thread.joinable() ) io_thread.join();
    db.reset();                    // release host co-owners FIRST
    account.reset();
    if ( pubsub ) pubsub->Stop();  // final host release
    pubsub.reset();
    io.reset();
}
```

### CR-02: `~CRDTFixture` stops GossipPubSub before releasing GlobalDB — same 12-14 crash class in the shared fixture

**File:** `test/testutil/storage/base_crdt_test.cpp:107-126`
**Issue:** The destructor calls `pubs_->Stop()` (line 111) before `db_.reset()`
(line 113). The fixture itself constructs the co-ownership chain that b9ad7d2b
documented: `graphsync::Network` from `pubs_->GetHost()` (lines 89-91) passed into
`GlobalDB::New` (line 93), and `pubs_->Start( 40001, { pubs_->GetLocalAddress() } )`
(line 102) bootstraps with the node's own address, so a self-connection typically exists.
Every CRDTFixture-derived test binary in the repo inherits this teardown window: after
`StopImpl` frees `m_context`, `db_.reset()` runs `~GlobalDB` → `~BasicHost`, which
deregisters leftover TcpConnections from the freed reactor. This file was modified this
round (1564d4b7) but only on the construction side; the destructor still violates the
invariant the same phase established two commits later.
**Fix:** Reorder the destructor so the database is released before the transport stops,
keeping the io_context last:
```cpp
CRDTFixture::~CRDTFixture()
{
    db_.reset();                  // release the host co-owner first
    if ( pubs_ ) pubs_->Stop();   // now the final host release
    pubs_.reset();
    io_.reset();

    try
    {
        fs::remove_all( keypair_path_ );
        fs::remove_all( db_path_ );
    }
    catch ( const fs::filesystem_error &err )
    {
        std::cerr << err.what() << std::endl;
    }
}
```

## Warnings

### WR-01: Multi-validator teardown in pending-lifecycle test stops pubsub while node DB still alive

**File:** `test/src/blockchain/consensus_pending_lifecycle_test.cpp:1326-1330` and `2021-2025`
**Issue:** Both multi-validator tests end with
```cpp
for ( auto &node : nodes )
{
    sgns::ConsensusPendingLifecycleTestAccess::Close( node.manager );
    node.pubsub->Stop();
}
```
`node.db` (GlobalDB with the graphsync chain co-owning the host) is never released
before `pubsub->Stop()`; it survives until `MultiValidatorNode` scope exit. This is the
same ordering violation as CR-01/CR-02 with lower connection counts (self-dial only),
so the crash window is narrower but of the same class the phase is closing.
**Fix:** Add `node.manager.reset(); node.db.reset(); node.account.reset();` inside the
teardown loop before `node.pubsub->Stop()`, mirroring the fixed `Peer::Stop`.

### WR-02: Barrier wakeups can be missed — `Stop()`/`Close()` notify condition variables without holding the paired mutex

**File:** `src/account/TransactionManager.cpp:314-323`; `src/blockchain/Consensus.cpp:155-159`
**Issue:** `TransactionManager::Stop()` stores `stopped_` and then calls
`cv_.notify_all(); fault_test_cv_.notify_all();` with no lock held; `ConsensusManager::Close()`
does the same for `timer_cv_`/`fault_test_cv_`. A barrier waiter that evaluated the
predicate (false) but has not yet blocked on the CV will miss the notification
(`EnterFinalityFaultBarrier`, `TransactionManager.cpp:2111-2126` and
`Consensus.cpp:1343-1356`, waits up to 30 s). In the restart scenarios this matters: the
parked certificate-handler thread holds the last strong `TransactionManager` reference
via the `weak_ptr` lock (`TransactionManager.cpp:128-149`), so a missed wakeup keeps the
old peer's GlobalDB alive for up to 30 s while `RestartPeer` reopens the same RocksDB
root — lock contention on the reopened store and a plausible source of the intermittent
restart-test failures observed in UAT round 2.
**Fix:** Take the matching mutex around the notify so the store+notify is serialized
with the waiter's predicate check:
```cpp
void TransactionManager::Stop()
{
    if ( stopped_.exchange( true ) ) return;
    { std::lock_guard lock( fault_test_mutex_ ); fault_test_cv_.notify_all(); }
    { std::lock_guard lock( cv_mutex_ ); cv_.notify_all(); }
}
```
and equivalently `timer_mutex_`/`fault_test_mutex_` in `ConsensusManager::Close()`.

### WR-03: CMake preflight hard-fails on Linux although every required primitive is POSIX

**File:** `test/src/blockchain/CMakeLists.txt:62-64`
**Issue:** `if(NOT APPLE OR WIN32)` triggers `FATAL_ERROR` on any non-Apple platform,
including Linux — yet the immediately following `check_symbol_exists` block verifies
fork/setsid/getpgid/getsid/kill/waitpid/socket/bind/sigwait, all of which exist on
Linux, and the runner uses only standard POSIX APIs. The configure step of the whole
test suite aborts on Linux CI.
**Fix:** Gate on the actual capability result, e.g. `if(WIN32)` plus the existing
per-primitive `P12_HAVE_*` checks (optionally combined with a deny-list for known-bad
platforms), instead of hard-coding `APPLE`.

### WR-04: Run-unique fixture paths do not make concurrent binaries safe — fixed listen port 40001 is still shared

**File:** `test/testutil/storage/base_crdt_test.cpp:102`
**Issue:** 1564d4b7's commit message states "concurrent ctest binaries cannot collide on
cwd paths". That is true for the paths, but every `CRDTFixture` still starts its
GossipPubSub on the fixed port 40001. Two CRDTFixture-derived test binaries running
concurrently in the same ctest invocation will contend for that port; the second
`Start(...).get()` returns an error and `BOOST_ASSERT_MSG` aborts (or, with asserts
disabled, the fixture proceeds with a dead transport). `consensus_pending_lifecycle_test`
has no `RUN_SERIAL`/`RESOURCE_LOCK` properties, so it can overlap any other
CRDTFixture-using binary.
**Fix:** Either bind port 0 and read back the assigned ephemeral port from the host, or
derive a per-fixture port (e.g., `40000 + (getpid() + fixture_id) % 1000` with retry),
or give every CRDTFixture-based CTest target `RUN_SERIAL TRUE`/a shared
`RESOURCE_LOCK crdt_fixture_port` property.

## Info

### IN-01: `fcntl(F_SETFD)` on already-closed descriptors in the evidence-collector child

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1551-1554`
**Issue:** The child branch closes `data_pipe[0]`/`control_pipe[0]` and then calls
`::fcntl( data_pipe[0], F_SETFD, FD_CLOEXEC )` / `::fcntl( control_pipe[0], ... )` on
the just-closed descriptors — guaranteed `EBADF` no-ops. The apparent intent (CLOEXEC
hygiene) is already satisfied by the subsequent `dup2`/`close`/explicit
`flags & ~FD_CLOEXEC` handling.
**Fix:** Delete the two dead `fcntl` calls.

### IN-02: Fixture reap failures are swallowed, silently reintroducing the poisoning the fix targets

**File:** `test/testutil/storage/base_crdt_test.cpp:76-80`
**Issue:** If `fs::remove_all` throws, the handler prints to stderr and construction
continues to open the very database the reap was meant to delete — reproducing the
stale-state failure mode 12-13 set out to eliminate, but now with only a stderr hint.
**Fix:** Fail the fixture loudly on reap error (throw or `GTEST_FAIL`), or retry once
before proceeding.

### IN-03: Stale doc comment on `NONCE_REQUEST_TIMEOUT_MS`

**File:** `src/account/TransactionManager.hpp:50-51`
**Issue:** The constant is `5000` ms but the comment says "(10 seconds)".
**Fix:** Update the comment to "(5 seconds)".

### IN-04: Dead legacy topic constant

**File:** `src/account/TransactionManager.hpp:49`
**Issue:** `GNUS_FULL_NODES_TOPIC_LEGACY` is declared but has no references anywhere in
`src/` or `test/`.
**Fix:** Remove it, or wire it into the legacy-topic fallback if that behavior is still
intended.

### IN-05: Consensus round-duration config is unsynchronized with the timer-thread read

**File:** `src/blockchain/Consensus.cpp:480-489` (write) vs `205` (read under `timer_mutex_`)
**Issue:** `ConfigureRoundDuration`/`ConfigureRoundSkew`/`ConfigureCertificateDelay`
write the `std::chrono::milliseconds` members without `timer_mutex_` while the round
timer reads `round_duration_` under the mutex. Not atomic — a concurrent reconfigure is
a data race (latent: no current callers outside tests).
**Fix:** Take `std::lock_guard lock( timer_mutex_ )` in the `Configure*` setters, or make
the durations atomics.

---

_Reviewed: 2026-09-02T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
