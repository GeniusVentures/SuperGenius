# Phase 8: Burn/Mint Datapath Robustness - Pattern Map

**Mapped:** 2026-07-16
**Files analyzed:** 10 (new/modified)
**Analogs found:** 10 / 10

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `test/src/bridge_race/bridge_race_fixture.hpp` | test-fixture | request-response (cluster bootstrap) | `test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp` (SetUpTestSuite) | exact |
| `test/src/bridge_race/bridge_race_single_burn_test.cpp` | test | event-driven (watcher race) | `test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp` (Test A) | exact |
| `test/src/bridge_race/bridge_race_batch_test.cpp` | test | event-driven (watcher race, batch) | `test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp` (kNumCatchupBurns loop) | exact |
| `test/src/bridge_race/bridge_race_fault_kill_test.cpp` | test | event-driven + lifecycle | `test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp` (TearDownTestSuite node.reset()) | role-match |
| `test/src/bridge_race/bridge_race_fault_rpc_test.cpp` | test | request-response (fault injection) | `test/src/mock/mock_rpc_transport.{hpp,cpp}` + catchup test's `WeightedRpcEndpoint` slot setup | exact |
| `test/src/bridge_race/bridge_race_fault_partition_test.cpp` | test | event-driven (pubsub) | `bridge_anvil_e2e_test.cpp` (AddPeers mesh bootstrap) | role-match |
| `test/src/bridge_race/CMakeLists.txt` | config | build registration | `test/src/bridge_e2e/CMakeLists.txt` | exact |
| `test/src/mock/mock_rpc_config.hpp` (extend) | utility | config/factory | existing file itself (additive helper function) | exact |
| `cmake/functions.cmake` `addfuzztarget()` | config | build tooling | `addtest()`/`addtest_part()` in same file | exact |
| `fuzz/*.cpp` + `fuzz/CMakeLists.txt` | utility (fuzz harness) | transform (bytes -> parsed struct) | `src/account/BridgeRelayer.cpp::ParseBurnEventValues` (function under test, not a test analog — no prior fuzz harness exists) | no analog (new pattern) |

## Pattern Assignments

### `test/src/bridge_race/bridge_race_fixture.hpp` (test-fixture, cluster bootstrap)

**Analog:** `test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp` lines 156-491

**Imports pattern** (lines 29-56):
```cpp
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <chrono>
#include <boost/dll.hpp>
#include <spdlog/spdlog.h>
#include <ProofSystem/EthereumKeyGenerator.hpp>
#include "account/ChainContractPair.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "watcher/impl/bridge_catchup_watcher.hpp"
#include "testutil/wait_condition.hpp"
#include "anvil_fixture.hpp"
```
NOTE: for the race suite, also `#include "anvil_fixture.hpp"` via a relative path back to `../bridge_e2e/anvil_fixture.hpp` OR copy/symlink — verify at plan time whether CMake include dirs already expose `bridge_e2e/` to `bridge_race/`.

**Node-array bootstrap pattern** (lines 156-272, static members + `s_configs` array): Extend the 3-element pattern to `kNodeCount = 11` — same `std::array<GeniusNodeConfig, kNodeCount>`, same `kAnvilAccountHexKeys[]` array (extend from 3 to 11 entries per D-06/Open-Question-1), same per-node `BaseWritePath`/port/dir naming convention (`kNodeMainPort = 40031u` style, sequential).

**Skip-cleanly + Anvil startup** (lines 306-341): Copy verbatim — `AnvilAvailable()`/`CastAvailable()` GTEST_SKIP, `s_anvil.Start(fork_url)`, `WaitForReady()`, `FundAccount0WithGnus`, fork-block capture via `cast block-number`.

