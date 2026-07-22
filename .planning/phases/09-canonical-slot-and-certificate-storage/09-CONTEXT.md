# Phase 9: Canonical Slot and Certificate Storage - Context

**Gathered:** 2026-07-22
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 9 establishes one deterministic finality identity for every consensus proposal, persists the authoritative certificate by that canonical slot, and preserves verified transaction-hash lookup for existing consumers. It covers canonical slot derivation, bridge burn identity propagation, v2 certificate/index storage, clean-state enforcement, and lookup semantics. Durable validator vote locking, finalized-slot state transitions, bridge reservation ownership, and the complete race/restart verification matrix remain in Phases 10-12.

</domain>

<decisions>
## Implementation Decisions

### Bridge Event Identity

- **D-01:** A source-chain transaction may contain multiple legitimate bridge burns. A burn is identified by source chain, transaction hash, and the absolute zero-based position of its log in the finalized transaction receipt's `logs` array.
- **D-02:** Do not use the RPC block-wide `logIndex` as the canonical event index. It can change when the same transaction is re-included after a reorganization. The receipt-local position becomes immutable with the finalized receipt and does not depend on node filtering rules.
- **D-03:** Carry the receipt-local log position as the synthetic bridge input's existing `InputUTXOInfo::output_idx_`. Together, `MintTxV2.chain_id`, the input transaction hash, and `output_idx_` contain the complete burn reference without adding a duplicate protobuf field.
- **D-04:** The event index is mandatory through the real-time relayer, catch-up path, `GeniusNode`, and `TransactionManager::MintFunds`. Missing indexes are rejected; there is no default-to-zero or RPC auto-selection fallback.
- **D-05:** Validators must select the exact receipt log and prove the proposed token, amount, and destination match that log. Those values are source-event facts, not proposer choices, but they do not define the finality slot.

### Canonical Slot Identity

- **D-06:** Preserve domain-specific canonical text as the human-readable slot preimage. A normal transaction retains its existing source-address-plus-nonce semantics.
- **D-07:** The bridge mint preimage is exactly `mint-v2:<source_chain_id>:<burn_tx_hash>:<receipt_log_index>`. Token ID, amount, destination, proposer address, and proposer nonce are excluded so every interpretation or proposal for the same external burn competes in one finality domain.
- **D-08:** The actual slot ID used for every consensus subject is the SHA-256 digest of its canonical text preimage. Arbitration maps, finalized-state checks, vote locks, and certificate keys use the same fixed-size ID; readable preimages remain available for diagnostics.
- **D-09:** Inputs used to construct a slot preimage must already be canonical. Reject aliases before consensus: numeric fields use unsigned decimal without leading zeros, hashes use exactly 64 lowercase hexadecimal characters without `0x`, and addresses use their canonical representation. Never hash caller formatting verbatim.
- **D-10:** Finality identity is defense in depth and must not depend on application validation being flawless. Even malformed proposals that reference the same canonical burn derive the same slot before being rejected semantically.

### Certificate and Index Persistence

- **D-11:** Publish the authoritative slot certificate and the winning transaction-hash index together in one atomic multi-key CRDT delta. Neither record may become visible without the other.
- **D-12:** Use `/cert/v2/slot/<slot_id>` for the serialized authoritative certificate and `/cert/v2/tx/<tx_hash>` for the secondary index.
- **D-13:** The transaction index value contains only the slot ID. Lookup then loads and validates the certificate and verifies that its computed slot and embedded winning transaction hash match the requested keys.
- **D-14:** Slot certificate storage is write-once. Rewriting the byte-identical certificate is idempotent success. A different certificate for an occupied slot is rejected, never overwrites the first value, and is reported as a consensus-safety conflict.
- **D-15:** v2.0 is a clean protocol-state break. Nodes fail startup with a clear error when legacy `/cert/<transaction_hash>` records are present; there is no migration, dual read, or silent mixing of namespaces.

### Certificate Lookup

- **D-16:** Expose separate lookup operations. `GetCertificateBySlotId(slot_id)` reads the authoritative slot record. `GetCertificateBySubjectHash(tx_hash)` follows the winning transaction index and verifies the loaded slot certificate.
- **D-17:** `CheckCertificateForSubject(subject)` derives the candidate's canonical slot and checks authoritative slot finality directly. It returns finalized even when the certificate's winning transaction hash differs from the candidate hash.
- **D-18:** Use typed, fail-closed lookup errors. An absent transaction index is `NotFound`; an index targeting a missing certificate or any slot/transaction mismatch is `IntegrityError`. Do not hide corruption as absence and do not scan certificates to repair a lookup.
- **D-19:** Only the certified winning transaction hash receives an index. A losing candidate hash returns `NotFound`; a caller holding the full subject can derive the slot and retrieve its winner.
- **D-20:** Existing previous-nonce and producer-UTXO consumers continue calling `GetCertificateBySubjectHash(tx_hash)`; their compatibility is provided by the verified secondary index rather than legacy storage.

### the agent's Discretion

- Exact C++ value types and error-enum names, provided they preserve the separate slot/hash APIs and the `NotFound` versus integrity-failure distinction.
- Whether the 32-byte slot ID is represented internally as a fixed-size hash type or encoded lowercase hexadecimal string at API boundaries.
- Logging structure and metric names for noncanonical input, legacy-state detection, index corruption, and conflicting write attempts.
- How implementation responsibilities are factored out of the existing large `Consensus.cpp`, provided certificate validation, storage, and lookup behavior remain as decided.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone Intent and Requirements

