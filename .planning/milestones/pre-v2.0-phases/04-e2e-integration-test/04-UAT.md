---
status: partial
phase: 04-e2e-integration-test
source: 04-01-SUMMARY.md, 04-02-SUMMARY.md, 04-03-SUMMARY.md, 04-RESEARCH.md
started: 2026-06-03T17:00:00Z
updated: 2026-06-03T17:00:00Z
---

## Current Test

[testing paused — 4 items blocked awaiting Sepolia access]

## Tests

### 1. Build and Skip Guard
expected: bridge_e2e_test target compiles. Without RUN_E2E_BRIDGE env var, all tests skip cleanly. With RUN_E2E_BRIDGE=1 but no PRIVATE_KEY, tests skip with clear message.
result: pass

### 2. CMake Integration
expected: test/src/CMakeLists.txt contains add_subdirectory(bridge_e2e). bridge_e2e_test links against genius_node_test.
result: pass

### 3. InvalidReceiptLogsRejected
expected: Standalone test runs without env vars. verify_receipt_log returns kMatched/kContractMismatch/kTopic0Mismatch correctly.
result: pass

### 4. ERC-20 vs ERC-1155 Consistency
expected: Burn method in test code matches deployed contract. RESEARCH.md flagged potential mismatch.
result: pass — code correct (ERC-1155), CONTEXT.md D-09 and send_test_transactions.sh docs need updating from ERC-20 to ERC-1155

### 5. BurnToMintPipeline (Positive E2E)
expected: With RUN_E2E_BRIDGE=1 + PRIVATE_KEY + cast: Sepolia burn → MintTokens → UTXO balance increases within 10s.
result: blocked
blocked_by: server
reason: requires live Sepolia access (RUN_E2E_BRIDGE=1 + PRIVATE_KEY + cast binary)

### 6. ReplayRejection
expected: MintTokens twice with same burn tx hash. Second call rejected by dedup cache.
result: blocked
blocked_by: server
reason: requires live Sepolia access (RUN_E2E_BRIDGE=1 + PRIVATE_KEY)

### 7. MissingEndpointsFailClosed
expected: MintTokens for chain "999999" fails closed — no RPC endpoints.
result: blocked
blocked_by: server
reason: requires live Sepolia access (RUN_E2E_BRIDGE=1 + PRIVATE_KEY)

### 8. SlotKeyCollisionResistance
expected: Two mints with same params but different burn hashes — both succeed, proving distinct slot keys.
result: blocked
blocked_by: server
reason: requires live Sepolia access (RUN_E2E_BRIDGE=1 + PRIVATE_KEY)

## Summary

total: 8
passed: 4
issues: 0
pending: 0
skipped: 0
blocked: 4

## Gaps

[none yet]
