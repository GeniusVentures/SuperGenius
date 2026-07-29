---
phase: 05-startup-wiring-mock-rpc
plan: 02
subsystem: testing
tags: [mock, rpc, transport, gtest, json-rpc, evm]

# Dependency graph
requires:
  - phase: 05-startup-wiring-mock-rpc
    provides: JsonRpcTransport interface, RPC receipt parsing (json_rpc.hpp), eth_getTransactionReceipt
provides:
  - MockRpcTransport implementing eth::rpc::JsonRpcTransport — drop-in replacement for RpcHttpTransport
  - 6 configurable failure modes: success, timeout, connection_refused, bad_json, wrong_status, wrong_logs
  - Stateful per-tx_hash response sequences with modulo wrapping
  - Per-node JSON config parser (mock_rpc_config.json)
  - 15 behavioral GTest cases validating all modes, sequences, config, and reset
affects:
  - PublicChainInputValidator (transport_factory DI injection point)
  - RPC verification Tier 1 testing

# Tech tracking
tech-stack:
  added: []
  patterns:
    - MockRpcTransport : public JsonRpcTransport — drop-in DI pattern for testing
    - MockBehavior enum class with kCamelCase values per codebase conventions
    - Stateful response_index_ map keyed by tx_hash for ordered sequence testing

key-files:
  created:
    - test/src/mock/mock_rpc_transport.hpp - MockRpcTransport class (D-07, D-10)
    - test/src/mock/mock_rpc_transport.cpp - call() with 6 failure modes (D-13), LoadMockConfig (D-08, D-09)
    - test/src/mock/mock_rpc_config.hpp - MockEndpointConfig struct, MockBehavior enum, LoadMockConfig declaration
    - test/src/mock/mock_rpc_test.cpp - 15 GTest behavioral tests
    - test/src/mock/CMakeLists.txt - mock_rpc_transport library + mock_rpc_test target
  modified:
    - test/src/CMakeLists.txt - added add_subdirectory(mock)

key-decisions:
  - "Canonical mocks in test/src/mock/ with STATIC library — production binary never links mock code (D-16)"
  - "evmrelay cmake target (not evmrelay_eth) for dependency linking — discovered during build"
  - "Include paths relative to test/ root (src/mock/...) matching existing test conventions"
  - "T-05-03 mitigated: LoadMockConfig catches boost::json::parse exceptions, returns empty config"

requirements-completed:
  - REQ-MOCK-01
  - REQ-MOCK-02
  - REQ-MOCK-03

# Metrics
duration: 12min
completed: 2026-06-04
---

# Phase 5 Plan 2: MockRpcTransport with 6 Failure Modes Summary

**In-process MockRpcTransport implementing eth::rpc::JsonRpcTransport — 6 configurable failure modes, stateful tx_hash-keyed response sequences, per-node JSON config, and 15 behavioral GTest tests all passing.**

## Performance

- **Duration:** 12 min
- **Started:** 2026-06-04T17:13:26Z
- **Completed:** 2026-06-04T17:25:40Z
- **Tasks:** 3
- **Files modified:** 6

## Accomplishments
- MockRpcTransport class implementing JsonRpcTransport interface — drop-in replacement per D-07
- All 6 failure modes: success, timeout, connection_refused, bad_json, wrong_status, wrong_logs per D-13
- Stateful response sequences keyed by tx_hash with modulo wrapping per D-10
- LoadMockConfig() parses per-node mock_rpc_config.json via boost::json per D-08, D-09
- 15 GTest behavioral tests all passing: 6 failure modes + sequences + config + reset

## Task Commits

Each task was committed atomically:

1. **Task 1: Create MockRpcTransport class and config headers** - `8fa4dd98` (feat)
2. **Task 2: Implement MockRpcTransport::call() with all 6 failure modes** - `a0016a64` (feat)
3. **Task 3: Create mock RPC behavioral tests and CMake wiring** - `9e007de4` (feat)

