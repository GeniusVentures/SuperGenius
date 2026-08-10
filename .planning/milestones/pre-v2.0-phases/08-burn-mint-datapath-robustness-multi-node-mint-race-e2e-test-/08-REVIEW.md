---
phase: 08-burn-mint-datapath-robustness-multi-node-mint-race-e2e-test-
reviewed: 2026-07-17T00:00:00Z
depth: standard
files_reviewed: 16
files_reviewed_list:
  - test/src/bridge_race/bridge_race_fixture.hpp
  - test/src/bridge_race/bridge_race_single_burn_test.cpp
  - test/src/bridge_race/bridge_race_batch_test.cpp
  - test/src/bridge_race/bridge_race_fault_rpc_test.cpp
  - test/src/bridge_race/bridge_race_fault_kill_test.cpp
  - test/src/bridge_race/bridge_race_fault_partition_test.cpp
  - test/src/bridge_race/CMakeLists.txt
  - test/src/CMakeLists.txt
  - test/src/mock/mock_rpc_config.hpp
  - test/src/mock/mock_rpc_transport.cpp
  - cmake/functions.cmake
  - fuzz/CMakeLists.txt
  - fuzz/fuzz_decode_log_data.cpp
  - fuzz/fuzz_parse_burn_event_values.cpp
  - fuzz/fuzz_mint_transaction_deserialize.cpp
  - test/CMakeLists.txt
findings:
  critical: 3
  warning: 5
  info: 4
  total: 12
status: issues_found
---

# Phase 8: Code Review Report

**Reviewed:** 2026-07-17
**Depth:** standard
**Files Reviewed:** 16
**Status:** issues_found

## Summary

Reviewed the Phase 8 mint-race e2e suite (fixture + 5 test binaries), the mock RPC transport divergence support, the libFuzzer harnesses, and CMake wiring. The CMake gating for SGNS_FUZZING is correct (option OFF by default; `add_subdirectory(fuzz)` only under the flag; `addfuzztarget` is defined unconditionally but never invoked outside the gated directory), and the fuzz harnesses are memory-safe. However, the core "stability window" double-mint guard in the race tests is a no-op due to a misuse of `EXPECT_WAIT_FOR_CONDITION`, meaning the flagship exactly-once assertion would pass even if dedup were broken via a delayed double-mint. The partition test's pre-heal window has the same defect, collapsing the partition duration to near zero. The mock transport throws uncaught exceptions on any non-receipt JSON-RPC request.

## Critical Issues

### CR-01: Stability window is a no-op — delayed double-mint would NOT be caught

**File:** `test/src/bridge_race/bridge_race_single_burn_test.cpp:76-80` (same pattern at `bridge_race_batch_test.cpp:113-117`)
**Issue:** The "stability/double-mint check" is implemented as:
```cpp
EXPECT_WAIT_FOR_CONDITION(
    [&]() { return s_nodes[0]->GetState() == GeniusNode::NodeState::READY; },
    BridgeRaceE2ETest::kRaceStabilityWindow, ... );
```
`expectWaitForCondition` (test/testutil/wait_condition.hpp) returns as soon as the condition is true, polling every 10 ms. Node 0 is already READY at this point (it was required READY in SetUpTestSuite and just serviced balance queries), so this "16 s stability window" completes in ~10 ms. The subsequent `EXPECT_EQ(final_balance, initial + kMintAmount)` therefore runs immediately after the first observed mint — a double-mint occurring on the watcher's *next* poll cycle (the exact failure mode `kRaceStabilityWindow` is documented to catch, fixture line 84-88: "> production poll interval") is never observed. The headline exactly-once invariant test passes even if dedup is broken in the delayed-double-mint way.
**Fix:** Actually elapse the window before the exact-balance re-check, e.g.:
```cpp
std::this_thread::sleep_for( BridgeRaceE2ETest::kRaceStabilityWindow );
// optionally still assert liveness afterwards:
EXPECT_EQ( s_nodes[0]->GetState(), GeniusNode::NodeState::READY );
```
Or invert the wait: wait for the *bad* condition (any node balance > initial + kMintAmount) for the full window and assert it timed out.

### CR-02: Partition test's "pre-heal partition window" collapses to ~10 ms — partition is healed before sub-groups can diverge

**File:** `test/src/bridge_race/bridge_race_fault_partition_test.cpp:101-105`
**Issue:** Same misuse as CR-01:
```cpp
EXPECT_WAIT_FOR_CONDITION(
    [&]() { return s_nodes[0]->GetState() == GeniusNode::NodeState::READY; },
    kPrePartitionHealWindow, ... );
```
Node 0 is already READY, so this returns on the first 10 ms poll and `AddPeers()` heal runs immediately after `ConfigureRpcEndpoint`. The 12-second `kPrePartitionHealWindow` — the interval during which "each sub-group's watcher poll [fires] independently while still partitioned" — never elapses. The test degenerates into the plain single-burn test with a momentary (<1 s) disconnect; D-11 (independent mint attempts in two partitioned sub-clusters, then CRDT reconciliation) is not actually exercised, yet the test will pass.
**Fix:** `std::this_thread::sleep_for( kPrePartitionHealWindow );` before the heal loop (optionally asserting each sub-group has independently observed the mint before healing, which would positively prove divergence occurred).

