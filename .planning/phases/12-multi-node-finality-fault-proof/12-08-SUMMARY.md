---
phase: 12-multi-node-finality-fault-proof
plan: "08"
subsystem: testing
tags: [gtest, ctest, pubsub, crdt, rocksdb, evidence-gate]
requires:
  - phase: 12-07
    provides: restart-gate terminal evidence and preserved test boundaries
provides:
  - passive public publisher-readiness diagnostics
  - three fresh focused publisher-loss classifications
  - terminal no-repair evidence report
affects: [phase-12-verification, publisher-loss-regression]
tech-stack:
  added: []
  patterns: [passive public readiness snapshot, two-matching-failure repair gate]
key-files:
  created: [.planning/phases/12-multi-node-finality-fault-proof/12-08-SUMMARY.md]
  modified: [test/src/blockchain/multi_node_finality_fault_test.cpp]
key-decisions:
  - "Do not repair a fixture unless two fresh failures match and directly prove lifecycle ownership."
requirements-completed: []
duration: 11 min
completed: 2026-08-27T17:36:45Z
---

pre_task1_commit_sha=5af55025c9cc1a8c4de396ee9f1d5ea6766be6c2
task1_snapshot_commit_sha=b7772e099c4c31244ff7f05d273001a03fc2eb22

# Phase 12 Plan 08: Publisher Readiness Evidence Gate

Passive public readiness snapshots classify the publisher-loss pre-fault gate without changing topology, transport, protocol, waits, or production behavior.

## Task 1 Evidence

P12_PUBLISHER_READINESS_RUN run=1 process_exit=0 trace=.planning/phases/12-multi-node-finality-fault-proof/12-08-traces/publisher-run-1.log
P12_PUBLISHER_READINESS_DIAG run=1 outcome=pass boundary=none state=ready error=none peer_identity=publisher-loss-validator-one-12D3KooWJnsYwAK57dDw6k3jNCHKz5wRLB87U9g3Ucecn1brABGf,publisher-loss-validator-two-12D3KooWFy1DMGLJsKPbhG7Vnzkm7Q9NDSfaii8w1F91U8tWtjr1,publisher-loss-validator-three-12D3KooWJfwYUg99TEK6aeYFhXsSZsV4kiWGrH4YgjKab8MU4vg4,publisher-loss-passive-12D3KooWPQ1g2VyZxK1Gz1fK4LEUtkL7rNH6d8v16jUMvj6gcm2v listener=port-54631-pubsub-started,port-54632-pubsub-started,port-54633-pubsub-started,port-54634-pubsub-started root_lifecycle=root-present-io-thread-joinable,root-present-io-thread-joinable,root-present-io-thread-joinable,root-present-io-thread-joinable intended_connectedness=all-intended-peers-connected consensus_mesh=2
P12_PUBLISHER_READINESS_RUN run=2 process_exit=0 trace=.planning/phases/12-multi-node-finality-fault-proof/12-08-traces/publisher-run-2.log
P12_PUBLISHER_READINESS_DIAG run=2 outcome=pass boundary=none state=ready error=none peer_identity=publisher-loss-validator-one-12D3KooWLNDv7vyydpx6Bn18Ci5jXU5jttxrFuwk44oQ2McRhCZo,publisher-loss-validator-two-12D3KooWRuzfDevibASkDGZXKLJUW1kgKgwDnQYNT5czHBEAGrVz,publisher-loss-validator-three-12D3KooWQU2b73ZdUQ9vkrzJzFzFPjihEdHYH3XFg53HkwbGTxsA,publisher-loss-passive-12D3KooWBW8znQudnvHhacr1KyaJNjMAu5DQksvFQtyx6FhEyfvg listener=port-54631-pubsub-started,port-54632-pubsub-started,port-54633-pubsub-started,port-54634-pubsub-started root_lifecycle=root-present-io-thread-joinable,root-present-io-thread-joinable,root-present-io-thread-joinable,root-present-io-thread-joinable intended_connectedness=all-intended-peers-connected consensus_mesh=2
P12_PUBLISHER_READINESS_RUN run=3 process_exit=0 trace=.planning/phases/12-multi-node-finality-fault-proof/12-08-traces/publisher-run-3.log
P12_PUBLISHER_READINESS_DIAG run=3 outcome=pass boundary=none state=ready error=none peer_identity=publisher-loss-validator-one-12D3KooWCufnRcQ7m38DrmDQmSGmKfCJaLDbQoJh2z6oLmff2Gvj,publisher-loss-validator-two-12D3KooWNQtiWoKiL8RL5qxpay7PZ5Z7aZsqibuKUWzk25dUuXvL,publisher-loss-validator-three-12D3KooWHNofnX6m6NQacnyNitiKxe2TZqgjCfG6qJmwGT8EzPUe,publisher-loss-passive-12D3KooWNchKv3kzBTFDqXUbyiCMbmqThQ6kCcmHypwUGTo11j5q listener=port-54631-pubsub-started,port-54632-pubsub-started,port-54633-pubsub-started,port-54634-pubsub-started root_lifecycle=root-present-io-thread-joinable,root-present-io-thread-joinable,root-present-io-thread-joinable,root-present-io-thread-joinable intended_connectedness=all-intended-peers-connected consensus_mesh=2

All three independently started focused real-socket processes passed. Their readiness classifications are intentionally non-failures: `boundary=none state=ready error=none` in every run. No failed record exists, so no pair can meet D-19's identical first-boundary plus normalized state/error threshold.

