---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-08-25T18:29:32Z
depth: deep
files_reviewed: 8
files_reviewed_list:
  - src/blockchain/Consensus.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Blockchain.hpp
  - src/account/TransactionManager.hpp
  - src/account/TransactionManager.cpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp
  - test/src/blockchain/multi_node_finality_fault_test.cpp
findings:
  critical: 1
  warning: 2
  info: 0
  total: 3
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-08-25T18:29:32Z
**Depth:** deep
**Files Reviewed:** 8
**Status:** issues_found

## Summary

The Phase 12 production seams and their certificate, active-vote, and Mint call chains were reviewed together with both real-PubSub test targets. The review found that the tests are not failure-safe once an I/O thread has started, do not establish a transport-ready barrier before non-replayed publications, and conflict on fixed listener ports under parallel CTest execution. The CRDT-precedence model was treated as authoritative: none of the fixes below calls for successor certificate re-advertisement.

## Blockers

### CR-01: Fatal assertions terminate the process instead of cleaning up running peers

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:207-225` (also `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp:205-220`)
**Classification:** BLOCKER

**Issue:** `Peer`/`ComponentPeer` own a joinable `std::thread` but have no RAII destructor. Cleanup exists only in the manually reached `StopPeer` calls (main target: lines 281-295; smoke target: lines 293-317). Every `ASSERT_*` and `ASSERT_WAIT_FOR_CONDITION` after `StartPeer` can return from the test before those calls. Destruction then reaches `std::thread::~thread` while it is joinable, which invokes `std::terminate`; it also leaves PubSub/CRDT state live. This is observable on the timeout path and converts a useful assertion failure into an aborted test process.

**Fix:** Make peer shutdown RAII and idempotent, so both normal teardown and every assertion-return path stop the manager/blockchain, stop and join I/O, stop PubSub, and reset dependencies. Keep `StopPeer` as a thin `peer.Stop()` wrapper. For example:

```cpp
struct Peer {
    // existing fields and explicit move operations
    ~Peer() { Stop(); }

    void Stop() noexcept {
        if (transactions) transactions->Stop();
        transactions.reset();
        if (blockchain) (void)blockchain->Stop();
        consensus.reset();
        blockchain.reset();
        if (io) io->stop();
        if (io_thread.joinable()) io_thread.join();
        if (pubsub) pubsub->Stop();
        db.reset();
        pubsub.reset();
        account.reset();
        io.reset();
    }
};
```

Apply the equivalent implementation to `ComponentPeer`, and preserve explicit move construction/assignment after adding the destructor.

## Warnings

### WR-01: “ConnectPeers” has no connection-ready barrier before one-shot gossip publications

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:349-354`
**Classification:** WARNING

**Issue:** `ConnectPeers` only invokes asynchronous `AddPeers`; it does not wait for a libp2p connection or topic mesh to exist. Callers immediately submit one-shot proposals (for example lines 479-504, 724-725, and 988-989). The test itself documents that GossipPubSub does not replay publications made before a peer link (lines 621-625), so a scheduler-dependent race can lose the proposal permanently and make the later vote/certificate waits fail. The smoke test has the same ineffective “wait”: its predicates at lines 333 and 343 only check non-null pointers, which were already non-null before `AddPeers`.

**Fix:** Add a named readiness helper that waits until every expected peer is connected via the public host/connection API, and call it after each `ConnectPeers`/restart before the initial proposal publication. Keep the already-selected CRDT recovery behavior: this readiness step must not introduce successor certificate re-advertisement.

### WR-02: New network tests collide on fixed ports when CTest runs in parallel

**File:** `test/src/blockchain/CMakeLists.txt:57-68`
**Classification:** WARNING

**Issue:** Both targets inherit `CRDTFixture`, which always starts a PubSub listener on port 40001, and they additionally use fixed peer-port ranges. The CMake properties set only timeouts, so `ctest -j` can schedule these targets together (or alongside other `CRDTFixture` tests) and cause listener bind failures and unrelated timeouts.

**Fix:** Mark these real-socket targets serial, or assign a shared CTest resource lock for the fixed PubSub ports. For example:

```cmake
set_tests_properties(multi_node_finality_fault_compatibility_smoke_test
    PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
set_tests_properties(multi_node_finality_fault_test
    PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
```

---

_Reviewed: 2026-08-25T18:29:32Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: deep_
