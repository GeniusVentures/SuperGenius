# Phase 16: Erasure Primitives & Bookkeeping Hardening - Pattern Map

**Mapped:** 2026-10-15
**Files analyzed:** 8 (6 modified source/header/CMake units + 2 new test files)
**Analogs found:** 8 / 8 (7 exact, 1 exact-with-carve-out; 1 sub-item — the grep gate — has no repo precedent)

Every excerpt below was read from source this session with the cited line numbers. Line numbers refer to the current `develop` tree (commit 317b7d674).

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/crdt/crdt_set.hpp` + `src/crdt/impl/crdt_set.cpp` | service (CRDT set domain owner) | batch staging (prefix-scan → staged deletes) | same files: batch-mode `SetValue` (`crdt_set.hpp:250-254`, `crdt_set.cpp:596-667`) and `PutElems` mutex+batch+commit (`crdt_set.cpp:669-747`) | exact |
| `src/crdt/graphsync_dagsyncer.hpp` + `src/crdt/impl/graphsync_dagsyncer.cpp` | service (block-storage owner) + test seam | guard query → batch staging | `removeNode` lock discipline (`impl/graphsync_dagsyncer.cpp:461-465`); test seam `SetBlacklistBackoffTimeoutForTest` (`graphsync_dagsyncer.hpp:134-145`, member `:197-198`) | exact |
| `src/crdt/crdt_datastore.hpp` (1-line visibility change; `.cpp` unchanged) | service (guard owner) | query | `WaitForJob` public precedent directly above the private block (`crdt_datastore.hpp:439` vs private `:462`) | exact |
| `src/crdt/globaldb/globaldb.hpp` + `.cpp` | facade/composer (the erase API, D-01) | guard ensemble → one batch → single commit | `GetRaw/PutRaw/QueryRaw` lifecycle-safe wrappers (`globaldb.cpp:746-785`); two-facades wiring in `Init` (`globaldb.cpp:322-344`) | exact shape |
| `src/transaction/TransactionManager.hpp` + `.cpp` | service (tx→CID map owner) | CRUD delete + tracked-map cleanup | `StoreTransactionCID` (`TransactionManager.cpp:4322-4345`); `RemoveTransactionFromProcessedMaps` (`:4177-4210`) — shape only, semantics differ (see carve-out) | exact (with anti-pattern carve-out) |
| `test/src/crdt/*erase*_test.cpp` (NEW — unit; exact name is discretion) | test | CRUD assertions on fresh temp DB | `crdt_set_test.cpp` fresh-DB-per-test (`:60-72`); anti-analog `crdt_datastore_test.cpp:64-79` (do NOT copy its InMemoryDatastore topology) | exact |
| `test/src/crdt/globaldb_erase_test.cpp` (NEW — integration, joins `globaldb_integration_test` target) | test (integration) | request-response via real pubsub node | `GlobalDBIntegrationTest::TestNodeCollection::addNode` (`globaldb_integration.cpp:97-138`) | exact |
| `test/src/crdt/CMakeLists.txt` | config | — | its own `addtest` blocks (`:1-10`, `:36-43`) and `target_link_libraries` (`:16-30`) | exact |

## Pattern Assignments

### `src/crdt/crdt_set.hpp` + `src/crdt/impl/crdt_set.cpp` (service, batch staging)

**Analog:** the same file pair — the batch-mode `SetValue` overload is the exact precedent for a caller-owned-batch primitive, and `PutElems` is the exact precedent for mutex scope + commit + (for erase: deliberately absent) hooks.

**Declaration pattern — caller-owned batch overload** (`crdt_set.hpp:250-254`):
```cpp
outcome::result<bool> SetValue( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                const std::string                           &aKey,
                                const std::string                           &aID,
                                const Buffer                                &aValue,
                                uint64_t                                     aPriority );
```
The new `StageErase...` declaration goes beside it, public, with a Doxygen comment carrying the no-hook contract (see Pitfall 1 below).

**Null-batch guard pattern** (`crdt_set.cpp:602-605`, same check in batch `SetPriority` `:545-548`):
```cpp
if ( aDataStore == nullptr )
{
    return outcome::failure( boost::system::error_code{} );
}
```

**Key builders to reuse — never re-derive** (`crdt_set.cpp:468-496`):
```cpp
HierarchicalKey CrdtSet::ElemsPrefix( const std::string &aKey ) const   // /namespace/s/<key>
HierarchicalKey CrdtSet::TombsPrefix( const std::string &aKey ) const   // /namespace/t/<key>
HierarchicalKey CrdtSet::KeysKey( std::string_view aKey ) const        // /namespace/k/<key>
HierarchicalKey CrdtSet::ValueKey( const std::string &aKey ) const      // /namespace/k/<key>/v
HierarchicalKey CrdtSet::PriorityKey( const std::string &aKey ) const   // /namespace/k/<key>/p
```
Namespace constants are `static constexpr std::string_view` at `crdt_set.hpp:341-345`. The erase primitive stages deletes for ALL FOUR families (`/s` prefix-scan, `/t` prefix-scan, `/k/<key>/v`, `/k/<key>/p`) — asymmetric erasure is a listed anti-pattern.

**Mutex + batch + single-commit discipline** (`crdt_set.cpp:690-736`, condensed):
```cpp
{
    std::lock_guard lg( this->mutex_ );            // recursive mutex, hpp:337
    auto batchDatastore = this->dataStore_->batch();
    for ( auto &elem : aElems )
    {
        Buffer keyBuffer;
        keyBuffer.put( kNamespace.GetKey() );
        BOOST_OUTCOME_TRY( batchDatastore->put( std::move( keyBuffer ), Buffer() ) );
    }
    auto commitResult = batchDatastore->commit();
    if ( commitResult.has_failure() )
    {
        return outcome::failure( commitResult.error() );
    }
} // lg released here
```
Note: the erase primitive stages under `mutex_` but does NOT commit — the caller (GlobalDB) commits the shared batch. Document the compose window in the Doxygen header (RESEARCH Pitfall 7); do not add new locking.

**Hook contract — copy the comment discipline, invert the action.** `SetValue` batch overload (`crdt_set.cpp:662-666`) documents why hooks must not fire pre-commit. For erase, `deleteHookFunc_` must NEVER fire at all — firing it drives `TransactionManager::DeleteElementCallback` → `ProcessDeletion` → `RevertTransaction` (genuine-spend reversion; `TransactionManager.cpp:4177-4210`). Say this in the primitive's header comment.

---

### `src/crdt/graphsync_dagsyncer.hpp` + `src/crdt/impl/graphsync_dagsyncer.cpp` (service, guard query → batch staging)

**Analog A — `dagMutex_` lock discipline** (`impl/graphsync_dagsyncer.cpp:461-465`):
```cpp
outcome::result<void> GraphsyncDAGSyncer::removeNode( const CID &cid )
{
    std::lock_guard lock( dagMutex_ );
    return dagService_->removeNode( cid );
}
```
The new `StageBlockRemoval(cid, batch)` takes the same lock, but instead of calling `dagService_->removeNode` (immediate standalone `db->Delete` — non-atomic, stays dormant per PRUNE-02), it stages `Buffer(cid.toBytes())` into the caller's batch.

**Analog B — test-seam precedent** (`graphsync_dagsyncer.hpp:134-145`, member at `:197-198`):
```cpp
/**
 * @brief Test-only configuration seam for the blacklist backoff timeout
 *        (developer directive 2026-09-03).
 * ...
 * @note ... No production caller may invoke this method.
 */
