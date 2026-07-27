# Roadmap: SuperGenius

## Milestones

- ✅ **v1.0 GeniusNode Construction Refactor** — Phases 1-3 (shipped 2026-07-03)

## Phases

<details>
<summary>✅ v1.0 GeniusNode Construction Refactor (Phases 1-3) — SHIPPED 2026-07-03</summary>

- [x] Phase 1: Config-Driven Settings Foundation (1/1 plans)
- [x] Phase 2: Variant Factory + Constructor Reorder (1/1 plans)
- [x] Phase 3: Call-Site Migration + Verification (3/3 plans)

**Core value delivered:** Constructing a `GeniusNode` is a single, self-documenting call — `New(dev_config, AccountSource)` — driven by config files (`network_config.json`, `sgns_config.json`); the three overloaded factories are gone.

Full phase details: `.planning/milestones/v1.0-ROADMAP.md`
Requirements: `.planning/milestones/v1.0-REQUIREMENTS.md`
Milestone summary: `.planning/MILESTONES.md`

</details>

## Backlog

(Items deferred to future milestones — see `.planning/milestones/v1.0-REQUIREMENTS.md` v2 section: PROP-01 NodeType propagation, PROP-02 Archive/Full behavior split, HARD-01 pubsub_port numeric, HARD-02 config schema versioning.)

---
*Roadmap last updated: 2026-07-03 (v1.0 archived)*

## Track A: EVM Bridge Integration

**Branch:** `evmrelay_integration`

### Completed

| Item | Status | Notes |
|------|--------|-------|
| evmrelay v1 (ChainList, RPC infra, tests) | Done | Submodule at `59d1ed2`, 55+ tests |
| Consensus opaque-payload refactor | Done | `subject_type_hash` dispatch |
| Bridge consensus adapter (now deleted) | Removed | No separate bridge consensus round |
| `MintTransactionV2` RPC cleanup | Done | No RPC members — clean value class |
| `PublicChainInputValidator` RPC verification | Done | `SetRpcEndpoints(chain_id, urls)`, `VerifyPublicChainSmartContract` calls 3+ RPC endpoints |
| `BridgeRpcWatcher` in src/watcher | Done | RPC-based event detection via evmrelay |
| SuperGenius build | Passes | 405 targets, consensus + watcher tests pass |

### Phase 1: Wire RPC Endpoints from evmrelay ChainList

**Goal:** Load chain RPC endpoints from evmrelay's ChainList provider (driven by `chains_config.json`) and configure `PublicChainInputValidator` at startup.

