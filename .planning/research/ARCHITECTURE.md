# Architecture: Transaction-Embedded Consensus Proposals

**Domain:** C++ block-lattice blockchain, consensus voting with CRDT persistence
**Researched:** 2026-05-27
**Confidence:** HIGH (verified against actual codebase)

## Executive Summary

The current architecture couples consensus validation to CRDT state: peers must have a transaction in their local CRDT store (`tx_processed_m`) to validate a proposal. The fix embeds the full serialized transaction in the `NonceSubject` protobuf, allowing any validator to deserialize, validate, and vote on a proposal from the proposal bytes alone — no CRDT dependency for the core validation path.

The change is surgical: one new field in protobuf, one new parameter threaded through two functions, and one code path in the handler replaced. The consensus mechanics (proposal creation, voting, quorum, certificate) are untouched — only the validation data source changes.

## Current Architecture (Before Fix)

### System Context

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         TransactionManager (Account Layer)                    │
│  - Creates/validates transactions                                            │
│  - Stores tx in CRDT at /tx/{tx_hash}                                        │
│  - Creates consensus proposals via Blockchain facade                         │
│  - Registers HandleNonceConsensusSubject as subject handler                  │
└───────────────────────────────┬──────────────────────────────────────────────┘
                                │
                                ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                            Blockchain (Facade Layer)                          │
│  - CreateConsensusProposal(src_addr, nonce, tx_hash, comm, witness)          │
│  - Threads through to ConsensusManager                                       │
│  - SubmitProposal()                                                          │
└───────────────────────────────┬──────────────────────────────────────────────┘
                                │
                                ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                      ConsensusManager (Core Consensus)                        │
│  - CreateNonceSubject(account_id, nonce, tx_hash, comm, witness)             │
│  - CreateProposal(subject, ...) → ConsensusProposal                          │
│  - SubmitProposal() → PubSub broadcast                                       │
│  - OnConsensusMessage() → HandleProposal() → subject_handler → vote          │
└───────────────────────────────┬──────────────────────────────────────────────┘
                                │ PubSub
                                ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                          Peer ConsensusManager                                │
│  HandleProposal(proposal)                                                    │
│    → DecodeNonceSubject() → extracts tx_hash                                 │
│    → subject_handler = HandleNonceConsensusSubject(subject)                  │
│    → Checks result: Approve/Pending/Reject                                  │
│    → Approve → ContinueProposalAfterSubject() → cast vote                   │
└──────────────────────────────────────────────────────────────────────────────┘
                                │
                                ▼ (if Pending: AddPendingProposal)
                      ┌─────────────────────────┐
                      │ Awaiting CRDT sync of tx │
                      └─────────────────────────┘
```

### Current Data Flow: Proposal Creation

```
TransactionManager::SendTransactionItem()
  1. [CRDT write] GlobalDB::Put(tx_key, serialized_tx_bytes)
  2. [CRDT commit] AtomicTransaction::Commit()
  3. For each committed tx:
     a. BuildUTXOTransitionCommitment(tx) → utxo_commitment (optional)
     b. BuildUTXOWitness(tx) → utxo_witness (optional)
     c. Blockchain::CreateConsensusProposal(
          src_addr, nonce, tx_hash,  // only a reference
          utxo_commitment, utxo_witness)
        │
        ├→ Blockchain::CreateConsensusNonceSubject(...)
        │     └→ ConsensusManager::CreateNonceSubject(
        │          account_id, nonce, tx_hash, // Only the hash
        │          utxo_commitment, utxo_witness)
        │          │
        │          ├→ NonceSubject.nonce = nonce
        │          ├→ NonceSubject.tx_hash = tx_hash (bytes)
        │          ├→ NonceSubject.utxo_commitment = ... (optional)
        │          └→ NonceSubject.utxo_witness = ... (optional)
        │
        ├→ ConsensusManager::CreateProposal(nonce_subject, ...)
        │     └→ ConsensusProposal with signed subject
        │
        └→ Write to CRDT: /prop/{proposal_hash}
  4. Blockchain::SubmitProposal(proposal)
     └→ ConsensusManager::SubmitProposal()
         └→ Publish(ConsensusMessage{proposal}) → PubSub broadcast
```

### Current Data Flow: Proposal Reception and Validation

```
Peer receives pubsub message
  →
