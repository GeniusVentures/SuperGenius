---
status: testing
phase: 02-conflict-and-replay-detection-hardening
source: 02-01-SUMMARY.md
started: 2026-05-29T00:00:00Z
updated: 2026-05-29T00:00:00Z
---

## Current Test

number: 1
name: OnConsensusCertificate signature change
expected: |
  `OnConsensusCertificate` now takes `const ConsensusCertificate &certificate`
  as a second parameter. The callback at line 127 passes it through. The header
  declaration at TransactionManager.hpp:456 matches.
awaiting: user response

## Tests

### 1. OnConsensusCertificate Signature Change
expected: `OnConsensusCertificate` now takes `const ConsensusCertificate &certificate` as a second parameter. The callback at TransactionManager.cpp:127 passes `certificate` through. Header at TransactionManager.hpp:456 matches.
result: pass

### 2. Certificate Fallback When Transaction Not Found
expected: When `GetTransactionByHash` returns null (standalone validator), the function deserializes from `certificate.proposal().subject()` via `DecodeNonceSubject`, checks for empty `transaction_data`, validates hash binding with `GetHash()` + `CheckHash()`, calls `ChangeTransactionState(tx, CONFIRMED)`, and falls through to the checkpoint code. No goto.
result: pending

### 3. No Regression for Full Nodes
expected: Existing code path (when `GetTransactionByHash` returns a valid tx) is unchanged — the regular `ChangeTransactionState(CONFIRMED)` + conflict resolution + checkpoint flow is preserved inside the `else` branch. Certificate fallback only activates when tx is NOT found locally.
result: pending

### 4. Failure Modes Return Approve
expected: All certificate fallback failures (empty transaction_data, hash mismatch, deserialization failure, non-decoded NonceSubject) return `Check::Approve` — never reject a valid consensus certificate. The certificate is the consensus ground truth regardless of local parsing ability.
result: pending

## Summary

total: 4
passed: 0
issues: 0
pending: 4
skipped: 0

## Gaps

[none yet]
