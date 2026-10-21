# Phase 18: TxPruner Service & First Real Prune - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-10-21
**Phase:** 18-TxPruner Service & First Real Prune
**Areas discussed:** Concurrency & lock protocol, Re-delivery guard, Dev-history seeding (became net-scoped dev-mint policy), Backfill & batching

---

## Concurrency & lock protocol

| Option | Description | Selected |
|--------|-------------|----------|
| Mutex-serialized compose | CrdtSet recursive mutex_ across check→stage→commit + UTXO-lock spent-ness re-verify; bounded holds; closes WR-02 by construction | ✓ |
| Optimistic epoch | Snapshot + re-check + abort-retry; no blocking, subtle correctness argument | |
| CRDT-worker-affine | Prunes on the single CRDT worker thread; pins the only DAG fetcher (archive-replication-flake hazard) | |

**User's choice:** Mutex-serialized (Recommended)
**Notes:** Evaluator stays lock-free read-only; D-04's bounded batches keep the hold short.

---

## Re-delivery guard

Presented: isResolved guard at root-job creation + cycle-as-sweep vs durable pruned-CID set. **Owner challenged**: "Is that an issue though, because markResolved gets called for a head cid and I don't think deletion reverts that." Source verification (HandleCIDBroadcast, crdt_datastore.cpp:694-701): the owner is right that the seal persists, but the broadcast dedup consults HasBlock (block EXISTENCE) — pruning deletes the block, evaporating the dedup; EnqueueRootCID would re-fetch.

| Option | Description | Selected |
|--------|-------------|----------|
| Seal-based dedup | isResolved check in HandleCIDBroadcast after the HasBlock miss (block gone + seal present ⇒ processed-and-pruned ⇒ skip); no new state; cycle-as-sweep via idempotent re-evaluation | ✓ |
| Durable pruned-CID set | New state consulted at fetch layer — duplicates what retained seals provide | |
| Discuss further | Argue the read | |

**User's choice:** Lock: seal-based dedup
**Notes:** Owner's persistence insight folded in — the fix is one check consulting existing persistent state; SC-3's re-broadcast test (zero fetch attempts) proves it.

---

## Dev-history seeding → net-scoped dev-mint policy

Presented: test-sanctioned helper + config-gated example command vs net-id gating. **Owner corrected the framing**: "I don't think you are right. The ctest does use consensus, that's the point of it. The only difference is that Mints don't require a Burn on ctest and that is something that I wanted to replicate on node example. I want to basically have that only on staging net probably and not being certified on test and main nets."

**Decision (owner's framing adopted):** burn-less mints through FULL consensus, permitted only on designated nets (staging 333 + local dev); every validator refuses certification on other nets. Resolves the witness-bypass-hardening todo (global ctest allowance becomes net-scoped). Research traces the exact ctest mint path (BuildUTXOWitness producer gate + validator semantics) and decides the designated-net representation + enforcement points.

---

## Backfill & batching

| Option | Description | Selected |
|--------|-------------|----------|
| Bounded K, no cursor | K per tick (config knob); erased txs vanish from enumeration so the backlog self-marks; cursor only if scans measure hot | ✓ |
| Persistent cursor now | /prune/ cursor record — extra state for a solved problem | |

**User's choice:** Bounded K, no cursor (Recommended)

---

## Claude's Discretion

- TxPruner class placement/shape; sweep/tick logging detail; RECL-04 queryable-status surface shape; exact new config key names

## Deferred Ideas

- Durable pruned-CID set (rejected); optimistic/worker-affine locking (rejected); persistent cursor (measure-gated); RPC status surface; bytes-reclaimed (Phase 20)
