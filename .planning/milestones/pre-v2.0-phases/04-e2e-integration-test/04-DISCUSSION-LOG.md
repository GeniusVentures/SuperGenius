# Phase 4: End-to-End Integration Test - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-31
**Phase:** 04-End-to-End Integration Test
**Areas discussed:** Test form factor, Test environment, Validator topology, Verification and assertions, Automation level

---

## Test Form Factor

| Option | Description | Selected |
|--------|-------------|----------|
| Shell script | `send_test_transactions.sh` driving a live node | |
| C++ GTest integration test | Multi-node fixture like `processing_multi_test.cpp` | ✓ |
| Hybrid | Shell for burn trigger, C++ for everything else | ✓ |

**User's choice:** E2E-style tests like `processing_multi_test.cpp` are preferred. But need to integrate RPC web3 in C++ instead of using what `send_test_transactions.sh` already does.
**Follow-up:** evmrelay does not currently have transaction-signing or sending capability. Decision: shell out to `cast send` for the burn trigger, do everything else in C++.

---

## Test Environment

| Option | Description | Selected |
|--------|-------------|----------|
| Live Sepolia testnet | Real GNUS contract, real network | ✓ |
| Local multi-node cluster | Mocked EVM, no testnet dependency | |
| Docker-compose | Containerized local setup | |

**User's choice:** Live Sepolia testnet is definitely needed. Environment variable with private key of wallet to use (see `send_test_transactions.sh`).
**Notes:** GNUS contract on Sepolia: `0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70`. PRIVATE_KEY env var provides both sender and destination (self-transfer burn).

---

## Validator Topology

| Option | Description | Selected |
|--------|-------------|----------|
| 3 nodes, 2-of-3 quorum | Standard test quorum | ✓ |
| 5 nodes, 3-of-5 quorum | Larger cluster for robustness | |
| Dynamic | Configurable node count | |

**User's choice:** 3 nodes, quorum threshold is 2 of 3 for testing.

---

## Verification and Assertions

| Option | Description | Selected |
|--------|-------------|----------|
| Query CRDT state | Poll UTXO set via GlobalDB/UTXOManager | ✓ |
| Query JSON-RPC | Use the API to check balance | |
| Wallet balance query | Check via wallet interface | |

**User's choice:** Query CRDT state for the destination UTXO confirmation. Timeout should be 10 seconds for tests. The voting and everything should happen really quickly over pub/sub.
**Notes:** The 10s timeout accounts for Sepolia block confirmation time (the slowest step). PubSub consensus over local nodes is fast.

---

## Automation Level

| Option | Description | Selected |
|--------|-------------|----------|
| CI-runnable | Part of the CI pipeline | |
| Repeatable demo | Manual but repeatable | ✓ |
| One-shot validation | Run once, not repeatable | |

**User's choice:** Not CI-runnable, should be skipped. The initial burn/bridge message should be caught by any nodes that have started up, but if not, those nodes can scan the RPC for syncing from last mintv2 UTXOs.
**Notes:** Guarded by `RUN_E2E_BRIDGE` env var check. Uses `GTEST_SKIP()` when env var not set.

---

## Negative Test Cases

**User's decision:** "Always test with negative case."
**Specific negatives (derived from Phase 3 fixes):**
- Burn tx hash replay → deduplicated (slot key collision fix)
- Missing RPC endpoints → rejected (fail-closed fix)
- Invalid receipt logs → rejected (log verification fix)

**Notes:** Negative tests can use mocked RPC for speed and determinism. Only the positive E2E path needs live Sepolia.

---

## Claude's Discretion

- Test file location: `test/src/bridge_e2e/bridge_e2e_test.cpp`
- CMake integration: `test/src/bridge_e2e/CMakeLists.txt`
- Fixture class name: `BridgeE2ETest`
- Cleanup in `TearDownTestSuite`

## Deferred Ideas

None — discussion stayed within phase scope.
