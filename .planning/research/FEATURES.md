# Feature Research

**Domain:** Light-node transaction-history pruning (sliding-window disk reclamation) on a C++17 UTXO+DAG blockchain with CRDT/IPFS persistence
**Researched:** 2026-10-15
**Confidence:** HIGH (external-system claims verified against official docs; SuperGenius claims grounded in PROJECT.md and source checks)

## How Production Systems Do This (Evidence Base)

Every mature system that reclaims transaction-history disk follows the same four-part shape. The feature landscape below is derived from these precedents, mapped onto SuperGenius's existing seams.

**Bitcoin Core prune mode** (`-prune=N`): validates the full chain during IBD, then deletes raw block+undo data beyond a reorg-safety margin. Keeps the UTXO set (chainstate) *completely* — unspent outputs never depend on block files. `N` is a MiB budget, minimum 550; `prune=0` disables. A pruned node is still a fully validating node; it just stops being a history *server*: it signals `NODE_NETWORK_LIMITED` (BIP-159, serves at least the last 288 blocks / ~2 days), never `NODE_NETWORK`, and refuses `-txindex`, `-rescan`, and `importwallet`. Observability: `getblockchaininfo` exposes `"pruned": true` and `"pruneheight"`; disk shrinks only as new blocks age past the window. Escape hatches: `getblockfrompeer` (v25+) fetches individual missing blocks on demand; AssumeUTXO `loadtxoutset` (v28+) bootstraps from a UTXO snapshot with background history verification. Operators get a *distinguishable* "pruned, not corrupted" answer rather than a generic not-found. [bitcoin.org full-node guide; bitcoincore.org 30.0.0 RPC docs; BIP-159]

**Ethereum geth**: a full node keeps only the last 128 block states (~25.6 min at 12s blocks); older states are pruned but regenerable on demand from peers. `--gcmode archive` disables GC entirely (archive nodes keep ~12 TB of everything). Snap sync starts from a recent checkpoint and fetches recent state — a snap-synced node *can never become* an archive node because pre-checkpoint history was never stored. History pruning (block bodies/receipts) is a separate, newer knob. [geth.ethereum.org docs: sync-modes, historypruning]

**IPFS/Kubo GC**: `ipfs repo gc` removes *unpinned* blocks. A pin (recursive/direct/indirect) marks content ineligible for GC; anything the system still needs must be reachable from a pin. Auto-GC triggers at `StorageGCWatermark` (default 90% of `StorageMax`, default 10 GB) checked per `GCPeriod` (1h) when `--enable-gc` is set. Manual GC prints every removed CID — operators see exactly what was reclaimed. [docs.ipfs.tech how-to/kubo-garbage-collection]

**CRDT retention (OR-Sets and delta-CRDTs)**: deletion in an add-wins OR-Set is a *tombstone*, not erasure — `CrdtDatastore::Remove`/`CreateDeltaToRemove` (src/crdt/crdt_datastore.hpp:230) writes a remove-delta; nothing is physically freed. Tombstone GC in the literature is gated on *causal stability* (all replicas have seen the delta) — Almeida et al., delta-state CRDTs (arXiv:1410.2803). Production precedent: Yjs GCs deleted *content* but replaces it with lightweight markers retaining identity, because fully removing tombstones breaks convergence ("we can't garbage collect deleted structs while ensuring a unique order"); `doc.gc=false` exists for cases needing full retention. Yorkie ships an explicit tombstone-GC pipeline. The universal rule: **what a replica may erase is bounded by what late-arriving or offline peers might still reference.** [crdt.tech glossary; yjs GitHub/INTERNALS; Almeida et al.]

**Nano (closest architectural analog — block-lattice, per-account chains)**: the whitepaper designed pruning in from the start: "A node may either store the entire ledger or a pruned history containing only the last few blocks of each account's blockchain," with full-history nodes remaining the source for pruned nodes. This is exactly SuperGenius's Light vs Full/Archive split. [Nano whitepaper; docs.nano.org]

