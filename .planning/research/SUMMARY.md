# Project Research Summary

**Project:** Consensus Voting Decentralization via Embedded Transaction Data
**Domain:** Block-lattice consensus protocol (C++17 + protobuf + CRDT)
**Researched:** 2026-05-27
**Confidence:** HIGH

## Executive Summary

This project fixes a fundamental architectural limitation in the SuperGenius consensus protocol: currently, only peers that have synced a transaction's data via CRDT can validate and vote on proposals for that transaction. The fix embeds the full serialized transaction protobuf (`SerializeByteVector()` output) directly into the `NonceSubject` message, enabling any consensus-group member to deserialize, validate, and vote from the proposal bytes alone — no CRDT dependency for the core validation path.

The recommended approach, confirmed by all four research tracks, is a surgical change: add two fields to the `NonceSubject` protobuf (`string transaction_type = 5`, `bytes transaction_data = 6`), thread the serialized transaction through the existing proposal creation pipeline (`CreateNonceSubject` → `CreateConsensusProposal`), and replace the CRDT lookup in `HandleNonceConsensusSubject` with deserialization from the embedded bytes. This is the dominant pattern in block-lattice and DAG-based systems (Nano, Avalanche, IOTA) — the consensus unit carries the data it needs to be validated. The existing serialization/deserialization infrastructure (`IGeniusTransactions`, `deserializers_map`), validation checks, and consensus mechanics (voting, quorum, certificates) all remain unchanged.

The key risk is that standalone validators (non-destination peers that vote without CRDT state) face informational gaps: they may lack the local UTXO history needed for double-spend detection and the nonce-tracking state needed for replay protection. The research identifies specific mitigations for each gap — cross-referencing the certificate chain for conflict detection, embedding `confirmed_nonce` in the proposal, and inserting deserialized transactions into the tracking map as temporary entries. These gaps are structural, not fatal, and the recommended approach addresses them in later phases so the core validation path can ship first.

## Key Findings

### Recommended Stack

**Core Decision:** Embed the full serialized transaction protobuf in `NonceSubject` alongside a type-tag string for dispatch. This uses protobuf `bytes` field (wire type 2, LEN-delimited) — the same pattern as `google.protobuf.Any` internally. Transaction protobuf schemas can evolve independently of the consensus schema; the embedded bytes are literally the same bytes used for hashing and CRDT persistence, eliminating "did we serialize the same way?" bugs.

Transaction sizes range from ~200 bytes (MintTx) to ~1200 bytes (large TransferTx). The `NonceSubject` grows from ~250 bytes to ~700-1500 bytes — a 3x to 6x increase that adds ~450 bytes per proposal, well within acceptable bounds for libp2p PubSub (at 10 tx/sec sustained: ~4.5 KB/sec additional throughput). The existing `deserializers_map` (static registration pattern) already dispatches by type string, so zero new infrastructure is needed for deserialization.

**Core technologies:**
- **Protobuf `bytes` field (field 6)**: Carries `SerializeByteVector()` output — self-delimiting, no schema collision with NonceSubject, exact byte-preserving round-trip
- **Protobuf `string` field (field 5)**: Transaction type tag for deserializer dispatch — avoids fragile partial parsing of the embedded bytes
- **Existing `IGeniusTransactions` interface**: `SerializeByteVector()`, `DeSerializeTransaction()`, `GetHash()`, `CheckHash()`, `CheckSignature()` — all work as-is with embedded bytes
- **Existing `deserializers_map`**: Static registration of type-specific deserializers invoked by `DeSerializeTransaction(type_string, bytes)` — zero new code needed

### Expected Features

**Must have (table stakes):**

The 12 validation checks that every validator must perform on an embedded transaction, with strict ordering:

1. **F1 — Deserialization from embedded bytes:** Parse `transaction_data` into a `shared_ptr<IGeniusTransactions>`. Short-circuit to `Reject` on parse failure.
2. **F2 — Structural well-formed check:** Hash matches content, source address non-empty, timestamp non-zero, type known.
3. **F3 — Signature authorization:** DAGStruct signature verification via `CheckSignature()`.
4. **F4 — Timestamp tolerance:** ±5 minutes from validator's local clock.
5. **F5 — Replay protection (nonce chain):** Nonce > confirmed_nonce, previous_hash chain valid, all intermediate nonces exist and aren't FAILED. **Most complex check** — depends on blockchain certificate state.
6. **F6 — Transaction type rules:** UTXO parameter validation, chain-specific ownership rules.
7. **F7 — Witness validation:** Reconstruct UTXO commitment from embedded tx params, compare against `NonceSubject.utxo_commitment`, verify Merkle inclusion proofs from `utxo_witness`. **Security-critical differentiator.**
8. **F8 — Input conflict detection:** Scan for double-spends against locally-confirmed transactions.