static void SetBlacklistBackoffTimeoutForTest( uint64_t override_ms );
...
/// Test-only flat override (milliseconds) for getBackoffTimeout; 0 == production formula.
static std::atomic<uint64_t> blacklist_backoff_override_ms_for_test_;
```
Copy this shape verbatim for the erase interruption seam (D-03): static setter + static hook member, "No production caller may invoke this method" note. The interruption hook itself most naturally lives on `GlobalDB` (which owns the compose sequence), styled exactly like this.

**Guard surface already public** (`graphsync_dagsyncer.hpp:148`, `:123-124`; base `dagsyncer.hpp:36`): `IsCIDInCache` (`cpp:837-840`, LRU membership), `isResolved` (`cpp:805-809`, `dagMutex_`-guarded, delegates to `dagService_->isResolved`). Use as-is for the D-C5 guards.

**Placement rule (Pitfall 8):** the method goes on the concrete `GraphsyncDAGSyncer` ONLY — not on the `DAGSyncer` base (`dagsyncer.hpp:16-44`) — so `CustomDagSyncer` (`test/src/crdt/crdt_custom_dagsyncer.hpp`) is untouched.

**Block-key format — the one new SuperGNUS-side home.** Vendored contract (`/Users/henriqueklein/gnus/thirdparty/ipfs-lite-cpp/ipfs_lite/ipfs/impl/datastore_rocksdb.cpp:14-18`):
```cpp
inline IPFS::outcome::result<common::Buffer> encode( const CID &value )
{
    BOOST_OUTCOME_TRY( auto encoded, value.toBytes() );
    return common::Buffer( std::move( encoded ) );
}
```
Seal marker (`:48-56`) is a DIFFERENT key (`cid.toString() + "sealed"`) and must never be constructed by the new code — grep-verifiable (`+ "sealed"` must not appear in `src/`).

---

### `src/crdt/crdt_datastore.hpp` (service, query — 1-line visibility change)

**Analog:** `WaitForJob` at `crdt_datastore.hpp:439` — an existing public query sitting directly above the `private:` block whose first member is the target at `:462`:
```cpp
outcome::result<CID> WaitForJob( const CID &cid );        // :439 public

