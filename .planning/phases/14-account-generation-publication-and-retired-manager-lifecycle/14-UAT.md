---
status: testing
phase: 14-account-generation-publication-and-retired-manager-lifecycle
source: 14-01-SUMMARY.md, 14-02-SUMMARY.md, 14-03-SUMMARY.md, 14-05-SUMMARY.md, 14-13-SUMMARY.md, 14-14-SUMMARY.md, 14-15-SUMMARY.md
started: 2026-08-21T16:32:01Z
updated: 2026-08-21T16:32:01Z
---

## Current Test

number: 2
name: Complete account-management regression health
expected: |
  The account-management test target completes its existing account transfer and payout scenarios without trust-readiness timeouts.
awaiting: user response

## Tests

### 1. Safe account switching
expected: A switch exposes only explicit switching or unavailable results until one complete new account generation is ready; it never exposes partial account, manager, or balance state.
result: issue
reported: "Well, the other tests are broken"
severity: major

### 2. Complete account-management regression health
expected: The account-management test target completes its existing account transfer and payout scenarios without trust-readiness timeouts.
result: [pending]

### 3. Retiring work reaches a terminal outcome
expected: Work admitted before an account switch is allowed to reach its real terminal outcome; new work is rejected until the new account generation is ready.
result: [pending]

### 4. Failed switch recovers explicitly
expected: A timed-out or failed replacement leaves account-bound operations explicitly unavailable and only one deliberate recovery can resume a ready account generation.
result: [pending]

### 5. Typed active account access
expected: Callers receive an explicit lifecycle error while switching or unavailable, rather than an empty address, zero balance, or raw invalid-status fallback.
result: [pending]

## Summary

total: 5
passed: 0
issues: 1
pending: 4
skipped: 0
blocked: 0

## Gaps

- truth: "The Phase 14 test suite should not leave unrelated account-management regressions failing outside its selected lifecycle checks."
  status: failed
  reason: "User reported: Well, the other tests are broken"
  severity: major
  test: 1
  artifacts:
    - path: "test/src/account/account_management_test.cpp"
      issue: "Full-target closure run reported TransferAccount and SetPayoutAddress trust-readiness timeouts."
  missing: []
