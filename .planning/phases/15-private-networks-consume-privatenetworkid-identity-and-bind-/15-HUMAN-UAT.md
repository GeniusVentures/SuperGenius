---
status: partial
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
source: [15-REVERIFICATION-3.md]
started: 2026-09-04
updated: 2026-09-04
---

## Current Test

[awaiting human testing]

## Tests

### 1. E2E two-node private-network job flow with public-node control

expected: Provision two GeniusNodes with the same `private_network_id` /
network_key / bootstrap peers (config surface from 15-01), publish a job from
one node, and verify (a) both private nodes replicate job-scoped state under
the `/chain/<privateNetworkId>/` scope, (b) a concurrently running public
node (no private_network_id) sees none of the private job's topics, keys, or
results, and (c) tearing down one private node installs deny-all ingest on
its still-live GlobalDB (no stale private data continues to flow).

context: No automated multi-process GeniusNode E2E exists — closest automated
partial proof is `PnetIsolationAndGaterBlocking` (green). A full multi-node
automated E2E is additionally blocked by the tracked phase-13 quorum-policy
regression (`.planning/todos/pending/genesis-e2e-suites-quorum-policy-regression.md`),
which breaks `blockchain_genesis_test`/`processing_nodes_test` fixture
topologies independently of phase 15.

result: [pending]

## Summary

total: 1
passed: 0
issues: 0
pending: 1
skipped: 0
blocked: 0

## Gaps
