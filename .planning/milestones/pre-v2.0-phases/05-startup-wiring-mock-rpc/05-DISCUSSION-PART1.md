---
phase: 05
part: 1
topic: BridgeRelayer::Start() — Startup Wiring
date: 2026-06-03
status: finalized
---

# Part 1 Discussion: Wire BridgeRelayer::Start() into Node Startup

## Current State

At `GeniusNode.cpp:427-432` (during `NodeState::INITIALIZING_CONSENSUS`):

```cpp
eth_watch_service_ = std::make_shared<eth::EthWatchService>();
bridge_relayer_ = BridgeRelayer::Create(tx_manager, eth_watch_service_);
// ← Start() is never called — no BridgeSourceBurned watch registered
```

Result: burn→MintFunds path is dead in normal nodes. PR #298 Codex review P1 finding.

## Design Decisions

### 1. Start() should accept a set of chains

Current signature: `Start(const std::string &chain_name, const std::string &contract_address)`

Must change to accept multiple chain/contract pairs. For each chain, register a `BridgeSourceBurned` watch on the shared `EthWatchService`.

### 2. Chain data source: chains_config.json

Bridge contract addresses added as optional `bridge_contract_address` field:

```json
{
  "name": "ethereum-mainnet",
  "chainId": 1,
  "bridge_contract_address": "0x614577036F0a024DBC1C88BA616b394DD65d105a",
  ...
}
```

### 3. Contract address mapping

| chains_config.json name | Contract |
|---|---|
| ethereum-mainnet | `0x614577036F0a024DBC1C88BA616b394DD65d105a` |
| ethereum-sepolia | `0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70` |
| bnb-smart-chain | `0x614577036F0a024DBC1C88BA616b394DD65d105a` |
| bnb-smart-chain-testnet | `0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB` |
| polygon-mainnet | `0x127E47abA094a9a87D084a3a93732909Ff031419` |
| polygon-amoy | `0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB` |
| base-mainnet | `0x614577036F0a024DBC1C88BA616b394DD65d105a` |
| base-sepolia | `0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB` |
| ethereum-holesky | *skip — no bridge deployed* |
| ethereum-hoodi | *skip — no bridge deployed* |
| gnosis-chain | *skip — no bridge deployed* |

### 4. Skip chains without bridge_contract_address

If the field is absent or empty, that chain is skipped during Start() — no watch registered.

## Open Questions (for Part 1b or Part 2)

- Call site: where in GeniusNode startup? Right after Create() in INITIALIZING_CONSENSUS, or deferred?
- How does GeniusNode read chains_config.json? Share parsing with InitializeRpcEndpoints()?
- BridgeRelayer currently stores single `watch_id_` — needs to track per-chain watch IDs
