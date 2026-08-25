---
phase: 12-multi-node-finality-fault-proof
plan: "01"
subsystem: testing
tags: [gtest, ctest, pubsub, crdt, rocksdb, consensus, mint]
requires:
  - phase: 11-convergent-certificate-consumption-mint-recovery
    provides: durable certificate consumption and idempotent Mint completion
provides:
  - Four-peer real PubSub/CRDT/RocksDB finality audit with durable restart assertions
  - Friend-scoped post-durability observations and bounded barriers for consensus and Mint
affects: [12-02, 12-03, finality-regression]
tech-stack:
  added: []
  patterns: [friend-only observation seams, bounded production-route integration waits]
key-files:
  created:
    - test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp
    - test/src/blockchain/multi_node_finality_fault_test.cpp
  modified:
    - src/blockchain/Consensus.cpp
    - src/account/TransactionManager.cpp
    - src/blockchain/Blockchain.hpp
    - test/src/blockchain/CMakeLists.txt
key-decisions:
  - "Use StopPeer/recreate from the retained root plus AddPeers because no installed public peer-disconnect operation was selected."
  - "Use the existing TestMintInputValidator registered for the test chain so the audit remains on the normal IInputValidator path."
patterns-established:
  - "Fault tests may observe or pause only after a named durable production boundary through friend-scoped state."
  - "Four-peer finality scenarios use real GossipPubSub, GlobalDB, public proposal submission, and named bounded predicates."
requirements-completed: [TEST-06]
duration: 64min
completed: 2026-08-25
---

# Phase 12 Plan 01: Multi-Node Finality Fault Proof Summary

**Four persistent peers now prove the real consensus-to-Mint route produces one durable canonical certificate and exact winning Mint through restart.**

## Performance

- **Duration:** 64 min
- **Started:** 2026-08-25T15:47:57Z
- **Completed:** 2026-08-25T16:51:33Z
- **Tasks:** 2/2
- **Files modified:** 9

## Accomplishments

- Added friend-only counters and bounded post-durability barriers at active-vote, certificate, and Mint-effect boundaries without adding production ingress APIs.
- Added a compatibility smoke proof for public Blockchain plus TransactionManager composition and the StopPeer/recreate/AddPeers recovery route.
- Added a normal 300-second CTest target that runs three validator peers plus one passive peer through real PubSub, CRDT, RocksDB, consensus, and registered Mint ingress, then proves durable exact-winner state after every peer is recreated.

## Task Commits

1. **Task 1: Prove harness compatibility, then add friend-scoped post-durability observers and barriers** - `211c506f` (test), `e2aa4d2e` (feat)
2. **Task 2: Build the persistent four-peer real-route harness and audit scenario** - `15c587f0` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp`, `src/blockchain/Consensus.cpp` - private production-boundary counters and barriers.
- `src/account/TransactionManager.hpp`, `src/account/TransactionManager.cpp` - Mint-effect observation after effects and before marker persistence.
- `src/blockchain/Blockchain.hpp` - narrow friendship to reach the fixture-owned registered ConsensusManager without a production getter.
- `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp` - runnable public-composition and restart/reconnect smoke proof.
- `test/src/blockchain/multi_node_finality_fault_test.cpp` - four-peer durable production-route audit.
- `test/src/blockchain/CMakeLists.txt` - normal smoke and audit CTest registrations; audit timeout is 300 seconds.

## Decisions Made

- Used the documented D-04 lifecycle fallback: stop a peer, recreate it from its unchanged root, then reconnect it with real `AddPeers`.
- Reused `TestMintInputValidator` and its registered `"test"` chain ID. The proof still executes normal TransactionManager validator selection, transaction binding, authorization, replay protection, consensus, and Mint ingress.
- Created a canonical fresh Mint with nonce `0`; this satisfies replay protection without introducing a synthetic predecessor or bypass.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical access] Reached the Blockchain-owned production ConsensusManager through narrow friendship**
- **Found during:** Task 2
- **Issue:** The registered TransactionManager consumer is attached to the ConsensusManager owned by Blockchain; the audit needed to observe that real manager without a production getter or a second test-created composition path.
- **Fix:** Added `MultiNodeFinalityFaultTestAccess` as a protected Blockchain friend and exposed only the fixture's existing manager reference through the test accessor.
- **Files modified:** `src/blockchain/Blockchain.hpp`, `test/src/blockchain/multi_node_finality_fault_test.cpp`
- **Verification:** `multi_node_finality_fault_test` passes through the registered production consumer.
- **Committed in:** `15c587f0`

---

**Total deviations:** 1 auto-fixed (1 Rule 2)
**Impact on plan:** Required to observe the existing production composition; it adds no public API, ingress, scheduler, CRDT write, or Mint-completion control.

## TDD Gate Compliance

- RED: `211c506f test(12-01): add failing finality fault compatibility proof`
- GREEN: `e2aa4d2e feat(12-01): add durable finality fault observation seams`

## Verification

- `multi_node_finality_fault_compatibility_smoke_test` passed using real GossipPubSub/GlobalDB and StopPeer/recreate/AddPeers.
- `multi_node_finality_fault_test` passed in 16.79 seconds with a four-peer persistent route and durable restart assertions.
- Built `multi_node_finality_fault_compatibility_smoke_test`, `consensus_pending_lifecycle_test`, `transaction_manager_certificate_fallback_test`, and `multi_node_finality_fault_test` together.
- Ran the declared focused CTest command. Its first completed regression, `transaction_manager_certificate_fallback_test`, passed in 29.03 seconds; the runner returned partial output before printing the remaining summaries, so no unobserved aggregate-pass claim is made here.
- The required static anti-shortcut gate passed: no direct handlers, certificate-receive calls, CRDT writes, forced timers, or sleeps occur in the new audit source.

## Known Stubs

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Phase 12 Plans 02 and 03 can build contention, delayed-delivery, restart-boundary, and publisher-failover scenarios on the persistent four-peer harness and its safe observation seams.

## Self-Check: PASSED

- Summary exists and all task commits (`211c506f`, `e2aa4d2e`, `15c587f0`) are present in git history.

---
*Phase: 12-multi-node-finality-fault-proof*
*Completed: 2026-08-25*
