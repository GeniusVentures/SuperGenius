# Phase 4: End-to-End Integration Test - Context

**Gathered:** 2026-05-31
**Status:** Ready for planning

<domain>
## Phase Boundary

Demonstrate the full EVM bridge pipeline in a single test: EVM burn on Sepolia → BridgeRelayer detection → MintTransactionV2 creation → UTXO consensus (2-of-3 quorum) → RPC verification → minted tokens in recipient's UTXO set. The test validates that Phases 1-3 work together as a complete system against a live testnet.

</domain>

<decisions>
## Implementation Decisions

### Test Form Factor
- **D-01:** GTest integration test following the `processing_multi_test.cpp` pattern — 3 `GeniusNode` instances in a `SetUpTestSuite`, bootstrapped via PubSub `AddPeers`.
- **D-02:** Shell out to `cast send` (Foundry) for the burn trigger. evmrelay does not currently have transaction-signing or sending capability, so the burn transaction is sent via the shell. Everything after the burn (detection, minting, consensus, verification) happens in C++.
- **D-03:** The test is NOT CI-runnable. Guard with `if (!getenv("RUN_E2E_BRIDGE")) GTEST_SKIP();` so it's skipped by default and only runs when developers explicitly opt in with the env var set.

### Test Environment
- **D-04:** Live Sepolia testnet. The GNUS contract is already deployed at `0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70`.
- **D-05:** The `PRIVATE_KEY` environment variable provides both the sender and destination wallet. The burn is a self-transfer (same wallet sends burn, receives mint). Sepolia ETH funding is a pre-requisite — the test does not fund the wallet.
- **D-06:** Sepolia RPC URLs come from `chains_config.json` (Phase 1 wiring) or can be overridden via env vars (`RPC_SEPOLIA`).

### Validator Topology
- **D-07:** 3 nodes, 2-of-3 quorum. All three nodes are `GeniusNode` instances in the same process (same as `processing_multi_test.cpp`).
- **D-08:** Each node gets its own `DevConfig` with a separate `BaseWritePath` (node1/, node2/, node3/). Nodes are bootstrapped via PubSub `AddPeers` in `SetUpTestSuite`.

### Burn Trigger
- **D-09:** The C++ test calls `system()` or `popen()` to invoke `cast send` on the Sepolia GNUS contract. The burn is an ERC-20 `transfer(address,uint256)` — the same pattern used in `send_test_transactions.sh`. The recipient is the sender's own address (self-transfer burn).
- **D-10:** The `cast` binary (Foundry) must be installed on the developer's machine. The test should check for its presence and skip with a clear message if not found.

### Verification and Assertions
- **D-11:** After the burn tx is sent, the test polls the destination account's UTXO set via CRDT state (GlobalDB/UTXOManager) using the project's `wait_condition.hpp` templates. Timeout: 10 seconds.
- **D-12:** The pipeline should complete quickly — PubSub consensus over local nodes is fast. The 10s timeout accounts for Sepolia block confirmation time (the slowest step).
- **D-13:** Verification confirms: (a) `MintTransactionV2` was created with correct chain_id, amount, token_id, burn_tx_hash; (b) UTXO consensus produced a certificate; (c) minted UTXO appears in recipient's set.

### Negative Test Cases (mandatory)
- **D-14:** Always include negative test cases. Specific negatives based on Phase 3 fixes:
  - **Replay rejection:** Send the same burn tx hash twice → second mint should be deduplicated (slot key collision fix).
  - **Missing endpoints:** Configure a chain with no RPC endpoints → verification should fail closed (fail-closed fix).
  - **Invalid receipt logs:** Mock or configure a mismatched bridge contract address → verification should reject (log verification fix).
- **D-15:** Negative tests can use mocked RPC (not live Sepolia) for speed and determinism. Only the positive E2E path needs the live testnet.

### Claude's Discretion
- Test file location: `test/src/bridge_e2e/bridge_e2e_test.cpp` (follows existing test directory structure)
- CMake integration: add test target in `test/src/bridge_e2e/CMakeLists.txt`
- The test fixture class name: `BridgeE2ETest`
- Cleanup: nodes are torn down in `TearDownTestSuite`, test data directories cleaned up

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### E2E Test Pattern
- `test/src/processing_multi/processing_multi_test.cpp` — 3-node GTest fixture pattern: `SetUpTestSuite` creates nodes, bootstraps PubSub, `TearDownTestSuite` cleans up

### Bridge Architecture
- `src/account/GeniusNode.hpp` — `GeniusNode::New()`, `MintTokens()`, node lifecycle
- `src/watcher/impl/evm_messaging_watcher.hpp` — Bridge event orchestration
- `src/account/InputValidators.hpp` — `PublicChainInputValidator`, `VerifyPublicChainSmartContract()`, `WeightedRpcEndpoint`
- `src/account/InputValidators.cpp` — `VerifyPublicChainSmartContract()` at line 474

