# Phase 19: Pruned-Node Survival & Recovery Hardening - Discussion Log

> **Audit trail only.** Decisions live in CONTEXT.md.

**Date:** 2026-10-23
**Areas discussed:** Checkpoint-rebuild guard, TM-SYNCING wedge fix, PRUNED answer surface, Pruned-mesh E2E harness

## Checkpoint-rebuild guard
| Option | Selected |
|--------|----------|
| Rebuild + consumed-crosscheck (durable set authoritative; rebuild gap-fills; consumed outpoints never re-created) | ✓ |
| Refuse + resync from full (needs nonexistent peer protocol + reachable full node) | |

## TM-SYNCING wedge fix
Presented with a code walkthrough of the wedge (baseline advance at :2037-2039 → pre-broadcast failure → SYNCING at :581 → SyncNonce polling a confirmation that can never arrive). Owner requested the explanation, then locked:

| Option | Selected |
|--------|----------|
| Baseline rollback (pre-broadcast = local failure, promise never kept) + bounded SYNCING exit (fall back to durable nonce, READY, loud log) | ✓ |
| Bounded exit only (phantom baseline lingers) | |

## PRUNED answer surface
| Option | Selected |
|--------|----------|
| Tri-state at tx lookup (found/not-found/pruned via LookupPrunedEntry); `ds` + local queries surface it; no RPC this phase | ✓ |
| Include API-layer mapping (pulls transport work in) | |

## Pruned-mesh E2E harness
| Option | Selected |
|--------|----------|
| 3-node extension of the 18-01 Redelivery fixture (2 pruned lights + 1 full server) | ✓ |
| multi_node_finality_fault_test family (heavier, consensus too) | |