**Race-critical divergence from catchup suite (D-03):** Do NOT stagger `ConfigureRpcEndpoint` calls with waits between nodes (see catchup suite lines 440-463, which loops but is still followed only by a single READY wait — that part IS the pattern to copy). Seed the burn(s) BEFORE any `ConfigureRpcEndpoint` call (catchup suite seeds at lines 343-366, well before node creation at line 397 — same ordering to replicate), then create all N nodes, register genesis validators (lines 412-420), bootstrap PubSub mesh (lines 423-425), wait node_main READY (lines 431-435), then loop `ConfigureRpcEndpoint` across ALL nodes back-to-back with NO per-node wait (this is the one line to change vs. catchup — catchup already does the loop for 3 nodes at lines 454-458, extend it to `kNodeCount=11` unchanged), THEN wait for all light nodes READY once (mirrors lines 466-474).

**Teardown pattern** (lines 479-491): Copy verbatim — `node.reset()` for each, `s_anvil.Stop()`, `std::filesystem::remove_all` per BaseWritePath. This is also the direct analog for `bridge_race_fault_kill_test.cpp`'s D-10 node-kill mechanism (call `.reset()` on one node mid-test instead of at TearDown).

---

### `test/src/bridge_race/bridge_race_single_burn_test.cpp` / `bridge_race_batch_test.cpp` (test, event-driven watcher race)

**Analog:** `test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp` `TEST_F(..., FullScanFromGenesisNoErrors)` lines 502-531 and the `kNumCatchupBurns` seeding loop lines 356-365.

**Core exactly-once assertion pattern** (lines 506-530):
```cpp
const std::string dest_addr       = node_main->GetAddress();
const uint64_t    initial_balance = node_main->GetBalance( dest_addr );

EXPECT_WAIT_FOR_CONDITION(
    [&]() { return node_main->GetBalance( dest_addr ) >= initial_balance + kNumCatchupBurns * kMintAmount; },
    kCatchupMintTimeout,
    "Catch-up scan must mint all pre-node burns",
    nullptr );

EXPECT_GE( node_main->GetBalance( dest_addr ), initial_balance + kNumCatchupBurns * kMintAmount );
```
For the race test (D-02), adapt to: (1) assert balance delta == burn amount from EVERY node (`node_main`, all light nodes) via `GetBalance(dest_addr)` reads across the array, not just `node_main`; (2) add the stability check pattern from Test C (lines 682-701) — read `balance_before`, wait one extra poll window (`kCatchupPollIntervalGate`-equivalent), read `balance_after`, `EXPECT_EQ` — to catch double-mint.

**Batch seeding loop** (lines 354-365) — reuse directly for D-04's 3-5 burn batch test, targeting Light-node destination addresses (D-07) instead of `kAnvilAccountHexKeys[0]`'s derived address.

**Anti-pattern warning (critical, from RESEARCH.md Pitfall 1):** do not insert `ASSERT_WAIT_FOR_CONDITION` between per-node `ConfigureRpcEndpoint` calls — this is the one place the race test must diverge structurally from the fixture pattern above.

---

### `test/src/bridge_race/bridge_race_fault_kill_test.cpp` (test, node lifecycle fault)

**Analog:** `TearDownTestSuite` `node.reset()` pattern, `bridge_anvil_catchup_e2e_test.cpp` lines 479-491.

