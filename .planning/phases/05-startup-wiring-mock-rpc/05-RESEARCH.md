# Phase 5: Startup Wiring + Mock RPC Transport - Research

**Researched:** 2026-06-04
**Domain:** C++ node startup lifecycle, RPC transport mock injection, UTXO state management
**Confidence:** HIGH

## Summary

Phase 5 wires two dead code paths — `BridgeRelayer::Start()` and `InitializeRpcEndpoints()` — into the GeniusNode startup state machine so the burn→mint pipeline actually runs in production nodes. It adds an in-process mock RPC transport (`JsonRpcTransport` interface) for Tier 1 verification testing, replaces 8 UTXOManager foreign-address guards so all nodes track UTXOs for all peers, and adds a `UTXO_RESERVED` lifecycle state for burn UTXOs.

The key architectural insight is that `PublicChainInputValidator::VerifyPublicChainSmartContract()` constructs `RpcHttpTransport` directly (hard reference, `PublicChainInputValidator.cpp:183`) — mock injection requires replacing this hard construction with a pluggable transport factory or a `std::function<JsonRpcTransport*>` indirection. The existing `ChainRpcEndpointProvider` already provides a DI-like injection point via `SetRpcEndpoints()`, but the transport is created at validation time, not at configuration time.

**Primary recommendation:** Add a `TransportFactory` callable to `WeightedRpcEndpoint` (or `PublicChainInputValidator`) that defaults to creating a real `RpcHttpTransport` — mock tests override this factory during setup. This follows the existing DI patterns in the codebase and requires minimal structural changes.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| BridgeRelayer::Start() wiring | API/Backend (GeniusNode) | — | Node lifecycle state machine owns subsystem initialization ordering |
| InitializeRpcEndpoints() wiring | API/Backend (GeniusNode) | — | Same node lifecycle path; both fire during CREATING |
| Mock RPC transport | Test/Infrastructure | API/Backend | Implements JsonRpcTransport interface used by PublicChainInputValidator |
| UTXO guard removal | Database/Storage (UTXOManager) | — | UTXOManager owns foreign-address gating logic |
| UTXO_RESERVED state | Database/Storage (UTXOManager) | Consensus (TransactionManager) | UTXOManager owns lifecycle state; TransactionManager coordinates transitions |
| UTXOType marker | Database/Storage (UTXOManager) | — | UTXOManager owns UTXOEntry schema |
| Startup catch-up scan | API/Backend (GeniusNode/BridgeRelayer) | RPC (PublicChainInputValidator) | Node orchestrates scan; validator provides RPC verification |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Boost.Beast (via evmrelay) | 1.87+ [VERIFIED: codebase CMakeLists.txt] | HTTP client for real RPC transport | Already linked; `RpcHttpTransport` uses it |
| Boost.JSON (via evmrelay) | 1.87+ [VERIFIED: codebase CMakeLists.txt] | JSON-RPC request/response serialization | Already linked; `make_json_rpc_request`, `parse_*` functions use it |
| Google Test (gtest) | 1.14+ [VERIFIED: test CMakeLists.txt] | Unit + integration test framework | Existing test infrastructure uses it |
| evmrelay (submodule) | 59d1ed2 [VERIFIED: ROADMAP.md] | RPC transport, chain list, eth watch, ABI decode | Provides `JsonRpcTransport`, `RpcHttpTransport`, `EthWatchService`, `parse_transaction_receipt_response()` |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| nlohmann/json (or boost::json) | — | Mock RPC config file parsing | Parsing `mock_rpc_config.json` |
| std::filesystem | C++17 | Config file path resolution | Resolving `<binary_dir>/mock_rpc_config.json` |

**Installation:**
No new external packages. All dependencies are already in the build tree (Boost, gtest, evmrelay submodule).

## Package Legitimacy Audit

> No external packages installed by this phase. All code is internal C++ sources in the existing build tree.

| Package | Registry | Age | Downloads | Source Repo | slopcheck | Disposition |
|---------|----------|-----|-----------|-------------|-----------|-------------|
| — | — | — | — | — | — | N/A (no external packages) |

