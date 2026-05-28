# Roadmap: SuperGenius Consensus Voting Decentralization

## Overview

A three-phase protocol change to make SuperGenius consensus truly decentralized. Phase 1 embeds full serialized transaction bytes in `NonceSubject` messages so any validator can validate and vote from the proposal alone — no CRDT dependency. Phase 2 hardens security for standalone validators by closing double-spend and nonce-replay detection gaps using the certificate chain. Phase 3 hardens network resilience with message size enforcement, clock-skew tolerance, memory cleanup, and operational metrics.

## Phases

- [x] **Phase 1: Core Embedded-Transaction Validation Path** — Validators validate and vote from embedded tx bytes, no CRDT needed (completed 2026-05-27)
- [ ] **Phase 2: Conflict and Replay Detection Hardening** — Standalone validators detect double-spends and nonce replays via certificate chain
- [ ] **Phase 3: Network Hardening and Operational Readiness** — Robust at scale: size enforcement, clock tolerance, cleanup, metrics

## Phase Details

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
Plans:
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

**Plans**: TBD

## Progress

**Execution Order:** Phases execute in numeric order: 1 → 2 → 3

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Core Embedded-Transaction Validation Path | 2/2 | Complete   | 2026-05-27 |
| 2. Conflict and Replay Detection Hardening | 0/TBD | Not started | - |
| 3. Network Hardening and Operational Readiness | 0/TBD | Not started | - |
