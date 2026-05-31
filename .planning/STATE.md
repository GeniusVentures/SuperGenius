---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: "Bridge Integration"
current_phase: 3
status: in-progress
last_updated: "2026-05-28T00:00:00Z"
progress:
  total_phases: 5
  completed_phases: 1
  total_plans: 3
  completed_plans: 1
---

# Project State: SuperGenius Bridge Integration

**Current Phase:** Phase 2 — Relayer — Burn Detection → MintFunds

## Phase Status

| Phase | Name | Status |
|-------|------|--------|
| Phase 1 | Wire RPC Endpoints from evmrelay ChainList | complete |
| Phase 2 | Relayer — Burn Detection → MintFunds | in-progress |
| Phase 3 | Burn Deduplication Cache | complete |
| Phase 4 | End-to-End Integration Test | Not Started |
| Phase 5 | Startup Bridge Recovery | Not Started |

## Phase 3 Progress

| Task | Status |
|------|--------|
| 1 — Canonical message_id | done |
| 2 — Deterministic slot keys | done |
| 3 — Processing reservation state | done |
| 4 — Persist executed bridge state | done |

### Task 2 Implementation

**Approach:** Detect MintV2 subjects via `EmbeddedTransaction` oneof (`transaction_case() == kMintV2`).
Build deterministic slot key from chain_id, token_id, amount, dest_address.

**Slot key format:** `mint-v2:{chain_id}:{token_id}:{amount}:{dest_address}`

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