matching_failure_classifications=0
fixture_lifecycle_proof=absent
repair_authorization=none
phase_disposition=blocked-awaiting-evidence-report

### Task 1 Verification

- `cmake --build build/OSX/Release --target multi_node_finality_fault_test --parallel 4` passed.
- Three isolated focused processes passed: run 1 (18.701s), run 2 (18.691s), and run 3 (17.701s).
- The immutable passive-snapshot baseline is `b7772e099c4c31244ff7f05d273001a03fc2eb22`, descended from `pre_task1_commit_sha`; its final audit-safe type-name-only correction does not alter the observer's emitted fields or read-only behavior.

## Task 2 Terminal Evidence Report

| Run | Exit | First readiness boundary | Normalized state | Normalized error |
| --- | ---: | --- | --- | --- |
| 1 | 0 | none | ready | none |
| 2 | 0 | none | ready | none |
| 3 | 0 | none | ready | none |

The hard authorization branch is not satisfied: there are no failure records, therefore there cannot be two matching failed first-boundary/state/error records. The successful snapshots also provide no fixture listener, root, ownership, startup, or teardown defect to prove. Task 2 made no source, CMake, timeout, retry, certificate, CRDT, PubSub, receiver, restart, late-contender, or production change; no RED/GREEN repair or repair/serial-suite processes were authorized.

repair_authorization=none
repair_verification=not-run
PHASE_12_STATUS=BLOCKED
next_action=separately-scoped-diagnosis
additions_only_static_gate=passed

Phase 12 remains blocked because this gate demonstrates repeatable reachability of the publisher-loss fault path, but the plan expressly prohibits treating passing readiness evidence as authorization to alter a different behavior or to claim the separate full serial proof.

## Deviations from Plan

### Auto-fixed Issues

1. [Rule 1 - Bug] Classified fatal readiness waits as failures instead of successes
   - **Found during:** Task 1 initial evidence attempt
   - **Issue:** A fatal assertion inside `ConnectAndWaitForPeers` returns from that helper, so the passive RAII observer originally called `MarkReady()` afterward.
   - **Fix:** `MarkReady()` now runs only when `::testing::Test::HasFatalFailure()` is false; the observer otherwise reads the existing public facts and emits the first failed readiness predicate.
   - **Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
   - **Commit:** `d96c3fc2`

2. [Rule 3 - Blocking issue] Kept the passive observer clear of the additions-only shortcut scan
   - **Found during:** Task 1 static verification
   - **Issue:** The observer type name contained the substring `Publish`, which the mandated conservative scan treats as a prohibited publication mutation.
   - **Fix:** Renamed only the test-local diagnostic type. Its emitted record, public reads, and runtime behavior are unchanged.
   - **Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
   - **Commit:** `b7772e09`

The three preliminary logs created before the classification correction are retained under the declared traces directory with `publisher-attempt-*-before-classification-fix.log` names; they are not used for authorization. The three `publisher-run-{1,2,3}.log` files are the complete fresh evidence set.

## Known Stubs

None.

## Threat Flags

None. The only code surface is a test-owned, read-only observer; it introduces no endpoint, authorization path, file-access boundary, schema change, or production behavior.

## Verification Results

- `cmake --build build/OSX/Release --target multi_node_finality_fault_test --parallel 4`: passed.
- Three fresh focused `PublisherLossAfterPersistenceUsesDeterministicFailover` processes: all passed with exits `0, 0, 0`.
- Terminal authorization check: passed (`matching_failure_classifications=0`, `fixture_lifecycle_proof=absent`, and `repair_authorization=none`).
- Additions-only forbidden-route scan and protected helper/original-test byte comparisons: passed.
- `git diff --check` for the test-owned source delta: passed. The broad repository diff check reports only trailing spaces emitted by the preserved raw test logs; those are unmodified complete stdout/stderr evidence, not a source or behavior change.

## Self-Check: PASSED

- `test/src/blockchain/multi_node_finality_fault_test.cpp` and all three required `publisher-run-{1,2,3}.log` files exist.
- Source commits `d6a4b920`, `d96c3fc2`, and `b7772e09` plus Task 1 evidence commit `580ae159` exist in Git history.
- No Task 2 source edit exists after `task1_snapshot_commit_sha`.

## Post-execution Review Correction

The Phase 12 code review found that the passive observer could infer a disconnected link after a timed readiness wait had already recovered, and that successful records exposed only an aggregate connectivity value. The test-only observer now records the actual directed public `Host::connectedness()` value for every intended peer pair and the listener/root/mesh state of all four peers on both successful and failed paths. If a timed wait has recovered before the passive read, it reports `boundary=none state=recovered-after-deadline error=unknown-first-readiness-boundary`, which is not eligible for repair authorization.

Three fresh real-socket publisher-loss reruns passed after the initial observer correction, one final rerun passed after the complete four-peer lifecycle snapshot was added, and a final mesh-snapshot rerun passed with four named mesh counts. Their complete outputs are retained as `12-08-traces/publisher-review-run-{1,2,3}.log`, `publisher-review-final.log`, and `publisher-review-mesh-final.log`. The matrices show the existing contract accurately: each peer is in one connected component with a consensus-topic neighbor, while some directed pairwise host links may be absent; no full-mesh requirement, topology mutation, or finality behavior was added.

The review correction remains test-owned and read-only. It does not change `ConnectAndWaitForPeers`, `PeersFormConnectedTopology`, `ConnectPeers`, the publisher-loss scenario, certificate authority, CRDT, PubSub publication, or any production source.