**Core pattern:** Trigger the race (as in single_burn_test), then at a chosen point (e.g., immediately after seeding, before the killed node's watcher completes its poll) call `node_to_kill.reset()` — same statement as line 482/483/484, just invoked mid-test instead of at suite teardown. Then assert remaining nodes still converge to exactly-once mint using the same `EXPECT_WAIT_FOR_CONDITION` balance-delta pattern.

---

### `test/src/bridge_race/bridge_race_fault_rpc_test.cpp` (test, RPC quorum disagreement)

**Analog:** `test/src/mock/mock_rpc_transport.hpp` + `mock_rpc_config.hpp` (existing, extend per D-09) + the `WeightedRpcEndpoint` 3-slot pattern in `bridge_anvil_catchup_e2e_test.cpp` lines 440-463.

**MockRpcTransport class** (full file, `test/src/mock/mock_rpc_transport.hpp`):
```cpp
class MockRpcTransport final : public eth::rpc::JsonRpcTransport
{
public:
    explicit MockRpcTransport( const MockEndpointConfig &config );
    [[nodiscard]] std::optional<std::string> call( const boost::json::object &request ) override;
    void ResetState();
    void SetBehavior( MockBehavior b );
    size_t CallCount() const { return call_count_; }
private:
    MockEndpointConfig            config_;
    size_t                        call_count_ = 0;
    std::map<std::string, size_t> response_index_;
};
```

**MockEndpointConfig / MockBehavior** (`test/src/mock/mock_rpc_config.hpp` lines 15-31):
```cpp
enum class MockBehavior { kSuccess, kTimeout, kConnectionRefused, kBadJson, kWrongStatus, kWrongLogs };
struct MockEndpointConfig
{
    std::string  url;
    MockBehavior behavior = MockBehavior::kSuccess;
    std::map<std::string, std::vector<std::string>> responses;
};
```

**TransportFactory DI seam** (`src/account/PublicChainInputValidator.hpp` lines 59-60, 153-156):
```cpp
using TransportFactory = std::function<std::unique_ptr<eth::rpc::JsonRpcTransport>(
    const std::string &url, std::chrono::seconds timeout )>;
void SetTransportFactory( TransportFactory factory ) { transport_factory_ = std::move( factory ); }
```

**3-slot WeightedRpcEndpoint pattern to diverge** (`bridge_anvil_catchup_e2e_test.cpp` lines 441-458 — same-URL pattern to AVOID per RESEARCH.md Pitfall 3; use DISTINCT URLs per slot instead):
```cpp
sgns::WeightedRpcEndpoint ep_direct;
ep_direct.url                     = s_anvil.RpcUrl();   // race test: use "mock://direct" instead
ep_direct.consensus_weight        = 100;
ep_direct.bridge_contract_address = sgns::test::anvil::kSepoliaBridgeContractLower;
ep_direct.accepted_topic0_hashes  = { sgns::test::anvil::BridgeEventTopic0() };
sgns::WeightedRpcEndpoint ep_public1 = ep_direct; ep_public1.consensus_weight = 0; // url = "mock://public1"
sgns::WeightedRpcEndpoint ep_public2 = ep_direct; ep_public2.consensus_weight = 0; // url = "mock://public2"
std::vector<sgns::WeightedRpcEndpoint> anvil_eps{ ep_direct, ep_public1, ep_public2 };
for ( unsigned int i = 0u; i < kNodeCount; ++i ) { nodes[i]->ConfigureRpcEndpoint( chain_id, anvil_eps ); }
```

**Factory-dispatch pattern to add (RESEARCH.md Pattern 3):**
```cpp
validator->SetTransportFactory(
    [=]( const std::string &url, std::chrono::seconds timeout ) -> std::unique_ptr<eth::rpc::JsonRpcTransport>
    {
        if ( url == direct_cfg.url )  return std::make_unique<sgns::test::MockRpcTransport>( direct_cfg );
        if ( url == public1_cfg.url ) return std::make_unique<sgns::test::MockRpcTransport>( public1_cfg );
        return std::make_unique<sgns::test::MockRpcTransport>( public2_cfg );
    } );
```

**Extension needed:** add a small helper in `mock_rpc_config.hpp` (e.g. `BuildDivergentSlotConfigs(...)`) returning 3 `MockEndpointConfig`s with distinct URLs/behaviors — additive, no rewrite of existing `MockRpcTransport`/`MockEndpointConfig`.

---

### `test/src/bridge_race/bridge_race_fault_partition_test.cpp` (test, pubsub partition)

**Analog:** `AddPeers` mesh-bootstrap pattern, `bridge_anvil_catchup_e2e_test.cpp` lines 423-425 (also see `bridge_anvil_e2e_test.cpp` for the 3-node mesh variant).

**Partition/heal seam** (RESEARCH.md, confirmed from `3rdparty/ipfs-pubsub/.../gossip_pubsub.hpp` and `3rdparty/libp2p/include/libp2p/host/host.hpp:171`):
```cpp
void AddPeers( const std::vector<std::string> &bootstrapPeers );   // heal / mesh bootstrap (existing call, reuse verbatim)
virtual void disconnect( const peer::PeerId &peer_id ) = 0;        // partition (via GetHost())
```
Usage sketch:
```cpp
node_light_5->GetPubSub()->GetHost()->disconnect( peer_id_of( node_full ) );
// ... run test under partition ...
node_light_5->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetLocalAddress() } ); // heal — same call as fixture bootstrap
```
**Open item (LOW confidence, flag for implementation):** exact `PeerId` extraction API for a remote node from the local node's perspective is unconfirmed — plan an investigation spike task before writing partition assertions (per RESEARCH.md Open Question 2).

---

### `test/src/bridge_race/CMakeLists.txt` (config, build registration)

**Analog:** `test/src/bridge_e2e/CMakeLists.txt` lines 72-94 (`addtest(bridge_anvil_catchup_e2e_test ...)` block).

```cmake
addtest(bridge_anvil_catchup_e2e_test
    bridge_anvil_catchup_e2e_test.cpp
    )

target_include_directories(bridge_anvil_catchup_e2e_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(bridge_anvil_catchup_e2e_test
    genius_node_test
    json_secure_storage
    )
if(WIN32)
    target_link_options(bridge_anvil_catchup_e2e_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(bridge_anvil_catchup_e2e_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(bridge_anvil_catchup_e2e_test PUBLIC
        "-Wl,--whole-archive" "$<TARGET_FILE:genius_node_test>" "-Wl,--no-whole-archive")
endif()
```
Repeat this block per new race-suite binary (`bridge_race_single_burn_test`, `bridge_race_batch_test`, `bridge_race_fault_kill_test`, `bridge_race_fault_rpc_test`, `bridge_race_fault_partition_test`), each its own `addtest()` call (D-15: own ctest target). Per D-15/Pitfall 2, add a `set_tests_properties(<name> PROPERTIES TIMEOUT <seconds>)` line after each `addtest()` call — not present in the existing bridge_e2e file, must be added new (extended timeout for 11-node startup cost).

---

### `cmake/functions.cmake` `addfuzztarget()` (config, build tooling)

**Analog:** `addtest()`/`addtest_part()` in the same file, lines 8-40.

```cmake
function(addtest test_name)
    add_executable(${test_name} ${ARGN})
    addtest_part(${test_name} ${ARGN})
    target_link_libraries(${test_name}
        GTest::gtest_main
        GTest::gmock_main
    )
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/xunit)
    set(xml_output "--gtest_output=xml:${CMAKE_BINARY_DIR}/xunit/xunit-${test_name}.xml")
    add_test(
        NAME ${test_name}
        COMMAND $<TARGET_FILE:${test_name}> ${xml_output}
    )
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_bin
        ARCHIVE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/test_lib
        LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/test_lib
    )
    disable_clang_tidy(${test_name})
endfunction()
```
`addfuzztarget()` should mirror this shape but: (1) gate the whole function body (or its call sites) behind `if(SGNS_FUZZING)`; (2) add `-fsanitize=fuzzer,address` compile+link flags instead of GTest link libs; (3) register a `ctest` smoke-run entry per D-14 (`add_test` invoking the fuzz binary with `-max_total_time=60` replaying the checked-in corpus) rather than a bare GTest XML run; (4) route output to a `fuzz_bin`/`fuzz_corpus` directory analogous to `test_bin`. No GTest linkage. Place immediately after `addtest_part()` (line 40) in the same file, or in a new `cmake/fuzz_functions.cmake` included conditionally — Claude's Discretion per CONTEXT.md.

---

### `fuzz/*.cpp` (fuzz harnesses, transform bytes -> struct)

**No direct test analog exists** (first fuzz infra in the repo, per RESEARCH.md "State of the Art"). Pattern source is the FUNCTION UNDER TEST itself, which already follows a safe `outcome::result` + `std::holds_alternative` pattern to imitate in the harness's success/failure handling (NOT to imitate — this is what's being fuzzed):

