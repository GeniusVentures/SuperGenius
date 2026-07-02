# Domain Pitfalls

**Domain:** Embedding transaction data in consensus proposals (block-lattice blockchain)
**Researched:** 2026-05-27
**Overall confidence:** HIGH (verified against actual source code paths)

---

## Critical Pitfalls

Mistakes that cause rewrites or major security issues.

### Pitfall 1: Deserialization of Untrusted Bytes from PubSub

**What goes wrong:** The `DeSerializeTransaction` path (line 1291) calls `DeSerializeDAGStruct` → protobuf `ParseFromArray` → type-specific deserializer → inner protobuf `ParseFromArray`. All of this runs on bytes arriving via PubSub (`OnConsensusMessage` at Consensus.cpp line 2437) — an untrusted source where any peer can inject arbitrary payloads. Currently, transaction bytes come from the local CRDT datastore (locally replicated, assumed well-formed). After the change, a malicious peer can embed crafted protobuf bytes that trigger memory exhaustion (massive repeated `repeated` fields), stack overflow (deeply nested variants), or undefined behavior on malformed data.

**Why it happens:** The deserialization path has no size limits, no recursion guards, and no sanitization layer. The `DeSerializeDAGStruct` wrapper at `IGeniusTransactions.cpp:7` uses `ParseFromArray` with no bounds checking on `data.size()`. Each type-specific deserializer (e.g., `TransferTransaction::DeSerializeByteVector` at line 62) does its own inner protobuf parse.

**Consequences:** Node crash (DoS), memory exhaustion (OOM), or silent data corruption. A single malicious proposal can crash every validator that receives it.

**Prevention:** Add a sanitization sandwich *before* deserialization runs:
- **Size cap:** Enforce a hard `max_transaction_data_bytes` limit (e.g., 64 KB) before any protobuf parse. A UTXO tx with >100 inputs is pathological.
- **Integrity check:** Verify that `hash(embedded bytes) == subject.tx_hash` *before* deserializing. This rejects mismatched payloads without hitting the parser.
- **Parse in sandbox:** Use a `std::nothrow` allocation check or a bounded arena allocator for the protobuf parse. Reject and return `Check::Reject` on any parse failure — do NOT crash.
- **Add `DeSerializeTransactionSafe`:** A new method that wraps `DeSerializeTransaction` with pre-checks and returns `outcome::failure` on any protobuf parse failure rather than allowing the deserializer's `std::cerr << "Failed"` fallthrough (which leaves partially-initialized objects).

**Detection:**
- Fuzz the `OnConsensusMessage` handler with malformed `ConsensusMessage` → `ConsensusProposal` → `ConsensusSubject.payload` bytes
- Monitor proposal sizes on PubSub; alert if any proposal payload exceeds the expected tx size + commitment + witness
- Add a metric for `deferred_due_to_parse_failure` in the subject handler

**Phase to address:** Phase 1 (core validation path) — must be addressed alongside the embedded-data change, not after.

---

### Pitfall 2: Transaction-Not-in-`tx_processed_m` Causes Pending on First Proposal

**What goes wrong:** `HandleNonceConsensusSubject` (line 3656) looks up the transaction in `tx_processed_m` via `GetTransactionPath(tx_hash)`. If not found (line 3662), it returns `Check::Pending` — meaning the validator defers, not rejects. On a non-destination peer that deserializes from embedded bytes, the deserialized `IGeniusTransactions` object is a stack-local temporary that is never inserted into `tx_processed_m`. The second time the subject arrives (e.g., via vote bundle), the lookup still fails, and the handler returns `Pending` again. The validator **never** reaches `Check::Approve`.

**Why it happens:** The `tx_processed_m` map is populated by the CRDT ingestion path (`DeSerializeAndTrack`, lines ~3040-3120) and the local transaction creation path (`TransferFunds`, `MintFunds`). There is no code path to insert a transaction deserialized from embedded proposal bytes into this tracking map.

