---
phase: 12-multi-node-finality-fault-proof
plan: "04"
subsystem: testing
tags: [consensus, crdt, rocksdb, pubsub, restart, multi-node, ctest]

requires:
  - phase: 12-03
    provides: real four-peer durability-boundary and publisher-loss fault scenarios
provides:
  - attainable public PubSub topology readiness for restart recovery
  - durable certificate, UTXO/outpoint, and bridge-marker exact-once assertions after recreation
affects: [phase-12-verification, consensus-regression-tests, finality-fault-proof]

tech-stack:
  added: []
  patterns: [public connected-component readiness, durable-state recovery assertions, live-only instrumentation assertions]

key-files:
  created: [.planning/phases/12-multi-node-finality-fault-proof/12-04-SUMMARY.md]
  modified: [test/src/blockchain/multi_node_finality_fault_test.cpp]

key-decisions:
  - "Topology readiness requires a started connected component and one consensus-topic neighbor per peer, not an all-to-all GossipSub mesh."
  - "Reopened peers prove exact-once Mint recovery through durable certificate, UTXO/outpoint, and bridge-marker state; Mint counters remain live-process observations only."
  - "Publisher-loss recovery remains CRDT-first and introduces no successor certificate notification or re-advertisement."

patterns-established:
  - "Before restarting an accepted-certificate receiver, retain the durable certificate on surviving peers so the fault proves recovery from actual replicated state."

requirements-completed: [TEST-04, TEST-05]

duration: 18m
completed: 2026-08-25
---

# Phase 12 Plan 04: Real Topology and Durable Recovery Proof Summary

**The four-peer regression now uses attainable public topology readiness and proves restart/publisher-loss exact-once behavior from persisted certificate, output, and marker state.**

## Performance

- **Duration:** 18m
- **Started:** 2026-08-25T19:29:00Z
- **Completed:** 2026-08-25T19:47:13Z
- **Tasks:** 2
- **Files modified:** 1

## Accomplishments

- Replaced the impossible all-to-all host and GossipSub mesh check with public `IsStarted`, libp2p connected-component, and one consensus-topic-neighbor readiness.
- Ensured the accepted-certificate restart retains the certificate on surviving peers before faulting the receiver, then recovers through the real reconnect path.
- Split durable recovery checks from process-local Mint observers; all reopened-state assertions now check canonical certificate, exact winner-only UTXO/outpoint state, loser absence, and bridge marker.
- Kept the selected publisher's zero-notification proof and CRDT-first recovery intact without adding certificate retry or successor advertisement behavior.

## Task Commits

Each task was committed atomically:

1. **Task 1: Replace all-to-all reconnect assumptions with real reachable-topology readiness** - `d8bb8b5e` (test)
2. **Task 2: Assert durable exact-once Mint state across restart and CRDT-first publisher loss** - `d8bb8b5e` (test)

**Plan metadata:** pending final documentation commit

## Files Created/Modified

- `test/src/blockchain/multi_node_finality_fault_test.cpp` - public connected-topology readiness and durable finality assertions for TEST-04 and TEST-05.

## Decisions Made

- Treat a connected overlay with one consensus-topic neighbor per participant as the test's attainable transport readiness condition.
- Do not infer durable exact-once behavior from a new `TransactionManager` observer counter after a peer has been recreated.
- Preserve CRDT as durable authority after publisher loss; PubSub remains best-effort cleanup.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing durable precondition] Retained the accepted certificate on surviving peers before restarting its receiver.**
- **Found during:** Task 1 (real reachable-topology readiness)
- **Issue:** Under the valid sparse GossipSub overlay, stopping the receiver immediately after its durable callback could remove the only currently routable certificate copy before the recovery assertion.
- **Fix:** Added a bounded public certificate-presence condition on the three surviving peers before restarting the receiver.
- **Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
- **Verification:** Focused TEST-04 passed in 51.938s; serial target passed.
- **Committed in:** `d8bb8b5e`

---

**Total deviations:** 1 auto-fixed reliability issue (Rule 2).
**Impact on plan:** The test retains real Stop/recreate and AddPeers recovery while proving recovery from replicated durable state rather than a transient full mesh.

## Verification

- `cmake --build build/OSX/Release --target multi_node_finality_fault_test --parallel 4` — passed.
- Focused TEST-04: `RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` — passed in 51.938s.
- Focused TEST-05: `PublisherLossAfterPersistenceUsesDeterministicFailover` — passed in 17.855s.
- `ctest --test-dir build/OSX/Release --output-on-failure --timeout 300 -R '^multi_node_finality_fault_test$'` — passed in 118.75s.
- Anti-shortcut source gate — passed with no direct handlers, CRDT writes, forced timers, mock transport, or sleep synchronization found.

## TDD Gate Compliance

- RED baseline: former TEST-04 readiness timed out at the all-to-all mesh predicate; the old TEST-05 evidence treated a fresh process counter as durable proof.
- GREEN: both focused scenarios and the serial target passed with public topology and durable-state evidence.

## Known Stubs

None.

## Issues Encountered

- The initial focused run in the sandbox could not open local listener ports. The approved real-socket execution was used for all behavioral verification.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- TEST-04 and TEST-05 now have passing real-socket proof under the locked CRDT-authoritative, PubSub-best-effort protocol behavior.
- The phase is ready for final code review and goal verification.

## Self-Check: PASSED

- Confirmed the only code change is the Phase 12 fault harness and all plan-declared focused and serial checks passed.

---
*Phase: 12-multi-node-finality-fault-proof*
*Completed: 2026-08-25*