```cpp
// Source: src/account/BridgeRelayer.cpp:210-234 — ParseBurnEventValues signature + safe variant pattern
outcome::result<BurnEventParams> BridgeRelayer::ParseBurnEventValues(
    const std::vector<eth::abi::AbiValue> &values )
{
    if ( values.size() < kExpectedMinParams ) { return outcome::failure( std::errc::invalid_argument ); }
    if ( !std::holds_alternative<intx::uint256>( values[kTokenIdIndex] ) )
    { return outcome::failure( std::errc::invalid_argument ); }
    // ... (see full file for kAmountIndex, kSgnsDestinationIndex, kDestinationYOddIndex handling)
}
```
Each `LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)` harness must: decode raw bytes into the function's expected input type (raw ABI log bytes -> `eth::abi::decode_log` -> `std::vector<AbiValue>` for the `ParseBurnEventValues` target; raw bytes directly for `MintTransactionV2::DeSerializeByteVector`), call the target function inside a try/catch (or rely on `outcome::result` — no throw expected per Pitfall 4), and return 0 unconditionally (libFuzzer convention — crashes are what ASan reports, not return codes). Per RESEARCH.md Pitfall 4, before writing harnesses, grep the 3 target functions for unchecked `[]`/`.at()`-free indexing on fuzzer-controlled buffers.