ConsensusManager::OnConsensusMessage()
  → Parse ConsensusMessage
  → Dispatch: HandleProposal(proposal)
      │
      ├→ CheckProposal() — structural validation
      ├→ IsTimestampSane() — time window check
      ├→ LoadRegistry() — validator registry check
      ├→ CheckSubject() — subject structure check
      ├→ CheckCertificateForSubject() — duplicate check
      │
      ├→ Lookup subject_handler by subject_type_hash
      │   └→ TransactionManager::HandleNonceConsensusSubject(subject)
      │       │
      │       ├→ DecodeNonceSubject(subject) → NonceSubject { tx_hash, nonce, ... }
      │       ├→ GetTransactionPath(tx_hash) → "/bc-X/tx/{tx_hash}"
      │       ├→ Lookup tx_processed_m[key]  ⚠️ CRDT DEPENDENCY
      │       │   └→ If NOT FOUND → return Check::Pending
      │       ├→ Validate nonce match vs tracked tx
      │       ├→ Validate account_id match
      │       ├→ Check status ≠ FAILED
      │       ├→ HasConfirmedInputConflict() — scans tx_processed_m
      │       ├→ ValidateWitnessForConsensus() — binding proof check
      │       ├→ Migration eligibility (optional)
      │       ├→ ValidateTransactionForConsensus(tracked_tx):
      │       │   ├→ CheckTransactionWellFormed
      │       │   ├→ CheckTransactionAuthorization
      │       │   ├→ CheckTransactionTimestamp
      │       │   ├→ CheckTransactionReplayProtection
      │       │   └→ CheckTransactionTypeRules
      │       └→ return Check::Approve
      │
      ├→ If Approve: ContinueProposalAfterSubject()
      │   ├→ GetSlotKey() → "account_id:nonce" (from DecodeNonceSubject)
      │   ├→ IsBetterProposal() — uses tx_hash from NonceSubject
      │   ├→ Slot arbitration
      │   └→ CreateVote() → SubmitVote() → PubSub
      │
      ├→ If Pending: AddPendingProposal() — queued for retry
      └→ If Reject: dropped
```

### Component Boundaries (Current)

| Component | Responsibility | Key Methods |
|-----------|---------------|-------------|
| **TransactionManager** | Transaction lifecycle, CRDT storage, consensus subject handler | `SendTransactionItem()`, `HandleNonceConsensusSubject()`, `BuildUTXOTransitionCommitment()`, `BuildUTXOWitness()`, `ValidateTransactionForConsensus()`, `ValidateWitnessForConsensus()`, `HasConfirmedInputConflict()` |
| **Blockchain** | Facade: delegates to ConsensusManager | `CreateConsensusProposal()`, `CreateConsensusNonceSubject()`, `SubmitProposal()` |
| **ConsensusManager** | Subject creation, proposal/vote/cert lifecycle, PubSub dispatch | `CreateNonceSubject()`, `CreateProposal()`, `SubmitProposal()`, `HandleProposal()`, `ContinueProposalAfterSubject()`, `DecodeNonceSubject()`, `GetSlotKey()`, `IsBetterProposal()` |
| **Consensus.proto** | Wire format | `NonceSubject` (nonce, tx_hash), `ConsensusSubject`, `ConsensusProposal`, etc. |
| **SGTransaction.proto** | Transaction types | `TransferTx`, `MintTx`, `EscrowTx`, etc. with `DAGStruct` wrapper |
| **IGeniusTransactions** | Transaction interface, serialization/deserialization | `SerializeByteVector()`, `DeSerializeTransaction()`, `GetHash()`, `CheckHash()` |

### The CRDT Dependency Chain

The current `HandleNonceConsensusSubject` requires the transaction to be in `tx_processed_m`. This map is populated by:

1. **Outgoing path:** `SendTransactionItem()` writes to CRDT and immediately populates `tx_processed_m` locally
2. **Incoming path:** CRDT sync → `FilterTransaction()` callback → `AddTransactionToProcessedMaps()` → populates `tx_processed_m`

Peers that are neither the Genesis node (which creates and submits the proposal), full nodes (which sync all CRDT data), nor the destination address (which subscribes to relevant topics) never receive the transaction CRDT entry. Their `tx_processed_m` lacks the entry, so `HandleProposal()` gets `Check::Pending` and the proposal is queued indefinitely.

## New Architecture (After Fix)

### Core Design Decision

Add `bytes transaction_data = 5` to the `NonceSubject` protobuf. At proposal creation, serialize the full transaction and embed it. At validation, deserialize from the subject instead of looking up in CRDT. The `tx_hash` field already in `NonceSubject` serves as the binding commitment — validators compute the hash from the deserialized transaction and compare against `tx_hash`.

### System Changes

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         TransactionManager (Account Layer)                    │
│  SendTransactionItem():                                                      │
│    ↳ ADD: serialized_tx = tx->SerializeByteVector()    ← NEW                │
│    ↳ PASS to CreateConsensusProposal via new parameter   ← NEW               │
│  HandleNonceConsensusSubject(subject):                                        │
│    ↳ ADD: tx_data = nonce_subject.transaction_data()    ← NEW                │
│    ↳ ADD: Deserialize tx from tx_data                    ← NEW                │
│    ↳ ADD: Verify GetHash() matches tx_hash               ← NEW (binding)     │
│    ↳ KEEP: All existing validation (now on deserialized tx)                   │
│    ↳ REMOVE: CRDT lookup via GetTransactionPath(tx_hash) ← GONE              │
│    ↳ KEEP: HasConfirmedInputConflict (needs local tx_processed_m)            │
└───────────────────────────────┬──────────────────────────────────────────────┘
                                │
                                ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                            Blockchain (Facade Layer)                          │
│  CreateConsensusProposal():                                                   │
│    ↳ ADD: transaction_data parameter                          ← NEW           │
│    ↳ Thread through to CreateConsensusNonceSubject()                         │
└───────────────────────────────┬──────────────────────────────────────────────┘
                                │
                                ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                      ConsensusManager (Core Consensus)                        │
│  CreateNonceSubject():                                                        │
│    ↳ ADD: transaction_data parameter                          ← NEW           │
│    ↳ SET: payload.set_transaction_data(transaction_data)      ← NEW           │
│  DecodeNonceSubject():                                                        │
│    ↳ NO CHANGE (protobuf ParseFromString auto-handles new field)             │
│  HandleProposal(), ContinueProposalAfterSubject(), GetSlotKey(),              │
│  IsBetterProposal(), voting:                                                  │
│    ↳ NO CHANGE (these use NonceSubject fields already present)               │
└──────────────────────────────────────────────────────────────────────────────┘
```

