# Phase 16: Erasure Primitives & Bookkeeping Hardening - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-10-15
**Phase:** 16-Erasure Primitives & Bookkeeping Hardening
**Areas discussed:** Erase-API composition point, Map deletion lifecycle, Crash-proof depth in 16

---

## Erase-API composition point

| Option | Description | Selected |
|--------|-------------|----------|
| GlobalDB single-entry | Narrow, generic two-level erase API (key + CID in, one WriteBatch, guards inside) on the GlobalDB facade; wrong deletion impossible by shape; Phase 18 TxPruner stays thin | ✓ |
| Owner-module + composed batch | CrdtSet local-erase, DAGSyncer guarded remove, TransactionManager map-delete — each accepting an injectable RocksDB batch; TxPruner (Phase 18) assembles | |
| Planner decides | Capture trade-off in CONTEXT.md, let researcher/planner decide with full code context | |

**User's choice:** GlobalDB single-entry (Recommended)
**Notes:** API stays generic (no tx semantics) so no domain knowledge leaks into the CRDT layer; owner-module key-layout knowledge stays inside CrdtSet/DAGSyncer/TransactionManager.

---

## Map deletion lifecycle

| Option | Description | Selected |
|--------|-------------|----------|
| Plain delete, marker later | Phase 16 builds plain map-entry deletion only; pruned-marker record designed in Phase 17 with the PRUNED error surface (likely under /prune/ namespace, not the map's) | ✓ |
| Marker variant now | Map lifecycle includes tombstone-to-marker transition now so Phases 17-19 never re-touch the map layer | |

**User's choice:** Plain delete, marker later (Recommended)
**Notes:** Risk of building the marker in the wrong place before the PRUNED design settles; research suggests the marker belongs under the pruner's own node-local prefix.

---

## Crash-proof depth in 16

| Option | Description | Selected |
|--------|-------------|----------|
| Unit-level in 16 | WriteBatch-atomicity unit tests on fresh temp DBs + failure-injection points at primitive boundaries; process-kill → fixed-point tests in Phase 18 | ✓ |
| Process-kill tests now | Actual process-kill restart tests on the primitives in Phase 16 | |

**User's choice:** Unit-level in 16 (Recommended)
**Notes:** The real crash surface (the four-step erase flow ordering) doesn't exist until Phase 18's service.

---

## Claude's Discretion

- Test-file placement and fixture naming
- Exact method signatures/parameter shapes for the GlobalDB erase API
- `maxHeight` exposure placement (here or Phase 17 if unused in 16)

## Deferred Ideas

- `bridge-race-not-all-11-mint-within-window` (P1 todo) — reviewed in cross-reference, stopword-noise match only, stays in pending for Track A bridge scope
