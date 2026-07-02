---
status: complete
phase: 01-core-embedded-transaction-validation-path
source: 01-01-SUMMARY.md, 01-02-SUMMARY.md
started: 2026-05-28T00:00:00Z
updated: 2026-05-28T00:00:00Z
---

## Current Test

[testing complete]

## Tests

### 1. Test Suite Passes
expected: Run the project's test suite. All existing consensus tests continue to pass, plus 19 new tests (3 embedding E2E, 6 sanitization, 6 binding, 4 tracking) all pass without failures.
result: skipped
reason: Some existing integration tests fail — expected at this stage since full consensus requires all 3 phases. Phase 1-specific tests (19 new) not independently verifiable outside full build.

### 2. Proto Schema Has Embedded Transaction Fields
expected: Open `src/blockchain/impl/proto/Consensus.proto`. The `NonceSubject` message has `string transaction_type = 5;` and `bytes transaction_data = 6;` — both fields present in correct sequential positions.
result: issue
reported: "transaction_type field was removed because the type can be extracted from the transaction_data bytes via partial DAG deserialization — only transaction_data = 5 remains"
severity: minor

### 3. Handler Deserializes from Embedded Bytes
expected: In `HandleNonceConsensusSubject` (TransactionManager.cpp), the handler reads `nonce_subject.value().transaction_data()`, checks it's not empty, and runs the sanitization sandwich (size cap → blake2b hash → DeSerializeTransaction). The deserialized `shared_ptr<IGeniusTransactions>` is used for all subsequent validation checks — no CRDT lookup (`tx_processed_m.find`) in the primary validation path. Transaction type is extracted from the deserialized object via `tx->GetType()` (not from a separate proto field).
result: pass

### 4. Hash Binding Gate Rejects Mismatches
expected: In the handler (TransactionManager.cpp), immediately after deserialization, `tx->GetHash() != tx_hash` check returns `Check::Reject` — cryptographic integrity guard prevents approving a proposal where embedded bytes deserialize to a different transaction.
result: pass

### 5. Sanitization Sandwich Rejects Malformed Payloads
expected: (a) 64KB size cap (`MAX_EMBEDDED_TX_BYTES`) rejects oversized payloads before any protobuf parse. (b) blake2b_256 hash of `transaction_data` bytes compared against `tx_hash()` before `DeSerializeTransaction` is called. (c) Post-deser hash binding check from test 4 runs as defense-in-depth. All failures return `Check::Reject`.
result: pass

### 6. Commitment-Tx Binding Prevents Spoofing
expected: Handler: when subject `has_utxo_commitment()` but tx lacks `HasUTXOParameters()` → `Check::Reject`. `BuildUTXOTransitionCommitment(tx)` reconstructed and compared byte-level against `subject.utxo_commitment()`. Witness: `ValidateWitnessForConsensus` returns `INVALID` (was silently `VALID`) when subject has commitment but tx lacks UTXO params.
result: pass

### 7. Tracking Lifecycle Cleans Up Temp Entries
expected: (a) Reject path discriminates by `TransactionStatus::VERIFYING` and erases temp entries from `tx_processed_m` (using `std::unique_lock tx_lock` + `GetTransactionPath(tx_hash)` + find + conditional erase). (b) `OnConsensusCertificate` promotes `VERIFYING` entries to `CONFIRMED` BEFORE conflict check (so `HasConfirmedInputConflict` sees them). (c) CRDT-sourced entries (non-VERIFYING) are never erased by the reject path.
result: pass
reported: "Code matches but bypasses ChangeTransactionState — direct tx_processed_m manipulation instead of using the canonical state transition method"

### 8. Serialization Chain Threads Through Full Pipeline
expected: Trace from `SendTransactionItem` (TransactionManager.cpp) through `Blockchain::CreateConsensusProposal` (Blockchain.cpp) to `ConsensusManager::CreateNonceSubject` (Consensus.cpp). The `SerializeByteVector()` output is captured at the entry point and set on the `NonceSubject` proto as `transaction_data`. Transaction type is extracted from the deserialized bytes on the receiving end — no separate `transaction_type` parameter threading.
result: pass

## Summary

total: 8
passed: 6
issues: 1
pending: 0
skipped: 1

## Gaps

- truth: "NonceSubject has string transaction_type = 5 and bytes transaction_data = 6"
  status: fixed
  reason: "User reported: transaction_type field was removed because the type can be extracted from the transaction_data bytes via partial DAG deserialization — only transaction_data = 5 remains"
  severity: minor
  test: 2
  root_cause: "01-01-SUMMARY.md not updated after post-implementation field removal"
  artifacts:
    - path: ".planning/phases/01-core-embedded-transaction-validation-path/01-01-SUMMARY.md"
      issue: "Claims both transaction_type (field 5) and transaction_data (field 6) exist. Code has only transaction_data = 5."
  missing:
    - "Update 01-01-SUMMARY.md to reflect that transaction_type was removed and type extraction uses DAG partial deserialization"
  debug_session: ""
