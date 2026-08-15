---
phase: 08-burn-mint-datapath-robustness-multi-node-mint-race-e2e-test-
plan: 04
subsystem: bridge-race-e2e-test
tags: [testing, bridge, mint-race, fault-injection, libp2p, pubsub]
dependency-graph:
  requires:
    - "test/src/bridge_race/bridge_race_fixture.hpp (08-01 fixture)"
  provides:
    - "bridge_race_fault_kill_test ctest target"
    - "bridge_race_fault_partition_test ctest target"
  affects:
    - "test/src/bridge_race/CMakeLists.txt"
tech-stack:
  added: []
  patterns:
    - "Object-lifecycle node destruction (shared_ptr::reset()) as D-10's fault scope, mirroring TearDownTestSuite's per-node reset() pattern but invoked mid-test"
    - "Remote PeerId obtained via Host::getId() on the remote node's own in-process Host instance, not multiaddress parsing"
key-files:
  created:
    - test/src/bridge_race/bridge_race_fault_kill_test.cpp
    - test/src/bridge_race/bridge_race_fault_partition_test.cpp
  modified:
    - test/src/bridge_race/CMakeLists.txt
decisions:
  - "Resolved 08-RESEARCH.md Open Question 2 by using libp2p::Host::getId() directly on each remote node's own Host instance (all 11 nodes run in-process in this fixture, so no multiaddress parsing of GetLocalAddress() was needed — confirmed via 3rdparty/libp2p/include/libp2p/host/host.hpp:80's virtual peer::PeerId getId() const). The plan's documented multiaddress-parsing fallback was not required."
  - "Partition test disconnects/reconnects cross-group peer links from BOTH sides (Host::disconnect() is local-only per-Host per the interface's single-peer_id signature, so full severance requires calling it on both endpoints of each cross-group pair)"
  - "kPartitionHealConvergenceTimeout=60000ms (generous, new constant local to the partition test file) to allow CRDT merge time after heal, distinct from the fixture's kRaceNodeReadyTimeout"
metrics:
  duration: "~30 min"
  completed: 2026-07-17
---

# Phase 8 Plan 04: Node-Kill and Pubsub-Partition Fault-Injection Tests Summary

Completed D-08's four fault scenarios by adding the two remaining lifecycle-level tests:
node-kill mid-mint (object destruction only, not process crash) and pubsub
partition-then-heal, both proving CRDT convergence to exactly-once mint survives the
fault.

## What Was Built

- **`test/src/bridge_race/bridge_race_fault_kill_test.cpp`**:
  `NodeKillMidMintStillConverges` seeds a burn to Light node index 1's destination,
  releases all 11 nodes' RPC endpoints together (D-03), then IMMEDIATELY (before any
  convergence wait) calls `s_nodes[5].reset()` — destroying a different Light node's
  `GeniusNode` object mid-mint (D-10, object-lifecycle scope, no `SIGKILL`/`fork`/
  subprocess spawning). Asserts the remaining 10 nodes still converge to exactly-once
  mint for the (unkilled) destination, then verifies exact balance (not `>=`) across the
  surviving subset, skipping the killed index.
- **`test/src/bridge_race/bridge_race_fault_partition_test.cpp`**:
  `PartitionThenHealConvergesExactlyOnce` splits the 11-node cluster into Group A (Full
  node + Light indices 1-5) and Group B (Light indices 6-10), disconnects every
  cross-group peer pair from both sides via `Host::disconnect(peer_id)`, seeds a burn to
  a Group B Light node's destination and releases all 11 nodes' RPC endpoints WHILE
  still partitioned (both groups can independently reach the real Anvil RPC — only the
  pubsub/CRDT mesh is split), waits a bounded 12s window for each group's watcher to
  poll independently, heals the partition via `AddPeers()` reconnecting every
  previously-severed pair, then asserts all 11 nodes converge to exactly-once mint
  within a 60s CRDT-reconciliation timeout.
- **`test/src/bridge_race/CMakeLists.txt`**: two new `addtest()` blocks
  (`bridge_race_fault_kill_test`, `bridge_race_fault_partition_test`) mirroring the
  existing three entries' include dirs, link libraries, and WHOLEARCHIVE triplet, each
  with `TIMEOUT 180`. `grep -c "addtest("` confirms 5 total invocations.

## Open Question 2 Resolution (carried from 08-RESEARCH.md)

Investigated `3rdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.hpp` and
`3rdparty/libp2p/include/libp2p/host/host.hpp` (read directly from the sibling checkout
at `/Users/henriqueklein/gnus/3rdparty/...` since this worktree's own `3rdparty/`
submodules are not populated — see Verification Status below) for the remote-`PeerId`
extraction API. Found `libp2p::Host::getId() const` (host.hpp:80), which returns a
`Host`'s OWN `PeerId`. Since this fixture runs all 11 nodes in-process (not as separate
processes), obtaining "node b's PeerId from node a's perspective" is trivially
`s_nodes[b]->GetPubSub()->GetHost()->getId()` — no multiaddress parsing of
`GetLocalAddress()`'s `/p2p/<peer_id>` suffix was needed. This is simpler and more
direct than the plan's documented fallback, and was used instead.

Also confirmed `Host::disconnect(const peer::PeerId &peer_id)` (host.hpp:182) is a
single-peer-id, presumably local-only per-`Host` call — the partition test calls it from
BOTH sides of each cross-group pair to fully sever the link, per the plan's guidance to
verify this rather than assume one-sided disconnect suffices.

## Deviations from Plan

None — plan executed as written, including the Task 2 investigation step. The `.cpp`
files' logic follows the plan's `<action>` sections exactly (kill index 5 distinct from
destination index 1; Group A = {0-5}, Group B = {6-10}; disconnect-then-heal ordering).

## Verification Status

**Build/ctest verification could NOT be run in this worktree** — this worktree has no
`3rdparty/` directory at all at the checked-out ref (submodules not populated), matching
the same limitation 08-01's and 08-03's summaries documented (no configured CMake build
tree, several vendored submodules absent). The Open Question 2 investigation was
performed by reading `gossip_pubsub.hpp`/`host.hpp` from a sibling checkout on the same
machine (`/Users/henriqueklein/gnus/3rdparty/...`) rather than this worktree, since the
files were otherwise unavailable here. Code was written and cross-checked line-by-line
against `GeniusNode`'s existing usage (`GetPubSub()`, `ConfigureRpcEndpoint`,
`GetBalance`), the 08-01 fixture's `DeriveLightDestination`/`s_nodes`/`s_anvil` members,
and the confirmed `Host::getId()`/`Host::disconnect()`/`GossipPubSub::AddPeers()`
signatures. The orchestrator/user should run
`ctest -R "bridge_race_fault_kill_test|bridge_race_fault_partition_test" --output-on-failure`
(and `ctest -N -R bridge_race` for full-suite registration, expecting 5 targets) in an
environment with submodules initialized and a configured build tree before considering
this plan's `<verification>` step complete.

## Self-Check: PASSED (with noted build-verification limitation)

Files:
- FOUND: test/src/bridge_race/bridge_race_fault_kill_test.cpp
- FOUND: test/src/bridge_race/bridge_race_fault_partition_test.cpp
- FOUND: test/src/bridge_race/CMakeLists.txt (5 `addtest(` invocations confirmed via `grep -c`)

Commits: all three task commits verified present in `git log --oneline`
(7967b584, 303a62a4, a73e244c).

## Known Stubs

None.
