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

**Current Phase:** Phase 3 — Burn Deduplication Cache (1/4 tasks done, 1 designed)

## Phase Status

| Phase | Name | Status |
|-------|------|--------|
| Phase 1 | Wire RPC Endpoints from evmrelay ChainList | complete |
| Phase 2 | Relayer — Burn Detection → MintFunds | Not Started |
| Phase 3 | Burn Deduplication Cache | in-progress |
| Phase 4 | End-to-End Integration Test | Not Started |
| Phase 5 | Startup Bridge Recovery | Not Started |

## Phase 3 Progress

| Task | Status |
|------|--------|
| 1 — Canonical message_id | done |
| 2 — Deterministic slot keys | **designed, needs review** |
| 3 — Processing reservation state | pending |
| 4 — Persist executed bridge state | pending |

### Task 2 Design (ready for Henrique review)

**Problem:** `GetSlotKey()` uses `account_id:nonce` for all nonce subjects.
Two validators minting for the same burn get different slots → no dedup.

**Approach:** Detect bridge mint nonce subjects in `GetSlotKey()` by checking
for a UTXO commitment with a consumed outpoint referencing an external tx hash
(the burn). Use that hash as the slot key instead of `account_id:nonce`.

**Open question:** Best way to distinguish bridge mint consumed outpoints from
native transfer consumed outpoints without a blockchain lookup on the hot path.
(3 options documented in Phase 3 PLAN)

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