### New Data Flow: Proposal Creation

```
TransactionManager::SendTransactionItem()
  1. [CRDT write] GlobalDB::Put(tx_key, serialized_tx_bytes)  ← UNCHANGED
  2. [CRDT commit] AtomicTransaction::Commit()                 ← UNCHANGED
  3. For each committed tx:
     a. BuildUTXOTransitionCommitment(tx) → utxo_commitment    ← UNCHANGED
     b. BuildUTXOWitness(tx) → utxo_witness                    ← UNCHANGED
     c. serialized_tx = tx->SerializeByteVector()              ← NEW
     d. Blockchain::CreateConsensusProposal(
          src_addr, nonce, tx_hash,
          serialized_tx,              ← NEW: full bytes
          utxo_commitment, utxo_witness)
        │
        ├→ CreateConsensusNonceSubject(..., serialized_tx, ...)
        │     └→ CreateNonceSubject(
        │          account_id, nonce, tx_hash,
        │          serialized_tx,       ← NEW: embedded in subject
        │          utxo_commitment, utxo_witness)
        │          │
        │          ├→ NonceSubject.nonce = nonce
        │          ├→ NonceSubject.tx_hash = tx_hash (bytes)
        │          ├→ NonceSubject.transaction_data = serialized_tx  ← NEW
        │          ├→ NonceSubject.utxo_commitment = ...
        │          └→ NonceSubject.utxo_witness = ...
        │
        ├→ CreateProposal(nonce_subject, ...)                  ← UNCHANGED
        └→ Write to CRDT: /prop/{proposal_hash}
  4. SubmitProposal(proposal) → PubSub                         ← UNCHANGED
```

### New Data Flow: Proposal Reception and Validation

