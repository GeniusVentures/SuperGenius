# Feature Landscape: Consensus Voting Decentralization via Embedded Transaction Data

**Domain:** Block-lattice consensus voting with CRDT-based data distribution
**Researched:** 2026-05-27
**Source code:** SuperGenius C++17 codebase, TransactionManager.cpp, Consensus.proto, SGTransaction.proto

---

## Table Stakes

Validation checks that every peer MUST perform when receiving a transaction embedded in a NonceSubject. Missing any of these = broken consensus security.

| # | Feature / Check | Why Expected | Complexity | Notes |
|---|----------------|--------------|------------|-------|
| **F1** | **Protobuf deserialization from embedded bytes** | The transaction arrives as `bytes transaction_data` in NonceSubject. Must deserialize at least the DAGStruct to extract type, then dispatch to type-specific deserializer for full transaction reconstruction. | Medium | Uses existing `DeSerializeDAGStruct()` and registered `deserializers_map`. New failure mode: rejected proposals due to corrupt/unparseable data. Must short-circuit to `Check::Reject` (not `Check::Pending`) — there's nothing to wait for. |
| **F2** | **Structural well-formed check** | Verifies the transaction is parseable and self-consistent: hash matches content (`CheckHash()`), source address is non-empty, timestamp is non-zero, transaction type is known. | Low | Existing code in `CheckTransactionWellFormed()`. Independent of tx source (CRDT or embedded). Block-lattice design makes this especially important since each account-chain has its own hash chain — broken hash = broken replay protection. |
| **F3** | **Signature authorization** | The proof that the account holder authorized this transaction. Signature covers the DAGStruct (includes hash, previous hash, nonce, timestamp, type, data_hash). Must verify `CheckSignature()` or legacy `CheckDAGSignatureLegacy()`. | Low | Existing code in `CheckTransactionAuthorization()`. The signature is in `DAGStruct.signature` and the source address (`DAGStruct.source_addr`) is the public key. No CRDT dependency. |
| **F4** | **Timestamp tolerance** | Prevents time-drift attacks and stale transaction replay. Current envelope: ±5 minutes (300,000 ms) from validator's local clock. | Low | Existing code in `CheckTransactionTimestamp()`. Configurable via `timestamp_tolerance_m`. Important: non-destination validators have their own clock — clock skew across the validator set must be within tolerance. |
| **F5** | **Replay protection (nonce chain)** | The core block-lattice anti-replay mechanism. Validates: (a) nonce > 0 requires valid previous_hash pointing to a known certificate, (b) previous_subject.account_id == tx source address, (c) previous_nonce + 1 == current nonce, (d) tx nonce > confirmed_nonce for that address, (e) tx nonce within window (`nonce_window_m`), (f) all intermediate nonces exist and aren't FAILED. | High | Most complex check. Requires blockchain state: `GetCertificateBySubjectHash()`, `GetPeerNonce()`, `GetTrackedTxByNonceAndAddress()`. **Key insight for embedded-tx model:** non-destination validators may not have intermediate transactions locally tracked, but they CAN resolve them from certificates on the blockchain. The nonce chain validation against certificates is the authoritative check. |
| **F6** | **Transaction type rules (UTXO parameters)** | Each tx type has structural rules. UTXO-carrying transactions (TransferTx, MintTxV2, MigrationTx, EscrowTx, EscrowReleaseTx) must have non-empty inputs and outputs, valid chain-specific ownership, proper UTXO manager parameters. | Medium | Chain-id-specific validation via `GetInputValidator(chain_id).ValidateUTXOParameters()`. For embedded txs: the UTXOTxParams are in the serialized transaction bytes themselves. The validator reconstructs them from deserialization. The `UTXOManager` reference is still needed for ownership verification on Genius-native chains. |
| **F7** | **Witness validation (UTXO commitment + Merkle proofs)** | Binds the transaction's UTXO inputs to the proposer's claimed UTXO state. Validator: (a) extracts UTXO parameters from the embedded transaction, (b) reconstructs the UTXO commitment from those parameters using the same hashing scheme, (c) compares reconstructed root against the `utxo_commitment` in NonceSubject, (d) verifies Merkle inclusion proofs from `utxo_witness`. | High | **This is the security-critical differentiator.** The commitment in NonceSubject is a cryptographic promise from the proposer about the UTXO state. The validator must prove that promise matches the transaction data. Currently the validator reads tx params from locally-tracked transaction; with embedding, both the tx params AND the commitment arrive together — the validator verifies self-consistency between them. Chain-id-specific via `IInputValidator::ValidateWitness()`. Genius-native chains require full witness data (`RequiresConsensusUTXOData() == true`); public-chain validators do not. |
| **F8** | **Input conflict detection (double-spend)** | Checks whether any input UTXO is already spent by a CONFIRMED transaction on this validator's local state. Critical for preventing double-spends across the network. | Medium | `HasConfirmedInputConflict()` iterates `tx_processed_m` for CONFIRMED txs with overlapping outpoints. **Tension with embedded-tx model:** non-destination validators lack locally-tracked transactions for the source address. However, they only need to check against CONFIRMED transactions (which they track via certificates), not pending ones. The fix should ensure that certificate-processing logic tracks confirmed UTXOs for all addresses, not just local ones. |
| **F9** | **Nonce consistency with subject** | The nonce in the embedded DAGStruct must match the `nonce` field in NonceSubject. Mismatch = proposal contains inconsistent data (either tx or subject is wrong). | Low | Existing check at line 3720 of TransactionManager.cpp. With embedded tx, both values come from the same proposer's message — still must cross-verify. |
| **F10** | **Source address consistency with subject** | The `source_addr` in DAGStruct must match `subject.account_id()`. Prevents cross-account proposal forgery. | Low | Existing check at line 3730. Same as nonce — cross-verify two fields from same message. |
| **F11** | **Transaction status check (not FAILED)** | Rejects proposals for transactions already known to be invalid. Prevents spam proposals for doomed transactions. | Low | For non-destination validators seeing the tx for the first time: they have no prior status. This check is a no-op for first-seen txs (no prior entry in `tx_processed_m`). But if they encounter the same proposal twice, they must reject re-proposals of already-rejected transactions. |
| **F12** | **Migration eligibility verification** | Special check for MigrationTx: validates that the source address is on the migration allowlist and has sufficient allocated amount. | Medium | Requires access to global database (`globaldb_m->GetDataStore()`). This is an opt-in check — failure means `Check::Pending` (not `Check::Reject`) because the validator may lack local state. This pattern is important: unknown-but-potentially-valid conditions should return Pending, not Reject. |

