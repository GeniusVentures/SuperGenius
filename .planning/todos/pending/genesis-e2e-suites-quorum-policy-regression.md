---
created: 2026-09-04
title: blockchain_genesis_test + processing_nodes_test hang under phase-13 canonical quorum policy
area: correctness
regression_introduced_in: 76f5fde28 feat(13-12): implement canonical quorum policy rules
found_during: phase-15 execute-phase regression gate (2026-09-04)
resolves_phase: 13
files:
  - src/trustedpeer/QuorumPolicy.cpp
  - src/securecrdt/QuorumThresholdValidation.hpp
  - test/src/blockchain/blockchain_genesis_test.cpp
  - test/src/processing_nodes/processing_nodes_test.cpp
---

## Problem

`blockchain_genesis_test` and `processing_nodes_test` hang forever (nodes never
reach `NodeState::READY`; 50s wait-condition timeouts in fixture setup /
body). Both fail deterministically (2/2 serial and parallel runs).

## Root cause (bisect-verified)

`git bisect` over `b50ee5fd6..phase-15-HEAD` (120 commits, discriminator
`ctest -R blockchain_genesis_test --timeout 150`) identifies first-bad commit:

- `76f5fde28` **feat(13-12): implement canonical quorum policy rules** (2026-08-12)

Mechanism: the 2-3 node fixtures of both suites can no longer satisfy the
canonical quorum policy (strict-majority membership, two-thirds burn floors,
versioned policy hash links). Genesis is never created → the validator
registry never seeds → `Blockchain::Start()` returns
`BLOCKCHAIN_NOT_INITIALIZED` in a 5s retry loop ("Error starting blockchain:
Blockchain not fully initialized" in `sgnslog2.log`) → nodes never reach
READY. Phase 13 updated its own `quorum_policy_test` in the same commit but
not these older e2e suites; no phase 13-15 verification re-ran them (last
green: phase-12 verification, 2026-07-27).

## Evidence

- Pre-regression baseline `b50ee5fd6`: both suites PASS (~18s each).
- First-bad `76f5fde28` parent `6f0501403`: PASS; `76f5fde28` and later: FAIL.
- At phase-15 HEAD: all 7 node-lifecycle suites (node_startup, full_node,
  node_shutdown_race, transaction_sync, globaldb_integration,
  genius_node_bootstrap_reconnect, multi_account) PASS; 22/24 prior-phase
  suites pass — only these two quorum-sensitive suites fail.
- NOT a phase-15 regression: the good bound already contains 15-01/15-02; all
  later phase-15 commits sit atop the already-broken state.

## Fix direction

Either (a) update the two suites' fixtures to satisfy the canonical policy
(documenting the required quorum shape — mirrors how 13-12 updated
`quorum_policy_test`), or (b) revisit `QuorumPolicy` canonical floors if they
were meant to admit the minimal test topologies. Needs a dedicated debug/fix
session — do not fold into phase 15.
