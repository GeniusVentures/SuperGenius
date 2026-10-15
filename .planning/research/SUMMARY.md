# Project Research Summary — v1.2 Light-node Transaction Pruning

**Project:** SuperGNUS milestone v1.2 — Light-node transaction pruning (sliding-window disk reclamation)
**Domain:** Physical erasure + real disk-space reclamation on a C++17 UTXO+DAG blockchain light node (CRDT/IPFS persistence)
**Researched:** 2026-10-15
**Confidence:** HIGH (all four research files verified claims against this repository's source; the three researcher disagreements were re-verified against the cited lines during synthesis — see Resolved Disagreements)

## Executive Summary

SuperGNUS light nodes store every transaction ever broadcast (they subscribe to the full tx stream unconditionally), and v1.2 bounds that growth by physically erasing spent transaction history older than a finality window. Production precedent is uniform (Bitcoin prune mode, geth state pruning, Nano's pruned-ledger design, IPFS pin-based GC): bound erasure by a safety margin, keep the spendable state inviolate, make the pruned node answer "history pruned" instead of "not found", and never let a pruned node masquerade as a history server. SuperGNUS's dual criterion — **all outputs genuinely spent AND depth/finality window elapsed** — is deliberately stricter than Bitcoin's depth-only rule because spent-ness here is mutable CRDT state and DAG conflict resolution may still need recent transactions. Critically, pruning must be a **local RocksDB operation**: the existing CRDT delete path (`DeleteKey`/`DeleteTransaction`) publishes network-wide tombstones that would erase history on full/archive nodes too.

The converged architecture story, verified against source: the node has **one shared RocksDB** (single column family) wrapped twice — the CRDT datastore and the ipfs-lite block store are two facades over the same DB handle (`src/crdt/globaldb/globaldb.cpp:322` creates the ipfs-lite datastore from `m_datastore->getDB()`). Erasing one transaction therefore means deleting, ideally in one atomic `WriteBatch`: the raw CID-keyed block bytes (the `removeNode(CID)` chain already exists end-to-end and is currently unused), the four CRDT set entries (`/crdt/s|t/<key>/<cid>`, `/crdt/k/<key>/v|p`), and the node-local tx→CID map entry (which **already exists** — `StoreTransactionCID`/`GetTransactionCID`, `TransactionManager.cpp:4322-4345, 4433-4458` — and needs a deletion lifecycle, not a replacement). Two invariants carry most of the correctness weight: **seal markers (`<cid>sealed`) are never erased** — they are the traversal terminator that stops a pruned chain from triggering doomed re-fetch storms on the single DAG worker (the archive-replication flake shape) — and **a delta block is only erased when every element it carries is prune-eligible**, because deltas are multi-element (batched sends commit into one DAG node). The reverse (CID→elements) direction should be computed at prune time by parsing the block (`GetNodeWithoutRequest` + `GetDeltaFromNode`, pattern already in `GetLocalDeltaKeyValues`), not maintained as a second persistent index.

The top risks, in order: (1) the **fetch-resurrect loop** — physical erasure breaks CRDT merge idempotence, so re-delivered pruned deltas re-create erased keys unless root processing gains an `isResolved`/pruned guard (root-job creation today has none, `crdt_datastore.cpp:774-818`); (2) **erasing a multi-element block** on the strength of one eligible tx, destroying other accounts' data in the same delta; (3) **erasing the node's own nonce-frontier transaction**, which breaks `previous_hash` chain resolution on cold start (this exact divergence class already caused a deterministic four-platform CI failure); (4) **restart/recovery paths written under the "history never disappears" assumption** — especially the checkpoint-mismatch clear-and-rebuild that would resurrect spent outputs from pruned history (the sharpest restart-safety edge); and (5) **RocksDB not actually shrinking** — deletes are tombstones; space returns only via `CompactRange` with `bottommost_level_compaction = kForce`, so the milestone's success metric must be measured bytes, not key counts. All five have concrete, source-verified mitigations baked into the phase plan below.

## Key Findings

### Resolved Disagreements (verified against source during synthesis)

The four researchers disagreed on three points. Each was settled by reading the cited lines; citations win over prose.

1. **One shared RocksDB vs two separate DBs — ONE DB.** Verified: `src/crdt/globaldb/globaldb.cpp:322-329` — `IpfsRocksDb::create( m_datastore->getDB() )`, then `RocksdbDatastore(ipfsDBResult.value())`: the ipfs-lite block store is constructed from the CRDT datastore's own DB handle; both wrappers are single-column-family. STACK and ARCHITECTURE are correct; PITFALLS' "two databases" claim (Pitfall 7, Integration Gotchas, checklist) is wrong on topology. **Consequences:** a single `WriteBatch` can atomically erase CID block + CRDT entries + map entry (no cross-DB coordination); a single whole-DB `CompactRange` pass reclaims everything; PITFALLS' "reclaim both DBs" checks collapse to one. The functional point behind PITFALLS' error survives unchanged: neither wrapper exposes compaction today, deletes don't shrink files, and the success metric must be measured bytes.
2. **Where the tx→CID map lives — it already exists; extend it.** Verified: `TransactionManager.cpp:4322-4345` (`StoreTransactionCID` — raw datastore `put` at the bare tx path, outside the `/crdt` namespace, node-local, never broadcast, populated from the already-firing put hook for local *and* remote elements) and `:4433-4458` (`GetTransactionCID`, already consumed by `AccountMessenger`). ARCHITECTURE is correct; STACK's proposed new `/prune/txcid/` forward prefix would create a second source of truth. **Reconciled recommendation:** keep the existing forward map and add (a) a deletion lifecycle (entries currently outlive `DeleteTransaction` and would outlive pruning) and (b) a minimal pruned-record so history queries can return PRUNED rather than NOT_FOUND (PITFALLS 9). The reverse direction (CID → element set) that PITFALLS rightly demands as a safety gate should be **computed at prune time by parsing the block** (ARCHITECTURE Pattern 3 reuses `GetNodeWithoutRequest`/`GetDeltaFromNode`; PITFALLS' "computed from the elems namespace at prune time" converges on the same answer) — no persistent reverse index in v1 (DRY: the block is the source of truth for what it contains). STACK's `/prune/` node-local prefix idea remains valid for *new* pruner-private state (pruned-marker records, prune cursor), following the `CRDTWorkJournal` write-direct-to-shared-DB pattern.
3. **Build order — converged below (5 phases).** STACK's erasure-vs-reclamation split maps to phases 1–4 vs 5; PITFALLS' P1→P5 ordering holds except its "P1 bidirectional map" phase becomes "harden the existing map" (the map exists; the bidirectional *safety gate* is the block-parse eligibility check); ARCHITECTURE's 6 steps compress into the same 5 phases with its two hard ordering constraints preserved: **block-eligibility knowledge before any block deletion**, and **the own-frontier/nonce-anchor survival fix before the first real prune**.

One additional correction adopted: the transaction namespace is `/bc-<netid>/tx/<hash>` (`TRANSACTION_BASE_FORMAT = "/bc-%hu/"`, `TransactionManager.hpp:433`); PITFALLS' `/chain/<net>/tx` references are stale naming. The pruner's prefix allowlist must use the verified constant.

### Recommended Stack

**No new dependencies.** RocksDB 9.4 (vendored, `thirdparty/rocksdb`) and the vendored ipfs-lite-cpp fork (workspace-level `/Users/henriqueklein/gnus/thirdparty/ipfs-lite-cpp`, not `3rdparty/`) already provide 100% of the required API surface. Everything runs through `getDB()` accessors both wrappers already expose.

- **Raw block erasure:** existing dormant `removeNode(CID)` chain (`dagsyncer.hpp` → `graphsync_dagsyncer.cpp:461-464` → `MerkleDagServiceImpl` → `RocksdbDatastore::remove` → `db->Delete`) — zero production callers today, so the pruner is its first consumer with no behavioral risk. It deletes only the CID-bytes key; the `<cid>sealed` marker (different key encoding) survives by construction — exactly what a pruned light node needs.
- **CRDT entry erasure:** new local batch-erase primitive inside `CrdtSet` (it owns the `/s /t /k` key layouts; no other module may construct those keys) via the SuperGNUS wrapper's plain `remove`/`BufferBatch` — deliberately NOT `GlobalDB::Remove`/`DeleteKey`, which is the broadcast-tombstone path.
- **Space reclamation:** batched `Delete`/optional `DeleteRange` + periodic light-node-only `CompactRange` over the whole DB (`bottommost_level_compaction = kForce`, `allow_write_stall = false`, scheduled off the hot path); telemetry/asserts via `GetApproximateSizes`. Rejected alternatives (with rationale in STACK.md): automatic compaction alone, `DeleteFilesInRange` as primary tool, compaction filters, column-family rework, any new storage engine.
- **Vendored change: zero required.** STACK's optional `removeSeal(CID)` addition is **resolved to NOT do it in v1.2**: all three safety-relevant findings converge on never erasing seal markers (they are the pruning enabler), so there is nothing to clean. Marker growth is accepted, monitored, and documented.
- **Config:** pruning knobs in `sgns_config.json` beside `node_type` (already read in `LoadSgnsConfig`), default off, INFO-log resolved values per the config convention.

### Expected Features

**Must have (table stakes, P1 priority — from FEATURES.md):**
- Erasure eligibility: all outputs spent AND depth/finality window elapsed, evaluated through owning modules
- Unspent UTXO set never erased (gate + test invariant; `/utxo`, `/utxo-checkpoint` on the never-erase list)
- Two-level erasure: CRDT entries AND raw CID block (value-only erasure reclaims nothing meaningful)
- tx→CID bookkeeping: durable, extended with deletion lifecycle (map already exists — see above)
- Light-node-only pruning; full/archive never erase (gate on `NodeType == Light`, fail-closed, ctest-asserted)
- Crash-consistent, idempotent prune cycle (intent → erase value → erase block → mark done; restart-safe to a fixed point) — highest-risk table-stakes item
- Re-arrival handling: sweep-based re-prune of resurrected entries (add-wins OR-Set semantics make this mandatory, not optional)
- Explicit "history pruned" answer for local queries; clean `CID_NOT_FOUND`/`RS_NOT_FOUND` refusal for peer requests (existing error paths suffice — must be tested against a pruned node)
- Non-tx CID data provably untouched (namespace allowlist + ctest: certificates, SecureCRDT policy, processing results, heads, seal markers)
- Pruned node stays operational: restart, tip sync, spend/receive green in multi-node production-path tests — the acceptance bar
- Config knobs with no-prune default; per-cycle observability log (count, bytes, watermark)

**Should have (differentiators, P2):** queryable prune status introspection; measured-bytes reclamation reporting (promoted into the roadmap's final phase since the milestone's headline claim is *real* disk reclamation — see Phase 5); background rate-limited batching with batch caps (build the knobs from the start); re-prune watermark/drop-at-receive (needs its own correctness research); finality-vs-age window policy flag.

**Defer (v2+):** on-demand historical fetch from full peers (getblockfrompeer analog); UTXO-snapshot bootstrap (assumeutxo analog); disk-budget auto-GC mode; retention policy for non-tx data classes (each belongs to its owning module's future decision).

**Anti-features (decided against):** light nodes serving historical sync; pruning on full/archive nodes; pruning non-tx data; spent-ness-only erasure without the window; value-only erasure; depth sweeps ignoring DAG reachability; IPFS-style disk-watermark auto-GC; whole-repo global GC; forced compaction every cycle.

### Architecture Approach

A new owning module — `TxPruner` in `src/transaction/`, beside `TransactionManager` — owns the pruning *rule* (when is a tx erasable) and orchestrates; the erasure *primitives* live in the layers that own the key shapes (`CrdtSet` owns `/s /t /k/v /k/p`, `GraphsyncDAGSyncer` owns block storage, `TransactionManager` owns the tx→CID map). `TxPruner` must not be a `GeniusNode` lambda (god-class facade already flagged in CONCERNS.md). Three patterns, all source-verified:

1. **Local physical erasure** — bypass the CRDT write path entirely; one RocksDB batch per prune step; never publish a tombstone.
2. **Seal-marker-retaining block removal** — `remove(cid)` only; sealed CIDs are never re-traversed (`graphsync_dagsyncer.cpp:752-765` skips resolved children), so a pruned ancestor does not trigger re-fetch. Assert `isResolved` before and after erase.
3. **Multi-element block eligibility via block parse** — read the block before deleting it and require every element key prune-eligible; the block is the source of truth for what it contains.

**Erase flow order (idempotent, restart-safe):** (1) delete CRDT value entries for the tx key → (2) look up CID via existing map → (3) parse block, verify all elements eligible, delete block with guards (`!IsHead ∧ !pending/active ∧ !in LRU`) → (4) delete the map entry last. While the block exists, recovery/re-merge can restore set state; deleting the block first would leave set entries that no longer replay. Map entry last so an interrupted run can still find the CID on re-run.

**Eligibility inputs, all existing:** `IsOutPointGenuinelyConsumed` (NOT bare `IsOutPointConsumed` — `ConsumeUTXOs` synthesizes zero-amount CONSUMED tombstones for unknown outpoints, a false-spend trap); depth = heads `maxHeight` − stored priority at `/crdt/k/<key>/p` (no block reads needed; `spent_epoch`/`spent_by_txid` are never populated at runtime — do not build on them); own-frontier exemption or cert-slot previous-hash resolution (certificates embed the winning tx hash and are never pruned — the pruning-safe pattern already exists in `GetRegisteredOutgoingPreviousHash`).

**Components:** NEW `src/transaction/TxPruner.{hpp,cpp}`. MODIFIED: `TransactionManager` (expose enumeration/tracked-state queries, map deletion, tracked-map cleanup via the `ProcessDeletion` pattern), `CrdtSet` (+local erase primitive), `CrdtDatastore`/`GlobalDB` (local-erase surface, expose `maxHeight`), `GraphsyncDAGSyncer` (thin: guarded `removeNode` exposure), `GeniusNode` (Light-only construct/start/stop, config), `AccountMessenger` (small: stays correct once map entries are deleted). UNTOUCHED: vendored ipfs-lite-cpp, `UTXOManager` internals (query API only), `/utxo*` keyspace.

### Critical Pitfalls (top 5 of 12 — full catalog in PITFALLS.md)

1. **Fetch-resurrect loop (Pitfalls 1, 3, 6)** — physical erasure breaks CRDT merge idempotence; re-delivered pruned deltas (restart head exchange, lagging-peer re-broadcast, retry queues) re-fetch and re-merge erased data. Avoid: never erase seal markers; add an `isResolved`/pruned guard at root-job creation (none exists today, `crdt_datastore.cpp:774-818`); audit every recovery consumer to treat pruned CIDs as terminal. Test: re-broadcast pruned root + restart → zero fetches, zero re-created elements.
2. **Erasing a multi-element block (Pitfall 2)** — one delta block carries a vector of elements (batched puts commit as one DAG node); erasing on one eligible tx destroys other accounts' data in the same delta. Avoid: block-parse eligibility gate before any block deletion; erase only when every element in the block is eligible. Note light nodes store raw blocks containing elements they never applied (`FilterElementsOnDelta`) — parse the block, not "what I applied".
3. **Nonce-anchor erasure (Pitfall 5)** — the node's own last-confirmed-nonce tx is a live dependency of the next outgoing tx; cold-start previous-hash recovery scans persisted txs. Avoid: own-frontier exemption or generalized cert-slot resolution — must land **before** the first real prune. This divergence class already caused a deterministic four-platform CI failure once.
4. **Restart recovery expecting removed history (Pitfalls 4, 6)** — boot re-fetches creating txs of READY outpoints; checkpoint-mismatch clear-and-rebuild replays `QueryTransactions` and can resurrect spent outputs from pruned history (double-spend window). Avoid: gate the pruner on `State::READY` + empty `missing_tx_hashes_` + settle window; "history pruned below depth D" marker so the rebuild path refuses instead of resurrecting; window in finality epochs, never wall-clock; re-verify spent-ness under lock immediately before each erase.
5. **Wrong node/data pruned + disk never shrinks (Pitfalls 8, 7)** — gate on the `NodeType` enum directly (fail-closed), constrain erasure to a `/bc-<netid>/tx/` prefix allowlist asserted in code and ctest; reclaim via explicit `CompactRange` and gate the milestone claim on measured bytes (SST sizes / `du`), not key counts.

Cross-cutting: never trigger prune work from CRDT element callbacks (they fire pre-batch-commit — proven invariant, cert-receipt memory); every pruning test uses a fresh temp DB with three-part assertions (present-before, absent-after in both stores, seal-marker-still-present) plus a negative control (Pitfalls 10, 11).

## Implications for Roadmap

Converged phase structure — respects every dependency claim from all three orderings (block-eligibility knowledge before any block deletion; own-frontier fix before first real prune; reclamation proof last). Phase names follow PITFALLS' P1–P5 vocabulary with content corrected by ARCHITECTURE's findings.

### Phase 1: Erasure primitives & bookkeeping hardening (no behavior change)
**Rationale:** everything else depends on the primitives and the map; both are pure additions with zero production callers today (`removeNode` is dormant), so this is the safest possible start and makes wrong deletion impossible by API shape.
**Delivers:** `CrdtSet` local batch-erase primitive (owns `/s /t /k/v /k/p`); guarded block-erase exposure on `GraphsyncDAGSyncer` (reuse `removeNode`; guards `IsHead`/`IsRootCIDPendingOrActive`/`IsCIDInCache`; seal marker retained by construction); `maxHeight` exposure; tx→CID map deletion lifecycle + tracked-map cleanup; fresh-DB fixture rules (three-part assertions + negative control) from the first erasure test onward.
**Addresses:** table-stakes two-level erasure mechanics, crash-consistency substrate.
**Avoids:** Pitfalls 3 (seal invariant baked into the API), 2 (parse gate available), 11 (fixture rules), and the DeleteCIDBlock-is-LRU-only trap (`graphsync_dagsyncer.cpp:855-862`).

### Phase 2: Eligibility, policy & gating (dry-run — evaluates, never erases in production)
**Rationale:** policy checks call erasure primitives whose semantics must already exist (Phase 1); the own-frontier survival fix and the multi-element gate are correctness dependencies of the first real prune and must land before it; dry-run mode proves the evaluator on live data with zero risk.
**Delivers:** eligibility evaluator (all outputs `IsOutPointGenuinelyConsumed` ∧ depth window via priority-vs-maxHeight, optional certificate tightening ∧ not own-frontier); multi-element block-parse gate; **own-frontier survival fix** (cert-slot previous-hash resolution generalized from `GetRegisteredOutgoingPreviousHash`, or own-nonce exemption — pick one); `NodeType::Light` fail-closed gate + `/bc-<netid>/tx/` allowlist; PRUNED-vs-NOT_FOUND error surface; dry-run logging.
**Addresses:** eligibility, light-only gating, config knobs (no-prune default), "history pruned" error.
**Avoids:** Pitfalls 2 (gate), 4 (window + genuine-spend + re-verify-under-lock), 5 (anchor fix), 8 (role/namespace gates), 9 (error surface), 10 (scheduler design: committed state only, proper locks).

### Phase 3: TxPruner service & first real prune
**Rationale:** the service composes verified primitives with verified policy; this is the first phase that erases production data, which is why Phases 1–2 are strict prerequisites.
**Delivers:** tick loop gated on Light ∧ `State::READY` ∧ empty `missing_tx_hashes_` ∧ settle window; the 4-step idempotent erase flow (value → CID lookup → block-eligibility+delete → map entry, in that order); re-delivery guard (skip fetch+merge for resolved/pruned CIDs at root processing) + sweep-based re-prune of resurrected entries; chunked first-prune backfill with bounded per-tick work; `GeniusNode` wiring + config (default off); per-cycle observability log.
**Addresses:** crash-consistent idempotent cycle, re-arrival handling, light-node-only activation.
**Avoids:** Pitfalls 1 (resurrect guard), 10 (race implementation: own schedule, locks, debug+release timing), performance traps (collect-then-delete, bounded batches, compaction off DAG workers).

### Phase 4: Pruned-node survival & recovery hardening
**Rationale:** survival tests are the preconditions that make the E2E and disk claims honest (PITFALLS' P4-before-P5 rationale); every existing recovery path was written under "history never disappears" and must be audited now that erasure exists.
**Delivers:** restart matrix (prune → restart → READY with zero pruned-CID fetches → tip sync → spend/receive → new tx `previous_hash` correct); checkpoint-rebuild guard ("history pruned below depth D" marker — refuse clear-and-rebuild, re-sync from full node instead); recovery-path audit (root jobs, `RetryDueFailedRoots`, `RetryStalledDeltas`, work-journal recovery, `missing_tx_hashes_` — all treat pruned CIDs as terminal); `AccountMessenger` correctness after map deletion; peer-serving verification (immediate `RS_NOT_FOUND`/`UNRESOLVED_LINK`, prompt requester failover); fresh-light-node-through-pruned-mesh bootstrap E2E.
**Addresses:** "pruned node stays operational" acceptance bar; clean refusal semantics.
**Avoids:** Pitfalls 6, 12, and the verification halves of 1, 4, 5.

### Phase 5: Reclamation proof & E2E hardening
**Rationale:** reclamation is deliberately last (STACK's split): it is meaningless until erasure is correct, compaction is IO-heavy and must not compete with debugging of correctness, and the measured-bytes gate is the milestone's headline claim — it deserves a dedicated verification phase.
**Delivers:** scheduled `CompactRange` (whole-DB, `bottommost_level_compaction = kForce`, off the hot path, light-node-only — one DB only, per the resolved topology); measured-bytes CI gate (`GetApproximateSizes`/SST totals before-vs-after, not key counts) + reclaimed-bytes in the cycle log; window-edge race fault tests (spend arriving during a prune cycle, debug+release timing); full/archive 100%-retention ctest; non-tx-namespace immutability ctest (certificates, policy values, heads, seal markers byte-identical across a prune run).
**Addresses:** "disk actually reclaimed" differentiator; the "looks done but isn't" checklist gates.
**Avoids:** Pitfalls 7 (measured bytes + explicit compaction), 8 (the two ctest gates), 11 (final fixture enforcement).

### Phase Ordering Rationale

- **Primitives → policy → service → survival → reclamation** mirrors the dependency chain: policy calls primitives; the service composes policy+primitives; survival tests need a pruned node; reclamation proof needs correct erasure.
- **The map hardening rides with Phase 1** (not a standalone "bookkeeping" phase as PITFALLS proposed) because the forward map already exists — the real P1 work is the deletion lifecycle, which belongs beside the primitives it will be batched with.
- **The block-parse gate (Phase 2) strictly precedes any block deletion (Phase 3)** — PITFALLS' "bidirectional map before erasure" constraint, satisfied via compute-at-prune-time instead of a persistent index.
- **The own-frontier fix (Phase 2) strictly precedes the first real prune (Phase 3)** — ARCHITECTURE's correctness-dependency constraint.
- **Grouping follows the one-owner-per-domain invariant:** each phase's work is attributable to specific owning modules (CrdtSet/DAGSyncer; TransactionManager policy; TxPruner service; recovery consumers; telemetry/compaction), keeping one rule change inside one module.

### Research Flags

Phases likely needing deeper research during planning (`--research-phase`):
- **Phase 2:** design decisions concentrate here — own-frontier mechanism choice (cert-slot generalization vs own-nonce exemption), window definition (height vs certificate-based epochs), lock/re-verify protocol shared with UTXOManager. The nonce/cert interaction is subtle and previously caused CI divergence.
- **Phase 3:** re-delivery guard placement (root-job creation vs. fetch vs. traversal — each has different blast radius); how the guard reads "pruned" state (seal markers retained means `isResolved` stays true for pruned CIDs — verify that is sufficient, or a pruned-set record is needed).
- **Phase 4:** the recovery-path audit spans five consumers with different state machines; the checkpoint-rebuild guard mechanism (marker vs refuse-and-resync) needs a decision; the checkpoint-mismatch path is the sharpest restart-safety edge.

Phases with standard patterns (skip research-phase):
- **Phase 1:** every API verified end-to-end with file:line citations; pure mechanical addition.
- **Phase 5:** compaction recipe fully specified in STACK.md (options, caveats, telemetry); test-gate patterns established in v1.1 Phase 12.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Every API claim verified against vendored RocksDB 9.4 and ipfs-lite-cpp source in this workspace; external mechanics corroborated by official RocksDB docs/issues |
| Features | HIGH | External claims from official docs (Bitcoin Core, geth, IPFS, BIP-159); scope grounded in PROJECT.md key decisions |
| Architecture | HIGH | Every claim file:line verified in-repo; the two disputed citations re-verified during synthesis (globaldb.cpp:322, TransactionManager.cpp:4322) |
| Pitfalls | HIGH (substance) | All 12 pitfall mechanisms verified against current source; two factual slips corrected during synthesis (two-DB topology; `/chain/` namespace) — neither affects any mitigation |

**Overall confidence:** HIGH

### Gaps to Address

Open questions that must become plan-phase decisions:

- **Re-delivery guard mechanism (Phase 3):** exact placement (root-job creation `crdt_datastore.cpp:774-818` vs fetch layer) and state source (is retained `isResolved` sufficient as the "already processed/pruned" signal, or is a durable pruned-CID record needed?). PITFALLS' dedicated test (re-broadcast → zero fetches) defines done.
- **Own-frontier mechanism (Phase 2):** generalize certificate-slot resolution (certs are never pruned and embed the winning tx hash) vs own-nonce exemption (last-K anchor per own address). Recommend cert-slot generalization as the durable fix; exemption as the fallback if it proves too invasive.
- **Checkpoint-rebuild guard (Phase 4):** marker + refuse vs automatic full-node re-sync; the clear-and-rebuild path (`InitializeUTXOs:3176-3193`) must not resurrect spent outputs — pick the mechanism and its failure mode.
- **Seal-marker lifecycle beyond v1.2:** markers grow unboundedly (one string key per processed root CID). v1.2 keeps them all (correct choice); the cleanup question (STACK's open question about `HandleCIDBroadcast`/`IsRootCIDPendingOrActive` tolerating a missing block+marker) is deferred — document expected size and monitor.
- **Proof records (`/bc-<netid>/proof/<hash>`) and registration records (`reg/<src>`):** proofs are out of milestone scope per PROJECT.md non-goals (treat as retained); confirm registrations are similarly excluded from the allowlist during Phase 2 planning.
- **First-prune backfill sizing:** chunk size / cursor design for a large backlog at first enable (ARCHITECTURE's scaling note: persistent cursor under a node-local `/prune/` prefix if prefix scans get hot — do not pre-build; measure first).
- **`spent_epoch`/`spent_by_txid` are dead schema:** never populated at runtime — the window must come from height/priority or certificates, and populating them is a temptation to resist in this milestone.

## Sources

### Primary (HIGH confidence — verified in this workspace)
- `src/crdt/globaldb/globaldb.cpp` (one DB, two facades :322-329; namespace "crdt" :360; backups :379-477)
- `src/transaction/TransactionManager.{hpp,cpp}` (tx→CID map :4322-4345, :4433-4458; namespace constant hpp:433; nonce ladder :1603-1867; cert-slot resolution :1688-1726; InitializeUTXOs :3148-3249; SendTransactionItem batching :2099-2126)
- `src/crdt/impl/crdt_datastore.cpp` (root-job creation :774-818; UpdateCRDTHeads :2111+; GetLocalDeltaKeyValues :2280-2302; retry ladder :541-546)
- `src/crdt/impl/graphsync_dagsyncer.cpp` (removeNode :461-464; resolved-skip traversal :752-765; DeleteCIDBlock LRU-only :855-862)
- `src/crdt/impl/crdt_set.cpp` (multi-element PutElems :669-735; key layouts :462-496; GetPriority :498-519)
- `src/account/UTXOManager.{hpp,cpp}` (ConsumeUTXOs :265-365; genuine-consumed discriminator hpp:385-397; `/utxo*` keyspace)
- Vendored: `thirdparty/ipfs-lite-cpp` (seal marker key format + remove semantics, `datastore_rocksdb.cpp`; UNRESOLVED_LINK, `merkledag_service_impl.cpp:44-63`), `thirdparty/rocksdb/include/rocksdb/{db.h, options.h, convenience.h}` (CompactRange :1495, DeleteRange :549, GetApproximateSizes :1436)
- Full citation lists: STACK.md, ARCHITECTURE.md, PITFALLS.md source sections

### Secondary (MEDIUM-HIGH confidence — official docs, corroborated)
- Bitcoin Core prune mode + BIP-159 (bitcoin.org, bitcoincore.org RPC docs); geth sync-modes/history-pruning (geth.ethereum.org); IPFS/Kubo GC (docs.ipfs.tech); Nano whitepaper (pruned ledger design)
- RocksDB space-reclamation mechanics (GH issue #8041, DeleteRange blog, Manual Compaction wiki); Lotus SplitStore docs; ScyllaDB tombstone-GC / data-resurrection literature; Almeida et al. delta-CRDTs (arXiv:1410.2803); go-graphsync partial-failure semantics

---
*Research completed: 2026-10-15*
*Ready for roadmap: yes*
