# Stack Research — v1.2 Light-node Transaction Pruning (Storage Layer)

**Domain:** Physical erasure + real disk-space reclamation on a C++17 blockchain light node
**Researched:** 2026-10-15
**Confidence:** HIGH (every API claim verified against vendored source in this workspace; file paths cited)

> Correction to the milestone brief: ipfs-lite-cpp is **not** under `3rdparty/` inside SuperGNUS. It is vendored at the **workspace level** in `/Users/henriqueklein/gnus/thirdparty/ipfs-lite-cpp` (CI downloads the `GeniusVentures/thirdparty` release into `thirdparty/`, see `.github/workflows/cmake.yml:318-328`; there is no `3rdparty/` dir in the repo). RocksDB is vendored at `/Users/henriqueklein/gnus/thirdparty/rocksdb`.

## Recommendation Summary

| Need | Recommendation | Confidence |
|------|----------------|------------|
| Raw CID-block erasure | Use the **existing** `removeNode(CID)` chain down to `RocksdbDatastore::remove` — no vendored change needed | HIGH |
| Physical disk-space return | Batched `Delete`/`DeleteRange` + periodic `DB::CompactRange` with `bottommost_level_compaction = kForce`, via the existing `getDB()` accessors — no vendored change | HIGH |
| tx→CID bookkeeping | New **node-local key prefix in the same single-CF RocksDB**, written directly through `sgns::storage::rocksdb` (pattern: `CRDTWorkJournal`), populated from the **already-firing** put-hook `cid` parameter | HIGH |
| Vendored ipfs-lite-cpp change | Zero or one tiny addition: `removeSeal(CID)` to clean `<cid>sealed` markers (marker format must keep its one home in vendored code) | MEDIUM |
| New dependencies | **None.** RocksDB 9.4 already vendored has every API required | HIGH |

## Storage Topology Today (verified)

One **shared RocksDB instance, single default column family**, opened with the single-CF `DB::Open(options, path, &db)` overload:

- SuperGNUS wrapper: `src/storage/rocksdb/rocksdb.cpp:55`
- Vendored ipfs-lite wrapper: `ipfs_lite/rocksdb/rocksdb.cpp:46` (thirdparty/ipfs-lite-cpp)

`GlobalDb` opens one DB and wraps the **same** `db_` handle twice (`src/crdt/globaldb/globaldb.cpp:322-344`):

```
storage::rocksdb (SuperGNUS wrapper, owns DB)          ← CRDT set/heads keys
  └─ IpfsRocksDb::create(getDB())                       ← same DB handle
       └─ RocksdbDatastore (CID → raw block bytes)      ← flat binary CID keys
            └─ GraphsyncDAGSyncer → MerkleDagServiceImpl
                 └─ CrdtDatastore(HierarchicalKey("crdt"))
```

**Consequence:** erasing one transaction's footprint means deleting keys in *one* DB — CID-keyed raw block(s), the CRDT set entries, and any bookkeeping — which can be done atomically in a single `WriteBatch`. No cross-database coordination exists or is needed.

### Key map of the shared DB (all prefixes verified)

| Physical key | Content | Written by | Erasable by pruner? |
|---|---|---|---|
| `<CID bytes>` (binary) | raw ipfs-lite DAG node block | `RocksdbDatastore::set` | YES — via `removeNode` |
| `<cid-base58-string>sealed` | "resolved" marker | `seal()` / `markResolved()` | YES (needs `removeSeal`, see below) |
| `/crdt/s/<key>/<cid>` | OR-Set **elems** (CID embedded in key) | `CrdtSet::PutElems` | YES for pruned tx keys |
| `/crdt/t/<key>/<cid>` | OR-Set **tombs** | `CrdtSet::PutTombs` | YES for pruned tx keys |
| `/crdt/k/<key>/v` \| `/p` | current value \| priority | `CrdtSet::SetValue` | YES for pruned tx keys |
| `/crdt/h/<topic>/<cid>` | CRDT **heads** (DAG tips) | `CrdtHeads::Add` | NO — sync liveness depends on tips |
| `/crdt/work/...` | node-local work journal | `CRDTWorkJournal` | managed by journal itself |
| `/utxo`, `/utxo-checkpoint` | **unspent UTXO set + checkpoints** | `UTXOManager` (`src/account/UTXOManager.hpp:481-484`) | **NEVER** |
| `/bc-<netid>/...` | transactions (`TRANSACTION_BASE_FORMAT = "/bc-%hu/"`, `src/transaction/TransactionManager.hpp:433`) | `TransactionManager` | YES (this is the target) |
| `:sgns:...` | finality lookup keys | `src/storage/predefined_keys.hpp` | NO |

