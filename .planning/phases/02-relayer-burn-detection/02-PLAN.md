---
phase: 02
phase_name: "Relayer — Burn Detection → MintFunds"
project: "SuperGenius"
generated: "2026-05-30"
status: in-progress
---

# Phase 2 Plan: Relayer — Burn Detection → MintFunds

## Goal

Wire `BridgeRpcWatcher` burn event detection to `TransactionManager::MintFunds`
so that EVM burn events automatically trigger mint transactions.

## What Already Exists

- `BridgeRpcWatcher` — fully implemented, polls eth_getLogs, verifies receipts,
  produces `BridgeEventClaim` objects
- `TransactionManager::MintFunds` — fully implemented with in-memory + persistent dedup
- `BridgeEventClaim` — struct with src_chain_id, tx_hash, amount, token_id, recipient
- `ChainRpcEndpointProvider` — loads RPC endpoints from chains.json

## What's Missing

The **glue** — a component that:
1. Configures `BridgeRpcWatcher` for each supported chain
2. Wires `BridgeClaimCallback` → `MintFunds`
3. Maps `BridgeEventClaim` fields to `MintFunds` parameters

## Task 1: Create BridgeRelayer Class

**File:** `src/account/BridgeRelayer.hpp` and `src/account/BridgeRelayer.cpp`

**Responsibility:** Owns one `BridgeRpcWatcher` per chain, bridges claims to MintFunds.

```cpp
class BridgeRelayer {
public:
    struct ChainConfig {
        std::string rpc_url;
        uint64_t    chain_id;
        std::string contract_address;
        std::string event_signature;  // topic0 of BridgeSourceBurned
    };

    BridgeRelayer(std::shared_ptr<TransactionManager> tx_manager,
                  std::shared_ptr<base::Logger> logger);

    void AddChain(const ChainConfig &config);
    void Start();
    void Stop();

private:
    void OnBridgeClaim(const eth::BridgeEventClaim &claim);

    std::shared_ptr<TransactionManager> tx_manager_;
    std::vector<std::unique_ptr<evmwatcher::BridgeRpcWatcher>> watchers_;
};
```

**Field mapping (BridgeEventClaim → MintFunds):**
- `amount` → `static_cast<uint64_t>(claim.amount)` (bridge amounts fit in uint64)
- `transaction_hash` → `claim.tx_hash.toReadableString()`
- `chainid` → `std::to_string(claim.src_chain_id)`
- `tokenid` → `TokenID::FromBytes(claim.token_id_or_nonce)` or GNUS token
- `destination` → `claim.recipient.toReadableString()`

## Task 2: Wire into GeniusNode

**File:** `src/account/GeniusNode.cpp`

Add `BridgeRelayer` member and initialize in `INITIALIZING_TRANSACTIONS` state
(after TransactionManager is ready). Use `ChainRpcEndpointProvider` to get RPC URLs.

## Task 3: Unit Test

**File:** `test/src/account/bridge_relayer_test.cpp`

Test that:
- `OnBridgeClaim` calls `MintFunds` with correct parameters
- Duplicate claims are rejected by the existing dedup logic
- Missing/invalid claim fields are handled gracefully

## Verification

- [ ] Build passes
- [ ] Unit test passes
- [ ] BridgeRelayer starts and logs watcher configuration
