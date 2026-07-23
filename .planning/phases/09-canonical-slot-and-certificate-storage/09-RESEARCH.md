---
phase: 09-canonical-slot-and-certificate-storage
status: complete
researched: 2026-07-23
---

# Phase 9 Research: Canonical Slot and Certificate Storage

## Executive Summary

Phase 9 needs one canonical slot derivation path, not a mint-only string change. The current system groups proposals with a transient `GetSlotKey()` value, persists certificates by winning transaction hash, and lets bridge discovery discard the event ordinal. A correct implementation must carry the finalized receipt-local log position from discovery into the mint input, hash a strict canonical preimage for every consensus subject, and persist a certificate plus its winning transaction index as one validated CRDT pair.

The existing certificate protobuf should remain unchanged: it already binds the complete winning proposal and votes. The storage key changes, not the signed object.

One boundary must be explicit. The current CRDT set provides atomic multi-key deltas but no distributed compare-and-swap for a logical key. Phase 9 can reject local rewrites, validate replicated certificate/index pairs, detect conflicts, and refuse corrupt lookups. It cannot by itself prove that two concurrently formed certificates will never race from empty state. Phase 10's durable one-signature-per-slot rule prevents honest validators from forming those competing certificates.

## Current Implementation Findings

### Slot derivation

- `GeniusTransaction::GetSlotID()` returns readable `source_address:nonce`.
- `MintTransactionV2::GetSlotID()` returns a readable string containing chain, token, amount, destination, and burn transaction hash, but omits the input output index.
- `TransactionManager::New()` registers the nonce slot handler. It deserializes an embedded transaction and calls `GetSlotID()`, otherwise it builds `account_id:nonce`.
- `ConsensusManager::GetSlotKey()` falls back to subject or proposal identity when derivation fails. That fallback is unsafe for canonical finality because malformed aliases can escape into distinct slots.
- Proposal arbitration uses `GetSlotKey()`, but certificate lookup does not.

### Bridge event identity

- `BridgeRelayer::OnWatchEvent()` receives `MatchedEvent::log_index`, which is documented as block-wide, then discards it.
- `BridgeCatchupWatcher` deduplicates discovered events with `std::set<std::string> seen_tx_hashes`; multiple legitimate burns in one transaction are collapsed.
- The catch-up `BurnProcessor` receives decoded values, transaction hash, and chain only.
- `GeniusNode::MintTokens()` and `TransactionManager::MintFunds()` have no event-index argument.
- `MintFunds()` creates and reserves synthetic bridge outpoint index `0`, so every burn in a source transaction currently aliases that outpoint.
- `eth::rpc::ReceiptResult` already contains both `receipt.logs` and their RPC block-wide `log_indices`. Existing `verify_receipt_log()` demonstrates the correct translation: locate the observed block-wide log index in `log_indices`, then use its zero-based distance as the receipt-local position.
- The live receipt watcher already processes a full receipt internally. The least error-prone shape is to expose a separate receipt-local ordinal on the matched event rather than reinterpret the existing block-wide field.

### Public-chain validation

- `ValidateUTXOParameters()` only requires nonempty inputs and outputs.
- `VerifyPublicChainSmartContract()` checks receipt success and accepts any log with a configured bridge address/topic.
- It does not select `InputUTXOInfo::output_idx_`, bounds-check that receipt-local position, or decode that exact log to compare token, amount, and destination with the proposed mint.
- Therefore canonical grouping must be paired with exact-log semantic validation. Grouping wrong candidates into the same slot is defense in depth, not authorization for consensus to choose event facts.

### Certificate persistence and retrieval