**Erasure-eligibility models compared:**

| System | Eligibility criterion | Protects |
|--------|----------------------|----------|
| Bitcoin | Depth only (reorg margin + budget) | UTXO set stored separately (chainstate), undo data kept with margin |
| geth | State-reachability from last 128 states | Current state; regenerable from peers |
| IPFS GC | Unpinned | Anything pin-reachable |
| CRDT tombstone GC | Causal stability / convergence safety | Ordering and conflict resolution |
| **SuperGenius (decided)** | **All outputs spent AND depth/finality window elapsed** | Unspent UTXO set, heads, non-tx CID data |

SuperGenius's dual criterion is *stricter* than Bitcoin's depth-only rule (spent-ness adds a safety margin for DAG conflict handling and catch-up sync, per the PROJECT.md key decision) and *more aggressive* than CRDT causal stability (light nodes are not obligated to keep data for peers they will never serve history to — that obligation stays with full/archive nodes, the Nano model).

## Feature Landscape

### Table Stakes (Users Expect These)

Features users/operators assume exist. Missing any = the pruner is unsafe or indistinguishable from corruption.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Erasure eligibility: all outputs spent AND depth/finality window elapsed | Bitcoin/geth/Nano all bound erasure by a finality margin; PROJECT.md key decision — spent-ness alone risks erasing data needed for conflict handling or catch-up sync | MEDIUM | Needs (a) a spent-ness query over *every* output of a tx (not just wallet-tracked ones) and (b) a finality/age watermark derived from the DAG tip. Existing dep: `UTXOManager` spend tracking |
| Unspent UTXO set is never erased | Bitcoin keeps chainstate inviolate; a pruner that can delete a spendable output is a consensus bug | LOW (as an invariant) / covered by above | Enforce as a gate + test invariant, not a separate mechanism |
| Two-level erasure: CRDT value AND raw ipfs-lite CID block | Value-only erasure leaves the larger raw-block RocksDB untouched (PROJECT.md key decision); IPFS reclamation semantics are block-level | MEDIUM | Existing seam: `GraphsyncDAGSyncer::removeNode(const CID&)` (src/crdt/graphsync_dagsyncer.hpp:99). CRDT `Remove` alone is a tombstone — physically erasing the value/delta is a new storage-layer operation |
| tx→CID bookkeeping map | The CID already arrives alongside the transaction key/value (PROJECT.md) but is unused; without the map, raw-block erasure cannot find its target | MEDIUM | Durable (survives restart), written transactionally with tx ingestion, sized with the tx population. Must be prunable itself once the tx is erased both levels |
| Prune is light-node-only; full/archive never erase | PROJECT.md key decision; mirrors Nano (pruned nodes exist only because full-history nodes remain the sync/audit source) | LOW | Gate on existing `is_full_node_` (derived from `node_type`, validated v1.0 Phases 1–2). Full/archive keep complete history by construction — add a test that asserts no prune path executes for them |
| Configurable window/depth knobs with safe default | Every system exposes one: `-prune=N` (MiB budget, Bitcoin), `--gcmode`/history-prune (geth), `StorageMax` (IPFS). Operators expect to trade retention vs disk | LOW | Follows the established config pattern (`sgns_config.json` for role-ish fields). Default should match today's behavior (no pruning) so deployed light nodes don't change character until opted in |
| Crash-consistent, idempotent prune cycle | An operator kills the node mid-prune; restart must neither corrupt state nor leak already-erased-but-unrecorded blocks. Bitcoin pruning is restart-safe by construction | MEDIUM-HIGH | Durable ordering: record erasure intent → erase CRDT value → erase CID block → mark done. Any crash point must be re-runnable to a fixed point. This is the highest-risk table-stakes item |
| Re-arrival handling (add-wins resurrection) | Add-wins OR-Set semantics: a late delta re-broadcast by a full peer re-adds what the pruner erased. A pruner that erases once and assumes permanence diverges from CRDT behavior | MEDIUM | MVP-acceptable: periodic idempotent sweep re-prunes re-arrivals. Better (P2): durable prune watermark so deltas older than the watermark are handled before fetch/store. Needs phase-level design — see flags |
| Explicit "history pruned" answer for local queries | Bitcoin distinguishes "pruned" from "never existed" (`pruneheight` in errors); a generic not-found is indistinguishable from corruption and generates operator panic | LOW-MEDIUM | Distinct error/message for tx-history queries hitting pruned data; do not return empty-success. Existing dep: `outcome::result<T>` error-enum convention |
| Clean refusal for peer requests to erased CIDs | BIP-159: a pruned node must not advertise what it cannot serve; graphsync requests for erased blocks must fail explicitly | LOW | Existing: `GraphsyncDAGSyncer::Error::CID_NOT_FOUND`. Verify graphsync failure paths return a clean NOT_FOUND, not a hang/retry storm |
| Pruned light node stays operational: restart, tip sync, spend/receive | PROJECT.md target; Bitcoin pruned nodes relay/validate normally. This is the acceptance bar for "correct" | MEDIUM (as verification cost) | Multi-node tests: prune → restart → sync new tips → spend/receive; unspent set intact. Existing dep: Phase 12-style production-path multi-node test apparatus |
| Non-transaction CID data untouched | PROJECT.md non-goal: certificates, SecureCRDT values, processing-grid results must survive the pruner. Erasing a certificate breaks canonical-finality invariants (e.g., the deferred-dispatch path) | LOW (as a scope gate) | Whitelist-by-key-prefix (transactions namespace only); explicit test that certificate/policy/processing keys survive a prune cycle |
| Prune observability: what ran, what was reclaimed | IPFS GC prints removed CIDs; Bitcoin exposes `pruned`/`pruneheight`; geth logs prune progress. Operators expect: enabled?, window, watermark, count/bytes reclaimed per cycle | LOW | Log line per cycle (count, bytes, watermark) at INFO; per-tx detail at DEBUG. Metrics-level surface can be P2 |

