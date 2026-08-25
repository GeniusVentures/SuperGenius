---
phase: 12-multi-node-finality-fault-proof
plan: "02"
subsystem: testing
tags: [gtest, ctest, pubsub, crdt, rocksdb, consensus, mint]
requires:
  - phase: 12-multi-node-finality-fault-proof
    provides: four-peer production-route fixture with durable finality observations
provides:
  - Same-burn contention proof with canonical-slot convergence and exact durable Mint recovery
  - Late-contender and passive-recipient proof with durable active-vote and receive-only assertions
affects: [12-03, finality-regression]
tech-stack:
  added: []
  patterns: [public proposal retransmission after real peer joins, durable active-vote readback, passive receive-only counters]
key-files:
  created: []
  modified:
    - test/src/blockchain/multi_node_finality_fault_test.cpp
key-decisions:
  - "Re-advertise the same signed public proposals after AddPeers because GossipPubSub intentionally does not replay offline broadcasts."
  - "Use the durable active-vote record rather than retrying vote-publication counters to prove a late contender cannot replace a vote."
patterns-established:
  - "Offline proposal ordering is exercised by initial public submit, real AddPeers, then public retransmission of the unchanged signed proposal."
  - "Passive recipients prove receive-only behavior with zero authoritative-write attempts plus notification, committed-readback, and Mint-effect observations."
requirements-completed: [TEST-01, TEST-02, TEST-03]
duration: 17min
completed: 2026-08-25
---

# Phase 12 Plan 02: Same-Burn Contention and Passive Recovery Summary

**Four real persistent peers now prove canonical same-burn winner selection, late-contender rejection, and passive durable Mint recovery without authority writes.**

## Performance

- **Duration:** 17 min
- **Started:** 2026-08-25T16:54:00Z
- **Completed:** 2026-08-25T17:10:42Z
- **Tasks:** 2/2
- **Files modified:** 1

## Accomplishments

- Added `SameBurnContentionUsesOneCanonicalSlotAndExactMint`, which submits two same-slot Mints through real public ingress, converges on one exact certificate winner, and proves winner-only effects across every peer restart.
- Added `LateContenderAndPassiveRecipientRemainReceiveOnly`, which proves the durable active-vote winner survives late admission, the accepted certificate remains authoritative, and the passive peer never attempts a certificate write.
- Extended the friend-scoped test accessor only with read-only counters and durable active-vote decoding; production consensus, CRDT, PubSub, and Mint code remain unchanged.

## Task Commits

1. **Task 1: Prove same-burn contention resolves one canonical exact winner** - `2bfc9a10` (RED test), `2d7123b2` (GREEN feature), `aee51e5d` (Rule 1 correction)
2. **Task 2: Prove late contenders fail closed and passive recipients stay receive-only** - `387808ca` (RED test), `999e7772` (GREEN feature)

## Files Created/Modified

- `test/src/blockchain/multi_node_finality_fault_test.cpp` - Named same-burn and late-contender scenarios, read-only durable active-vote inspection, passive authority-write assertions, and per-peer restart readback.

## Decisions Made

- Re-advertised the exact same signed proposals with public `SubmitProposal` after the real `AddPeers` barrier. Offline PubSub delivery is not replayed, so this keeps the intended order while exercising ordinary production ingress.
- Replaced a retry-sensitive vote-publication total with a durable active-vote record assertion. The counter correctly records normal retry publications and cannot prove unique vote ownership by itself.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Test correctness] Re-advertised offline proposals after the peer topology barrier**
- **Found during:** Task 1
- **Issue:** The initial submissions were correctly made while validators were disconnected, but real GossipPubSub does not replay those offline publications after `AddPeers`; the certificate convergence predicate timed out.
- **Fix:** Kept the isolated initial `SubmitProposal` calls, then re-submitted the unchanged signed proposals through the same public API immediately after the named real-peer barrier.
- **Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
- **Verification:** The direct real-PubSub focused scenario passed in 17.718 seconds.
- **Committed in:** `aee51e5d`

**2. [Rule 1 - Test correctness] Asserted durable vote identity instead of retry publication totals**
- **Found during:** Task 2
- **Issue:** `vote_publications` includes normal active-vote retransmissions, producing a valid 3-to-12 increase that does not represent a second vote.
- **Fix:** Read and decode each peer's existing durable active-vote record through the friend-only observer, asserting it remains bound to the original winning proposal before certificate acceptance.
- **Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
- **Verification:** The direct real-PubSub focused scenario passed in 16.664 seconds.
- **Committed in:** `999e7772`

---

**Total deviations:** 2 auto-fixed (2 Rule 1)
**Impact on plan:** Both corrections preserve real transport, public ingress, CRDT ownership, and durable proof while removing assumptions about replay and retry counters.

## TDD Gate Compliance

- RED: `2bfc9a10`, `387808ca`
- GREEN: `2d7123b2`, `999e7772`

## Verification

- `cmake --build build/OSX/Release --target multi_node_finality_fault_test --parallel 4` passed.
- Direct real-PubSub focused runner: `SameBurnContentionUsesOneCanonicalSlotAndExactMint` passed (1/1, 17.718s).
- Direct real-PubSub focused runner: `LateContenderAndPassiveRecipientRemainReceiveOnly` passed (1/1, 16.664s).
- The declared focused CTest command was attempted with local-network permission. In this runner, the CTest wrapper stopped after its start line without producing a final result; this is documented behavior from the predecessor phase. Direct invocation of the same registered binary supplied the focused pass evidence.
- Static anti-shortcut gate passed: the test source contains no direct handlers, certificate receive/consumer calls, CRDT writes, forced timers, mock transport, or sleeps.

## Known Stubs

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Plan 12-03 can add restart-boundary and publisher-failover scenarios on the verified four-peer fixture, retaining the public retransmission pattern whenever a scenario intentionally creates data before a real peer link exists.

## Self-Check: PASSED

- Summary exists and all task commits (`2bfc9a10`, `2d7123b2`, `aee51e5d`, `387808ca`, `999e7772`) are present in git history.

---
*Phase: 12-multi-node-finality-fault-proof*
*Completed: 2026-08-25*