**Consequences:** Even after the protocol change, non-destination peers still return `Check::Pending` — the entire goal of the fix is defeated. The voting pool does not expand.

**Prevention:** Restructure `HandleNonceConsensusSubject` so that:
1. Deserialize from embedded bytes (with Pitfall 1 protections)
2. Insert the deserialized transaction into `tx_processed_m` as a **temporary tracking entry** (status = `VERIFYING`) so all validation checks that dereference `tracked_tx` work
3. Run validation
4. If validation fails, remove the temporary entry; if it passes, leave it (or migrate to a proper tracked state)
5. On certificate arrival (`OnConsensusCertificate`), promote the entry to `CONFIRMED`

**Critical constraint:** The temporary entry MUST NOT be persisted to CRDT. The non-destination peer only validates and votes — it does not replicate the transaction data. Use a separate `externally_validated_` map or a flag in the existing `TrackedTransaction` struct to distinguish CRDT-sourced from proposal-sourced entries.

**Detection:**
- Test: set up a 3-peer network (Genesis, destination, standalone). Have Genesis propose a transaction. Verify the standalone peer receives the proposal via PubSub, deserializes, and casts an `Approve` vote.
- Metric: count `Check::Approve` returns from `HandleNonceConsensusSubject` on nodes where `full_node_m == false`.

**Phase to address:** Phase 1 (core validation path) — this is the central mechanism change.

---

### Pitfall 3: Double-Spend Gap — Standalone Validator Has No Local UTXO State

**What goes wrong:** `HasConfirmedInputConflict` (line 3365) iterates `tx_processed_m` looking for CONFIRMED transactions with overlapping UTXO inputs. On a standalone validator that only receives proposals, `tx_processed_m` contains no entries except the transactions it validates from proposals. A malicious proposer submits two conflicting proposals in rapid succession: the standalone validator sees Proposal A first (approves), then sees Proposal B (checks for conflicts, finds nothing in its empty `tx_processed_m`, also approves). The destination peer, which has its full UTXO state, correctly rejects one.

**Why it happens:** The conflict detection is purely local — it depends on the validator having seen and tracked the conflicting transaction. A validator that has never ingested the chain's UTXO history via CRDT cannot detect conflicts against historical transactions. This is the fundamental tension of the fix: the validator gains the ability to validate the *current* proposal, but lacks the *historical context* to detect conflicts.

**Consequences:** A validator can be tricked into approving a double-spend. If enough validators are standalone, a double-spend certificate can be formed despite the destination peer's rejection.

**Prevention:** This is a **structural limitation**, not a simple bug. Options (trade-offs):

1. **Accept the gap (simplest, riskiest):** Document that standalone validators approve any non-conflicting-among-proposals transaction, and rely on the destination peer's certificate-check logic (`CheckCertificateForSubject`) to catch double-spends post-certificate. This works because the destination peer also votes and can reject, and the certificate requires a quorum of the total validator weight (including the destination). However, a Sybil attack on the validator set could overwhelm the destination's vote.

2. **Cross-reference certificate chain (recommended):** When checking input conflicts in `HasConfirmedInputConflict`, also query `blockchain_->GetCertificateBySubjectHash(input.txid_hash_)`. If the input was created by a certified transaction, that certificate is proof the input is valid. If the input was consumed by another certified transaction, it's a conflict. This requires the validator to track the `consumed_outpoints` from all certified proposals they've seen (not all historical transactions, just certified ones). Add a `certified_outputs_` and `certified_spends_` set to `TransactionManager` populated from `OnConsensusCertificate`.

3. **Minimum validator requirement:** Require that voting validators also run a light CRDT sync for UTXO commitments (not full transaction data, just `{outpoint → consumed_by_tx_hash}` pairs). This is architecturally more complex but gives full conflict detection.

**Recommendation:** Option 2 (cross-reference certificate chain) is the right balance. It catches conflicts from previously certified transactions without requiring full CRDT sync. The validator already has access to the certificate store via `blockchain_`.

