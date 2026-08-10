---
phase: 01
phase_name: "Wire RPC Endpoints from evmrelay ChainList"
project: "SuperGenius"
github_issue: "https://github.com/GeniusVentures/SuperGenius/issues/293"
generated: "2026-05-27"
status: "complete"
---

# Phase 1 Plan: Wire RPC Endpoints from evmrelay ChainList

## Goal

Load public RPC endpoint URLs from the evmrelay ChainList provider (chainid.network
chains.json), filter to the four configured EVM chains, and wire them into
`PublicChainInputValidator` at GeniusNode startup before any bridge transactions
are processed.

## Requirements Addressed

| ID | Description |
|----|-------------|
| RPC-WIRE-01 | Chain RPC endpoints loaded from ChainList data, not hardcoded |
| RPC-WIRE-02 | Endpoints registered before TransactionManager reaches READY |
| RPC-WIRE-03 | Weighted consensus: public=25%, direct=50%, threshold >=75% |
| RPC-WIRE-04 | Platform-agnostic config (no `std::getenv`), mobile-compatible |
| RPC-WIRE-05 | Direct API-key endpoints supplied by app layer (secure storage) |

## Architecture

```
App Layer (desktop JSON / mobile secure storage)
         │
         ▼
ChainRpcEndpointProvider  ←── ChainRpcProviderConfig
  │  chain_id_map (name → numeric ID)
  │  reads chains.json from ChainList
  │  filters to configured chain IDs
  │  groups by chain_id with weights
  │
  ▼
PublicChainInputValidator::SetRpcEndpoints(chain_id, WeightedRpcEndpoint[])
  │
  ▼
VerifyPublicChainSmartContract() — weighted >=75% consensus
```

## Files Created

| File | Purpose |
|------|---------|
| `src/account/ChainRpcEndpointProvider.hpp` | Config struct + provider class declaration |
| `src/account/ChainRpcEndpointProvider.cpp` | ChainList loading, filtering, validator wiring |

## Files Modified

| File | Change |
|------|--------|
| `src/account/InputValidators.hpp` | Added `WeightedRpcEndpoint` struct; updated `SetRpcEndpoints` signature and `rpc_endpoints_` type |
| `src/account/InputValidators.cpp` | Rewrote `VerifyPublicChainSmartContract` for weighted consensus (≥75%) |
| `src/account/TransactionManager.hpp` | Added `GetPublicChainInputValidator()` accessor |
| `src/account/GeniusNode.hpp` | Added `InitializeRpcEndpoints()` declaration |
| `src/account/GeniusNode.cpp` | One-shot RPC endpoint wiring in `INITIALIZING_TRANSACTIONS` state |
| `src/account/CMakeLists.txt` | Registered `ChainRpcEndpointProvider.cpp` in `genius_node` target |

## Task Breakdown

1. **Add validator accessor** — `TransactionManager::GetPublicChainInputValidator()`
2. **Add weighted endpoint storage** — `WeightedRpcEndpoint` struct, updated `SetRpcEndpoints`
3. **Implement weighted consensus** — `VerifyPublicChainSmartContract` sums weights, requires ≥75%
4. **Create provider class** — `ChainRpcEndpointProvider` encapsulates ChainList loading
5. **Wire at startup** — `GeniusNode::InitializeRpcEndpoints()` called once in state machine
6. **Build verification** — Full project compiles (405 targets)

## Success Criteria

- [x] Each configured chain receives public RPC endpoints from ChainList
- [x] Direct API-key endpoints accepted via config struct (app layer responsibility)
- [x] Weighted consensus validation (public 25%, direct 50%, ≥75% threshold)
- [x] No `std::getenv` — platform-agnostic config struct
- [x] One-shot initialization (not on every state change)
- [x] Build passes
