---
title: Bridge Startup Wiring + Mock RPC Endpoints
date: 2026-06-03
priority: P1
source: PR #298 Codex review (June 2) — 3 deferred findings
---

## Tasks

### 1. Wire BridgeRelayer::Start() into node startup
- Currently: BridgeRelayer is DI-constructed but Start(chain, contract) is never called
- Need: Call site in GeniusNode startup path that invokes Start() with the configured chain and bridge contract
- Effect: BridgeSourceBurned watch is registered → burn→MintFunds path goes live

### 2. Wire InitializeRpcEndpoints() into node startup
- Currently: method defined but never called from startup
- Need: Call site that loads ChainList endpoints and calls SetRpcEndpoints() for each chain
- Effect: bridge_contract_address and event_topic0 are populated on endpoints → log verification gate works

### 3. Mock RPC Transport
- In-process mock for Tier 1 RPC verification in tests
- Per-node configuration (local config file, test/dev only)
- Data variance: configurable receipt logs, tx statuses, addresses per endpoint
- Behavioral variance: success, error, timeout per endpoint
- Stateful variance: remembers prior calls for multi-step scenarios
- Multi-chain support including testnet chains
- Majority verification: ≥2 of 3 endpoints must agree

## Design Decisions
- Tier 1: Simple majority (≥2 of 3), not weighted consensus
- Disagreement between endpoints is not flagged (lagging endpoint is expected)
- Tier 2 (network voting weight classes) is a separate follow-up phase
