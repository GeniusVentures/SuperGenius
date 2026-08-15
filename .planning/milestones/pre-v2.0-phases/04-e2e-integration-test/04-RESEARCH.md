# Phase 4: End-to-End Integration Test - Research

**Researched:** 2026-06-03
**Domain:** C++ GTest multi-node integration test, EVM smart contract interaction via shell, UTXO consensus polling
**Confidence:** HIGH

## Summary

Phase 4 validates the complete EVM bridge pipeline end-to-end: EVM burn on Sepolia → BridgeRelayer detection → MintTransactionV2 → UTXO consensus (2-of-3 quorum) → RPC verification → minted tokens. The test infrastructure, fixture, and all test cases already exist in `test/src/bridge_e2e/bridge_e2e_test.cpp` (750 lines) and were committed on 2026-05-31 across 3 plans (04-01, 04-02, 04-03). The build is wired via `test/src/bridge_e2e/CMakeLists.txt` and registered in `test/src/CMakeLists.txt`.

The test follows the established `processing_multi_test.cpp` pattern: 3 GeniusNode instances in a static fixture with separate `BaseWritePath` per node, PubSub `AddPeers` bootstrap in `SetUpTestSuite`, and data cleanup in `TearDownTestSuite`. The burn trigger shells out to `cast send` (Foundry) since evmrelay lacks signing capability. UTXO confirmation is verified via polling `node_main->GetBalance()` with `ASSERT_WAIT_FOR_CONDITION` / `EXPECT_WAIT_FOR_CONDITION` templates (10s timeout).

**Primary recommendation:** The implementation is complete and ready for verification. Focus planning effort on: (1) confirming the burn method matches the actual Sepolia contract interface, (2) ensuring the event_topic0 used for RPC log verification aligns with the contract's emitted events, and (3) defining the UAT verification criteria that a human will execute.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** GTest integration test following the `processing_multi_test.cpp` pattern — 3 `GeniusNode` instances in a `SetUpTestSuite`, bootstrapped via PubSub `AddPeers`.
- **D-02:** Shell out to `cast send` (Foundry) for the burn trigger. evmrelay does not have transaction-signing or sending capability.
- **D-03:** NOT CI-runnable. Guard with `if (!getenv("RUN_E2E_BRIDGE")) GTEST_SKIP();`.
- **D-04:** Live Sepolia testnet. GNUS contract at `0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70`.
- **D-05:** `PRIVATE_KEY` env var provides both sender and destination wallet. Sepolia ETH funding is a pre-requisite.
- **D-06:** Sepolia RPC URLs from `chains_config.json` or overridden via `RPC_SEPOLIA`.
- **D-07:** 3 nodes, 2-of-3 quorum.
- **D-08:** Separate `BaseWritePath` per node (node1/, node2/, node3/). PubSub `AddPeers` bootstrap.
- **D-09:** `system()` or `popen()` to invoke `cast send`. Burn is `transfer(address,uint256)` — self-transfer.
- **D-10:** Check `cast` binary presence; skip if not found.
- **D-11:** Poll UTXO set via CRDT state (GlobalDB/UTXOManager) using `wait_condition.hpp`. 10s timeout.
- **D-12:** Pipeline should complete quickly; 10s timeout accounts for Sepolia block confirmation.
- **D-13:** Verify: MintTransactionV2 fields (chain_id, amount, token_id, burn_tx_hash), UTXO certificate, minted UTXO in recipient's set.
- **D-14:** Mandatory negatives: replay rejection, missing endpoints fail-closed, invalid receipt logs rejected.
- **D-15:** Negative tests can use mocked RPC; only positive E2E needs live Sepolia.

### the agent's Discretion
- Test file: `test/src/bridge_e2e/bridge_e2e_test.cpp`
- CMake: `test/src/bridge_e2e/CMakeLists.txt`
- Fixture class: `BridgeE2ETest`
- Cleanup: `TearDownTestSuite` with data cleanup

