---
status: complete
quick_id: 260827-hbf
date: 2026-08-27
commit: 8c9e1b4f
---

# Quick Task 260827-hbf: GetGraphsyncNetwork accessor — Complete

## Outcome

Investigation confirmed the accessor **must** live in `GeniusNode.hpp`:

- `graphsyncnetwork_` is private (`src/account/GeniusNode.hpp:829` post-change)
- `crdt::GlobalDB` receives the network via ctor (`globaldb.hpp:59,273`) but exposes no getter
- `GeniusNode` has no `GetGlobalDB()` to chain through
- GeniusSDK already reaches the node via `GeniusSDKGetNode()` (`GeniusSDK.cpp:179`) — SDK needs zero changes; it calls `GeniusSDKGetNode()->GetGraphsyncNetwork()`

## Change

`8c9e1b4f` — `src/account/GeniusNode.hpp`: inserted public `GetGraphsyncNetwork()` (user-supplied snippet, verbatim) after `GetPubSub()`. 10 lines, accessor verified once, in public section. No build run (submodules not required; type already used at the member declaration).

## Related

- GeniusVentures/libp2p#11 — `setProtocolHandler` silent-replacement footgun (Bug B) that made the original clobber invisible. Assigned to itsafuu. Node-side mitigation for Bug A is this shared-Network accessor.

## Deviations

Executor agent terminated without committing; orchestrator applied the single-task edit directly per user instruction (no plan-check/verify ceremony for a one-function add).