### Validation Dependency Chain

The checks above have a strict ordering dependency. The following diagram shows what must execute before what:

```
Deserialization (F1)
    │
    ▼
Well-formed check (F2) ─── if fail → Reject immediately
    │
    ├─► Signature authorization (F3) ─── if fail → Reject immediately
    │
    ├─► Timestamp tolerance (F4) ─── if fail → Reject immediately
    │
    ├─► Source address consistency (F10) ─── if fail → Reject immediately
    │
    ├─► Nonce consistency (F9) ─── if fail → Reject immediately
    │
    ├─► Replay protection (F5) ─── if fail → Reject immediately
    │         │
    │         └── depends on: blockchain certificates, confirmed nonce map, tracked txs
    │
    ├─► Transaction status (F11) ─── if FAILED → Reject
    │
    ├─► Input conflict (F8) ─── if conflict → Reject immediately
    │         │
    │         └── depends on: local CONFIRMED tx tracking
    │
    ├─► Type rules (F6) ─── if fail → Reject immediately
    │         │
    │         └── depends on: transaction type, chain-specific validator
    │
    ├─► Witness validation (F7) ─── if fail → Reject immediately
    │         │
    │         ├── depends on: F6 (UTXO params), F3 (signature)
    │         └── depends on: subject's utxo_commitment + utxo_witness
    │
    └─► Migration eligibility (F12) ─── if fail → Pending (not Reject)
              │
              └── only executed for MigrationTx type
```

**Key insight:** The "hard" checks (F2-F10) all produce `Reject` on failure — the validator definitively knows the transaction is invalid from the embedded data alone. Only F12 produces `Pending` (may lack local eligibility data). This is deliberate: embedded-tx validation should be deterministic and self-contained for security checks, while leaving escape hatches for state-dependent policy checks.

---

## Differentiators

Features that set this implementation apart from alternative approaches. Not required for functionality, but define the quality and character of the solution.