```
Peer receives pubsub message
  →
ConsensusManager::OnConsensusMessage()                        ← UNCHANGED
  → Parse ConsensusMessage
  → Dispatch: HandleProposal(proposal)
      │
      ├→ CheckProposal(), IsTimestampSane(), LoadRegistry(),
      │   CheckSubject(), CheckCertificateForSubject()        ← ALL UNCHANGED
      │
      ├→ Lookup subject_handler by subject_type_hash
      │   └→ TransactionManager::HandleNonceConsensusSubject(subject)
      │       │
      │       ├→ DecodeNonceSubject(subject) → NonceSubject
      │       │   Now includes: { nonce, tx_hash, transaction_data, ... }
      │       │
      │       ├→ transaction_data = nonce_subject.transaction_data()     ← NEW
      │       ├→ Deserialize: tx = DeSerializeTransaction(transaction_data) ← NEW
      │       │   Uses existing IGeniusTransactions deserializer registry
      │       │
      │       ├→ Binding check: tx->GetHash() == nonce_subject.tx_hash() ← NEW
      │       │   (Reject if mismatch — proposal is malformed)
      │       │
      │       ├→ Validate nonce match: tx->GetNonce() == nonce_subject.nonce() ← ADAPTED
      │       ├→ Validate account_id: tx->GetSrcAddress() == subject.account_id()
      │       ├→ CheckTransactionWellFormed(*tx) — includes CheckHash()
      │       ├→ CheckTransactionAuthorization(*tx) — signature verification
      │       ├→ CheckTransactionTimestamp(*tx)
      │       ├→ CheckTransactionReplayProtection(*tx)
      │       ├→ HasConfirmedInputConflict(tx) — needs local tx_processed_m ← KEPT
      │       ├→ ValidateWitnessForConsensus(subject, tx) — commitment binding
      │       ├→ Migration eligibility (optional, needs GlobalDB) ← KEPT
      │       ├→ CheckTransactionTypeRules(tx)
      │       └→ return Check::Approve
      │
      ├→ If Approve: ContinueProposalAfterSubject()             ← UNCHANGED
      │   ├→ GetSlotKey() → "account_id:nonce"                 ← UNCHANGED
      │   ├→ IsBetterProposal() → uses tx_hash from NonceSubject ← UNCHANGED
      │   ├→ Slot arbitration
      │   └→ CreateVote() → SubmitVote() → PubSub
      │
      ├→ If Pending: AddPendingProposal()  ← STILL POSSIBLE
      │   (e.g., migration eligibility needs local data, input conflict scan)
      └→ If Reject: dropped
```

### Component Boundaries (After Fix)

| Component | What Changes | What Stays |
|-----------|-------------|------------|
| **TransactionManager** | `SendTransactionItem()`: adds `SerializeByteVector()` call, passes to `CreateConsensusProposal()`. `HandleNonceConsensusSubject()`: replaces CRDT lookup with deserialize-from-subject, adds binding hash check, keeps all validation. | `BuildUTXOTransitionCommitment()`, `BuildUTXOWitness()`, `ValidateTransactionForConsensus()`, `ValidateWitnessForConsensus()`, `HasConfirmedInputConflict()`, all validation check methods remain unchanged. |
| **Blockchain** | `CreateConsensusProposal()`: new `transaction_data` parameter threaded through. `CreateConsensusNonceSubject()`: same. | `SubmitProposal()`, `RegisterSubjectHandler()`, `TryResumeProposal()`, `CheckCertificate()` — all unchanged. |
| **ConsensusManager** | `CreateNonceSubject()`: new `transaction_data` parameter, sets it on `NonceSubject`. | `CreateProposal()`, `SubmitProposal()`, `HandleProposal()`, `ContinueProposalAfterSubject()`, `DecodeNonceSubject()`, `GetSlotKey()`, `IsBetterProposal()` — all unchanged. The consensus layer is unaware of transaction semantics. |
| **Consensus.proto** | `NonceSubject`: add `bytes transaction_data = 5`. | All other messages unchanged. `ConsensusSubject.payload` grows (includes transaction_data in serialized NonceSubject), but this is transparent to consensus logic. |
| **SGTransaction.proto** | No changes. | Transaction schemas unchanged. |
| **IGeniusTransactions** | No changes. | `SerializeByteVector()`, `DeSerializeTransaction()`, `GetHash()`, `CheckHash()` all work as-is. |
| **CRDT/GlobalDB** | No changes. | CRDT writes and sync continue as before. Transaction CRDT entries still exist and are used by other code paths. |

### Subject ID and Hashing Impact

`ComputeSubjectId()` hashes the full serialized `ConsensusSubject`, which includes `payload = subject_type_hash || serialized(NonceSubject)`. Since `NonceSubject` now contains `transaction_data`, the serialized payload is larger and the resulting subject ID is different. This is correct and desired: a proposal with embedded transaction data has a different subject ID than a proposal without, preventing collision with old-protocol proposals.

