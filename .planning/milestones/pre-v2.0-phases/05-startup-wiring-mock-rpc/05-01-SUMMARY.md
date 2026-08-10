---
phase: 05-startup-wiring-mock-rpc
plan: 01
subsystem: bridge
tags: [evmrelay, eth-watch, multi-chain, best-effort, gtest]

# Dependency graph
requires:
  - phase: 02
    provides: BridgeRelayer with Start(chain, address) single-chain baseline
  - phase: 03
    provides: TokenID::FromUint256, burn dedup cache
provides:
  - Multi-chain BridgeRelayer::Start(vector<ChainContractPair>) with per-chain watch tracking
  - Best-effort watch registration (skip failed chains, continue with others per D-21)
  - ChainContractPair struct for (chain_name, contract_address) pairs
affects: [05-05 GeniusNode startup wiring, 05-02 mock RPC transport]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Multi-chain watch tracking: std::unordered_map<std::string, eth::EventWatchId> chain_watches_"
    - "Best-effort registration: try/catch per chain in Start(), log warning on failure, continue"
    - "Lambda capture for chain_name: passed from Start() callback to OnWatchEvent for per-chain log traceability"

key-files:
  created: []
  modified:
    - src/account/BridgeRelayer.hpp — ChainContractPair struct, Start(vector<ChainContractPair>), chain_watches_ map
    - src/account/BridgeRelayer.cpp — Multi-chain Start() loop with best-effort, OnWatchEvent with chain_name
    - test/src/account/bridge_relayer_test.cpp — Multi-chain tests (MultiChainStart, SkipsChains*, BestEffort*, StartEmptyVector)
    - test/src/account/CMakeLists.txt — Registered bridge_relayer_test target

key-decisions:
  - "Used default-constructed eth::EthWatchService for multi-chain Start() tests (watch_event is non-virtual, mock override not possible)"
  - "Added logger parameter to BridgeRelayer constructor for test injection (defaults to BridgeRelayerLogger())"
  - "BridgeRelayerTestAccess::CreateForTest() factory pattern for test construction (private constructor, factory pattern)"
  - "Best-effort failure tested via invalid hex addresses and empty contract_address (same code path as exception catch)"

requirements-completed: [REQ-WIRE-01, REQ-CATCH-02]

# Metrics
duration: 16min
completed: 2026-06-04
---

# Phase 5 Plan 1: Multi-Chain BridgeRelayer Start() Summary

**Refactored BridgeRelayer::Start() from single-chain to multi-chain with per-chain watch ID tracking, chain_name in log output, and best-effort registration per D-21.**

## Performance

- **Duration:** 16 min
- **Started:** 2026-06-04T16:53:57Z
- **Completed:** 2026-06-04T17:10:23Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Replaced single `watch_id_` with `std::unordered_map<std::string, eth::EventWatchId> chain_watches_` for per-chain watch tracking
- Refactored `Start()` to accept `std::vector<ChainContractPair>` — registers a `BridgeSourceBurned` watch per chain
- Added `chain_name` parameter to `OnWatchEvent()` for per-chain traceability in all log messages
- Implemented best-effort registration: invalid hex addresses, empty addresses, and watch failures on one chain don't prevent others from registering (D-21)
- 16 unit tests pass (9 existing + 7 new): MultiChainStart, SkipsChainsWithoutAddress, SkipsChainsWithEmptyAddress, BestEffortSkipsInvalidAndEmptyNames, StartEmptyVector, NoWatchServiceReturnsEarly, OnWatchEventLogsChainName

## Task Commits

Each task was committed atomically:

1. **Task 1: Refactor BridgeRelayer for multi-chain Start()** - `45f444b8` (feat)
2. **Task 2: Update BridgeRelayer tests for multi-chain behavior** - `593d1571` (test)

## Files Created/Modified
- `src/account/BridgeRelayer.hpp` — Added `ChainContractPair` struct, multi-chain `Start()` signature, `chain_watches_` map, logger constructor parameter
- `src/account/BridgeRelayer.cpp` — Rewrote `Start()` with per-chain loop and try/catch, updated `OnWatchEvent` with `chain_name` parameter
- `test/src/account/bridge_relayer_test.cpp` — Added 7 new multi-chain tests, fixed constructor visibility via `CreateForTest()`, updated accessor for `chain_name`
- `test/src/account/CMakeLists.txt` — Registered `bridge_relayer_test` target with `genius_node_test` linkage