### CR-03: MockRpcTransport::call throws uncaught boost::json exceptions on any non-receipt request

**File:** `test/src/mock/mock_rpc_transport.cpp:38-42, 131-134`
**Issue:** `ExtractTxHash` is called unconditionally at the top of `call()` (even for `kTimeout`/`kConnectionRefused` behaviors):
```cpp
const auto &params = request.at("params").as_array();
return params.at(0).as_string().c_str();
```
- `request.at("params")` throws `std::out_of_range` if the request has no `params` key (e.g., `eth_blockNumber`, `eth_chainId`).
- `params.at(0)` throws if `params` is an empty array.
- `.as_string()` throws `std::invalid_argument` if the first param is an object (e.g., `eth_getLogs` filter objects, which a log-discovery watcher plausibly issues).

`call()` returns `std::optional<std::string>` — the intended failure signal is `nullopt`, not an exception — so any such throw propagates into production validator/watcher code with unknown consequences (crash or silently absorbed depending on caller). The fault_rpc tests only work today by accident of the validator issuing exclusively `eth_getTransactionReceipt`-shaped requests; any method-set drift turns this into a hard crash inside the quorum path.
**Fix:**
```cpp
std::optional<std::string> ExtractTxHash(const boost::json::object &request)
{
    const auto *params = request.if_contains("params");
    if (!params || !params->is_array() || params->as_array().empty()
        || !params->as_array()[0].is_string())
    {
        return std::nullopt;
    }
    return std::string(params->as_array()[0].as_string());
}
```
and have `call()` fall back to behavior-appropriate handling (e.g., return `std::nullopt` or a generic error response) when extraction fails.

## Warnings

### WR-01: fault_rpc test 2 can pass via test 1's leftover kSuccess transport — PUBLIC-pair quorum path may never be exercised

**File:** `test/src/bridge_race/bridge_race_fault_rpc_test.cpp:126-146`
**Issue:** Both TEST_Fs share the suite-level node 0 in one binary. `RpcDisagreementStillReachesCorrectQuorum` (runs first) leaves node 0's watcher configured with a DIRECT `kSuccess` mock. `RpcDisagreementPublicPairQuorumStillCorrect` then seeds its burn (line 135) *before* calling `ConfigureDivergentQuorum` (line 143). If the still-active test-1 watcher polls in that gap, the burn is discovered and minted under test 1's configuration (DIRECT-succeeds shortcut), and test 2's assertions pass without the PUBLIC-pair/REQ-SLOT-03 dedup quorum path ever being taken. The test can silently stop testing what its name claims.
**Fix:** Reconfigure the divergent quorum (kWrongLogs DIRECT) *before* seeding the burn in test 2, or verify via `MockRpcTransport` call counts that the PUBLIC slots actually served the deciding receipts.

### WR-02: `ASSERT_TRUE` inside void helper only aborts the helper, not the test

**File:** `test/src/bridge_race/bridge_race_fault_rpc_test.cpp:50-51, 77, 82, 87`
**Issue:** `ConfigureDivergentQuorum` is a free void function; gtest `ASSERT_*` inside it only `return`s from the helper. If `GetTransactionManager()` has no value, the helper records the failure and returns — the TEST_F body continues, then burns the full 90 s `kRaceNodeReadyTimeout` on a wait that can never succeed, producing a confusing secondary timeout failure.
**Fix:** Follow the gtest pattern — make the helper report and have callers guard:
```cpp
ConfigureDivergentQuorum(...);
if (::testing::Test::HasFatalFailure()) { return; }
```
or return a bool and `ASSERT_TRUE` it at the call site.

### WR-03: No ctest serialization — five 11-node suites share fixed ports 40041-40051 and an Anvil instance port

**File:** `test/src/bridge_race/CMakeLists.txt` (all five `set_tests_properties` calls), `test/src/bridge_race/bridge_race_fixture.hpp:74`
**Issue:** All five bridge_race binaries bind the same pubsub port block (`kNodePortBase = 40041` + 11) and each starts its own Anvil subprocess (which, per the shared `AnvilProcess` fixture, listens on a fixed RPC port). Under `ctest -j N` these tests run concurrently and collide on ports, producing flaky bind failures unrelated to the code under test. Only `TIMEOUT 180` is set; no `RESOURCE_LOCK` or `RUN_SERIAL`.
**Fix:** Add to each test:
```cmake
set_tests_properties(bridge_race_single_burn_test PROPERTIES
    TIMEOUT 180
    RESOURCE_LOCK "bridge_race_ports;anvil")
```
(or `RUN_SERIAL TRUE`).

