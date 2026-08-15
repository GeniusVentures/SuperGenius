---
phase: 02-relayer-burn-detection
plan: 02
subsystem: bridge
tags: [c++17, evmrelay, bridge, relayer, gtest, eth-watch]

# Dependency graph
requires:
  - phase: 01-rpc-endpoint-wiring
    provides: "ChainRpcEndpointProvider, RPC endpoint loading from chains_config.json"
  - phase: 03-burn-dedup-cache
    provides: "Burn dedup via deterministic slot keys, in-memory reservation, RocksDB persistence"
provides:
  - BridgeRelayer: DI EthWatchService, BridgeSourceBurned watch registration, burn → MintFunds wiring
  - BridgeRelayer unit tests: 9 GTest cases (data extraction + OnWatchEvent behavior)
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "BridgeRelayerTestAccess friend accessor for private OnWatchEvent testing"
    - "CRDTFixture-free BridgeRelayer tests using null TransactionManager for error path coverage"

key-files:
  created:
    - src/account/BridgeRelayer.hpp
    - src/account/BridgeRelayer.cpp
    - test/src/account/bridge_relayer_test.cpp
  modified:
    - src/account/GeniusNode.cpp
    - src/account/GeniusNode.hpp
    - test/src/account/CMakeLists.txt

key-decisions:
  - "DI EthWatchService via shared_ptr — no singletons in dynamically loaded libraries"
  - "BridgeRelayerTestAccess friend accessor pattern for GTest private method access"
  - "Null TransactionManager tests for error path coverage without CRDTFixture complexity"

patterns-established:
  - "Friend accessor pattern: forward-declare test class outside sgns namespace, friend class ::TestAccess inside"
  - "Bridge event ABI decoding: values[0]=sender, values[1]=token_id, values[2]=amount, values[3]=srcChainID"

requirements-completed: []

# Metrics
duration: ~15min
completed: 2026-05-31
---

# Phase 02 Plan 02: Relayer — Burn Detection → MintFunds Summary

**BridgeRelayer with DI EthWatchService wiring burn events to MintFunds, 9 unit tests covering data extraction and OnWatchEvent behavior**

## Performance

- **Duration:** ~15 min (this execution — added OnWatchEvent tests)
- **Tasks:** 3 of 3
- **Files modified:** 2

## Accomplishments

- Added 4 OnWatchEvent behavior tests via BridgeRelayerTestAccess friend accessor
- Tests cover: null TransactionManager, insufficient ABI values, zero amount, uint256 overflow
- All 9 tests pass (5 data extraction + 4 behavior)

## Task Commits

1. **Task 1: BridgeRelayer with DI EthWatchService** - `84fc6fa4` (feat)
2. **Task 2: Wire into GeniusNode** - `84fc6fa4` (feat)
3. **Task 3: Unit Test** - `5f4fa265` (test, initial) + `c18a4a70` (test, OnWatchEvent behavior)

## Files Created/Modified

- `src/account/BridgeRelayer.hpp` - Added friend class ::BridgeRelayerTestAccess
- `test/src/account/bridge_relayer_test.cpp` - Added BridgeRelayerTestAccess accessor + 4 OnWatchEvent tests

## Decisions Made

- Used BridgeRelayerTestAccess friend accessor to test private OnWatchEvent without CRDTFixture
- Null TransactionManager tests provide error path coverage without full TM setup complexity

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Friend declaration namespace mismatch**
- **Found during:** Task 3 (test build)
- **Issue:** BridgeRelayerTestAccess in global namespace, friend declaration in sgns namespace — compiler rejected access
- **Fix:** Added forward declaration `class BridgeRelayerTestAccess;` outside namespace, used `friend class ::BridgeRelayerTestAccess;`
- **Files modified:** src/account/BridgeRelayer.hpp
- **Verification:** Build succeeds, all 9 tests pass
- **Committed in:** c18a4a70

---

**Total deviations:** 1 auto-fixed (1 namespace bug)
**Impact on plan:** Fix necessary for test compilation. No scope creep.

## Issues Encountered

None — plan executed cleanly after namespace fix.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- BridgeRelayer fully tested (9 cases, all passing)
- Ready for Phase 4 (E2E integration) or Phase 5 (startup catch-up)
- Codex review findings on PR #298 deferred to Phase 2 continuation (see memory: pr298-codex-review-findings)

---
*Phase: 02-relayer-burn-detection*
*Completed: 2026-05-31*