## Decisions Made
- Used default-constructed `eth::EthWatchService` for multi-chain tests — `watch_event()` is non-virtual so subclass mocking is not viable; real registration succeeds for valid inputs
- Added `logger` parameter to `BridgeRelayer` constructor for test injection, defaulting to `BridgeRelayerLogger()` when null
- Created `BridgeRelayerTestAccess::CreateForTest()` static factory to construct `BridgeRelayer` for unit tests (private constructor, factory pattern)
- Tested best-effort failure via invalid hex addresses and empty `contract_address` — exercises the same skip-and-continue code path as the exception catch block

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] bridge_relayer_test not registered in CMake**
- **Found during:** Task 1 verification
- **Issue:** `test/src/account/bridge_relayer_test.cpp` existed on disk but was not registered in `test/src/account/CMakeLists.txt` — build target didn't exist
- **Fix:** Added `addtest(bridge_relayer_test ...)` block with `genius_node_test` linkage and platform-specific `-force_load`/`--whole-archive` options
- **Files modified:** test/src/account/CMakeLists.txt
- **Verification:** `cmake --build build/OSX/Debug --target bridge_relayer_test` succeeds
- **Committed in:** `45f444b8` (Task 1 commit)

**2. [Rule 1 - Bug] Test constructor used nullptr for std::weak_ptr**
- **Found during:** Task 1 build
- **Issue:** Test code used `BridgeRelayer relayer(nullptr, nullptr, logger)` but `std::weak_ptr<TransactionManager>` cannot be constructed from `nullptr`
- **Fix:** Created `BridgeRelayerTestAccess::CreateForTest(logger)` static factory that constructs with `std::weak_ptr<TransactionManager>()` (empty weak_ptr) and a null watch_service
- **Files modified:** test/src/account/bridge_relayer_test.cpp
- **Verification:** Build succeeds, all 16 tests pass
- **Committed in:** `45f444b8` (Task 1 commit)

**3. [Rule 1 - Bug] Test used rlp::base::createLogger instead of base::createLogger**
- **Found during:** Task 1 build
- **Issue:** Test code called `rlp::base::createLogger()` which is not the standard project logger factory (`base::createLogger()`)
- **Fix:** Changed all test logger creation to `base::createLogger("bridge_relayer_test")`
- **Files modified:** test/src/account/bridge_relayer_test.cpp
- **Committed in:** `45f444b8` (Task 1 commit)

---

**Total deviations:** 3 auto-fixed (1 blocking, 2 bugs)
**Impact on plan:** All auto-fixes necessary for build correctness and testability. No scope creep.

## Issues Encountered
- `eth::EthWatchService::watch_event()` is non-virtual — cannot create a subclass mock that overrides watch_event behavior. Tested best-effort failure paths through invalid/empty address skip logic instead. The exception catch block in `Start()` remains as defensive code for unexpected errors.
- Direct test binary execution works (16/16 pass) but `ctest` doesn't discover the test — CMake's `addtest()` custom function may not register with CTest. Not blocking since binary execution succeeds.

## Threat Flags

None — no new security-relevant surface beyond what the plan's `<threat_model>` already covers. `contract_address` validation via `rlp::base::parse::hex_array()` provides the T-05-01 mitigation. `ChainContractPair` crosses from trusted call site (GeniusNode) to trusted component (BridgeRelayer) within same memory space.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness
- BridgeRelayer now accepts multi-chain input — ready for GeniusNode startup wiring (Plan 05-05)
- Per-chain `chain_watches_` map provides watch ID lookup for unwatch/health checks
- `chain_name` in all OnWatchEvent log output enables per-chain debugging and alerting
- Best-effort registration unblocks multi-chain deployment where not all chains have bridge contracts

---
*Phase: 05-startup-wiring-mock-rpc*
*Completed: 2026-06-04*
