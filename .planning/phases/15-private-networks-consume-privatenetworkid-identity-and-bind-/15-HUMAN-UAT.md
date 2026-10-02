---
status: complete
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
source: [15-REVERIFICATION-3.md]
started: 2026-09-04
updated: 2026-10-02
---

## Current Test

[testing complete]

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

result: issue
reported: "So I think we should have an automated test for this"
severity: major

## Summary

total: 1
passed: 0
issues: 1
pending: 0
skipped: 0
blocked: 0

## Gaps

<!-- YAML format for plan-phase --gaps consumption -->
- truth: "The E2E two-node private-network job flow with public-node control
  (private scope replication under /chain/<privateNetworkId>/, public-node
  isolation, deny-all ingest on private-node teardown) is covered by an
  automated test"
  status: failed
  reason: "User reported: So I think we should have an automated test for this"
  severity: major
  test: 1
  artifacts: []  # Filled by diagnosis
  missing: []    # Filled by diagnosis
