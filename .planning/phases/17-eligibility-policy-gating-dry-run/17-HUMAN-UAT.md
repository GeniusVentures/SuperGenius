---
status: resolved
phase: 17-eligibility-policy-gating-dry-run
source: [17-VERIFICATION.md]
started: 2026-10-19T18:30:00Z
updated: 2026-10-20T15:30:00Z
---

## Current Test

[awaiting human testing]

## Tests

### 1. CR-01 concurrency fix concurrence review
expected: `RunPruneDryRunCycle` builds the evaluator from a `SnapshotAccountServices()` snapshot held for the whole cycle (commit eb1a23adf). No unit seam exists (GeniusNode is not unit-constructible — documented at result_gc_test.cpp:1-14); the invariant is source-audited. A human review of the lock discipline against the Phase-13 generation-lifecycle paths is the residual check.
result: passed — owner-directed generation tag landed (follows the Phase-13 house pattern): the cycle's snapshot generation is re-checked after Evaluate(); a mid-cycle switch tags the summary "[generation superseded — retired snapshot]". No switch blocking needed — verified at the swap site that old generations are only reassigned under lifecycle_mutex_ and die by refcount, with dependencies on the never-swapped node-scoped GlobalDB. Build + format green.

### 2. Live-node dry-run timer observation
expected: with `pruning.enabled=true`, `pruning.dry_run=true`, and a `finality_window` set in `sgns_config.json` on a Light node with real history, the per-cycle INFO summary appears every 10 minutes and no data is erased (dry-run never writes). Observational on a live node — no automated harness.
result: passed — observed live on staging net 333 (Light role, 2026-10-20 15:11:36): the dry-run cycle fired with 5 REAL candidates (the net's tx history incl. a prior failed mint), per-candidate CID resolution logged, INFO summary emitted ("5 candidate(s), 0 eligible / 5 ineligible, 0/0 block(s) erasable"), and zero writes — disk and CRDT state untouched. 0-eligible is the correct verdict (no spent outputs on the net). The eligible>0 case is not observable live without real bridged tokens (unbacked dev mints are blocked at three independent gates — witness-build producer-existence, consensus RPC evidence, remote type-rules; all verified this session); that case is covered by the 22 automated evaluator tests (exact-set fixtures on real CRDT stores) and folds into Phase 19's E2E seeding harness. Session also produced three recorded incident todos (boot wedge → P19, MintFunds leak → P18, TM Syncing wedge → P19) and one security-hardening todo (witness bypass).

## Summary

total: 2
passed: 2
issues: 0
pending: 0
skipped: 0
blocked: 0

## Gaps
