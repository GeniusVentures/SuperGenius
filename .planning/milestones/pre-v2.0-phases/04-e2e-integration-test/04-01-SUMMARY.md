---
phase: 04-e2e-integration-test
plan: 01
subsystem: testing
tags: [e2e, bridge, sepolia, gtest, cast, foundry, utxo, consensus]

requires:
  - phase: 01-evm-bridge
    provides: "BridgeRelayer, EthWatchService, MintTransactionV2 pipeline"
  - phase: 02-conflict-and-replay-detection-hardening
    provides: "Slot key collision fix, fail-closed RPC, log verification"
  - phase: 03-burn-dedup-cache
    provides: "Burn dedup cache, UTXO witness fix, bridge contract topic0 verification"

provides:
  - "BridgeE2ETest fixture: 3 GeniusNode instances bootstrapped via PubSub"
  - "BurnToMintPipeline E2E test: Sepolia burn -> MintTokens -> UTXO consensus"
  - "CMake target bridge_e2e_test wired into build"

affects: [04-e2e-integration-test]

tech-stack:
  added: []
  patterns: ["E2E test fixture with env-var guards", "cast send via popen for burn trigger", "wait_condition polling for async UTXO verification"]

key-files:
  created:
    - test/src/bridge_e2e/CMakeLists.txt
    - test/src/bridge_e2e/bridge_e2e_test.cpp
  modified:
    - test/src/CMakeLists.txt

key-decisions:
  - "BaseWritePath uses std::string assignment (not strncpy) to match current DevConfig definition"
  - "Uses PRIVATE_KEY env var for node keys since E2E test targets live Sepolia"
  - "Follows processing_multi_test fixture pattern: 3 nodes, PubSub AddPeers, StopProcessing on proc nodes"

patterns-established:
  - "E2E test guard pattern: RUN_E2E_BRIDGE + PRIVATE_KEY + binary presence check via popen(which)"
  - "cast send via popen with JSON output parsing for burn transaction"

requirements-completed: []

duration: 5min
completed: 2026-05-31
---

# Phase 4 Plan 01: Bridge E2E Test Infrastructure Summary

**Three-node GTest fixture with BurnToMintPipeline E2E test exercising Sepolia burn-to-mint via cast send, MintTokens, and UTXO consensus polling**

## Performance

- **Duration:** 5 min
- **Started:** 2026-05-31T23:44:42Z
- **Completed:** 2026-05-31T23:49:39Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Created BridgeE2ETest fixture with 3 GeniusNode instances bootstrapped via PubSub (matching processing_multi_test pattern)
- Implemented BurnToMintPipeline test: cast wallet address derivation, cast send burn on Sepolia, MintTokens with outcome assertions, UTXO polling with wait_condition
- Three-guard skip logic: RUN_E2E_BRIDGE env var, PRIVATE_KEY env var, cast binary presence check
- Build passes cleanly, test skips without env vars

## Task Commits

Each task was committed atomically:

1. **Task 1+2: Create test infrastructure, fixture, and BurnToMintPipeline test** - `dbc9f871` (feat)

## Files Created/Modified
- `test/src/bridge_e2e/CMakeLists.txt` - CMake target for bridge_e2e_test with platform-specific whole-archive link flags
- `test/src/bridge_e2e/bridge_e2e_test.cpp` - BridgeE2ETest fixture (3 nodes, PubSub bootstrap) and BurnToMintPipeline E2E test
- `test/src/CMakeLists.txt` - Added add_subdirectory(bridge_e2e) in alphabetical order

## Decisions Made
- Used std::string assignment for BaseWritePath instead of strncpy — current DevConfig uses std::string, not char[]
- Used PRIVATE_KEY env var for node Ethereum keys since this is an E2E test against live Sepolia
- Followed processing_multi_test pattern: node_main (non-processor), node_proc1/2 (processor), StopProcessing on proc nodes, PubSub AddPeers mesh

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] BaseWritePath strncpy incompatible with std::string**
- **Found during:** Task 1 (build verification)
- **Issue:** Plan specified strncpy pattern from processing_multi_test.cpp, but current DevConfig.BaseWritePath is std::string, not char[]
- **Fix:** Used direct std::string assignment instead of strncpy; removed null-termination lines
- **Files modified:** test/src/bridge_e2e/bridge_e2e_test.cpp
- **Verification:** Build passes cleanly with ninja -j4
- **Committed in:** dbc9f871

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Minimal — strncpy vs assignment is a mechanical difference, no scope change.

## Issues Encountered
None — build passed on first attempt after fixing the strncpy issue.

## User Setup Required
None — no external service configuration required. The test requires:
- `RUN_E2E_BRIDGE=1` environment variable
- `PRIVATE_KEY` environment variable with a Sepolia-funded wallet
- `cast` binary (Foundry) installed

## Next Phase Readiness
- E2E test infrastructure ready for negative test cases (Plan 04-02)
- Test compiles and skips cleanly without env vars
- Test executes past all guards when env vars and cast are present

---
*Phase: 04-e2e-integration-test*
*Completed: 2026-05-31*