## Files Created/Modified
- `test/src/mock/mock_rpc_config.hpp` - MockBehavior enum (6 values), MockEndpointConfig struct, LoadMockConfig declaration
- `test/src/mock/mock_rpc_transport.hpp` - MockRpcTransport final class implementing JsonRpcTransport
- `test/src/mock/mock_rpc_transport.cpp` - call() switch on 6 behaviors, LoadMockConfig JSON parser, ResetState/SetBehavior
- `test/src/mock/mock_rpc_test.cpp` - 15 GTest behavioral tests with MockRpcTest fixture
- `test/src/mock/CMakeLists.txt` - STATIC mock_rpc_transport library + mock_rpc_test executable target
- `test/src/CMakeLists.txt` - Added add_subdirectory(mock)

## Decisions Made
- Canonical mocks in `test/src/mock/` with STATIC library — production binary never links mock code (D-16)
- Corrected cmake target name from `evmrelay_eth` → `evmrelay` (actual library name in build system)
- Include paths follow existing test conventions: `src/mock/...` relative to `test/` root
- T-05-03 mitigated: LoadMockConfig wraps `boost::json::parse()` in try/catch, returns empty config on malformed JSON

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed include paths for cmake test root**
- **Found during:** Task 3 (CMake build)
- **Issue:** Include paths used `test/src/mock/...` prefix, but cmake's test include root is `test/`. Compiler couldn't find headers.
- **Fix:** Changed includes from `"test/src/mock/mock_rpc_transport.hpp"` to `"src/mock/mock_rpc_transport.hpp"` in 3 files
- **Files modified:** mock_rpc_transport.cpp, mock_rpc_transport.hpp, mock_rpc_test.cpp
- **Committed in:** 9e007de4 (Task 3 commit)

**2. [Rule 3 - Blocking] Corrected cmake target name**
- **Found during:** Task 3 (CMake linking)
- **Issue:** Plan specified `evmrelay_eth` as link target, but the actual library name in the build system is `evmrelay`
- **Fix:** Changed `target_link_libraries(mock_rpc_transport evmrelay_eth ...)` to `evmrelay`
- **Files modified:** test/src/mock/CMakeLists.txt
- **Committed in:** 9e007de4 (Task 3 commit)

**3. [Rule 3 - Blocking] Resolved missing spdlog transitive dependency**
- **Found during:** Task 3 (CMake build)
- **Issue:** Initial build failed with `spdlog/fmt/ostr.h` not found — `evmrelay_eth` targets link `spdlog::spdlog` as PRIVATE, not propagated
- **Fix:** Using `evmrelay` target (fix #2) resolved this — `evmrelay` links `spdlog::spdlog` PUBLICly
- **Files modified:** test/src/mock/CMakeLists.txt
- **Committed in:** 9e007de4 (Task 3 commit)

---

**Total deviations:** 3 auto-fixed (3 blocking)
**Impact on plan:** All fixes necessary for compilation/linking. No scope creep.

## Issues Encountered
- Build system uses `build/OSX/` as cmake source root with `PROJECT_ROOT` pointing to repo root — required `cmake -B build/OSX/Debug -S build/OSX` to reconfigure after adding new subdirectory
- Test binary registered by CTest as `mock_rpc_test` (lowercase), not `MockRpcTest` — verified with `ctest -R mock_rpc_test`

## User Setup Required
None — no external service configuration required.

## Next Phase Readiness
- MockRpcTransport ready for DI injection into PublicChainInputValidator::VerifyPublicChainSmartContract (Plan 05-03)
- All 15 behavioral tests passing; ready for integration testing with the full validator pipeline
- Production binary is mock-free per D-16 — `mock_rpc_transport` library only linked by test targets

---
*Phase: 05-startup-wiring-mock-rpc*
## Self-Check: PASSED

- [x] All 6 key files exist on disk
- [x] All 3 task commits (8fa4dd98, a0016a64, 9e007de4) found in git log
- [x] All 15 mock_rpc_test GTest cases pass via ctest

---

*Completed: 2026-06-04*