Impact on other functions:
- `GetSlotKey()` — unchanged. Slot key is `account_id + ":" + nonce`, derived from `DecodeNonceSubject()`. Both old and new NonceSubject have the same nonce for the same transaction.
- `IsBetterProposal()` — unchanged. Compares `tx_hash` from `DecodeNonceSubject()`, which is identical for the same transaction regardless of whether it's embedded.
- `CheckCertificateForSubject()` — unchanged. Uses subject hash, which naturally differs between old and new proposals.
- `Certificate` reconstruction — unchanged. Certificate carries the proposal, which carries the subject with embedded data.

### Serialization Path Detail

The transaction serialization uses the existing `IGeniusTransactions::SerializeByteVector()` chain:

```cpp
// Serialization (proposal creation side):
// tx->SerializeByteVector() returns vector<uint8_t>
// This is the full binary blob written to CRDT at /tx/{tx_hash}
// Concrete implementations (TransferTransaction, MintTransaction, etc.)
// each implement SerializeByteVector(DAGStruct)
// Example: TransferTransaction serializes TransferTx protobuf

// Deserialization (validation side):
// IGeniusTransactions::DeSerializeTransaction(const base::Buffer &tx_data)
//   1. Parses DAGStruct from front of buffer
//   2. Looks up type-specific deserializer by dag.type()
//   3. Calls deserializer function which reconstructs concrete transaction
```

The deserialized transaction object is a full `shared_ptr<IGeniusTransactions>` — identical to what would be fetched from CRDT. All validation methods that accept `const std::shared_ptr<IGeniusTransactions> &` work unchanged.

### Validation Steps That Still Need Local State

Even with embedded transaction data, two validation checks require local node state:

1. **`HasConfirmedInputConflict()`** — scans `tx_processed_m` for confirmed transactions that consume the same UTXO outpoints. This is a double-spend check against local knowledge. If the local `tx_processed_m` is incomplete, this could produce false negatives, but never false positives (it only detects conflicts it actually knows about). The quorum mechanism ensures enough validators collectively have sufficient coverage.

2. **Migration Eligibility Check** — reads `MigrationAllowList` from GlobalDB. This check is specific to migration transactions and returns `Check::Pending` when the local data is unavailable. This is acceptable — the proposal will remain pending until CRDT sync provides the necessary data (or be rejected by other validators).

These checks do NOT prevent voting — a peer that can't fully determine input conflicts or migration eligibility can still cast a vote if the basic validation passes. The `HandleNonceConsensusSubject` handler currently returns `Check::Pending` only for missing transaction and migration eligibility; after the fix, only migration eligibility can trigger `Pending`. Input conflicts return `Reject` (when detected), not `Pending`.

## Build Order (Dependency Graph)

The changes have a strict dependency order:

```
Phase 1: Protobuf Schema
  └→ Add transaction_data field to NonceSubject
      (Zero code dependency — just schema change)
      
Phase 2: ConsensusManager Signature Changes
  ├→ PREREQ: Phase 1 (protobuf generated code must include new field)
  ├→ CreateNonceSubject() adds transaction_data parameter
  ├→ Sets transaction_data on NonceSubject proto
  └→ TEST: consensus_subject_test.cpp callsites updated
      
Phase 3: Blockchain Facade Threading
  ├→ PREREQ: Phase 2 (CreateNonceSubject signature changed)
  ├→ CreateConsensusProposal() adds transaction_data parameter
  ├→ CreateConsensusNonceSubject() adds transaction_data parameter
  └→ TEST: consensus_certificate_test.cpp callsites updated

Phase 4: TransactionManager Proposal Creation Side
  ├→ PREREQ: Phase 3 (Blockchain facade signature changed)
  ├→ SendTransactionItem(): serialize tx, pass to CreateConsensusProposal()
  └→ No test changes needed (serialization is transparent to output)

Phase 5: TransactionManager Validation Side (THE CORE CHANGE)
  ├→ PREREQ: Phase 1 (protobuf field exists)
  ├→ HandleNonceConsensusSubject():
  │   ├→ Extract transaction_data from NonceSubject
  │   ├→ DeSerializeTransaction(transaction_data)
  │   ├→ Binding hash check: GetHash() == tx_hash
  │   ├→ Remove CRDT lookup (GetTransactionPath + tx_processed_m find)
  │   └→ All validation calls adapted to use deserialized tx
  └→ TEST: New integration tests for non-CRDT peer voting

Phase 6: Test Suite Update
  ├→ PREREQ: All above
  ├→ Update consensus_certificate_test.cpp (8 call sites of CreateNonceSubject)
  ├→ Update consensus_subject_test.cpp (4 call sites)
  ├→ Add tests: validator without CRDT tx can approve/vote
  ├→ Add tests: binding hash mismatch → reject
  └→ Verify existing tests pass
```