- `SubmitCertificate()` publishes the certificate, then writes one `/cert/<subject_hash>` value.
- `GetCertificateBySubjectHash()` reads that same key and verifies only the embedded subject hash.
- `CheckCertificateForSubject(subject)` computes the subject hash, so it cannot recognize that a losing candidate's slot is already finalized by a different winning hash.
- Existing previous-nonce and producer-UTXO checks call `GetCertificateBySubjectHash()` and must retain winner-hash semantics through an index.
- `GlobalDB::Put(vector<DataPair>, topics)` and `AtomicTransaction` can place certificate and index in one delta.
- The current CRDT element filter sees one element at a time. It cannot prove that a `/cert/v2/slot/...` element and `/cert/v2/tx/...` sibling in the same incoming delta agree. Phase 9 therefore needs a delta-level validation hook (or an equivalent pair-aware pre-merge facility), not two independent syntax filters.
- CRDT logical keys are resolved by priority and do not expose distributed compare-and-swap. Local preflight and incoming conflict checks are still required, but the plan must not describe them as a global CAS.

### Startup sequencing

`ConsensusManager::New()` currently constructs the instance and journal, subscribes to pubsub, starts the round timer, registers the certificate filter, and recovers work. Legacy `/cert/<tx_hash>` detection must run after dependencies exist but before subscription, timer, listeners, or recovery. Because `New()` returns `shared_ptr`, a clear logged protocol-state error plus `nullptr` is compatible with the existing factory contract.

## Recommended Architecture

### Canonical slot service

Create one result-returning canonical slot derivation seam used by both transaction handlers and consensus:

1. Build a canonical readable preimage.
2. Reject missing or noncanonical components.
3. SHA-256 the exact bytes.
4. Expose the lowercase 64-hex digest as the operational slot ID.

For normal nonce transactions, the preimage remains the existing canonical address-plus-nonce text. For bridge mints, it is exactly:

`mint-v2:<source_chain_id>:<burn_tx_hash>:<receipt_log_index>`

Do not include token, amount, destination, proposer, proposal ID, transaction hash, or proposer nonce. Do not fall back to proposal identity when a registered slot derivation fails.

Canonical checks should reject:

- chain IDs or numeric indexes with signs, whitespace, or leading-zero aliases;
- hashes with `0x`, uppercase characters, non-hex characters, or length other than 64;
- missing/multiple ambiguous source inputs;
- noncanonical addresses in the normal transaction preimage.

Keep readable preimages available for diagnostics, but all maps and datastore keys consume the digest.

### Receipt-local event propagation

Treat receipt-local position as a mandatory value:

`receipt watcher / catch-up RPC -> BridgeRelayer or BurnProcessor -> GeniusNode::MintTokens -> TransactionManager::MintFunds -> InputUTXOInfo::output_idx_`

The live path should add an explicit receipt-local field to `MatchedEvent`, populated while iterating `receipt.logs`. The existing `log_index` remains block-wide observation metadata.

The catch-up path should resolve each `RpcLog.log_index` against the transaction receipt's ordered `log_indices`; it must not infer ordinal from filtered `eth_getLogs` results. Deduplicate with `(tx_hash, receipt_log_index)`, not transaction hash.

`MintFunds()` must use the received index for UTXO state checks, synthetic `GeniusUTXO`, `InputUTXOInfo`, reservation, rollback, and persistence identity. A missing index should be unrepresentable at the API boundary; no overload should default it to zero.

### Exact event verification

For a public-chain mint, require exactly one external input and at least one output, use its `output_idx_` to select `receipt.logs[index]`, then verify:

- receipt status succeeds;
- index is in range;
- selected log address and topic are configured for the source chain;
- ABI decoding succeeds for the selected event version;
- decoded source chain, token, amount, and destination match the serialized mint and produced output.

All endpoints participating in the existing weighted verification independently validate the same selected receipt-local log.

### V2 certificate store

Use:

- `/cert/v2/slot/<slot_id>` -> serialized `ConsensusCertificate`
- `/cert/v2/tx/<winning_tx_hash>` -> `<slot_id>`

Before publishing a local pair:

1. Validate the certificate.
2. Derive its canonical slot and winning subject hash.
3. Validate canonical key syntax.
4. Read the existing slot record.
5. Return idempotent success for byte-identical data.
6. Return a typed conflict/integrity error for differing data.
7. Write the slot and index records in one `GlobalDB::Put(vector<DataPair>)` delta.

