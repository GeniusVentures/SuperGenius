# Phase 17: Eligibility, Policy & Gating (Dry-Run) - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-10-16
**Phase:** 17-Eligibility, Policy & Gating (Dry-Run)
**Areas discussed:** Own-frontier mechanism, Window definition & knobs, Dry-run reporting surface, Pruned-marker design

---

## Own-frontier mechanism

| Option | Description | Selected |
|--------|-------------|----------|
| Cert-slot generalization | Resolve previous_hash from certificates (never pruned, embed winning tx hash); generalize GetRegisteredOutgoingPreviousHash (:1688-1726); durable | ✓ |
| Own-nonce exemption | Retain last-K own-nonce txs per address; K magic, burst re-exposure | |

**User's choice:** Cert-slot generalization (Recommended)
**Notes:** Exemption stays as planning fallback only. Divergence class previously failed CI on 4 platforms.

---

## Window definition & knobs

Presented: priority-depth only (research recommendation) vs cert-tightened. **Owner challenged the premise** with two verified-correct objections: (1) fresh items sit at depth zero and a tx's priority measures its creation; (2) "we could have just spent a really old UTXO that has a high depth" — a creation-age window instantly exposes a just-spent ancient tx. Source check confirmed `spent_by_txid` is written nullopt/reset today (UTXOManager.cpp:179, :390).

| Option | Description | Selected |
|--------|-------------|----------|
| Constraint + Candidate A first | Window = spend recency (never creation age, never wall-clock); verify consumed-entry CRDT priority as the signal; fallback B populates spent_by_txid + amends out-of-scope row | ✓ |
| Go straight to B | Populate spent_by_txid now, amend the out-of-scope row | |

**User's choice:** Constraint + A-first (Recommended)
**Notes:** N = config knob required when pruning enabled; no default (default-off). Cert-tightening deferred.

---

## Dry-run reporting surface

| Option | Description | Selected |
|--------|-------------|----------|
| Structured result + log | Pure evaluator result (eligible/ineligible + reasons), fixture-tested exact set; per-cycle INFO in production; introspection stays Phase 18 (RECL-04) | ✓ |
| Queryable report now | Also build operator-facing report surface this phase | |

**User's choice:** Structured result + log (Recommended)

---

## Pruned-marker design

| Option | Description | Selected |
|--------|-------------|----------|
| Per-tx marker records | One /prune/ record per erased tx (path + CID) in the shared RocksDB (CRDTWorkJournal precedent); precise PRUNED answers | ✓ |
| Watermark-only record | Single "pruned below depth D" record; per-tx lookups can't distinguish pruned from never-existed | |

**User's choice:** Per-tx marker records (Recommended)
**Notes:** Owner confirmed RocksDB placement ("should live in rocksdb, right?"). Designed here; written in Phase 18; answers surfaced Phase 19. Owner also described a future "extra final prune" distinct from this spent prune — captured as deferred (own phase if pursued).

---

## Claude's Discretion

- Evaluator module placement (one-owner-per-domain rule; TxPruner service is Phase 18)
- Config key names (`pruning.*`) and INFO log format
- Exact-set fixture construction

## Deferred Ideas

- Extra final prune (owner-described retirement-style prune; future phase)
- Cert-tightened window; queryable dry-run report (Phase 18 RECL-04); own-nonce exemption (fallback)
