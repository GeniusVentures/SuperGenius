---
phase: 03
phase_name: "Burn Deduplication Cache"
project: "SuperGenius"
generated: "2026-05-27"
updated: "2026-05-28"
status: "in-progress"
---

# Phase 3 Plan: Burn Deduplication Cache

## Goal

Prevent double-minting by tracking which burn transaction hashes have already been
processed.  Comprises four sub-tasks tracked as GitHub issues.

## Architecture Note (2026-05-28)

`BridgeConsensusAdapter` has been deleted.  The bridge consensus flow now operates on
the mint transaction's nonce subject (`sgns.nonce.v1`) directly, rather than through
a separate `gnus.bridge_event.v1` consensus subject.  The burn tx hash flows from
`MintFunds` → `MintTransactionV2` → UTXO commitment → nonce subject's
`consumed_outpoints[0].tx_id_hash`.

## Sub-tasks

| # | GitHub | Task | Status |
|---|--------|------|--------|
| 1 | [#269](https://github.com/GeniusVentures/SuperGenius/issues/269) | Define canonical message_id for EVM bridge source events | done |
| 2 | [#270](https://github.com/GeniusVentures/SuperGenius/issues/270) | Map bridge mints to deterministic consensus slot keys | done |
| 3 | [#271](https://github.com/GeniusVentures/SuperGenius/issues/271) | Add processing reservation state | done |
| 4 | [#286](https://github.com/GeniusVentures/SuperGenius/issues/286) | Persist executed bridge message state | done |

## Task 1: Canonical message_id (done)

Implemented in evmrelay as `compute_bridge_message_id()` and `bridge_message_id()`.

**Canonical fields:** src_chain_id (uint64), bridge_contract (20 bytes), tx_hash (32 bytes), log_index (uint32)
**Derivation:** keccak256 of concatenated big-endian bytes (64 bytes total)
**Files:**
- `evmrelay/include/eth/bridge_event.hpp` — declarations
- `evmrelay/src/eth/bridge_event.cpp` — implementation
- `evmrelay/test/eth/bridge_event_test.cpp` — 9 tests

## Task 2: Map bridge mints to deterministic slot keys (pending)

### Problem

Currently, `ConsensusManager::GetSlotKey()` produces `account_id:nonce` for all
nonce subjects.  When two validators both create a mint for the same burn
transaction, they get different slot keys (different `account_id` values) and
therefore **never compete in the same consensus slot**.  This means no
double-mint prevention at the consensus level.

### Approach

Modify `GetSlotKey()` in `src/blockchain/Consensus.cpp` to detect bridge mint
nonce subjects and derive the slot key from the burn tx hash instead.

**Detection:** A bridge mint nonce subject has a `utxo_commitment` with exactly
one consumed outpoint whose `tx_id_hash` references an external transaction
(i.e., no local certificate exists for it).

**Slot key formula for bridge mints:**
```
slot_key = hex_lower(burn_tx_hash)
```
(where `burn_tx_hash` = `utxo_commitment.consumed_outpoints[0].tx_id_hash`)

This replaces `account_id:nonce` for bridge mints, ensuring all validators
competing for the same burn land in the same slot.  Existing `IsBetterProposal`
resolves any ties.

**Files to modify:**
- `src/blockchain/Consensus.cpp` — `GetSlotKey()` (~line 2044)

### Open question

Need to decide how to distinguish a bridge mint's consumed outpoint from a
native transfer's consumed outpoint without a blockchain lookup on the hot
path.  Options:
- Check if the outpoint's `output_index == 0` and the tx doesn't match known
  local UTXO patterns
- Add a flag field to `NonceSubject` protobuf
- Pass chain context through to `GetSlotKey`

## Task 3: Processing reservation state (pending)

Add in-memory reservation state to prevent a burn from being processed
concurrently by multiple handlers.

## Task 4: Persist executed bridge message state (pending)

Persist which bridge messages have been executed so restarted nodes don't
re-process completed mints.

## Design Notes

- `BridgeEventKey` already exists (src_chain_id, tx_hash, log_index) but omits `bridge_contract`. The message_id adds it for cross-chain uniqueness.
- message_id is stable across observers, timestamps, and non-canonical fields (amount, block_number, etc.)
- `bridge_event_claim_hash()` already exists but includes mutable fields (observed_at, finality_depth) — unsuitable as a canonical identifier.
- The burn `transaction_hash` is already passed to `MintFunds` and stored in the UTXO commitment — no additional data plumbing is needed for Task 2.
