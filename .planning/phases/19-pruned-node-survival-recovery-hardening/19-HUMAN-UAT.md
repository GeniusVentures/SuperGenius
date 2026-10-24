---
status: partial
phase: 19-pruned-node-survival-recovery-hardening
source: [19-VERIFICATION.md]
started: 2026-10-24T00:00:00Z
updated: 2026-10-24T00:00:00Z
---

## Current Test

[awaiting human testing]

## Tests

### 1. Staging spot-check — pruned node restart + query + spend/receive
expected: Restart the node_example demo node (staging, Light) that carries pruned history from the phase-18 demo. Boot completes to READY (no wedge). Run `tx <known-pruned-hash>` at the CLI → the distinct "history pruned" answer (never a bare not-found). Then a normal `transfer` (spend) and observe an incoming tip/receive completing. The automated RestartMatrix already proves all of this on a real stack — this is the confirmatory live-net run.
result: [pending]

## Summary

total: 1
passed: 0
issues: 0
pending: 1
skipped: 0
blocked: 0

## Gaps
