---
status: complete
phase: 03-network-hardening-and-operational-readiness
source: 03-01-SUMMARY.md, 03-02-SUMMARY.md
started: 2026-05-29T00:00:00Z
updated: 2026-05-29T00:00:00Z
---

## Current Test

number: 1
name: Pre-publish Size Enforcement Gate
expected: |
  In `SendTransactionItem` at TransactionManager.cpp, after `SerializeByteVector()`
  and before `CreateConsensusProposal`, a 64KB size check rejects oversized
  transactions with `outcome::failure`. Same threshold as `MAX_EMBEDDED_TX_BYTES`.
awaiting: user response

## Tests

### 1. Pre-publish Size Enforcement Gate (SIZE-01)
expected: In `SendTransactionItem`, after `SerializeByteVector()` and before `CreateConsensusProposal`, a 64KB size check rejects oversized transactions with `outcome::failure`. Same threshold as existing `MAX_EMBEDDED_TX_BYTES` in handler.
result: pending

### 2. Configurable Timestamp Tolerance (TS-01)
expected: `DevConfig` in GeniusNode.hpp has `timestamp_tolerance_ms` field (default 300000). Wired via `SetTimeFrameToleranceMs` during `GeniusNode::New()`. Existing `CheckTransactionTimestamp` reads the configurable value.
result: pending

### 3. Operational Metrics Counters (METRICS-01)
expected: TransactionManager.hpp declares 7 `std::atomic<uint64_t>` counters. TransactionManager.cpp increments them at key lifecycle points. `~TransactionManager()` destructor logs all counter values on shutdown.
result: pending

### 4. ProposalCleanupHandler Infrastructure (CLEAN-01)
expected: `ProposalCleanupHandler` typedef exists. `RegisterProposalCleanupHandler`/`FireProposalCleanupCallbacks` added to ConsensusManager. Fired from timeout callers (lines 1392, 1476), NOT from certificate caller (line 1912). Blockchain delegates the registration.
result: pending

### 5. Timeout Cleanup Transitions to FAILED (CLEAN-01)
expected: TransactionManager's `OnProposalTimeoutCleanup` handler finds the tx via `GetTransactionByHash`, checks status is VERIFYING, calls `ChangeTransactionState(tx, FAILED)`. CONFIRMED entries untouched, missing entries skipped silently.
result: pending

## Summary

total: 5
passed: 0
issues: 0
pending: 5
skipped: 0

## Gaps

[none yet]
