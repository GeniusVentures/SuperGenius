---
phase: 02
phase_name: "Relayer — Burn Detection → MintFunds"
project: "SuperGenius"
generated: "2026-05-30"
updated: "2026-05-30"
status: in-progress
---

# Phase 2 Plan: Relayer — Burn Detection → MintFunds

## Goal

Wire evmrelay burn event detection to `TransactionManager::MintFunds`
so that EVM burn events automatically trigger mint transactions.

## Architecture

```
GeniusNode
  ├── owns EthWatchService (shared_ptr, singleton-style)
  ├── passes it to BridgeRelayer via DI
  └── future watchers also receive EthWatchService

BridgeRelayer
  ├── references shared EthWatchService
  ├── registers BridgeSourceBurned watch on Start()
  ├── OnWatchEvent(notification) → extracts burn details → MintFunds
  └── Stop() → unregisters watch

EthWatchService (evmrelay)
  ├── chain configs (multi-chain)
  ├── peer pool / RLPx connections
  └── registered watches (BridgeSourceBurned, future events)
```

## Task 1: BridgeRelayer with DI EthWatchService ✅

**Files:** `src/account/BridgeRelayer.hpp`, `src/account/BridgeRelayer.cpp`

BridgeRelayer takes `shared_ptr<EthWatchService>` via constructor.
Registers a `BridgeSourceBurned` watch using `eth::cli::event_registry()`.
Processes decoded ABI values to extract burn details.

**Field mapping (from decoded ABI values):**
- `values[0]` (address) → sender → destination
- `values[1]` (uint256) → id → token_id (ERC-1155)
- `values[2]` (uint256) → amount
- `values[3]` (uint256) → srcChainID → chain_id
- `notification.event.tx_hash` → transaction hash

## Task 2: Wire into GeniusNode ✅

**File:** `src/account/GeniusNode.cpp`

- `eth_watch_service_` created in `INITIALIZING_TRANSACTIONS`
- Passed to `BridgeRelayer` via constructor
- BridgeRelayer started when TransactionManager is READY

## Task 3: Unit Test

**File:** `test/src/account/bridge_relayer_test.cpp`

Test that:
- `OnWatchEvent` calls `MintFunds` with correct parameters
- Duplicate claims are rejected by existing dedup logic
- Missing/invalid event fields are handled gracefully

## Verification

- [x] Build passes
- [ ] Unit test passes
- [ ] BridgeRelayer starts and logs watcher registration
