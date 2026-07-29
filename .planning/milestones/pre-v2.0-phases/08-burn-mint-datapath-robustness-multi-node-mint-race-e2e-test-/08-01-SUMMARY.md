---
phase: 08-burn-mint-datapath-robustness-multi-node-mint-race-e2e-test-
plan: 01
subsystem: bridge-race-e2e-test
tags: [testing, bridge, mint-race, anvil, e2e]
dependency-graph:
  requires: []
  provides:
    - "test/src/bridge_race/bridge_race_fixture.hpp (BridgeRaceE2ETest 11-node fixture)"
    - "bridge_race ctest target (bridge_race_single_burn_test, bridge_race_batch_test)"
  affects:
    - "test/src/CMakeLists.txt"
tech-stack:
  added: []
  patterns:
    - "SHA-256-derived deterministic node identity keys (DeriveNodeKey) replacing hardcoded key literal arrays"
    - "Star-topology PubSub mesh bootstrap (Light nodes peer directly with the Full node)"
    - "Seed-before-start, release-together RPC endpoint configuration (D-03 race trigger)"
key-files:
  created:
    - test/src/bridge_race/bridge_race_fixture.hpp
    - test/src/bridge_race/bridge_race_single_burn_test.cpp
    - test/src/bridge_race/bridge_race_batch_test.cpp
    - test/src/bridge_race/CMakeLists.txt
  modified:
    - test/src/CMakeLists.txt
decisions:
  - "Node identity keys derived at runtime via SHA-256(seed + index) instead of extending a hardcoded 3-key array to 11 entries, per D-06"
  - "Star-topology mesh bootstrap (each Light node peers only with the Full node) instead of a full 11x11 mesh, for lower setup cost"
  - "Batch test size fixed at 4 burns (within D-04's 3-5 range) with 6 additional unburned Light destinations (indices 5-10) used as the cross-burn-interference control group"
metrics:
  duration: "~35 min"
  completed: 2026-07-17
---

# Phase 8 Plan 01: 11-Node Mint-Race Fixture and Tests Summary

Built the 11-node (1 Full + 10 Light) `BridgeRaceE2ETest` fixture and two concurrency
tests proving `TransactionManager::MintFunds`'s exactly-once mint invariant holds under
genuine cross-node watcher discovery (zero manual `MintTokens()`/`MintFunds()` calls).

## What Was Built

- **`test/src/bridge_race/bridge_race_fixture.hpp`**: reusable `BridgeRaceE2ETest` fixture
  with `SetUpTestSuite()`/`TearDownTestSuite()` bodies inline in the header (so all
  bridge_race test binaries in this phase can `#include` and derive from it). Bootstraps
  11 `GeniusNode` instances (index 0 = Full, 1-10 = Light) against a local Anvil fork of
  Sepolia, registers genesis validators, bootstraps a star-topology PubSub mesh, and waits
  for the Full node to reach READY — all WITHOUT calling `ConfigureRpcEndpoint` (that call
  is deliberately left to each TEST_F body, per D-03). Node identity keys are derived via
  a new `DeriveNodeKey(index)` helper (SHA-256 over a fixed seed + index, re-hashed on the
  practically-unreachable all-zero-digest case) rather than a hardcoded key literal array
  (D-06). `DeriveLightDestination(light_index)` derives the SGNS destination address for a
  given Light node index (D-07).
- **`test/src/bridge_race/bridge_race_single_burn_test.cpp`**: `SingleContestedBurnExactlyOnce`
  seeds one burn to a Light-node destination, then releases `ConfigureRpcEndpoint` on all
  11 nodes back-to-back with no waits between calls, asserting every node mints the burn
  exactly once and the balance stays exactly at `initial + kMintAmount` through a stability
  window (double-mint guard).
- **`test/src/bridge_race/bridge_race_batch_test.cpp`**: `BatchBurnsNoInterference` seeds 4
  distinct burns to 4 distinct Light-node destinations before releasing RPC endpoints on
  all 11 nodes together, asserting each of the 4 destinations mints exactly once on every
  node and that unburned Light destinations (indices 5-10) show zero balance increase
  (cross-burn interference check).
- **`test/src/bridge_race/CMakeLists.txt`**: two `addtest()` blocks mirroring
  `bridge_e2e/CMakeLists.txt`'s link/whole-archive triplet, each with
  `set_tests_properties(... PROPERTIES TIMEOUT 180)` for the 11-node startup cost
  (D-15/Pitfall 2), isolated from the fast `bridge_e2e` suite.
- **`test/src/CMakeLists.txt`**: added `add_subdirectory(bridge_race)`.

## Deviations from Plan

None — plan executed as written. `DeriveNodeKey`'s re-hash-on-all-zero-digest loop caps
at 8 attempts (practically unreachable; SHA-256 producing an all-zero digest even once is
not something any real test run will encounter) rather than looping indefinitely, to avoid
an unbounded loop in test code.

## Verification Status

Code was written and reviewed against the exact signatures of `GeniusNode`, `TokenID`,
`WeightedRpcEndpoint`, `Blockchain`, and the `anvil_fixture.hpp` helpers used by the
existing `bridge_anvil_catchup_e2e_test.cpp` (verified line-by-line for parameter order,
types, and field names). **The build/ctest verification step
(`ctest -R bridge_race --output-on-failure`) could NOT be run**: this worktree has no
configured CMake build tree, and several vendored submodules (`ProofSystem`,
`SGProcessingManager`, `GeniusKDF`, `gRPCForSuperGenius`) are present as empty placeholder
directories (not checked out), so a from-scratch configure+build was not feasible within
this session. This is a known limitation — the orchestrator/user should run
`ctest -R bridge_race --output-on-failure` (or the project's standard build) in an
environment with submodules initialized and a configured build tree before considering
this plan's `<verification>` step complete. The "deliberately-broken dedup" manual sanity
check described in the plan's `<verification>` section was likewise not performed for the
same reason.

## Self-Check: PASSED (with noted build-verification limitation)

Files:
- FOUND: test/src/bridge_race/bridge_race_fixture.hpp
- FOUND: test/src/bridge_race/bridge_race_single_burn_test.cpp
- FOUND: test/src/bridge_race/bridge_race_batch_test.cpp
- FOUND: test/src/bridge_race/CMakeLists.txt
- FOUND: add_subdirectory(bridge_race) in test/src/CMakeLists.txt

Commits: all three task commits verified present in `git log --oneline`.

## Known Stubs

None.
