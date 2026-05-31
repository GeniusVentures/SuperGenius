# SuperGenius Roadmap

Two active development tracks: **EVM Bridge Integration** and **Consensus Voting Decentralization**.

---

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
- [ ] Unit test for BridgeRelayer
- [ ] Validators independently verify the burn via `PublicChainInputValidator::VerifyPublicChainSmartContract`

**Success criteria:**
- Burn event on EVM chain triggers `MintFunds` via evmrelay signal
- `MintTransactionV2` created with correct chain_id, amount, token_id, burn_tx_hash
- Transaction enters nonce consensus

---

### Phase 3: Burn Deduplication Cache

**Goal:** Prevent double-minting by tracking which burn transaction hashes have already been processed.

**Tasks:**
- [ ] Add a `burn_cache[burn_tx_hash].used` map to `PublicChainInputValidator`
- [ ] Check the cache in `VerifyPublicChainSmartContract` before RPC verification
- [ ] Mark the burn hash as used after successful mint consensus certificate
- [ ] Persist cache across restarts (CRDT or local RocksDB)

**Success criteria:**
- Same burn tx hash cannot produce two mint transactions
- Cache survives node restart
- Unit tests for duplicate rejection

---

### Phase 4: End-to-End Integration Test

**Goal:** Demonstrate the full pipeline: EVM burn → detection → MintTransactionV2 → UTXO consensus → RPC verification → minted tokens.

**Tasks:**
- [ ] Live test against Ethereum Sepolia GNUS contract
- [ ] Send a test burn transaction via `send_test_transactions.sh`
- [ ] Relayer detects the burn, creates MintTransactionV2
- [ ] Validator(s) verify the burn receipt via RPC
- [ ] UTXO consensus produces certificate
- [ ] Minted tokens appear in recipient's UTXO set

**Success criteria:**
- Single end-to-end test script or C++ integration test demonstrating the complete flow
- RPC verification succeeds with 3+ independent endpoints
- No manual steps beyond sending the initial burn tx

---

### Phase 5: Startup Catch-Up for Unprocessed Burns

**Goal:** On node startup (especially full/archive nodes), after syncing CRDT data, grab the last mint message transaction by date and use RPC to check the contract for any unprocessed bridged transactions that need to be minted.

**Depends on:** Phase 4
**Plans:** 0 plans

- [ ] TBD (run /gsd-plan-phase 5 to break down)

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
**Requirements**: CONFLICT-01, NONCE-01
**Success Criteria** (what must be TRUE):

  1. **Double-spend detection without CRDT state:** A standalone validator that receives a proposal spending a UTXO already consumed in a prior certified transaction correctly returns `Check::Reject`, using certificate store cross-referencing instead of local `tx_processed_m` history.
  2. **Nonce replay detection without CRDT state:** A standalone validator receiving a transaction whose nonce was already used in a prior certified transaction correctly rejects it, using either an embedded `confirmed_nonce` field or a lazy certificate-store fallback.
  3. **No regression for full nodes:** Validators with full CRDT state (Genesis, full nodes) continue to detect double-spends and nonce replays with identical accuracy as before — the Phase 2 changes are additive, not substitutive.
  4. **Deterministic across validators:** For the same proposal, a CRDT-full node and a CRDT-less standalone validator produce the same Accept/Reject decision — no divergence due to state availability differences.

**Plans**: 1 plan

**Wave 1**

- [ ] 02-01-PLAN.md — Certificate Fallback Deserialization: signature change + certificate fallback path in OnConsensusCertificate + edge case hardening (CONFLICT-01, NONCE-01)

### Phase 3: Network Hardening and Operational Readiness

**Goal**: The protocol is robust at scale — oversized messages are caught before PubSub publish, timestamp validation tolerates distributed clock skew, temporary tracking data is cleaned up, and operational metrics are available for monitoring and debugging.
**Mode**: mvp
**Depends on**: Phase 2
**Requirements**: SIZE-01, TS-01, CLEAN-01, METRICS-01
**Success Criteria** (what must be TRUE):

  1. **Message size enforcement:** Proposals exceeding the configured PubSub message size threshold are rejected at proposal creation time (pre-publish) with a clear error, preventing silent PubSub message drops.
  2. **Clock skew tolerance:** Proposals from validators whose clocks are within a configurable tolerance window (default wider than the current ±5 minutes) are accepted rather than rejected on timestamp grounds alone, enabling geographically distributed validators to participate.
  3. **Tracking cleanup:** Temporary `tx_processed_m` entries from Phase 1's TRACK-01 are removed after the voting lifecycle completes (certificate produced, proposal rejected, or proposal timed out), with no memory leak observed over sustained multi-hour operation.
  4. **Observability:** Operational logs/metrics show standalone validator voting rate, proposal validation success/failure breakdown (by check type), and tracking entry lifecycle events — enabling troubleshooting in production.

**Plans**: 2 plans

**Wave 1**

- [ ] 03-01-PLAN.md — Size Enforcement + Timestamp Tolerance + Metrics: pre-publish 64KB gate at SendTransactionItem, configurable DevConfig_st timestamp tolerance, atomic metrics counters + lifecycle logging + destructor flush (SIZE-01, TS-01, METRICS-01)

**Wave 2** *(blocked on Wave 1 completion)*

- [ ] 03-02-PLAN.md — Tracking Entry Cleanup via ProposalCleanupHandler: callback registration on ConsensusManager/Blockchain, FireProposalCleanupCallbacks at timeout callers (NOT certificate path), TransactionManager OnProposalTimeoutCleanup handler transitioning VERIFYING → FAILED (CLEAN-01)

---

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

---

## Progress Summary

| Track | Phase | Status | Completed |
|-------|-------|--------|-----------|
| A. EVM Bridge | 1. Wire RPC Endpoints | Complete | 2026-05-27 |
| A. EVM Bridge | 2. Relayer — Burn Detection | Not started | - |
| A. EVM Bridge | 3. Burn Dedup Cache | Not started | - |
| A. EVM Bridge | 4. E2E Integration Test | Not started | - |
| A. EVM Bridge | 5. Startup Catch-Up | Not started | - |
| B. Consensus | 1. Embedded-Transaction Validation | Complete | 2026-05-27 |
| B. Consensus | 2. Conflict and Replay Hardening | Not started | - |
| B. Consensus | 3. Network Hardening | Planned | - |