Plus F9-F12: nonce/subject consistency, source address consistency, status check, migration eligibility.

**Validation dependency chain:** Deserialization → Well-formed → Signature → Timestamp → Address/Nonce consistency → Replay protection → Status → Input conflict → Type rules → Witness → Migration eligibility. Everything through witness validation returns `Reject` on failure (deterministic); only migration eligibility returns `Pending` (may lack local data).

**Should have (differentiators):**
- **D1 — Zero-CRDT validation path:** Non-destination validators vote without storing transaction data in CRDT — the core purpose of the fix
- **D2 — Commitment self-verification:** Validator reconstructs UTXO commitment from embedded tx params and verifies against proposer's claimed commitment — cryptographic binding without trust
- **D3 — Single-message self-containment:** All validation data (tx bytes + commitment + witness) in one PubSub message

**Defer (v2+):**
- **F12 (Migration eligibility):** Can return `Pending` for non-destination peers initially. Migration is a bounded lifecycle event.
- **Full UTXO history for standalone validators:** Documented as a structural limitation with certificate-chain-based mitigation recommended for Phase 2.

**Anti-features to guard against:**
- **AF1:** Full CRDT sync for validators — defeats the purpose
- **AF2:** Transaction storage on non-destination validators — unbounded storage growth
- **AF4:** Partial transaction embedding — breaks commitment/witness binding verification
- **AF6:** Lazy validation (vote first, validate later) — dangerous in UTXO blockchain
- **AF8:** Separate validation paths for embedded vs CRDT-sourced transactions — maintenance burden

### Architecture Approach

The change is surgical: one new field in protobuf, one new parameter threaded through three functions, one code path in the handler replaced. The consensus mechanics (proposal creation, voting, quorum, certificate) are completely untouched — only the validation data source changes.

In the current architecture, `HandleNonceConsensusSubject` looks up `tx_hash` in `tx_processed_m` (a map populated by CRDT sync). If not found, it returns `Check::Pending` — meaning non-destination peers can never vote. In the new architecture, the handler extracts `transaction_data` from `NonceSubject`, deserializes it via `DeSerializeTransaction()`, verifies `hash(embedded_bytes) == tx_hash`, and runs all existing validation checks against the deserialized transaction. The CRDT lookup is removed; all validation operates on the same `shared_ptr<IGeniusTransactions>` it always did.

**Major components (touch points):**

1. **Consensus.proto / NonceSubject:** Add `string transaction_type = 5` and `bytes transaction_data = 6`. Pure addition — no field renumbering, no backward compat needed per PROJECT.md's clean-break decision.

2. **ConsensusManager (`CreateNonceSubject`):** New `transaction_type` and `transaction_data` parameters, sets them on the protobuf. All downstream logic (`CreateProposal`, `HandleProposal`, `GetSlotKey`, `IsBetterProposal`, voting) unchanged.

3. **Blockchain facade (`CreateConsensusProposal`):** Threads the new parameters through to `CreateNonceSubject`. Thin delegation layer — mechanical change.

4. **TransactionManager (proposal creation):** `SendTransactionItem()` calls `SerializeByteVector()` and passes result to `CreateConsensusProposal`. Uses existing serialization — zero new serialization code.

5. **TransactionManager (validation):** `HandleNonceConsensusSubject()` replaces CRDT lookup with deserialization from embedded bytes, adds hash-binding verification, runs all existing checks. One behavioral location change, all checks preserved.

**Architecture invariants preserved:** Consensus PubSub topic, vote/certificate mechanics, CRDT persistence (tx still written to `/tx/{tx_hash}`), all `Check*` methods, slot arbitration, subject handler registration, certificate creation, pending proposal retry.

**Build order (dependency graph):** Protobuf schema → `CreateNonceSubject` signature → Blockchain facade threading → Proposal creation side (serialize+embed) → Validation side (deserialize+check) → Test suite. Phases 2-4 can be developed together (mechanical threading); Phase 5 is the behavioral change and gets focused review.

### Critical Pitfalls

1. **Deserialization DoS from untrusted PubSub bytes (Critical):** A malicious peer can embed crafted protobuf bytes that trigger memory exhaustion or undefined behavior. **Prevent:** Add sanitization sandwich — hard size cap (64 KB), hash-before-deserialize integrity check, bounded-memory parse with `Reject` on failure. Must be addressed in Phase 1 alongside the core change.

