# Phase 3 Summary: Burn Deduplication Cache

**Status:** Complete (gap closure superseded with 4 additional fixes in 03-01-SUMMARY.md)

## What was built

### Task 1 — Canonical message_id
Added `compute_bridge_message_id()` and `bridge_message_id()` to evmrelay. Produces a keccak256 hash of src_chain_id, bridge_contract, tx_hash, and log_index. 9 unit tests in `bridge_event_test.cpp`.

### Task 2 — Deterministic consensus slot keys
Added MintV2 detection in `ConsensusManager::GetSlotKey()` via `EmbeddedTransaction` oneof. Bridge mints produce `mint-v2:{chain_id}:{token_id}:{amount}:{dest}` slot keys instead of `account_id:nonce`, ensuring validators minting the same burn compete in the same slot. Later refactored to `MintTransactionV2::GetSlotID()` override dispatched through `slot_key_handlers_` map.

### Task 3 — Processing reservation state
Added in-memory reservation map in `TransactionManager` to prevent concurrent processing of the same burn by multiple handlers.

### Task 4 — Persist executed bridge state
Added RocksDB-backed persistence under `/bridge/executed/` path. Restarted nodes skip already-processed burn transactions.

## Key decisions
- Bridge consensus flow uses nonce subjects directly (`sgns.nonce.v1`) after `BridgeConsensusAdapter` deletion
- Burn tx hash flows: `MintFunds` → `MintTransactionV2` → UTXO commitment → `consumed_outpoints[0].tx_id_hash`
- Slot key dispatch uses `slot_key_handlers_` map (same pattern as `subject_handlers_`), keyed by subject type hash

## Files changed
- `evmrelay/include/eth/bridge_event.hpp` — message_id declarations
- `evmrelay/src/eth/bridge_event.cpp` — message_id implementation
- `src/blockchain/Consensus.cpp` — `GetSlotKey()` MintV2 oneof detection
- `src/account/MintTransactionV2.cpp` — `GetSlotID()` override
- `src/account/TransactionManager.cpp` — reservation state, persistence, deserializer registration
- `src/account/GeniusTransaction.hpp` — static `deserializers_map`
