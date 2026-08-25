---
phase: 12-multi-node-finality-fault-proof
fixed_at: 2026-08-25T18:42:00Z
review_path: .planning/phases/12-multi-node-finality-fault-proof/12-REVIEW.md
iteration: 1
findings_in_scope: 3
fixed: 3
skipped: 0
status: all_fixed
---

# Phase 12: Code Review Fix Report

**Fixed at:** 2026-08-25T18:42:00Z
**Source review:** `.planning/phases/12-multi-node-finality-fault-proof/12-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 3
- Fixed: 3
- Skipped: 0

## Fixed Issues

### CR-01: Fatal assertions terminate the process instead of cleaning up running peers

**Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`, `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp`
**Commit:** 4860323d
**Applied fix:** Added idempotent RAII shutdown and explicit move operations to `Peer` and `ComponentPeer`; `StopPeer` now delegates to that cleanup. Shutdown stops transaction and blockchain services, joins I/O, then releases PubSub and storage dependencies.

### WR-01: “ConnectPeers” has no connection-ready barrier before one-shot gossip publications

**Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`, `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp`
**Commit:** 9a95c3e8
**Applied fix:** Added connection-and-consensus-mesh readiness barriers using the public libp2p host connection state and public PubSub topic peer count. Initial and re-advertised proposals now follow the barrier; no certificate re-advertisement or protocol behavior was introduced.

### WR-02: New network tests collide on fixed ports when CTest runs in parallel

**Files modified:** `test/src/blockchain/CMakeLists.txt`
**Commit:** 0f646f95
**Applied fix:** Registered both real-socket targets with `RUN_SERIAL TRUE` while retaining their existing timeouts.

## Verification

- `cmake --build build/OSX/Release --target multi_node_finality_fault_test multi_node_finality_fault_compatibility_smoke_test -j2` — passed after CR-01 and WR-01.
- `cmake -S build/OSX -B build/OSX/Release` — passed; generated CTest registrations show `RUN_SERIAL "TRUE"` for both targets.
- `ctest --test-dir build/OSX/Release --output-on-failure -R '^multi_node_finality_fault_compatibility_smoke_test$'` — passed with local-socket permission (5.67s). The sandboxed attempt was blocked from binding listeners, as expected.
- `multi_node_finality_fault_test --gtest_filter=FinalityFaultNetwork.ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress` — passed with local-socket permission.
- `multi_node_finality_fault_test --gtest_filter=FinalityFaultNetwork.SameBurnContentionUsesOneCanonicalSlotAndExactMint` — passed with local-socket permission.

---

_Fixed: 2026-08-25T18:42:00Z_
_Fixer: the agent (gsd-code-fixer)_
_Iteration: 1_