Prefix constants: elems `s` / tombs `t` / keys `k` in `src/crdt/crdt_set.hpp:341-345`; heads namespace in `src/crdt/crdt_datastore.hpp:488`.

## What Already Exists — No New Library Needed

### 1. Raw CID-block deletion is implemented end-to-end (unused)

```
sgns::crdt::DAGSyncer::removeNode(cid)                          (src/crdt/dagsyncer.hpp — virtual)
  → GraphsyncDAGSyncer::removeNode                              (src/crdt/impl/graphsync_dagsyncer.cpp:461-464)
    → MerkleDagServiceImpl::removeNode                          (thirdparty/.../merkledag/impl/merkledag_service_impl.cpp:39-42)
      → IpfsBlockService / RocksdbDatastore::remove             (thirdparty/.../ipfs/impl/datastore_rocksdb.cpp:78-82)
        → db_->Delete(key)                                      (thirdparty/.../rocksdb/rocksdb.cpp:131-140)
```

Grep confirms **zero production callers** of `removeNode` today — the path exists but is dormant. The pruner is its first consumer; no behavioral risk to existing flows.

**Trap:** `GraphsyncDAGSyncer::DeleteCIDBlock(cid)` (`graphsync_dagsyncer.cpp:855-862`) sounds like deletion but only evicts the in-memory LRU cache. Do not build on it.

### 2. The "CID arrives with the key/value" claim — verified true, twice over

- **Put hook:** `putHookFunc_(key, value, aID)` fires for every applied element, local *and* remote origin (`src/crdt/impl/crdt_set.cpp:588-590, 738-742`), and the `aID` **is the DAG-node CID**. It flows to `CRDTCallbackManager::PutDataCallback(key, value, cid)` — the `cid` param TransactionManager callbacks already receive and ignore.
- **Elems key:** the element version key is `/crdt/s/<key>/<cid-string>` (`crdt_set.cpp:707`), so the CID set for any key is recoverable by a prefix scan of `ElemsPrefix(key)` even with no new map.

Also `GlobalDB::Put` returns `outcome::result<CID>` (`src/crdt/globaldb/globaldb.hpp:99`).

### 3. Node-local (non-broadcast) persistence precedent

- `CrdtDatastore::PutKeyLocal` / `GlobalDB::PutLocal` — local-only writes, documented contract for per-node derived state (`src/crdt/crdt_datastore.hpp:160-177`).
- `CRDTWorkJournal` — a node-local journal written **directly** to the shared `storage::rocksdb` under its own namespace `/crdt/work/` (`src/crdt/globaldb/crdt_work_journal.hpp:117`), never replicated. This is exactly the pattern the tx→CID map should copy.

### 4. Physical per-key erase of CRDT-side keys exists in the SuperGNUS wrapper

`sgns::storage::rocksdb::remove(Buffer)` and `BufferBatch::remove` (`src/storage/rocksdb/rocksdb.hpp:93`, `rocksdb_batch.hpp:21`) are plain `DB::Delete` calls — suitable for physically erasing `/crdt/s|t|k` entries for pruned keys. Note this is deliberately **not** `GlobalDB::Remove` / `CrdtDatastore::DeleteKey`, which is the CRDT *tombstone* path (broadcasts a Remove delta, writes tombs, keeps elems — logical delete only, and it *adds* data).

## RocksDB 9.4 Space-Reclamation Mechanics (core question)