### Deferred Ideas (OUT OF SCOPE)
- D-DEF-1: RPC Endpoint Initialization Tests (new phase)
- D-DEF-2: Multi-Chain Bridge Stress Test (new phase)
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| E2E-01 | Create test/src/bridge_e2e/ with CMakeLists.txt and BridgeE2ETest fixture | Implemented — 3-node fixture with separate DevConfig per node, PubSub bootstrap |
| E2E-02 | Wire into test/src/CMakeLists.txt via add_subdirectory | Implemented — line 4 of test/src/CMakeLists.txt |
| E2E-03 | Positive E2E: burn on Sepolia via cast send → detection → MintTransactionV2 → UTXO consensus → minted tokens | Implemented — BurnToMintPipeline TEST_F |
| E2E-04 | Negative: replay rejection (same burn tx hash twice → deduplicated) | Implemented — ReplayRejection TEST_F |
| E2E-05 | Negative: missing RPC endpoints → fail-closed (Phase 3 D-03) | Implemented — MissingEndpointsFailClosed TEST_F |
| E2E-06 | Negative: invalid receipt logs → rejected (Phase 3 D-05/D-06) | Implemented — InvalidReceiptLogsRejected TEST() |
| E2E-07 | Slot key: two distinct burns with identical params → different slot keys | Implemented — SlotKeyCollisionResistance TEST_F |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Node lifecycle (3 GeniusNode instances) | API/Backend | — | C++ process manages full node infrastructure (CRDT, PubSub, Consensus, Watcher) |
| EVM burn trigger | External CLI | — | `cast send` is an external Foundry binary called via `popen()` |
| Burn detection → MintFunds | API/Backend | — | BridgeRelayer + TransactionManager::MintFunds() run in-process |
| RPC verification (3+ endpoints) | API/Backend | External RPC | PublicChainInputValidator queries live Sepolia RPC endpoints |
| UTXO consensus (2-of-3 quorum) | API/Backend | — | PubSub gossip between 3 in-process nodes |
| UTXO balance assertion | API/Backend | — | GeniusNode::GetBalance() → UTXOManager::GetUTXOs() queries in-process CRDT |
| Slot key collision verification | API/Backend | — | ConsensusManager::GetSlotKey() runs in-process |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Google Test (GTest) | Project bundled | Test framework, fixture lifecycle, assertions | Already used across 23+ test directories |
| Foundry `cast` | External binary | EVM transaction signing and submission | Only tool in the project ecosystem for EVM transaction signing; evmrelay lacks this capability |
| evmrelay (submodule) | 59d1ed2 | RPC transport, receipt parsing, BridgeEventClaim, verify_receipt_log | Already integrated in Phases 1-3 |
| Boost::dll | Project bundled | Binary path resolution for test data directories | Already used in processing_multi_test.cpp |
| spdlog | Project bundled | Structured logging | Already used throughout the codebase |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `testutil/wait_condition.hpp` | Project | Polling-based async assertions (ASSERT_WAIT_FOR_CONDITION, EXPECT_WAIT_FOR_CONDITION) | When waiting for async consensus/UTXO state changes |
| `testutil/outcome.hpp` | Project | outcome::result assertions (EXPECT_OUTCOME_TRUE, ASSERT_OUTCOME_SUCCESS) | When calling MintTokens() or other outcome-returning functions |
| `testutil/mint_source_hash.hpp` | Project | Deterministic mint source hash generation (NextMintSourceHash()) | For tests that need unique burn tx hashes without live testnet interaction |
| `eth/bridge_event.hpp` | evmrelay | BridgeEventClaim, verify_receipt_log(), ReceiptLogVerificationResult | For the InvalidReceiptLogsRejected negative test |
| `eth/objects.hpp` | evmrelay | eth::Address, eth::Hash256, eth::codec::Receipt, eth::codec::LogEntry | For constructing mock receipt data in negative tests |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `popen()` for cast send | `system()` | `popen()` allows reading stdout for tx hash parsing; `system()` returns only exit code |
| Live Sepolia RPC | Mocked RPC transport | Mock would be CI-runnable but would not validate real bridge pipeline against testnet |
| Direct ConsensusManager unit test for slot keys | E2E balance verification | ConsensusManager requires 6 constructor dependencies and can't be instantiated in isolation; E2E approach verifies the fix indirectly via node behavior |
| strncpy for BaseWritePath | std::string assignment | DevConfig.BaseWritePath changed from char[] to std::string — strncpy is incompatible with current definition |

**Installation:**
```bash
# No npm/pip packages required — this is a C++ GTest integration test
# External dependency: Foundry cast binary
# Install: curl -L https://foundry.paradigm.xyz | bash && foundryup
```

**Version verification:** All libraries are project-bundled (GTest, Boost, spdlog, evmrelay) — no external package versioning applies. The `cast` binary is an external CLI dependency with no C++ library linkage.

## Package Legitimacy Audit

> No external packages are installed by this phase. The test is a pure C++ GTest binary that links against existing project libraries (`genius_node_test`). The only external dependency is the `cast` binary from Foundry, which is a system CLI tool, not an npm/pip/cargo package.

**External tool dependency (not a package):**

| Tool | Source | Version Check | Authenticity |
|------|--------|---------------|--------------|
| `cast` (Foundry) | https://github.com/foundry-rs/foundry | `cast --version` | Well-established ecosystem tool — verified via official GitHub org |

