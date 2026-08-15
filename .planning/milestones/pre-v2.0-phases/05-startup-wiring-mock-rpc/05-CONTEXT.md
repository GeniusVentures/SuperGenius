# Phase 5: Startup Wiring + Mock RPC Transport - Context

**Gathered:** 2026-06-04
**Status:** Ready for planning

<domain>
## Phase Boundary

Wire `BridgeRelayer::Start()` and `InitializeRpcEndpoints()` into GeniusNode's startup path so the burn→MintFunds pipeline actually runs in normal nodes (currently dead — Start() is never called). Add in-process mock RPC transport for Tier 1 verification testing. Implement startup catch-up scan for unprocessed bridged transactions. Source: PR #298 Codex review — 3 deferred P1 findings.

</domain>

<decisions>
## Implementation Decisions

### Node Startup Wiring (from DISCUSSION-PART1 + PART2)
- **D-01:** `BridgeRelayer::Start()` changes from single chain/contract to multi-chain — accepts a set of `(chain_name, contract_address)` pairs, registers a `BridgeSourceBurned` watch on the shared `EthWatchService` per chain.
- **D-02:** Chain data sourced from `chains_config.json` with optional `bridge_contract_address` field per chain. Chains without the field are skipped.
- **D-03:** Contract address mapping for 8 chains (ethereum-mainnet, ethereum-sepolia, bnb-smart-chain, bnb-smart-chain-testnet, polygon-mainnet, polygon-amoy, base-mainnet, base-sepolia). ethereum-holesky, ethereum-hoodi, gnosis-chain skipped — no bridge deployed.
- **D-04:** Both `InitializeRpcEndpoints()` and `BridgeRelayer::Start()` fire as a single async function from `INITIALIZING_TRANSACTIONS` state (after `transaction_manager_` is created — it is null during CREATING, per RESEARCH.md Pitfall #1). No new state machine state. Non-blocking — node proceeds through normal transitions. Ordering guaranteed: Start() only fires after endpoints are ready.
- **D-05:** Config sharing: both consume `chains_config.json`. `InitializeRpcEndpoints()` → `ChainRpcEndpointProvider` → `PublicChainInputValidator::SetRpcEndpoints()`. `BridgeRelayer::Start()` → iterates chains, registers watch per chain with `bridge_contract_address`.
- **D-06:** API-key direct endpoints via `ChainRpcProviderConfig.direct_endpoints` (already supported) — configured at deployment time via Keychain/Keystore. No API keys in git/env.

### Mock RPC Transport (from DISCUSSION-PART3)
- **D-07:** Mock implements the same interface as `RpcHttpTransport` — drop-in replacement. `PublicChainInputValidator` doesn't know it's talking to a mock. Dependency injection via `ChainRpcEndpointProvider` → `SetRpcEndpoints()`.
- **D-08:** Per-node JSON config file at `<binary_dir>/mock_rpc_config.json`. Test fixtures point to their own config.
- **D-09:** Config fields per endpoint: `url` (match key), `behavior` (`success` | `timeout` | `connection_refused` | `bad_json` | `wrong_status` | `wrong_logs`), `responses` (ordered list of canned `eth_getTransactionReceipt` JSON responses, keyed by tx_hash for stateful sequences).
- **D-10:** Stateful sequences: ordered list of responses keyed by tx_hash. First call gets response[0], second gets response[1], etc. Resets per test case.
- **D-11:** Canned response format: raw JSON strings matching live `eth_getTransactionReceipt` response format. `eth::rpc::parse_transaction_receipt_response()` parses identically to live data. No new types needed.
- **D-12:** Multi-chain support through `chains_config.json` — mock reads the same chain list. Skipped chains use real RPC or fail-closed if no endpoints.
- **D-13:** All 6 failure modes supported: success, timeout, connection_refused, bad_json, wrong_status, wrong_logs.

### Mock Transport Enablement
- **D-14:** Test executables default to mock transport. Both mock and real transports compile into test binary — selection is runtime via DI injection.
- **D-15:** Real RPC opt-in per test via runtime switch (env var or gtest flag, e.g. `SGNS_E2E_REAL_RPC=1`). No compile flags that affect the entire build.
- **D-16:** Production `genius_node` binary uses real RPC only — mock transport not compiled into production.

### Startup Catch-Up Scan
- **D-17:** Remove the 8 `!is_full_node_ && address != address_` guards in `UTXOManager.cpp` — all nodes store UTXOs for all peers. Required so validators can track burn UTXOs for conflict detection.
- **D-18:** Add `UTXO_RESERVED` to `UTXOState` enum. Burn UTXO lifecycle: `READY` (burn detected, UTXO inserted) → `RESERVED` (mint initiated, blocks local reuse but allows consensus voting) → `CONSUMED` (certificate produced).
- **D-19:** Add a `UTXOType` field or marker to `UTXOEntry` to distinguish bridge/burn UTXOs from regular UTXOs, making them easy to find during scans.
- **D-20:** Startup catch-up scan: probe RPC for historical burns with a maximum depth → match against known UTXO set → insert missing burns as READY. When a READY burn is found, trigger `MintFunds()` → transitions to RESERVED during consensus → CONFIRMED on certificate.

### BridgeRelayer Failure Handling
- **D-21:** Best-effort on multi-chain Start(). If some chains fail to register watches (missing endpoints, contract not deployed, network issues), skip the failed chain and continue with others. Log warning per skipped chain.

### Claude's Discretion
- `BridgeRelayer` internal refactoring from single `watch_id_` to per-chain watch ID tracking
- `InitializeRpcEndpoints()` implementation details — parsing `chains_config.json`, populating `ChainRpcEndpointProvider`
- Mock transport class name and file location within the codebase conventions
- Exact `UTXOType` enum design and placement
- Startup scan depth and query mechanism
- GTest flag name for real-RPC opt-in

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Node Startup & Lifecycle
- `src/account/GeniusNode.hpp` — `GeniusNode::New()`, `NodeState` machine, `BridgeRelayer` instantiation at line 427-432, `InitializeRpcEndpoints()` at line 1961
- `src/account/GeniusNode.cpp` — State machine transitions, subsystem initialization order

### BridgeRelayer
- `src/watcher/impl/evm_messaging_watcher.hpp` — `BridgeRelayer::Create()`, `Start()`, `EthWatchService`
- `src/watcher/impl/evm_messaging_watcher.cpp` — Current `Start()` implementation (single chain/contract), `watch_id_` member

### RPC & Input Validation
- `evmrelay/include/eth/rpc_manager.hpp` — `RpcHttpTransport` interface, `RpcManager` multi-endpoint pool
- `evmrelay/src/eth/rpc_manager.cpp` — Transport construction, endpoint management
- `src/account/InputValidators.hpp` — `PublicChainInputValidator`, `WeightedRpcEndpoint`, `SetRpcEndpoints()`
- `src/account/InputValidators.cpp` — `VerifyPublicChainSmartContract()`, endpoint iteration

### Chain Configuration
- `evmrelay/include/eth/chain_list_provider.hpp` — `ChainRpcEndpointProvider`, `ChainRpcProviderConfig`
- `evmrelay/examples/chains_config.json` — Chain list with Sepolia, mainnet, testnet entries
- `evmrelay/examples/send_test_transactions.sh` — Contract addresses per chain

### UTXO & Conflict Detection
- `src/account/UTXOManager.hpp` — `UTXOState` enum, `UTXOEntry` struct, `PutUTXO()`, foreign-address methods
- `src/account/UTXOManager.cpp` — Address guards (lines 83, 124, 161, 191, 338, 573, 850, 931), `BuildUTXORecordKey()`
- `src/account/TransactionManager.cpp` — `HasConfirmedInputConflict()` (line 3524), `MintFunds()`, `ChangeTransactionState()`

### E2E Test Infrastructure (Phase 4)
- `test/src/bridge_e2e/bridge_e2e_test.cpp` — Existing 3-node E2E test fixture, `GTEST_SKIP(RUN_E2E_BRIDGE)` pattern
- `test/testutil/wait_condition.hpp` — `ASSERT_WAIT_FOR_CONDITION` polling templates

### Phase 4 Context
- `.planning/phases/04-e2e-integration-test/04-CONTEXT.md` — Test form factor, validator topology, negative test cases, deferred ideas (RPC endpoint init tests, multi-chain stress test)

### Phase 3 Context
- `.planning/phases/03-burn-dedup-cache/03-CONTEXT.md` — Slot key collision fix, fail-closed on missing endpoints, log verification

### Architecture
- `.planning/notes/rpc-verification-tiers.md` — RPC verification tier design (referenced in ROADMAP)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `BridgeRelayer` (Phase 2) — Already wires burn detection → `MintFunds`. Extend `Start()` to multi-chain.
- `InitializeRpcEndpoints()` — Empty method at `GeniusNode.cpp:1961`. Wire to `ChainRpcEndpointProvider`.
- `RpcHttpTransport` interface — Mock implements the same interface, drop-in replacement.
- `ChainRpcEndpointProvider` + `SetRpcEndpoints()` — DI injection point for mock transport.
- `GTest fixture` pattern from Phase 4 — 3-node setup, `SetUpTestSuite`/`TearDownTestSuite`, PubSub bootstrap. Reuse for mock RPC tests.
- `chains_config.json` — Already consumed by `ChainRpcEndpointProvider`. Extend with `bridge_contract_address`.
- `UTXOManager::PutUTXO(utxo, address)` — Already accepts foreign addresses, just needs guard removal.

### Established Patterns
- Factory pattern: `New()` / `Create()` returning `std::shared_ptr`
- `outcome::result<T>` for all fallible operations
- `CComponentFactory` DI container for service wiring
- `AppStateManager` FSM for lifecycle (`CREATING → ... → READY`)
- Friend accessor pattern for GTest private method access
- `GTEST_SKIP()` for environment-gated tests
- Dependency injection for transport backends

### Integration Points
- `GeniusNode::New()` — Insert async `InitializeAndStartBridge()` call during `CREATING` state
- `BridgeRelayer::Start()` — Signature change from `(chain, contract)` to `(vector<pair<chain, contract>>)`
- `ChainRpcEndpointProvider::Initialize()` — Mock injection point before `SetRpcEndpoints()`
- `UTXOManager` guard removal — 8 locations in `UTXOManager.cpp`
- `UTXOState` enum — Add `UTXO_RESERVED` member

</code_context>

<specifics>
## Specific Ideas

- The Phase 4 deferred idea D-DEF-1 (RPC Endpoint Initialization Tests) is partially addressed by this phase's wiring — `InitializeRpcEndpoints()` becomes callable and testable.
- Phase 4 deferred idea D-DEF-2 (Multi-Chain Bridge Stress Test) becomes feasible once mock transport supports multi-chain with behavioral variance.
- Burn UTXO type marker should make it easy to find bridge/burn UTXOs across all peer addresses during startup scan.
- RESERVED state semantics: blocks local MintFunds() reuse but does not reject incoming consensus proposals — the validator votes on the proposal normally, and if the certificate confirms, transitions RESERVED → CONSUMED.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 05-Startup Wiring + Mock RPC Transport*
*Context gathered: 2026-06-04*
