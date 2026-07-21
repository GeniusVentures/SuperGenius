---
title: bridge_race fixture — not all 11 nodes mint within the 90s race window (post-fix)
date: 2026-07-21
priority: P1
source: /gsd-debug session 2-of-11-nodes-start-bridge (resolved) — follow-up
resolves_phase: 08
---

## Problem

After fixing two confirmed bugs (see `.planning/debug/resolved/2-of-11-nodes-start-bridge.md`,
commits `a133fced` and `fdfe98d5`):

1. `ValidatorRegistry` genesis-registry discovery race — now all 11 nodes reliably reach
   `InitializeAndStartBridge` (was 2-3/11).
2. Stale per-node data directories surviving a crash — fixed with proactive cleanup, which also
   resolved a SEGFAULT observed during verification.

`bridge_race_single_burn_test` still does not pass. With both fixes applied, a clean run now shows:

- 11/11 nodes start their `BridgeCatchupWatcher` correctly.
- 33 `MintTokens` attempts fire (up from 2-6 before the fixes) — real, substantial progress; most
  nodes are now genuinely racing to observe and mint the burn.
- But not all 11 nodes complete a mint within `kRaceNodeReadyTimeout` (90s) — the test still fails
  at `bridge_race_single_burn_test.cpp:71` ("All 11 nodes must independently mint the contested
  burn exactly once (timeout: 90000ms)") and the subsequent per-node exact-balance checks.
- Additionally, that same clean run's `ctest` wall-clock hit the outer `--timeout 180` limit during
  teardown (not during the test body) — `TearDownTestSuite` appears to take meaningfully longer
  now that there's more in-flight CRDT/pubsub/watcher activity to unwind.

## What's ruled out

- Not the original `ValidatorRegistry` bug — confirmed via `InitializeAndStartBridge` count 11/11.
- Not a repeat of the stale-directory/segfault issue — this run reported clean gtest assertion
  failures with no crash.
- Not a fixture-registration-order bug — `bridge_chains_config.json` writes and genesis-authority
  registration are confirmed correct for all 11 nodes.

## Next steps

1. Add per-node timing instrumentation (when did each node's `CatchUpScan` first find the burn vs.
   when the 90s window elapsed) to see whether this is a pure timing/tuning gap (raise
   `kRaceNodeReadyTimeout`) or whether some nodes are stuck in a retry loop that never progresses
   (a real bug, similar in shape to the `ValidatorRegistry` one).
2. Investigate whether `TearDownTestSuite`'s slowdown under heavier load is expected (more
   async work to flush) or symptomatic of something not shutting down cleanly (e.g. a lingering
   watcher poll timer, CRDT worker thread not exiting promptly).
3. Consider whether `ctest`'s per-test `TIMEOUT` (currently 180s per
   `test/src/bridge_race/CMakeLists.txt`) needs raising alongside any change to
   `kRaceNodeReadyTimeout`.

## Affected tests

All 5 `bridge_race_*` binaries share `bridge_race_fixture.hpp` and are affected by any fixture-level
timing changes: `bridge_race_single_burn_test`, `bridge_race_batch_test`,
`bridge_race_fault_rpc_test`, `bridge_race_fault_kill_test`, `bridge_race_fault_partition_test`.
