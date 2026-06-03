---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: Bridge Integration
current_phase: Phase 3 — Burn Deduplication Cache (Gap Closure)
status: in-progress
last_updated: "2026-06-03T13:11:20.000Z"
progress:
  total_phases: 9
  completed_phases: 5
  total_plans: 11
  completed_plans: 10
  percent: 56
---

# Project State: SuperGenius Bridge Integration

**Current Phase:** Phase 3 — Burn Deduplication Cache (Gap Closure)

## Phase Status

| Phase | Name | Status |
|-------|------|--------|
| Phase 1 | Wire RPC Endpoints from evmrelay ChainList | complete |
| Phase 2 | Relayer — Burn Detection → MintFunds | complete |
| Phase 3 | Burn Deduplication Cache | in-progress (gap closure) |
| Phase 4 | End-to-End Integration Test | Not Started |
| Phase 5 | Startup Bridge Recovery | Not Started |

## Quick Tasks Completed

| Date | Task | Status |
|------|------|--------|
| 2026-06-03 | Implement full TokenID FromUint256 tests for both little and big endian | complete |
| 2026-06-03 | Add configurable TokenID FromUint256 endianness parameter defaulting to host | complete |
| 2026-06-03 | Fix TokenID::FromUint256 to use host-independent big-endian value serialization | complete |
| 2026-06-03 | Add TokenID::FromUint256 with endian detection and use it in BridgeRelayer | complete |
| 2026-06-02 | Separate InputValidator derived classes into their own headers and sources | complete |

## Phase 3 Progress

| Task | Status |
|------|--------|
| 1 — Canonical message_id | done |
| 2 — Deterministic slot keys | done |
| 3 — Processing reservation state | done |
| 4 — Persist executed bridge state | done |
| Gap: Fix 1 — Slot key collision (P1 #3) | planned |
| Gap: Fix 2 — Fail-closed on missing endpoints (P2 #4) | planned |
| Gap: Fix 3 — UTXO witness for bridge mints (P1 #1) | planned |
| Gap: Fix 4 — Receipt log verification (P1 #2) | planned |

### Gap Closure Plan

Plan 03-01 addresses 4 Codex review findings from PR #298:

- **Fix 1 (D-01/D-02):** Add burn tx hash to GetSlotKey() — `consumed_outpoints[0].tx_id_hash`
- **Fix 2 (D-03):** Return false when rpc_endpoints_ has no entry for chain ID
- **Fix 3 (D-04):** RequiresConsensusUTXOData() returns false for PublicChainInputValidator
- **Fix 4 (D-05/D-06):** Verify receipt logs match configured bridge_contract_address + event_topic0

### Task 2 Implementation

**Approach:** Detect MintV2 subjects via `EmbeddedTransaction` oneof (`transaction_case() == kMintV2`).
Build deterministic slot key from chain_id, token_id, amount, dest_address.

**Slot key format (current):** `mint-v2:{chain_id}:{token_id}:{amount}:{dest_address}`
**Slot key format (after Fix 1):** `mint-v2:{chain_id}:{token_id}:{amount}:{dest_address}:{burn_tx_hash}`

**Resolved:** The proto cleanup (Phase 4) enabled direct oneof detection — no deserialization needed.
See `Consensus.cpp:GetSlotKey()` and issue #297.

## Deferred Items (from Phase 1 review 2026-05-28)

| # | Item | Handler |
|---|------|---------|
| 1 | Hardcoded chain name→ID map in GeniusNode::InitializeRpcEndpoints() | Phase 2 — add chainId to chains_config.json entries |
| 2 | CWD-relative chains.json path (fragile at runtime) | Phase 2 — make path configurable via ChainRpcProviderConfig |
| 3 | VerifyPublicChainSmartContract returns true on missing config | Phase 2 — revisit defaults once bridge consensus quorum is in place |

## Accumulated Context

### Architecture (2026-05-28)

- `BridgeConsensusAdapter` deleted — bridge flow now operates on mint nonce subjects directly
- Burn tx hash flows: `MintFunds` → `MintTransactionV2` → UTXO commitment → `consumed_outpoints[0].tx_id_hash`

### Roadmap Evolution

- Phase 5 added: on node startup after it has got all the crdt data, especially if a full or archive node, it should grab the last mint message transaction by the date and then use RPC to check the contract for any unprocessed bridged transactions that need to be minted
