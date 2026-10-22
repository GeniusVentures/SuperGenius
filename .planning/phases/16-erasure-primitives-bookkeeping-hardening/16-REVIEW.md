---
phase: 16-erasure-primitives-bookkeeping-hardening
reviewed: 2026-10-15T21:47:59Z
depth: standard
files_reviewed: 16
files_reviewed_list:
  - src/crdt/crdt_datastore.hpp
  - src/crdt/crdt_set.hpp
  - src/crdt/globaldb/globaldb.cpp
  - src/crdt/globaldb/globaldb.hpp
  - src/crdt/graphsync_dagsyncer.hpp
  - src/crdt/impl/crdt_datastore.cpp
  - src/crdt/impl/crdt_set.cpp
  - src/crdt/impl/graphsync_dagsyncer.cpp
  - src/transaction/TransactionManager.cpp
  - src/transaction/TransactionManager.hpp
  - test/src/crdt/CMakeLists.txt
  - test/src/crdt/crdt_custom_dagsyncer.cpp
  - test/src/crdt/crdt_custom_dagsyncer.hpp
  - test/src/crdt/crdt_erase_test.cpp
  - test/src/crdt/crdt_set_test.cpp
  - test/src/crdt/globaldb_erase_test.cpp
findings:
  critical: 0
  warning: 0
  info: 4
  total: 4
fixed:
  critical: 1
  warning: 3
  total: 4
status: issues_found
---

# Phase 16: Code Review Report

**Reviewed:** 2026-10-15T21:47:59Z
**Depth:** standard (with cross-file tracing into `TransactionManager`, `CrdtSet`, `CrdtHeads`, `GraphsyncDAGSyncer`, the sgns `storage::rocksdb` batch/query substrate, and the vendored `RocksdbDatastore`/`MerkleDagServiceImpl`)
**Files Reviewed:** 16
**Status:** issues_found

## Summary

The phase's core claims were verified against source and hold up well: `StageBlockRemoval` stages exactly `CID::toBytes()` (the vendored `encode()`, `thirdparty/ipfs-lite-cpp/ipfs_lite/ipfs/impl/datastore_rocksdb.cpp:14-18`) and never the distinct `<cid>sealed` key (`seal()` at `:48-56`, `isResolved` -> `is_sealed` via `MerkleDagServiceImpl::isResolved`); the GlobalDB batch and the IPFS block store share one underlying RocksDB (`IpfsRocksDb::create(m_datastore->getDB())`, `globaldb.cpp:322-329`), so the one-WriteBatch atomicity claim is sound; `rocksdb::Batch::remove` is a `WriteBatch::Delete` (no-op on absent keys), so the unconditional value/priority staging is safe; no CRDT hooks fire on the erase path; and the gate/test suites are genuinely wired (fresh-DB fixtures, present-before/absent-after, negative controls, seal retention, interruption injection, source-scan gate with `PROJECT_ROOT` correctly resolved).

However, the review found one correctness defect in the new `TransactionManager::EraseTransactionEntry` driver (cross-network key mismatch producing exactly the asymmetric partial erase the phase forbids), a latent over-erase in `CrdtSet::StageEraseElement`'s prefix scan, a documented-but-shipped guard race window in `GlobalDB::EraseEntry`, and a post-commit failure that misreports a committed erase. The `EraseTransactionEntry` defect matters even though the method ships unwired: it is the phase's D-C4 deliverable and Phase 18's TxPruner inherits it as-is.

## Critical Issues

### CR-01: EraseTransactionEntry resolves the CID across all monitored networks but erases the local-network path only

**Status: FIXED in iteration 1** (commit 55e80f64c; see 16-REVIEW-FIX.md).

**File:** `src/transaction/TransactionManager.cpp:4239-4251`
**Issue:** `GetTransactionCID(tx_hash)` (`TransactionManager.cpp:4480-4505`) iterates **every** monitored network (`GetMonitoredNetworkIDs()`: local network plus TEST_NET and MAIN_NET on any DEV-net node) and returns the CID stored under the **first** network path that has it — without reporting which network/path matched. `EraseTransactionEntry` then erases `GetTransactionPath(tx_hash)` (`:4244`), which is built from `GetBlockChainBase()` == the **local** network only (`TransactionManager.cpp:2212-2215`). For any transaction whose tx->CID entry lives under a non-local monitored network's path (real state on dev-net nodes — production code reads cross-network tx paths in 10 places, e.g. `:3231`, `:4492`):

