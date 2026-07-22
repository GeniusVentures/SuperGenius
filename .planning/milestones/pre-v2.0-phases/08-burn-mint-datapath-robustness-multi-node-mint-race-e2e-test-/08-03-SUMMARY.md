---
phase: 08-burn-mint-datapath-robustness-multi-node-mint-race-e2e-test-
plan: 03
subsystem: bridge-race-e2e-test
tags: [testing, bridge, mint-race, rpc-quorum, mock-transport]
dependency-graph:
  requires:
    - "test/src/bridge_race/bridge_race_fixture.hpp (08-01 fixture)"
  provides:
    - "sgns::test::BuildDivergentSlotConfigs() in test/src/mock/mock_rpc_config.hpp"
    - "bridge_race_fault_rpc_test ctest target"
  affects:
    - "test/src/bridge_race/CMakeLists.txt"
tech-stack:
  added: []
  patterns:
    - "Additive per-slot mock RPC config helper (BuildDivergentSlotConfigs), zero changes to existing MockEndpointConfig/MockBehavior/MockRpcTransport"
    - "TransportFactory dispatch lambda keyed on exact mock:// URL match (08-PATTERNS.md Pattern 3), installed via GeniusNode::GetTransactionManager()->GetPublicChainInputValidator().SetTransportFactory()"
key-files:
  created:
    - test/src/bridge_race/bridge_race_fault_rpc_test.cpp
  modified:
    - test/src/mock/mock_rpc_config.hpp
    - test/src/mock/mock_rpc_transport.cpp
    - test/src/bridge_race/CMakeLists.txt
decisions:
  - "BuildDivergentSlotConfigs() implemented in mock_rpc_transport.cpp (not a new mock_rpc_config.cpp) — matches the existing LoadMockConfig linkage placement; no dedicated mock_rpc_config.cpp exists in this codebase, so creating one would break the established file-layout convention rather than follow it"
  - "WeightedRpcEndpoint.bridge_contract_address/accepted_topic0_hashes for the mock:// slots use MockRpcTransport's own default success/wrong-logs literal address+topic0 (mirrored locally as kMockBridgeContractAddress/kMockBridgeEventTopic0), not the real Sepolia bridge contract — the mock:// endpoints never touch the real chain, so quorum verification must match what MockRpcTransport actually returns"
metrics:
  duration: "~25 min"
  completed: 2026-07-17
---

# Phase 8 Plan 03: RPC-Endpoint Disagreement Fault-Injection Test Summary

Extended the Phase 5 Mock RPC Transport with `BuildDivergentSlotConfigs()` and used it
to prove the existing >75% weighted RPC quorum still reaches the correct mint decision
under genuine 3-slot RPC-endpoint disagreement (one wrong-logs, one timeout, one
success) — the specific gap 08-RESEARCH.md flagged Phase 5 as never having exercised
(all prior tests pointed all 3 slots at the same real Anvil URL).

## What Was Built

- **`test/src/mock/mock_rpc_config.hpp`**: added `BuildDivergentSlotConfigs(direct_behavior,
  public1_behavior, public2_behavior)` declaration returning `std::array<MockEndpointConfig, 3>`
  with 3 distinct literal URLs (`"mock://direct"`, `"mock://public1"`, `"mock://public2"`)
  and independently configurable per-slot `MockBehavior` (defaults: `kSuccess`, `kWrongLogs`,
  `kTimeout`, matching 08-RESEARCH.md Pattern 3's exact example). Purely additive — zero
  changes to `MockEndpointConfig`, `MockBehavior`, or `MockRpcTransport`.
- **`test/src/mock/mock_rpc_transport.cpp`**: implementation of `BuildDivergentSlotConfigs()`,
  placed alongside `LoadMockConfig`'s existing implementation (matching linkage style — no
  dedicated `mock_rpc_config.cpp` exists in the codebase, so the .cpp was NOT created per the
  plan's own escape hatch: "follow the SAME pattern... to keep the file's linkage style
  consistent").
- **`test/src/bridge_race/bridge_race_fault_rpc_test.cpp`**: two `TEST_F` cases on the 08-01
  `BridgeRaceE2ETest` fixture:
  - `RpcDisagreementStillReachesCorrectQuorum`: DIRECT slot succeeds (weight=100) while
    PUBLIC1 returns wrong logs and PUBLIC2 times out — asserts the mint still completes via
    the DIRECT weight-100 shortcut despite genuine PUBLIC-slot disagreement.
  - `RpcDisagreementPublicPairQuorumStillCorrect`: DIRECT slot disagrees (`kWrongLogs`) while
    both PUBLIC slots succeed and agree with each other — asserts the mint completes via
    PUBLIC-pair agreement, exercising the `WeightedRpcEndpoint` dedup-based quorum path
    (not just the DIRECT shortcut).

  Both cases seed the burn against the real Anvil instance BEFORE installing the mock
  `TransportFactory`/`ConfigureRpcEndpoint` (D-03 ordering preserved), then install the
  3-slot divergent transport via `node0->GetTransactionManager().value()
  ->GetPublicChainInputValidator().SetTransportFactory(...)`, dispatching on exact
  `mock://` URL match per 08-PATTERNS.md Pattern 3.
- **`test/src/bridge_race/CMakeLists.txt`**: added a third `addtest(bridge_race_fault_rpc_test
  ...)` block mirroring the two 08-01 blocks exactly, plus an explicit
  `target_link_libraries(... mock_rpc_transport)` (not transitively pulled in via
  `genius_node_test`, confirmed by grepping `genius_node_test`'s `GENIUS_NODE_LIBS`
  definition in `src/account/CMakeLists.txt`), and `TIMEOUT 180` matching the other two
  race-suite targets.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug avoidance] Used mock-default contract address/topic0, not the real Sepolia bridge address**