Vendored version: `thirdparty/rocksdb/include/rocksdb/version.h` → `ROCKSDB_MAJOR 9`, `ROCKSDB_MINOR 4`. DB opens with leveled compaction, 32 MB write buffer/target file, bloom(10), `CappedPrefixTransform(3)`, `wo_.sync = true` (`src/crdt/globaldb/globaldb.cpp:194-200`; `src/storage/rocksdb/rocksdb.cpp:38-70`).

### What actually returns space to the OS

| Operation | Mechanism | Space returned immediately? | Where declared (vendored 9.4) |
|---|---|---|---|
| `Delete(key)` | point tombstone | **No** — file still holds the key until compaction rewrites it | `db.h` (`DB::Delete`) |
| `DeleteRange(begin,end)` | range tombstone (cheap bulk delete) | **No** — same as above; tombstones must reach the bottommost level | `db.h:549-560` |
| `CompactRange(opts, begin, end)` with `bottommost_level_compaction = kForce` | rewrites bottommost SSTs overlapping the range, drops covered deleted keys, obsolete files unlinked | **Yes** (after background completion) | `db.h:1495-1498`, `options.h:2071` |
| `DeleteFilesInRange(db, cf, begin, end)` | drops whole SSTs **fully contained** in the range | **Yes**, instantly | `convenience.h:443-450` |
| `DropColumnFamily` | fast bulk reclaim per CF | n/a — **no column families are used** | `db.h` |