### Differentiators (Competitive Advantage)

Not required for correctness, but they separate a well-engineered pruner from a minimal one.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Prune status introspection (queryable, not just logs) | Bitcoin's `getblockchaininfo` `{pruned, pruneheight}` is the model — one call answers "am I pruned, since when, what's the watermark" | LOW-MEDIUM | A JSON-RPC field or dedicated status getter on the node facade; mirrors the existing INFO-log-everything config pattern |
| RocksDB space actually reclaimed after erase | Deleting LSM keys does not shrink files; without compaction the operator sees no disk drop and files a bug. Bitcoin sets this expectation (disk shrinks over time, not instantly) | LOW-MEDIUM | Periodic/after-batch `CompactRange` on the affected column families, or document the lag explicitly. Do not build a watermark-triggered auto-compactor in v1 |
| Background, rate-limited pruning that never stalls the node | IPFS GC is (in)famously stop-the-world; geth's `snapshot prune-state` is an offline-ish heavyweight pass. A pruner that runs as a low-priority periodic task with a batch cap avoids both | MEDIUM | Runs on existing Asio threading; batch size + interval knobs; yields to sync traffic. Directly serves the "stays operational" table stake |
| Re-prune watermark (drop-at-receive for pre-watermark deltas) | Turns O(re-arrivals) work into O(1): stale deltas never enter storage at all | MEDIUM-HIGH | Correctness-sensitive: must not drop deltas reachable from current heads. Defer to v1.x after the sweep-based loop is proven; flag for phase research |
| Configurable policy: finality-based vs age-based window | Different deployments have different reorg/conflict risk appetites; geth exposes multiple prune knobs for this reason | LOW | Two knobs max (`prune_depth`/`prune_age` or one with a mode flag) — resist a policy DSL |
| On-demand historical fetch from full peers (getblockfrompeer analog) | A pruned light node recovers a specific historical tx by asking a full node, instead of a full re-sync | MEDIUM-HIGH | Bitcoin shipped this years after prune mode (v25). Defer; requires peer-capability awareness |
| UTXO-snapshot bootstrap (assumeutxo analog) | Fresh light node starts from a snapshot of unspent outputs and verifies history lazily | HIGH | Far beyond this milestone; note as future direction only |

