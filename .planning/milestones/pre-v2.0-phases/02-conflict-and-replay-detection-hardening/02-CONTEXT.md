# Phase 2: Conflict and Replay Detection Hardening - Context

**Gathered:** 2026-05-28
**Status:** Ready for planning

## Phase Boundary

Make standalone validators process certificate-embedded transactions into their local UTXO state, so the existing double-spend and nonce replay detection infrastructure works without CRDT transaction sync. The `ConsensusCertificate` already carries the full proposal with embedded `transaction_data` — Phase 2 makes `OnConsensusCertificate` use it when the local transaction store doesn't have the tx.

## Implementation Decisions

### Certificate Fallback Deserialization
- **D-01:** In `OnConsensusCertificate`, when `GetTransactionByHash(tx_hash)` returns null (standalone validator without CRDT state), deserialize the transaction from `certificate.proposal().subject()` — which contains `NonceSubject.transaction_data` (field 5, embedded in Phase 1).
- **D-02:** The deserialization uses the same path as the handler: `DeSerializeTransaction(tx_data)` with type dispatch via `GetDeSerializers()`.

### UTXO State from Certificates
- **D-03:** After deserialization, call `ChangeTransactionState(tx, CONFIRMED)` — this triggers `ParseTransaction` → `PutUTXO`/`ConsumeUTXOs`, populating the local UTXO set (`utxo_outpoints_`).
- **D-04:** Once the UTXO set is populated, existing validation infrastructure works naturally for standalone validators:
  - `VerifyParameters` (UTXOManager.cpp:473) checks signatures, existence, state, ownership against `utxo_outpoints_`
  - `HasConfirmedInputConflict` (TransactionManager.cpp:3369) scans `tx_processed_m` for CONFIRMED entries with conflicting outpoints
  - `CheckTransactionReplayProtection` (TransactionManager.cpp:4164) verifies nonce chain via certificate lookups + `GetPeerNonce` nonce window
- **D-05:** The temp VERIFYING entry from Phase 1 (TRACK-01) is already promoted to CONFIRMED by the certificate handler — no additional promotion logic needed.

### No Separate Index Needed
- **D-06:** No separate certificate-derived outpoint or nonce index is required. The certificate path feeds the existing `tx_processed_m` and `utxo_outpoints_` — same data structures, same conflict detection code, just populated from certificates instead of CRDT transaction sync.
- **D-07:** The existing `OnConsensusCertificate` conflict resolution logic (lines 3453-3526) — `GetConflictingTransaction`, `ShouldReplaceTransaction`, FAILED transitions — applies identically.

### the agent's Discretion
- Error handling for certificate deserialization failures (malformed embedded data, type dispatching failure)
- Whether to insert the deserialized tx into `tx_processed_m` before or after calling `ChangeTransactionState(tx, CONFIRMED)`
- Logging and metrics for the certificate-based validation path

## Canonical References

### Protocol Schema
- `src/blockchain/impl/proto/Consensus.proto` — `ConsensusCertificate` (lines 105-114), `ConsensusProposal` (lines 78-86), `NonceSubject` (with `transaction_data = 5`)
- `src/account/proto/SGTransaction.proto` — Transaction types, DAGStruct

### Core Implementation Files
- `src/account/TransactionManager.cpp` — `OnConsensusCertificate()` (line 3416), `HandleNonceConsensusSubject()` (line ~3640), `HasConfirmedInputConflict()` (line 3369), `CheckTransactionReplayProtection()` (line 4164), `DeSerializeTransaction()` (line 1471)
- `src/account/UTXOManager.cpp` — `VerifyParameters()` (line 473), `PutUTXO()`, `ConsumeUTXOs()`
- `src/account/IGeniusTransactions.hpp` — `GetDeSerializers()` type dispatch, `HasUTXOParameters()`

### Planning Context
- `.planning/PROJECT.md` — Project scope, constraints, key decisions
- `.planning/REQUIREMENTS.md` — Phase 2 requirements (CONFLICT-01, NONCE-01)
- `.planning/ROADMAP.md` — Phase 2 success criteria
- `.planning/phases/01-core-embedded-transaction-validation-path/01-CONTEXT.md` — Phase 1 decisions (embedded tx, ChangeTransactionState lifecycle)
- `.planning/phases/01-core-embedded-transaction-validation-path/01-RESEARCH.md` — Phase 1 research findings

## Existing Code Insights

### Reusable Assets
- `DeSerializeTransaction()` — already deserializes from embedded bytes using type dispatch; reuse for certificate path
- `ChangeTransactionState(tx, CONFIRMED)` — already handles ParseTransaction + UTXO updates + nonce tracking
- `GetTransactionByHash()` — existing lookup; the fallback path fires when this returns null
- `ConsensusCertificate.proposal()` — proto field 8 carries the full proposal with embedded transaction_data

### Established Patterns
- Certificate handler pattern: `OnConsensusCertificate` receives tx_hash, looks up tx, confirms, resolves conflicts
- Deserialization pattern (Phase 1): empty check → sanitization → `DeSerializeTransaction` → hash bind → validate
- State transition pattern: `ChangeTransactionState` for all tx_processed_m mutations

### Integration Points
- `ConsensusManager::HandleCertificate()` → dispatches to `TransactionManager::OnConsensusCertificate()`
- Certificate CRDT replication via `GlobalDB` — certificates arrive at all validators
- `VerifyParameters` called from `CheckTransactionTypeRules` at line 3954 in the handler
- `HasConfirmedInputConflict` called from handler at line 3883

## Specific Ideas

The certificate already carries the full serialized transaction through the chain: `ConsensusCertificate.proposal.subject` → `NonceSubject.transaction_data`. The data is already broadcast to every validator via CRDT gossip. Phase 1 embedded it; Phase 2 uses it on the certificate path.

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 2-Conflict and Replay Detection Hardening*
*Context gathered: 2026-05-28*