External corroboration: RocksDB issue [#8041](https://github.com/facebook/rocksdb/issues/8041) ("not freeing up space after a delete" — resolved by manual compaction), the official [DeleteRange blog](http://rocksdb.org/blog/2018/11/21/delete-range.html) (DeleteFilesInRange before compacting), and the [Manual Compaction wiki](https://github.com/facebook/rocksdb/wiki/Manual-Compaction). Confidence HIGH.

### Recommended reclamation recipe for the pruner

1. **Erase**: per-tx `WriteBatch` (single-CF default handle) containing — for each CID of the tx: `Delete(cid-bytes)` (via `removeNode`, keeping the format knowledge in the vendored layer) and `Delete("<cid>sealed")`; plus `Delete` of `/crdt/s/<txkey>...`, `/crdt/t/<txkey>...`, `/crdt/k/<txkey>/v|p`; plus the bookkeeping-map entries.
2. **Optionally `DeleteRange`** for the contiguous per-tx key spans (`/crdt/s/<txkey>` → next key) instead of N point deletes when a tx has many element versions. Respect the caveats documented in `db.h:530-548`: accumulated range tombstones degrade reads — flush periodically; `max_open_files = -1` preferred; **incompatible with `row_cache`** (not configured here, verified `create()` sets neither).
3. **Reclaim**: periodic, light-node-only, low-frequency `CompactRange` over the whole DB (`begin = end = nullptr`) with `CompactRangeOptions{ bottommost_level_compaction = kForce, allow_write_stall = false }`. Whole-DB is right because CID-block keys are binary and scattered (no shared prefix to compact per-tx). The milestone goal is a *bounded* DB, not instant shrink — schedule compaction off the hot path (idle/backoff), not per-batch.
4. **Verify**: `DB::GetApproximateSizes` (`db.h:1436-1444`) for pruner telemetry and tests asserting bytes-before < bytes-after.

All four steps run through the raw `std::shared_ptr<DB>` that **both wrappers already expose** (`storage::rocksdb::getDB()`, `rocksdb.hpp:100-103`; `ipfs_lite::rocksdb::getDB()`, vendored `rocksdb.hpp:74-77`). **No wrapper or vendored change is required for any of this.** Put the compaction helper in the new pruner module (one owning module for the pruning domain, per project invariant 1).

### Why not the alternatives

| Alternative | Verdict | Why rejected |
|---|---|---|
| Rely on automatic compaction alone | Reject | Deletes eventually compact away, but timing is workload-dependent and unbounded; `max_compaction_bytes = 32MB` (`globaldb.cpp:197`) keeps automatic compactions small; a bounded light-node DB needs a deterministic trigger |
| `DeleteFilesInRange` as primary tool | Reject for v1 | Only drops SSTs *fully* inside the range and can expose older key versions in lower levels; risky on a DB shared with live domains; fine as a future optimization for large contiguous runs |
| Compaction filter (drop keys during background compaction) | Reject | Implicit GC: fires during *every* compaction, hard to bound/test, can silently drop data on unrelated compactions; milestone wants explicit, gated erasure |
| Column-family per domain + `DropColumnFamily` | Reject | Both wrappers are single-CF (`DB::Open(options, path, &db)`); CF support means reworking both open paths and the shared-DB wiring in `globaldb.cpp:322` — invasive, zero functional gain for this milestone |
| New RocksDB version | Reject | 9.4 already provides DeleteRange/CompactRange/GetApproximateSizes/DeleteFilesInRange; constraint says no new dependency |
| Any new storage engine (SQLite, LMDB, …) | Reject | Everything needed is present; adds a second engine to a codebase that standardizes on RocksDB |

## tx→CID Bookkeeping Map — Where It Lives

**Recommendation: a new node-local key prefix in the same shared DB, written directly via `sgns::storage::rocksdb`, never broadcast — the `CRDTWorkJournal` pattern.**

- **Shape:** forward index `/prune/txcid/<txkey>` → CID-string set; reverse `/prune/cidtx/<cid>` → tx-key set. The reverse direction lets the pruner find all keys sharing a DAG node before erasing the block.
- **Multiplicity is mandatory, not optional:** `TransactionManager` batches **many Puts into one committed DAG node** (`crdt_transaction->Commit(topicSet)` after a loop of `Put`s, `src/transaction/TransactionManager.cpp:2099-2126`), and a single key can have multiple element versions over time (re-puts). One CID ↔ many tx keys; one tx key ↔ potentially many CIDs. The elems namespace `/crdt/s/<key>/<cid>` already encodes the forward direction and can serve as ground truth/repair source.
- **Population source:** the existing put hook's `cid` parameter (fires for both self-created and remotely-synced elements — verified `crdt_set.cpp:588-590, 738-742`). No new write-path interception needed; a registered callback (or a small extension where the journal is written) is additive.
- **Erase atomically** with the other keys of the pruned tx in the same `WriteBatch`.

**Why not:** a new column family (single-CF wrappers, above); a separate RocksDB instance/file (breaks atomic batch erase across the map, the CRDT keys, and the CID block; extra lifecycle; GlobalDB already owns the one DB); an in-memory map (milestone requires restart-safe operation); inside the replicated CRDT namespace via normal `PutKey` (would broadcast and tombstone — the map is per-node bookkeeping, `PutKeyLocal`'s documented rationale covers exactly this case).

## Vendored ipfs-lite-cpp — Minimal Extension Set

The vendored fork is already locally modified relative to upstream (`sgns::ipfs_lite` namespace, the seal/`markResolved` additions in `datastore_rocksdb.cpp:48-65` and `merkledag_service_impl.cpp:23-36`). Extending it is the established pattern; upstream is not consumed.

| Change | File | Why needed | Size |
|---|---|---|---|
| **None required** for block erasure or compaction | — | `removeNode` chain + `getDB()` cover it | 0 |
| `removeSeal(CID)` (sibling of `seal`) on `IpfsDatastore` + `RocksdbDatastore` | `ipfs_lite/ipfs/datastore.hpp`, `ipfs/impl/datastore_rocksdb.{hpp,cpp}` | The `<cid>sealed` marker key format (`cid.toString() + "sealed"`) has one home in vendored code; erasing markers from SuperGNUS would duplicate that format knowledge (DRY invariant). Markers are written for *every* processed root CID and never cleaned — unbounded growth the pruner should also stop | ~10 lines |
| (Optional) thread `removeSeal` through `MerkleDagServiceImpl`/`DAGSyncer` | merkledag_service_impl, dagsyncer.hpp | Symmetry with `markResolved` so callers never touch the datastore directly | ~5 lines |

**Open question (flag for architecture phase):** confirm the seal-marker lifecycle before erasing them. `isResolved` is consulted in `CrdtDatastore::UpdateCRDTHeads` (`crdt_datastore.cpp:2111+`) to avoid re-marking; if a pruned CID is later re-broadcast, `HandleCIDBroadcast` gating (pending-jobs/heads checks, `IsRootCIDPendingOrActive`) must be shown to tolerate a missing block+marker. If unresolved, keep the markers (they are small) and only erase them for CIDs whose heads have also moved on.

## Integration Inventory (SuperGNUS side)

| Component | File(s) | Addition |
|---|---|---|
| New pruner service module (owns the pruning domain) | new, e.g. `src/pruning/` or under `src/transaction/` | Sliding-window eligibility (all-outputs-spent + depth/finality), erase-batch builder, compaction scheduler, `GetApproximateSizes` telemetry |
| Config | `sgns_config.json` via `LoadSgnsConfig` (`src/account/GeniusNode.cpp:251`) — `node_type` already read here | Light-node-only pruning knobs (window depth, enable flag, compaction cadence); default-off or safe-default per PROJECT.md "only light nodes prune" |
| GlobalDB facade | `src/crdt/globaldb/globaldb.{hpp,cpp}` | Narrow accessor for the pruner: direct local-storage namespace writes + physical erase of a key's CRDT-side entries (today only the CRDT-tombstone `Remove` exists) |
| UTXO safety | `src/account/UTXOManager.hpp:481-484` (`/utxo`, `/utxo-checkpoint`) | No change — these prefixes are on the pruner's never-erase list |
| Backup engine | `globaldb.cpp:237-266, 381-436` | No code change required; note that backups taken before pruning retain old data until `PurgeOldBackups(keep_count)` rotates them out — expected, not a bug |

## What NOT to Add (per constraint: no new dependency without concrete need)

- **No new third-party library of any kind.** RocksDB 9.4 (vendored) + the vendored ipfs-lite-cpp fork provide 100% of the required API surface.
- **No RocksDB version bump, no column-family rework, no TTL/blob-DB/compaction-filter configuration changes** to the shared DB — those alter behavior for every other domain sharing the DB.
- **No second storage engine** for bookkeeping.
- **No GC framework.** A single pruner service with batch + periodic compaction is the smallest shape (KISS/YAGNI).

## Sources

- Vendored source (HIGH confidence, primary): `/Users/henriqueklein/gnus/thirdparty/ipfs-lite-cpp/ipfs_lite/{ipfs/datastore.hpp, ipfs/impl/datastore_rocksdb.cpp, ipfs/impl/ipfs_block_service.hpp, rocksdb/rocksdb.cpp, ipfs/merkledag/impl/merkledag_service_impl.cpp}`
- SuperGNUS source (HIGH): `src/crdt/globaldb/globaldb.cpp`, `src/crdt/crdt_datastore.hpp`, `src/crdt/impl/crdt_set.cpp`, `src/crdt/impl/graphsync_dagsyncer.cpp`, `src/storage/rocksdb/rocksdb.{hpp,cpp}`, `src/transaction/TransactionManager.{hpp,cpp}`, `src/account/UTXOManager.hpp`, `src/crdt/globaldb/crdt_work_journal.hpp`
- Vendored RocksDB 9.4 headers (HIGH): `thirdparty/rocksdb/include/rocksdb/{db.h, options.h, convenience.h, version.h}`
- External (MEDIUM corroboration of mechanics): [RocksDB issue #8041](https://github.com/facebook/rocksdb/issues/8041), [DeleteRange blog](http://rocksdb.org/blog/2018/11/21/delete-range.html), [Manual Compaction wiki](https://github.com/facebook/rocksdb/wiki/Manual-Compaction), [Stack Overflow: space after delete](https://stackoverflow.com/questions/66543704/rocksdb-is-not-freeing-up-space-after-a-delete)