### Anti-Features (Commonly Requested, Often Problematic)

| Feature | Why Requested | Why Problematic | Alternative |
|----------|---------------|-----------------|-------------|
| Light nodes serving historical sync to peers | "It already has most blocks" | PROJECT.md non-goal. BIP-159 exists precisely because pruned peers answering history requests mislead others; SuperGenius light nodes are disk-constrained consumers, not history servers | Fail cleanly with "history pruned"/CID_NOT_FOUND; full/archive remain the sync sources |
| Pruning on full/archive nodes | Symmetry seems cleaner | Full/archive are the complete-history sync/audit source (Nano model); pruning them removes the network's recovery path. Also violates a decided PROJECT.md key decision | Role-gate the pruner on `is_full_node_==false`; assert with a test |
| Pruning non-transaction CID data (certificates, SecureCRDT policy, processing results) | "It's the same disk problem" | Certificates are the canonical-finality record; policy values are quorum-signed state; erasing them breaks restart recovery and audit — and each is its own ownership domain with different retention rules | tx-namespace-only whitelist; each other data class gets its own (future) retention decision in its owning module |
| Erase on spent-ness alone (no depth window) | "If it's spent, it's dead" | Decided against (PROJECT.md): recent spent txs are still needed for conflict handling and catch-up sync; a just-spent tx may be referenced by in-flight deltas | Keep the AND: all-spent AND window elapsed |
| Value-only erasure (CRDT remove, skip raw block) | It's one API call | `CrdtDatastore::Remove` is an OR-Set tombstone — reclaims nothing; and skipping the CID block leaves the bulk of raw-block RocksDB untouched | Two-level erasure via tx→CID map + `removeNode` |
| Unconditional depth-based sweep that ignores DAG reachability | Simplicity — "erase anything older than N" | Erasing blocks still reachable from CRDT heads or referenced by un-erased deltas corrupts future delta-DAG sync for this node and graphsync for peers (IPFS's pin-set exists to prevent exactly this) | Eligibility must consider head reachability: the unspent set + current heads + unpruned window are effectively "pinned" |
| IPFS-style auto-GC by disk watermark (StorageMax/StorageGCWatermark clone) | "Reclaim automatically when disk fills" | Reactive budget-driven GC races ingest; can erase at unpredictable depths during sync bursts; complicates the eligibility invariant. Even IPFS Cluster docs advise disabling auto-GC in some setups | Deterministic window-based periodic sweep for v1; disk-budget mode is a future opt-in |
| Whole-repo `repo gc`-style global sweep | "Clean everything unreferenced at once" | One-owner-per-domain: transaction retention rules belong to the transaction domain; a global GC would encroach on certificate/policy/processing ownership | Namespace-scoped pruner over the tx→CID map |
| Erase-then-compaction-online immediately every cycle | Operators want to see the disk drop now | Forced compaction every cycle is expensive and competes with write traffic | Batch deletions; compact (or document lag) per interval |

## Feature Dependencies

```
[Two-level erasure (CRDT value + CID block)]
    └──requires──> [tx→CID bookkeeping map]
    └──requires──> [Erasure eligibility: all-spent AND window]
                       └──requires──> existing UTXOManager spend tracking
                       └──requires──> finality/depth watermark (DAG tip)

[Crash-consistent idempotent prune cycle]
    └──requires──> [tx→CID bookkeeping map] (durable, transactional)
    └──requires──> [Two-level erasure] (defines the erasure steps to order)

[Pruned node stays operational (restart/tip-sync/spend-receive)]
    └──requires──> [Crash-consistent idempotent prune cycle]
    └──requires──> [Re-arrival handling]
    └──requires──> [Head-reachability safety] (from anti-features: never erase head-reachable blocks)

["History pruned" reporting + clean CID_NOT_FOUND for peers]
    └──requires──> nothing new (uses existing error paths) — but must be tested against a pruned node

[Prune light-node-only gating]
    └──requires──> existing node_type / is_full_node_ (validated v1.0)

[Configurable window knobs]
    └──requires──> existing sgns_config.json plumbing pattern

[Prune status introspection] ──enhances──> [Prune observability (logs)]
[RocksDB compaction trigger] ──enhances──> disk actually reclaimed
[Re-prune watermark] ──enhances──> [Re-arrival handling] (replaces sweep with drop-at-receive)

[Light nodes serving historical sync] ──conflicts──> [Clean refusal for erased CIDs]
[Auto-GC by disk watermark] ──conflicts──> [Erasure eligibility invariant] (unpredictable erase depth)
```

### Dependency Notes

- **Two-level erasure requires tx→CID map:** the CID arrives with the tx key/value but is unused (PROJECT.md); the map is the index that turns "prune tx X" into two concrete storage operations. Everything downstream depends on it, so it must be Phase-1 material.
- **Eligibility requires existing capabilities only:** `UTXOManager` (spend tracking) and the DAG tip (finality watermark) already exist — the new work is the *query* shape ("are ALL outputs of tx X spent") which must go through the owning module, not a transport-layer scan.
- **Crash-consistency requires the map and the erasure steps to be defined first:** it is an ordering/durability contract over them, so it cannot be built before they exist — but it must land with them, not after (a prune cycle without crash-consistency is an anti-feature).
- **Operational correctness (restart/sync/spend) requires crash-consistency AND re-arrival handling:** these three are the milestone's acceptance bar; they compose, they don't overlap.
- **Refusal/reporting conflicts with any "serve history anyway" idea:** the clean-refusal semantics only stay honest if the light node never re-advertises pruned history (BIP-159 lesson).

## MVP Definition

### Launch With (v1)

The milestone's minimum viable pruner — enough to prove "bounded disk, still correct."

- [ ] tx→CID bookkeeping map, durable, written with tx ingestion — the enabling index for everything else
- [ ] Erasure eligibility: all outputs spent AND depth/finality window elapsed, evaluated through the owning modules
- [ ] Two-level erasure: CRDT value + raw ipfs-lite CID block (`removeNode`), light nodes only
- [ ] Crash-consistent idempotent prune cycle (intent → erase value → erase block → mark done; restart-safe to a fixed point)
- [ ] Re-arrival handling, sweep-based (periodic re-prune of resurrected entries)
- [ ] "History pruned" explicit error for local queries; clean CID_NOT_FOUND for peer requests to erased blocks
- [ ] Non-tx CID data provably untouched (namespace whitelist + test)
- [ ] Pruned node stays operational: restart, tip sync, spend/receive green in multi-node production-path tests
- [ ] Config knobs with no-prune default; INFO-log resolved values per the config convention
- [ ] Per-cycle observability log line (count, bytes reclaimed, watermark)

### Add After Validation (v1.x)

- [ ] Prune status introspection (queryable status: enabled, window, watermark, cumulative reclaimed) — trigger: operators running long-lived light nodes asking "is it working"
- [ ] RocksDB compaction trigger / documented space-reclamation lag — trigger: first operator report of "disk didn't shrink"
- [ ] Background rate-limited pruning with batch caps — trigger: any observed stall of sync or RPC during a prune cycle (build the batch knobs from the start even if MVP uses a simple timer)
- [ ] Re-prune watermark (drop-at-receive) — trigger: measurable re-arrival churn from full-peer re-broadcasts; needs its own correctness research (head-reachability interaction)
- [ ] Finality-based vs age-based window policy flag — trigger: deployment diversity

### Future Consideration (v2+)

- [ ] On-demand historical tx fetch from full peers (getblockfrompeer analog) — a peer-capability protocol feature, not a pruner feature
- [ ] UTXO-snapshot bootstrap for fresh light nodes (assumeutxo analog) — changes the initial-sync contract; large standalone effort
- [ ] Disk-budget (StorageMax-style) prune mode — only after the window-based invariant is battle-tested
- [ ] Retention policy for non-tx data classes (processing results, old certificates) — each belongs to its owning module's future decision, one class at a time

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| tx→CID bookkeeping map | HIGH (enables all reclamation) | MEDIUM | P1 |
| Eligibility: all-spent AND window | HIGH (safety invariant) | MEDIUM | P1 |
| Two-level erasure (light-only) | HIGH (the milestone's point) | MEDIUM | P1 |
| Crash-consistent idempotent cycle | HIGH (data integrity) | MEDIUM-HIGH | P1 |
| Sweep-based re-arrival handling | HIGH (correctness under CRDT semantics) | MEDIUM | P1 |
| "History pruned" reporting + clean refusal | HIGH (vs. corruption panic) | LOW-MEDIUM | P1 |
| Operational acceptance tests (restart/sync/spend) | HIGH (the acceptance bar) | MEDIUM (test cost) | P1 |
| Non-tx data untouched (whitelist + test) | HIGH (protocol safety) | LOW | P1 |
| Config knobs, no-prune default | MEDIUM | LOW | P1 |
| Per-cycle observability log | MEDIUM | LOW | P1 |
| Prune status introspection | MEDIUM | LOW-MEDIUM | P2 |
| RocksDB compaction trigger / lag docs | MEDIUM | LOW-MEDIUM | P2 |
| Background rate-limited batching | MEDIUM | MEDIUM | P2 |
| Re-prune watermark | MEDIUM | MEDIUM-HIGH | P2 |
| Window-policy flag (finality vs age) | LOW-MEDIUM | LOW | P2 |
| On-demand history fetch from full peers | LOW (this milestone) | HIGH | P3 |
| UTXO-snapshot bootstrap | LOW (this milestone) | HIGH | P3 |
| Disk-budget auto-GC mode | LOW | HIGH | P3 |

**Priority key:**
- P1: Must have for launch
- P2: Should have, add when possible
- P3: Nice to have, future consideration

## Competitor Feature Analysis

| Feature | Bitcoin Core (prune mode) | geth (full/history prune) | IPFS/Kubo (repo gc) | Our Approach |
|---------|---------------------------|---------------------------|---------------------|--------------|
| Erasure criterion | Depth (MiB budget, min 550); UTXO set separate | Reachability from last 128 states; history prune for bodies/receipts | Unpinned (pin-set reachability) | All-outputs-spent AND depth/finality window (both margins, deliberately stricter) |
| What is never erased | Chainstate (UTXO set), undo data (with margin), last ~288 blocks | Current state; archive mode disables GC (`--gcmode archive`) | Pin-reachable content | Unspent UTXO set, heads, non-tx CID data, unpruned window |
| Serving history after prune | Never advertises it: NODE_NETWORK_LIMITED (BIP-159, last 288 blocks); no txindex/rescan | Old states regenerable on demand from peers; snap-synced can never be archive | N/A (blocks served only while pinned/present) | Light node refuses with explicit "history pruned"/CID_NOT_FOUND; never a sync source (non-goal) |
| Eligibility authority | Node core (block-storage layer) | Execution client core | Repo layer, driven by pin registry (app-level intent) | Transaction domain owns retention; storage layer only executes erasure (one-owner-per-domain) |
| Observability | `getblockchaininfo` {pruned, pruneheight}; prune progress logs | Prune logs, sync mode in logs | GC prints each removed CID; StorageMax/GCWatermark config | INFO per cycle (count/bytes/watermark); P2 status introspection |
| Crash/restart safety | Restart-safe by construction; prune only removes validated data | Prune is offline-ish, resumable | GC is transactional per-block | Durable intent → erase → mark; idempotent re-run to fixed point (P1, highest-risk item) |
| Escape hatch | Re-download; getblockfrompeer (v25+); assumeutxo snapshots (v28+) | Re-sync; archive node | Re-fetch from network (content-addressed) | Re-sync from full/archive via existing bootstrap; on-demand fetch is v2+ |
| GC pacing | Inline during block acceptance, bounded | Periodic heavyweight pass | Stop-the-world sweep (manual or watermark-triggered) | Periodic sweep, batch-capped, low priority (P2 knobs; MVP simple timer) |

## Sources

- bitcoin.org, *Run a full node — Reduce Storage* (`-prune=N` semantics, minimum 550, txindex/rescan/importwallet incompatibility, disk savings) — https://bitcoin.org/en/full-node#reduce-storage [HIGH confidence, official]
- BIP-159 NODE_NETWORK_LIMITED (288-block service guarantee, pruned peers must not signal NODE_NETWORK, anti-fingerprinting rationale) — https://github.com/bitcoin/bips/blob/master/bip-0159.mediawiki [HIGH, official]
- Bitcoin Core 30.0.0 RPC docs, `getblockchaininfo` (`pruned`, `pruneheight`), `getblockfrompeer` (25.0), `loadtxoutset` (assumeutxo) — https://bitcoincore.org/en/doc/30.0.0/rpc/blockchain/getblockchaininfo et al. [HIGH, official]
- geth docs, *Sync modes* (full vs snap vs archive; 128-state retention; snap-synced can never be archive; 12 TB archive) — https://geth.ethereum.org/docs/fundamentals/sync-modes [HIGH, official]
- geth docs, *History pruning* (block bodies/receipts reclamation) — https://geth.ethereum.org/docs/fundamentals/historypruning [HIGH, official]
- IPFS Docs, *Using garbage collection in Kubo* (repo gc removes unpinned; StorageGCWatermark 90%, GCPeriod 1h, --enable-gc; GC prints removed CIDs) — https://docs.ipfs.tech/how-to/kubo-garbage-collection [HIGH, official]
- IPFS Docs, *Pin files* (pins protect from GC; `ipfs add` pins recursively by default) — https://github.com/ipfs/ipfs-docs/blob/master/docs/how-to/pin-files.md [HIGH, official]
- Almeida, Shoker, Baquero, *Delta State Replicated Data Types* (delta-CRDT OR-Sets, causal-stability basis for tombstone pruning) — https://arxiv.org/abs/1410.2803 [HIGH, peer-reviewed]
- Yjs project (tombstone GC replaces content with markers, identity retained; full GC breaks ordering; `doc.gc=false`) — https://github.com/yjs/yjs and https://docs.yjs.dev/api/y.doc [HIGH, official project docs]
- crdt.tech glossary (tombstone memory cost, GC algorithms exist) — https://crdt.tech/glossary [MEDIUM]
- Nano whitepaper (node may store pruned history containing only the last few blocks of each account's chain; full-history nodes remain the source) — via docs.nano.org protocol-design/ledger [MEDIUM-HIGH]
- SuperGenius PROJECT.md (v1.2 scope, key decisions: light-only, two-level erasure, spent+window criterion, CID-alongside-tx fact) — `.planning/PROJECT.md` [HIGH, project authority]
- Source checks: `src/crdt/graphsync_dagsyncer.hpp` (`removeNode(CID)`, `Error::CID_NOT_FOUND`), `src/crdt/crdt_datastore.hpp` (`CreateDeltaToRemove`, `RemoveHead` — tombstone semantics, no existing prune), `src/account/UTXOManager.hpp` (address-filtered `GetUTXOs`) [HIGH, verified in-tree]

---
*Feature research for: light-node transaction pruning (sliding-window disk reclamation)*
*Researched: 2026-10-15*