2. **Transaction-not-in-tx_processed_m causes permanent Pending (Critical):** The deserialized transaction is a stack temporary never inserted into the tracking map. On re-proposal (vote bundles), the lookup still fails and the validator never reaches `Approve`. **Prevent:** Insert deserialized transactions into `tx_processed_m` as temporary VERIFYING entries; remove on failure, promote on certificate arrival. Use a flag to distinguish proposal-sourced from CRDT-sourced entries. Must be addressed in Phase 1.

3. **Double-spend gap on standalone validators (Critical):** `HasConfirmedInputConflict` scans `tx_processed_m` for CONFIRMED transactions — but standalone validators lack historical UTXO state. A validator can approve conflicting proposals. **Prevent:** Cross-reference certificate chain for input conflict detection (query `GetCertificateBySubjectHash` for each input's producer and consumer certificates). Add `certified_spends_` and `certified_outputs_` sets populated from `OnConsensusCertificate`. Recommended for Phase 2.

4. **Nonce replay protection depends on local CRDT state (Critical):** `CheckTransactionReplayProtection` reads `GetPeerNonce()` from `confirmed_nonces_` — a map populated by CRDT certificate ingestion. Standalone validators return error (treated as "no confirmed nonce" → accepts anything). **Prevent:** Embed `confirmed_nonce` in `NonceSubject` (8 bytes, trivial); proposer includes last certified nonce, validator verifies `nonce == confirmed_nonce + 1`. Fall back to scanning certificate store if not embedded. This requires an additional protobuf field beyond what STACK.md proposes — a gap to address during planning. Recommended for Phase 2.

5. **Commitment-tx-params binding not verified on embedded path (Critical):** If deserialization produces a transaction where `HasUTXOParameters()` returns false despite valid commitment/witness in the subject, the witness validation is silently skipped. **Prevent:** Add cross-check — if subject has `utxo_commitment` but tx has no UTXO params, reject. Add mandatory `BuildCommitment(deserialized_tx) == subject.utxo_commitment` check after deserialization. Must be addressed in Phase 1.

Moderate pitfalls include PubSub message size exceeding transport limits (silent drops — add pre-publish size validation), signature check being necessary but not sufficient for authorization (input signatures verified separately — move earlier in pipeline), and timestamp non-determinism across validators with clock skew (relax tolerance for consensus). Minor pitfalls include memory growth from temporary tracking entries and atomic consistency between embedded bytes and commitment/witness fields.

## Implications for Roadmap

Based on combined research, the work naturally decomposes into three phases with clear dependencies:

### Phase 1: Core Embedded-Transaction Validation Path

**Rationale:** This is the minimum viable change that achieves the project goal — non-destination validators can validate and vote on proposals. The protobuf schema change is a compile-time dependency for everything else. The deserialization path in the handler is the behavioral heart of the fix. All four research tracks agree this phase is the foundation.

**Delivers:** A validator that receives a `NonceSubject` with embedded `transaction_data` can deserialize the transaction, verify its integrity (hash binding, signature, well-formedness, UTXO commitment binding), and cast an `Approve` or `Reject` vote — without any CRDT state for the transaction.

**Implements (from FEATURES.md):** F1-F4, F6-F7, F9-F10, D1, D3, D5  
**Uses (from STACK.md):** `bytes transaction_data` (field 6), `string transaction_type` (field 5), `DeSerializeTransaction`, `deserializers_map`  
**Components (from ARCHITECTURE.md):** All six build-order phases (protobuf → ConsensusManager → Blockchain → proposal creation → validation → tests)  
**Must avoid (from PITFALLS.md):** Pitfall 1 (deserialization DoS), Pitfall 2 (tx_processed_m gap), Pitfall 5 (binding bypass), Pitfall 7 (input signature ordering), Pitfall 10 (atomic consistency), Pitfall 11 (legacy signature bypass)

**Research flags:** MEDIUM. The core approach is well-documented and verified against the existing codebase. However, the exact mechanism for inserting deserialized transactions into the tracking map (Pitfall 2 prevention) and the sanitization sandwich design (Pitfall 1 prevention) benefit from detailed planning-phase research. The protobuf schema change is trivial and needs no further research.

### Phase 2: Conflict and Replay Detection Hardening

**Rationale:** Phase 1 enables voting but leaves structural gaps in double-spend detection and nonce replay protection for standalone validators. These gaps exist because standalone validators lack the historical UTXO and nonce state that full nodes have. Phase 2 closes these gaps using the certificate chain (which all validators have access to) as the authoritative source of truth.

**Delivers:** Standalone validators can detect double-spends against previously certified transactions (via certificate chain cross-referencing) and reject nonce replays (via embedded `confirmed_nonce` or lazy certificate store fallback). The voting pool can expand safely because validators don't need CRDT state for security-critical checks.

**Implements (from FEATURES.md):** F5 (hardened), F8 (certificate-aware), D4 (deterministic rejection strengthened)  
**Uses (from STACK.md):** Certificate store (`GetCertificateBySubjectHash`), potential new `confirmed_nonce` field in `NonceSubject`  
**Components (from ARCHITECTURE.md):** Enhanced `HasConfirmedInputConflict` with certificate chain queries, enhanced `CheckTransactionReplayProtection` with lazy nonce fetch  
**Must avoid (from PITFALLS.md):** Pitfall 3 (double-spend gap), Pitfall 4 (nonce replay), Pitfall 6 (PubSub size limit enforcement)

**Research flags:** HIGH. This phase needs detailed planning research. The `confirmed_nonce` embedding decision (Pitfall 4, option 2) requires a protobuf schema change beyond what STACK.md scoped — this is a notable gap between the Pitfalls and Stack research files. The certificate-chain-based conflict detection (Pitfall 3, option 2) needs validation against the actual certificate store API and indexing strategy. Both warrant `/gsd-plan-phase --research-phase` during planning.

### Phase 3: Network Hardening and Operational Readiness

**Rationale:** With the core validation and security hardening in place, Phase 3 addresses production concerns: PubSub message size enforcement, timestamp tolerance for distributed validators, memory management for per-proposal tracking entries, and operational monitoring.

**Delivers:** Proposals exceeding PubSub limits return clear errors instead of being silently dropped. Timestamp tolerance is generous enough for geographically distributed validators. Temporary tracking entries are cleaned up after certificate arrival or timeout. Metrics exist for debugging and monitoring.

**Implements (from FEATURES.md):** F4 (hardened timestamp tolerance), D2 (implicitly validated through monitoring)  
**Uses (from STACK.md):** PubSub transport configuration, message size budget documentation  
**Components (from ARCHITECTURE.md):** `SubmitProposal` size check, `tx_processed_m` periodic cleanup  
**Must avoid (from PITFALLS.md):** Pitfall 6 (PubSub silent drops), Pitfall 8 (timestamp non-determinism), Pitfall 9 (memory growth)

**Research flags:** LOW. Standard operational hardening — well-documented patterns. No dedicated research-phase needed.

### Phase Ordering Rationale

The ordering is strictly dependency-driven:
- **Phase 1 must come first:** The protobuf schema change is a compile-time dependency for everything. Without embedded deserialization working, nothing else matters.
- **Phase 2 depends on Phase 1:** Conflict and replay detection operate on deserialized embedded transactions. You can't harden validation you can't run.
- **Phase 3 follows Phase 2:** Operational hardening is meaningless until the functional path works and security checks are in place.

The grouping follows architecture patterns: Phase 1 is the "happy path" (make the new data flow work end-to-end), Phase 2 is the "adversarial path" (make it safe against attacks), Phase 3 is the "production path" (make it robust at scale).

### Research Flags

Phases likely needing deeper research during planning:
- **Phase 2:** `confirmed_nonce` protobuf embedding — not covered by STACK.md research; needs schema design and migration path validation. Certificate-chain-based conflict detection — needs API surface validation against `ConsensusManager` and `Blockchain` certificate store.
- **Phase 1 (partial):** Deserialization sanitization sandwich design — size caps, arena allocation, fuzzing strategy. Temporary tracking entry mechanism — flag design, cleanup lifecycle.

Phases with standard patterns (skip research-phase):
- **Phase 1 (protobuf and threading):** Pure addition to protobuf, mechanical parameter threading — no novel patterns.
- **Phase 3:** Standard PubSub configuration, memory management, operational monitoring — well-documented patterns.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Verified against actual serialization code paths, protobuf wire format docs, and domain patterns (Nano, Avalanche). All decision rationales backed by codebase inspection. |
| Features | HIGH | Every validation check traced to actual source lines in TransactionManager.cpp. Dependency chain verified against the code. Anti-features grounded in protocol design principles. |
| Architecture | HIGH | Before/after data flow traced through actual method signatures and call graphs. Component boundaries verified against codebase. Build order derived from compile-time and runtime dependencies. |
| Pitfalls | HIGH | All 11 pitfalls verified against actual code paths in the source. Prevention strategies evaluated against available APIs. Phase warnings cross-referenced with architecture dependency graph. |

**Overall confidence:** HIGH

The research is thorough and internally consistent. All four tracks agree on the core approach and validate each other's findings. The primary source is the actual codebase (lines cited throughout), supported by protocol documentation (Nano, protobuf) and domain patterns from multiple block-lattice/DAG systems.

### Gaps to Address

- **`confirmed_nonce` protobuf field (Pitfall 4 vs. STACK.md):** The Pitfalls research recommends embedding `confirmed_nonce` in `NonceSubject` (option 2) as the cleanest solution to nonce replay protection for standalone validators. The Stack and Architecture research only scope `transaction_type` (field 5) and `transaction_data` (field 6). This is a genuine gap — the roadmapper should decide during Phase 2 planning whether to add a third field (`confirmed_nonce = 7`) or rely on the lazy certificate store fallback alone. Both approaches work; embedding is cleaner but requires schema coordination.

- **Double-spend detection coverage analysis:** Pitfall 3 identifies the structural limitation but recommends certificate-chain cross-referencing (option 2) without quantifying its coverage. During Phase 2 planning, research should validate: (a) what percentage of historical UTXO consumption is covered by locally-available certificates vs. CRDT-only data, and (b) whether the certificate store API supports efficient reverse-lookup by `outpoint` (not just by `subject_hash`).

- **Fuzzing strategy for untrusted deserialization:** All research tracks agree a sanitization sandwich is needed, but none specify the fuzzing approach. Planning-phase research should design a fuzz harness for `HandleNonceConsensusSubject` with malformed `transaction_data` payloads.

- **Input signature verification ordering:** Pitfall 7 notes that input signatures are verified late in the pipeline (in witness validation). Moving them earlier (right after deserialization) closes a gap where early returns could skip input authorization. The Architecture track doesn't address this ordering concern. Planning should decide whether to restructure the validation order or add a dedicated `VerifyInputAuthorization` method.

- **Proof system re-enablement:** The "What Might Have Been Missed" section of Pitfalls notes that the dead proof verification code at TransactionManager.cpp line 2842 may need re-enabling for standalone validators. This was not explored in any research track and should be investigated during Phase 1 planning.

## Sources

### Primary (HIGH confidence)
- `src/account/TransactionManager.cpp` — Full validation flow (lines 3640-4399), conflict detection (3365-3410), replay protection (4031-4152), proposal handling
- `src/blockchain/Consensus.cpp` — `CreateNonceSubject` (2231-2261), `HandleProposal` (1131-1286), `GetSlotKey`/`IsBetterProposal` (2052-2089), `DecodeNonceSubject` (2181-2194)
- `src/blockchain/impl/proto/Consensus.proto` — `NonceSubject`, `ConsensusSubject`, `ConsensusProposal` definitions
- `src/account/proto/SGTransaction.proto` — `DAGStruct`, `TransferTx`, `MintTx`, `EscrowTx` definitions
- `src/account/IGeniusTransactions.hpp` — `SerializeByteVector()`, `DeSerializeTransaction()`, `deserializers_map`, `GetHash()`, `CheckHash()`, `CheckSignature()`
- `src/account/InputValidators.hpp` / `.cpp` — `IInputValidator`, `GeniusInputValidator`, witness validation logic
- `src/blockchain/impl/Blockchain.cpp` — `CreateConsensusProposal` facade (1690-1749)

### Protobuf Documentation (HIGH confidence)
- [Protobuf Language Guide (proto3) — bytes type](https://protobuf.dev/programming-guides/proto3/#scalar)
- [Protobuf Encoding — LEN wire type](https://protobuf.dev/programming-guides/encoding/#length-types)
- [Protobuf Best Practices — Dos and Don'ts](https://protobuf.dev/best-practices/dos-donts/)
- [Protobuf — Serialization Is Not Canonical](https://protobuf.dev/programming-guides/serialization-not-canonical/)

### Domain Patterns (HIGH confidence)
- [Nano Protocol Design — ORV Consensus](https://docs.nano.org/protocol-design/orv-consensus/) — Block-lattice self-contained blocks (216-byte fixed format)
- [Nano Integration Basics — Block Format](https://docs.nano.org/integration-guides/the-basics/#blocks-specifications)
- Avalanche DAG consensus — Transaction embedded in vertex pattern
- Cosmos/Tendermint — Block carries transactions, votes reference block hash
- IOTA 2.0 Tangle — Transactions broadcast independently, self-contained

### Project Specification (HIGH confidence)
- `.planning/PROJECT.md` — Requirements, scope, out-of-scope, key decisions (clean break, no backward compat)
- `.planning/codebase/ARCHITECTURE.md` — System overview
- `.planning/codebase/CONCERNS.md` — Cross-referenced concerns about proof verification, blank error codes

---
*Research completed: 2026-05-27*
*Ready for roadmap: yes*