### Why This Order

1. Protobuf first — generated code is a compile-time dependency for everything below it.
2. ConsensusManager next — it's the lowest layer, depended on by both creation and validation paths. Signature changes here cascade upward.
3. Blockchain facade third — threading the new parameter upward through the thin delegation layer.
4. Proposal creation side fourth — can be tested independently (verify that serialized tx survives round-trip: serialize → embed → extract → deserialize → compare).
5. Validation side fifth — the actual behavioral change. Depends on creation side working (can create proposals with embedded data) before testing reception.
6. Tests last — need the full pipeline working.

**Practical note:** Phases 2-4 can be developed together (they're mechanical threading changes). Phase 5 is the behaviorally sensitive change and should get focused review. Phases 1-4 together form the "infrastructure" that Phase 5 builds on.

## Scalability Considerations

| Concern | Assessment |
|---------|-----------|
| **PubSub message size** | Transactions are ~0.5-5 KB serialized (DAG struct + type-specific params). The NonceSubject gains this payload. PubSub messages were already carrying commitment/witness data; this is incremental. For a 1KB tx, the overhead is ~1KB per proposal. |
| **Deserialization cost per peer** | Every peer deserializes the transaction once at proposal reception. This is the same cost CRDT peers were already paying (on CRDT receipt). Non-CRDT peers now pay this cost too — zero-sum shift. |
| **Memory pressure** | The deserialized `shared_ptr<IGeniusTransactions>` is temporary (lives for the duration of `HandleNonceConsensusSubject`). CRDT peers also held it in `tx_processed_m` anyway. |
| **Bandwidth multiplication** | A 10-validator network now sends ~10 copies of the transaction (one per proposal to each peer) vs ~2-3 copies before (CRDT sync). This is inherent to the decentralization goal — each validator needs the data to validate. |

## Architecture Invariants Preserved

- **Consensus pubsub topic** — `consensus-channel-{topic}` unchanged, message format unchanged (ConsensusMessage wrapping Proposal wrapping Subject)
- **Vote/certificate mechanics** — untouched. Votes and certificates are constructed identically.
- **CRDT persistence** — transactions still written to CRDT at `/tx/{tx_hash}`. The CRDT path is not removed, only bypassed in consensus validation.
- **Existing validation logic** — all `Check*` methods, `BuildUTXOTransitionCommitment`, `BuildUTXOWitness`, `ValidateWitnessForConsensus` remain identical.
- **Slot arbitration** — `GetSlotKey()` returns `account_id:nonce` as before. `IsBetterProposal()` compares `tx_hash` as before.
- **Subject handler registration** — `RegisterSubjectHandler(NONCE_SUBJECT_TYPE, handler)` unchanged.
- **Certificate creation** — `CreateCertificate()`, `ValidateCertificate()`, `CheckCertificateForSubject()` unchanged.
- **Pending proposal retry** — `AddPendingProposal()`, `ResumeProposalHandling()` unchanged (still needed for migration eligibility checks).

## Sources

- `src/blockchain/Consensus.hpp` (interface: 695 lines)
- `src/blockchain/Consensus.cpp` (implementations: 2762 lines, verified lines 2231-2261 CreateNonceSubject, 1131-1286 HandleProposal, 460-559 ContinueProposalAfterSubject, 2052-2089 GetSlotKey/IsBetterProposal, 2091-2105 ComputeSubjectId, 2181-2194 DecodeNonceSubject)
- `src/blockchain/impl/proto/Consensus.proto` (122 lines, NonceSubject definition)
- `src/account/proto/SGTransaction.proto` (135 lines, transaction types)
- `src/account/TransactionManager.hpp` (682 lines, interface)
- `src/account/TransactionManager.cpp` (verification of: line 1110-1187 SendTransactionItem, 3640-3802 HandleNonceConsensusSubject, 3839-3907 ValidateTransactionForConsensus, 4191-4250 ValidateWitnessForConsensus, 3365-3410 HasConfirmedInputConflict)
- `src/account/IGeniusTransactions.hpp` (165 lines, serialization interface)
- `src/blockchain/impl/Blockchain.cpp` (verification: lines 1690-1749 CreateConsensusProposal/SubmitProposal)
- `.planning/PROJECT.md` (requirements and context)
- `.planning/codebase/ARCHITECTURE.md` (system overview)
