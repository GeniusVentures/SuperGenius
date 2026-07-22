---
status: complete
phase: 05-startup-wiring-mock-rpc
source: 05-01-SUMMARY.md, 05-02-SUMMARY.md, 05-03-SUMMARY.md, 05-04-SUMMARY.md, 05-05-SUMMARY.md, 05-06-SUMMARY.md
started: 2026-06-04T17:00:00Z
updated: 2026-06-04T18:00:00Z
---

## Current Test

number: 2
name: Existing tests pass — Phase 5 regression-free
expected: |
  `ctest --test-dir build -R "utxo_manager|bridge_relayer|public_chain"` — all existing
  tests that touch Phase 5 files still pass. No regressions introduced.
awaiting: user response

## Tests

### 1. Build — Phase 5 compiles without errors
expected: `cmake --build build --target sgns_genius_account` exits 0. All modified source files compile.
result: pass

### 2. Existing tests pass — Phase 5 regression-free
expected: `ctest --test-dir build -R "utxo_manager|bridge_relayer|public_chain"` — all existing tests that touch Phase 5 files still pass. No regressions introduced.
result: pass

### 3. Multi-chain BridgeRelayer::Start() API
expected: `ChainContractPair` struct exists. `Start(vector<ChainContractPair>)` signature replaces old `Start(string, string)`. `chain_watches_` map tracks per-chain watch IDs. Best-effort skips invalid addresses.
result: pass

### 4. MockRpcTransport — 6 failure modes implemented
expected: MockRpcTransport class with `MockBehavior` enum (kSuccess, kTimeout, kConnectionRefused, kBadJson, kWrongStatus, kWrongLogs). `call()` dispatches to correct canned response per behavior. Mock compiled into `test/src/mock/` static library only — not linked into production `genius_node`.
result: pass

### 5. TransportFactory DI — no compile-time flags
expected: `TransportFactory` typedef on `PublicChainInputValidator`. `VerifyPublicChainSmartContract()` uses factory call. No `#ifdef MOCK_RPC` anywhere. `SGNS_E2E_REAL_RPC=1` env var switches to real RPC at runtime. Production default factory creates real `RpcHttpTransport`.
result: pass

### 6. UTXO guard removal — zero guards remain
expected: `grep -c "!is_full_node_ && address != address_" src/account/UTXOManager.cpp` returns 0. All 8 foreign-address guards removed. `PutUTXO()` accepts foreign addresses.
result: pass

### 7. UTXO_RESERVED state and UTXOType enum
expected: `UTXO_RESERVED` member exists in `UTXOState` enum between READY and CONSUMED. `UTXOType` enum has `UTXO_NORMAL=0` and `UTXO_BRIDGE=1`. `UTXOEntry` has `type` field. `IsOutPointReserved()` predicate returns true for RESERVED.
result: pass

### 8. Startup wiring — InitializeAndStartBridge fires asynchronously
expected: `InitializeAndStartBridge()` declared in GeniusNode.hpp. Uses `boost::asio::post(*io_, ...)` for async launch. Call site in `INITIALIZING_TRANSACTIONS` case after `transaction_manager_` is created. `BridgeRelayer::Start()` called only after `InitializeRpcEndpoints()` returns.
result: pass

### 9. chains_config.json — 8 bridge contract addresses
expected: `grep -c "bridge_contract_address" evmrelay/examples/chains_config.json` returns 8. Addresses match `send_test_transactions.sh` values. `ChainRpcEndpointProvider` populates `bridge_contract_address` and `event_topic0` on `WeightedRpcEndpoint`.
result: pass

### 10. Catch-up scan — backfills historical burns
expected: `PerformStartupCatchupScan()` probes RPC for burns after CRDT sync. Missing burns inserted as `UTXO_READY` with `UTXOType::UTXO_BRIDGE`. Already-CONSUMED burns skipped. Scan depth capped at 10,000 blocks. Best-effort across chains.
result: pass

### 11. CWD path fix — chains.json path is binary-relative
expected: `InitializeRpcEndpoints()` uses `boost::dll::program_location().parent_path()` instead of `std::filesystem::current_path()`. No hardcoded 4-chain `ChainIdMap` — all 8 chains sourced from `chains_config.json`.
result: pass

### 12. New unit tests — 32 tests added for Phase 5
expected: `test/src/account/utxo_manager_test.cpp` has RESERVED state + UTXOType tests. `test/src/startup/startup_wiring_test.cpp` has startup wiring + catch-up scan tests. `test/src/account/chain_rpc_endpoint_provider_test.cpp` has bridge field tests. `test/src/CMakeLists.txt` includes `add_subdirectory(startup)`.
result: pass

## Summary

total: 12
passed: 9
issues: 3
pending: 0
skipped: 0

## Gaps

- truth: "PerformStartupCatchupScan() uses UTXO set (not in-memory tx variables) and scan_depth is honored"
  status: failed
  reason: "User reported: No, it's not using UTXO, it's relying on in memory variables on transaction manager. Also scan_depth doesn't seem to be used."
  severity: major
  test: 10
  root_cause: ""
  artifacts: 
    - path: src/account/GeniusNode.cpp
      issue: "PerformStartupCatchupScan uses TransactionManager in-memory state instead of UTXOManager UTXO set"
  missing:
    - "Rewrite catch-up scan to use UTXOManager::GetUTXOs()/PutUTXO() instead of TransactionManager in-memory variables"
    - "Honor DevConfig.bridge_catchup_scan_depth in eth_getLogs fromBlock calculation"
  debug_session: ""

- truth: "InitializeRpcEndpoints() chains.json path is writable on all platforms (Android, macOS, Linux, Windows)"
  status: failed
  reason: "User reported: Not sure that this is correct, because on Android for instance the binary path can't be written into. That's why we have a base write path being informed on the GeniusNode."
  severity: major
  test: 11
  root_cause: ""
  artifacts:
    - path: src/account/GeniusNode.cpp
      issue: "InitializeRpcEndpoints() uses boost::dll::program_location().parent_path() which is read-only on Android"
  missing:
    - "Use DevConfig base_write_path (or GeniusNode-provided writable path) instead of binary directory for chains.json"
  debug_session: ""

- truth: "startup_wiring_test.cpp compiles without errors"
  status: failed
  reason: "User reported: startup_wiring_test.cpp doesn't compile — boost::json::parse and boost::asio::io_context errors (missing includes)"
  severity: blocker
  test: 12
  root_cause: ""
  artifacts:
    - path: test/src/startup/startup_wiring_test.cpp
      issue: "Missing #include <boost/json.hpp> and #include <boost/asio.hpp>"
  missing:
    - "Add missing boost includes to startup_wiring_test.cpp"
    - "Verify test compiles with ctest --test-dir build"
  debug_session: ""