- `.planning/PROJECT.md` — Core finality value, brownfield constraints, clean-state decision, and existing compatibility dependencies.
- `.planning/REQUIREMENTS.md` — Phase 9 requirements SLOT-01..04, CERT-01..04, and COMP-01..02, plus milestone-wide safety boundaries.
- `.planning/ROADMAP.md` — Phase boundary, dependencies, and success criteria.

### Consensus and Certificate Model

- `src/blockchain/Consensus.hpp` — Slot handler registration, certificate APIs, state structures, certificate key constants, and extension points.
- `src/blockchain/Consensus.cpp` — Current `GetSlotKey`, `GetSubjectHash`, `SubmitCertificate`, CRDT certificate callbacks, and certificate lookup behavior.
- `src/blockchain/impl/proto/Consensus.proto` — Proposal, subject, vote, and certificate signing/storage schema. The certificate must remain bound to the complete winning proposal.
- `src/account/GeniusTransaction.hpp` — Existing normal transaction `GetSlotID()` source-address-plus-nonce semantics.

### Bridge Mint and Source-Event Identity

- `src/account/MintTransactionV2.cpp` — Existing mint slot composition and existing serialization of input `output_idx_`.
- `src/account/TransactionManager.cpp` — Slot-key handler, current synthetic burn input construction, `MintFunds`, and transaction-hash certificate consumers.
- `src/account/BridgeRelayer.cpp` — Current burn notification path that forwards transaction hash but drops event position.
- `src/account/PublicChainInputValidator.cpp` — Current receipt validation, including the gap where any matching event is accepted without binding the exact log's token, amount, and destination.
- `src/account/proto/SGTransaction.proto` — `MintTxV2` and UTXO input wire fields; the existing output index carries the receipt-local event ordinal.
- `evmrelay/include/eth/event_filter.hpp` — Defines `MatchedEvent::log_index` as block-wide, which must not be reused as the canonical receipt-local ordinal.
- `evmrelay/src/eth/bridge_event.cpp` — Existing claim key and receipt-log translation behavior; informs conversion from RPC log index to receipt-local position.

### Atomic CRDT Persistence

- `src/crdt/atomic_transaction.hpp` — Atomic multi-key delta abstraction available for certificate and index publication.
- `src/crdt/impl/atomic_transaction.cpp` — Commit behavior that combines all operations into one published delta.
- `src/crdt/globaldb/globaldb.hpp` — Batch `Put`, prefix queries, and transaction entry points available to certificate storage and legacy-state checks.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- `ConsensusManager::RegisterSlotKeyHandler`: Already lets the nonce subject deserialize an embedded transaction and delegate canonical slot derivation to `GeniusTransaction::GetSlotID()`.
- `InputUTXOInfo::output_idx_` and `SGTransaction::TransferUTXOInput.output_index`: Already serialize a stable per-source-transaction ordinal and can carry the receipt-local burn position without a new wire field.
- `GlobalDB::Put(vector<DataPair>, topics)` / `AtomicTransaction`: Already publish multiple keys in one CRDT delta and should be reused for certificate plus index.
- `GetCertificateBySubjectHash`: Existing compatibility seam used by nonce-chain and producer-certificate validation; its implementation can change without rewriting those consumers.

### Established Patterns

- Consensus dispatches subject-specific behavior through handlers keyed by subject type hash.
- `outcome::result<T>` is the repository's error propagation convention; typed lookup failures should follow it.
- Certificates currently serialize the complete proposal and votes, so no certificate signing-surface redesign is needed.
- CRDT filters validate incoming certificate payloads before callbacks. The v2 filter must cover both versioned key families and preserve atomic pair integrity.

### Integration Points

- `BridgeRelayer::OnWatchEvent` and startup catch-up must derive an absolute receipt-local log position and pass it through every mint API layer.
- `MintTransactionV2::GetSlotID()` must stop using token, amount, and destination in its preimage and return the fixed canonical slot digest.
- `ConsensusManager::GetSlotKey`, proposal arbitration, and finalized checks must use the same digest for all subjects.
- `ConsensusManager::SubmitCertificate`, `CertificateReceived`, recovery, filters, and key regexes must move from `/cert/<subject_hash>` to the paired v2 namespaces.
- Startup must query or otherwise detect legacy `/cert/<transaction_hash>` keys before consensus begins.
- `TransactionManager` previous-nonce handling and `GeniusInputValidator` producer-certificate checks must continue working through the verified transaction index.

</code_context>

<specifics>
## Specific Ideas

- Keep the readable mint resource shape visible in diagnostics even though the operational slot ID is hashed: `mint-v2:<source_chain_id>:<burn_tx_hash>:<receipt_log_index>`.
- Treat receipt finality as the point after which the receipt-local event position is immutable. A provisional block-wide RPC `logIndex` is observation metadata, not finality identity.
- A wrong destination proposal must be rejected because the source event specifies the destination; placing it in the same burn slot is additional protection, not permission for consensus to choose arbitrary payload values.

</specifics>

<deferred>
## Deferred Ideas

### Reviewed Todos (not folded)

- **bridge_race fixture — not all 11 nodes mint within the 90s race window (post-fix):** Remains assigned to archived Phase 8 timing and teardown work; it does not define Phase 9 certificate storage.
- **Bridge Startup Wiring + Mock RPC Endpoints:** Remains pending as older relayer startup and RPC-simulation work; it is outside canonical slot and certificate lookup scope.

</deferred>

---

*Phase: 09-canonical-slot-and-certificate-storage*
*Context gathered: 2026-07-22*
