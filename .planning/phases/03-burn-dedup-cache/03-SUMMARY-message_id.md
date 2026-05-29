---
phase: 03
task: "Define canonical message_id for EVM bridge source events"
github_issue: "https://github.com/GeniusVentures/SuperGenius/issues/269"
generated: "2026-05-27"
status: "done"
---

# Canonical message_id — Summary

## What Was Done

Defined `compute_bridge_message_id(src_chain_id, bridge_contract, tx_hash, log_index)`
and convenience overload `bridge_message_id(claim)` in evmrelay's bridge event module.
The message_id is a keccak-256 hash over a deterministic 64-byte encoding of the four
canonical source event identity fields.  It is stable across observers, non-canonical
claim fields, and consensus rounds.

## Decisions

### 1. Separate function, not extension of BridgeEventKey
**What:** Created `compute_bridge_message_id()` as a free function rather than modifying `BridgeEventKey`.
**Why:** `BridgeEventKey` is used for in-memory peer dedup (EventDeduper) and intentionally omits `bridge_contract` since tx_hash+log_index is sufficient within a chain. The message_id adds `bridge_contract` for cross-chain uniqueness and is a hash, not a comparable key.
**Source:** Analysis of existing bridge event types.

### 2. keccak-256 over big-endian encoding
**What:** Hash a 64-byte canonical encoding: src_chain_id(8) || bridge_contract(20) || tx_hash(32) || log_index(4).
**Why:** Consistent with Ethereum-native hashing used throughout evmrelay.  Big-endian encoding uses existing `base/byte_encoding` utilities.  Excludes mutable fields (`observed_at`, `finality_depth`, `amount`, `block_number`) for stability.
**Source:** Existing `bridge_event_claim_hash()` pattern uses keccak-256.

### 3. Exclude dest_chain_id
**What:** message_id does not include `dest_chain_id`.
**Why:** The message_id identifies the source event uniquely, regardless of destination. Different dest_chain_id values produce the same message_id (verified by test).  This is correct — the same burn event bridged to two different chains should have the same source identifier.
**Source:** Issue requirement: "source chain id, source bridge contract, tx hash, log index."

## Files Changed

| File | Lines | Type |
|------|-------|------|
| `evmrelay/include/eth/bridge_event.hpp` | +25 | Declaration + inline convenience |
| `evmrelay/src/eth/bridge_event.cpp` | +19 | Implementation |
| `evmrelay/test/eth/bridge_event_test.cpp` | +77 | 9 tests |

## Tests (9/9 passing)

| Test | Purpose |
|------|---------|
| IsDeterministic | Same inputs → same hash |
| MatchesFreeFunction | Claim overload matches direct call |
| ChangesWithDifferentChainId | src_chain_id affects output |
| ChangesWithDifferentContract | bridge_contract affects output |
| ChangesWithDifferentTxHash | tx_hash affects output |
| ChangesWithDifferentLogIndex | log_index affects output |
| IgnoresNonCanonicalFields | amount/block_number/observed_at don't affect output |
| ProducesValidHashWithZeroFields | Zero-init claim produces non-zero hash |
| NearCollisionDifferentDestChain | Different dest_chain_id → same message_id |