private:
...
bool IsRootCIDPendingOrActive( const CID &cid );           // :462 → move above the private:
bool IsRootCIDPendingOrActiveLocked( const CID &cid ) const; // :463 stays private
```
Move `IsRootCIDPendingOrActive` (impl `impl/crdt_datastore.cpp:2201-2225`) up beside `WaitForJob`; keep `...Locked` private. Pure visibility change, zero behavior change. Also relevant: `CrdtDatastore` holds `set_` (`:480`) and `dagSyncer_` (`:484`) — GlobalDB reaches the set through `CrdtDatastore`'s existing public surface or a new forwarder, matching how `GetHeadList` already forwards.

---

### `src/crdt/globaldb/globaldb.hpp` + `.cpp` (facade/composer, guard ensemble → one batch → single commit)

**Analog A — lifecycle-safe wrapper shape** (`globaldb.cpp:746-785`). Every new GlobalDB method that touches store handles copies this exact shape:
```cpp
outcome::result<GlobalDB::Buffer> GlobalDB::GetRaw( const Buffer &key ) const
{
    auto datastore = ActiveDataStore();
    if ( !datastore )
    {
        return outcome::failure( std::errc::operation_canceled );
    }
    return datastore->get( key );
}
```
(`ActiveDataStore()`/`ActiveCRDTDataStore()` take `lifecycle_mutex_`; rationale comment at `globaldb.cpp:752-756`.)

**Analog B — the two-facades topology the erase composes over** (`globaldb.cpp:322-344`):
```cpp
auto ipfsDBResult = IpfsRocksDb::create( m_datastore->getDB() );      // SAME underlying DB
...
auto ipfsDataStore = std::make_shared<RocksdbDatastore>( ipfsDBResult.value() );
...
auto dagSyncer = std::make_shared<GraphsyncDAGSyncer>( ipfsDataStore, graphsync, host );
```
`dagSyncer` is a local variable here — the phase adds a `std::shared_ptr<GraphsyncDAGSyncer> m_dagSyncer;` member assigned in `Init` (concrete type, per Pitfall 8). Test fixtures reproduce exactly these three lines.

**Analog C — declaration placement + error enum** (`globaldb.hpp:174-196` raw-key family, `:81-90` `enum class Error : uint8_t`, `:370` `OUTCOME_HPP_DECLARE_ERROR_2( sgns::crdt, GlobalDB::Error )`). The erase API declares beside `GetRaw/PutRaw` and reuses existing error values (`INVALID_PARAMETERS` for guard rejections, `GLOBALDB_NOT_STARTED` for missing handles) — no new enum entries unless the planner finds one genuinely needed. Error-to-string mapping lives at the top of `globaldb.cpp` (`:40-52`) — extend the switch if an error is added.

**Head guard input:** `GlobalDB::GetCRDTHeadList()` (`globaldb.cpp:793-801`) → `CrdtHeads::GetList` (`crdt_heads.hpp:112`, impl returns heads + maxHeight). Check the CID against heads of EVERY topic — `IsHead(cid, topic)` (`crdt_heads.hpp:72`) takes a topic, so a single-topic call is the known gap (Pitfall 3a).

**Batch substrate** (`src/storage/rocksdb/rocksdb_batch.cpp:23-38`):
```cpp
outcome::result<void> rocksdb::Batch::remove( const Buffer &key )
{
    batch_.Delete( make_slice( key ) );
    return outcome::success();
}
outcome::result<void> rocksdb::Batch::commit()
{
    auto status = db_.db_->Write( db_.wo_, &batch_ );   // wo_.sync = true (rocksdb.cpp:69)
    ...
}
```
`store->batch()` returns `std::unique_ptr<BufferBatch>` — the compose uses `GetDataStore()->batch()` once and hands the same `unique_ptr<storage::BufferBatch>&` to all three staging calls (exactly how `CrdtSet::PutElems` passes `batchDatastore` into `SetValue(batchDatastore, ...)`, `crdt_set.cpp:718`).

**CrdtHeads protected batch-mode precedent** (`crdt_heads.hpp:128-131`): `Write(storage::BufferBatch&, ...)` — second example of the caller-owned-batch idiom, for reference only (heads are guarded, never erased, this phase).

---

### `src/transaction/TransactionManager.hpp` + `.cpp` (service, CRUD delete + tracked-map cleanup)

**Analog A — map-entry write whose key the deletion copies verbatim** (`TransactionManager.cpp:4322-4345`):
```cpp
outcome::result<void> TransactionManager::StoreTransactionCID( const std::string &key, const std::string &cid )
{
    ...
    auto datastore = globaldb_m ? globaldb_m->GetDataStore() : nullptr;
    if ( !datastore )
    {
        m_logger->error( "RocksDB datastore unavailable, cannot store CID for tx {}", key );
        return outcome::failure( std::errc::bad_file_descriptor );
    }
    crdt::GlobalDB::Buffer key_buffer;
    key_buffer.put( key );                       // ← the map key IS the bare tx path
    ...
    BOOST_OUTCOME_TRY( datastore->put( key_buffer, value_buffer ) );
}
```
The deletion lifecycle stages `batch->remove(Buffer(GetTransactionPath(...)))` — the identical key, same shape. `GetTransactionPath` overloads at `:2202-2215` build `GetBlockChainBase() + GeniusTransaction::GetTransactionFullPath(tx_hash)`; declared `static` at `TransactionManager.hpp:317-319`. The map key must be built by TransactionManager (the owner), staged via a new owner method — GlobalDB does not concatenate it itself (D-01).

**Analog B — read side / iteration shape** (`GetTransactionCID`, `:4433-4458`): iterates `GetMonitoredNetworkIDs()`, buffers the path, `datastore->get`. Deletion-side lookups follow the same network-iteration shape.

**Analog C — tracked-map cleanup, with the carve-out** (`RemoveTransactionFromProcessedMaps`, `:4177-4210`):
```cpp
{
    std::unique_lock tx_lock( tx_mutex_m );
    auto             it = tx_processed_m.find( transaction_key );
    if ( it != tx_processed_m.end() )
    {
        if ( it->second.tx )
        {
            BOOST_OUTCOME_TRY( RevertTransaction( *it->second.tx ) );   // ← DO NOT COPY
            if ( delete_from_crdt )
            {
                ... DeleteTransaction( transaction_key, topics );        // ← forbidden tombstone path (D-C2)
            }
            account_m->RollBackPeerConfirmedNonce( ... );                // ← DO NOT COPY
        }
        tx_processed_m.erase( it );                                       // ← COPY ONLY THIS
        found = true;
    }
}
```
The new prune-side cleanup keeps: `std::unique_lock tx_lock( tx_mutex_m )` + `find` + `erase` + debug-log-on-miss (`:4205-4208`). It must NOT call `RevertTransaction`, `RollBackPeerConfirmedNonce`, or `DeleteTransaction`. Queued-call shape reference: `ProcessDeletion` (`:4310-4320`) — Phase 16 uses the synchronous plain erase (RESEARCH open question 4 recommendation).

---

### `test/src/crdt/*erase*_test.cpp` (NEW, test, fresh-DB CRUD assertions)

**Analog A — fresh temp DB per test** (`crdt_set_test.cpp:60-72`):
```cpp
// Remove leftover database
std::string databasePath = "supergenius_crdt_set_test_set_value";
test::removeAllWithRetry( databasePath );

// Create new database
rocksdb::Options options;
options.create_if_missing = true;  // intentionally
auto dataStoreResult = rocksdb::create( databasePath, options );
auto dataStore = dataStoreResult.value();
```
Includes to copy (`crdt_set_test.cpp:1-7`): `<gtest/gtest.h>`, `<storage/rocksdb/rocksdb.hpp>`, `"outcome/outcome.hpp"`, `<testutil/outcome.hpp>`, `<testutil/remove_all.hpp>`, `<boost/filesystem.hpp>`. Assertion macros: `EXPECT_OUTCOME_TRUE_1`, `EXPECT_OUTCOME_EQ`, `EXPECT_OUTCOME_FALSE_1` from `<testutil/outcome.hpp>`.

**Anti-analog — do NOT copy the `CrdtDatastoreTest` fixture topology** (`crdt_datastore_test.cpp:78-79`):
```cpp
ipfsDataStore_ = std::make_shared<InMemoryDatastore>();   // cannot prove one-batch atomicity
dagSyncer_     = std::make_shared<CustomDagSyncer>( ipfsDataStore_ );
```
The erase fixture instead wires the real store (mirroring `globaldb.cpp:322-344`):
```cpp
auto ipfsDBResult = ipfs_lite::rocksdb::create( db->getDB() );
auto ipfsDataStore = std::make_shared<ipfs_lite::ipfs::RocksdbDatastore>( ipfsDBResult.value() );
```
behind a real `MerkleDagServiceImpl(ipfsDatastore)` — either via the real `GraphsyncDAGSyncer` (heavy) or an extended `CustomDagSyncer` wrapping the real service (light; it already delegates `markResolved`/`isResolved`).

**Seal-marker test assertion (test-only raw key construction)** — construct `cid.toString().value() + "sealed"` ONLY in the test, with a comment naming the vendored contract (`datastore_rocksdb.cpp:48-56`) as the one production home. Assert presence via `GlobalDB::GetRaw`/`datastore->get` before AND after erase.

**Async wait before erase:** `CrdtDatastore::WaitForJob(cid)` (`crdt_datastore.hpp:439`, public) or `test::assertWaitForCondition` (`testutil/wait_condition.hpp`, used at `globaldb_integration.cpp:152`).

**Interruption tests (D-03):** set the static seam callback so it throws/aborts between staging steps; the `unique_ptr` batch dies uncommitted; reopen the temp DB (`rocksdb::create` on the same path) and assert all-or-nothing.

### `test/src/crdt/globaldb_erase_test.cpp` (NEW, integration test, real node)

**Analog — `GlobalDBIntegrationTest::TestNodeCollection::addNode`** (`globaldb_integration.cpp:97-138`): per-test `removeAllWithRetry(basePath)` + key pair + `GossipPubSub` start + scheduler/network/generator wiring + `GlobalDB::New(...)` + `db->Start()`. Copy the whole block; single node suffices (SC-1). File header comment + logger config block at `:1-76`. Negative control (D-C6): write a sibling key, snapshot bytes via `GetRaw` before erase, byte-compare after.

### `test/src/crdt/CMakeLists.txt` (config)

**Analog — its own `addtest` blocks** (`:1-10`, `:36-43`):
```cmake
addtest(crdt_test
    crdt_hierarchical_key_test.cpp
    ...
)

target_link_libraries(crdt_test
    crdt_datastore
    ...
    ipfs-lite-cpp::ipfs_datastore_in_memory
    ...
)

addtest(globaldb_integration_test
    globaldb_integration.cpp
)
target_link_libraries(globaldb_integration_test
    crdt_globaldb
)
```
Wiring: append the new unit test filename to the `crdt_test` list and add `ipfs-lite-cpp::ipfs_datastore_rocksdb` to its `target_link_libraries` (`:16-30`); add the new integration file to the `globaldb_integration_test` sources (`:36-38`). `addtest` (in `cmake/functions.cmake`, with `disable_clang_tidy` at `:27`) auto-registers ctest. No new targets.

## Shared Patterns

### Caller-owned BufferBatch staging
**Source:** `src/crdt/crdt_set.hpp:250-254`, `src/crdt/impl/crdt_set.cpp:596-667`, `:541-561`; `src/crdt/crdt_heads.hpp:128-131` (protected `Write`)
**Apply to:** `CrdtSet` erase primitive, `GraphsyncDAGSyncer::StageBlockRemoval`, `TransactionManager` map-entry staging. Signature idiom: `const std::unique_ptr<storage::BufferBatch> &aDataStore` (first or early parameter), null-check returning `outcome::failure`, `BOOST_OUTCOME_TRY` for staged ops, commit owned by the composer (GlobalDB).

### Error handling: `outcome::result<T>` + `enum class Error` + outcome macros
**Source:** `src/crdt/globaldb/globaldb.hpp:81-90` + `:370`; `src/crdt/graphsync_dagsyncer.hpp:42-51` + `:288`
**Apply to:** every new/changed function. Pattern: `enum class Error : uint8_t` inside the class, `OUTCOME_HPP_DECLARE_ERROR_2( ns, Class::Error )` after the class, string mapping in the `.cpp` `result<void>` style — `BOOST_OUTCOME_TRY` / `OUTCOME_TRY`, explicit `outcome::failure(...)` propagation, never swallow. Prefer existing error values (`GlobalDB::Error::INVALID_PARAMETERS`, `GLOBALDB_NOT_STARTED`) over new ones.

### Fresh temp DB test fixture rule (D-C6)
**Source:** `test/src/crdt/crdt_set_test.cpp:60-72`; `test/src/crdt/globaldb_integration.cpp:99-103`
**Apply to:** every erase test, both files. `test::removeAllWithRetry(path)` at start; present-before asserts; absent-after asserts (both stores); seal-marker-still-present; untouched-sibling byte-identical control; unique per-test database path.

### Test-only static seam
**Source:** `src/crdt/graphsync_dagsyncer.hpp:134-145` (doc + "No production caller may invoke this method") + `:197-198` (static member)
**Apply to:** the erase interruption hook (lives on `GlobalDB`, same shape): static setter + `static std::function<void(int)>` member, doc-commented as test-only.

### GlobalDB lifecycle null-check wrapper
**Source:** `src/crdt/globaldb/globaldb.cpp:746-801` (comment at `:752-756`)
**Apply to:** the erase API body: `ActiveDataStore()` / `ActiveCRDTDataStore()` + null-check → `GLOBALDB_NOT_STARTED` (or `std::errc::operation_canceled` to match neighbors) before touching handles.

### Lock discipline per module
**Sources:** `CrdtSet::mutex_` recursive (`crdt_set.hpp:330-337` with the reentry rationale comment; `PutElems` scope `crdt_set.cpp:690-736`); `GraphsyncDAGSyncer::dagMutex_` (`graphsync_dagsyncer.cpp:461-465, :805-809`); `TransactionManager::tx_mutex_m` (`TransactionManager.cpp:4183-4203`)
**Apply to:** staging methods take their own module's lock; the erase API never holds three module locks simultaneously (Pitfall 7); document the staging-vs-commit window in the API contract.

### File layout / headers / logging
**Sources:** Doxygen file header (`globaldb_integration.cpp:1-8`), class Doxygen (`crdt_set.hpp:13-19`), logger member (`globaldb.hpp:363` `sgns::base::createLogger("GlobalDB")`)
**Apply to:** both new test files (file-header comment) and any new declarations (documented public functions per CLAUDE.md). C++17, Allman, `.clang-format` — no hand-formatting.

## No Analog Found

| Item | Role | Data Flow | Reason |
|------|------|-----------|--------|
| Tombstone-ban grep gate (`DeleteKey`/`DeleteTransaction`/`removeNode`/`DeleteCIDBlock` absent from the new surface; no `+ "sealed"` in `src/`) | verification | static source scan | No repo precedent for source-scan ctest or plan-level grep gates (confirmed by RESEARCH Wave 0 gaps). Planner chooses the mechanism — a plan verification step is the lowest-risk option; a source-scan test would itself be first-of-kind. Use RESEARCH.md's suggested grep command shape. |

Everything else in the phase has an in-repo analog; this is composition work, not infrastructure (RESEARCH "Key insight").

## Metadata

**Analog search scope:** `src/crdt/` (headers + `impl/`), `src/crdt/globaldb/`, `src/storage/rocksdb/`, `src/transaction/`, `test/src/crdt/` (sources + `CMakeLists.txt`), `cmake/functions.cmake` (addtest, via RESEARCH verification), vendored `thirdparty/ipfs-lite-cpp/ipfs_lite/ipfs/impl/datastore_rocksdb.cpp` (key-format contract, read-only)
**Files read in full:** `crdt_set.hpp`, `graphsync_dagsyncer.hpp`, `dagsyncer.hpp`, `globaldb.hpp`, `rocksdb_batch.hpp/.cpp`, `test/src/crdt/CMakeLists.txt`
**Files read targeted (non-overlapping ranges):** `crdt_set.cpp` (440-810), `graphsync_dagsyncer.cpp` (450-490, 790-875), `globaldb.cpp` (40-100, 300-390, 740-815), `TransactionManager.hpp` (420-480), `TransactionManager.cpp` (4165-4220, 4305-4360, 4425-4465, 2198-2222), `crdt_datastore.hpp` (425-505), `crdt_heads.hpp` (60-135), `crdt_set_test.cpp` (1-130), `crdt_datastore_test.cpp` (1-85), `globaldb_integration.cpp` (1-160), vendored `datastore_rocksdb.cpp` (1-90)
**Pattern extraction date:** 2026-10-15
