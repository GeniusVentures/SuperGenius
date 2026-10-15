# Phase 16: Erasure Primitives & Bookkeeping Hardening - Research

**Researched:** 2026-10-15
**Domain:** Atomic two-level RocksDB erasure primitives (CRDT set entries + raw CID block + tx→CID map entry) behind owner-module APIs, with safety invariants baked into API shape; fresh-DB test-fixture rules; zero production behavior change.
**Confidence:** HIGH (every API claim re-verified against this repo's source this session, file:line cited; milestone research `.planning/research/{SUMMARY,STACK,ARCHITECTURE,PITFALLS}.md` cross-checked where cited)

## Summary

The one-DB-two-facades topology is confirmed at `src/crdt/globaldb/globaldb.cpp:322-329`: the vendored ipfs-lite `RocksdbDatastore` is constructed from the SuperGNUS wrapper's own `shared_ptr<DB>` (`IpfsRocksDb::create(m_datastore->getDB())`), so a single RocksDB `WriteBatch` submitted through the SuperGNUS wrapper can atomically delete keys belonging to both facades plus the tx→CID map. The composition surface already exists: `storage::rocksdb::batch()` returns a `BufferBatch` whose `remove(key)` stages into a private `ROCKSDB_NAMESPACE::WriteBatch` and whose `commit()` issues one `db_->Write(wo_, &batch_)` with `wo_.sync = true` (`src/storage/rocksdb/rocksdb_batch.cpp:23-38`, `rocksdb.cpp:69`), and `CrdtSet` already has a caller-owned-batch overload pattern (`SetValue(const std::unique_ptr<storage::BufferBatch>&, ...)` at `src/crdt/crdt_set.hpp:250`) to copy.

The one hard mechanical constraint the planner must design around: **the existing `removeNode(CID)` chain cannot participate in an external WriteBatch.** Every layer of that chain (`GraphsyncDAGSyncer::removeNode` → `MerkleDagServiceImpl::removeNode` → `IpfsBlockService::remove` → `RocksdbDatastore::remove` → immediate `db->Delete`) executes its own standalone delete, and both wrapper `Batch` classes keep their `WriteBatch` member private with no accessor. Therefore PRUNE-02's "raw CID block … in one atomic WriteBatch" is only achievable by staging a raw delete of the block key — `CID::toBytes()`, the exact key format the vendored `encode()` helper builds (`thirdparty-ipfs-lite-cpp/ipfs_lite/ipfs/impl/datastore_rocksdb.cpp:14-18, 78-82`) — into the shared batch from the SuperGNUS side. With D-C3 forbidding vendored changes, the recommended owner of that key construction is the guarded exposure on `GraphsyncDAGSyncer` (the block-storage owner), which should *stage* the delete into a caller-owned batch rather than *execute* `removeNode`. "Reuse the dormant removeNode chain" (D-C3) is thereby satisfied semantically — same key, same no-vendored-change property, seal marker untouched by construction — while `removeNode()` itself stays dormant with zero production callers (verified by grep this session).

Three subtle hazards surfaced during code verification that the plan must encode: (1) firing the CRDT `deleteHookFunc_` on erase would drive `TransactionManager::DeleteElementCallback` → `ProcessDeletion` → `RemoveTransactionFromProcessedMaps`, which calls `RevertTransaction` whenever a tx object is tracked — reverting genuine spends; the physical erase must not fire the CRDT delete hook, and tracked-map cleanup must be a plain erase from `tx_processed_m` without revert (`TransactionManager.cpp:4177-4210`). (2) `CrdtDatastore::IsRootCIDPendingOrActive` is **private** (`crdt_datastore.hpp:462`), and `GlobalDB` does not retain the `dagSyncer` shared_ptr (local variable in `Init`, `globaldb.cpp:344`) — the guard composition needs two small wiring additions. (3) The existing unit fixture `CrdtDatastoreTest` uses an `InMemoryDatastore` behind `CustomDagSyncer` — it physically cannot prove one-batch atomicity or seal-marker retention; the erase fixture must wire the real `RocksdbDatastore` from the temp DB handle (three lines, no pubsub needed, mirroring `globaldb.cpp:322-329`).

**Primary recommendation:** Implement the erase as one `BufferBatch` from `GlobalDB::GetDataStore()->batch()`, staged by three owner contributions — `CrdtSet` batch-erase primitive (prefix-scan `/s`, `/t`, plus `/k/<key>/v|p`), guarded `GraphsyncDAGSyncer` block staging (`CID::toBytes()` key, guards inside), raw tx-path map key — committed once, with a test-only interruption seam; no production caller, no new build target.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-C1:** One shared RocksDB behind two facades (CRDT datastore + ipfs-lite block store, `src/crdt/globaldb/globaldb.cpp:322-329`); atomicity = one `WriteBatch`
- **D-C2:** Physical local RocksDB delete only — the CRDT tombstone path (`DeleteKey`/`DeleteTransaction`) is forbidden for pruning (owner directive: "RocksDB deletes should be physical deletion, not writing tombstones")
- **D-C3:** Seal markers (`<cid>sealed`) are never erased — they are the traversal terminator (GATE-03); reuse the dormant `removeNode` chain; zero vendored ipfs-lite-cpp changes
- **D-C4:** The existing tx→CID map (`StoreTransactionCID`/`GetTransactionCID`, `src/transaction/TransactionManager.cpp:4322-4345, 4433-4458`) is extended with a deletion lifecycle, not replaced
- **D-C5:** In-use guards enumerated: head CID, root-CID pending/active, LRU-cached CIDs; `isResolved` asserted before and after erase
- **D-C6:** Test rules: fresh temp DB, three-part assertions (CRDT entries absent, raw block absent, seal marker still present) plus a negative control (untouched sibling key byte-identical)
- **D-01:** The atomic two-level erase surfaces as ONE narrow, generic API on the GlobalDB facade — key + CID in, single `WriteBatch` internally, guards inside. The API stays generic (no transaction semantics leak into the CRDT layer). Owner-module key-layout knowledge (`CrdtSet` owns `/s /t /k/v /k/p`; `GraphsyncDAGSyncer` owns block storage; `TransactionManager` owns the map) stays inside those modules — GlobalDB composes them into one batch.
- **D-02:** Phase 16 builds PLAIN map-entry deletion only. The pruned-marker record is Phase 17 scope.
- **D-03:** Phase 16's crash proof is unit-level: `WriteBatch`-atomicity tests on fresh temp DBs plus failure-injection points at primitive boundaries (crash *between* steps simulated). Real process-kill tests belong to Phase 18.

### Claude's Discretion
- Test-file placement and fixture naming (repo conventions: gtest, PascalCase fixtures, `*_test.cpp`)
- Exact method signatures/parameter shapes for the GlobalDB erase API (planner/refactor stage)
- `maxHeight` exposure placement (needed by Phase 17's evaluator; may land here or in 17 if unused in 16)

### Deferred Ideas (OUT OF SCOPE)
- `bridge-race-not-all-11-mint-within-window` (P1) — bridge-mint-race scope; SDK matched on stopword overlap only; remains in pending for Track A bridge work
- Pruned-marker record + PRUNED-vs-NOT_FOUND error surface (Phase 17)
- Process-kill crash convergence tests (Phase 18)
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| PRUNE-02 | Erasure physically deletes, in one atomic RocksDB `WriteBatch`, the CRDT set entries (`/crdt/s\|t/<key>/<cid>`, `/crdt/k/<key>/v\|p`), the raw CID block (existing `removeNode` chain), and the tx→CID map entry — never via the CRDT tombstone path | Batch composition mechanics verified end-to-end (BufferBatch staging + one `db->Write`); the removeNode-chain atomicity conflict and its resolution documented below; CrdtSet caller-owned-batch pattern precedent at `crdt_set.hpp:250` |
| GATE-03 | Seal markers (`<cid>sealed`) are never erased — they are the traversal terminator that prevents re-fetch of pruned chains | Seal marker is a separate string-suffixed key (`datastore_rocksdb.cpp:48-65`); staging a delete of only `CID::toBytes()` cannot address it; test assertion strategy defined (raw key construct in test only) |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

Directives that bind this phase's implementation (from `/Users/henriqueklein/gnus/SuperGNUS/CLAUDE.md`):

- **C++17 only** — no C++20 features; Allman braces; brace all control statements; `enum class`; explicit casts; `constexpr` over macros; `.hpp/.cpp`; GNUS naming conventions; Doxygen-compatible file headers on every new file; public functions documented in the header.
- **`outcome::result<T>`** for fallible operations — the erase API and every primitive returns results; no exceptions; errors are part of the contract; never swallow failures.
- **One owning module per domain rule** — key-layout knowledge stays in its owner (`CrdtSet`, `GraphsyncDAGSyncer`, `TransactionManager`); GlobalDB composes; no function-level includes to dodge cycles.
- **Never duplicate logic** — grep before writing; one home per key-format/wire fact. Applies acutely to: the block key format, the seal-marker format, the tx→CID map key, the CRDT set key builders.
- **No domain logic in the transport layer** and conversely no transport concerns in the erase primitives.
- **Refactor first, then change** — Phase 16 is pure addition (no behavior change), so each task lands additive code with tests; no mixed refactor+behavior diffs.
- **Verification is a gate** — clang-format changed files, clang-tidy Required-clean (test targets are auto-exempt via `disable_clang_tidy` in the `addtest` macro, `cmake/functions.cmake:27`), build affected targets, run relevant tests; never claim a check passed that was not run.
- **RAII / ownership explicit** — no raw `new`/`delete`; `shared_ptr` only where genuinely shared (the DB handle is genuinely shared — that is the topology).
- **Build-system pre-PR checklist** applies if any CMakeLists changes (this phase should need only: possibly one new test file in an existing `addtest` target + one link dependency — still run `/gsd-code-review` before PR).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| CRDT set-entry erasure (`/crdt/s\|t\|k` keys) | `CrdtSet` (`src/crdt`, lib `crdt_set`) | — | Sole owner of the four key layouts (`crdt_set.hpp:341-345`, builders `:462-496`); no other module may construct these keys |
| Raw CID-block erasure (block bytes key) | `GraphsyncDAGSyncer` (`src/crdt`, lib `crdt_graphsync_dagsyncer`) | vendored `RocksdbDatastore` (read-only contract) | Block-storage owner; guards (`IsCIDInCache`, `isResolved`) already live here; key format is the vendored datastore's contract, mirrored in one SuperGNUS-side home on the guarded exposure |
| tx→CID map deletion lifecycle | `TransactionManager` (`src/transaction`) | — | Map owner (`StoreTransactionCID`/`GetTransactionCID`); tracked-map (`tx_processed_m`) cleanup owner |
| One-batch composition + in-use guards | `GlobalDB` facade (`src/crdt/globaldb`) | `CrdtDatastore` (pending/active guard), `CrdtHeads` (head guard) | D-01: GlobalDB holds the datastore wrapper, the CRDT datastore, and (after a one-line addition) the DAGSyncer; guards evaluate before staging |
| Head-CID guard | `CrdtHeads` via `GlobalDB::GetCRDTHeadList()` | — | `GetList` already returns all-topic heads + maxHeight (`crdt_heads.cpp:259-284`) |
| Root-CID pending/active guard | `CrdtDatastore` | — | `IsRootCIDPendingOrActive` exists but is private (`crdt_datastore.hpp:462`) — needs public exposure |
| Atomicity substrate | `sgns::storage::rocksdb` wrapper (`Batch::commit` → one `DB::Write`, sync) | — | Single-CF DB shared by both facades; no wrapper change needed |
| Fresh-DB test fixtures + three-part assertions | `test/src/crdt/` (new/extended test files) | — | Repo gtest conventions; `addtest` macro |

## Standard Stack

**No new external dependencies. Zero vendored changes.** Everything rides existing internal APIs (all verified this session):

### Core
| API | Location | Purpose | Why Standard |
|-----|----------|---------|--------------|
| `storage::rocksdb::batch()` → `BufferBatch` | `src/storage/rocksdb/rocksdb.hpp:67`, `rocksdb_batch.hpp:13-30` | The single atomic WriteBatch: `put/remove` stage, `commit()` = one `db_->Write(wo_, &batch_)` | Established repo batch mechanism; `CrdtSet::PutElems`/`PutTombs` already commit this way |
| `BufferBatch::remove(Buffer)` | `src/storage/rocksdb/rocksdb_batch.cpp:23-27` | Stages `WriteBatch::Delete` for ANY key in the shared DB — including the block key and map key | The one API that makes cross-fade composition possible |
| `CrdtSet` key builders + prefix queries | `src/crdt/crdt_set.hpp:167-202`, `impl/crdt_set.cpp:462-496` | `ElemsPrefix`/`TombsPrefix` enumeration + `ValueKey`/`PriorityKey` for the four entry families | Owner-internal; a new batch-mode erase primitive follows the existing `SetValue(batch, ...)` overload pattern (`crdt_set.hpp:250-254`) |
| `CrdtSet::mutex_` (recursive) | `crdt_set.hpp:330-337` | Merge-vs-erase mutual exclusion; `PutElems` holds it across batch build+commit (`crdt_set.cpp:691-736`) | The lock the erase primitive must take (note: `PutTombs` is const and lockless — `crdt_set.cpp:749`) |
| `GraphsyncDAGSyncer` guards | `src/crdt/graphsync_dagsyncer.hpp:123-124, 148`; base `dagsyncer.hpp:36` | `IsCIDInCache` (public virtual), `isResolved`/`markResolved` (`dagMutex_`-guarded, `.cpp:799-809`) | Existing public guard surface |
| `CrdtDatastore::IsRootCIDPendingOrActive` | `src/crdt/crdt_datastore.hpp:462` (private), `.cpp:2201-2225` | pending/active root-job guard under `dagWorkerMutex_` | Exists; needs public exposure for GlobalDB |
| `CrdtHeads::GetList` / `GlobalDB::GetCRDTHeadList` | `src/crdt/crdt_heads.hpp:112`, `globaldb.hpp:264` | Head guard (CID ∈ heads of any topic) + maxHeight in `CRDTListResult.second` | Already public — **no new maxHeight API needed** |
| `TransactionManager` map functions | `src/transaction/TransactionManager.cpp:4322-4345, 4433-4458, 4177-4210` | Map-entry key shape (bare tx path, raw put), tracked-map cleanup pattern | D-C4: extend, not replace |
| `CID::toBytes()` | `primitives/cid/cid.hpp` (used at `crdt_datastore.cpp:1529`) | Block-key construction on the SuperGNUS side | Public CID API; already used in SuperGNUS src |

### Supporting
| API | Location | Purpose | When to Use |
|-----|----------|---------|-------------|
| `GlobalDB::GetDataStore()` | `globaldb.hpp:174` | The wrapper whose `batch()` composes the erase | GlobalDB erase impl |
| `GlobalDB::GetRaw/PutRaw/QueryRaw` | `globaldb.hpp:181-196` | Node-local raw-key surface precedent (and test assertion vehicle) | Assertions on physical keys |
| `CrdtDatastore::WaitForJob(cid)` | `crdt_datastore.hpp:439` (public) | Test: wait until a Put's root job completed (seal marker written) | Fixtures before erase |
| `IpfsRocksDb::create(db)` + `RocksdbDatastore` | `globaldb.cpp:62-63, 322-329`; vendored `ipfs_lite/rocksdb/rocksdb.hpp:74-77` (`getDB()`) | Fixture wiring: real block store sharing the temp DB, no pubsub | Erase test fixtures |
| `MerkleDagServiceImpl(ipfsDatastore)` | vendored `merkledag_service_impl.hpp:22-24` | Real seal/remove semantics behind a test DAGSyncer | Fixture wiring |
| `test::removeAllWithRetry` + `testutil/wait_condition.hpp` | `test/src/crdt/*_test.cpp` includes | Fresh-DB rule + async waits | Every erase test |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Staging block delete into shared `BufferBatch` | Calling `removeNode(cid)` after the batch commits | **Rejected** — two separate writes, breaks PRUNE-02/SC-2 atomicity (value-without-block window). `removeNode` stays dormant |
| Staging block delete into shared `BufferBatch` | A batch-aware variant in vendored `RocksdbDatastore` | **Rejected** — D-C3 forbids vendored changes |
| SuperGNUS `BufferBatch` | Raw `rocksdb::WriteBatch` on `getDB()` | Equivalent result; `BufferBatch` is the repo idiom and keeps error mapping — prefer it |
| Exposing raw key builders from `CrdtSet` to GlobalDB | Batch-staging method inside `CrdtSet` | **Use the staging method** — keeps key-layout knowledge inside the owner (D-01) |

**Installation:** none — no packages. (Vendored RocksDB 9.4 confirmed: `thirdparty/rocksdb/include/rocksdb/version.h` MAJOR 9 / MINOR 4.)

## Package Legitimacy Audit

No external packages are installed, recommended, or required by this phase. All code lands in existing internal libraries (`crdt_set`, `crdt_graphsync_dagsyncer`, `crdt_globaldb`, transaction lib) and existing test targets. Vendored dependencies (RocksDB 9.4, ipfs-lite-cpp fork) are untouched per D-C3. Slopcheck gate: not applicable (zero registry installs).

## Architecture Patterns

### System Architecture Diagram

```
                    GlobalDB::Erase(key, cid)            [D-01: one narrow generic API]
                              │
                    ┌─────────▼──────────┐
                    │ GUARDS (inside)     │  head?      GetCRDTHeadList() → CID ∈ heads (any topic)
                    │ fail-closed rejects │  pending?   CrdtDatastore::IsRootCIDPendingOrActive (make public)
                    │                     │  in LRU?    GraphsyncDAGSyncer::IsCIDInCache
                    │                     │  resolved?  assert isResolved(cid) == true  [D-C5]
                    └─────────┬──────────┘
                              │ all guards pass
                    batch = GetDataStore()->batch()      ← the ONE shared RocksDB
                              │
        ┌─────────────────────┼──────────────────────────────┐
        ▼                     ▼                              ▼
  CrdtSet::StageErase   GraphsyncDAGSyncer::          batch->remove(Buffer(key))
  (takes mutex_;         StageBlockRemoval            ← tx→CID map entry (bare
   prefix-scan           (guards already passed;       tx path, same key shape
   /crdt/s/<key>/…        constructs CID::toBytes()    StoreTransactionCID writes)
   /crdt/t/<key>/…        key; NEVER <cid>sealed)
   /crdt/k/<key>/v|p     → batch->remove)
   → batch->remove each)
        │                     │                              │
        └─────────────────────┼──────────────────────────────┘
                              ▼
                     batch->commit()      ONE db->Write(wo_.sync=true)
                              │           [D-01/PRUNE-02 atomicity point]
                              ▼
              (post-commit, no hooks fired — see Pitfall 1)
              Three-part assertion: /crdt/* gone, block gone, <cid>sealed PRESENT
```

Entry point: the erase API (Phase 16: called only by tests). Decision points: the four guards. External boundary: none new — everything is local RocksDB. A reader can trace: guards → one batch → three owner contributions → single commit.

### Recommended Project Structure

```
src/crdt/
├── crdt_set.hpp / impl/crdt_set.cpp        # MODIFIED — batch-staging local erase primitive
│                                             (caller-owned BufferBatch; mutex_; no hooks)
├── graphsync_dagsyncer.hpp / impl/…cpp     # MODIFIED — guarded StageBlockRemoval(cid, batch)
│                                             (concrete class, NOT the DAGSyncer base — see Pitfall 8)
├── crdt_datastore.hpp                      # MODIFIED (1 line class) — IsRootCIDPendingOrActive → public
└── globaldb/
    ├── globaldb.hpp / globaldb.cpp         # MODIFIED — the erase API (D-01) + retain m_dagSyncer member
src/transaction/
└── TransactionManager.hpp / .cpp           # MODIFIED — map-entry deletion lifecycle + tracked-map
                                              cleanup WITHOUT RevertTransaction (plain tx_processed_m erase)
test/src/crdt/
└── (new) crdt_erase_test.cpp or similar    # NEW — fresh-temp-DB fixtures; unit + atomicity +
                                              failure-injection tests; three-part assertions + negative control
test/src/crdt/globaldb_integration.cpp      # MODIFIED (or new globaldb_erase_integration file)
                                              # — single-node GlobalDB-level erase test (SC-1 end-to-end)
```

No new libraries, no new build targets; new files join existing `addtest` targets (test/src/crdt/CMakeLists.txt). `crdt_test` currently links `ipfs-lite-cpp::ipfs_datastore_in_memory` — the erase unit test needs `ipfs-lite-cpp::ipfs_datastore_rocksdb` added to that target's links (or the test lives in `globaldb_integration_test`, which links `crdt_globaldb`; note `crdt_globaldb` links the rocksdb datastore PRIVATE, so direct symbol use requires an explicit link either way).

### Pattern 1: Caller-owned batch staging (existing repo pattern, extend it)
**What:** A module stages its key operations into a caller-owned `BufferBatch`; the caller commits once. Precedent: `CrdtSet::SetValue(const std::unique_ptr<storage::BufferBatch>&, ...)` (`crdt_set.hpp:250-254`) + private batch-mode `SetPriority` (`crdt_set.cpp:541-561`); `CrdtHeads::Write(storage::BufferBatch&, ...)` (`crdt_heads.hpp`, protected).
**When to use:** every multi-owner atomic write — exactly the erase composition.
**Example:**
```cpp
// Shape of the CrdtSet primitive (final signature is planner discretion):
// Source pattern: src/crdt/impl/crdt_set.cpp:596-667 (SetValue batch overload),
//                 src/crdt/impl/crdt_set.cpp:697-733 (PutElems batch build under mutex_)
outcome::result<void> CrdtSet::StageEraseElement(
    const std::string                                          &aKey,
    const std::unique_ptr<storage::BufferBatch>                &aBatch )
{
    std::lock_guard lg( this->mutex_ );  // same recursive mutex PutElems holds
    // 1. prefix-scan ElemsPrefix(aKey)  -> batch->remove(each)
    // 2. prefix-scan TombsPrefix(aKey)  -> batch->remove(each)
    // 3. batch->remove(ValueKey(aKey)); batch->remove(PriorityKey(aKey));
    // 4. DO NOT fire deleteHookFunc_ (see Pitfall 1); return success
}
```

### Pattern 2: Guarded block staging on the concrete DAGSyncer
**What:** `GraphsyncDAGSyncer` gains a method that stages `remove(Buffer(cid.toBytes()))` into the caller's batch; the vendored key format (`encode()` at `datastore_rocksdb.cpp:14-18`) gets exactly one SuperGNUS-side home here. The seal marker (`cid.toString() + "sealed"`, `:48-56`) is a different key and is never constructed by this path — unaddressable by construction.
**When to use:** the block half of every atomic erase.
**Note:** goes on the concrete `GraphsyncDAGSyncer`, not the `DAGSyncer` base interface — a new pure virtual would force `CustomDagSyncer` (test double, `test/src/crdt/crdt_custom_dagsyncer.hpp`) to implement it. GlobalDB must therefore retain the concrete pointer (`std::shared_ptr<GraphsyncDAGSyncer>` member added in `Init`, mirroring how `dagSyncer` is already created there at `globaldb.cpp:344`).

### Pattern 3: Map deletion lifecycle without revert
**What:** The map-entry delete is `batch->remove(Buffer(tx_path))` — the identical key `StoreTransactionCID` writes (`TransactionManager.cpp:4336-4342`). Tracked-map cleanup is a plain `tx_processed_m.erase` under `tx_mutex_m` — the queuing shape of `ProcessDeletion` (`:4310-4320`) but explicitly NOT `RemoveTransactionFromProcessedMaps`, which calls `RevertTransaction` + nonce rollback whenever a tx object is tracked (`:4189-4199`) and can call `DeleteTransaction` (the forbidden tombstone path) when `delete_from_crdt=true`.
**When to use:** always for prune-side cleanup. Phase 16 exposes the lifecycle; Phase 18's TxPruner drives it.

### Pattern 4: Test-only interruption seam (D-03 failure injection)
**What:** A static test hook invoked between staging steps; when set, the erase aborts (batch destroyed uncommitted). Precedent: `GraphsyncDAGSyncer::SetBlacklistBackoffTimeoutForTest` — static, doc-commented "No production caller may invoke this method" (`graphsync_dagsyncer.hpp:134-145`).
**When to use:** SC-2 crash-injection tests: interrupt between (a) CRDT staging, (b) block staging, (c) map staging, (d) commit — then reopen the temp DB and assert all-or-nothing (never value-without-block, never block-without-map-entry).

### Anti-Patterns to Avoid
- **Executing `removeNode` inside the compose:** it issues its own immediate `db->Delete` — a second, non-atomic write (SC-2 violation). Stage instead.
- **Reusing `DeleteCIDBlock` for erasure:** it only evicts the in-memory LRU entry (`graphsync_dagsyncer.cpp:855-862`).
- **Erasing elems without tombs (or k/v without k/p):** asymmetric state skews add-wins resolution on any re-delivery; all four families in one batch.
- **Firing CRDT hooks on physical erase:** drives `ProcessDeletion` → `RevertTransaction` → genuine-spend reversion (Pitfall 1 below).
- **Constructing CRDT keys outside `CrdtSet`, block keys outside the DAGSyncer exposure, or the map key outside TransactionManager's shape:** violates one-owner-per-fact (D-01's whole point).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Atomic multi-key delete | Manual delete sequencing / retry loops | One `BufferBatch` + single `commit()` | RocksDB WriteBatch atomicity + `wo_.sync=true` WAL fsync already guarantee all-or-nothing (`rocksdb_batch.cpp:29-38`, `rocksdb.cpp:69`) |
| Cross-facade transaction coordinator | Two-phase protocol across wrappers | One DB handle behind both facades (`globaldb.cpp:322-329`) — one batch covers all keys | The topology already solved cross-store atomicity |
| Key-format encoders | Copying `/crdt/s…` string building, `toBytes()` encoding, `<cid>sealed` construction into multiple sites | `CrdtSet` builders / the one DAGSyncer staging method / vendored contract untouched | One home per wire fact (hard invariant 2); the seal format must have ZERO new homes in prod code |
| Crash simulation infrastructure | Process-kill harnesses, fault-injection RocksDB `Env` | Test-only interruption seam (Pattern 4) + DB reopen assertions | D-03 scopes crash proof to unit level; Phase 18 owns process-kill |
| Fresh temp-DB fixture machinery | Custom dir management | `test::removeAllWithRetry` + per-test fixed path (repo convention, `crdt_set_test.cpp:61-68`) | Established pattern; v1.1 Phase 12 stale-fixture lesson |

**Key insight:** every mechanical ingredient for PRUNE-02/GATE-03 already exists in this repo; the phase is composition + guards + tests, not infrastructure.

## Common Pitfalls

### Pitfall 1: CRDT delete-hook side effects on physical erase
**What goes wrong:** Mirroring `PutElems`' hook discipline by firing `deleteHookFunc_` post-commit drives `CrdtDatastore::DeleteElementsCallback` → `TransactionManager::DeleteElementCallback` (`TransactionManager.cpp:4392-4403`) → `ProcessDeletion` → `RemoveTransactionFromProcessedMaps` → **`RevertTransaction` + `RollBackPeerConfirmedNonce`** (`:4189-4199`) — reverting the very spends that made the tx prunable, and (with `delete_from_crdt=true`) publishing the forbidden tombstone.
**Why it happens:** `RemoveTransactionFromProcessedMaps` reverts whenever `it->second.tx` exists, regardless of the `delete_from_crdt` flag; the delete-hook path was built for CRDT-logical deletes, not physical prunes.
**How to avoid:** The physical erase fires NO hooks. Tracked-map cleanup is a separate, explicit, plain-erase operation (new, revert-free). Document the no-hook contract in the primitive's Doxygen header.
**Warning signs:** tests seeing nonce rollback or UTXO state changes after an erase; `deleted_data_queue_` activity during erase tests.

### Pitfall 2: The removeNode-chain vs single-WriteBatch conflict (the core design tension)
**What goes wrong:** Reading D-C3 ("reuse the dormant removeNode chain") literally and calling `removeNode(cid)` as the block-erase step produces TWO writes — the batch commit plus a standalone `db->Delete` — so a crash between them leaves block-without-value or value-without-block (exactly what SC-2 forbids).
**Why it happens:** every layer of the chain executes an immediate delete; no layer accepts an external batch; both `Batch` classes privatize their `WriteBatch`.
**How to avoid:** Interpret "reuse the chain" as *reuse its deletion semantics* (delete exactly the `CID::toBytes()` key; never the seal marker; zero vendored changes) — staged into the shared batch by the guarded DAGSyncer exposure. `removeNode()` itself stays dormant.
**Warning signs:** an erase implementation that calls `removeNode`, `MerkleDagServiceImpl::removeNode`, or `RocksdbDatastore::remove` anywhere on the erase path.

### Pitfall 3: Guard placement gaps
**What goes wrong:** (a) head check only against one topic (`IsHead(cid, topic)` needs a topic — must check all topics via `GetCRDTHeadList`); (b) `IsRootCIDPendingOrActive` is private (`crdt_datastore.hpp:462`) — compile error or, worse, someone re-implements the check against the queues; (c) `GlobalDB` has no DAGSyncer member (local var in `Init`), so guards get skipped or downcast hacks appear.
**How to avoid:** Make `IsRootCIDPendingOrActive` public (pure query, zero behavior change); add the `m_dagSyncer` member in `GlobalDB::Init`; compose all four guards (head, pending/active, LRU, isResolved) inside the erase API before any staging.
**Warning signs:** erase API taking a "pre-verified" flag; guards evaluated at call sites.

### Pitfall 4: Test-double topology mismatch
**What goes wrong:** Erase tests built on `CrdtDatastoreTest`'s `InMemoryDatastore` + `CustomDagSyncer` cannot prove the phase's central claims — no shared RocksDB (no one-batch atomicity), no physical seal markers (no GATE-03 assertion), no block bytes in the DB (no three-part assertion).
**How to avoid:** The erase fixture wires the real store: temp `storage::rocksdb` → `IpfsRocksDb::create(db->getDB())` → `RocksdbDatastore` → real `MerkleDagServiceImpl` (ctor takes just an `IpfsDatastore`) — behind either the real `GraphsyncDAGSyncer` (needs graphsync+host: heavy) or an extended `CustomDagSyncer` wrapping the real `MerkleDagServiceImpl` (light; `CustomDagSyncer` already delegates `markResolved`/`isResolved` to its service). For the GlobalDB facade API itself, one single-node `GlobalDBIntegrationTest`-style fixture (real pubsub, `globaldb_integration.cpp:97-138`) covers SC-1 end-to-end.
**Warning signs:** erase tests whose fixture has no `RocksdbDatastore`; atomicity tests asserting on in-memory maps.

### Pitfall 5: Fixture reuse false-greens (D-C6)
**What goes wrong:** Stale DB state from a previous run makes absence-assertions pass vacuously or presence-assertions read leftovers (v1.1 Phase 12 paid for this class).
**How to avoid:** Every erase test: fresh temp DB (`removeAllWithRetry` at start), present-before asserts, absent-after asserts (both stores), seal-marker-still-present, teardown leaves nothing; plus the negative control (SC-1: untouched sibling key byte-identical before/after — snapshot via raw `get` and compare bytes).
**Warning signs:** erase tests sharing fixtures; no present-before step.

### Pitfall 6: Seal-marker addressing in production code
**What goes wrong:** A "complete cleanup" helper constructs `<cid>sealed` (the traversal terminator) and deletes it — fetch-resurrect storms on the single DAG worker (the archive-repl flake shape).
**How to avoid:** No production code path constructs the seal key: the DAGSyncer staging method builds ONLY `cid.toBytes()`; grep-verifiable. Tests MAY construct the raw seal key to assert its presence (tests encode wire facts for physical assertion — with a comment naming the vendored contract as the one prod home).
**Warning signs:** any `+ "sealed"` string concatenation in `src/`.

### Pitfall 7: Compose-window race (staging under CrdtSet lock, committing outside it)
**What goes wrong:** `PutElems` holds `mutex_` across ITS build+commit; if the GlobalDB compose stages CRDT deletes under the lock but commits later, a concurrent merge on the same key can commit in between — the erase then deletes the re-created value.
**Why it's tolerable in Phase 16:** no production caller exists; tests are single-threaded. Phase 18's TxPruner must define the concurrency protocol.
**How to avoid (shape now):** document the window in the API contract; keep the staging method's lock scoped so a Phase-18 caller can wrap compose+commit under a wider critical section if it chooses. Do not invent a locking framework now (YAGNI).
**Warning signs:** erase API trying to hold three module locks simultaneously.

### Pitfall 8: Base-interface pollution
**What goes wrong:** Adding the staging method as a pure virtual on `DAGSyncer` breaks `CustomDagSyncer` and any other double; or forces a default no-op that silently skips guards.
**How to avoid:** Concrete `GraphsyncDAGSyncer` only; GlobalDB holds the concrete `shared_ptr`.

## Code Examples

### The composition (GlobalDB erase — signature is planner discretion; shape is verified)
```cpp
// Sources: globaldb.cpp:322-329 (topology), rocksdb_batch.cpp:23-38 (staging/commit),
//          crdt_set.cpp:596-667 (caller-owned batch pattern), TransactionManager.cpp:4336-4342 (map key)
outcome::result<void> GlobalDB::EraseTransactionEntry( const std::string &key, const CID &cid )
{
    auto crdt_ds = ActiveCRDTDataStore();
    auto store   = ActiveDataStore();
    if ( !crdt_ds || !store || !m_dagSyncer )
    {
        return outcome::failure( Error::GLOBALDB_NOT_STARTED );
    }

    // Guards (D-C5) — all fail-closed, before any staging
    OUTCOME_TRY( auto head_list, GetCRDTHeadList() );          // heads + maxHeight (crdt_heads.cpp:259)
    for ( const auto &[topic, cids] : head_list.first )         // check EVERY topic
    {
        if ( cids.count( cid ) > 0 ) { return outcome::failure( Error::INVALID_PARAMETERS ); }
    }
    if ( crdt_ds->IsRootCIDPendingOrActive( cid ) )             // needs public exposure (crdt_datastore.hpp:462)
    {
        return outcome::failure( Error::INVALID_PARAMETERS );
    }
    if ( m_dagSyncer->IsCIDInCache( cid ) )                     // public virtual (dagsyncer.hpp:36)
    {
        return outcome::failure( Error::INVALID_PARAMETERS );
    }
    OUTCOME_TRY( auto resolved, m_dagSyncer->isResolved( cid ) );
    if ( !resolved ) { return outcome::failure( Error::INVALID_PARAMETERS ); }  // asserted again post-commit

    auto batch = store->batch();                                // ONE WriteBatch, shared DB
    OUTCOME_TRY( crdt_ds->StageEraseElement( key, batch ) );    // /s,/t,/k/v,/k/p  (CrdtSet-owned)
    OUTCOME_TRY( m_dagSyncer->StageBlockRemoval( cid, batch ) );// CID::toBytes() only (never <cid>sealed)
    OUTCOME_TRY( batch->remove( Buffer{}.put( key ) ) );        // tx→CID map entry (raw tx path)
    OUTCOME_TRY( batch->commit() );                             // ONE db->Write, sync=true
    // post-commit: assert isResolved still true; fire NO CRDT hooks (Pitfall 1)
    return outcome::success();
}
```

### Block-key staging (the one new key-format home on the SuperGNUS side)
```cpp
// Source of the key contract: vendored datastore_rocksdb.cpp:14-18 (encode = CID::toBytes())
// and :78-82 (remove = delete of that key). Seal format :48-56 — deliberately NOT constructed here.
outcome::result<void> GraphsyncDAGSyncer::StageBlockRemoval(
    const CID                                 &cid,
    const std::unique_ptr<storage::BufferBatch> &aBatch )
{
    std::lock_guard lock( dagMutex_ );                 // same discipline as removeNode (.cpp:461-465)
    BOOST_OUTCOME_TRY( auto key_bytes, cid.toBytes() );
    Buffer key_buffer;
    key_buffer.put( key_bytes );
    return aBatch->remove( key_buffer );               // stages ONLY the block-bytes key
}
```

### Failure-injection seam (D-03)
```cpp
// Precedent: GraphsyncDAGSyncer::SetBlacklistBackoffTimeoutForTest (graphsync_dagsyncer.hpp:134-145)
// Test-only interruption between staging steps; production code never sets it.
static std::function<void( int step )> erase_interrupt_for_test_;  // step: 1=after CRDT, 2=after block, 3=after map
// In the erase impl: if ( erase_interrupt_for_test_ ) { erase_interrupt_for_test_( step ); }
// The test's callback throws/aborts; the unique_ptr batch dies uncommitted;
// reopen the temp DB and assert: EITHER everything present OR everything absent — never partial.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| CRDT `DeleteKey`/`DeleteTransaction` (network-wide tombstones) as "deletion" | Physical local batch erase for pruning (D-C2) | v1.2 decision | Pruning stays node-local; full/archive nodes unaffected |
| Value-only cleanup assumptions | Two-level erase (CRDT entries + raw block) in one batch | v1.2 PRUNE-02 | Raw blocks are the bulk of the footprint; atomicity required |
| Immediate `removeNode` delete | Batch-staged block delete | Phase 16 | Single-WriteBatch atomicity (the D-C3/PRUNE-02 reconciliation) |
| Hook-driven cleanup (`ProcessDeletion` → revert) | Plain tracked-map erase for pruned keys | Phase 16 | Genuine spends not reverted |

**Deprecated/outdated (this repo):** none being removed — Phase 16 adds; `DeleteKey`/`DeleteTransaction`/`removeNode` all remain untouched (grep gate proves non-use in the new surface).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | CI fetches the GeniusVentures `thirdparty` release into `thirdparty/` at build time (local dev uses the workspace-level `/Users/henriqueklein/gnus/thirdparty`) | Environment Availability | Test include/link paths differ in CI — mitigate by using the existing CMake target names (`ipfs-lite-cpp::ipfs_datastore_rocksdb`) exactly as `crdt_globaldb` does [CITED: `.planning/research/STACK.md`, verified by milestone research; not re-verified this session] |
| A2 | The top-level build entry (no CMakeLists.txt at SuperGNUS root; configured tree at `build/OSX/Release`) — `cmake --build build/OSX/Release` is the working build command on this machine | Validation Architecture | Wrong build dir name in test commands only; targets and `addtest` mechanics are unaffected [VERIFIED: `build/OSX/Release/CTestTestfile.cmake` + `test_bin/crdt_test` exist] |

**All other claims were verified against source this session** (file:line cited inline).

## Open Questions

1. **Where does the GlobalDB-level erase test live — extend `globaldb_integration.cpp` or a new file?**
   - What we know: `globaldb_integration_test` links `crdt_globaldb` only; single-node fixtures are established there (`addNode`, ~seconds startup); `crdt_test` has the light fixtures but no GlobalDB (requires pubsub).
   - What's unclear: whether the team prefers a new dedicated `*_test.cpp` in the same target vs. appending to the existing file (discretion item).
   - Recommendation: new file in the `globaldb_integration_test` target (e.g., `globaldb_erase_test.cpp`) — keeps the erase suite isolated per D-C6 fresh-DB discipline; CMake change is one line in an existing `addtest`.
2. **Does the erase API assert `isResolved` post-commit by re-reading, or return and let tests assert?**
   - What we know: SC-3 says "isResolved asserted before and after erase"; guards cover before; after is a one-line re-read.
   - Recommendation: re-read inside the API (fail loudly if the marker vanished — would indicate a seal-addressing bug) AND test-assert independently.
3. **Should `StageEraseElement` return the count of staged deletes (for observability/empty-detection) or void?**
   - Planner discretion (signature-level). An empty result (no elems/tombs/k entries found) is worth surfacing — erasing a nonexistent key should probably be an error or a documented no-op so Phase 17/18 callers can distinguish.
4. **Exact tracked-map-cleanup surface on TransactionManager** (plain-erase method name, whether it also drives the queued-async shape of `ProcessDeletion`): needs a decision balancing "no revert" against reuse of the existing queue plumbing. Recommendation: synchronous plain erase under `tx_mutex_m` for Phase 16 (no production caller; simplest correct shape); Phase 18 revisits if the service needs async.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Vendored RocksDB | atomicity substrate | ✓ | 9.4 (`version.h` MAJOR 9/MINOR 4) | — |
| Vendored ipfs-lite-cpp (workspace) | block store, seal contract | ✓ | fork at `/Users/henriqueklein/gnus/thirdparty/ipfs-lite-cpp` | — |
| cmake | build | ✓ | 3.31.4 | — |
| ctest | test runner | ✓ | (bundled) | — |
| Configured build tree | incremental builds/tests | ✓ | `build/OSX/Release` (`test_bin/crdt_test` present) | reconfigure |
| clang-format / clang-tidy | repo verification gate | per CLAUDE.md | — | must be run; state if unavailable |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** none.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest via the repo `addtest` macro (`cmake/functions.cmake` — auto-registers ctest, auto `disable_clang_tidy`) |
| Config file | none needed — per-target registration in `test/src/crdt/CMakeLists.txt` |
| Quick run command | `cmake --build build/OSX/Release --target crdt_test && build/OSX/Release/test_bin/crdt_test --gtest_filter='CrdtErase*'` |
| Full suite command | `cmake --build build/OSX/Release && ctest --test-dir build/OSX/Release --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| PRUNE-02 (atomicity) | One batch erases CRDT entries + block + map entry; interrupted stage-then-crash leaves NO partial state | unit (fresh temp DB + interruption seam + DB reopen) | `test_bin/crdt_test --gtest_filter='*EraseAtomicity*'` | ❌ Wave 0 |
| PRUNE-02 (composition) | Guarded GlobalDB erase removes all three families end-to-end on a real single-node GlobalDB | integration (1 node, pubsub) | `ctest --test-dir build/OSX/Release -R globaldb_integration` | ❌ Wave 0 |
| PRUNE-02 (tombstone ban) | `DeleteKey`/`DeleteTransaction`/`removeNode`/`DeleteCIDBlock` absent from the new erase surface | static grep (source-scan test or plan-level verify step; no repo precedent — first of its kind) | `grep -n "DeleteKey\|DeleteTransaction\|removeNode\|DeleteCIDBlock" src/crdt/globaldb/globaldb.{hpp,cpp} src/crdt/crdt_set.hpp …` (scoped to the new surface) | ❌ Wave 0 (mechanism choice is planner's) |
| GATE-03 (seal retention) | `<cid>sealed` key still present after erase; `isResolved` true before and after | unit + integration (raw-key assertion in test) | `test_bin/crdt_test --gtest_filter='*SealRetained*'` | ❌ Wave 0 |
| GATE-03 (unaddressable) | No production code path constructs the seal key | static grep (`+ "sealed"` / `sealed` concat in `src/`) | grep gate as above | ❌ Wave 0 |
| SC-1 three-part + negative control | Present-before, absent-after (both stores), seal-present; sibling byte-identical | unit + integration | covered by the two suites above | ❌ Wave 0 |
| SC-3 guards | Head CID / pending-active / LRU-cached each rejected fail-closed | unit (fixture places CID in each state) | `test_bin/crdt_test --gtest_filter='*EraseGuard*'` | ❌ Wave 0 |
| SC-5 no behavior change | Default build green; full existing ctest suite green; `removeNode` still zero production callers | full suite + grep | `ctest --test-dir build/OSX/Release` | ✅ (existing suite) |

### Sampling Rate
- **Per task commit:** targeted quick run (`--gtest_filter` on the new suites) + clang-format/clang-tidy on changed files
- **Per wave merge:** `ctest --test-dir build/OSX/Release --output-on-failure` (full suite — SC-5)
- **Phase gate:** full suite green + grep gates clean before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `test/src/crdt/*erase*_test.cpp` — fresh-temp-DB fixture wiring the REAL `RocksdbDatastore` from the temp DB handle (`IpfsRocksDb::create(db->getDB())`); covers PRUNE-02 atomicity/failure-injection, GATE-03 retention, guard rejections
- [ ] GlobalDB-level erase integration test (new file in `globaldb_integration_test` target) — SC-1 end-to-end with negative control
- [ ] CMake: add `ipfs-lite-cpp::ipfs_datastore_rocksdb` (and any CID-CRD deps) to the unit test target's links
- [ ] Decide grep-gate mechanism (ctest source-scan vs. plan verification step) — no existing precedent to copy
- [ ] Framework install: none needed (gtest already integrated)

## Security Domain

ASVS level 1 (config: `security_enforcement: true`, `security_asvs_level: 1`, `security_block_on: high`).

### Applicable ASVS Categories
| ASVS Category | Applies | Standard Control |
|---------------|---------|------------------|
| V2 Authentication | no | no new network/auth surface (local storage primitives only) |
| V3 Session Management | no | — |
| V4 Access Control | yes (narrow) | fail-closed guard ensemble on the erase API: in-use CIDs (head/pending/LRU) are refused by shape; API takes key+CID and validates internally — callers cannot bypass guards |
| V5 Input Validation | yes | `outcome::result` error contract; guard predicates validate CID/key state before any staging; empty/invalid inputs rejected (`INVALID_PARAMETERS` pattern per `GlobalDB::Error`) |
| V6 Cryptography | no | CID handling uses existing library codecs only |
| V7 Error handling | yes | typed `enum class Error` results; no swallowed failures (repo standard) |
| V8 Data protection | yes (invariant) | destructive operation bounded by construction: seal markers unaddressable, `/utxo*`/certs/heads never in the erase key set (only the three enumerated families are staged) |

### Known Threat Patterns for this stack
| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Destructive API misuse (wrong CID/key → history destruction) | Tampering | Guards inside the API (D-01) — wrong deletion impossible by API shape; single narrow entry point |
| Partial-erase crash state → inconsistent store | Tampering/Denial | One WriteBatch, sync commit — all-or-nothing (SC-2) |
| Seal-marker erasure → re-fetch storms / DoS on the single DAG worker | Denial | Seal key unconstructible on the erase path (GATE-03); grep gate |
| Hook-driven side effects during erase → spend reversion | Tampering | No-hook contract on the physical erase (Pitfall 1) |

## Sources

### Primary (HIGH confidence — verified in this repo this session)
- `src/storage/rocksdb/rocksdb.{hpp,cpp}` (wrapper, `batch()`, `getDB()`, `wo_.sync=true` :69, `remove` :247-256)
- `src/storage/rocksdb/rocksdb_batch.{hpp,cpp}` (private WriteBatch :29; staging :10-27; commit = one `db_->Write` :29-38)
- `src/storage/face/write_batch.hpp` (BufferBatch contract: put/remove/commit/clear)
- `src/crdt/crdt_set.hpp` + `impl/crdt_set.cpp` (key builders :462-496; batch-mode SetValue :596-667; PutElems mutex+batch+post-commit hooks :669-747; PutTombs lockless :749-802; recursive `mutex_` :330-337; namespace constants :341-345)
- `src/crdt/graphsync_dagsyncer.hpp` + `impl/graphsync_dagsyncer.cpp` (removeNode + dagMutex_ :461-465; isResolved/markResolved :799-809; IsCIDInCache :837-839; DeleteCIDBlock LRU-only :855-862; test-seam precedent :134-145; LRU cache :250-278)
- `src/crdt/dagsyncer.hpp` (base interface: IsCIDInCache public virtual :36; removeNode inherited virtual)
- `src/crdt/crdt_datastore.hpp` + `impl/crdt_datastore.cpp` (IsRootCIDPendingOrActive private :462 / impl :2201-2225; WaitForJob public :439; UpdateCRDTHeads seal-writing :2111-2199; GetLocalDeltaKeyValues :2280-2302; private set_/heads_/dagSyncer_ :480-484)
- `src/crdt/crdt_heads.hpp` + `impl/crdt_heads.cpp` (IsHead :72; GetList + maxHeight :112, :259-284)
- `src/crdt/globaldb/globaldb.{hpp,cpp}` (two-facades wiring :62-63, :322-329; dagSyncer local var :344; GetDataStore :174; GetRaw family :181-196; GetCRDTHeadList :264; no dagSyncer member — header :318-352)
- `src/transaction/TransactionManager.{hpp,cpp}` (StoreTransactionCID :4322-4345; GetTransactionCID :4433-4458; ProcessDeletion :4310-4320; RemoveTransactionFromProcessedMaps revert semantics :4177-4210; DeleteTransaction tombstone :3524-3540; TRANSACTION_BASE_FORMAT hpp:433)
- Vendored: `ipfs_lite/ipfs/impl/datastore_rocksdb.{hpp,cpp}` (encode=toBytes :14-18; seal key format :48-56; remove :78-82), `ipfs_lite/rocksdb/rocksdb.{hpp,cpp}` (create-from-DB :60-71; getDB :74; immediate remove :131-140), `ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.{hpp,cpp}` (ctor takes IpfsDatastore; removeNode→block_service_->remove :39-42), `ipfs_lite/ipfs/impl/ipfs_block_service.cpp` (:42-44)
- `thirdparty/rocksdb/include/rocksdb/version.h` (9.4)
- Build/test: `src/crdt/CMakeLists.txt` (crdt_set :12, crdt_graphsync_dagsyncer :50, crdt_datastore :74), `src/crdt/globaldb/CMakeLists.txt` (crdt_globaldb links), `test/src/crdt/CMakeLists.txt` (crdt_test, globaldb_integration_test), `cmake/functions.cmake` (addtest + disable_clang_tidy :27)
- Tests: `test/src/crdt/crdt_set_test.cpp` (fresh-DB-per-test pattern), `crdt_datastore_test.cpp` (fixture + InMemoryDatastore/CustomDagSyncer), `globaldb_integration.cpp` (single/multi-node GlobalDB fixture :78-138), `crdt_custom_dagsyncer.{hpp,cpp}` (delegates markResolved/isResolved)
- Grep verification: `removeNode` has zero production callers in `src/` outside its own declaration/override (checked 2026-10-15)

### Secondary (MEDIUM-HIGH confidence)
- `.planning/research/STACK.md`, `ARCHITECTURE.md`, `PITFALLS.md`, `SUMMARY.md` — milestone research (file:line-verified 2026-10-15; re-cross-checked where load-bearing this session)
- RocksDB WriteBatch atomicity + WAL sync semantics — [CITED: vendored `thirdparty/rocksdb/include/rocksdb/db.h` (`DB::Write`, `WriteBatch`) as indexed by STACK.md; external docs corroboration listed in STACK.md sources]

### Tertiary (LOW confidence)
- none used

## Metadata

**Confidence breakdown:**
- Standard stack (internal APIs): HIGH — every cited symbol read in source this session
- Architecture (composition shape): HIGH — mechanics verified; open items are signature/discretion-level only
- Pitfalls: HIGH — five of eight hazards verified line-level in source (hook-revert path, private guard, missing dagSyncer member, immediate-delete chain, test-double topology); the remainder are documented risk shapes from milestone research
- Build/test wiring: HIGH with A2 caveat (build-tree path)

**Research date:** 2026-10-15
**Valid until:** 2026-11-14 (stable internal codebase; re-verify only if `src/crdt` or `src/storage` changes land first)