| # | Feature | Value Proposition | Complexity | Notes |
|---|---------|-------------------|------------|-------|
| **D1** | **Zero-CRDT validation path** | Non-destination validators vote without storing transaction data in their CRDT. The transaction lives only in the consensus message and validation happens in-flight. Eliminates the data distribution bottleneck — currently only Genesis/full-nodes/destination get the data via CRDT; now anyone with consensus membership can vote. | Medium | This is the core purpose of the fix. The implementation is simple (add bytes field, deserialize at handler), but the validation implications are deep: every check in the table-stakes list must be verified to work without local CRDT state. |
| **D2** | **Commitment self-verification** | The validator reconstructs the UTXO commitment from the embedded transaction's UTXO parameters and compares against the proposer's claimed commitment in NonceSubject. This provides cryptographic binding proof without trusting the proposer. If they mismatch, the proposal is provably invalid. | Medium | `BuildUTXOTransitionCommitment()` already exists on the proposer side. The validator-side reconstruction uses the same algorithm but with the embedded tx params as input (not the CRDT-tracked tx). The comparison: `reconstructed_root == nonce_subject.utxo_commitment().*_root`. |
| **D3** | **Single-message self-containment** | The full consensus validation data (transaction bytes + UTXO commitment + UTXO witness) arrives in one PubSub message. No multi-round protocol needed. Validator either accepts or rejects based on the single message + its local blockchain state. | Low | Contrast with systems like Narwhal/Tusk where data availability and consensus are separated into distinct sub-protocols. Self-containment simplifies the validator's state machine but increases message size. |
| **D4** | **Deterministic rejection criteria** | Every Reject decision is reproducible. A second validator receiving the same proposal (same bytes, same commitment, same witness) MUST reach the same Reject/Accept decision (assuming equivalent blockchain state). This is critical for consensus safety — if validators disagree, votes split and quorum stalls. | Low | Pending is NOT deterministic ("I don't have the data to decide") — but Pending is the safe default. The fix reduces Pending cases by making good proposals self-validating. |
| **D5** | **Chain-agnostic input validation** | The witness/UTXO validation is routed through `IInputValidator` strategy, which is chain-id-aware. Genius-native chains use full Merkle proof verification; external/public chains use lightweight reference validation. The embedded-tx model works identically regardless of chain. | Low | Already exists in the codebase. The fix doesn't change the validator strategy — it changes where the validator gets the tx from. |
| **D6** | **Progressive trust model** | Validators start skeptical (embedded tx could be garbage) and build confidence through deterministic checks. Each passed check increases confidence; the first failure produces a definitive Reject. No "partial trust" states — the validator either has enough information to decide or it doesn't. | Low | This is a design philosophy, not a code feature. It matters for the implementation: checks must be ordered from cheapest/fastest (deserialization, well-formed) to most expensive (witness Merkle proof verification). |

---

## Anti-Features

Features to explicitly NOT build. These represent design choices that would harm the system or add complexity without value.

| # | Anti-Feature | Why Avoid | What to Do Instead |
|---|-------------|-----------|-------------------|
| **AF1** | **Full CRDT sync for validators** | Would require every validator to subscribe to every account's CRDT topic and replicate transaction data — defeats the purpose of the fix and introduces unbounded storage growth. | Validators need only blockchain (certificate) state and consensus messages. Transaction storage remains the responsibility of Genesis/full-nodes/destination peers. |
| **AF2** | **Transaction storage on non-destination validators** | Writing every voted-on transaction to local CRDT would balloon storage for validators who exist purely to vote. The block-lattice design intentionally separates data distribution (CRDT) from consensus participation (voting). | Validators cache tx data only for the duration of pending proposals (in-memory, bounded). After vote, discard. |
| **AF3** | **Backward compatibility shim** | Legacy peers send NonceSubject without `transaction_data` bytes. Building a dual-path handler (check CRDT first, fall back to embedded) adds branching complexity, test surface area, and potential for subtle bugs where one path validates differently from the other. | Clean protocol break. Old peers simply can't participate in new consensus rounds. Per PROJECT.md Key Decisions: "Clean break, no backward compat — simpler implementation." |
| **AF4** | **Partial transaction embedding** | Sending only a subset of transaction fields (e.g., just the DAGStruct header) might seem bandwidth-efficient, but UTXO transactions need the full UTXOTxParams to reconstruct the commitment for witness validation. Without full params, the commitment binding can't be verified. | Embed the complete serialized `TransferTx`/`MintTxV2`/`MigrationTx`/etc. protobuf message — not the DAGStruct alone, not a hand-picked subset. The cost of extra bytes is negligible compared to the security cost of unverifiable data. |
| **AF5** | **Data availability network / erasure coding** | Some systems (Ethereum Danksharding, Celestia) separate data availability from consensus using erasure coding. For SuperGenius, this is massive over-engineering — the transaction data is small (hundreds of bytes) and PubSub already delivers it. | Embedded bytes in the consensus message. PubSub handles distribution. No separate data layer. |
| **AF6** | **Lazy validation (vote first, validate later)** | Some consensus systems allow validators to vote first and validate later (optimistic voting). In a financial blockchain with UTXO semantics, this is dangerous — a validator could approve a double-spend before verifying inputs. | Always validate fully before voting. The embedded-tx model enables fast validation because all data is in-hand; there's no excuse to skip checks. |
| **AF7** | **Proposer-selected validator subset** | Restricting which validators vote on which proposals (leader-selected committees) would trade decentralization for throughput. The whole point of this fix is MORE validators voting, not fewer. | All active validators in the consensus membership receive all proposals and vote independently. The PubSub topic-based filtering already handles which validators see which proposal types. |
| **AF8** | **Separate validation for embedded vs CRDT-sourced transactions** | Maintaining two validation code paths (one for txs from CRDT, one for txs from embedded bytes) creates a maintenance burden and risk of divergent behavior. | Single validation pipeline. The only difference is how the `std::shared_ptr<IGeniusTransactions>` is obtained: from CRDT lookup OR from embedded bytes deserialization. After that, `ValidateTransactionForConsensus()` and all downstream checks operate identically. |

