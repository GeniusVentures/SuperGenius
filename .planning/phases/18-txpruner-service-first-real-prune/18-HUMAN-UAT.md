---
status: resolved
phase: 18-txpruner-service-first-real-prune
source: [18-VERIFICATION.md]
started: 2026-10-21T23:30:00Z
updated: 2026-10-23T00:00:00Z
---

## Current Test

[awaiting human testing]

## Tests

### 1. Live staging grow-then-prune demo
expected: On a staging-net Light node: enable `pruning` with `dry_run: false`, a small `finality_window`, and `settle_seconds` tuned for the demo; dev-mint (the new policy permits burn-less mints through full consensus on net 333) → spend the minted UTXOs (transfers) → wait for the first real cycle (log states the schedule: first tick after the settle window from gate hold, ~11 min at defaults) → verify: the spent tx is GONE at both levels (CRDT value + block bytes, e.g. `ds` dump / direct store check), the `/prune/tx/<hash>` marker is present, the unspent UTXO set and disk are otherwise intact, and the per-cycle INFO line + `GetPruneStatus` report the erase counts and watermark.
result: passed — live demo executed by the owner (2026-10-23): staging validators redeployed with the phase-18 build; dev-mint certified through full consensus; the real prune cycle erased spent transactions; owner confirmed "it's validated".

### 2. CR-01 logic-fix sign-off
expected: Owner review of commit 08421f54a (MintFunds burn-reservation-id capture before the try). The automated regression (`BurnCleanup_MintFundsCatchPathRemovesBurnUtxoAfterPostMoveThrow`, driving the real exception path through a post-move seam hook) passes — the review is the logic-class-fix protocol's human eye on the capture-before-move pattern and the seam's placement.
result: [pending]

## Summary

total: 2
passed: 2
issues: 0
pending: 0
skipped: 0
blocked: 0

## Gaps
