---
phase: 12-multi-node-finality-fault-proof
plan: "03"
subsystem: testing
tags: [consensus, crdt, durable-recovery, finality, multi-node, ctest]

requires:
  - phase: 12-01
    provides: durable consensus boundary observers and real peer stop/recreate fixture support
  - phase: 12-02
    provides: production-route multi-node finality scenarios and source-gate discipline
provides:
  - restart proofs at durable vote, accepted-certificate, and Mint-before-marker boundaries
  - publisher-loss proof that uses durable CRDT finality as the recovery authority
affects: [phase-12-validation, consensus-regression-tests, finality-fault-proof]

tech-stack:
  added: []
  patterns: [stop-aware post-durability barriers, real peer recreation and AddPeers reconnection, CRDT-authoritative recovery assertions]

key-files:
  created: [.planning/phases/12-multi-node-finality-fault-proof/12-03-SUMMARY.md]
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - test/src/blockchain/multi_node_finality_fault_test.cpp

key-decisions:
  - "Post-durability test barriers become stop-aware, leaving unfinished journal work for normal recovery on the recreated peer."
  - "CRDT immutable finality is authoritative after publisher loss; PubSub is best-effort cleanup and no successor re-advertisement or retry is introduced."
  - "Publisher-loss assertions prove canonical durable finality and exact-once Mint effects, not byte identity of concurrently generated valid certificate envelopes."

patterns-established:
  - "Durable-boundary fault tests stop a real peer only after the completed persistence action and reconnect it through AddPeers."
  - "Where production round selection is time-derived, observer barriers identify the actual publisher without forcing rounds or injecting protocol state."

requirements-completed: [TEST-04, TEST-05]

duration: 1h 8m
completed: 2026-08-25
---

# Phase 12 Plan 03: Durable-Boundary Restart and Publisher-Loss Proof Summary

**Real four-peer recovery regressions prove exact-once finality across durable restarts and selected-publisher loss, with CRDT immutable state as the final authority.**

## Performance

- **Duration:** 1h 8m
- **Started:** 2026-08-25T17:12:09Z
- **Completed:** 2026-08-25T18:20:14Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments

- Added TEST-04 with three independently initialized real stop/recreate paths at persisted vote, accepted certificate, and Mint-before-marker boundaries.
- Made post-durability barriers stop-aware so process shutdown cannot complete the paused continuation; the existing durable journals instead recover the remaining work after recreation.
- Added revised TEST-05: a real selected publisher persists `/cert/<slot>` with zero outbound notification then stops, a CRDT-unaware live peer remains deterministically eligible in a later production round, and all peers—including the recreated publisher—converge to one durable Mint result.

## Task Commits

Each TDD task was committed atomically:

1. **Task 1: Prove restart recovery at vote, certificate, and Mint durable boundaries** - `61d94d0c` (test: RED), `78b21122` (feat: GREEN)
2. **Task 2: Prove persistence-before-advertisement and deterministic publisher failover** - `581f949a` (test: RED), `3f2a1c46` (test: GREEN)

**Plan metadata:** pending final documentation commit

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - exposes stop-aware consensus fault-barrier return status for the existing test seam.
- `src/blockchain/Consensus.cpp` - abandons paused post-durability continuation during peer shutdown so durable recovery owns the retry.
- `src/account/TransactionManager.hpp` - makes the existing Mint test barrier report release versus shutdown.
- `src/account/TransactionManager.cpp` - wakes a paused Mint boundary on stop and preserves journal recovery after durable effects.
- `test/src/blockchain/multi_node_finality_fault_test.cpp` - adds TEST-04/TEST-05 four-peer production-route recovery proofs and read-only observations.

## Decisions Made