---

## Shared Patterns

### Wait-condition macros
**Source:** `testutil/wait_condition.hpp`, used throughout `bridge_anvil_catchup_e2e_test.cpp` (e.g. lines 431-435, 518-522, 691-695)
**Apply to:** All new race/fault test files — use `ASSERT_WAIT_FOR_CONDITION` for setup-blocking waits, `EXPECT_WAIT_FOR_CONDITION` for assertion-style waits that should still let the test report failure detail.
```cpp
ASSERT_WAIT_FOR_CONDITION(
    [&]() { return node_main->GetState() == GeniusNode::NodeState::READY; },
    kNodeReadyTimeout,
    "node_main READY",
    nullptr );
```

### Secure storage / chainlist isolation
**Source:** `bridge_anvil_catchup_e2e_test.cpp` lines 302-304 (MemorySecureStorage factory), 381-386 (chainlist_fetcher lambda)
**Apply to:** All race-suite fixture SetUpTestSuite methods — same `SetSecureStorageFactory` call and `SetChainlistFetcher` lambda returning only the local Anvil endpoint, to keep tests hermetic from real network fetches.

### Foundry skip-cleanly guard
**Source:** `bridge_anvil_catchup_e2e_test.cpp` lines 306-310, 322-328
**Apply to:** All e2e test files in the new suite (D-16) — `GTEST_SKIP()` when `AnvilAvailable()`/`CastAvailable()` are false or funding fails.

### Genesis validator + mesh bootstrap ordering
**Source:** `bridge_anvil_catchup_e2e_test.cpp` lines 412-425
**Apply to:** `bridge_race_fixture.hpp` — `SetAuthorizedFullNodeAddress` / `SetAdditionalGenesisValidatorAddresses` must be called immediately after node creation and BEFORE genesis block creation; `AddPeers` mesh bootstrap follows.

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `fuzz/CMakeLists.txt` | config | build tooling | First fuzzing build config in the repo; no prior `-DSGNS_FUZZING` gated target exists — use `addtest()`/`addfuzztarget()` conventions from `cmake/functions.cmake` as the structural template, but the gating/flag logic itself is new |
| `fuzz/corpus/**` seed files | data | batch | No prior corpus convention in repo; Claude's Discretion per CONTEXT.md for layout |

## Metadata

**Analog search scope:** `test/src/bridge_e2e/`, `test/src/mock/`, `cmake/functions.cmake`, `src/account/`, `src/blockchain/`, `3rdparty/ipfs-pubsub`, `3rdparty/libp2p`
**Files scanned:** 7 read in full/targeted (bridge_anvil_catchup_e2e_test.cpp, mock_rpc_transport.hpp, mock_rpc_config.hpp, cmake/functions.cmake, bridge_e2e/CMakeLists.txt, BridgeRelayer.cpp excerpt, PublicChainInputValidator.hpp excerpt)
**Pattern extraction date:** 2026-07-16
