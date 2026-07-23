# Phase 9: SecureCRDT Layer - Context

**Gathered:** 2026-07-23
**Status:** Ready for planning

<domain>
## Phase Boundary

A layer on top of `GlobalDB`/CRDT that lets specific keys require quorum-verified signatures (via Phase 8's `MultiSig` primitive) to be trusted, using CRDT itself as the only transport for proposing values and collecting signatures — no new networking/RPC. This phase delivers the `ISignedCRDTData` interface, a static topic/key → policy registry, and the CRDT-transported propose/sign/quorum mechanics. It does NOT implement `TrustedPeerRegistry`, `BURN_BASIS_POINTS`, or migrate `ValidatorRegistry` — those are Phases 10-12.

</domain>

<decisions>
## Implementation Decisions

### Pending signature storage
- **D-01:** Partial signatures during collection are stored in CRDT itself (not in-memory per-node like `ValidatorRegistry`'s batch tracking). Each signer's signature is its own durable, replicated CRDT entry. This lets any peer observe collection progress and independently contribute a signature without a live connection to whoever proposed the value, and survives node restarts. Matches the milestone-level decision that CRDT itself carries all propose/sign/quorum messages.

### CRDT key layout
- **D-02:** For a registered value at `base_key`: the proposed/current value is stored at `base_key` directly (via `HierarchicalKey`, no extra "pending" sub-key layer — see D-04's revised finalization model, which removes the need for a separate pending vs. final key). Each signer's signature is its own entry at `base_key.ChildString("sig").ChildString(signer_address)`, using `HierarchicalKey::ChildString` (`src/crdt/hierarchical_key.hpp`/`impl/hierarchical_key.cpp`).

### Local-write self-validation gap
- **D-03:** `GlobalDB`'s filter callback (`RegisterElementFilter`) only runs on remote-originated deltas (confirmed in research: `crdt_datastore.cpp`'s `GetDeltaFromNode` only invokes the filter `if (!created_by_self)`), never on local `Put` calls. Therefore all writes to a registered key/topic MUST go through the new SecureCRDT wrapper API — never raw `GlobalDB::Put` directly — and that wrapper runs the same `EvaluateQuorum`/`VerifyPayloadSignature` checks locally before calling `GlobalDB::Put`. This is the single enforcement point for both local and remote writes; nothing bypasses it.

### Quorum finalization semantics
- **D-04 (revised from initial "promote pending to final" idea):** There is no separate "final" CRDT write and no node "declares" a value final. A registered value's trustworthiness is always re-derived by the reader: read the value at `base_key` plus all `base_key.ChildString("sig").ChildString(<address>)` entries, verify each signature over the value's canonical bytes via `MultiSig::VerifyPayloadSignature`, dedupe by signer (per Phase 8 D-04), and only treat the value as authoritative if `MultiSig::EvaluateQuorum` reports quorum met. This avoids a single point of trust and prevents a bad actor from writing a fake "final" marker — every reader independently re-verifies the real signature set against the real policy.
- **D-05 (local performance cache):** To avoid re-scanning all `sig/*` entries on every read, each node MAY maintain a local-only RocksDB cache memoizing "quorum already verified for `base_key` at value/CID `X`". This cache is purely a local optimization — never replicated, never trusted from other peers, and invalidated/recomputed whenever a new `sig/*` entry appears for that `base_key`. It does not change the trust model in D-04; it just avoids redundant re-verification work.

### Claude's Discretion
- Exact registry API shape for declaring a {topic/key pattern, signer-set source, quorum rule, `ISignedCRDTData` type} entry (e.g. a static registration macro/function called at startup) — not discussed in depth; base it on `ValidatorRegistry::RegisterFilter`'s shape but generalized to be reusable across multiple registered types, per the milestone decision to keep `ISignedCRDTData` as interface-based per-type classes (not a generic template).
- Exact serialization format per registered value (protobuf per type, following `ValidatorRegistry`'s pattern of `.SerializeToString()` into a `base::Buffer`) — left to the planner, since each `ISignedCRDTData` implementer owns its own payload codec (per Phase 8 D-01).
- Where the local RocksDB cache (D-05) lives in the source tree and its exact key scheme — not discussed, planner's discretion, though it should NOT be part of the CRDT-replicated data path.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### GlobalDB / CRDT facade to build on
- `src/crdt/globaldb/globaldb.hpp` — `Put(HierarchicalKey, Buffer, topics)` (L98-100), batch `Put` (L108-109), `Remove` (L122), `AddBroadcastTopic`/`AddTopicName`/`AddListenTopic` (L152-154), `GetMonitoredTopics` (L244), `RegisterElementFilter`/`RegisterNewElementCallback`/`RegisterDeletedElementCallback`/`Unregister*` (L167-198), `GlobalDBFilterCallback`/`GlobalDBNewElementCallback`/`GlobalDBDeletedElementCallback` type aliases (L72-74).
- `src/crdt/impl/crdt_data_filter.cpp` (L90-160) — `CRDTDataFilter::FilterElementsOnDelta`: exact filter-veto semantics. Filter returning `std::nullopt` = element kept (and `work_journal_->MarkSeen` called); filter returning any vector (even empty) = element(s) rejected/deleted from the delta BEFORE merge. This is a genuine synchronous pre-merge veto, not post-hoc logging.
- `src/crdt/impl/crdt_datastore.cpp` — `GetDeltaFromNode` (L906-928, filter only applied `if (!created_by_self)`), `MergeDataFromDelta` (L930-936), `ProcessJobIteration` (L955, runs on the DAG-sync worker thread — filter runs synchronously/inline on that thread, blocking it until the callback returns).

### Precedent to generalize from (do not modify ValidatorRegistry this phase — that's Phase 12)
- `src/blockchain/ValidatorRegistry.hpp`/`.cpp` — `RegisterFilter()` (cpp L1231-1261): registers an `ElementFilterCallback` for a key pattern + `AddListenTopic`. `FilterRegistryUpdate` (cpp L1263-1284): deserializes element, verifies, returns `std::nullopt` (accept) or empty vector (reject). `RegistryUpdateReceived` (cpp L1286-1327): post-accept side effect (updates cache, persists CID). Note: `ValidatorRegistry`'s own `VerifyUpdate` is bespoke and NOT built on `MultiSig.hpp` — Phase 9 is the first real consumer wiring `EvaluateQuorum`/`VerifyPayloadSignature` into a `GlobalDBFilterCallback`, following this shape as the template.
- `ValidatorRegistry`'s pending/batch tracking (hpp L639-643: `pending_certificate_subjects_by_base_`, `pending_batch_subject_ids_`, etc.) is purely in-memory — explicitly NOT the pattern to follow per D-01 above (this phase stores pending signatures in CRDT instead).
- Serialization precedent: `ValidatorRegistry` serializes protobuf messages via `.SerializeToString()` into a `base::Buffer` before `Put` (cpp ~L779-780); signing bytes are computed over a dedicated signing-payload sub-message (`validator::RegistrySigningPayload`, cpp L1332-1345) rather than the full stored value — useful precedent for framing propose/sign payloads.

### MultiSig primitive (Phase 8, already shipped)
- `src/multisig/MultiSig.hpp` (L33-69) — `VerifyPayloadSignature(address, signature, payload)`, `EvaluateQuorum(signer_set, threshold, collected_signatures, payload) -> QuorumResult{has_quorum, valid_unique_count}`. Pure, CRDT-agnostic, not yet wired into anything — Phase 9 is its first consumer.

### Key derivation
- `src/crdt/hierarchical_key.hpp` / `src/crdt/impl/hierarchical_key.cpp` — `ChildString(std::string_view) const` (cpp L24-33) appends a `/`-separated path component; `GetList()` (L40-49); `IsTopLevel()` (L35-38). This is the only mechanism for deriving `base_key.ChildString("sig").ChildString(address)`-style sub-keys — no wildcard/tree support beyond string concatenation.

### Build wiring
- `src/crdt/CMakeLists.txt`, `src/crdt/globaldb/CMakeLists.txt` — `crdt_globaldb` target (links `crdt_datastore`, `ipfs-pubsub`, `sgns_version`, `p2p::p2p_peer_address` PUBLIC; `crdt_graphsync_dagsyncer`, `crdt_globaldb_proto`, rocksdb datastore PRIVATE). Linking `crdt_globaldb` transitively pulls in `crdt_datastore`/`crdt_data_filter`/`crdt_callback_manager`/`crdt_set`/`crdt_heads`/`hierarchical_key` publicly.

### Project-level context
- `.planning/PROJECT.md` — v1.1 milestone Key Decisions (esp. "Propose/sign/quorum flow transported over CRDT itself... no new networking").
- `.planning/REQUIREMENTS.md` §"Milestone v1.1" — SCRDT-01, SCRDT-02, SCRDT-03, SCRDT-04 (this phase's requirements).
- `.planning/phases/08-multisig-primitive/08-CONTEXT.md` — Phase 8's locked decisions (D-01 raw bytes, D-02 account-address identity, D-03 stateless `EvaluateQuorum`, D-04 dedup-before-verify), which this phase's design must remain consistent with.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/multisig/MultiSig.hpp` — the signature/quorum primitive this phase wires into CRDT.
- `GlobalDB::RegisterElementFilter`/`RegisterNewElementCallback` — the exact mechanism for synchronous pre-merge veto and post-accept side effects.
- `HierarchicalKey::ChildString` — sub-key derivation for the `sig/<address>` layout.

### Established Patterns
- `ValidatorRegistry`'s filter-registration shape (`RegisterFilter`/`Filter*Update`/`*Received`) is the template to generalize, but this phase must NOT modify `ValidatorRegistry` itself (Phase 12's job).
- Protobuf-serialize-to-`Buffer` is the established CRDT value-storage pattern.

### Integration Points
- Depends on Phase 8's `src/multisig/MultiSig.hpp` (already shipped).
- Will be consumed by Phase 10 (`TrustedPeerRegistry`), Phase 11 (`BURN_BASIS_POINTS`), and Phase 12 (`ValidatorRegistry` migration) — the registry/interface API shape here must generalize cleanly for those three future consumers.

</code_context>

<specifics>
## Specific Ideas

The user's own mental model, refined during discussion: don't write a "final" marker to CRDT at all — a value is just data, and each reader independently decides whether to trust it by checking if it has enough valid signatures from the current authorized signer set. A local (non-replicated) RocksDB cache can memoize "I already checked quorum for this value" purely to avoid rescanning `sig/*` entries on every read — this is a performance optimization only, not part of the trust/replication model.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope. (Carried forward from Phase 8: raw-public-key signer identity, still deferred to Phase 10 if genesis-time seeding needs it.)

</deferred>

---

*Phase: 9-SecureCRDT Layer*
*Context gathered: 2026-07-23*