---

## Feature Interaction Matrix

How features and anti-features interact across the system:

| | F1 Deserialize | F2 Well-formed | F5 Replay Prot | F7 Witness | F8 Conflict | F12 Migration |
|---|---|---|---|---|---|---|
| **AF1 Full CRDT sync** | Made unnecessary | Same | Can't help (need certs anyway) | Irrelevant | REQUIRES local tx tracking | REQUIRES global DB |
| **AF4 Partial embed** | Can't deserialize full tx | Can't check extra fields | Works for nonce only | **CANNOT verify commitment** | Can't extract UTXO params | Can't check amount |
| **AF6 Lazy validation** | Bypasses | Bypasses | Bypasses — dangerous | Bypasses — dangerous | Bypasses — dangerous | Bypasses |

**Critical interaction:** AF4 (partial embedding) breaks F7 (witness validation). The UTXO commitment in NonceSubject binds to the full set of UTXO parameters. Without full parameters, the validator can't reconstruct the commitment — making the witness data unverifiable and the entire consensus security dependent on proposer honesty.

---

## MVP Recommendation

**Prioritize these features for the first implementation iteration:**

1. **F1 (Deserialization from embedded bytes)** — Foundational. Nothing works without this.
2. **F2 (Well-formed check)** — First validation gate, cheap to run.
3. **F3 (Signature authorization)** — Core security check, no CRDT dependency.
4. **F9 + F10 (Nonce + address consistency with subject)** — Cross-field integrity, cheap.
5. **F4 (Timestamp tolerance)** — Basic liveness guard.
6. **F5 (Replay protection)** — Complex but essential for block-lattice security.
7. **F6 (Type rules)** — UTXO parameter validation.
8. **F7 (Witness validation)** — Commitment reconstruction and verification.
9. **F8 (Input conflict)** — Double-spend prevention.

**Defer:**
- **F12 (Migration eligibility):** Can return `Check::Pending` for non-destination peers initially, since migration is a bounded lifecycle event. The existing pattern of returning Pending when eligibility can't be determined is acceptable.
- **D2 (Commitment self-verification):** This is implicitly part of F7 — the same code path. Don't build a separate feature.

**Anti-feature most likely to creep in:**
- **AF6 (Lazy validation).** Pressure to optimize voting latency may tempt developers to vote before full validation. Guard against this by ensuring the return path from `HandleNonceConsensusSubject` only reaches `Check::Approve` after all checks pass. The existing code already does this.

---

## Sources

| Source | Confidence | Details |
|--------|-----------|---------|
| SuperGenius codebase: `TransactionManager.cpp` lines 3640-4399 | HIGH | Primary source — the entire validation flow in `HandleNonceConsensusSubject` and `ValidateTransactionForConsensus` |
| SuperGenius codebase: `Consensus.proto` | HIGH | NonceSubject, ConsensusSubject, UTXOTransitionCommitment, UTXOWitness message definitions |
| SuperGenius codebase: `SGTransaction.proto` | HIGH | DAGStruct, TransferTx, MintTxV2, MigrationTx, EscrowTx, EscrowReleaseTx definitions |
| SuperGenius codebase: `IGeniusTransactions.hpp` | HIGH | Transaction interface, deserialization registry, signature/hash/UTXO accessors |
| SuperGenius codebase: `UTXOStructs.hpp` | HIGH | InputUTXOInfo, OutputDestInfo, UTXOTxParameters types |
| SuperGenius codebase: `InputValidators.hpp` | HIGH | IInputValidator strategy interface, GeniusInputValidator, PublicChainInputValidator |
| SuperGenius codebase: `TransactionManager.hpp` | HIGH | Class structure, timestamp_tolerance default (300s), nonce_window_m |
| `.planning/PROJECT.md` | HIGH | Project scope, requirements, out-of-scope items, key decisions |
| Nano Protocol Documentation (docs.nano.org) | MEDIUM | Block-lattice design principles: vote-by-hash, block cementing, account-chains. Verified against Nano node source on GitHub. |