**Status:** Implementation complete — in-review ([#293](https://github.com/GeniusVentures/SuperGenius/issues/293))
**Plan:** [01-PLAN.md](phases/01-rpc-endpoint-wiring/01-PLAN.md)
**Summary:** [01-SUMMARY.md](phases/01-rpc-endpoint-wiring/01-SUMMARY.md)

**Tasks:**
- [x] Load `chains_config.json` to determine supported chains
- [x] For each supported chain, ingest RPC URLs via `eth::rpc::load_chainlist_from_json_text()`
- [x] Filter/deduplicate by chain ID
- [x] Call `PublicChainInputValidator::SetRpcEndpoints(chain_id, urls)` for each chain
- [x] Wire this into the application startup path (GeniusNode or equivalent)

**Success criteria:**
- Each configured chain has at least 3 RPC endpoints available
- Endpoints are loaded from data, not hardcoded
- Build passes

---

### Phase 2: Relayer — Burn Detection → MintFunds

**Goal:** Wire evmrelay burn event detection to `TransactionManager::MintFunds()` via `BridgeRelayer`.

**Architecture:** `BridgeRelayer` takes a shared `EthWatchService` (DI), registers a `BridgeSourceBurned` watch, and calls `MintFunds` when burns are detected.

**Tasks:**
- [x] Create `BridgeRelayer` with DI `EthWatchService`
- [x] Register `BridgeSourceBurned` watch via `eth::cli::event_registry()`
- [x] Extract burn details from decoded ABI values (sender, token_id, amount, srcChainID, tx_hash)
- [x] Call `MintFunds(amount, tx_hash, chain_id, token_id, destination)`
- [x] Unit test for BridgeRelayer
- [ ] Validators independently verify the burn via `PublicChainInputValidator::VerifyPublicChainSmartContract`

**Success criteria:**
- Burn event on EVM chain triggers `MintFunds` via evmrelay signal
- `MintTransactionV2` created with correct chain_id, amount, token_id, burn_tx_hash
- Transaction enters nonce consensus

---

### Phase 3: Burn Deduplication Cache

**Goal:** Prevent double-minting by tracking which burn transaction hashes have already been processed.

**Status:** Complete (2026-05-31, verified 2026-06-09)
**Plans:** 1 plan

Plans:
- [x] 03-01-PLAN.md — Gap closure: slot key collision, fail-closed endpoints, UTXO witness fix, receipt log verification

**Tasks:**
- [x] Define canonical message_id for EVM bridge source events
- [x] Map bridge mints to deterministic consensus slot keys
- [x] Add processing reservation state
- [x] Persist executed bridge message state
- [x] Fix 1: Add burn tx hash to slot key (MintTransactionV2::GetSlotID() line 229-233)
- [x] Fix 2: Fail-closed on missing RPC endpoints (PublicChainInputValidator.cpp:166)
- [x] Fix 3: Disable UTXO witness requirement for bridge mints (RequiresConsensusUTXOData → false)
- [x] Fix 4: Add receipt log verification for bridge contract + topic0 (PublicChainInputValidator.cpp:229-252)

**Success criteria:** All met. Fixes verified present in bridge_phase5 refactored code.

---

### Phase 4: End-to-End Integration Test

**Goal:** Demonstrate the full pipeline: EVM burn → detection → MintTransactionV2 → UTXO consensus → RPC verification → minted tokens.

**Status:** Complete (impl verified 2026-06-09)
**Plans:** 3/3 plans complete

Plans:
- [x] 04-01-PLAN.md — Test infrastructure + positive E2E test (fixture, burn-to-mint pipeline)
- [x] 04-02-PLAN.md — Negative tests (replay rejection, missing endpoints, invalid logs)
- [x] 04-03-PLAN.md — Slot key collision resistance verification

**Tasks:**
- [x] Create test/src/bridge_e2e/ directory with CMakeLists.txt and BridgeE2ETest fixture
- [x] Wire into test/src/CMakeLists.txt via add_subdirectory
- [x] Positive E2E: burn on Sepolia via cast send -> detection -> MintTransactionV2 -> UTXO consensus -> minted tokens
- [x] Negative: replay rejection (same burn tx hash twice -> deduplicated)
- [x] Negative: missing RPC endpoints -> fail-closed (Phase 3 D-03)
- [x] Negative: invalid receipt logs -> rejected (Phase 3 D-05/D-06)
- [x] Slot key: two distinct burns with identical chain/token/amount/dest -> different slot keys

**Success criteria:** All met. E2E test at `test/src/bridge_e2e/bridge_e2e_test.cpp`.

---

### Phase 04.1: Anvil Local Bridge E2E Test (INSERTED)

**Goal:** Self-contained local-Anvil E2E test — forks Sepolia, funds Anvil account #0 via `anvil_impersonateAccount`, sends bridge burn with `cast send`, verifies MintTokens pipeline, tests replay rejection. Also exercises Phase 5's startup catch-up scan (nodes scan RPC for historical burns and auto-mint).

**Depends on:** Phase 4, Phase 5
**Status:** In progress
**Plans:** 1/2 plans complete

Plans:
- [x] 04.1-01-PLAN.md — AnvilProcess helper + BridgeAnvilE2ETest (positive burn-to-mint + replay rejection) + Sepolia-direct fallback
- [ ] 04.1-02-PLAN.md — Startup catch-up scan E2E: nodes start, scan RPC for historical burns via BridgeRelayer watch, auto-mint, verify

---

### Phase 04.2: P2P RLPx Burn Event Gossip (INSERTED)

**Goal:** Move RLPx burn event gossip from deferred to active. Nodes receive real bridge events via devp2p RLPx protocol, watch them stream for 10-20 seconds (1-2s intervals), verify each mints on SuperGenius. Requires real Sepolia PRIVATE_KEY.

**Depends on:** Phase 04.1, Phase 5
**Status:** Planned
**Plans:** 1/1 plans complete

Plans:
- [x] 04.2-01-PLAN.md — Live-Sepolia RLPx E2E: 3 nodes each run EthWatchService in production RLPx mode (kDiscoverFirst), BridgeRelayer auto-mints streamed burns (10 burns at 1-2s cadence), all-3-nodes mint verification

---

### Phase 5: Startup Wiring + Mock RPC Transport

**Goal:** Wire `BridgeRelayer::Start()` and `InitializeRpcEndpoints()` into the node startup path so the burn→MintFunds pipeline actually runs in normal nodes. Add in-process mock RPC transport for Tier 1 verification testing. On startup (especially full/archive nodes), after syncing CRDT data, check for unprocessed bridged transactions that need to be minted.

**Source:** PR #298 Codex review (June 2, 2026) — 3 deferred P1 findings

**Depends on:** Phase 4
**Status:** Complete — PR #309 (bridge_phase5 → develop) in review
**Plans:** 6 plans (5 implementation + 1 test generation)

**Requirements:** REQ-WIRE-01, REQ-WIRE-02, REQ-WIRE-03, REQ-MOCK-01, REQ-MOCK-02, REQ-MOCK-03, REQ-MOCK-04, REQ-UTXO-01, REQ-UTXO-02, REQ-UTXO-03, REQ-CATCH-01, REQ-CATCH-02

**Wave 1** (parallel — independent subsystems):

Plans:
- [x] 05-01-PLAN.md — BridgeRelayer multi-chain Start(): ChainContractPair struct, per-chain watch_id tracking, best-effort registration (D-01, D-21); REQ-WIRE-01, REQ-CATCH-02
- [x] 05-02-PLAN.md — Mock RPC Transport: MockRpcTransport implements JsonRpcTransport, 6 failure modes (D-13), stateful sequences (D-10), config parser (D-08/D-09), behavioral tests; REQ-MOCK-01, REQ-MOCK-02, REQ-MOCK-03
- [x] 05-03-PLAN.md — Transport Factory DI: TransportFactory + SetTransportFactory() in PublicChainInputValidator, replaces hard RpcHttpTransport construction, SGNS_E2E_REAL_RPC=1 env var (D-15); REQ-MOCK-01, REQ-MOCK-03, REQ-MOCK-04
- [x] 05-04-PLAN.md — UTXO Changes: remove 8 foreign-address guards (D-17), add UTXO_RESERVED state (D-18), add UTXOType::UTXO_BRIDGE marker (D-19); REQ-UTXO-01, REQ-UTXO-02, REQ-UTXO-03

**Wave 2** (blocked on Wave 1 completion — depends on 05-01, 05-03, 05-04):

Plans:
- [x] 05-05-PLAN.md — GeniusNode Startup Wiring: InitializeAndStartBridge() async init from INITIALIZING_TRANSACTIONS (D-04), rewritten InitializeRpcEndpoints() from chains_config.json (D-02/D-05), startup catch-up scan (D-20), CWD path fix; REQ-WIRE-02, REQ-WIRE-03, REQ-CATCH-01
- [x] 05-06-SUMMARY.md — Unit Test Generation: 28 GTest tests across 3 modules (UTXOManager, GeniusNode startup, ChainRpcEndpointProvider)

**Architecture note:** See `.planning/notes/rpc-verification-tiers.md`

---


### Phase 05.2: Bridge V2 — X-only compressed encoding: smart contract updated with `bridgeOut(uint256,uint256,uint256,bytes32)` accepting 32-byte X-only compressed SG public key (not an Ethereum address). Event renamed from `BridgeSourceBurned` to `BridgeOutInitiated`. C++ side must decode 32-byte X-only key → decompress to full X+Y → match `GetAddress()`. Versioned catch-up scan handles old topic0 for v1 burns.

Contract:
```solidity
function bridgeOut(uint256 amount, uint256 id, uint256 destChainID, bytes32 sgnsDestination) external {
    address sender = _msgSender();
    require(GNUSNFTFactoryStorage.layout().NFTs[id].nftCreated, "Token not created.");
    require(balanceOf(sender, id) >= amount, "Insufficient tokens.");
    require(destChainID != GNUSControlStorage.layout().chainID, "Cannot bridge to same chain");
    _burn(sender, id, amount);
    emit BridgeOutInitiated(sender, id, amount, GNUSControlStorage.layout().chainID, destChainID, sgnsDestination);
}
``` (INSERTED)

	**Goal:** Decode 32-byte X-only compressed SG public key from BridgeOutInitiated events → decompress to full X+Y key → construct destination matching GetAddress(). Register new event in EventRegistry (kBytes32), retain v1 BridgeSourceBurned (kBytes). BridgeRelayer registers dual watches and dispatches v1/v2 events. Catch-up scan queries both v1 and v2 topic0 hashes.
	**Requirements**: REQ-V2-01, REQ-V2-02, REQ-V2-03, REQ-V2-04, REQ-V2-05, REQ-V2-06, REQ-V2-07, REQ-V2-08, REQ-V2-09
	**Depends on:** Phase 05.1 (Observer pattern, ChainContractPair, IBridgeInitObserver)
	**Plans:** 4/4 plans complete
	
	**Wave 1** (evmrelay foundation — independent):
	Plans:
	- [x] 05.2-01-PLAN.md — EventRegistry v2 registration (D-04, D-05) + DecompressXOnlyPubkey free function (D-07, D-08, D-09, D-10) + 5 secp256k1 decompression tests (REQ-V2-01, REQ-V2-02, REQ-V2-05, REQ-V2-06, REQ-V2-09)
	
	**Wave 2** (BridgeRelayer + GeniusNode — parallel, depends on Wave 1):
	Plans:
	- [x] 05.2-02-PLAN.md — BridgeRelayer::Start() dual-watch v1+v2 (D-14, D-15) + OnWatchEvent() variant dispatch ByteBuffer/Hash256 with v2 decompression (D-06) (REQ-V2-03, REQ-V2-04)
	- [x] 05.2-03-PLAN.md — GeniusNode::PerformStartupCatchupScan() dual topic0 query v1+v2 (D-11, D-12) + v2 X-only decompression before MintFunds (D-13) (REQ-V2-07, REQ-V2-08)
	
	**Wave 3** (test coverage — depends on Waves 1+2):
	Plans:
	- [x] 05.2-04-PLAN.md — EventRegistry v2 tests + BridgeRelayer v2 dispatch/dual-watch tests + Startup catch-up scan v2 topic0 tests (REQ-V2-03, REQ-V2-04, REQ-V2-07, REQ-V2-09)

### Phase 05.1: Refactor: Move RPC endpoint initialization from GeniusNode to ChainRpcEndpointProvider (INSERTED)

**Goal:** Move `GeniusNode::InitializeRpcEndpoints()` (~175 lines of path resolution, JSON parsing, provider construction, validator registration) into `ChainRpcEndpointProvider::Initialize()`. GeniusNode becomes a thin orchestrator: resolve the config path, construct the provider, register observers (BridgeRelayer + self for catch-up scan), and post `Initialize()` to the io_context. Chain IDs come from a new `chain_id` field in `bridge_chains_config.json`, eliminating the hardcoded `kChainNameToId` map.
**Requirements**: None — internal refactor, no formal requirement IDs mapped.
**Depends on:** Phase 5
**Plans:** 3 plans

Plans:
- [ ] 05.1-01-PLAN.md — Define IBridgeInitObserver + re-signature ChainRpcEndpointProvider::Initialize(path, validator, logger); move JSON read, chain_id parse, IInputValidator::Register, and observer notification into the provider (D-01, D-02, D-03, D-04)
- [ ] 05.1-02-PLAN.md — Make BridgeRelayer implement IBridgeInitObserver (OnRpcEndpointsReady delegates to Start); add numeric chain_id to bridge_chains_config.json (D-03, D-04)
- [ ] 05.1-03-PLAN.md — Rewrite GeniusNode as thin orchestrator (resolve path, construct provider, AddObserver relayer+self, post Initialize); remove InitializeRpcEndpoints body, kChainNameToId, bridge_chains_; catch-up scan reads catchup_chains_ via observer callback (D-01, D-02, D-04)

### Phase 6: Network Voting Weight Classes (Tier 2)

**Goal:** Implement slot-based RPC-hash voting for bridge mints (CONTEXT.md D-01..D-10). ConsensusVote carries 3 slot hashes (DIRECT_API + 2 PUBLIC). Cumulative quorum: slot 0 contributes voter reputation x 0.50; slots 1-2 each contribute reputation x 0.25 only for hash groups with >=2 distinct validators. Certificate produced iff the cumulative qualified_sum > 75% of total voter reputation. Reputation = existing ValidatorEntry.weight; Role::FULL promotion via ApplyVoteEffects retained. Tier 1 per-node RPC verification (Phase 5) unchanged.

**Depends on:** Phase 5
**Requirements:** REQ-SLOT-01, REQ-SLOT-02, REQ-SLOT-03, REQ-SLOT-04, REQ-SLOT-05, REQ-SLOT-06, REQ-REPUT-01, REQ-DETERM-01
**Plans:** 3/4 plans executed

**Wave 1** (proto foundation + node self-classification):

Plans:
- [x] 06-01-PLAN.md — Extend ConsensusVote proto with slot_0_hash/slot_1_hash/slot_2_hash (D-01, D-04, D-09) + PublicChainInputValidator hashing accessors + GeniusNode::PopulateVoteSlotHashes wiring (D-10 Tier 1 unchanged) (REQ-SLOT-01)

**Wave 2** (slot tally + dual tally-site routing — depends on Wave 1; touches shared consensus hot paths):

Plans:
- [x] 06-02-PLAN.md — ValidatorRegistry::EvaluateSlotQuorum (D-02 slot 0 50%, D-03 slots 1-2 dedup 25%, D-06 cumulative >75%) + ConsensusManager::EvaluateQuorum dispatcher routing BOTH TallyVotes + HandleVote through one helper (D-05 abstain, D-07 rep=weight, REQ-DETERM-01) (REQ-SLOT-02, REQ-SLOT-03, REQ-SLOT-04, REQ-SLOT-05, REQ-SLOT-06)

**Wave 3** (Role::FULL promotion — depends on Waves 1+2; serialized due to ValidatorRegistry file overlap):

Plans:
- [x] 06-03-PLAN.md — REGULAR->FULL promotion in ApplyVoteEffects via full_promotion_weight_ + penalty gate (D-07 reuse weight, D-08 independence from tally) (REQ-REPUT-01)

**Wave 4** (test coverage + full regression gate — depends on Waves 1+2+3):

Plans:
- [ ] 06-04-PLAN.md — consensus_slot_quorum_test.cpp (golden D-06 example, dedup, both-sites-agree, determinism) + validator_registry_promotion_test.cpp + blocking full ctest -j8 checkpoint per CLAUDE.md shared-library mandate (all REQs)

---

## Track B: Consensus Voting Decentralization

### Overview

A three-phase protocol change to make SuperGenius consensus truly decentralized. Phase 1 embeds full serialized transaction bytes in `NonceSubject` messages so any validator can validate and vote from the proposal alone — no CRDT dependency. Phase 2 hardens security for standalone validators by closing double-spend and nonce-replay detection gaps using the certificate chain. Phase 3 hardens network resilience with message size enforcement, clock-skew tolerance, memory cleanup, and operational metrics.

### Phases

- [x] **Phase 1: Core Embedded-Transaction Validation Path** — Validators validate and vote from embedded tx bytes, no CRDT needed (completed 2026-05-27)
- [ ] **Phase 2: Conflict and Replay Detection Hardening** — Standalone validators detect double-spends and nonce replays via certificate chain
- [ ] **Phase 3: Network Hardening and Operational Readiness** — Robust at scale: size enforcement, clock tolerance, cleanup, metrics

### Phase 1: Core Embedded-Transaction Validation Path

**Goal**: Any validator receiving a `NonceSubject` proposal with embedded transaction data can deserialize the transaction, run all existing validation checks against it, and cast an Approve or Reject vote — without needing the transaction in their local CRDT store.
**Mode**: mvp
**Depends on**: Nothing (first phase)
**Requirements**: PROTO-01, SER-01, DESER-01, BIND-01, SANTZ-01, TRACK-01, VALID-ALL
**Success Criteria** (what must be TRUE):

  1. **Embedding works:** A proposal originator serializes and embeds the full transaction bytes into `NonceSubject` (fields 5 and 6); other validators parse these fields from received protobuf messages without errors.
  2. **Non-CRDT validation works:** A validator that lacks the transaction in its local CRDT store successfully deserializes the embedded data and progresses beyond `Check::Pending` — reaching `Check::Approve` for valid proposals and `Check::Reject` for invalid ones.
  3. **Integrity and binding verified:** The validator rejects proposals where the deserialized transaction's hash does not match `NonceSubject.tx_hash`, or where the reconstructed UTXO commitment from the embedded tx params does not match `subject.utxo_commitment` — preventing commitment-tx binding bypass.
  4. **DoS resistant:** Maliciously oversized or malformed `transaction_data` payloads are safely rejected without crashing, hanging, or leaking resources (sanitization sandwich: size cap → hash verify → bounded parse).
  5. **All checks pass from embedded data:** All 12 existing validation checks (well-formed, authorization, timestamp, replay protection, type rules, witness, input conflict, nonce/address consistency, transaction status, migration eligibility) produce correct results when the transaction is sourced from embedded bytes. Re-proposals (vote bundles) find the temporarily tracked transaction and reach `Check::Approve`, enabling the validator to cast a vote.

**Plans**: 2 plans

**Wave 1**

- [x] 01-01-PLAN.md — Walking Skeleton: proto schema + serialization threading + handler deserialization + temp tracking + all 12 validation checks (PROTO-01, SER-01, DESER-01, TRACK-01, VALID-ALL)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 01-02-PLAN.md — Validation Hardening: sanitization sandwich + commitment-tx binding + witness hardening + tracking lifecycle cleanup (SANTZ-01, BIND-01, TRACK-01)

### Phase 2: Conflict and Replay Detection Hardening

**Goal**: Standalone validators (without CRDT nonce/UTXO state) can reliably detect double-spends against previously certified transactions and reject nonce replays, using certificate chain data that all validators have access to — no CRDT state dependency for security-critical rejection.
**Mode**: mvp
**Depends on**: Phase 1
**Status**: Complete
**Requirements**: CONFLICT-01, NONCE-01
**Success Criteria** (what must be TRUE):

  1. **Double-spend detection without CRDT state:** A standalone validator that receives a proposal spending a UTXO already consumed in a prior certified transaction correctly returns `Check::Reject`, using certificate store cross-referencing instead of local `tx_processed_m` history.
  2. **Nonce replay detection without CRDT state:** A standalone validator receiving a transaction whose nonce was already used in a prior certified transaction correctly rejects it, using either an embedded `confirmed_nonce` field or a lazy certificate-store fallback.
  3. **No regression for full nodes:** Validators with full CRDT state (Genesis, full nodes) continue to detect double-spends and nonce replays with identical accuracy as before — the Phase 2 changes are additive, not substitutive.
  4. **Deterministic across validators:** For the same proposal, a CRDT-full node and a CRDT-less standalone validator produce the same Accept/Reject decision — no divergence due to state availability differences.

**Plans**: 1 plan

**Wave 1**

- [x] 02-01-PLAN.md — Certificate Fallback Deserialization: signature change + certificate fallback path in OnConsensusCertificate + edge case hardening (CONFLICT-01, NONCE-01)

### Phase 3: Network Hardening and Operational Readiness

**Goal**: The protocol is robust at scale — oversized messages are caught before PubSub publish, timestamp validation tolerates distributed clock skew, temporary tracking data is cleaned up, and operational metrics are available for monitoring and debugging.
**Mode**: mvp
**Depends on**: Phase 2
**Status**: Complete
**Requirements**: SIZE-01, TS-01, CLEAN-01, METRICS-01
**Success Criteria** (what must be TRUE):

  1. **Message size enforcement:** Proposals exceeding the configured PubSub message size threshold are rejected at proposal creation time (pre-publish) with a clear error, preventing silent PubSub message drops.
  2. **Clock skew tolerance:** Proposals from validators whose clocks are within a configurable tolerance window (default wider than the current ±5 minutes) are accepted rather than rejected on timestamp grounds alone, enabling geographically distributed validators to participate.
  3. **Tracking cleanup:** Temporary `tx_processed_m` entries from Phase 1's TRACK-01 are removed after the voting lifecycle completes (certificate produced, proposal rejected, or proposal timed out), with no memory leak observed over sustained multi-hour operation.
  4. **Observability:** Operational logs/metrics show standalone validator voting rate, proposal validation success/failure breakdown (by check type), and tracking entry lifecycle events — enabling troubleshooting in production.

**Plans**: 2 plans

**Wave 1**

- [x] 03-01-PLAN.md — Size Enforcement + Timestamp Tolerance + Metrics: pre-publish 64KB gate at SendTransactionItem, configurable DevConfig timestamp tolerance, atomic metrics counters + lifecycle logging + destructor flush (SIZE-01, TS-01, METRICS-01)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 03-02-PLAN.md — Tracking Entry Cleanup via ProposalCleanupHandler: callback registration on ConsensusManager/Blockchain, FireProposalCleanupCallbacks at timeout callers (NOT certificate path), TransactionManager OnProposalTimeoutCleanup handler transitioning VERIFYING → FAILED (CLEAN-01)

## Design Notes

### Consensus Trust Model for Bridge Mints

Public-chain mint validation differs from internal transfers: a compromised validator can skip RPC verification and vote `Approve` without detection. Reputation-based voting alone is insufficient — a quorum of colluding nodes could mint fake tokens.

**Proposed safeguard:** Require at least **N validators** to independently and verifiably confirm the burn receipt via RPC before the certificate is produced. Each validator's vote should include a commitment to the RPC result (e.g., `sha256(rpc_response)`) so other nodes can detect fabricated votes. This moves trust from "reputation" to "independent verification count."

**Current state:** `VerifyPublicChainSmartContract` queries 3+ RPC endpoints per validator. This protects against RPC endpoint compromise but does not protect against validator compromise. The consensus protocol layer needs an additional quorum rule for bridge mints.

---

## Out of Scope (v1)

| Concern | Disposition |
|---------|-------------|
| Multi-provider quorum (`RpcQuorumClient`, `ProviderVote<T>`) | SuperGenius builds on evmrelay's endpoint pool |
| `SecurityDecision` structured evidence | Deferred — validator uses simple majority |
| P2P RLPx burn event gossip | Deferred — WebSocket/RPC polling for v1 |
| `EthWatchService` integration into watcher | Deferred — uses dedicated BridgeRpcWatcher |
| Burn contract deployment/management | External concern |

### Phase 7: Deferred Validation and Pending Proposal Lifecycle

**Goal**: Proposals that are temporarily unverifiable remain eligible for validation and voting when
their dependencies arrive or transient failures recover, without retaining unbounded state or
misclassifying inconclusive consensus as transaction failure.
**Mode**: mvp
**Depends on**: Phase 3
**Status**: Not started
**Requirements**: PEND-01, PEND-02, PEND-03, PEND-04, PEND-05, PEND-06, PEND-07, TXSTATE-01
**Design note**: [Deferred Consensus Validation](notes/deferred-consensus-validation.md)
**Success Criteria** (what must be TRUE):

  1. **Out-of-order certificate recovery:** When Peer B receives `tx2` before `tx1`'s certificate,
     Peer B keeps `tx2` pending and automatically revalidates it when the certificate arrives. A
     valid `tx2` then receives Peer B's Approval vote without requiring re-proposal.
  2. **Generic deferred outcomes:** Subject handlers can identify explicit dependency keys or request
     bounded scheduled retry for transient failures. Pending remains local and never contributes to
     quorum.
  3. **Bounded lifecycle:** Pending state uses a compile-time three-minute default TTL, supports an
     injected ten-second test TTL, and enforces count and retained-byte limits.
  4. **Complete cleanup:** Certification, terminal rejection, and expiry remove proposal,
     dependency, vote, retry, and temporary transaction state without duplicate callbacks or leaks.
  5. **Correct terminal state:** Locally proven invalid transactions become `FAILED`; proposals that
     reach TTL without a conclusive outcome become `EXPIRED` or `UNCONFIRMED`.
  6. **Retry safety:** Revalidation is idempotent, emits at most one local Approval vote per proposal
     slot, and never double-counts validator weight.

**Plans**: 5 plans
Plans:

**Wave 0**

- [x] 07-01-PLAN.md — Wave 0 focused pending lifecycle test targets

**Wave 1** *(blocked on Wave 0 completion)*

- [x] 07-02-PLAN.md — Structured validation result and local-only Pending contract

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 07-03-PLAN.md — Bounded pending pool, dependency indexes, and cleanup accounting

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 07-04-PLAN.md — Dependency wakeups, scheduled retry, TTL expiry, and retry safety

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 07-05-PLAN.md — TransactionManager pending dependency and UNCONFIRMED expiry semantics

---

## Progress Summary

| Track | Phase | Status | Completed |
|-------|-------|--------|-----------|
| A. EVM Bridge | 1. Wire RPC Endpoints | Complete | 2026-05-27 |
| A. EVM Bridge | 2. Relayer — Burn Detection | Complete | 2026-05-31 |
| A. EVM Bridge | 3. Burn Dedup Cache | Complete | 2026-05-31 |
| A. EVM Bridge | 4. E2E Integration Test | Complete | impl verified 2026-06-09 |
| A. EVM Bridge | 5. Startup Wiring + Mock RPC | In review (PR #309) | 2026-06-04 |
| A. EVM Bridge | 6. Network Voting (Tier 2) | Not started | - |
| B. Consensus | 1. Embedded-Transaction Validation | Complete | 2026-05-27 |
| B. Consensus | 2. Conflict and Replay Hardening | Complete | impl verified 2026-06-09 |
| B. Consensus | 3. Network Hardening | Complete | impl verified 2026-06-09 |
| B. Consensus | 7. Deferred Validation Lifecycle | Not started | - |

---

## Milestone v1.1: Multi-Signature Secure CRDT Storage

**Goal:** Add a decoupled multi-signature primitive and a secure CRDT storage layer so specific CRDT-backed values require quorum signatures to create/update — applied first to a new `TrustedPeerRegistry` and to `BURN_BASIS_POINTS`; `ValidatorRegistry` migrated onto the same interface.

### Phases

- [x] **Phase 8: MultiSig Primitive** - Standalone N-of-M signature/quorum component reusing `ConsensusAuth` primitives (completed 2026-07-23)
- [x] **Phase 9: SecureCRDT Layer** - `ISignedCRDTData` interface + static policy registry + CRDT-transported propose/sign/quorum flow (completed 2026-07-23)
- [x] **Phase 10: TrustedPeerRegistry** - Genesis-seeded, quorum-updatable trusted-peer set built on SecureCRDT (completed 2026-07-24)
- [x] **Phase 11: BurnConfig Quorum Wiring** - `BURN_BASIS_POINTS` becomes a TrustedPeerRegistry-quorum-signed CRDT value, cached in `TransactionManager` (completed 2026-07-27)
- [ ] **Phase 12: ValidatorRegistry Migration** - `ValidatorRegistry`'s genesis-path signature verification migrated onto `multisig::VerifyPayloadSignature`, existing behavior/tests preserved

## Phase Details

### Phase 8: MultiSig Primitive

**Goal**: A standalone, node-independent component can compute canonical signing-bytes for an arbitrary payload, verify signatures against it, and evaluate N-of-M quorum for a given signer set and threshold.
**Depends on**: Nothing (first phase of this milestone)
**Requirements**: MSIG-01, MSIG-02, MSIG-03
**Success Criteria** (what must be TRUE):
  1. Given a payload, the component produces canonical signing-bytes and verifies a valid signature against them using `ConsensusAuth`'s SHA-256/`VerifySignature` primitives, rejecting invalid/tampered signatures.
  2. Given a signer set and a required threshold (N-of-M, no hardcoded N), the component correctly reports quorum-met/quorum-not-met for varying valid-signature counts, including boundary cases (exactly N, N-1, all M).
  3. The component can be constructed, exercised, and unit-tested with no running node, no CRDT store, and no network dependency.
**Plans**: 1 plan

Plans:
- [x] 08-01-PLAN.md — MultiSig library (VerifyPayloadSignature + EvaluateQuorum) + CMake wiring + tests (MSIG-01, MSIG-02, MSIG-03)

### Phase 9: SecureCRDT Layer

**Goal**: Registered CRDT keys can only be created/updated when accompanied by quorum-verified signatures, using CRDT's own put/filter-callback mechanism as the sole transport — no unsigned or under-signed write is ever applied.
**Depends on**: Phase 8
**Requirements**: SCRDT-01, SCRDT-02, SCRDT-03, SCRDT-04
**Success Criteria** (what must be TRUE):
  1. An `ISignedCRDTData` interface exists; a concrete implementer can supply a payload codec plus `Verify()`/`Apply()`, and the interface compiles/links independent of any specific data type.
  2. A static, code-declared registry maps topic/key patterns to {signer-set source, quorum rule, `ISignedCRDTData` type}, resolvable at startup for a given key.
  3. A propose+sign+quorum sequence for a registered key — driven entirely by CRDT puts and filter callbacks (pending-value entry + signature entries) — results in the value being applied only after quorum is reached, with no new networking/RPC code path introduced.
  4. An unsigned or under-signed write attempt to a registered key is rejected locally (never applied) before quorum is reached, verified by an automated test.
**Plans**: 2 plans

Plans:
- [x] 09-01-PLAN.md — ISignedCRDTData interface + SecureCrdtRegistry static registry + Wave-0 tests (SCRDT-01, SCRDT-02)
- [x] 09-02-PLAN.md — SecureCrdt wrapper (local-write gate + filter registration + read-path quorum re-derivation) + quorum-gate/propose-sign-quorum tests (SCRDT-03, SCRDT-04)

### Phase 10: TrustedPeerRegistry

**Goal**: A new `TrustedPeerRegistry` component maintains a genesis-seeded, quorum-updatable set of trusted peers, implemented entirely via the SecureCRDT abstraction rather than bespoke signature-checking logic.
**Depends on**: Phase 9
**Requirements**: TPR-01, TPR-02, TPR-03
**Success Criteria** (what must be TRUE):
  1. A freshly-initialized genesis node's `TrustedPeerRegistry` contains exactly the initial trusted-peer set hardcoded in genesis config, with no manual bootstrapping step required.
  2. Adding, removing, or replacing a trusted-peer member succeeds only when a configurable N-of-M quorum of signatures from the CURRENT trusted-peer set is presented; a sub-quorum attempt is rejected and the membership set is unchanged.
  3. `TrustedPeerRegistry`'s implementation is a consumer of `ISignedCRDTData`/SecureCRDT (registered via the Phase 9 policy registry) — code inspection confirms no parallel/duplicate signature-verification logic exists outside SecureCRDT.
**Plans**: 2 plans

Plans:
- [x] 10-01-PLAN.md — TrustedPeerRegistry core: TrustedPeerListPayload (ISignedCRDTData) + cache/signer-set-source/Propose/Sign/TryConfirm/SeedGenesis + CMake wiring (TPR-01, TPR-02, TPR-03)
- [x] 10-02-PLAN.md — Genesis ceremony test helper + genesis/quorum tests + sgns_config.json trusted_peers/bootstrapper_node parsing (TPR-01, TPR-02, TPR-03)

### Phase 11: BurnConfig Quorum Wiring

**Goal**: `BURN_BASIS_POINTS` is a live, quorum-signed CRDT value gated by `TrustedPeerRegistry` membership instead of a compile-time constant, and `TransactionManager` reads a cached value refreshed via callback rather than performing a CRDT read on every `PayEscrow` call.
**Depends on**: Phase 10
**Requirements**: BURN-01, BURN-02, BURN-03
**Success Criteria** (what must be TRUE):
  1. `BURN_BASIS_POINTS` is stored and updated as a `TrustedPeerRegistry`-quorum-signed CRDT value (via the Phase 9/10 SecureCRDT machinery), not a hardcoded constant in `TransactionManager.hpp`.
  2. `TransactionManager::PayEscrow` uses a cached in-memory value for the burn rate; no CRDT read occurs on the `PayEscrow` call path, and the cache updates automatically via a CRDT-change callback when a quorum-signed update lands.
  3. A freshly-seeded genesis node burns exactly 1% (`BURN_BASIS_POINTS=100`) on `PayEscrow` by default, matching pre-milestone behavior, until a quorum-signed update changes the value.
**Plans**: 2 plans

Plans:
- [x] 11-01-PLAN.md — Majority-floor quorum validation (D-07) + TrustedPeerRegistry::New breaking-change retrofit + BurnConfigPayload/BurnConfig core (genesis auto-seed, signer-set-source, cache-refresh) + tests (BURN-01)
- [x] 11-02-PLAN.md — TransactionManager cached burn-rate + GeniusNode INITIALIZING_TRANSACTIONS wiring (SecureCrdt/TrustedPeerRegistry/BurnConfig construction) + config fields + CMake linkage (BURN-02, BURN-03)

### Phase 12: ValidatorRegistry Migration

**Goal**: `ValidatorRegistry`'s genesis-path signature verification is migrated from `GeniusAccount::VerifySignature` onto the shared `multisig::VerifyPayloadSignature` primitive (Phase 8), narrowed per 12-CONTEXT.md D-01/D-03 to signature-verification-only reuse — `ValidatorRegistry` does not adopt `ISignedCRDTData`/`SecureCrdt` (its weighted-quorum/certificate machinery is structurally incompatible), with zero regression in existing behavior or tests.
**Depends on**: Phase 8
**Requirements**: MIG-05, MIG-06
**Success Criteria** (what must be TRUE):
  1. `VerifyUpdate`'s genesis-path signature check calls `multisig::VerifyPayloadSignature` instead of `GeniusAccount::VerifySignature`; `blockchain_genesis` links `multisig` directly.
  2. All pre-migration `ValidatorRegistry` unit/integration tests pass unchanged, with no behavioral regression in genesis-signature verification, and the D-05 `multi_account_test` exit gate (5-10 consecutive clean runs) is satisfied.
**Plans:** 1 plan
Plans:
- [ ] 12-01-PLAN.md — Migrate genesis-path signature verification onto multisig::VerifyPayloadSignature, wire CMake link, run D-05 exit gate

### Progress Table (v1.1)

| Phase | Plans Complete | Status | Completed |
|-------|-----------------|--------|-----------|
| 8. MultiSig Primitive | 1/1 | Complete   | 2026-07-23 |
| 9. SecureCRDT Layer | 2/2 | Complete   | 2026-07-23 |
| 10. TrustedPeerRegistry | 2/2 | Complete   | 2026-07-24 |
| 11. BurnConfig Quorum Wiring | 2/2 | Complete   | 2026-07-27 |
| 12. ValidatorRegistry Migration | 0/1 | Not started | - |
