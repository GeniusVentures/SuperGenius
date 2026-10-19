# Requirements: SuperGenius

**Defined:** 2026-10-15
**Milestone:** v1.2 — Light-node transaction pruning (sliding-window disk reclamation)
**Core Value:** Light-node disk stays bounded by physically erasing spent transaction history — both the CRDT value and its raw ipfs-lite CID block — beyond a depth/finality window, while the node keeps its unspent UTXO set and remains fully operational.

## v1 Requirements

Requirements for milestone v1.2. Each maps to roadmap phases.

### Pruning Core

- [x] **PRUNE-01**: A transaction is erasable only when every output is genuinely consumed (`IsOutPointGenuinelyConsumed`, not synthesized zero-amount tombstones) AND its depth beyond the current DAG head exceeds the configured finality window
- [x] **PRUNE-02**: Erasure physically deletes, in one atomic RocksDB `WriteBatch`, the CRDT set entries (`/crdt/s|t/<key>/<cid>`, `/crdt/k/<key>/v|p`), the raw CID block (existing `removeNode` chain), and the tx→CID map entry — never via the CRDT tombstone path (`DeleteKey`/`DeleteTransaction` are forbidden for pruning)
- [x] **PRUNE-03**: A delta block is erased only when every element it carries is prune-eligible (parse the block at prune time; deltas are multi-element)
- [ ] **PRUNE-04**: The prune cycle is idempotent and crash-consistent — ordered erase flow (value → CID lookup → block-eligibility+delete → map entry) reaches a fixed point across crashes and restarts
- [ ] **PRUNE-05**: Re-delivered/resurrected entries (add-wins OR-Set re-merge from lagging peers or restart head exchange) are re-pruned by a periodic sweep, and root processing treats pruned/resolved CIDs as terminal — no re-fetch storm

### Gating & Safety

- [x] **GATE-01**: Pruning runs only on `NodeType::Light` (fail-closed gate on the enum) and only within the `/bc-<netid>/tx/` prefix allowlist
- [ ] **GATE-02**: Non-transaction data is provably untouched — certificates, SecureCRDT policy values, processing results, DAG heads, and seal markers are byte-identical across a prune run (ctest)
- [x] **GATE-03**: Seal markers (`<cid>sealed`) are never erased — they are the traversal terminator that prevents re-fetch of pruned chains
- [x] **GATE-04**: Outgoing `previous_hash` resolution survives pruning of the node's own frontier transaction (cert-slot based resolution; certificates are never pruned) — must land before the first real prune
- [ ] **GATE-05**: Boot-time UTXO assembly/checkpoint rebuild treats a missing pruned transaction as not relevant — skips it and completes boot without failure, and never resurrects spent outputs
- [x] **GATE-06**: Pruning config knobs live beside `node_type` in `sgns_config.json`, default off, resolved values INFO-logged

### Pruned-Node Survival

- [ ] **SURV-01**: Restart matrix on a pruned node passes — READY with zero pruned-CID fetch attempts, new-tip sync, spend/receive, and correct outgoing nonce chain
- [ ] **SURV-02**: Local history queries answer "history pruned" (distinct from not-found); peer requests get clean `RS_NOT_FOUND`/`CID_NOT_FOUND` refusal with prompt requester failover — both tested against a pruned node
- [ ] **SURV-03**: A fresh light node bootstraps through a mesh whose peers have pruned the history it needs (full node serves) — E2E

### Reclamation & Observability

- [ ] **RECL-01**: Deleted bytes actually leave the disk — periodic light-node-only `CompactRange` (`bottommost_level_compaction = kForce`) scheduled off the hot path
- [ ] **RECL-02**: CI gate proves real reclamation — measured SST/`du` bytes before-vs-after a prune+compaction cycle (not key counts)
- [ ] **RECL-03**: Per-cycle observability log — txs examined/erased, bytes reclaimed, watermark advance
- [ ] **RECL-04**: Prune status is queryable — watermark and last-cycle stats

## v2 Requirements

Deferred to future milestones. Tracked but not in current roadmap.

### History Services

- **HIST-01**: On-demand historical fetch from full peers (getblockfrompeer analog)
- **HIST-02**: UTXO-snapshot bootstrap for fresh/pruned nodes (assumeutxo analog)
- **HIST-03**: Disk-budget-driven automatic GC mode (IPFS-style watermark)

### Retention Policy