**Detection:**
- Test: submit two conflicting transactions. Verify that a standalone validator that approved the first rejects the second.
- Metric: log when `HasConfirmedInputConflict` returns false on a standalone validator and the input's producer certificate exists (indicating the gap is being exercised).

**Phase to address:** Phase 2 (conflict detection enhancement) — requires the core validation path (Phase 1) to be working first.

---

### Pitfall 4: Nonce Replay Protection Depends on Local CRDT State

**What goes wrong:** `CheckTransactionReplayProtection` (line 4031) calls `account_m->GetPeerNonce(tx.GetSrcAddress())` which reads from `confirmed_nonces_` (GeniusAccount.cpp line 948) — a map populated by the CRDT ingestion path when certificates are processed. On a standalone validator that has never processed a certificate for `tx.GetSrcAddress()`, this returns `outcome::failure(std::errc::invalid_argument)`. The check at line 4078 treats this as "no confirmed nonce" and returns `true` (accepts!). The validator approves *any* nonce value for a previously unseen peer.

Additionally, the `GetCertificateBySubjectHash(previous_hash)` call at line 4051 requires blockchain certificate state the validator may not have (e.g., if it joined after the previous certificate was issued).

**Why it happens:** The nonce protection is designed for full nodes that ingest all certificates via CRDT. Standalone validators lack this state.

**Consequences:** A proposer can replay a previously-confirmed transaction with the same nonce and a different tx_hash. The standalone validator, having no confirmed nonce record, accepts it. The `previous_hash` chain check can also be bypassed if the validator doesn't have the previous certificate.

**Prevention:**

1. **Lazy nonce fetch from certificate store:** When `GetPeerNonce` returns error, fall back to scanning the local certificate store (`blockchain_->GetCertificateBySubjectHash`) for the most recent certificate with `account_id == tx.GetSrcAddress()`. Extract the nonce from that certificate's `NonceSubject`. This works because certificates are propagated via PubSub and stored locally regardless of CRDT ingestion.

2. **Embed confirmed nonce in NonceSubject:** Add a `uint64 confirmed_nonce` field to the protobuf. The proposer includes the nonce of their last certified transaction. The validator verifies that `nonce == confirmed_nonce + 1`. This removes the dependency on local state entirely — the proposer provides the context.

3. **Cross-check against the certificate store for `previous_hash`:** If `GetCertificateBySubjectHash` returns error from the blockchain, check if the certificate store has it (it may be fetched lazily or arrive via PubSub retransmission). If truly missing, return `Check::Pending` — do NOT reject, because the certificate may arrive later.

**Recommendation:** Use option 2 (embed confirmed_nonce) combined with option 1 as fallback. Embedding makes the proposal self-contained and removes the validator's dependency on historical state for nonce validation. The field is trivial (8 bytes) and already exists implicitly in the DAGStruct's `previous_hash` chain — making it explicit in the subject makes validation deterministic.

**Detection:**
- Test: submit a transaction replay (same nonce as a previously certified transaction) to a standalone validator. Verify it rejects.
- Test: submit a transaction whose `previous_hash` points to a certificate the validator doesn't have. Verify the handler returns `Check::Pending` (not `Check::Reject` and not `Check::Approve`).

**Phase to address:** Phase 2 (replay protection hardening) — depends on Phase 1 completing the base validation path.

---

### Pitfall 5: Commitment-Tx-Params Binding Not Verified on Embedded-Data Path