- Re-advertise the unchanged signed proposal after reconnecting a pre-vote restart, because offline GossipPubSub broadcasts are not replayed; this keeps recovery on public production ingress.
- CRDT immutable authority has precedence after the selected publisher stops. PubSub notification only accelerates cleanup, so the test does not require a successor notification/retry path.
- Concurrent valid certificates may have distinct serialized vote envelopes. Existing CRDT conflict resolution chooses the lower serialized hash, therefore canonical slot authority and exact-once Mint effects—not initial envelope byte identity—are the durable correctness contract.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical lifecycle behavior] Made post-durability barriers shutdown-aware.**
- **Found during:** Task 1 (durable-boundary restart proof)
- **Issue:** A peer stopped while blocked after a completed durable action could resume its old in-process continuation during teardown, bypassing the normal recreated-peer recovery path the test must prove.
- **Fix:** Barrier waits return whether they were released normally; `Stop()` wakes them and their callers leave remaining work to the existing durable recovery journals.
- **Files modified:** `src/blockchain/Consensus.hpp`, `src/blockchain/Consensus.cpp`, `src/account/TransactionManager.hpp`, `src/account/TransactionManager.cpp`, `test/src/blockchain/multi_node_finality_fault_test.cpp`
- **Verification:** TEST-04 passed directly in 50.875s and in the final target run in 48.914s.
- **Committed in:** `78b21122`

### Plan-Assumption Correction

**2. [User decision] TEST-05 proves CRDT-authoritative recovery rather than successor PubSub notification.**
- **Found during:** Task 2 (publisher-loss proof)
- **Issue:** The original plan required a later selected successor to publish the initial certificate bytes. That conflicts with the explicit decision that CRDT has precedence and PubSub has no retry; normal recovery may also generate a separately valid envelope, after which CRDT's existing lower-serialized-hash rule preserves canonical authority.
- **Fix:** No production re-advertisement was added. The test pauses only after real immutable persistence, proves the stopped publisher made zero notifications, observes a record-unaware peer's normal later-round eligibility, then proves canonical CRDT finality and exact-once Mint effects on every live and recreated peer.
- **Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
- **Verification:** TEST-05 passed directly in 17.795s and in the final target run in 17.178s.
- **Committed in:** `3f2a1c46`

---

**Total deviations:** 1 auto-fixed correctness issue and 1 user-directed plan-assumption correction.
**Impact on plan:** The test remains production-route and bounded, but correctly evaluates the existing CRDT-first protocol rather than an unimplemented PubSub retry design.

## Verification

- `cmake --build build/OSX/Release --target multi_node_finality_fault_test consensus_pending_lifecycle_test transaction_manager_certificate_fallback_test --parallel 4` — passed.
- Direct TEST-04: `RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` — passed in 50.875s.
- Direct TEST-05: `PublisherLossAfterPersistenceUsesDeterministicFailover` — passed in 17.795s.
- Declared three-target CTest run: `transaction_manager_certificate_fallback_test` passed in 32.11s; `consensus_pending_lifecycle_test` passed in 44.42s; the target executed both new scenarios successfully (TEST-04 48.914s, TEST-05 17.178s) but was red overall because `SameBurnContentionUsesOneCanonicalSlotAndExactMint` observed a missing bridge marker at line 658 after 17.2s.
- Targeted reproduction after that combined failure: `SameBurnContentionUsesOneCanonicalSlotAndExactMint` passed twice alone, in 17.160s and 17.208s. The failure did not reproduce deterministically, so no unrelated contention/protocol code was changed.
- Anti-shortcut source gate — passed with no forbidden direct handlers, CRDT writes, forced timers, or sleep synchronization found.

## TDD Gate Compliance

- Task 1 RED: `61d94d0c`; GREEN: `78b21122`.
- Task 2 RED: `581f949a`; GREEN: `3f2a1c46`.

## Known Stubs

None.

## Issues Encountered

- The combined target had an order-dependent contention failure: `HasBridgeMarker(*peer, *winner)` was false during `SameBurnContentionUsesOneCanonicalSlotAndExactMint`. Two immediate isolated runs passed, including after the combined sequence. The two newly added scenarios passed in the same combined invocation, and no unrelated contention logic was modified.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Durable restart and selected-publisher loss are covered using real CRDT, persistence, transport, and bounded condition barriers.
- The direct two-run reproduction cleared the same-burn contention case; retain the combined-run failure record if a later full-suite validation shows the order-dependent behavior again.

## Self-Check: PASSED

- Found `12-03-SUMMARY.md` and all four TDD commits: `61d94d0c`, `78b21122`, `581f949a`, and `3f2a1c46`.

---
*Phase: 12-multi-node-finality-fault-proof*
*Completed: 2026-08-25*
