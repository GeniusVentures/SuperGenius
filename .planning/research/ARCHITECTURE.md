# Architecture Research

**Domain:** Light-node transaction pruning in an existing C++17 UTXO+DAG blockchain (physical erasure of spent transaction history from CRDT + raw-block storage)
**Researched:** 2026-10-15
**Confidence:** HIGH (every claim below was verified against this repository's source; file:line citations throughout)

## Standard Architecture

### Physical Storage Reality: One RocksDB, Two Facades

The "CRDT store" and the "raw ipfs-lite block store" are **two wrappers over the same underlying RocksDB**. `GlobalDB::Init` creates the ipfs-lite `RocksdbDatastore` from the CRDT datastore's own DB handle (`src/crdt/globaldb/globaldb.cpp:322-329`), and `CrdtDatastore::New` uses namespace `"crdt"` (`globaldb.cpp:360-364`). The vendored ipfs-lite-cpp lives at `/Users/henriqueklein/gnus/thirdparty/ipfs-lite-cpp` (outside this repo tree, wired via `src/CMakeLists.txt` targets `ipfs-bitswap-cpp` etc.).

```
ONE RocksDB (per node, at <base>/<network>/)
├── /crdt/s/<key>/<delta-cid-string>      elem marker   (crdt_set.cpp:707, "s" crdt_set.hpp:341)
├── /crdt/t/<key>/<delta-cid-string>      tombstone     (crdt_set.cpp:773, "t" crdt_set.hpp:342)
├── /crdt/k/<key>/v                       current value (crdt_set.cpp:630,654; suffix "v" :344)
├── /crdt/k/<key>/p                       priority      (crdt_set.cpp:528; suffix "p" :345)
├── /crdt/h/<topic>/<cid-string>          head => height (crdt_heads.cpp:52-86; headsNamespace "h" crdt_datastore.hpp:488)
├── <cid-bytes>                           RAW BLOCK: node bytes (datastore_rocksdb.cpp set(), key = CID.toBytes())
├── <cid-base58-string>+"sealed"          SEAL MARKER: resolved flag (datastore_rocksdb.cpp seal()/is_sealed())
├── /bc-<net>/tx/<hash>                   tx->CID map (raw, non-CRDT put; TransactionManager.cpp:4336-4342)
├── /utxo/<addr>/<outpoint>               UTXO registry incl. CONSUMED (UTXOManager.hpp:482, UTXOManager.cpp:1052-1055)
├── /utxo-checkpoint/...                  checkpoints (UTXOManager.hpp:484)
└── (work journal internal namespace, certs /cert/<slot>, reg/, SecureCRDT, processing...)
```

The `Element.id` of every merged delta is the **CID string of the delta block** (`CrdtDatastore::MergeDataFromDelta` passes `node_cid.toString()` as `aID`, `crdt_datastore.cpp:986-992`), which is why elem/tomb markers are keyed by delta CID.

### System Overview (pruning-relevant slice)

```
┌───────────────────────────────────────────────────────────────────────┐
│ GeniusNode facade (src/account/GeniusNode.cpp — InitDatabase :2124)    │
│   NodeType (Light default) · tx_globaldb_ = GlobalDB                   │
├───────────────────────────────┬───────────────────────────────────────┤
│ TransactionManager            │ UTXOManager (src/account/UTXOManager) │
│  SendTransactionItem :1982    │  PutUTXO :156 (all peers, D-17 :160)  │
│  NewElementCallback :4370     │  ConsumeUTXOs :265 (spent-ness!)      │
│  StoreTransactionCID :4322    │  LoadUTXOs :827 / StoreUTXOs :1073    │
│  InitializeUTXOs :3148        │  IsOutPointConsumed :765              │
│  QueryTransactions :2473      │  IsOutPointGenuinelyConsumed (hpp:397)│
├───────────────────────────────┴───────────────────────────────────────┤
│ GlobalDB (src/crdt/globaldb/globaldb.cpp)                              │
│  Put/Get/Remove/QueryKeyValues · GetRaw/PutRaw/QueryRaw :181-196       │
├────────────────────────────────────────────────────────────────────────┤
│ CrdtDatastore (src/crdt/impl/crdt_datastore.cpp)                       │
│  PutKey→Publish→CreateDAGNode→AddDAGNode :1381/1455/1550/1594          │
│  ProcessJobIteration :994 · UpdateCRDTHeads :2111 · heads_/set_        │
│  CRDTCallbackManager (put hook carries CID) :2096 / crdt_set.cpp:738   │
├───────────────────────────────┬────────────────────────────────────────┤
│ GraphsyncDAGSyncer            │ ipfs-lite (vendored thirdparty)        │
│  getNode :205 (LRU→dag→fetch) │  MerkleDagServiceImpl::addNode =       │
│  HasBlock :511 (bytes OR LRU) │    block_service_->set(cid,raw)        │
│  removeNode :461 (EXISTS)     │  markResolved = seal(cid)              │
│  markResolved/isResolved :799 │  isResolved = is_sealed(cid)           │
│  InitCIDBlock/DeleteCIDBlock  │  RocksdbDatastore (datastore_rocksdb)  │
│  = LRU-cache in-flight prot.  │  GraphsyncImpl::onRemoteRequest :286   │
└───────────────────────────────┴────────────────────────────────────────┘
```

### Component Responsibilities (as-built, pruning-relevant)

| Component | Responsibility | File |
|-----------|----------------|------|
| `CrdtSet` | Add-wins OR-Set state in RocksDB (`/s /t /k/v /k/p`), put/delete hooks | `src/crdt/crdt_set.hpp`, `impl/crdt_set.cpp` |
| `CrdtHeads` | Head table `/crdt/h/<topic>/<cid>`, `PrimeCache` on construction | `src/crdt/crdt_heads.hpp`, `impl/crdt_heads.cpp:14,286` |
| `CrdtDatastore` | Delta DAG lifecycle: jobs, merge, head update, broadcast, retries | `src/crdt/impl/crdt_datastore.cpp` |
| `GraphsyncDAGSyncer` | Block fetch/serve, LRU in-flight cache, routes, blacklist, resolved | `src/crdt/impl/graphsync_dagsyncer.cpp` |
| `GlobalDB` | Public store surface + raw node-local keys; owns the one RocksDB | `src/crdt/globaldb/globaldb.cpp` |
| `TransactionManager` | Tx write/parse/track lifecycle, nonce chain, tx→CID map, callbacks | `src/transaction/TransactionManager.cpp` |
| `UTXOManager` | Durable per-outpoint lifecycle (READY/CONSUMED/RESERVED) | `src/account/UTXOManager.cpp` |
| `AccountMessenger` | Answers peer "give me tx X" with the CID from the tx→CID map | `src/account/AccountMessenger.cpp:669-684` |

## Answers to the Six Research Questions (from source)

### (1) Where transaction key/values are written, and what references them afterwards

**Write path (sender):** `SendTransactionItem` builds `HierarchicalKey` at `/bc-<net>/tx/<hash>` (`TRANSACTION_BASE_FORMAT = "/bc-%hu/"` `TransactionManager.hpp:433`; `"tx/" + hash` `GeniusTransaction.hpp:172-183`) and `AtomicTransaction::Put` (batched — one delta per commit, `TransactionManager.cpp:2099-2105`); optional proof at `/bc-<net>/proof/<hash>` (`:2109-2116`). Registration txs go to `reg/<src>` instead (`:2080-2093`). Commit → `CrdtDatastore::Publish` → `CreateDAGNode` (priority = heads maxHeight+1; links EVERY live head — merge-point invariant, `crdt_datastore.cpp:1563-1572`) → `AddDAGNode` → worker merges: `ProcessJobIteration` → `MergeDataFromDelta` → `CrdtSet::Merge` → `PutElems`/`PutTombs` write the four set entries and fire the put hook `(key, value, cid)` post-commit (`crdt_set.cpp:699-744`); block persisted via `dagSyncer_->addNode` (`crdt_datastore.cpp:1029`).

**References to the stored value afterwards:**
- Set reads: `GetKey`→`GetElement` (`crdt_set.cpp:185`), `HasKey`→`IsValueInSet` (`:375`, fast-path on `/k/<key>/v` existence), prefix queries (`QueryElements`, used by `TransactionManager::QueryTransactions` `TransactionManager.cpp:2473-2503`).
- In-memory `tx_processed_m` rebuilt on every start from CRDT queries; per-key tracking (`AddTransactionToProcessedMaps` `:4212`).
- Restart: `InitializeUTXOs` (`:3148`) re-reads the *creating transaction* of every READY outpoint via `FetchAndProcessTransaction` (`:3228-3239`); empty/cleared UTXO state triggers full `QueryTransactions` replay (`:3196-3200`).
- Nonce chain: `QueryOutgoingPreviousHashFromCRDT` scans all tx values for (own address, nonce−1, valid cert) (`:1603-1649`); `GetPersistedOutgoingPreviousHash` requires `FetchTransaction` of the persisted head hash *and* a matching certificate (`:1828-1867`). **Pruning hazard:** the node's OWN last-confirmed-nonce tx is a live dependency of the *next* outgoing tx. `GetRegisteredOutgoingPreviousHash` (`:1688-1726`) shows the pruning-safe pattern already in the codebase: resolve nonce→hash from `/cert/<slot>` (certificates embed the exact winning tx hash) without needing the tx value.
- Peer service: `AccountMessenger` answers `BlockRequest{tx_hash}` with the CID from `GetTransactionCID` (`AccountMessenger.cpp:669-684` → `TransactionManager.cpp:4433-4458`); the peer then graphsync-fetches that block.
- Filters: `FilterTransaction` re-validates any re-received element and rejects overwrite only if `KeyExistsInDB` (`:3793`) — a pruned key would be re-accepted on re-broadcast (resurrection; tolerable, see constraints).

### (2) Delta-DAG retention: value vs block, and when a block is erasable

**The CRDT value and the delta block are separable.** The merged set state (`/k/<key>/v|p`, `/s/<key>/<cid>`, `/t/<key>/<cid>`) is written once at merge time; the raw block (key = CID bytes) is only read by: (a) traversal of *unresolved* subtrees (`TraverseCIDsLinks` `graphsync_dagsyncer.cpp:674-797`), (b) graphsync serving to peers (`onRemoteRequest`), (c) `GetLocalDeltaKeyValues` (`crdt_datastore.cpp:2280-2302`), (d) `PrintDAG`.

**Seal markers are the traversal cut-off.** `isResolved(cid)` ⇔ the `<cid>sealed` key exists (`datastore_rocksdb.cpp`). `TraverseCIDsLinks` checks `isResolved(child)` *before* `GetNodeWithoutRequest(child)` and skips resolved CIDs (`graphsync_dagsyncer.cpp:752-765`); the root itself is skipped when resolved (`:693-708`). `UpdateCRDTHeads` marks both the retired head(s) and the new head resolved (`crdt_datastore.cpp:2125-2188`). Consequences:
- Once sealed, a block's **bytes are never needed again for local DAG walking** — even if deleted, a new root linking to it is not re-fetched through that link.
- `remove(cid)` deletes only the block-bytes key, **not** the seal marker (different key encoding: CID bytes vs `<cid-string>sealed`, `datastore_rocksdb.cpp`). A pruner that uses `remove` keeps the resolved flag — exactly what a pruned light node wants.

**A delta block is erasable when ALL hold:**
1. It is sealed (`isResolved` true) — i.e. it was processed and retired from head duty (or is the current head — excluded by 2).
2. It is **not** a current head in any topic (`CrdtHeads::IsHead`; heads are re-broadcast every ~60 s by `RebroadcastHeads` `crdt_datastore.cpp:1209-1272`, and `HandleCIDBroadcast` re-enqueues anything `HasBlock` reports missing, `:706-759`).
3. No pending/active root job or stalled-delta retry references it (`IsRootCIDPendingOrActive` `:2201-2225`; `stalledDeltas_` retry re-reads the node, `:1826`); not in the LRU in-flight cache (`IsCIDInCache`).
4. The node is Light (full/archive never erase — milestone decision).
5. **Every element carried in that delta block is prune-eligible.** This is the non-obvious constraint: deltas are multi-element (AtomicTransaction batches a whole `transaction_batch` + proofs into ONE delta; `SendTransactionItem:1984-2116`). The tx→CID map is many-keys→one-CID, so one tx's eligibility does not license block deletion. The pruner must enumerate the block's elements before deleting it — reuse `GetNodeWithoutRequest` + `GetDeltaFromNode` (pattern already in `GetLocalDeltaKeyValues`, `crdt_datastore.cpp:2280-2302`).

**Never delete on a live node:** seal markers (`<cid>sealed`); current head entries and their blocks; `/utxo` prefix (the unspent set AND the CONSUMED tombstones — they are the restart-safe spent-ness record); `/crdt/h` heads table; work-journal entries; stalled/failed-root bookkeeping subjects; certificates (`/cert/<slot>`), `reg/`, SecureCRDT/TrustedPeer/BurnConfig values, processing results; anything outside `/bc-<net>/tx/` (proofs are out of milestone scope per PROJECT.md non-goals — treat `proof/` pairs as retained unless separately scoped).

**Never use the replicated delete for pruning:** `CrdtDatastore::DeleteKey` (`crdt_datastore.cpp:1438-1453`) and `TransactionManager::DeleteTransaction` (`:3524-3540`) publish tombstone deltas **network-wide** — that erases history on full nodes too. Pruning is a *local* RocksDB operation only.

### (3) Graphsync behavior on missing blocks; what a light node must answer

- **Responder (light node asked for a pruned CID):** `GraphsyncImpl::onRemoteRequest` — with an empty selector, `service->getNode(root_cid)` failing yields `select_res = 0` → explicit **`RS_NOT_FOUND`** response (`graphsync_impl.cpp:368-381, 430-455`; the comment at `:370-373` documents this as deliberate fail-fast). No blacklist, no error state — the light node is a polite non-holder.
- **Requester (whoever asked):** `GraphsyncDAGSyncer::getNode` walks route peers; a FAILED response that is not a connection failure → `RecordCIDFailure` (4 s per-(peer,CID) suppression, `:158`, `:365-386`) → next route; exhaustion → `ROUTE_NOT_FOUND`; `CrdtDatastore` then applies the failed-root retry ladder (5 s→60 s, max 8, `crdt_datastore.cpp:541-546`).
- **Implication:** light nodes may prune blocks and remain correct citizens, provided full/archive nodes remain the holders of history (they never prune). Aligned with the milestone non-goal "light nodes serving historical sync to other peers". One caveat: `AccountMessenger` currently answers "have tx X?" from the tx→CID **map** without checking block presence — after pruning, either delete the map entry (recommended; the messenger then answers empty) or gate the answer on `HasBlock`.

### (4) Where the CID is available at transaction-put time — the map ALREADY EXISTS

- The put hook fires `(key, value, cid)` where `cid` = the delta block's CID string, from `PutElems` post-commit (`crdt_set.cpp:738-744`), wired in `CrdtDatastore::New` (`crdt_datastore.cpp:63-80`), dispatched synchronously by regex through `CRDTCallbackManager::PutDataCallback` (`crdt_callback_manager.cpp:141-198`).
- `TransactionManager::New` registers pattern `^/?/bc-<net>/tx/[^/]+` → `NewElementCallback` (`TransactionManager.cpp:273-282`) which calls **`StoreTransactionCID(key, cid)`** — a raw RocksDB `put` at the bare tx path (`:4322-4345`), i.e. outside the `/crdt` namespace, node-local, never broadcast. `GetTransactionCID(tx_hash)` reads it back (`:4433-4458`) and is already consumed by `AccountMessenger` (`:671-684`).
- Sender side additionally gets the CID as the return of `PutKey→Publish→AddDAGNode→WaitForJob` (`crdt_datastore.cpp:1455-1461, 1624-1713`).
- **Gaps to close (this milestone's "bookkeeping" work is extension, not creation):** (a) no deletion lifecycle — map entries outlive `DeleteTransaction` and would outlive pruning; (b) only `tx/` keys are covered — batched deltas mean the map alone cannot decide block erasability (see (2).5); (c) no reverse index CID→keys (either add one or parse the block at prune time — parsing reuses existing code and avoids a second source of truth).

### (5) What restart / head-sync recovery re-reads (pruning must not break these)

- `CrdtHeads` constructor runs `PrimeCache` over `/crdt/h` (`crdt_heads.cpp:14, 286-363`).
- `GlobalDB::Start` → `StartCIDProcessing` (broadcast intake thread) + `StartRebroadcastHeads` (60 s default, `CrdtOptions::DefaultOptions` `crdt_options.hpp:46`).
- `TransactionManager::InitializeUTXOs` (`:3148-3249`): `LoadUTXOs` restores the registry **including CONSUMED entries** (`UTXOManager.cpp:899-934`); then for every **READY** outpoint it re-fetches the *creating* transaction — a tx with any unspent output is a live restart dependency (pruning rule "all outputs spent" already protects this, but the pruner must not race it); missing own-address txs land in `missing_tx_hashes_` and gate `State::READY`.
- Periodic head request: 30 s until first data, then 10 min (`TransactionManager.cpp:596-617`) — pulls **current heads only**; the v1.1 archive-replication lesson (memory: `archive-repl-flake-root-cause`) is that initial sync and settle windows race — the pruner must be gated on `State::READY` plus a settle condition, and must never run while `missing_tx_hashes_` is non-empty.
- Every node subscribes `GNUS_FULL_NODES_TOPIC` unconditionally (`GeniusNode.cpp:713`, documented in `test/src/archive_node/archive_replication_test.cpp:11-19`) — light nodes receive the full tx stream; that is exactly the disk growth this milestone bounds.
- **Checkpoint-mismatch rebuild path** (`:3176-3193`): on root mismatch the node clears local UTXOs and rebuilds from `QueryTransactions` — on a pruned node this replay loses the consuming effects of pruned spenders and can resurrect spent outputs (double-spend window). Guard: a pruned node must not silently clear-and-rebuild from its own (incomplete) history; either refuse the clear when a "history pruned below depth D" marker exists, or re-sync from a full node. This is the sharpest restart-safety edge the roadmap must own.
- UTXO registry writes are address-bucketed whole-snapshot rewrites (`StoreUTXOSnapshot` `UTXOManager.cpp:985-1071`) — CONSUMED entries persist and reload; the pruner never touches `/utxo`.

### (6) Where spent-ness is observable today (pruner trigger source)

- `ParseTransferTransaction` → `UTXOManager::ConsumeUTXOs(inputs, owner)` (`TransactionManager.cpp:2632-2638`); mints consume burn inputs (`:2647+`). `ConsumeUTXOs` flips state to `UTXO_CONSUMED`, synthesizes zero-amount CONSUMED tombstones for unknown outpoints, and persists per-owner snapshots (`UTXOManager.cpp:265-365`).
- **D-17 removed light-node address filtering**: "all nodes store UTXOs for all peers" (`UTXOManager.cpp:160, :232`) — a light node has complete outpoint state for every parsed tx, so "all outputs spent" is locally decidable.
- Query surface: `GetOutPointState` (`:752`), `IsOutPointConsumed` (`:765`), `IsOutPointGenuinelyConsumed` (`UTXOManager.hpp:385-397` — CONSUMED **with non-zero amount** distinguishes a real spend from a synthesized/repair tombstone; the pruner must use this, not bare `IsOutPointConsumed`). Output enumeration of a tx: `GeniusTransaction::GetProducedUTXOs()` (as used by `PutProducedUTXOs`, `TransactionManager.cpp:2562-2575`).
- `spent_epoch` / `spent_by_txid` exist in the persisted schema but are **never populated at runtime** (only reset to `nullopt` at `PutUTXO`, `UTXOManager.cpp:178-179`, and restored on load `:918-930`) — do not build the depth window on them.
- **Depth metric without reading blocks:** current chain height = heads `maxHeight` (`CrdtHeads::GetList`); the tx's stored priority at `/crdt/k/<key>/p` (`CrdtSet::GetPriority`, `crdt_set.cpp:498-519`) equals the height of the winning delta. depth ≈ maxHeight − priority. Both are RocksDB reads independent of raw blocks. (Certificates already bind finality per slot; using cert presence for the finality half of the window is an optional tightening via `GetTransactionCertificate`.)

## Recommended Project Structure (new vs modified)

```
src/transaction/
├── TxPruner.hpp / TxPruner.cpp        # NEW — owning module for the pruning rule
│                                        (eligibility + orchestration; owns nothing else's state)
├── TransactionManager.cpp/.hpp        # MODIFIED — expose tx enumeration + tracked-state queries
│                                        to TxPruner; extend previous-hash cert-slot resolution;
│                                        delete tx→CID map entry on prune; prune tick wiring
src/crdt/
├── crdt_datastore.hpp/.cpp (+globaldb)# MODIFIED — local-erase API: erase /s,/t,/k/v,/k/p entries
│                                        for a key in ONE batch (never Publish/tombstone);
│                                        expose maxHeight; keep GetLocalDeltaKeyValues reuse
├── crdt_set.hpp/.cpp                  # MODIFIED — raw-batch element erase primitive (CrdtSet owns
│                                        the four key shapes; erasure goes through it, not around it)
├── dagsyncer.hpp / graphsync_dagsyncer# MODIFIED (thin) — expose removeNode-based block erase that
│                                        keeps the seal marker (removeNode already exists:
│                                        graphsync_dagsyncer.cpp:461-465; route/LRU guards added)
src/account/
├── GeniusNode.cpp/.hpp                # MODIFIED — construct/start/stop TxPruner for Light only;
│                                        read window config in LoadSgnsConfig next to node_type
├── AccountMessenger.cpp               # MODIFIED (small) — tx-answer path stays correct once map
│                                        entries are deleted on prune (optionally gate on HasBlock)
└── UTXOManager.hpp/.cpp               # MODIFIED (thin) — if needed: "all outputs of txid" helper
                                         (or enumerate via tx's GetProducedUTXOs — already public)
3rdparty/ipfs-lite-cpp                  # UNTOUCHED — remove()/seal()/is_sealed() already provide
                                         the exact block-erase + resolved-retention semantics
```

### Structure Rationale

- **One owning module per domain rule:** the pruning rule (when is a tx erasable) is a transaction-domain rule; `TxPruner` lives beside `TransactionManager` in `src/transaction/` and depends only on public contracts (`GlobalDB`, `CrdtDatastore` erase API, `UTXOManager` queries). It must NOT be a `GeniusNode` lambda (god-class facade already flagged in `.planning/codebase/CONCERNS.md`).
- **Erasure primitives live in the layers that own the key shapes:** `CrdtSet` owns `/s /t /k` layouts (no other module may construct those keys), `GraphsyncDAGSyncer` owns block storage. `TxPruner` orchestrates; it never concatenates storage keys itself.

## Architectural Patterns

### Pattern 1: Local physical erasure (bypass the CRDT write path)

**What:** Delete RocksDB entries directly under one batch; never `DeleteKey`/`AtomicTransaction::Remove` (those publish network-wide tombstones, `crdt_datastore.cpp:1438-1453`).
**When:** always, for pruning.
**Trade-offs:** erased keys lose CRDT replay protection — a re-broadcast of the same delta resurrects the value (window-gated; re-prune handles it). No peer coordination needed.

```cpp
// Shape of the erase primitive (inside CrdtSet — it owns the key layouts):
outcome::result<void> CrdtSet::EraseElementLocally( const std::string &aKey )
{
    auto batch = this->dataStore_->batch();
    // /crdt/s/<key>/<each delta-id>  (query prefix, remove all)
    // /crdt/t/<key>/<each delta-id>  (query prefix, remove all)
    // /crdt/k/<key>/v , /crdt/k/<key>/p
    return batch->commit();  // one atomic RocksDB write batch
}
```

### Pattern 2: Seal-marker-retaining block removal

**What:** Block erase = `remove(cid)` (bytes key only); the `<cid>sealed` marker survives by construction (different key, `datastore_rocksdb.cpp`). Sealed CIDs are never re-traversed (`graphsync_dagsyncer.cpp:752-765`), so a pruned ancestor does not trigger re-fetch when a new head links to it.
**When:** any block erasure on a light node.
**Trade-offs:** `isResolved` stays true forever — correct here (we never want that subtree walked again). If the seal marker were also deleted, every new root linking the pruned block would queue a doomed fetch (5 s→60 s ladder, pinned single DAG worker — the exact stall shape from the archive-replication flake).

### Pattern 3: Multi-element block eligibility via block parse (no second bookkeeping source of truth)

**What:** Before deleting a delta block, read it (`GetNodeWithoutRequest`) and enumerate its delta elements (`GetDeltaFromNode`, both existing — see `GetLocalDeltaKeyValues` `crdt_datastore.cpp:2280-2302`); erase the block only when **every** element key is prune-eligible.
**When:** always for block erasure.
**Trade-offs:** one extra read per prune; avoids maintaining a CID→{keys} reverse index that could drift from the truth (DRY: the block IS the source of truth for what it contains).

## Data Flow

### Existing tx flow (context)

```
SendTransactionItem → AtomicTransaction::Put(/bc-N/tx/<hash>) ─ Commit(one delta)
  → CreateDAGNode(priority=maxHeight+1, links ALL heads) → AddDAGNode → worker
  → MergeDataFromDelta → CrdtSet::Merge → /s,/k/v,/k/p written ─ putHook(key,value,cid)
  → CallbackManager(regex) → NewElementCallback → StoreTransactionCID(/bc-N/tx/<hash> = cid)
  → dagSyncer->addNode(block bytes @cid) ; UpdateCRDTHeads(markResolved old+new)
```

### New erasure flow (trigger → value delete → CID lookup → block delete)

```
[Trigger] UTXOManager ConsumeUTXOs flips last output of tx T to CONSUMED
    (spent-ness durable in /utxo; survives restart via LoadUTXOs)
      ↓ (polled by TxPruner on its tick; gated: Light node ∧ State::READY
         ∧ missing_tx_hashes_ empty ∧ settle window elapsed)
[Eligibility] TxPruner:
    1. tx value still present? GetKey(/bc-N/tx/<hash>) → parse outputs
    2. every output outpoint (T,idx): IsOutPointGenuinelyConsumed == true
    3. depth window: heads.maxHeight − GetPriority(tx key) ≥ window
       (finality tightening: GetTransactionCertificate matches — optional)
    4. NOT the node's own outgoing frontier (nonce of T ≥ local confirmed
       nonce ⇒ exempt until the previous-hash path no longer needs the value)
      ↓
[1. CRDT value delete] GlobalDB erase API → CrdtSet::EraseElementLocally
    (/crdt/s|t/<key>/<cid>, /crdt/k/<key>/v|p — one batch, no broadcast)
      ↓
[2. CID lookup] GetTransactionCID(T) → cid  (existing map)
      ↓
[3. Block eligibility + delete] GetNodeWithoutRequest(cid) → GetDeltaFromNode
    → every element key prune-eligible? → DAGSyncer block erase = removeNode(cid)
    (bytes key only; seal marker kept; guards: !IsHead ∧ !pending/active ∧ !in LRU)
      ↓
[4. Bookkeeping delete] remove raw /bc-N/tx/<hash> map entry
    (AccountMessenger then answers "no CID" for pruned txs)
```

Order matters: value delete (1) before block delete (3) — while the block exists, recovery/re-merge can restore set state; deleting the block first would leave set entries that no longer replay. Map entry (4) last so steps 2-3 can still find the CID if interrupted; the whole sequence is idempotent and restart-safe (re-running a partially-executed prune converges).

## Anti-Patterns

### Anti-Pattern 1: Pruning via DeleteKey / DeleteTransaction

**What people do:** call the existing CRDT remove path to "delete" the spent tx.
**Why it's wrong:** it publishes a tombstone delta network-wide (`crdt_datastore.cpp:1438-1453`, `TransactionManager.cpp:3524-3540`) — full/archive nodes would also drop the value, destroying the sync/audit source, and the tombstone delta itself is new unprunable data.
**Do this instead:** local batch erase in `CrdtSet` (Pattern 1).

### Anti-Pattern 2: Deleting the raw block but not keeping the seal marker

**What people do:** "clean up" both keys, or erase via a prefix scan over CID-like keys.
**Why it's wrong:** unsealed (resolved-flagless) ancestors make every later root re-queue fetches for pruned history; with a single DAG worker this pins CRDT sync (the archive-repl flake shape).
**Do this instead:** `remove(cid)` only; assert `isResolved(cid)` true before and after erase.

### Anti-Pattern 3: Treating any CONSUMED outpoint as a genuine spend

**What people do:** prune as soon as `IsOutPointConsumed` is true for all outputs.
**Why it's wrong:** `ConsumeUTXOs` synthesizes zero-amount CONSUMED tombstones for outpoints it cannot find (durability retries leave them; `UTXOManager.cpp:302-311`, discriminator documented at `UTXOManager.hpp:385-397`).
**Do this instead:** `IsOutPointGenuinelyConsumed` for every output.

### Anti-Pattern 4: Block deletion keyed only by the tx→CID map

**What people do:** tx eligible → delete its mapped CID's block.
**Why it's wrong:** deltas are multi-element (batched sends + proofs in one block); other keys in the same block may be ineligible.
**Do this instead:** parse the block and require all elements eligible (Pattern 3).

### Anti-Pattern 5: Pruning the node's own nonce-frontier transaction

**What people do:** depth window says old → erase own outgoing tx at the confirmed nonce frontier.
**Why it's wrong:** the next outgoing tx chains onto nonce−1's hash; the persisted-head recovery path re-reads the tx value (`TransactionManager.cpp:1847`), and the CRDT scan fallback reads all tx values (`:1603-1649`). Empty previous-hash breaks the chain.
**Do this instead:** exempt own-address txs at ≥ local confirmed nonce, or generalize `GetRegisteredOutgoingPreviousHash`'s certificate-slot resolution (`:1688-1726`) to all nonces (certs are never pruned).

## Integration Points

### Internal Boundaries

| Boundary | Communication | Notes / gotchas |
|----------|---------------|-----------------|
| TxPruner ↔ UTXOManager | direct calls (existing query API) | use `IsOutPointGenuinelyConsumed`; never write `/utxo` |
| TxPruner ↔ TransactionManager | query tx values + tracked state; map-entry delete via raw datastore | tracked `tx_processed_m` entries for pruned keys should be dropped the same way `ProcessDeletion` does (`:4310-4320`) to keep memory bounded |
| TxPruner ↔ GlobalDB/CrdtDatastore | new local-erase API + `GetLocalDeltaKeyValues` pattern | erase must take the same locks `CrdtSet::Merge` takes (`mutex_`) or run on the CRDT worker — never race a merge |
| TxPruner ↔ GraphsyncDAGSyncer | block erase via `removeNode` + guards (`IsHead`, `IsRootCIDPendingOrActive`, `IsCIDInCache`) | `DeleteCIDBlock` is the LRU-cache delete — a different thing; do not reuse it for persisted blocks |
| Pruner lifecycle ↔ GeniusNode | constructed in `InitDatabase`/after `TransactionManager` READY; Light only | config in `sgns_config.json` beside `node_type` (role-adjacent, per v1.0 decision pattern `PROJECT.md:114`) |
| AccountMessenger ↔ tx→CID map | existing `GetTransactionCID` | after prune deletes the entry, peers get an empty BlockResponse and ask full nodes |

### External Services

None — no new dependencies; everything rides existing RocksDB/graphsync/libp2p facilities (constraint: "no new dependency unless research establishes a concrete need" — none needed; the required primitives `remove()`, `seal()/is_sealed()`, `removeNode`, `GetLocalDeltaKeyValues` all exist).

## CRDT Delta-DAG Correctness Constraints (precise statement)

On a live Light node, the pruner MUST NOT delete:
1. Any CID that is a current head in `/crdt/h/<topic>/*` for any topic (rebroadcast every ~60 s; deletion ⇒ re-fetch loop).
2. Any `<cid>sealed` marker (deletion ⇒ re-traversal + doomed fetches of pruned ancestors).
3. Block bytes of any CID that is not yet sealed (still traversable, possibly still fetchable by an in-flight job), any CID in the LRU in-flight cache, any CID pending/active/stalled in `CrdtDatastore` job state (`IsRootCIDPendingOrActive`, `stalledDeltas_`, `failedRootRetries_`).
4. Anything outside `/bc-<net>/tx/` and its raw-block/map counterparts: `/utxo*` (unspent set + CONSUMED tombstones), heads, certs, `reg/`, proofs (out of scope), SecureCRDT/TrustedPeer/BurnConfig, processing data.
5. A tx value while any of its outputs is READY/RESERVED — both a rule input (eligibility) and a restart dependency (`InitializeUTXOs` re-reads creating txs of READY outpoints).
6. The node's own outgoing-frontier tx (Anti-Pattern 5) until the certificate-based previous-hash resolution exists.

And MUST: keep erasure local (never a published tombstone); make each prune step idempotent; tolerate resurrection (re-broadcast of a pruned delta re-creates the value at a fresh-or-equal priority — FilterTransaction accepts it because `KeyExistsInDB` is false; the next prune cycle re-erases); never run during initial sync/settle (`State::READY` + empty `missing_tx_hashes_` + settle delay).

## Scaling Considerations

| Scale | Architecture Adjustments |
|-------|--------------------------|
| Small (light node, weeks of history) | Prune tick piggybacks on `TransactionManager` loop; eligibility computed lazily from `QueryKeyValues("/bc-N/tx")` prefix scan |
| Medium (large tx backlog at first enable) | First prune pass over a big prefix scan should be chunked (bounded per-tick work) to avoid starving the tick loop and the single DAG worker; RocksDB deletes are cheap but the parse step reads values |
| Large | If prefix scans become hot, add a persistent pruner cursor (raw key, e.g. `/prune/cursor`) instead of rescanning; do NOT add an index of eligible txs — the UTXO set already is one (spent-ness per outpoint) |

### Scaling Priorities

1. **First bottleneck: prune-tick scan cost** — chunked scans + cursor.
2. **Second bottleneck: resurrection churn** if many lagged peers rebroadcast old heads — depth window bounds it; log-and-accept.

## Suggested Build Order (dependency-respecting)

1. **Erase primitives (no behavior change):** `CrdtSet::EraseElementLocally` + `CrdtDatastore/GlobalDB` local-erase surface + block-erase-with-guards on `GraphsyncDAGSyncer` (reusing `removeNode`); expose heads `maxHeight`. Unit-tested on scratch DBs; nothing calls them yet. *(Everything else depends on these; bookkeeping already exists.)*
2. **Bookkeeping hardening:** tx→CID map deletion lifecycle (erase raw `/bc-N/tx/<hash>` entry), decide proof-map scope (defer — out of milestone scope), keep `GetTransactionCID` semantics. *(Map must be complete before any deletion depends on it.)*
3. **Eligibility evaluator (pure, unit-testable):** tx value → outputs → `IsOutPointGenuinelyConsumed` all-true ∧ depth window (priority vs maxHeight) ∧ not own-frontier. No erasure yet; dry-run logging.
4. **Own-frontier survival fix:** generalize certificate-slot previous-hash resolution (extend `GetRegisteredOutgoingPreviousHash` pattern) OR own-nonce exemption — pick one, gate it on tests. Must land **before** first real pruning. *(Correctness dependency, not a code dependency.)*
5. **TxPruner service + GeniusNode wiring:** tick loop (READY+settle gated), the 4-step erase flow, tracked-map cleanup, config knob beside `node_type` in `sgns_config.json` (Light-only activation). Multi-node E2E: prune → restart → tip sync → spend/receive still green; full/archive bit-identical history; fresh light syncs from full.
6. **Checkpoint-rebuild guard:** "history pruned below depth" marker so the checkpoint-mismatch clear-and-rebuild path (`InitializeUTXOs:3176-3193`) cannot resurrect spent outputs from pruned history (fail to full-node re-sync instead). *Last because it hardens a rare path, but before UAT.*

## Sources

All findings verified directly against this repository's source (HIGH confidence, no external sources required):

- `src/crdt/impl/crdt_datastore.cpp` (PutKey :1381, PutKeyLocal :1394, DeleteKey :1438, CreateDAGNode :1550 head-link invariant :1563-1572, AddDAGNode :1594, ProcessJobIteration :994, UpdateCRDTHeads :2111, HandleCIDBroadcast :677, Sync storage-layout comment :1993-2039, GetLocalDeltaKeyValues :2280, retry ladder :541-546)
- `src/crdt/impl/crdt_set.cpp` (PutElems :669 hook-fires-post-commit :736-744, PutTombs :749, Merge :804, GetElement :185, IsValueInSet :375, key layouts :462-496, GetPriority :498)
- `src/crdt/impl/crdt_heads.cpp` (key format :52-64, PrimeCache :286)
- `src/crdt/impl/graphsync_dagsyncer.cpp` (getNode :205, HasBlock :511, removeNode :461, TraverseCIDsLinks :674 resolved-skip :752-765, markResolved/isResolved :799-809, LRU CID cache :250-278, InitCIDBlock/DeleteCIDBlock :811-862)
- `src/crdt/impl/crdt_callback_manager.cpp` (regex dispatch, cid passthrough :141-246)
- `src/crdt/globaldb/globaldb.cpp` (one-DB-two-facades :322-329, namespace "crdt" :360, Start :479, backups :379-477)
- `src/crdt/globaldb/pubsub_broadcaster_ext.cpp` (OnMessage/AddRoute :128-283, AddMultiCIDInfo HasBlock gate :625-667)
- `src/transaction/TransactionManager.cpp` (patterns :207-292, SendTransactionItem :1982-2116, nonce ladder :1660-1868, cert-slot resolution :1688-1726, QueryTransactions :2473, InitializeUTXOs :3148-3249, ConsumeUTXOs call sites :2632-2638, DeleteTransaction :3524, StoreTransactionCID :4322, NewElementCallback :4370, GetTransactionCID :4433)
- `src/account/UTXOManager.cpp/.hpp` (PutUTXO D-17 :156-226, ConsumeUTXOs :265-365, LoadUTXOs :827-938, StoreUTXOSnapshot :985-1071, query APIs, DB_PREFIX :482)
- `src/account/AccountMessenger.cpp` (:630-704 tx CID answer path)
- `src/account/GeniusNode.cpp` (InitDatabase :2124-2164); `src/account/GeniusTransaction.hpp` (:172-183)
- `thirdparty/ipfs-lite-cpp`: `ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.cpp` (addNode/markResolved→seal :17-42), `ipfs_lite/ipfs/impl/datastore_rocksdb.cpp` (seal marker key format, remove semantics), `ipfs_lite/ipfs/graphsync/impl/graphsync_impl.cpp` (RS_NOT_FOUND responder :286-457)
- `test/src/archive_node/archive_replication_test.cpp` (:11-19 unconditional GNUS_FULL_NODES_TOPIC note)
- `.planning/PROJECT.md` (v1.2 scope, decisions, non-goals); memory: `archive-repl-flake-root-cause`, `cert-receipt-deferred-dispatch-invariant`

---
*Architecture research for: SuperGNUS v1.2 light-node transaction pruning*
*Researched: 2026-10-15*