**Packages removed due to slopcheck [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** none

## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** `BridgeRelayer::Start()` changes from single chain/contract to multi-chain — accepts a set of `(chain_name, contract_address)` pairs. [VERIFIED: CONTEXT.md]
- **D-02:** Chain data sourced from `chains_config.json` with optional `bridge_contract_address` field per chain. [VERIFIED: CONTEXT.md]
- **D-03:** Contract address mapping for 8 chains (ethereum-mainnet, ethereum-sepolia, bnb-smart-chain, bnb-smart-chain-testnet, polygon-mainnet, polygon-amoy, base-mainnet, base-sepolia). [VERIFIED: CONTEXT.md]
- **D-04:** Both `InitializeRpcEndpoints()` and `BridgeRelayer::Start()` fire as a single async function from `CREATING` state. Non-blocking. Ordering: Start() only fires after endpoints are ready. [VERIFIED: CONTEXT.md]
- **D-05:** Config sharing: both consume `chains_config.json`. [VERIFIED: CONTEXT.md]
- **D-06:** API-key direct endpoints via `ChainRpcProviderConfig.direct_endpoints` (already supported). [VERIFIED: CONTEXT.md]
- **D-07:** Mock implements `JsonRpcTransport` interface — drop-in replacement. DI via `ChainRpcEndpointProvider` → `SetRpcEndpoints()`. [VERIFIED: CONTEXT.md]
- **D-08:** Per-node JSON config at `<binary_dir>/mock_rpc_config.json`. [VERIFIED: CONTEXT.md]
- **D-09:** Config fields: `url`, `behavior` (6 failure modes), `responses` (ordered list of canned responses). [VERIFIED: CONTEXT.md]
- **D-10:** Stateful sequences: ordered responses keyed by `tx_hash`. Resets per test case. [VERIFIED: CONTEXT.md]
- **D-11:** Canned response format: raw JSON matching `eth_getTransactionReceipt` response. [VERIFIED: CONTEXT.md]
- **D-12:** Multi-chain support through `chains_config.json`. [VERIFIED: CONTEXT.md]
- **D-13:** All 6 failure modes: success, timeout, connection_refused, bad_json, wrong_status, wrong_logs. [VERIFIED: CONTEXT.md]
- **D-14:** Test executables default to mock. Both mock + real compile into test binary — selection is runtime via DI. [VERIFIED: CONTEXT.md]
- **D-15:** Real RPC opt-in via runtime switch (env var or gtest flag). No compile flags affecting entire build. [VERIFIED: CONTEXT.md]
- **D-16:** Production `genius_node` binary uses real RPC only. [VERIFIED: CONTEXT.md]
- **D-17:** Remove 8 `!is_full_node_ && address != address_` guards in `UTXOManager.cpp`. [VERIFIED: CONTEXT.md]
- **D-18:** Add `UTXO_RESERVED` to `UTXOState` enum. Lifecycle: READY → RESERVED → CONSUMED. [VERIFIED: CONTEXT.md]
- **D-19:** Add `UTXOType` field to `UTXOEntry` to distinguish bridge/burn UTXOs. [VERIFIED: CONTEXT.md]
- **D-20:** Startup catch-up scan: probe RPC for historical burns, match against UTXO set, insert missing as READY. [VERIFIED: CONTEXT.md]
- **D-21:** Best-effort multi-chain Start(). Skip failed chains, log warning. [VERIFIED: CONTEXT.md]

### the agent's Discretion
- `BridgeRelayer` internal refactoring from single `watch_id_` to per-chain watch ID tracking
- `InitializeRpcEndpoints()` implementation details — parsing `chains_config.json`, populating `ChainRpcEndpointProvider`
- Mock transport class name and file location within codebase conventions
- Exact `UTXOType` enum design and placement
- Startup scan depth and query mechanism
- GTest flag name for real-RPC opt-in

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope.

## Phase Requirements

No phase requirement IDs were mapped for this phase.

## Architecture Patterns

### System Architecture Diagram

```
chains_config.json ──────────────────────────────────────────────────────────────┐
  │ (reads on startup)                                                           │
  ▼                                                                              │
GeniusNode::New() [CREATING state]                                               │
  │                                                                              │
  ├── Create BridgeRelayer ─────────────────────────────────────────┐            │
  │    ├── Start() ← iterates chains with bridge_contract_address   │            │
  │    │    └── Registers BridgeSourceBurned watch per chain         │            │
  │    │         │ (EthWatchService::watch_event)                    │            │
  │    │         ▼                                                   │            │
  │    │    Burn detected → OnWatchEvent → MintFunds()              │            │
  │    │         │                                                   │            │
  │    │         ▼                                                   │            │
  │    │    MintFunds → MintTransactionV2 → UTXO commit             │            │
  │    │         │                                                   │            │
  │    │         ▼                                                   │            │
  │    │    Consensus round ───> PublicChainInputValidator           │            │
  │    │                              │                              │            │
  │    └── InitializeRpcEndpoints()   │                              │            │
  │         ├── Reads chains_config.json                             │            │
  │         ├── ChainRpcEndpointProvider::Initialize()              │            │
  │         │    └── SetRpcEndpoints(chain_id, endpoints)           │            │
  │         │         │                                              │            │
  │         │         ▼                                              │            │
  │         │    WeightedRpcEndpoint[] ───→ VerifyPublicChain... ◄──┘            │
  │         │                              │                                      │
  │         │                              ├── [prod] RpcHttpTransport           │
  │         │                              ├── [test] MockRpcTransport ◄── DI     │
  │         │                              │                                      │
  │         │                              ▼                                      │
  │         │    parse_transaction_receipt_response(json)                         │
  │         │    Verify logs match bridge_contract_address + event_topic0         │
  │         │    Weighted consensus ≥ 75%                                         │
  │         │                                                                     │
  │         └── bridge_contract_address + event_topic0 per chain                 │
  │                                                                              │
  ▼  [INITIALIZING_TRANSACTIONS state]                                           │
Startup catch-up scan                                                            │
  ├── GetLastMintMessage date from CRDT                                          │
  ├── Probe RPC for burns after that date                                        │
  ├── Match against known UTXO set                                               │
  └── Insert missing burns as READY → trigger MintFunds                          │
```

### Recommended Project Structure (new/modified files)

```
src/account/
├── BridgeRelayer.hpp           # [MODIFY] Multi-chain watch_id tracking
├── BridgeRelayer.cpp           # [MODIFY] Start(vector<ChainContract>)
├── ChainRpcEndpointProvider.hpp # [MODIFY] bridge_contract_address, event_topic0
├── ChainRpcEndpointProvider.cpp # [MODIFY] Populate contract/topic0 on WeightedRpcEndpoint
├── GeniusNode.cpp              # [MODIFY] Wire InitializeAndStartBridge() into CREATING
├── GeniusNode.hpp              # [MODIFY] Add InitializeAndStartBridge() declaration
├── PublicChainInputValidator.hpp # [MODIFY] Transport factory injection
├── PublicChainInputValidator.cpp # [MODIFY] Use transport factory instead of hard RpcHttpTransport
├── UTXOManager.hpp             # [MODIFY] UTXO_RESERVED state, UTXOType enum, UTXOEntry field
├── UTXOManager.cpp             # [MODIFY] Remove 8 guards, handle RESERVED state
test/src/
├── mock/
│   ├── mock_rpc_transport.hpp  # [NEW] MockRpcTransport implements JsonRpcTransport
│   ├── mock_rpc_transport.cpp  # [NEW] Implementation
│   └── mock_rpc_config.hpp     # [NEW] Config parser
├── account/
│   └── bridge_relayer_test.cpp # [MODIFY] Multi-chain Start() tests
└── startup/
    ├── startup_wiring_test.cpp  # [NEW] Test InitializeAndStartBridge, catch-up scan
    └── mock_rpc_test.cpp       # [NEW] Mock transport behavioral tests
evmrelay/examples/
└── chains_config.json           # [MODIFY] Add optional bridge_contract_address to chains
```

### Pattern 1: Transport Factory DI (Mock Injection Point)

**What:** Instead of constructing `RpcHttpTransport` directly, `PublicChainInputValidator` uses a callable factory. In production, the factory creates `RpcHttpTransport`. In tests, the factory creates a `MockRpcTransport`.

**When to use:** Any code path that needs to vary RPC transport behavior at runtime without compile flags. This is the only place RPC calls are made in the validation path.

**Existing code to modify** (`PublicChainInputValidator.cpp:181-183`):
```cpp
// Current (hard reference):
eth::rpc::RpcHttpTransportOptions opts;
opts.timeout = kTimeout;
eth::rpc::RpcHttpTransport transport(ep.url, opts);
```

**Refactored approach:**
```cpp
// Source: evmrelay/include/eth/rpc_http_transport.hpp + rpc_receipt_source.hpp
// Transport factory type:
using TransportFactory = std::function<std::unique_ptr<eth::rpc::JsonRpcTransport>(
    const std::string& url, 
    std::chrono::seconds timeout)>;

// Default factory (in production):
auto realFactory = [](const std::string& url, std::chrono::seconds timeout) {
    eth::rpc::RpcHttpTransportOptions opts;
    opts.timeout = timeout;
    return std::make_unique<eth::rpc::RpcHttpTransport>(url, opts);
};

// Mock factory (in tests):
auto mockFactory = [&config](const std::string& url, std::chrono::seconds) {
    auto mock = std::make_unique<MockRpcTransport>();
    mock->loadConfig(config, url);
    return mock;
};
```

### Pattern 2: Multi-Chain Watch Tracking

**What:** `BridgeRelayer` replaces `EventWatchId watch_id_` with `std::unordered_map<std::string, EventWatchId>` keyed by chain name. `Start()` iterates a `std::vector<std::pair<std::string, std::string>>` (chain_name, contract_address).

**When to use:** `BridgeRelayer::Start()` — currently accepts single chain/contract (BridgeRelayer.cpp:79).

**Current signature** (`BridgeRelayer.hpp:41`):
```cpp
void Start(const std::string &chain_name, const std::string &contract_address);
```

**New signature:**
```cpp
struct ChainContractPair {
    std::string chain_name;
    std::string contract_address;
};
void Start(std::vector<ChainContractPair> chains);
```

**Per-chain tracking:**
```cpp
std::unordered_map<std::string, eth::EventWatchId> watch_ids_;
```

### Pattern 3: Startup Catch-Up Scan Integration

**What:** After CRDT sync in INITIALIZING_TRANSACTIONS, a new method `PerformStartupCatchupScan()` queries RPC for historical burns since the last known mint, matches against the UTXO set, and inserts missing burns as READY.

**When to use:** During node startup, after TransactionManager is initialized but before the node reaches READY.

**Integration point** (`GeniusNode.cpp:403-434`, `case NodeState::INITIALIZING_TRANSACTIONS`):
```cpp
case NodeState::INITIALIZING_TRANSACTIONS:
{
    // ... existing TransactionManager initialization ...
    
    // After transaction_manager_ is ready, before state transition:
    PerformStartupCatchupScan(); // New call
    
    StateTransition(NodeState::INITIALIZING_PROCESSING); // Existing
    break;
}
```

### Anti-Patterns to Avoid

- **Compile-time mock switching (`#ifdef MOCK_RPC`)**: Violates D-15 (runtime switch). Use DI injection instead so both mock and real transports compile into the test binary.
- **Adding new state machine states for bridge startup**: Violates D-04 (no new state). Use async launch within existing CREATING state.
- **Synchronous blocking RPC calls during startup**: Violates D-04 (non-blocking). Use `boost::asio::post` or launch on a separate strand.
- **Hardcoding contract addresses in C++ source**: Violates D-02 (sourced from chains_config.json). These must be in the JSON config file.
- **Single global mock transport instance**: Violates D-08 (per-node config) and D-12 (multi-chain). Each test node loads its own config.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON-RPC serialization | Custom JSON generation | `boost::json::serialize()` + `eth::rpc::make_get_transaction_receipt_request()` | Already in evmrelay; mock responses must match this format |
| Receipt response parsing | Custom parser | `eth::rpc::parse_transaction_receipt_response()` | Validators use this parser; mock must produce parseable output |
| JSON config file parsing | Custom tokenizer | `boost::json::parse()` or `rapidjson` | Already in codebase; handles all edge cases (nested objects, arrays, unicode) |
| Hex string ↔ address conversion | Hand-written loops | `rlp::base::parse::hex_array()` / `hex_array_string()` | Already used in BridgeRelayer and PublicChainInputValidator |
| Event topic0 computation | Manual keccak256 | `eth::cli::event_registry().params_for(event_sig)` | Already used in BridgeRelayer::Start(); returns standard ABI params |
| Wait-for-condition in tests | Busy-loop polling | `ASSERT_WAIT_FOR_CONDITION` from `testutil/wait_condition.hpp` | Existing Phase 4 pattern; handles timeouts, backoff, deadlock detection |
| Multi-node test setup | Manual node wiring | `SetUpTestSuite`/`TearDownTestSuite` static GTest fixture | Existing Phase 4 pattern; 3-node with PubSub bootstrap |

**Key insight:** Mock RPC transport is NOT a reimplementation of JSON-RPC — it returns canned responses that the existing `parse_transaction_receipt_response()` parser consumes. This eliminates an entire class of serialization bugs.

## Runtime State Inventory

> Phase 5 is a greenfield wiring phase (no rename/refactor/migration), so this section is intentionally brief.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | None — no renamed strings in databases | N/A |
| Live service config | None — no renamed strings in external services | N/A |
| OS-registered state | None | N/A |
| Secrets/env vars | None — new `SGNS_E2E_REAL_RPC` env var will be introduced | Document env var |
| Build artifacts | None | N/A |

**Nothing found in any category — this is a greenfield wiring phase, not a rename/refactor.**

## Common Pitfalls

### Pitfall 1: InitializeRpcEndpoints Called Before TransactionManager Ready

**What goes wrong:** `InitializeRpcEndpoints()` checks `if (!transaction_manager_)` and returns early (GeniusNode.cpp:1963). In the current state machine, `transaction_manager_` is created in `INITIALIZING_TRANSACTIONS`, which comes AFTER `CREATING`. If the async bridge init fires from `CREATING`, `transaction_manager_` will be null.

**Why it happens:** The state machine creates `transaction_manager_` late — its constructor needs `blockchain_` which isn't ready until `INITIALIZING_TRANSACTIONS`.

**How to avoid:** Launch the async bridge init function from within `case NodeState::INITIALIZING_TRANSACTIONS:`, AFTER `transaction_manager_` is created (line 405-410) but BEFORE the state transitions to `INITIALIZING_PROCESSING`. Or: use a `boost::asio::post` with a continuation that re-checks `transaction_manager_` availability.

**Warning signs:** "InitializeRpcEndpoints called before transaction manager is ready" log message, bridge not functioning silently.

### Pitfall 2: BridgeRelayer::Start() Racing With EthWatchService Initialization

**What goes wrong:** `BridgeRelayer::Start()` calls `watch_service_->watch_event()` which may fail silently or crash if the `EthWatchService` hasn't been fully initialized with chains/config yet.

**Why it happens:** `EthWatchService` is created as a default-constructed `shared_ptr` in `CREATING` state (GeniusNode.cpp:428). Its `initialize()` method is never called in the current code. The `watch_event()` method may work with a default-initialized service or may require `initialize()` first.

**How to avoid:** In the production path (non-mock), the `EthWatchService` needs its `initialize()` called with proper chain configs before `BridgeRelayer::Start()`. This should be part of the `InitializeAndStartBridge()` function. In test path, mock RPC transport bypasses the watch service entirely — tests trigger `OnWatchEvent` directly or via mock RPC.

**Warning signs:** Null pointer dereference in `EthWatchService`, "no EthWatchService" log, watch not firing.

### Pitfall 3: chains_config.json CWD-Relative Path

**What goes wrong:** `InitializeRpcEndpoints()` currently sets:
```cpp
config.chains_json_path = std::filesystem::current_path() / "chains.json";
```
This fails when the binary is launched from a different working directory, or when the config file is bundled elsewhere.

**Why it happens:** Phase 1 deferred item #2 — "CWD-relative chains.json path (fragile at runtime)"

**How to avoid:** Resolve the path relative to the binary directory using `boost::dll::program_location()` or accept a configurable path via `DevConfig_st`. The `ChainRpcProviderConfig` struct already supports this — the fix is just changing the path assignment.

**Warning signs:** "chains.json not found at ..." log on production startup.

### Pitfall 4: UTXO Guard Removal Breaking Non-Full-Node Consensus

**What goes wrong:** Removing the 8 `!is_full_node_ && address != address_` guards allows non-full nodes to store UTXOs for arbitrary addresses. This could cause:
1. Memory/disk consumption from tracking all peers' UTXOs
2. Race conditions if multiple nodes write UTXOs for the same address
3. Incorrect balance calculations if UTXOs from other addresses are counted

**Why it happens:** The guards were there for a reason — to limit non-full nodes to their own address's UTXOs.

**How to avoid:** Phase 5 only needs to track burn/bridge UTXOs across all addresses. **Scope the guard removal to only `PutUTXO` (line 161)** — that's the only entry point for inserting burn UTXOs. For `GetBalance` and other query methods, keep or modify the guard to only count burn/bridge UTXOs for foreign addresses. This is narrower and safer than removing all 8 guards unconditionally.

Alternatively, add a new method like `PutForeignBridgeUTXO()` that skips the guard without removing it from the general `PutUTXO()`.

**Warning signs:** Memory growth after guard removal, incorrect balance reports, consensus failures.

### Pitfall 5: Mock Transport Config Path Resolution

**What goes wrong:** D-08 specifies `<binary_dir>/mock_rpc_config.json` — but `boost::dll::program_location()` returns the binary path, not its directory. The config must be in the binary's directory, not the binary itself.

**Why it happens:** Path confusion between binary path and binary directory.

**How to avoid:** Use `boost::dll::program_location().parent_path() / "mock_rpc_config.json"` and validate with `std::filesystem::exists()` before reading. If not found, fall back to creating a default mock config or skipping gracefully.

## Code Examples

### Mock Transport Interface Implementation

```cpp
// Source: evmrelay/include/eth/rpc_receipt_source.hpp (JsonRpcTransport)
// File: test/src/mock/mock_rpc_transport.hpp
#pragma once

#include <eth/rpc_receipt_source.hpp>
#include <boost/json.hpp>
#include <map>
#include <string>
#include <vector>

namespace sgns::test {

enum class MockBehavior {
    kSuccess,
    kTimeout,
    kConnectionRefused,
    kBadJson,
    kWrongStatus,
    kWrongLogs
};

struct MockEndpointConfig {
    std::string url;
    MockBehavior behavior = MockBehavior::kSuccess;
    std::map<std::string, std::vector<std::string>> responses; // tx_hash -> ordered responses
};

class MockRpcTransport final : public eth::rpc::JsonRpcTransport {
public:
    explicit MockRpcTransport(const MockEndpointConfig& config);
    
    [[nodiscard]] std::optional<std::string> call(
        const boost::json::object& request) override;
    
    // Test control methods
    void resetState();
    void setBehavior(MockBehavior b);
    size_t callCount() const { return call_count_; }

private:
    MockEndpointConfig config_;
    size_t call_count_ = 0;
    std::map<std::string, size_t> response_index_; // per-tx_hash index
};

} // namespace sgns::test
```

### Per-Chain Watch ID Tracking

```cpp
// Source: src/account/BridgeRelayer.hpp line 67 (current single watch_id_)
// Modified: multi-chain tracking
struct ChainWatchEntry {
    eth::EventWatchId watch_id = 0;
    std::string chain_name;
    std::string contract_address;
};

std::unordered_map<std::string, ChainWatchEntry> chain_watches_;

// Start() iterates pairs:
void BridgeRelayer::Start(std::vector<ChainContractPair> chains) {
    for (const auto& [chain_name, contract_address] : chains) {
        // ... existing watch registration logic ...
        auto watch_id = watch_service_->watch_event(addr, event_sig, params, callback);
        chain_watches_[chain_name] = {watch_id, chain_name, contract_address};
        logger_->info("BridgeRelayer: watching {} contract={} watch_id={}", 
                      chain_name, contract_address, watch_id);
    }
}
```

### UTXOType Enum Addition

```cpp
// Source: src/account/UTXOManager.hpp line 51-55 (current enum)
// Extended:
enum class UTXOState : uint8_t {
    UTXO_READY,     ///< UTXO is unspent and available for use
    UTXO_RESERVED,  ///< UTXO is reserved (burn UTXO with mint in consensus)
    UTXO_CONSUMED   ///< UTXO has been consumed by a transaction
};

enum class UTXOType : uint8_t {
    UTXO_NORMAL = 0,  ///< Standard UTXO from local transfers/mints
    UTXO_BRIDGE = 1   ///< UTXO from cross-chain bridge burn
};

struct UTXOEntry {
    UTXOState state{UTXOState::UTXO_READY};
    UTXOType  type{UTXOType::UTXO_NORMAL};  // NEW
    GeniusUTXO utxo;
    // ... existing fields ...
};
```

### Startup Catch-Up Scan Integration

```cpp
// Source: src/account/GeniusNode.cpp, after line 434 (INITIALIZING_TRANSACTIONS)
// New async function added to GenomeNode:
void GeniusNode::PerformStartupCatchupScan() {
    if (!transaction_manager_ || !bridge_relayer_) return;
    
    // 1. Get last processed mint message date from CRDT
    auto last_mint = transaction_manager_->GetLastMintTimestamp();
    
    // 2. For each chain with bridge_contract_address, probe RPC
    for (const auto& chain : bridge_chains_) {
        if (chain.contract_address.empty()) continue;
        
        // Query eth_getLogs for BridgeSourceBurned events after last_mint
        auto burns = QueryHistoricalBurns(chain, last_mint);
        
        for (const auto& burn : burns) {
            // 3. Check if burn is already in UTXO set
            if (transaction_manager_->HasBurnUTXO(burn.tx_hash)) continue;
            
            // 4. Insert missing burn UTXO as READY with UTXOType::UTXO_BRIDGE
            transaction_manager_->InsertBurnUTXO(burn);
        }
    }
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `BridgeRelayer::Start(chain, address)` single | `Start(vector<pair>)` multi-chain | Phase 5 | Per-chain watch tracking needed |
| CWD-relative `chains.json` path | Binary-relative or configurable path | Phase 5 | Fixes production path fragility |
| `RpcHttpTransport` hard-constructed | Transport factory DI injection | Phase 5 | Enables mock transport without compile flags |
| `InitializeRpcEndpoints()` dead code | Wired into CREATING startup | Phase 5 | Bridge pipeline runs in normal nodes |
| `UTXO_READY → UTXO_CONSUMED` only | Add `UTXO_RESERVED` intermediate state | Phase 5 | Burn UTXO lifecycle tracking |
| No UTXO type distinction | `UTXOType` enum in `UTXOEntry` | Phase 5 | Easy scan for bridge UTXOs |
| Foreign address UTXO guard | Remove guard (or scope to PutUTXO only) | Phase 5 | All nodes track burn UTXOs |

**Deprecated/outdated:**
- Single `watch_id_` in BridgeRelayer — replaced by `chain_watches_` per-chain map
- `InitializeRpcEndpoints()` hardcoded `chain_id_map` — should source from `chains_config.json`
- `chains_json_path` as `std::filesystem::current_path() / "chains.json"` — fragile CWD dependency

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `EthWatchService` default-constructed (no `initialize()` call) supports `watch_event()` calls | Common Pitfalls #2 | If `initialize()` is required, `Start()` will fail silently |
| A2 | Removing only the `PutUTXO` guard (line 161) is sufficient for burn UTXO tracking — other 7 guards can stay | Common Pitfalls #4 | If validators need `GetBalance`/`ComputeUTXOMerkleRoot` for foreign addresses, those guards must also be removed |
| A3 | `TransactionManager::GetLastMintTimestamp()` exists or can be derived from CRDT query | Code Examples (catch-up scan) | If no API exists, the catch-up scan needs a different approach to find the last mint date |
| A4 | `chains_config.json` JSON parser tolerates new `bridge_contract_address` key without schema changes | Architecture Patterns | If chainlist_provider uses strict schema validation, the new field may cause parse failures |
| A5 | `boost::dll::program_location().parent_path()` returns valid writable directory on all platforms (macOS, Linux, Windows) | Common Pitfalls #5 | macOS .app bundles may have read-only binary directories — test fixture dir is safer |
| A6 | `public_coutpoints` existence of consumed outpoints pointer in UTXOManager persists across restarts | Common Pitfalls #4 | If the data model doesn't persist outpoints for other addresses, data will be lost on restart |

**If this table is empty:** N/A — assumptions exist and need validation.

## Open Questions (RESOLVED)

1. **EthWatchService initialization requirement**
   - What we know: `EthWatchService` is created as `std::make_shared<eth::EthWatchService>()` (GeniusNode.cpp:428) — default-constructed, never `initialize()`'d
   - What's unclear: Whether `watch_event()` requires prior `initialize()` or works on a bare service
   - Recommendation: Test `watch_event()` on a default-constructed `EthWatchService` early in implementation. If `initialize()` is required, add it to `InitializeAndStartBridge()` with proper chain configs.

2. **Scope of UTXO guard removal**
   - What we know: 8 guards exist; CONTEXT.md says "remove the 8 guards" (D-17)
   - What's unclear: Whether all 8 must be removed, or if only `PutUTXO` (line 161) is needed for burn UTXO tracking
   - Recommendation: Start with removing only the `PutUTXO` guard on line 161 — this is the entry point for burn UTXO insertion. If validators report additional guard blocks during testing, remove remaining guards incrementally.

3. **chains_config.json bridge_contract_address format**
   - What we know: 8 chains with deployed bridges; chains_config.json currently has no `bridge_contract_address` field
   - What's unclear: Exact contract addresses for all 8 chains; whether the JSON schema change is backward-compatible with `load_chainlist_from_json_text()`
   - Recommendation: Add `bridge_contract_address` as an optional top-level field per chain in chains_config.json. Verify `load_chainlist_from_json_text()` tolerates unknown fields (most JSON parsers do). Source addresses from `send_test_transactions.sh` and deployment records.

4. **Startup catch-up scan depth/mechanism**
   - What we know: "grab last mint message by date, check contract via RPC for unprocessed burns" (CONTEXT.md)
   - What's unclear: Maximum block depth to scan; whether to use `eth_getLogs` (range query) or iterate `eth_getBlockByNumber` + `eth_getTransactionReceipt`
   - Recommendation: Use `eth_getLogs` with the BridgeSourceBurned event topic0 as the filter — this allows efficient historical scanning without iterating individual transactions. Default depth: 10,000 blocks (~2 days on mainnet, configurable).

5. **UTXOType integration with existing UTXO lifecycle**
   - What we know: UTXOEntry already tracks state transitions with `spent_by_txid` and `spent_epoch`; RESERVED stops local `SelectUTXOs()` but allows consensus voting
   - What's unclear: Whether `SelectUTXOs`/`GetBalance` already checks `reserved_outpoints_` (they do — lines 99/102, 148) — so RESERVED UX is well-understood. The question is whether a new `UTXO_RESERVED` state interacts correctly with `IsOutPointConsumed()`.
   - Recommendation: `IsOutPointConsumed()` returns false for RESERVED (not yet consumed). `GetBalance()` skips RESERVED (same as current reservation logic). Add `IsOutPointReserved()` predicate for consensus logic.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Boost (Beast, JSON, ASIO, DLL) | RpcHttpTransport, mock config, async startup | ✓ (via build tree) | 1.87+ [VERIFIED: CMakeLists.txt] | — |
| evmrelay submodule | JsonRpcTransport, EthWatchService, chainlist, RPC parse | ✓ | 59d1ed2 [VERIFIED: ROADMAP.md] | — |
| Google Test | Unit + integration tests | ✓ (via build tree) | 1.14+ [VERIFIED: test CMakeLists.txt] | — |
| rapidjson | Mock config parsing (or use boost::json) | ✓ (via build tree) | — [VERIFIED: BridgeRelayer.cpp includes] | boost::json |
| cast CLI (Foundry) | E2E tests with real RPC | ✓ (if installed) | — [VERIFIED: Phase 4 env check] | Skip if SGNS_E2E_REAL_RPC not set |

**Missing dependencies with no fallback:**
- None — all dependencies are in the build tree.

**Missing dependencies with fallback:**
- Foundry `cast` — only needed for E2E tests with `SGNS_E2E_REAL_RPC=1`. Not needed for mock RPC tests.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Google Test 1.14+ |
| Config file | none — see Wave 0 |
| Quick run command | `ctest --test-dir build -R BridgeRelayerTest` |
| Full suite command | `ctest --test-dir build -R "BridgeRelayer|MockRpc|StartupWiring"` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| — | Multi-chain Start() registers watches on all 8 chains | unit | `ctest -R BridgeRelayerTest.MultiChainStart` | ❌ Wave 0 |
| — | Start() skips chains without bridge_contract_address | unit | `ctest -R BridgeRelayerTest.SkipsChainsWithoutAddress` | ❌ Wave 0 |
| — | Mock transport returns success for valid receipt | unit | `ctest -R MockRpcTest.SuccessReceipt` | ❌ Wave 0 |
| — | Mock transport returns timeout (nullopt) | unit | `ctest -R MockRpcTest.Timeout` | ❌ Wave 0 |
| — | Mock transport returns connection_refused | unit | `ctest -R MockRpcTest.ConnectionRefused` | ❌ Wave 0 |
| — | Mock transport returns bad_json (unparseable) | unit | `ctest -R MockRpcTest.BadJson` | ❌ Wave 0 |
| — | Mock transport returns wrong_status (status=false) | unit | `ctest -R MockRpcTest.WrongStatus` | ❌ Wave 0 |
| — | Mock transport returns wrong_logs (log mismatch) | unit | `ctest -R MockRpcTest.WrongLogs` | ❌ Wave 0 |
| — | Stateful sequences: ordered responses per tx_hash | unit | `ctest -R MockRpcTest.StatefulSequence` | ❌ Wave 0 |
| — | VerifyPublicChainSmartContract uses transport factory | unit | `ctest -R PublicChainValidator.TransportFactoryInjection` | ❌ Wave 0 |
| — | Startup catch-up: inserts missing burn UTXO | integration | `ctest -R StartupWiring.CatchUpScan` | ❌ Wave 0 |
| — | Best-effort: failed chain doesn't block other chains | unit | `ctest -R BridgeRelayerTest.BestEffortFailure` | ❌ Wave 0 |
| — | UTXO_RESERVED state blocks local SelectUTXOs but allows vote | unit | `ctest -R UTXOManager.ReservedState` | ❌ Wave 0 |
| — | UTXOType::UTXO_BRIDGE distinguishable from UTXO_NORMAL | unit | `ctest -R UTXOManager.BridgeUtxoType` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest --test-dir build -R "BridgeRelayerTest|MockRpcTest"` (unit tests, < 5 sec)
- **Per wave merge:** `ctest --test-dir build` (full suite)
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `test/src/mock/mock_rpc_transport.hpp` — MockRpcTransport class
- [ ] `test/src/mock/mock_rpc_transport.cpp` — Implementation
- [ ] `test/src/mock/mock_rpc_config.hpp` — Config parser
- [ ] `test/src/mock/mock_rpc_test.cpp` — Behavioral tests (6 failure modes + sequences)
- [ ] `test/src/startup/startup_wiring_test.cpp` — Startup integration tests
- [ ] Existing `test/src/account/bridge_relayer_test.cpp` — Extend for multi-chain Start()
- [ ] Framework config: test CMakeLists.txt updates for new directories

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | Not applicable — no new auth endpoints |
| V3 Session Management | no | Not applicable — no sessions |
| V4 Access Control | yes | UTXO guard removal impacts cross-peer UTXO access; validate that removed guards don't leak into unauthenticated UTXO reads |
| V5 Input Validation | yes | Mock config JSON parsing; `chains_config.json` with new `bridge_contract_address` field — validate before use |
| V6 Cryptography | no | Not applicable — no new cryptography |

### Known Threat Patterns for BridgeRelayer + RPC Transport

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Mock transport shipping in production binary (D-16 violation) | Information Disclosure | Compile-time `#ifndef NDEBUG` guard or separate test-only CMake target; verify in CI |
| Malformed `mock_rpc_config.json` causing crash | Denial of Service | Validate JSON schema before use; fail gracefully with clear error |
| `bridge_contract_address` not validated — injecting wrong address | Spoofing | Address format validation (0x + 40 hex); cross-check against known deployment addresses |
| `chains_config.json` tampering to point to attacker's contract | Tampering | Validate hash/signature of config file in production; read-only permissions |
| Startup catch-up scan querying unlimited block range | Denial of Service | Cap max scan depth (default 10,000 blocks); timeout per RPC call (10s) |
| UTXO guard removal allowing foreign address write attacks | Elevation of Privilege | Verify that only burn UTXOs (via BridgeRelayer/MintFunds path) write to foreign addresses; audit callers of PutUTXO |

## Sources

### Primary (HIGH confidence)
- `src/account/BridgeRelayer.hpp` (lines 1-69) — Class definition, single `watch_id_`, `Start(chain, address)` signature
- `src/account/BridgeRelayer.cpp` (lines 1-201) — Full implementation, burn event decoding, `MintFunds` call
- `src/account/GeniusNode.cpp` (lines 330-489) — State machine, CREATING state, `bridge_relayer_` creation, `InitializeRpcEndpoints()` dead code at line 1961
- `src/account/GeniusNode.hpp` (lines 130-143, 590-629) — NodeState enum, member variables
- `src/account/PublicChainInputValidator.hpp` (lines 1-102) — `SetRpcEndpoints()`, `WeightedRpcEndpoint` with `bridge_contract_address`/`event_topic0`
- `src/account/PublicChainInputValidator.cpp` (lines 130-259) — Hard `RpcHttpTransport` construction at line 183, consensus threshold logic
- `evmrelay/include/eth/rpc_receipt_source.hpp` (lines 1-78) — `JsonRpcTransport` abstract interface
- `evmrelay/include/eth/rpc_http_transport.hpp` (lines 1-57) — `RpcHttpTransport` implementation
- `evmrelay/include/eth/eth_watch_service.hpp` (lines 187-259) — `EthWatchService` API, `watch_event()` signature
- `src/account/UTXOManager.hpp` (lines 1-427) — `UTXOState` enum, `UTXOEntry`, `reserved_outpoints_`
- `src/account/UTXOManager.cpp` (lines 78-194) — 8 guard locations at lines 83, 124, 161, 191, 338, 573, 850, 931
- `src/account/ChainRpcEndpointProvider.hpp` (lines 1-95) — `ChainRpcProviderConfig`, `Initialize()` API
- `src/account/ChainRpcEndpointProvider.cpp` (lines 1-130) — Full implementation, endpoint wiring
- `evmrelay/examples/chains_config.json` (lines 1-656) — Chain config format, currently no `bridge_contract_address`
- `.planning/phases/05-startup-wiring-mock-rpc/05-CONTEXT.md` — 21 locked decisions
- `.planning/notes/rpc-verification-tiers.md` — Tier 1/2 verification architecture
- `.planning/config.json` — `nyquist_validation: true`, `security_enforcement: true`

### Secondary (MEDIUM confidence)
- `test/src/bridge_e2e/bridge_e2e_test.cpp` — Phase 4 E2E test fixture pattern
- `test/src/account/bridge_relayer_test.cpp` — 9 existing BridgeRelayer unit tests
- `evmrelay/include/eth/json_rpc.hpp` — `make_get_transaction_receipt_request()`, `parse_transaction_receipt_response()`

### Tertiary (LOW confidence)
- None — all findings verified against source code.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all dependencies already in build tree; no new packages
- Architecture: HIGH — source code at specific lines verified for all integration points
- Pitfalls: MEDIUM — 3 assumptions (A1, A2, A3) need validation during implementation
- UTXO guard removal: MEDIUM — exact scope needs runtime validation (Pitfall #4)

**Research date:** 2026-06-04
**Valid until:** 2026-07-04 (30 days — stable C++ codebase, no fast-moving external dependencies)