- `StageEraseElement` and the raw-map `batch->remove(Buffer{}.put(key))` stage removes against a **nonexistent** wrong-network key (silent no-ops);
- `StageBlockRemoval` really erases the block bytes of the resolved CID;
- commit lands a **partial asymmetric erase**: the surviving other-network CRDT entries and raw map entry now reference physically erased block bytes — the exact anti-pattern the phase's own D-C6/PRUNE-02 documentation forbids ("erasing only some families skews add-wins resolution on re-delivery"), and the tx becomes locally unfetchable (its seal survives, so nothing certifies re-fetch);
- `RemoveProcessedTransactionEntry(path)` (`:4251`) also keys on the wrong-network path, so the tracked-map entry under the found network's path survives too.

**Fix:** Resolve the path and the CID from the same network. Either iterate in `EraseTransactionEntry`:

```cpp
for ( auto network_id : GetMonitoredNetworkIDs() )
{
    const auto path = GetTransactionPath( network_id, tx_hash );
    auto value_res = globaldb_m->GetDataStore()->get( Buffer{}.put( path ) );
    if ( value_res.has_value() )
    {
        BOOST_OUTCOME_TRY( auto cid, CID::fromString( value_res.value().toString() ) );
        BOOST_OUTCOME_TRY( globaldb_m->EraseEntry( path, cid ) );
        // ... tracked-map cleanup with the SAME path
        return outcome::success();
    }
}
return outcome::failure( std::errc::no_such_file_or_directory );
```

or add an overload of `GetTransactionCID` that returns the resolved key alongside the CID, and use that key for `EraseEntry` and `RemoveProcessedTransactionEntry` alike.

## Warnings

### WR-01: StageEraseElement's prefix scan over-erases hierarchical descendants and is unvalidated against the empty key

**Status: FIXED in iteration 1** (commits 176de6520, 3335739fa; see 16-REVIEW-FIX.md).

**File:** `src/crdt/impl/crdt_set.cpp:688-700` (contract at `src/crdt/crdt_set.hpp:256-281`)
**Issue:** The scan prefixes are `ElemsPrefix(aKey).GetKey() + "/"` and `TombsPrefix(aKey).GetKey() + "/"`. `HierarchicalKey::ChildString` does **not** escape `/` (`src/crdt/impl/hierarchical_key.cpp`), and this codebase's element keys are genuinely hierarchical (`/bc-0/tx/<hash>` from `TRANSACTION_BASE_FORMAT` + `GetTransactionFullPath`). The trailing slash prevents sibling-suffix matches ("key" vs "key2", as the comment claims) but **not** parent/child matches: erasing key `a` scans `/namespace/s/a/`, which matches every entry of descendant keys `a/b/...` (`/namespace/s/a/b/<id>`), staging their elems/tombs for removal while their `/k/v` and `/k/p` records survive — an asymmetric partial erase of the descendants. With `aKey == ""` the scan prefix degenerates to `/namespace/s/` and `/namespace/t/`, matching **every** element/tombstone entry in the namespace. The sibling read paths (`InElemsNotTombstoned`, `CreateDeltaToRemove`) guard exactly this hazard with `HierarchicalKey(id).IsTopLevel()` (`crdt_set.cpp:153-159, 434-443`); the new scan has no such filter and no input validation, despite being a public primitive documented as generic ("Generic node-local physical erase", `globaldb.hpp:200-231`). No current caller hits it (TransactionManager erases leaf hash paths), but nothing prevents the next caller from erasing a subtree root (e.g. a `reg/<address>` registry key).

**Fix:** Validate at entry and filter like the sibling methods:

```cpp
if ( aKey.empty() )
{
    return outcome::failure( std::errc::invalid_argument );
}
// ... inside the scan loop:
std::string id( entry.first.toString().substr( scanPrefix.size() ) );
if ( !HierarchicalKey( id ).IsTopLevel() )
{
    continue; // entry belongs to a descendant key, not aKey
}
```

(Alternatively, assert leaf-only in debug builds and document the restriction in the header contract.)

### WR-02: EraseEntry's guards are check-then-act with no window protection; header contract states them as absolute

**Status: FIXED in iteration 1** (commit d043b86f4; see 16-REVIEW-FIX.md).

**File:** `src/crdt/globaldb/globaldb.cpp:805-862` (contract at `src/crdt/globaldb/globaldb.hpp:204-231`)
**Issue:** The four guards (all-topic head scan, `IsRootCIDPendingOrActive`, `IsCIDInCache`, `isResolved`) run before staging, but nothing prevents a concurrent `AddHead`/`EnqueueRootCID`/`AddCIDBlock` (LRU insert from `BlockReceivedCallback`) between the checks and `batch->commit()`. A CID that becomes a topic head or enters the LRU block cache after its guards passed is erased anyway — the cache then serves a node whose bytes are gone. Likewise a concurrent `PutElems` on the same key can commit between `StageEraseElement`'s under-lock query and the caller's outside-the-lock commit, and the erase batch then deletes the re-created value while the post-query elem entries survive. RESEARCH Pitfall 7 documents this window as tolerable **for Phase 16** (no production caller; single-threaded tests), but `EraseEntry` is a shipped public API whose header presently reads as an absolute invariant ("a CID that is ... LRU-cached, or unresolved can **never** be erased through this path") — which is only true single-threaded.

