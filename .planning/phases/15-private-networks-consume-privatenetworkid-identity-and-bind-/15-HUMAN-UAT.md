---
status: resolved
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
source: [15-REVERIFICATION-3.md]
started: 2026-09-04
updated: 2026-10-02
---

## Current Test

[testing complete]

## Tests

### 1. E2E two-node private-network job flow with public-node control

expected: Provision two GeniusNodes with the same `private_network_id` /
network_key / bootstrap peers (config surface from 15-01), publish a job from
one node, and verify (a) both private nodes replicate job-scoped state under
the `/chain/<privateNetworkId>/` scope, (b) a concurrently running public
node (no private_network_id) sees none of the private job's topics, keys, or
results, and (c) tearing down one private node installs deny-all ingest on
its still-live GlobalDB (no stale private data continues to flow).

context: No automated multi-process GeniusNode E2E exists — closest automated
partial proof is `PnetIsolationAndGaterBlocking` (green). A full multi-node
automated E2E is additionally blocked by the tracked phase-13 quorum-policy
regression (`.planning/todos/pending/genesis-e2e-suites-quorum-policy-regression.md`),
which breaks `blockchain_genesis_test`/`processing_nodes_test` fixture
topologies independently of phase 15.

result: issue
reported: "So I think we should have an automated test for this"
severity: major
resolution: "Automated coverage delivered by plan 15-18 (gap-closure, 2026-10-02): NetworkMembershipFilterFlowTest.PrivateNetworkJobFlowReplicatesIsolatesAndDeniesOnTeardown — test/src/networkregistry/network_membership_filter_test.cpp case (11). All three legs machine-checked: scoped replication via the real data path, data-level public-node isolation on a live GlobalDB, deny-all teardown with HasMembershipFilter() persisting. Verified: 3/3 standalone green, non-vacuity mutations fail as designed, full suite 13/13, phase-15 battery 15/15 (verification cycle 5, 11/11 must-haves)."

## Summary

total: 1
passed: 0
issues: 1
pending: 0
skipped: 0
blocked: 0

## Gaps

<!-- YAML format for plan-phase --gaps consumption -->
- truth: "The E2E two-node private-network job flow with public-node control
  (private scope replication under /chain/<privateNetworkId>/, public-node
  isolation, deny-all ingest on private-node teardown) is covered by an
  automated test"
  status: resolved
  resolved_by: "15-18 (commits eafb89742, d1ffbc415) — automated E2E case (11); verification cycle 5 passed"
  reason: "User reported: So I think we should have an automated test for this"
  severity: major
  test: 1
  root_cause: "Coverage-composition gap, not a production bug. Every ingredient
    exists and is green in isolation, but no automated harness joins them: the
    historical job-flow E2E host (processing_multi_test) is unregistered in
    test/src/CMakeLists.txt; the GenesisNode E2E homes (blockchain_genesis_test,
    processing_nodes_test) hang from the tracked phase-13 quorum regression; and
    the surviving fixtures each stop one layer short —
    NetworkMembershipFilterFlowTest hand-rolls keys/topics instead of the real
    job path, its public control node has no GlobalDB (isolation proven only as
    transport non-connectivity), and no teardown-to-deny-all leg exists. The
    flow does NOT require Blockchain/genesis startup: single-peer quorum floors
    are 1/1, so a GlobalDB-level composition avoids the quorum-broken fixtures
    entirely."
  artifacts:
    - path: "test/src/networkregistry/network_membership_filter_test.cpp"
      issue: "Primary extension point — fixture (MakeNode/JoinTopic/CommitPut/
        AssertKeyNeverPresentWithin/IsConnectedTo/TearDownNodes) already
        supports PSK GlobalDB nodes; lacks job-shaped data path, public
        GlobalDB control node, and deny-all teardown leg"
    - path: "test/src/CMakeLists.txt"
      issue: "processing_multi (historical multi-GeniusNode job-flow E2E) not
        registered among the 42 add_subdirectory entries"
    - path: "test/testutil/genius_node_test_access.hpp"
      issue: "No tx_globaldb_/task_queue_ accessors (GeniusNode.hpp:1079/:1163
        private) — needed only for optional GeniusNode-level tier"
  missing:
    - "New NetworkMembershipFilterFlowTest case: two SYMMETRIC same-PSK GlobalDB
      member nodes publishing via the real scoped data path
      (TaskQueueImpl::New(db, TaskKeys::ScopedTopic(\"SGNUS.Processing.Channel\",
      kId), kId) + EnqueueTask + SubTaskResultStorageImpl), asserting
      TaskKeys::TaskKey/SubTaskKey/ClaimableListKey/SubTaskResultKey replicate
      to node B (bounded wait)"
    - "Public control node (different PSK) WITH its own GlobalDB, asserted via
      AssertKeyNeverPresentWithin for every /chain/<kId>/ key AND unscoped job
      keys (data-level isolation, not just IsConnectedTo == false)"
    - "Teardown leg: install MakeBootstrapMembershipFilter({}) (production
      shape from GeniusNode.cpp:2377-2378) on node A while its GlobalDB stays
      live; new scoped writes on B never arrive on A; HasMembershipFilter()
      stays true"
    - "Plan risks to carry: do NOT touch blockchain_genesis_test/
      processing_nodes_test (phase-13 todo); assert distinct host ids (DI Host
      aliasing precedent pubsub_counts.cpp:178-186); keep every member gated
      (ungated members mule intruder deltas, comment at
      network_membership_filter_test.cpp:607-617); PSK ctor needs explicit
      gossip Config; generous-but-bounded waits (15-25s) with wired teardown"
  debug_session: .planning/debug/automated-e2e-private-network-job-flow.md