Register pair-aware pre-merge validation for replicated v2 certificate deltas. A valid pair must contain exactly the derived slot key and winning-hash index with the expected value. Reject the entire pair for malformed payloads, missing siblings, key/value mismatches, unexpected rewrites, or invalid certificates. The callback path should process only the authoritative slot record and treat the transaction index as lookup metadata.

### Lookup contract

- `GetCertificateBySlotId(slot_id)` validates the slot format, loads the certificate, re-derives its slot, and rejects mismatches.
- `GetCertificateBySubjectHash(tx_hash)` loads the index, validates its slot ID, calls slot lookup, and verifies the certificate's winning subject hash equals the requested hash.
- `CheckCertificateForSubject(subject)` derives the subject slot directly and performs authoritative slot lookup, so a losing candidate observes the winning finality.
- `NotFound` is reserved for absent requested slot/index. Missing certificate behind an existing index, malformed values, or mismatches return `IntegrityError`.
- Existing previous-nonce and producer-UTXO consumers remain unchanged at the call site.

### Clean protocol-state break

At startup, query `/cert/` and reject any record that is not in `/cert/v2/slot/` or `/cert/v2/tx/`. Log the first offending key and the required clean-state action, return `nullptr`, and perform no pubsub subscription, timer start, filter registration, listener addition, or work recovery.

## Architectural Responsibility Map

| Capability | Owning tier | Primary files | Why |
|------------|-------------|---------------|-----|
| Canonical slot preimage and digest | Transaction/domain + consensus boundary | `GeniusTransaction.hpp`, `MintTransactionV2.*`, `Consensus.*`, `TransactionManager.cpp` | Domain objects know immutable identity fields; consensus enforces one result-returning path |
| Receipt-local ordinal production | EVM observation tier | `evmrelay/.../event_filter.*`, `bridge_catchup_watcher.*` | Only the receipt observer has ordered receipt context |
| Event identity propagation | Account orchestration tier | `BridgeRelayer.*`, `GeniusNode.*`, `TransactionManager.*` | These APIs carry source facts without redefining them |
| Exact burn-log validation | Public-chain validation tier | `PublicChainInputValidator.*` | Untrusted external receipt and proposal fields cross here |
| Atomic certificate/index persistence | Consensus persistence tier | `Consensus.*`, `crdt_data_filter.*`, `GlobalDB` | Storage owns pair atomicity and replicated-data validation |
| Compatibility lookup | Consensus API tier | `Consensus.*`, `Blockchain.*` | Consumers request by hash but must not interpret storage layout |
| Legacy-state rejection | Consensus construction boundary | `ConsensusManager::New()` | Protocol compatibility must be decided before background side effects |

## Security Analysis

### Trust boundaries

- EVM RPC receipt/log data is untrusted until independently validated.
- Embedded consensus transactions and their canonical fields are untrusted network input.
- CRDT certificate and index deltas are replicated input and may be malformed, partial, or conflicting.
- Legacy local datastore content is incompatible protocol state, not trusted migration input.

### Primary threats

- **Spoofing/tampering:** formatted aliases or candidate-controlled mint fields create multiple slots for one burn.
- **Tampering:** a valid certificate is stored beneath the wrong slot or transaction index.
- **Tampering:** a partial or mismatched replicated pair becomes visible.
- **Denial of service:** malformed indexes induce scans or repeated fallback work.
- **Repudiation/diagnostics:** conflicting certificates are silently overwritten without identifying slot and proposals.

Mitigations are strict canonical parsing, exact receipt-log verification, pair-aware delta filtering, no scan repair, typed integrity errors, idempotent byte replay only, and actionable conflict logs.

No package installation is required.

## Implementation Pitfalls

1. Do not hash the current mint string and call it complete; it includes candidate-controlled fields and omits the receipt-local index.
2. Do not use RPC `logIndex` directly. It is block-wide and may change after re-inclusion.
3. Do not deduplicate catch-up logs by transaction hash.
4. Do not keep synthetic outpoint index `0` in any reservation, rollback, or persistence path.
5. Do not let `GetSlotKey()` fall back to proposal ID after a canonical handler reports invalid input.
6. Do not implement certificate/index validation as unrelated element filters; pair consistency needs delta context.
7. Do not report index corruption as `NotFound`.
8. Do not scan slot certificates to repair a broken index.
9. Do not run the legacy-state check after consensus background side effects start.
10. Do not claim write-once preflight is distributed CAS; Phase 10 supplies formation safety.