- **RTN-01**: Retention policy for non-transaction data classes (proof records, registration records — each belongs to its owning module's future decision)
- **RTN-02**: Seal-marker lifecycle/cleanup (markers grow unboundedly; v1.2 keeps them all — monitor and revisit)
- **RTN-03**: Receive-time watermark drop (skip storing pre-watermark deltas at ingress) — needs its own correctness research vs head reachability

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Pruning on full/archive nodes | Full/archive are the complete-history sync/audit source by design (PROJECT.md key decision) |
| Light nodes serving historical sync to peers | Pruned nodes must not masquerade as history servers (Bitcoin BIP-159 precedent); full nodes serve |
| Pruning non-transaction CID data (processing results, SecureCRDT values, certificates, proofs) | Transactions only this milestone; each other data class belongs to its owning module's future decision |
| Spent-only erasure without the depth/finality window | Spent-ness is mutable CRDT state; DAG conflict resolution may still need recent transactions (stricter than Bitcoin deliberately) |
| Value-only erasure (CRDT entries without raw block deletion) | Reclaims nothing meaningful — raw blocks are the bulk of the footprint |
| CRDT tombstone deletion as the pruning mechanism | `DeleteKey`/`DeleteTransaction` publish network-wide tombstones that would erase history on full/archive nodes too — pruning is a local operation |
| Vendored ipfs-lite-cpp changes (e.g. `removeSeal`) | Zero vendored changes required; seal markers must be retained anyway |
| Populating `spent_epoch` — remains dead schema (wall-clock-adjacent; D-02 forbids wall-clock semantics). `spent_by_txid` is no longer out of scope: D-02 Candidate B makes it the spend-recency carrier, populated at `UTXOManager::ConsumeUTXOs` (Phase 17) | Candidate A (per-key CRDT priority on CONSUMED entries) failed verification — CONSUMED entries are raw `/utxo/` RocksDB puts outside the CRDT namespace, so no priority exists; the narrow Candidate B amendment is D-02-authorized |
| New storage dependencies, column-family rework, compaction filters | RocksDB 9.4 + vendored ipfs-lite-cpp already provide the full API surface (STACK.md) |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| PRUNE-01 | Phase 17 | Complete |
| PRUNE-02 | Phase 16 | Complete |
| PRUNE-03 | Phase 17 | Complete |
| PRUNE-04 | Phase 18 | Pending |
| PRUNE-05 | Phase 18 | Pending |
| GATE-01 | Phase 17 | Complete |
| GATE-02 | Phase 20 | Pending |
| GATE-03 | Phase 16 | Complete |
| GATE-04 | Phase 17 | Complete |
| GATE-05 | Phase 19 | Pending |
| GATE-06 | Phase 17 | Complete |
| SURV-01 | Phase 19 | Pending |
| SURV-02 | Phase 19 | Pending |
| SURV-03 | Phase 19 | Pending |
| RECL-01 | Phase 20 | Pending |
| RECL-02 | Phase 20 | Pending |
| RECL-03 | Phase 18 | Pending |
| RECL-04 | Phase 18 | Pending |

v2 (future) requirements — no phase mapping by design:

| Requirement | Phase | Status |
|-------------|-------|--------|
| HIST-01 | — (v2) | Deferred |
| HIST-02 | — (v2) | Deferred |
| HIST-03 | — (v2) | Deferred |
| RTN-01 | — (v2) | Deferred |
| RTN-02 | — (v2) | Deferred |
| RTN-03 | — (v2) | Deferred |

**Coverage:**
- v1 requirements: 18 total
- Mapped to phases: 18/18 (Phase 16: 2 · Phase 17: 5 · Phase 18: 4 · Phase 19: 4 · Phase 20: 3)
- Unmapped: 0

---
*Requirements defined: 2026-10-15*
*Last updated: 2026-10-16 — Phase 16 complete (PRUNE-02, GATE-03); v2 IDs added to traceability as Deferred*
*Last updated: 2026-10-19 — Phase 17 amendment (D-02 Candidate B): `spent_by_txid` now in scope as the spend-recency carrier populated at `UTXOManager::ConsumeUTXOs`; `spent_epoch` stays out of scope*
*Last updated: 2026-10-19 — Phase 17 complete (PRUNE-01, PRUNE-03, GATE-01, GATE-04, GATE-06); substance verified in 17-VERIFICATION.md*