**Packages removed due to slopcheck [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** none

## Architecture Patterns

### System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         BridgeE2ETest GTest Fixture                         │
│                                                                             │
│  SetUpTestSuite():                                                          │
│  ┌──────────────┐    PubSub AddPeers    ┌──────────────┐                   │
│  │  node_main   │◄────────────────────►│  node_proc1  │                   │
│  │  (full node) │     ┌──────────────┐ │ (processor)  │                   │
│  │  port:40001  │◄────│  node_proc2  │─┤  port:40002  │                   │
│  │  path:node1/ │     │ (processor)  │ │  path:node2/ │                   │
│  └──────┬───────┘     │  port:40003  │ └──────┬───────┘                   │
│         │             │  path:node3/ │        │                            │
│         │             └──────┬───────┘        │                            │
│         │                    │                 │                            │
│         ▼                    ▼                 ▼                            │
│  ┌─────────────────────────────────────────────────┐                       │
│  │  2-of-3 UTXO Consensus via PubSub Gossip        │                       │
│  │  BridgeRelayer → TransactionManager::MintFunds  │                       │
│  │  PublicChainInputValidator (3+ RPC endpoints)   │                       │
│  └─────────────────────────────────────────────────┘                       │
│                                                                             │
│  BurnToMintPipeline TEST_F:                                                 │
│  ┌──────────┐    popen()     ┌──────────────┐    MintTokens()              │
│  │ cast send├───────────────►│ Sepolia GNUS  ├──────────────────────┐       │
│  │ (Foundry)│   EVM burn tx  │ 0x9af805...  │                      │       │
│  └──────────┘                └──────┬───────┘                      │       │
│         ▲                           │ tx_hash                      │       │
│         │  PRIVATE_KEY              ▼                              │       │
│    ┌────┴─────┐           ┌──────────────────┐                     │       │
│    │  Env Vars│           │ node_main polls  │                     │       │
│    │PRIVATE_KEY          │ UTXO set via     │                     │       │
│    │RPC_SEPOLIA          │ GetBalance()     │◄────────────────────┘       │
│    └──────────┘          └────────┬─────────┘                             │
│                                   │                                        │
│                            ┌──────▼──────┐                                │
│                            │ UTXO balance│                                │
│                            │ >= initial  │                                │
│                            │ + amount    │                                │
│                            └─────────────┘                                │
│                                                                             │
│  Negative Tests:                                                            │
│  ┌─────────────────────────┐  ┌────────────────────┐  ┌──────────────────┐│
│  │ ReplayRejection:       │  │ MissingEndpoints:  │  │ InvalidReceipt   ││
│  │ MintTokens(x2) with    │  │ MintTokens with    │  │ LogsRejected:    ││
│  │ same burn_tx_hash →    │  │ chain_id="999999"  │  │ verify_receipt_  ││
│  │ second rejected by     │  │ (no endpoints) →   │  │ log() with mock  ││
│  │ dedup cache            │  │ fail-closed        │  │ mismatched data  ││
│  └─────────────────────────┘  └────────────────────┘  └──────────────────┘│
└─────────────────────────────────────────────────────────────────────────────┘
```

### Recommended Project Structure
```
test/src/bridge_e2e/
├── CMakeLists.txt            # CMake target: bridge_e2e_test, links genius_node_test
└── bridge_e2e_test.cpp       # BridgeE2ETest fixture + 5 test cases (750 lines)
```

Already wired in:
```
test/src/CMakeLists.txt       # line 4: add_subdirectory(bridge_e2e)
```

### Pattern 1: Multi-Node GTest Fixture with Static Setup

**What:** Three GeniusNode instances created in `SetUpTestSuite()`, bootstrapped via PubSub `AddPeers`, torn down in `TearDownTestSuite()` with data directory cleanup.

**When to use:** Any test that requires consensus across multiple in-process validator nodes.

**Key implementation details:**
- Static `shared_ptr<GeniusNode>` members (node_main, node_proc1, node_proc2)
- Static `DevConfig` per node with separate `BaseWritePath`
- `GeniusNode::New()` called with: DevConfig, private_key, autodht=false, isprocessor=false, base_port, is_full_node
- `Boost::dll::program_location()` for binary-relative paths
- `SetAuthorizedFullNodeAddress()` for genesis block creation
- 60s polling for node READY state transition
- `std::filesystem::remove_all()` for data cleanup in `TearDownTestSuite`

**Reference:** `test/src/processing_multi/processing_multi_test.cpp` (lines 30-113)
[VERIFIED: codebase]

### Pattern 2: Shell-Out for External CLI Interaction

**What:** Use `popen()` to invoke `cast send` and parse JSON output for the transaction hash.

**When to use:** When evmrelay lacks transaction signing capability and a real EVM transaction must be sent.

**Key implementation details:**
- Check binary presence via `popen("which cast 2>/dev/null", "r")` before attempting
- Derive sender address: `cast wallet address <private_key>`
- Send transaction: `cast send <contract> "<sig>" <args> --private-key <key> --rpc-url <url> --json`
- Parse `transactionHash` from JSON output via string search
- Check `pclose()` exit code for success

**Reference:** `evmrelay/examples/send_test_transactions.sh` (lines 140-198)
[VERIFIED: codebase]

### Pattern 3: Async UTXO Consensus Polling

**What:** Poll node balance with `ASSERT_WAIT_FOR_CONDITION`/`EXPECT_WAIT_FOR_CONDITION` templates to verify minted tokens appear in the UTXO set.

**When to use:** When verifying that consensus has produced a certificate and the minted UTXO is reflected in the destination account's balance.

**Key implementation details:**
- Call `node_main->GetBalance(dest_addr)` in the condition lambda
- Compare against pre-mint balance: `balance > initial_balance`
- 10s timeout default (accounts for Sepolia block confirmation)
- 10ms check interval (internal default in `waitForCondition()`)

**Reference:** `test/testutil/wait_condition.hpp` (lines 67-145)
[VERIFIED: codebase]

### Pattern 4: Environment-Guarded Test Execution

**What:** Skip entire test suite when required environment variables or external tools are missing.

**When to use:** For tests that depend on live testnets, private keys, or external binaries.

**Guard order (in SetUpTestSuite):**
1. `RUN_E2E_BRIDGE` env var → `GTEST_SKIP()`
2. `SIGNING_KEY` or `PRIVATE_KEY` env var → validate hex/base64 → `GTEST_SKIP()`
3. `cast` binary → check via `popen("which cast")` → `GTEST_SKIP()`

### Pattern 5: Standalone TEST() for Env-Independent Negatives

**What:** Use `TEST()` instead of `TEST_F()` for tests that don't need the multi-node fixture.

**When to use:** For tests that run entirely with mock data (no live testnet, no private key).

**Example:** `TEST(BridgeE2ENegativeTest, InvalidReceiptLogsRejected)` — constructs mock `eth::codec::Receipt` and `eth::BridgeEventClaim` directly.

### Anti-Patterns to Avoid
- **strncpy on std::string:** DevConfig.BaseWritePath is `std::string`, not `char[]`. Use direct assignment. The processing_multi_test.cpp uses strncpy because it predates the type change. [VERIFIED: codebase — SUMMARY 04-01]
- **sleep_for instead of polling:** Don't use fixed sleeps for consensus confirmation. Use `ASSERT_WAIT_FOR_CONDITION`/`EXPECT_WAIT_FOR_CONDITION` which poll at 10ms intervals.
- **Assuming ConsensusManager can be unit tested:** Cannot instantiate ConsensusManager in isolation (requires 6 constructor dependencies). Verify slot key behavior indirectly through node behavior. [VERIFIED: codebase — SUMMARY 04-03]
- **Using system() instead of popen():** `system()` returns only exit code. `popen()` allows reading stdout to parse the transaction hash from cast's JSON output.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| EVM transaction signing/sending | Custom Web3 C++ integration | `cast send` via `popen()` | evmrelay lacks signing; Foundry is the standard Ethereum CLI toolset |
| Async state polling | Custom sleep-loop with sleeps | `wait_condition.hpp` templates | Already battle-tested in the codebase; avoids timing fragility |
| UTXO balance query | Direct CRDT/GlobalDB query | `GeniusNode::GetBalance(address)` | Properly encapsulates UTXOManager internals |
| RPC endpoint configuration for tests | Modify chains_config.json | `GeniusNode::ConfigureRpcEndpoint()` | Per-node RPC endpoint injection without config file changes |
| Receipt log verification | Custom log matching | `eth::verify_receipt_log()` | Already implemented in evmrelay with comprehensive error codes |
| Deterministic test burn hashes | Random hash generation | `sgns::test::NextMintSourceHash()` | Deterministic, repeatable across test runs |

**Key insight:** The bridge E2E test demonstrates the complete pipeline by orchestrating existing components, not by building new infrastructure. Every step (mint creation, consensus, RPC verification, UTXO confirmation) uses established APIs from Phases 1-3. The only novel integration is the shell-out to `cast send`.

## Common Pitfalls

### Pitfall 1: Burn Method Mismatch Between cast send and RPC Log Verification

**What goes wrong:** The test sends an ERC-1155 `safeTransferFrom` but the deployed Sepolia GNUS contract may only emit ERC-20 `Transfer` events (topic0: `0xddf252ad...`). The RPC log verification in `VerifyPublicChainSmartContract` checks for event_topic0 `0xc3d58168...` (ERC-1155 TransferSingle). If these don't match what the contract emits, RPC verification will fail.

**Why it happens:** CONTEXT.md D-09 specifies ERC-20 `transfer(address,uint256)`, but the implementation uses ERC-1155 `safeTransferFrom`. The Sepolia contract at `0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70` must be verified to confirm which interface it implements.

**How to avoid:** Verify the actual Sepolia GNUS contract ABI before running the test. The `send_test_transactions.sh` script uses `transfer(address,uint256)` (ERC-20), suggesting the contract implements ERC-20. If so, the cast send command should use `transfer(address,uint256)` and the event_topic0 should be `0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef` (keccak256 of `Transfer(address,address,uint256)`).

**Warning signs:** RPC log verification failures in `VerifyPublicChainSmartContract` with "log mismatch bridge=... topic0=...".

**Confidence:** MEDIUM — sources disagree. CONTEXT.md and `send_test_transactions.sh` indicate ERC-20. The test implementation assumes ERC-1155. The contract must be inspected to resolve.

### Pitfall 2: Node READY State Timeout on Slow Machines

**What goes wrong:** The 60s timeout for `node_main->GetState() == READY` may be insufficient on slow machines or when database initialization takes longer than expected.

**Why it happens:** Database migrations, CRDT initialization, and blockchain genesis creation all happen asynchronously. Slow I/O or large existing databases can extend startup time.

**How to avoid:** The 60s timeout is already generous for fresh test data directories. If timeouts occur, check for stale data directories from previous test runs (TearDownTestSuite cleans these up, but crashes can leave them). Consider increasing timeout or adding diagnostic logging.

**Warning signs:** "node_main not ready" assertion failure in SetUpTestSuite.

### Pitfall 3: Sepolia RPC Endpoint Availability

**What goes wrong:** The 6 hardcoded Sepolia RPC endpoints may be unavailable, rate-limited, or return inconsistent data.

**Why it happens:** Public RPC endpoints have no SLA. Some may be temporarily down, rate-limited, or behind by several blocks.

**How to avoid:** The test configures 6 endpoints at consensus_weight=25 each, requiring 3 successes to reach the 75-weight threshold. This provides redundancy. The `RPC_SEPOLIA` env var allows overriding with a preferred endpoint. If all public endpoints fail, the test assertions will catch it.

**Warning signs:** "VerifyPublicChainSmartContract insufficient consensus" errors during test execution.

### Pitfall 4: Sepolia Transaction Nonce Conflicts

**What goes wrong:** Multiple test runs or concurrent usage of the same PRIVATE_KEY can cause nonce conflicts, where `cast send` fails because a previous transaction is still pending.

**Why it happens:** The test sends a real transaction to Sepolia. If a previous test run's transaction hasn't been mined, the nonce may be wrong.

**How to avoid:** Wait for previous transactions to confirm before re-running. The test is manual-only (not CI), so this is manageable. Consider checking pending nonce via `cast nonce` before sending.

**Warning signs:** "cast send failed with exit code" containing nonce-related errors.

## Code Examples

### GTest Multi-Node Fixture Setup (from bridge_e2e_test.cpp)
```cpp
// Source: test/src/bridge_e2e/bridge_e2e_test.cpp lines 120-348
// [VERIFIED: codebase]

class BridgeE2ETest : public ::testing::Test
{
protected:
    static std::shared_ptr<GeniusNode> node_main;
    static std::shared_ptr<GeniusNode> node_proc1;
    static std::shared_ptr<GeniusNode> node_proc2;
    static DevConfig gGeniusNodeConfig;
    static DevConfig gGeniusNodeConfig2;
    static DevConfig gGeniusNodeConfig3;
    static std::string s_eth_private_key;

    static void SetUpTestSuite()
    {
        // Guard 1: opt-in env var
        if (!std::getenv("RUN_E2E_BRIDGE")) {
            GTEST_SKIP() << "Set RUN_E2E_BRIDGE=1 to run E2E bridge tests";
        }
        // Guard 2: PRIVATE_KEY env var (with base64 fallback)
        // Guard 3: cast binary check via popen("which cast")
        
        // Per-node BaseWritePath
        std::string binary_path = boost::dll::program_location().parent_path().string();
        gGeniusNodeConfig.BaseWritePath  = binary_path + "/node1/";
        gGeniusNodeConfig2.BaseWritePath = binary_path + "/node2/";
        gGeniusNodeConfig3.BaseWritePath = binary_path + "/node3/";
        
        // Full node first (creates genesis)
        node_main = GeniusNode::New(gGeniusNodeConfig, s_eth_private_key.c_str(), false, false, 40001, true);
        sgns::Blockchain::SetAuthorizedFullNodeAddress(node_main->GetAddress());
        // Wait for READY state (60s timeout)
        
        // Processor nodes
        node_proc1 = GeniusNode::New(gGeniusNodeConfig2, s_eth_private_key.c_str(), false, false, 40002);
        node_proc2 = GeniusNode::New(gGeniusNodeConfig3, s_eth_private_key.c_str(), false, false, 40003);
        
        // PubSub bootstrap
        node_proc1->GetPubSub()->AddPeers({...});
        node_proc2->GetPubSub()->AddPeers({...});
        
        // Configure Sepolia RPC endpoints
        node_main->ConfigureRpcEndpoint("11155111", sepolia_eps);
    }

    static void TearDownTestSuite()
    {
        node_main.reset();
        node_proc1.reset();
        node_proc2.reset();
        std::filesystem::remove_all(gGeniusNodeConfig.BaseWritePath);
        std::filesystem::remove_all(gGeniusNodeConfig2.BaseWritePath);
        std::filesystem::remove_all(gGeniusNodeConfig3.BaseWritePath);
    }
};
```

### Burn Trigger via popen() + cast send
```cpp
// Source: test/src/bridge_e2e/bridge_e2e_test.cpp lines 376-439
// [VERIFIED: codebase]

// Derive sender address
std::string wallet_cmd = "cast wallet address " + s_eth_private_key + " 2>&1";
FILE *wallet_pipe = popen(wallet_cmd.c_str(), "r");
char addr_buf[256] = {};
std::fgets(addr_buf, sizeof(addr_buf), wallet_pipe);
pclose(wallet_pipe);
std::string sender_addr(addr_buf);
sender_addr.pop_back(); // trim newline

// Send burn transaction
std::string cast_cmd = "cast send " + std::string(kSepoliaContract) +
    " \"" + kTransferSig + "\" " + sender_addr + " " + sender_addr +
    " 0 " + std::to_string(kMintAmount) +
    " 0x --private-key " + s_eth_private_key +
    " --rpc-url " + kSepoliaRpc + " --json 2>&1";
FILE *cast_pipe = popen(cast_cmd.c_str(), "r");
// Read output...
int cast_rc = pclose(cast_pipe);
ASSERT_EQ(cast_rc, 0);

// Parse transactionHash from JSON
size_t hash_pos = cast_output.find("\"transactionHash\":\"0x");
// Extract tx_hash...
```

### UTXO Balance Polling with wait_condition
```cpp
// Source: test/src/bridge_e2e/bridge_e2e_test.cpp lines 449-463
// [VERIFIED: codebase]

uint64_t initial_balance = node_main->GetBalance(dest_addr);

// Trigger mint
EXPECT_OUTCOME_TRUE(mint_result,
    node_main->MintTokens(kMintAmount, tx_hash, "11155111",
                          sgns::TokenID::FromBytes({0x00}), dest_addr, kMintTimeout));

// Poll for UTXO confirmation
std::chrono::milliseconds e2e_timeout{10000};
EXPECT_WAIT_FOR_CONDITION(
    [&]() { return node_main->GetBalance(dest_addr) > initial_balance; },
    e2e_timeout,
    "Minted UTXO appears in recipient balance on node_main",
    nullptr);

uint64_t final_balance = node_main->GetBalance(dest_addr);
EXPECT_GE(final_balance - initial_balance, kMintAmount);
```

### Mock Receipt for Negative Log Verification Test
```cpp
// Source: test/src/bridge_e2e/bridge_e2e_test.cpp lines 667-749
// [VERIFIED: codebase]

// STANDALONE TEST() — no fixture needed, no env vars required
TEST(BridgeE2ENegativeTest, InvalidReceiptLogsRejected)
{
    // Build mock receipt with LogEntry
    eth::codec::LogEntry log_entry;
    log_entry.address = test_addr;
    log_entry.topics.push_back(test_topic0);
    
    eth::codec::Receipt mock_receipt;
    mock_receipt.status = true;
    mock_receipt.logs.push_back(log_entry);
    
    eth::ReceiptResult receipt_result;
    receipt_result.receipt = mock_receipt;
    
    // Matching claim → succeeds
    auto match_result = eth::verify_receipt_log(receipt_result, matching_claim);
    EXPECT_TRUE(match_result);
    EXPECT_EQ(match_result.error, eth::ReceiptLogVerificationError::kNone);
    
    // Wrong contract → kContractMismatch
    // Wrong topic0 → kTopic0Mismatch
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `strncpy` for DevConfig.BaseWritePath | Direct `std::string` assignment | 2026-05-31 (Plan 04-01) | DevConfig.BaseWritePath changed from `char[]` to `std::string` — strncpy is now incompatible |
| 5-arg GeniusNode::New (random key) | 6-arg GeniusNode::New (private key) | Always for E2E | E2E test needs deterministic keys from PRIVATE_KEY for Sepolia interaction |
| Unit-testing ConsensusManager directly | Indirect E2E verification | 2026-05-31 (Plan 04-03) | ConsensusManager requires 6 ctor deps — can't be instantiated in unit tests |

**Deprecated/outdated:**
- `strncpy` pattern for BaseWritePath — replaced by direct assignment matching current `std::string` type
- Direct ConsensusManager unit testing for slot key validation — replaced by E2E node behavior verification

## Assumptions Log

> All claims tagged `[ASSUMED]` in this research. The planner and discuss-phase use this section to identify decisions that need user confirmation before execution.

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The Sepolia GNUS contract at `0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70` supports ERC-1155 `safeTransferFrom` and emits `TransferSingle` events with topic0 `0xc3d58168...` | Common Pitfalls #1 | **HIGH** — The test will fail with RPC log verification errors. CONTEXT.md D-09 specifies ERC-20 `transfer(address,uint256)`, and `send_test_transactions.sh` uses ERC-20. The ERC-1155 implementation may be incorrect for this contract. |
| A2 | The 6 hardcoded Sepolia RPC endpoints are sufficient and reachable | Common Pitfalls #3 | MEDIUM — If 4+ endpoints are down, verification fails. The `RPC_SEPOLIA` override provides a fallback. |
| A3 | Node READY state completes within 60s on the developer's machine | Common Pitfalls #2 | LOW — Fresh test directories should initialize quickly. Only a concern on very slow machines or with stale data. |
| A4 | The `GeniusNode::New()` 7-parameter overload with default `use_upnp=true` (6 args passed) is the correct overload for the test | Architecture Patterns #1 | LOW — verified against GeniusNode.hpp signature. The 7th parameter defaults to true. |

## Open Questions

1. **Is the Sepolia GNUS contract ERC-20 or ERC-1155?**
   - What we know: D-09 says ERC-20 `transfer(address,uint256)`. `send_test_transactions.sh` uses ERC-20. The test code uses ERC-1155 `safeTransferFrom` and ERC-1155 TransferSingle topic0.
   - What's unclear: Which interface does the actual deployed contract at `0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70` support?
   - Recommendation: **Verify the contract ABI before UAT.** Run `cast call 0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70 "supportsInterface(bytes4)(bool)" 0xd9b67a26` (ERC-1155 interface ID) and `0x36372b07` (ERC-20 via optional metadata). If the contract is ERC-20 only, update: (a) `kTransferSig` to `"transfer(address,uint256)"`, (b) `kEventTopic0` to `0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef` (keccak256 of `Transfer(address,address,uint256)`), (c) remove the `0x` data parameter from cast send.

2. **Should the slot key collision test use live Sepolia or mock data?**
   - What we know: The current implementation uses `NextMintSourceHash()` for burn hashes (mock data, no live transaction). It then calls `MintTokens` which goes through `VerifyPublicChainSmartContract` — but the mock burn tx hashes won't have real Sepolia receipts, causing RPC verification to fail.
   - What's unclear: Will `MintTokens` succeed when `VerifyPublicChainSmartContract` queries Sepolia RPC for a fake tx hash?
   - Recommendation: The slot key test may need to (a) skip RPC verification for fake hashes, (b) use an RPC mock transport, or (c) be refactored to test `GetSlotKey()` behavior directly (if ConsensusManager can be accessed through node internals). Alternatively, the test currently checks if `MintTokens` succeeds with `EXPECT_OUTCOME_TRUE` — if RPC verification fails, the test will fail, not the mint. This needs runtime verification.

3. **Should the `MissingEndpointsFailClosed` test also run as a standalone TEST()?**
   - What we know: It's currently a `TEST_F(BridgeE2ETest, ...)` which requires the full fixture (RUN_E2E_BRIDGE + PRIVATE_KEY). But it doesn't need live Sepolia — it just needs a GeniusNode with no RPC endpoints for chain "999999".
   - What's unclear: Is it worth creating a lightweight fixture for this test to make it CI-runnable?
   - Recommendation: Low priority — the test works correctly in the fixture. Making it standalone would require a single-node lightweight setup. Document as a potential follow-up optimization.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| `cast` (Foundry) | Burn transaction signing + submission | ✗ (check at runtime) | — | Test skips via `GTEST_SKIP()` when missing |
| Sepolia RPC endpoints | `VerifyPublicChainSmartContract` log verification | ✓ (public) | — | 6 endpoints configured; need 3 for 75-weight threshold |
| `PRIVATE_KEY` env var | Burn transaction sender + node identity | ✗ (manual) | — | Test skips via `GTEST_SKIP()` when missing |
| `RUN_E2E_BRIDGE` env var | Test opt-in gate | ✗ (manual) | — | Test skips via `GTEST_SKIP()` when unset |
| Sepolia ETH | Gas for burn transaction | ✗ (pre-requisite) | — | Must be pre-funded by developer |
| GTest framework | Test execution | ✓ | Project bundled | — |
| `boost::dll` | Binary path resolution | ✓ | Project bundled | — |
| `genius_node_test` library | Test binary linking | ✓ | Project built | — |

**Missing dependencies with no fallback:**
- `cast` binary — must be installed by developer. Test skips gracefully.
- `PRIVATE_KEY` env var — must be set by developer. Test skips gracefully.
- Sepolia ETH — must be pre-funded. Test will fail at runtime (not at startup).

**Missing dependencies with fallback:**
- Sepolia RPC endpoints — 6 endpoints configured, 3 needed. If <3 are reachable, `RPC_SEPOLIA` env var can override.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Google Test (GTest) — project bundled |
| Config file | none — GTest auto-discovers tests in the binary |
| Quick run command | `RUN_E2E_BRIDGE=1 PRIVATE_KEY=0x... ./build/test/src/bridge_e2e/bridge_e2e_test --gtest_filter='*InvalidReceiptLogs*'` |
| Full suite command | `RUN_E2E_BRIDGE=1 PRIVATE_KEY=0x... ./build/test/src/bridge_e2e/bridge_e2e_test` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| E2E-03 | Positive E2E: Sepolia burn → mint → UTXO confirmation | integration | `./bridge_e2e_test --gtest_filter='*BurnToMintPipeline*'` | ✅ `test/src/bridge_e2e/bridge_e2e_test.cpp:373` |
| E2E-04 | Replay rejection: same burn hash twice → rejected | integration | `./bridge_e2e_test --gtest_filter='*ReplayRejection*'` | ✅ `test/src/bridge_e2e/bridge_e2e_test.cpp:567` |
| E2E-05 | Missing endpoints → fail-closed | integration | `./bridge_e2e_test --gtest_filter='*MissingEndpointsFailClosed*'` | ✅ `test/src/bridge_e2e/bridge_e2e_test.cpp:632` |
| E2E-06 | Invalid receipt logs → rejected | unit (standalone) | `./bridge_e2e_test --gtest_filter='*InvalidReceiptLogsRejected*'` | ✅ `test/src/bridge_e2e/bridge_e2e_test.cpp:667` |
| E2E-07 | Slot key collision: two distinct burns with same params → different slot keys | integration | `./bridge_e2e_test --gtest_filter='*SlotKeyCollisionResistance*'` | ✅ `test/src/bridge_e2e/bridge_e2e_test.cpp:480` |

### Sampling Rate
- **Per task commit:** N/A (all tests already implemented and committed)
- **Per wave merge:** Run `InvalidReceiptLogsRejected` (no env vars needed) to verify fixture integrity
- **Phase gate:** Full suite green on developer machine with `RUN_E2E_BRIDGE=1 PRIVATE_KEY=0x...` before `/gsd-verify-work`

### Wave 0 Gaps
- [x] `test/src/bridge_e2e/bridge_e2e_test.cpp` — exists, 750 lines, 5 test cases
- [x] `test/src/bridge_e2e/CMakeLists.txt` — exists, links genius_node_test
- [x] `test/src/CMakeLists.txt` — already has `add_subdirectory(bridge_e2e)` (line 4)
- [x] Framework install: GTest is project-bundled — no additional install required
- [ ] **Gap:** No dedicated `conftest` or shared fixture file (unnecessary — all test logic is self-contained in the single .cpp file per the project convention)

*(If no gaps above: "None — existing test infrastructure covers all phase requirements")*

**Summary:** All test infrastructure is in place. No Wave 0 gaps to address.

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A — test uses env var PRIVATE_KEY for deterministic identity; no user authentication in test code |
| V3 Session Management | no | N/A — no session state in test code |
| V4 Access Control | no | N/A — test runs in-process; no access control boundaries |
| V5 Input Validation | yes | Private key validation (hex 64-char or base64 → 32-byte), burn tx hash parsing, cast output parsing with string bounds checking |
| V6 Cryptography | yes | Private key is used for ECDSA signing via `cast send` (Foundry); key is read from env var and validated for format |

### Known Threat Patterns for C++ GTest E2E Bridge Tests

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Private key exposure in test logs | Information Disclosure | Test avoids logging the raw PRIVATE_KEY; only derived address is logged via spdlog |
| Shell injection via PRIVATE_KEY env var | Elevation of Privilege | `cast send` uses `--private-key` flag with the validated key string; `popen()` accepts the key as part of a command string — mitigation relies on `popen()` argument handling and key format validation (hex check) |
| RPC endpoint manipulation | Spoofing | `VerifyPublicChainSmartContract` requires ≥75 consensus weight from 3+ independent endpoints; single endpoint compromise cannot pass verification |
| Double-mint via replay | Repudiation | Test validates that `MintFunds()` reservation check + persistence check rejects duplicate burn tx hashes (Phase 3 dedup cache) |
| Fake burn transaction | Spoofing | `VerifyPublicChainSmartContract` queries 3+ independent RPC endpoints to confirm burn receipt exists on-chain; fake tx hashes fail at RPC verification |
| Unverified receipt logs | Tampering | `verify_receipt_log()` validates bridge contract address and event topic0 match configured values; test validates rejection of mismatched data |

## Sources

### Primary (HIGH confidence)
- `test/src/processing_multi/processing_multi_test.cpp` — 3-node GTest fixture pattern [VERIFIED: codebase]
- `test/src/bridge_e2e/bridge_e2e_test.cpp` — complete implementation (750 lines, 5 test cases) [VERIFIED: codebase]
- `test/src/bridge_e2e/CMakeLists.txt` — CMake target definition [VERIFIED: codebase]
- `test/src/CMakeLists.txt` — add_subdirectory(bridge_e2e) registration [VERIFIED: codebase]
- `test/testutil/wait_condition.hpp` — ASSERT_WAIT_FOR_CONDITION/EXPECT_WAIT_FOR_CONDITION templates [VERIFIED: codebase]
- `evmrelay/examples/send_test_transactions.sh` — cast send pattern, Sepolia contract address, PRIVATE_KEY usage [VERIFIED: codebase]
- `src/account/GeniusNode.hpp` — GeniusNode::New() signature, MintTokens(), ConfigureRpcEndpoint(), GetBalance() [VERIFIED: codebase]
- `src/account/PublicChainInputValidator.hpp` — WeightedRpcEndpoint struct, VerifyPublicChainSmartContract [VERIFIED: codebase]
- `src/account/PublicChainInputValidator.cpp` — RPC verification implementation with 75-weight consensus threshold [VERIFIED: codebase]
- `src/account/TransactionManager.cpp` (lines 620-709) — MintFunds() with reservation + persistence dedup [VERIFIED: codebase]
- `src/account/UTXOManager.hpp` — GetUTXOs() API [VERIFIED: codebase]
- `.planning/phases/04-e2e-integration-test/04-CONTEXT.md` — User decisions and constraints [VERIFIED: codebase]
- `.planning/phases/04-e2e-integration-test/04-DISCUSSION-LOG.md` — Decision rationale audit trail [VERIFIED: codebase]

### Secondary (MEDIUM confidence)
- `.planning/phases/04-e2e-integration-test/04-01-SUMMARY.md` — Plan 01 execution summary (fixture + positive E2E) [VERIFIED: codebase]
- `.planning/phases/04-e2e-integration-test/04-02-SUMMARY.md` — Plan 02 execution summary (negative tests) [VERIFIED: codebase]
- `.planning/phases/04-e2e-integration-test/04-03-SUMMARY.md` — Plan 03 execution summary (slot key collision) [VERIFIED: codebase]
- `evmrelay/examples/chains_config.json` — Sepolia chain configuration [VERIFIED: codebase]
- `src/account/GeniusNode.cpp` (lines 1950-1959) — ConfigureRpcEndpoint implementation [VERIFIED: codebase]

### Tertiary (LOW confidence)
- None — all claims are verified against the codebase or official project documentation.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all libraries are project-bundled or verified via SUMMARY files and codebase inspection
- Architecture: HIGH — fixture pattern verified against processing_multi_test.cpp; 5 test cases verified in bridge_e2e_test.cpp
- Pitfalls: MEDIUM — Pitfall #1 (burn method mismatch) is flagged as an unresolved inconsistency between CONTEXT.md (ERC-20) and implementation (ERC-1155); needs contract ABI verification
- Security: HIGH — threat patterns identified from codebase analysis and Phase 3 security fixes

**Research date:** 2026-06-03
**Valid until:** 2026-07-03 (30 days — stable project, no external API changes expected)