## Validation Architecture

### Test infrastructure

| Property | Value |
|----------|-------|
| Framework | GoogleTest through repository `addtest(...)` CMake helper |
| Build tree | `build/OSX/Release` |
| Test binaries | `build/OSX/Release/test_bin/<target>` |
| Focused build | `cmake --build build/OSX/Release --target <target> -j2` |
| Focused run | `build/OSX/Release/test_bin/<target> --gtest_brief=1` |
| Full relevant run | `ctest --test-dir build/OSX/Release -R '(consensus_slot_key|consensus_certificate_store|bridge_relayer|bridge_event_identity|public_chain_mint_validation|certificate_compatibility)' --output-on-failure` |

### Requirement-to-test map

| Requirement | Behavior | Test level | Target | Status before Phase 9 |
|-------------|----------|------------|--------|------------------------|
| SLOT-01, SLOT-02 | Every subject uses deterministic hashed slot; normal address+nonce contenders collide | unit | `consensus_slot_key_test` | Existing target, assertions need replacement |
| SLOT-03, SLOT-04 | Mint slot uses chain/hash/receipt index only | unit | `consensus_slot_key_test` | Existing target, missing receipt-index and adversarial-field cases |
| CERT-01..04 | Complete certificate stored by slot with verified winning-hash index | integration/unit with CRDT fixture | `consensus_certificate_store_test` | Missing Wave 0 target |
| COMP-01 | Previous-nonce and producer lookups work through v2 index | integration/unit with CRDT fixture | `certificate_compatibility_test` | Missing Wave 0 target |
| COMP-02 | Legacy certificate key prevents startup before side effects | integration/unit with CRDT fixture | `consensus_certificate_store_test` | Missing Wave 0 case |
| Context D-01..D-05 | Multiple burns survive discovery and exact receipt log is verified | unit/integration | `bridge_event_identity_test`, `public_chain_mint_validation_test` | Missing Wave 0 targets; existing relayer/catch-up tests provide fixtures to reuse |

### Wave 0 gaps

- Add active `consensus_certificate_store_test` target with `base_crdt_test` fixture support rather than relying on the currently commented stale `consensus_certificate_test`.
- Add `bridge_event_identity_test` for receipt-local ordinal derivation, multi-burn deduplication, and mandatory API propagation.
- Add `public_chain_mint_validation_test` with deterministic mocked receipt responses containing multiple logs.
- Add `certificate_compatibility_test` for hash-index consumers and losing-candidate slot lookup.

The existing `consensus_slot_key_test` can be updated in place and requires no scaffold.

## Sources

- `.planning/phases/09-canonical-slot-and-certificate-storage/09-CONTEXT.md`
- `.planning/REQUIREMENTS.md`
- `.planning/ROADMAP.md`
- `src/blockchain/Consensus.{hpp,cpp}`
- `src/account/GeniusTransaction.hpp`
- `src/account/MintTransactionV2.{hpp,cpp}`
- `src/account/TransactionManager.{hpp,cpp}`
- `src/account/BridgeRelayer.{hpp,cpp}`
- `src/account/GeniusNode.{hpp,cpp}`
- `src/account/PublicChainInputValidator.{hpp,cpp}`
- `src/watcher/impl/bridge_catchup_watcher.{hpp,cpp}`
- `evmrelay/include/eth/event_filter.hpp`
- `evmrelay/src/eth/event_filter.cpp`
- `evmrelay/src/eth/bridge_event.cpp`
- `evmrelay/src/eth/json_rpc.cpp`
- `src/crdt/atomic_transaction.hpp`
- `src/crdt/impl/atomic_transaction.cpp`
- `src/crdt/impl/crdt_data_filter.cpp`
- `src/crdt/globaldb/globaldb.{hpp,cpp}`
- `test/src/blockchain/consensus_slot_key_test.cpp`
- `test/src/blockchain/consensus_certificate_test.cpp`