- **Found during:** Task 2 (writing the fault-injection test)
- **Issue:** The plan's `<action>` implied reusing `sgns::test::anvil::kSepoliaBridgeContractLower` /
  `BridgeEventTopic0()` for the `WeightedRpcEndpoint`'s expected contract/topic0 fields. But
  `MockRpcTransport`'s default `kSuccess`/`kWrongLogs` receipt builders (in
  `mock_rpc_transport.cpp`, confirmed by reading the file) bake in their OWN literal address
  (`0x1234...`) and topic0 (`0x1234...1234`) — NOT the real Sepolia bridge contract. Since all
  3 configured slots in this test point at `mock://` URLs (never the real Anvil chain), using
  the real Sepolia address as the expected value would make every mock `kSuccess` response look
  like a wrong-logs mismatch, breaking both test cases.
- **Fix:** Declared local `kMockBridgeContractAddress`/`kMockBridgeEventTopic0` constants in the
  test file's anonymous namespace, mirroring `mock_rpc_transport.cpp`'s private defaults exactly
  (confirmed against the existing `mock_rpc_test.cpp`'s own local mirror of the same constants),
  and used those for the `WeightedRpcEndpoint` fields instead.
- **Files modified:** `test/src/bridge_race/bridge_race_fault_rpc_test.cpp`
- **Commit:** 5f4b3a78

### Not Auto-fixed (documented limitation)

**Build/ctest verification could NOT be run in this worktree.** No configured CMake build
tree exists here (only the top-level `build/{OSX,Linux,...}/CMakeLists.txt` platform
selectors, no `build/OSX/Release` build directory with a populated `CMakeCache.txt`), matching
the same limitation 08-01's summary documented. Code was written and cross-checked line-by-line
against the exact signatures of `GeniusNode::GetTransactionManager()`,
`TransactionManager::GetPublicChainInputValidator()`, `PublicChainInputValidator::SetTransportFactory()`,
`WeightedRpcEndpoint`, `MockRpcTransport`, and `MockEndpointConfig` (all read directly from source),
plus the established usage pattern in `test/src/bridge_e2e/bridge_rlpx_e2e_test.cpp` (for
`GetTransactionManager()`'s `outcome::result` unwrap) and `test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp`
(for `SetTransportFactory()` call shape). The orchestrator/user should run
`ctest -R bridge_race_fault_rpc_test --output-on-failure` (or `ctest -N -R bridge_race_fault_rpc_test`
for registration-only) in an environment with a configured build tree before considering this
plan's `<verification>` step complete. The plan's manual "deliberately-broken dedup" sanity check
(giving all 3 slots the same URL to confirm the test would then fail to exercise real
disagreement) was likewise not performed for the same reason.

## Verification Status

Not run (no configured build tree in this worktree — see limitation above).
`ctest -R bridge_race_fault_rpc_test --output-on-failure` must be run by the orchestrator/user
post-merge in an environment with submodules initialized and a configured build tree.

## Self-Check: PASSED (with noted build-verification limitation)

Files:
- FOUND: test/src/mock/mock_rpc_config.hpp (BuildDivergentSlotConfigs declaration added)
- FOUND: test/src/mock/mock_rpc_transport.cpp (BuildDivergentSlotConfigs implementation added)
- FOUND: test/src/bridge_race/bridge_race_fault_rpc_test.cpp
- FOUND: test/src/bridge_race/CMakeLists.txt (3 addtest() invocations confirmed via grep -c)

Commits: all three task commits verified present in `git log --oneline`
(becd10ee, 5f4b3a78, b1b367ef).

## Known Stubs

None.
