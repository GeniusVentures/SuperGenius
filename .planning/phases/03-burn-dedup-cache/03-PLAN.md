---
phase: 03
phase_name: "Burn Deduplication Cache"
project: "SuperGenius"
generated: "2026-05-27"
status: "in-progress"
---

# Phase 3 Plan: Burn Deduplication Cache

## Goal

Prevent double-minting by tracking which burn transaction hashes have already been
processed.  Comprises four sub-tasks tracked as GitHub issues.

## Sub-tasks

| # | GitHub | Task | Status |
|---|--------|------|--------|
| 1 | [#269](https://github.com/GeniusVentures/SuperGenius/issues/269) | Define canonical message_id for EVM bridge source events | in-review |
| 2 | [#270](https://github.com/GeniusVentures/SuperGenius/issues/270) | Map bridge messages to deterministic slot keys | pending |
| 3 | [#271](https://github.com/GeniusVentures/SuperGenius/issues/271) | Add processing reservation state | pending |
| 4 | [#286](https://github.com/GeniusVentures/SuperGenius/issues/286) | Persist executed bridge message state | pending |

## Task 1: Canonical message_id

Implemented in evmrelay as `compute_bridge_message_id()` and `bridge_message_id()`.

**Canonical fields:** src_chain_id (uint64), bridge_contract (20 bytes), tx_hash (32 bytes), log_index (uint32)
**Derivation:** keccak256 of concatenated big-endian bytes (64 bytes total)
**Files:**
- `evmrelay/include/eth/bridge_event.hpp` — declarations
- `evmrelay/src/eth/bridge_event.cpp` — implementation
- `evmrelay/test/eth/bridge_event_test.cpp` — 9 tests

## Design Notes

- `BridgeEventKey` already exists (src_chain_id, tx_hash, log_index) but omits `bridge_contract`. The message_id adds it for cross-chain uniqueness.
- message_id is stable across observers, timestamps, and non-canonical fields (amount, block_number, etc.)
- `bridge_event_claim_hash()` already exists but includes mutable fields (observed_at, finality_depth) — unsuitable as a canonical identifier.