**What goes wrong:** The `ValidateWitnessForConsensus` method calls `validator.ValidateWitness(subject, tx, params_opt.value(), blockchain_)` which **does** verify that the UTXO commitment matches the transaction parameters (GeniusInputValidator lines 141-225 reconstruct Merkle roots from commitment AND from tx params, verifying both match). However, **this binding check is only reached if `tx->HasUTXOParameters()` returns true** (line 4233). If the deserialized transaction from embedded bytes produces a `GetUTXOParametersOpt()` that returns `std::nullopt` (e.g., due to a parse error that doesn't throw but leaves the object in a default state), the validator skips the binding check and returns `WitnessValidationResult::VALID` (line 4240).

**Why it happens:** The validation chain has multiple "if-no-UTXO-params-then-VALID" early returns (lines 4233-4241, 4220-4228). These were safe when the transaction came from the CRDT (a failed parse means the CRDT data is corrupt — a different failure mode). But with embedded, untrusted bytes, a malicious proposer can craft a serialized transaction that parses into an object where `HasUTXOParameters()` returns false despite having UTXO commitment/witness in the subject — bypassing the witness check.

**Consequences:** A proposal where the subject claims UTXO transitions (has commitment + witness) but the embedded tx bytes produce no UTXO parameters would pass validation. The transaction would be "valid" with a commitment it doesn't satisfy.

**Prevention:**
- **Cross-check:** Before returning `WitnessValidationResult::VALID` for a non-UTXO tx, verify that the subject also has no `utxo_commitment` and no `utxo_witness`. If the subject claims UTXO data but the tx doesn't, return `INVALID` (mismatch).
- **Mandatory reconstruction check:** After deserializing from embedded bytes, reconstruct the `UTXOTransitionCommitment` from the tx params (`BuildUTXOTransitionCommitment`, line 4317) and compare it byte-for-byte with `nonce_subject.utxo_commitment()`. If they don't match, reject. This is a separate, independent binding check from the witness validation.
- **Add a `ValidateProposalBinding` step** that runs after deserialization and before witness validation, checking:
  1. `hash(embedded_bytes) == subject.tx_hash`
  2. `BuildCommitment(deserialized_tx) == subject.utxo_commitment` (or `!subject.has_utxo_commitment()` if tx has no UTXO params)
  3. If commitment exists but tx has no UTXO params → reject

**Detection:**
- Test: craft a proposal with valid commitment/witness but embedded bytes that deserialize to a non-UTXO transaction type. Verify the validator rejects.
- Fuzz test: generate random `transaction_data` bytes, embed in a valid NonceSubject, verify the handler rejects (never crashes, never returns `Approve`).

**Phase to address:** Phase 1 — this is a binding-check gap that must be closed in the initial implementation.

---

## Moderate Pitfalls

### Pitfall 6: PubSub Message Size Blowup Silently Drops Proposals

**What goes wrong:** The `ConsensusMessage` wraps a `ConsensusProposal` which wraps a `ConsensusSubject` whose `payload` field contains the serialized `NonceSubject` (already including `utxo_commitment` with full consumed/produced outpoint lists and `utxo_witness` with full Merkle proofs). Adding `transaction_data` — full serialized protobuf including DAGStruct + UTXOTxParams (inputs with 32B txid_hash + 4B idx + 64B sig per input, outputs with 8B amount + address + token) — can push the total message size well beyond typical PubSub limits.

A modest transfer of 10 inputs × 10 outputs: ~10KB for tx_data alone, plus commitment (20 outpoints + 10 outputs ≈ 3KB), plus witness (10 Merkle proofs ≈ 5KB), plus protobuf overhead = ~20KB per proposal. Libp2p PubSub defaults to 1MB, but some configurations/libraries enforce smaller per-message limits. If the serialized `ConsensusMessage` exceeds the PubSub transport limit, it is **silently dropped** — neither sender nor receiver get an error.

**Why it happens:** Protobuf uses length-delimited framing but the transport layer has its own limits. The `Publish` method at Consensus.cpp line 204 serializes the `ConsensusMessage` and calls the broadcaster — if the bytes exceed the limit, they simply don't appear on the receiving end.

**Consequences:** Large UTXO transactions (many inputs from many small outputs) cannot be proposed. Worse, the proposer has no feedback — it thinks the proposal was published, but no validator receives it. The proposal times out, the transaction fails, and the user has no idea why.

**Prevention:**
- **Add a size validation before publishing:** In `SubmitProposal` (Consensus.cpp line 1005), check `message.ByteSizeLong()` against a known `MAX_CONSENSUS_MESSAGE_SIZE` before calling `Publish`. Return a descriptive error to the caller so the user knows the transaction is too complex.
- **Document the transaction size budget:** From the known PubSub limit, subtract the overhead (proposal metadata, commitment, witness) and communicate the remaining budget for `transaction_data`. A 64KB hard cap on `transaction_data` (from Pitfall 1) plus PubSub limit awareness ensures no silent drops.
- **Consider streaming or chunking:** If large UTXO txs are common, split the proposal into a header (NonceSubject with hash + commitment) + separate data message. This is a protocol-level change and likely out of scope for the initial fix, but should be noted as a scaling consideration.

**Detection:**
- Add a metric `proposal_publish_failed_due_to_size` in `SubmitProposal`
- Test with a transaction that has 50+ inputs and verify the proposer receives a clear error, not a silent timeout

**Phase to address:** Phase 2 (robustness) — the core path can accept a size cap in Phase 1.

---

### Pitfall 7: Signature Check Is Necessary But Not Sufficient for Authorization

**What goes wrong:** `CheckTransactionAuthorization` (line 3964) verifies `tx.CheckSignature() || tx.CheckDAGSignatureLegacy()`. The signature covers `SerializeByteVector(dag_copy)` — the DAGStruct fields (type, previous_hash, source_addr, nonce, timestamp, uncle_hash, data_hash). However, for UTXO transactions, the **input signatures** (`InputUTXOInfo.signature_`) are verified separately in `GeniusInputValidator::ValidateWitness` (line 264-273). These verify that the transaction creator has the right to spend each input.

With embedded bytes, the proposal may carry a validly-signed DAGStruct but the input signatures could be forged, missing, or replayed from a different transaction. The DAG signature authorizes the transaction envelope, but the input signatures authorize the specific spends.

**Why it happens:** The authorization check and input signature verification happen at different points in the validation pipeline with different data dependencies. The DAG signature is checked early (line 3964), but the input signature verification depends on the witness data in the NonceSubject and runs much later (line 3761 → ValidateWitnessForConsensus → validator.ValidateWitness). If an error between these two checks causes an early return, the input signatures may never be verified.

**Consequences:** A transaction with a valid DAG signature but invalid or missing input signatures could slip through if a code change introduces an early return between `CheckTransactionAuthorization` and `ValidateWitnessForConsensus`.

**Prevention:**
- **Move input signature verification earlier:** Verify input signatures immediately after deserializing the transaction, before any other checks. This ensures they are always verified regardless of code path modifications.
- **Add a dedicated `VerifyInputAuthorization` method** that validates all input signatures against the `source_addr`. This is called right after deserialization and cannot be skipped.
- **Unit test the validation order:** Verify that if input signatures are invalid, the handler returns `Check::Reject` regardless of DAG signature validity.

**Detection:**
- Test: create a proposal with a valid DAG signature but forged input signatures. Verify rejection.
- Test: create a proposal with valid DAG signature but zero input signatures (empty signature fields). Verify rejection.

**Phase to address:** Phase 1 — this is part of the validation pipeline restructure.

---

### Pitfall 8: Timestamp Validation Is Non-Deterministic Across Validators

**What goes wrong:** `CheckTransactionTimestamp` (line 3988) uses `GetElapsedTime(ts)` and `timestamp_tolerance_m` — comparing the transaction timestamp against the validator's local clock. Different validators with different clock skews may reach different conclusions about the same transaction. One validator accepts, another rejects — creating a consensus split.

This already exists in the current code, but the expanded validator pool (from 1-2 validators per proposal to potentially many) amplifies the problem. If 40% of validators have clocks skewed > `timestamp_tolerance_m`, they reject a transaction that 60% accept, and quorum is reached but with a split vote that may confuse the certificate logic.

**Why it happens:** The timestamp tolerance is a compile-time or config-time constant, and real-world clock skew can exceed it. The `IsTimestampSane` check in `HandleProposal` (Consensus.cpp line 1147) adds a second clock-dependent check.

**Consequences:** Non-deterministic validation across the network. Proposals may fail to reach quorum because a minority of validators reject for clock reasons. This is a liveness issue, not a safety issue (a split vote is safe, but proposals timeout unnecessarily).

**Prevention:**
- **Tighten the tolerance window:** Make `timestamp_tolerance_m` significantly larger (e.g., 30 seconds instead of current) for consensus validation to account for clock skew across distributed validators.
- **Consider proposal timestamp, not transaction timestamp:** The `ConsensusProposal.timestamp` is set by the proposer and validated by `IsTimestampSane`. Use this as the authoritative timestamp and relax the transaction timestamp check (the tx timestamp was set by the client, which may have even more skew).
- **Add a `timestamp_tolerance_consensus` config:** Separate the local validation tolerance from the consensus validation tolerance.

**Detection:**
- Monitor `Check::Reject` reasons across validators for the same proposal. If some validators reject with "Timestamp out of tolerance" while others approve, the tolerance is too tight.
- This is more of a network-operational concern than a code bug.

**Phase to address:** Phase 3 (network hardening) — acceptable to ship with a generous tolerance in Phase 1 and tighten based on operational data.

---

## Minor Pitfalls

### Pitfall 9: `tx_processed_m` Memory Growth on Standalone Validators

**What goes wrong:** If standalone validators insert validated-but-not-certified transactions into `tx_processed_m` (per Pitfall 2 prevention), these entries are never cleaned up — there is no CRDT pruning path for them. Over time, `tx_processed_m` grows unboundedly.

**Prevention:** Add a periodic cleanup: after a proposal is certified (or times out), remove the temporary tracking entry. Track entries inserted from embedded proposals with a `proposal_sourced` flag and clean them up on certificate receipt or timeout.

**Phase to address:** Phase 3 (memory management) — Phase 1 can start with a simple approach and add cleanup later.

---

### Pitfall 10: Embedded Bytes and Commit/Witness Must Be Atomically Consistent

**What goes wrong:** The protobuf `NonceSubject` is constructed in `CreateNonceSubject` (Consensus.cpp line 2231). Currently it sets `tx_hash`, `utxo_commitment`, and `utxo_witness`. Adding `transaction_data` means four fields that must be mutually consistent. If the proposer's code has a race condition where the commitment is built from one version of the transaction but `transaction_data` is serialized from a different version (e.g., after a concurrent modification), the proposal is self-contradictory and will be rejected by all validators.

**Prevention:** Build all four fields (hash, tx_data, commitment, witness) from a single atomic snapshot of the transaction. Construct them in a single method call with no intervening mutable operations. Add a self-validation step in `CreateNonceSubject` that deserializes the embedded bytes and verifies hash and commitment match before publishing.

**Detection:** Add an assertion or log-warning in `SubmitProposal` if the built proposal fails the binding check (deserialize + compare).

**Phase to address:** Phase 1 — this is a proposer-side correctness concern.

---

### Pitfall 11: Legacy DAG Signature Bypass

**What goes wrong:** `CheckDAGSignatureLegacy` (line 85) serializes the DAGStruct using protobuf `SerializeToArray` and verifies the signature + hash. If a newly-deserialized-from-embedded-bytes transaction uses the legacy signature scheme but the fields (especially `data_hash`) are populated inconsistently with the new flow, the check could pass for reasons unrelated to the actual data integrity.

**Prevention:** After deserializing from embedded bytes, explicitly call `FillHash()` to recompute `data_hash` from the actual serialized content. Then run `CheckHash()` to verify the hash matches the embedded data. This catches hash mismatches regardless of signature scheme.

**Detection:** Test with transactions using both signature schemes.

**Phase to address:** Phase 1.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| Phase 1: Core validation (deserialize + basic checks) | Pitfall 1 (deserialization DoS), Pitfall 2 (tx_processed_m gap), Pitfall 5 (binding bypass) | Add sanitization sandwich before deserialization; insert deserialized tx into tracking map; add mandatory binding check |
| Phase 2: Conflict/replay detection | Pitfall 3 (double-spend gap), Pitfall 4 (nonce replay) | Cross-reference certificate chain for conflicts; embed confirmed_nonce in NonceSubject; add lazy certificate store fallback |
| Phase 3: Network hardening | Pitfall 6 (PubSub size), Pitfall 8 (timestamp non-determinism), Pitfall 9 (memory growth) | Add message size validation; relax timestamp tolerance for consensus; add tracking entry cleanup |
| Phase 1: Proposer-side | Pitfall 10 (atomic consistency) | Build all NonceSubject fields from single atomic snapshot; self-validate before publishing |

---

## What Might Have Been Missed

1. **Interaction with the proof system (`processProofs`):** The dead code at TransactionManager.cpp line 2842 (`valid_proof = true; break;`) skips C++ proof verification because CRDT resync guarantees the proof exists. If standalone validators process proposals without CRDT state, the proof verification path (currently dead) may need to be re-enabled. This was noted in the codebase concerns but its interaction with this change was not fully explored.

2. **ValidatorRegistry weight dynamics:** The quorum threshold depends on `validator_registry_->GetRegistryEpoch()` and total weight. If non-destination validators now vote where they previously couldn't, the voting weight distribution changes. A validator that votes `Reject` on a valid transaction (due to any of the pitfalls above) consumes voting capacity. The quorum logic (Consensus.cpp) should be reviewed to ensure it handles the case where some validators are "silent" (never receive the proposal at all) vs. explicitly rejecting.

3. **Replay of previously-embedded proposals:** If a proposal with embedded bytes is published, reaches quorum, and produces a certificate, the certificate is stored. A malicious peer could republish the *identical proposal* (same bytes, same signature). `CheckCertificateForSubject` at Consensus.cpp line 1217 would catch it and ignore the duplicate. But this should be verified — the check uses `subject_hash`, and with embedded bytes the subject payload is identical to the original, so the hash is identical. This works correctly but should be explicitly tested.

4. **Migration concern from CONCERNS.md:** The migration system has version-specific files with duplicated patterns. If the protobuf schema change (adding `transaction_data` to `NonceSubject`) requires a database migration, the migration chain must be tested. Since this is a clean protocol break (no backward compatibility per PROJECT.md), migration of existing certificates is not needed, but the path should be verified.

---

## Sources

- **Code review of `TransactionManager::HandleNonceConsensusSubject`** (TransactionManager.cpp lines 3640-3801): [HIGH confidence] — direct analysis of the actual validation flow.
- **Code review of `DeSerializeTransaction` / `DeSerializeDAGStruct`** (TransactionManager.cpp line 1291, IGeniusTransactions.cpp line 7): [HIGH confidence] — deserialization attack surface.
- **Code review of `GeniusInputValidator::ValidateWitness`** (InputValidators.cpp lines 86-339): [HIGH confidence] — commitment-params binding verification.
- **Code review of `CheckTransactionReplayProtection`** (TransactionManager.cpp lines 4031-4152): [HIGH confidence] — nonce and certificate dependency analysis.
- **Code review of `HasConfirmedInputConflict`** (TransactionManager.cpp lines 3365-3410): [HIGH confidence] — local-state dependency for conflict detection.
- **Code review of `Consensus.proto`** (Consensus.proto lines 57-62): [HIGH confidence] — NonceSubject protobuf structure.
- **Code review of `ConsensusManager::CreateNonceSubject`** (Consensus.cpp lines 2231-2261): [HIGH confidence] — proposal construction flow.
- **Code review of `ConsensusManager::OnConsensusMessage`** (Consensus.cpp lines 2427-2466): [HIGH confidence] — PubSub message reception path.
- **Codebase concerns analysis** (.planning/codebase/CONCERNS.md): [MEDIUM confidence] — cross-referenced concerns about proof verification, blank error codes, and fragile areas.
- **Project specification** (.planning/PROJECT.md): [HIGH confidence] — confirmed design decisions and scope.
