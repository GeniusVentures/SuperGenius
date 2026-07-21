---
title: bridge_race fixture — only 2 of 11 nodes reach InitializeAndStartBridge within 90s
date: 2026-07-21
priority: P1
source: Phase 8 execution — running bridge_race_single_burn_test against a real Anvil fork
resolves_phase: 08
---

## Problem

After fixing two confirmed bugs in `test/src/bridge_race/bridge_race_fixture.hpp` (genesis-authority
address-derivation race, and missing `bridge_chains_config.json` watcher wiring — see commit
`87ae6c9f`), `bridge_race_single_burn_test` still fails: only **2 of 11** nodes ever log
`InitializeAndStartBridge: thin orchestrator` within the test's 90s `kRaceNodeReadyTimeout` window.
Of those 2, both successfully scan the correct Anvil block range and mint the seeded burn
(`CatchUpScan: scanned 1 chains — 1 historical burns backfilled`) — the watcher pipeline itself
works correctly once reached. The other 9 nodes simply never reach that lifecycle stage in time.

## What's ruled out

- Not a role-based code gate — `InitializeAndStartBridge()`'s call site (`GeniusNode.cpp` ~line 735)
  has no `is_processor_`/node-type conditional; it's posted via `boost::asio::post` unconditionally
  once the account-creation state-machine step completes for every node.
- Not the watcher logic itself — the 2 nodes that do reach it work correctly end-to-end (scan, mint).

## Suspected cause (untested)

Likely a timing/tuning gap specific to the 11-node topology (1 Full + 10 Light, star-topology
pubsub mesh) — Light nodes may take longer to reach the account-creation/READY lifecycle stage
than the 3-node fixtures this pattern was proven on. `kRaceNodeReadyTimeout` (90s) may simply be
too short for 9 of 10 Light nodes under this load, or there may be a genuine startup-ordering
defect specific to processing 10 Light nodes concurrently against one Full node.

## Next steps

1. Add explicit instrumentation (or an intermediate `EXPECT_WAIT_FOR_CONDITION` on all 11 nodes
   reaching READY, before the mint-race wait) to see how many nodes are actually READY at the
   90s mark — this will show whether it's purely a timeout-too-short issue.
2. If most/all nodes are already READY but only 2 ever call `InitializeAndStartBridge`, that points
   to a real ordering/lifecycle bug worth its own debug session (`/gsd-debug`).
3. If the majority are still bootstrapping at 90s, raise `kRaceNodeReadyTimeout` and re-measure
   actual wall-clock cost for an 11-node cluster before locking in a new value.

## Affected tests

All 5 `bridge_race_*` binaries share `bridge_race_fixture.hpp`, so this blocks:
`bridge_race_single_burn_test`, `bridge_race_batch_test`, `bridge_race_fault_rpc_test`,
`bridge_race_fault_kill_test`, `bridge_race_fault_partition_test`.
