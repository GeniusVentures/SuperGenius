---
phase: 09-canonical-slot-and-certificate-storage
created: 2026-07-23
---

# Phase 9 Repository Pattern Map

## Outcome and error handling

- Public operations return `outcome::result<T>`.
- Repository-specific failures use a scoped enum plus `OUTCOME_HPP_DECLARE_ERROR_2`.
- Use a dedicated certificate lookup/storage error enum so callers can distinguish `NotFound`, `IntegrityError`, `Conflict`, and invalid canonical input.
- Boolean certificate checks may collapse `NotFound` to `false`, but must log integrity failures and never silently treat corruption as absence.

## Hashing and canonical encoding

- SHA-256 is available through `crypto::sha2_256` / `crypto::sha256`.
- Fixed hashes use `base::Hash256`; readable storage/API keys use lowercase hex.
- `base::Hash256::fromReadableString()` parses fixed hash text, but phase code must separately enforce exact canonical spelling before parsing.
- Keep canonical preimage construction and hashing together so no caller can hash unchecked text.

## Consensus extension seams

- Subject behavior is registered by subject-type hash.
- The nonce slot handler deserializes the embedded transaction and delegates to the transaction domain object.
- Handler registries are guarded by shared mutexes.
- Canonical slot derivation must be result-returning or have an explicit invalid result; never use proposal identity as a fallback for a recognized but malformed subject.

## CRDT persistence

- `GlobalDB::Put(vector<DataPair>, topics)` emits one atomic multi-key delta.
- `RegisterElementFilter()` and `RegisterNewElementCallback()` use regex key families.
- Existing element filters are per-element; certificate/index pair validation requires a new pre-merge delta-aware filter seam.
- Work-journal processing should attach to authoritative `/cert/v2/slot/...` records only. Index entries are metadata and must not independently execute certificate handlers.

## Bridge identity

- `InputUTXOInfo::output_idx_` is serialized as `TransferUTXOInput.output_index`.
- Use that existing field for the absolute zero-based receipt-local log position.
- Preserve block-wide `MatchedEvent::log_index` for observation/debugging and add an explicit receipt-local value.
- Live and catch-up discovery must share the same identity tuple and reject absent ordinals.

## Public-chain verification

- Endpoint RPC transport is injectable through the existing factory, enabling deterministic receipt tests.
- Verification already aggregates endpoint consensus weights.
- Exact-log validation belongs inside each endpoint verification result: select first, then validate address/topic and decoded event facts.

## Test conventions

- Tests use GoogleTest and CMake `addtest`.
- Full-node/account targets link `genius_node_test` with Apple `-force_load` where needed.
- CRDT-backed consensus tests link `base_crdt_test`.
- Prefer focused binaries under `build/OSX/Release/test_bin` for task feedback.
- Add new focused targets instead of reviving the commented legacy certificate suite wholesale.

## File ownership map

| Concern | Files |
|---------|-------|
| Canonical slot model | `GeniusTransaction.hpp`, `MintTransactionV2.*`, `Consensus.*`, `TransactionManager.cpp` |
| Receipt-local observation | `evmrelay/.../event_filter.*`, `bridge_catchup_watcher.*` |
| Mint API propagation | `BridgeRelayer.*`, `GeniusNode.*`, `TransactionManager.*` |
| Exact source-event proof | `PublicChainInputValidator.*` |
| Atomic pair filtering | `crdt_data_filter.*`, `crdt_datastore.*`, `globaldb.*` |
| Certificate v2 store | `Consensus.*`, `Blockchain.*` |
| Focused verification | `test/src/blockchain`, `test/src/account`, `test/src/bridge_e2e` |

