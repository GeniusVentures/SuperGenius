---
phase: 01
phase_name: "Wire RPC Endpoints from evmrelay ChainList"
project: "SuperGenius"
github_issue: "https://github.com/GeniusVentures/SuperGenius/issues/293"
generated: "2026-05-27"
status: "complete"
---

# Phase 1 Summary: Wire RPC Endpoints from evmrelay ChainList

## What Was Done

Wired ChainList public RPC endpoints into SuperGenius bridge verification at startup.
Created `ChainRpcEndpointProvider` — a platform-agnostic class that loads chains.json
from chainid.network, filters to the four configured EVM chains, assigns consensus
weights, and configures `PublicChainInputValidator`.  Updated the validator to use
weighted consensus (public endpoints 25%, direct API-key endpoints 50%, ≥75% threshold).

## Decisions

### 1. Provider class, not inline method
**What:** Extracted ChainList loading into `ChainRpcEndpointProvider` class.
**Why:** The user requested a proper helper class wrapper encapsulating all RPC
endpoint functionality.  This separates concerns — GeniusNode stays thin, the
provider is independently testable.
**Source:** User feedback on initial implementation.

### 2. Config struct over environment variables
**What:** `ChainRpcProviderConfig` carries file paths and direct endpoint definitions.
**Why:** `std::getenv` doesn't work on mobile (iOS/Android).  The app layer supplies
API keys from secure storage.  This follows the existing `DevConfig` pattern.
**Source:** User feedback on `EVMRELAY_DIRECT_RPC_ENDPOINTS` environment variable.

### 3. Weighted consensus (public 25%, direct 50%, ≥75%)
**What:** Each RPC endpoint carries a weight.  Verification sums successful weights.
**Why:** Direct API-key endpoints are more trustworthy than public ChainList endpoints.
A single direct endpoint alone (50%) can't pass — need at least one public confirmation
too (25%).  Three public endpoints also pass (75%).  This balances trust and availability.
**Source:** User requirement for multi-tier RPC validation.

### 4. One-shot startup wiring
**What:** `InitializeRpcEndpoints()` called once in `INITIALIZING_TRANSACTIONS`, not in `TransactionStateChanged`.
**Why:** The state change callback fires every time TM transitions to READY (including
reconnects/recoveries).  RPC endpoints should be configured exactly once at boot.
**Source:** User feedback on incorrect placement in `TransactionStateChanged`.

## Lessons

### 1. State machine placement matters
**What:** Placing initialization in state-change callbacks causes repeated execution.
**Context:** Initially wired `InitializeRpcEndpoints()` in `TransactionStateChanged`
callback, which fires on every TM READY transition — not just first boot.
**Source:** Code review after initial implementation.

### 2. ChainList name → chain ID mapping is a data gap
**What:** `chains_config.json` has internal names ("ethereum-mainnet") but no `chainId` field.
**Context:** Had to hardcode the 4-chain mapping.  Future: add `chainId` to each chain
entry in `chains_config.json` and parse it in the evmrelay config loader.
**Source:** Implementation decision during ChainList integration.

## Patterns

### Platform-agnostic config injection
**Pattern:** Pass platform-specific data through a plain config struct rather than
reading from environment variables, files, or global state.
**When to use:** Any library code that needs runtime configuration on both desktop
and mobile platforms.  The app layer owns platform integration.
**Source:** `ChainRpcProviderConfig` struct design.

## Files Changed

| File | Lines | Type |
|------|-------|------|
| `ChainRpcEndpointProvider.hpp` | 79 | New |
| `ChainRpcEndpointProvider.cpp` | 142 | New |
| `InputValidators.hpp` | +42 | Modified |
| `InputValidators.cpp` | +125 | Modified |
| `TransactionManager.hpp` | +29 | Modified |
| `GeniusNode.hpp` | +13 | Modified |
| `GeniusNode.cpp` | +25 | Modified |
| `CMakeLists.txt` | +2 | Modified |

## Next Steps

Phase 2: Relayer — Burn Detection → MintFunds (#285).  Henrique owns this phase.
Depends on Phase 1 RPC endpoints being available for `VerifyPublicChainSmartContract()`.