**Fix:** At minimum, amend the `EraseEntry` Doxygen to state the guards are best-effort under concurrency and that Phase 18's TxPruner owns the critical section (the CrdtSet header already carries this caveat for its own half — `crdt_set.hpp:269-275` — the facade should match). When wired, close the window in the pruner (e.g. quiesce the sync worker or re-check guards post-commit and repair).

### WR-03: Post-commit seal re-check failure returns a guard-equivalent error although the erase already committed

**Status: FIXED in iteration 1** (commit 46cd906ad; see 16-REVIEW-FIX.md).

**File:** `src/crdt/globaldb/globaldb.cpp:862-871`
**Issue:** If the post-commit `isResolved` re-check fails, `EraseEntry` returns `Error::INVALID_PARAMETERS` — the same error value as every pre-staging guard rejection — even though `batch->commit()` at `:862` has already durably removed the CRDT entries, block bytes, and map entry. A caller cannot distinguish "nothing was erased" from "erased, then an out-of-band anomaly was detected": `EraseTransactionEntry`'s `BOOST_OUTCOME_TRY` (`TransactionManager.cpp:4247`) then skips the tracked-map cleanup and reports failure while the data is gone. (A sweep re-run happens to heal the bookkeeping because re-erasing an already-erased entry passes the guards and commits no-ops — but that is luck, not contract.)

**Fix:** Return a distinguishable error (add an `Error` enum value, or at minimum log at critical level and document the semantics), and make `EraseTransactionEntry` still run `RemoveProcessedTransactionEntry` when the failure is known to be post-commit — or simply document that this failure implies a committed erase.

## Info

### IN-01: RemoveProcessedTransactionEntry can never fail; EraseTransactionEntry's cleanup error branch is dead code

**File:** `src/transaction/TransactionManager.cpp:4212-4227, 4251-4254`
**Issue:** The function only ever returns `outcome::success()`, so the `cleanup.has_error()` warn branch is unreachable. Either drop the `outcome::result` return (make it `void`, matching its always-succeeds contract) or keep the result but delete the dead check — as written it implies a failure mode that cannot occur.

### IN-02: New staging methods return empty/default error codes on failure

**File:** `src/crdt/impl/crdt_set.cpp:672-680`, `src/crdt/impl/graphsync_dagsyncer.cpp:467-473`, `src/crdt/impl/crdt_datastore.cpp:1381-1389`
**Issue:** Null-batch/null-store guards return `outcome::failure( boost::system::error_code{} )` — a default-constructed error carrying no category, message, or code, indistinguishable across all three sites. This matches the surrounding files' existing convention, but per the project rule "errors are part of the contract" a typed error (e.g. `storage::DatabaseError::UNITIALIZED` or `std::errc::invalid_argument`) would be diagnosable in logs. Low priority; fix opportunistically when touching these files next.

### IN-03: globaldb_erase_test silently swallows GlobalDB::New failure

**File:** `test/src/crdt/globaldb_erase_test.cpp:126-129`
**Issue:** `addNode` returns without adding the node when `GlobalDB::New` fails; the caller's `ASSERT_EQ(testNodes->getNodes().size(), 1)` then fails with no hint at the underlying init error (only `ASSERT_FALSE(startError)` for pubsub start is reported). Add `ASSERT_FALSE(globaldb_ret.has_error()) << globaldb_ret.error().message()` before use so CI diagnostics point at the real failure. (Test reliability only.)

### IN-04: Byte-equality assertions use case-insensitive/string compares in the new set tests

**File:** `test/src/crdt/crdt_set_test.cpp` (new `EraseElementStagingTest` cases: `EXPECT_STRCASEEQ(...)` x2, `EXPECT_STREQ(...)`)
**Issue:** `EXPECT_STRCASEEQ` is weaker than the intended byte-identical check (a case-differing value would pass). Harmless for identical strings, but it weakens the D-C6 "byte-identical" claim the comments make. Prefer `EXPECT_EQ(std::string(value.toString()), expected)`.

## Fix Summary (iteration 1, 2026-10-15)

All four in-scope findings (CR-01, WR-01, WR-02, WR-03) are fixed; the four
Info findings (IN-01..IN-04) remain open, out of fix scope. Details, commits,
and test results in `16-REVIEW-FIX.md`.

---

_Reviewed: 2026-10-15T21:47:59Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
