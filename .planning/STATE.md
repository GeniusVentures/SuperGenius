---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: GeniusNode Construction Refactor
current_phase: 05
status: executing
last_updated: "2026-07-16T21:37:59.637Z"
last_activity: 2026-07-16 -- Phase 8 planning complete
progress:
  total_phases: 9
  completed_phases: 8
  total_plans: 12
  completed_plans: 13
  percent: 89
---

# State: SuperGenius — GeniusNode Construction Refactor

**Last updated:** 2026-07-02
**Milestone:** v1.0 — GeniusNode Construction Refactor
**Current Phase:** 05
**Current Phase:** 04.1

## Project Reference

See: .planning/PROJECT.md (updated 2026-07-02)

**Core value:** Constructing a `GeniusNode` must be a single, self-documenting call driven by config files.
**Current focus:** Phase 05 — on node startup after it has got all the crdt data especiall

## Current Position

Phase: 04.2 (p2p-rlpx-burn-event-gossip) — EXECUTING
Plan: Not started
Status: Ready to execute
Last activity: 2026-07-16 -- Phase 8 planning complete

## Roadmap Snapshot

| Phase | Name | Status | Requirements |
|-------|------|--------|--------------|
| 1 | Config-Driven Settings Foundation | ○ not started | CFG-01, CFG-02, CFG-04 |
| 2 | Variant Factory + Constructor Reorder | ○ blocked by 1 | INTF-01..04, CFG-03 |
| 3 | Call-Site Migration + Verification | ○ blocked by 2 | MIG-01..04 |

## Key Decisions

- `node_type` → `sgns_config.json`; `autodht`+`base_port` → `network_config.json` (deployment-time, not per-call)
- `is_full_node_` stays a derived bool at the GeniusNode boundary; NodeType enum NOT propagated downstream this milestone
- Single `New(dev_config, AccountSource)` with `std::variant`; no compat shim; all 18 call sites migrated
- Account creation moves INTO the constructor (after `LoadSgnsConfig`) to resolve init-order chicken-and-egg
- Phase 04.1 Plan 03: D-19/D-20 swapped scan_depth (10000) -> start_block (0=genesis) in BridgeCatchupWatcher::Config; D-23 added exponential-backoff retry to RpcHttpTransport::call(); D-03 corrected Sepolia fallback URL to sepolia.drpc.org

## Notes

- Research produced inline (subagent runtime returned schema error `no such column: replacement_seq` on all spawns); 5 docs in `.planning/research/`.
- Brownfield codebase map exists at `.planning/codebase/` (7 docs).
- Strictly sequential phases (1 → 2 → 3); later phases won't compile until earlier ones land.

## Operator Next Steps

- Start the next milestone with /gsd-new-milestone

### Architecture

- `BridgeConsensusAdapter` deleted — bridge flow now operates on mint nonce subjects directly
- Burn tx hash flows: `MintFunds` → `MintTransactionV2` → UTXO commitment → `consumed_outpoints[0].tx_id_hash`
- Phase 3 refactored InputValidators into separate files: PublicChainInputValidator, GeniusInputValidator
- PR #298 merged into evmrelay_integration — Phase 1-4 complete

### Roadmap Evolution

- Phase 5: Wire BridgeRelayer::Start() and InitializeRpcEndpoints() into node startup, add mock RPC transport for testing
- Phase 6 (follow-up): Tier 2 network voting weight classes with reputation scoring
- See `.planning/notes/rpc-verification-tiers.md` for architecture decisions
- Phase 05.1 inserted after Phase 5: Refactor: Move RPC endpoint initialization from GeniusNode to ChainRpcEndpointProvider (URGENT)
- Phase 7 added: Deferred Validation and Pending Proposal Lifecycle; renumbered from duplicate Track B Phase 4 to global Phase 7
- Phase 05.2 inserted after Phase 05.1: Smart contract updated to 32-byte X-only compressed SG public key (bytes32). Event renamed BridgeOutInitiated. C++ side: decode X-only → decompress to full X+Y → match GetAddress(). Versioned catch-up scan. (URGENT)
- Phase 04.1 inserted after Phase 4: Anvil local bridge E2E: use TokenContracts .env for Anvil instance, private key bridge tx, scan. Fallback: direct Sepolia test. (URGENT)
- Phase 8 added: Burn/mint datapath robustness — multi-node mint-race e2e test (exactly-once mint across cluster, extends bridge_e2e Anvil fixture), fault injection (node kill mid-mint, disagreeing RPC endpoints via WeightedRpcEndpoint quorum), libFuzzer harnesses for ParseBurnEventValues and bridge deserializers

## Phase 5 Progress

| Part | Topic | Status |
|------|-------|--------|
| 1 | BridgeRelayer::Start() — multi-chain from chains_config.json | done |
| 2 | InitializeRpcEndpoints() + async startup wiring | done |
| 3 | Mock RPC Transport — design & interface | done |
| 4 | Startup catch-up scan, mock enablement, failure handling | done |
| — | CONTEXT.md | written (21 decisions captured) |
| — | 05-01-PLAN through 05-05-PLAN | implemented |
| — | 05-06 Unit Test Generation (28 tests across 3 modules) | implemented |

**Branch:** `bridge_phase5` (pushed to origin)

**Discussion artifacts:**

- `.planning/phases/05-startup-wiring-mock-rpc/05-DISCUSSION-PART1.md`
- `.planning/phases/05-startup-wiring-mock-rpc/05-DISCUSSION-PART2.md`
- `.planning/phases/05-startup-wiring-mock-rpc/05-DISCUSSION-PART3.md`
- `.planning/phases/05-startup-wiring-mock-rpc/05-DISCUSSION-PART4.md`

**Context:** `.planning/phases/05-startup-wiring-mock-rpc/05-CONTEXT.md` (21 decisions)

**Key decisions:**

- `bridge_contract_address` added as optional field to `chains_config.json`
- Both fire as async during CREATING — no state machine coupling
- `Start()` awaits `InitializeRpcEndpoints()` completion
- Mock implements `RpcHttpTransport` interface — drop-in replacement
- Per-node JSON config `<binary_dir>/mock_rpc_config.json`
- Stateful ordered responses keyed by tx_hash
- 6 failure modes: success, timeout, connection_refused, bad_json, wrong_status, wrong_logs
- Remove `!is_full_node_ && address != address_` guards — all nodes store all UTXOs
- Add `UTXO_RESERVED` state to Burn UTXO lifecycle (READY → RESERVED → CONFIRMED)
- Startup catch-up: probe RPC for historical burns, backfill missing UTXOs
- Test executables default to mock; real RPC opt-in via runtime switch
- BridgeRelayer multi-chain Start() uses best-effort (skip failed chains)