### WR-04: Baseline balances read while Light nodes may not be READY

**File:** `test/src/bridge_race/bridge_race_fixture.hpp:255-262`; e.g. `bridge_race_single_burn_test.cpp:21, 87`
**Issue:** `SetUpTestSuite` only waits for node 0 (Full) to reach READY; Light nodes 1-10 may still be initializing when a TEST_F body starts. Each test reads `initial_balance` from node 0 only, but the final `EXPECT_EQ` compares *every* node's balance against that node-0 baseline. If any Light node's ledger view is behind node 0's at baseline time (or ahead after prior in-binary tests in fault_rpc), the exactly-once EXPECT_EQ can produce false failures — or, if a node's baseline was already inflated, mask a missing mint. The comment at fixture line 255-257 claims "Each TEST_F body will separately wait for the Light nodes," but no test body contains such a wait.
**Fix:** In SetUpTestSuite (or at the top of each test) wait for all 11 nodes READY, and/or capture a per-node baseline vector and assert `final[i] == baseline[i] + kMintAmount`.

### WR-05: SGNS_FUZZING with non-Clang compiler proceeds to a guaranteed build failure with only a STATUS message

**File:** `test/CMakeLists.txt:10-15`, `cmake/functions.cmake:42-50`
**Issue:** When `SGNS_FUZZING=ON` and the compiler is GCC/MSVC, the code prints `message(STATUS ...)` and still executes `add_subdirectory(fuzz)`. `addfuzztarget` unconditionally applies `-fsanitize=fuzzer,address`, which GCC does not support (`fuzzer` is Clang-only) and MSVC rejects outright — the configure succeeds but the build fails deep in compilation with an opaque flag error. Default builds (`SGNS_FUZZING=OFF`) are correctly unaffected.
**Fix:** Fail fast:
```cmake
if(SGNS_FUZZING)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "SGNS_FUZZING requires Clang (libFuzzer); current compiler is ${CMAKE_CXX_COMPILER_ID}")
    endif()
    add_subdirectory(...)
endif()
```

## Info

### IN-01: kWrongStatus/kWrongLogs canned-response paths never advance the response sequence

**File:** `test/src/mock/mock_rpc_transport.cpp:149-155, 158-166`
**Issue:** Unlike `kSuccess` (which cycles via `response_index_`), the wrong-status/wrong-logs branches always return `it->second.front()`, so multi-entry "ordered responses ... for stateful sequences (D-10)" (mock_rpc_config.hpp:30) silently degrade to a single response under those behaviors.
**Fix:** Factor the indexed lookup into a shared helper used by all three canned-response branches.

### IN-02: Mock bridge contract/topic0 constants duplicated between test and transport

**File:** `test/src/bridge_race/bridge_race_fault_rpc_test.cpp:35-37` vs `test/src/mock/mock_rpc_transport.cpp:30-34`
**Issue:** The literals `0x1234...7890` / `0x1234...1234` are copy-pasted; if the mock's private constants change, the fault_rpc endpoints silently stop matching and the tests time out with no compile-time signal. The comment acknowledges the mirroring but nothing enforces it.
**Fix:** Export the constants from `mock_rpc_transport.hpp` (or `mock_rpc_config.hpp`) and reference them from the test.

### IN-03: bridge_race/CMakeLists.txt is 5x copy-paste of an identical 25-line block

**File:** `test/src/bridge_race/CMakeLists.txt:1-137`
**Issue:** Five identical addtest/include/link/whole-archive/TIMEOUT stanzas differing only in target name. A future change (e.g., adding RESOURCE_LOCK per WR-03) must be applied 5 times.
**Fix:**
```cmake
foreach(t single_burn batch fault_rpc fault_kill fault_partition)
    set(target bridge_race_${t}_test)
    addtest(${target} bridge_race_${t}_test.cpp)
    ...
endforeach()
```
(with a per-target extra-libs list for `mock_rpc_transport`).

### IN-04: DeriveNodeKey does not check the secp256k1 group-order upper bound

**File:** `test/src/bridge_race/bridge_race_fixture.hpp:117-155`
**Issue:** Only the all-zero digest is rejected; a digest >= the secp256k1 order n would also be an invalid private key. Probability is ~2^-128 and the 11 fixed inputs are known-good, so this is theoretical — but the 8-attempt loop's fallback (`std::string(64, '1')`) would additionally collide across indices if ever reached.
**Fix:** Not required for correctness of this suite; if hardening, validate the digest against the curve order the same way the all-zero case is handled.

---

_Reviewed: 2026-07-17_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
