---
phase: 05
part: 2
topic: InitializeRpcEndpoints() + BridgeRelayer::Start() — Startup Wiring
date: 2026-06-03
status: finalized
---

# Part 2 Discussion: Wire Both into Node Startup

## Current State

- `InitializeRpcEndpoints()` defined at `GeniusNode.cpp:1961` but never called
- `BridgeRelayer::Start()` never called (see Part 1)
- Both need to fire early in node lifecycle
- `BridgeRelayer::Start()` depends on `InitializeRpcEndpoints()` completing first

## State Machine Context

Current states: `CREATING → MIGRATING_DATABASE → INITIALIZING_DATABASE → INITIALIZING_PROCESSING → INITIALIZING_BLOCKCHAIN → INITIALIZING_TRANSACTIONS → READY`

BridgeRelayer is currently created in `INITIALIZING_TRANSACTIONS` (line 431).

## Design Decision

Both fire as a single async function during `CREATING` state:

```
async {
    await InitializeRpcEndpoints();   // populate RPC endpoints for all chains
    await BridgeRelayer::Start();     // register BridgeSourceBurned watches
}
```

- **No state machine dependency** — launches from CREATING, runs independently
- **No new state** — node proceeds through normal transitions
- **Non-blocking** — node hits READY without waiting for RPC endpoint resolution
- **Ordering guaranteed** — Start() only fires after endpoints are ready

## Config Sharing

Both consume `chains_config.json`:
- `InitializeRpcEndpoints()` → `ChainRpcEndpointProvider` → `PublicChainInputValidator::SetRpcEndpoints()`
- `BridgeRelayer::Start()` → iterates chains, registers watch per chain with `bridge_contract_address`

Chains without `bridge_contract_address` are skipped by Start() but still get RPC endpoints via InitializeRpcEndpoints().

## Direct Endpoints (API Keys)

- `ChainRpcProviderConfig.direct_endpoints` already supports this
- Not needed for Phase 5 mock/testing
- Full/archive nodes configure at deployment time via app layer (Keychain/Keystore)
- No API keys in git, no environment variables