### Burn Detection
- `evmrelay/include/eth/bridge_event.hpp` — `BridgeEventClaim`, `verify_receipt_log()`, `ReceiptLogVerificationResult`
- `evmrelay/include/eth/eth_receipt_source.hpp` — `ReceiptResult` struct
- `src/account/TransactionManager.cpp` — `MintFunds()` call path

### Consensus Slot Key
- `src/blockchain/Consensus.cpp` — `GetSlotKey()` at line 2130, MintV2 slot key at line 2140

### Burn Trigger Script
- `evmrelay/examples/send_test_transactions.sh` — Sepolia GNUS contract address (`0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70`), `cast send` pattern, PRIVATE_KEY env var usage

### Test Utilities
- `test/testutil/wait_condition.hpp` — `ASSERT_WAIT_FOR_CONDITION`, `EXPECT_WAIT_FOR_CONDITION` — polling-based async assertions (no sleep_for)
- `test/testutil/outcome.hpp` — `EXPECT_OUTCOME_TRUE`, `ASSERT_OUTCOME_SUCCESS` — outcome::result assertions
- `test/testutil/mint_source_hash.hpp` — `NextMintSourceHash()` helper for deterministic test data

### Phase 3 Decisions (validated by negative tests)
- `.planning/phases/03-burn-dedup-cache/03-CONTEXT.md` — Slot key collision fix (D-01/D-02), fail-closed fix (D-03), UTXO witness fix (D-04), log verification fix (D-05/D-06)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `processing_multi_test.cpp` fixture pattern — 3 nodes, PubSub bootstrap, separate DevConfig per node. Direct reuse for the test structure.
- `send_test_transactions.sh` — Sepolia contract addresses, `cast send` command pattern, PRIVATE_KEY handling. Reference for the shell-out burn trigger.
- `wait_condition.hpp` templates — Polling-based async assertions. Use for UTXO confirmation polling (10s timeout).
- `NextMintSourceHash()` — Deterministic mint source hash generation for test data.
- `BridgeRelayer` (Phase 2) — Already wires burn detection → `MintFunds`. The E2E test exercises this path end-to-end.
- `PublicChainInputValidator::VerifyPublicChainSmartContract()` — Already queries 3+ RPC endpoints. The E2E test validates this works against live Sepolia.

### Established Patterns
- GTest fixture with `SetUpTestSuite`/`TearDownTestSuite` for multi-node tests
- `DevConfig` per node with separate `BaseWritePath`
- PubSub `AddPeers` for node bootstrapping
- `ASSERT_WAIT_FOR_CONDITION` for async state polling
- `EXPECT_OUTCOME_TRUE` for outcome::result assertions
- `GTEST_SKIP()` for environment-dependent tests

### Integration Points
- `GeniusNode::New()` — Creates a full node with all subsystems (CRDT, PubSub, Consensus, Watcher)
- `GeniusNode::GetPubSub()->AddPeers()` — Bootstraps node-to-node connectivity
- `UTXOManager::GetUTXOs()` — Query destination account's UTXO set for minted token verification
- `cast send` (external) — Foundry CLI for sending burn transaction to Sepolia

</code_context>

<specifics>
## Specific Ideas

- Follow `processing_multi_test.cpp` as the structural template — same 3-node setup, same PubSub bootstrap pattern
- The burn trigger shells out to `cast send` because evmrelay lacks signing capability
- Self-transfer burn: same wallet sends and receives (PRIVATE_KEY env var)
- 10-second timeout for the full pipeline (accounts for Sepolia block confirmation)
- Negative tests are mandatory and can use mocked RPC for speed — only the positive path needs live Sepolia
- Not CI-runnable — guarded by `RUN_E2E_BRIDGE` env var check

</specifics>

<deferred>
## Deferred Ideas

### D-DEF-1: RPC Endpoint Initialization Tests (new phase)
Validate that `InitializeRpcEndpoints()` correctly configures minimum 3 RPC endpoints per chain:
- **Public ChainList endpoints** from `chainid.network/chains.json` (weight 25 each)
- **Optional API-key'd direct endpoints** (weight 50 each) supplied via `ChainRpcProviderConfig::direct_endpoints`
- **Testnets** (Sepolia, Amoy, BNB testnet, Base Sepolia) must also be covered — currently `InitializeRpcEndpoints()` only maps 4 mainnet chains
- **ChainList caching** — currently `ChainRpcEndpointProvider::Initialize()` reads from disk on every call with no in-memory cache or TTL
- Test should verify: each chain (mainnet + testnet) reaches the 75-weight consensus threshold with its configured endpoints

### D-DEF-2: Multi-Chain Bridge Stress Test (new phase)
Stress test the bridge pipeline under concurrent multi-chain load:
- Mock bridge signals on ALL configured chains simultaneously
- Fire bridge events every 2-3 seconds across all chains
- Verify: correct UTXO consensus per chain, no cross-chain message confusion, no dropped events, correct chain_id in MintTransactionV2
- Validates the system handles realistic multi-chain bridge traffic

</deferred>

---

*Phase: 04-End-to-End Integration Test*
*Context gathered: 2026-05-31*
