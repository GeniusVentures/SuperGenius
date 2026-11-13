---
phase: 16-erasure-primitives-bookkeeping-hardening
plan: 02
subsystem: database
tags: [crdt, graphsync-dagsyncer, rocksdb, bufferbatch, erasure, pruning, transaction-manager]

# Dependency graph
requires:
  - phase: 16-erasure-primitives-bookkeeping-hardening (plan 01)
    provides: caller-owned-batch erase staging precedent (CrdtSet::StageEraseElement shapes)
provides:
  - GraphsyncDAGSyncer::StageBlockRemoval — stages exactly the CID::toBytes() block-bytes key into a caller-owned BufferBatch under dagMutex_; never constructs the seal key; never calls the removeNode chain
  - TransactionManager::RemoveProcessedTransactionEntry — public revert-free tx_processed_m erase under tx_mutex_m (no RevertTransaction, no nonce rollback, no CRDT tombstone)
  - EraseBlockStagingTest (test/src/crdt/crdt_erase_test.cpp) — the D-C6 real-store fixture: fresh temp RocksDB shared by both facades, never-started GraphsyncDAGSyncer; 16-04 extends this file
affects: [16-03 (GlobalDB atomic compose calls both new primitives), 18 (TxPruner critical section), PRUNE-02, GATE-03]

# Tech tracking
tech-stack:
  added: []  # no new libraries — vendored ipfs_datastore_rocksdb target newly linked into crdt_test only
  patterns:
    - one-DB-two-facades test wiring (sgns::storage::rocksdb owns the DB; vendored ipfs_lite::rocksdb + RocksdbDatastore share db->getDB())
    - guard-then-stage under the owner's mutex; caller owns the single commit (extends 16-01's stage-not-commit pattern to block storage)

key-files:
  created:
    - test/src/crdt/crdt_erase_test.cpp
  modified:
    - src/crdt/graphsync_dagsyncer.hpp
    - src/crdt/impl/graphsync_dagsyncer.cpp
    - src/transaction/TransactionManager.hpp
    - src/transaction/TransactionManager.cpp
    - test/src/crdt/CMakeLists.txt
    - .gitignore

key-decisions:
  - "StageBlockRemoval lives on the concrete GraphsyncDAGSyncer only (not the DAGSyncer base) so CustomDagSyncer and other test doubles stay untouched (RESEARCH Pitfall 8)"
  - "The Doxygen cites datastore_rocksdb.cpp:14-18 as the one home of the vendored block-key format but phrases the seal marker and the dormant removeNode chain WITHOUT their bare tokens — the plan's grep gates ('\"sealed\"' == 0 in src/; removeNode grep unchanged at 3 owner lines) are the enforcement mechanism, so the doc must not break them (gates, not promises)"
  - "RemoveProcessedTransactionEntry returns success on a missing key (pruned key with no tracked entry is a legitimate no-op; Phase 18 sweeps re-run) and never reverts — the spends that made the tx prunable must not be undone"
  - "Mutation-verified RED: temporarily staging an empty key made StagedRemovalDeletesBlockKeyOnly fail, then the revert restored green — proving the fixture exercises the real staging path despite the plan's impl-before-test task order"

patterns-established:
  - "Erase fixture wiring: production one-DB-two-facades shape mirrored in tests — db->batch() (SuperGNUS facade) commits deletions the vendored RocksdbDatastore observes by CID"

requirements-completed: [PRUNE-02, GATE-03]

# Metrics
duration: 18min
completed: 2026-10-15
---

# Phase 16 Plan 02: Block-Storage Erase Staging + Tracked-Map Cleanup Summary

**GraphsyncDAGSyncer::StageBlockRemoval (guarded CID::toBytes() key staging into a caller-owned batch, seal unaddressable, removeNode dormant) plus TransactionManager::RemoveProcessedTransactionEntry (revert-free tx_processed_m erase), proven by the D-C6 real-store fixture EraseBlockStagingTest on a fresh temp RocksDB shared by both facades**

## Performance

- **Duration:** 18 min (started 2026-10-15T19:12:32Z, completed 2026-10-15T19:30:17Z)
- **Tasks:** 3
- **Files:** 7 (5 planned + .gitignore + plan-listed test CMake)

## Accomplishments
- `StageBlockRemoval` on the concrete `GraphsyncDAGSyncer`: null-batch guard, `dagMutex_` lock identical to `removeNode`, `BOOST_OUTCOME_TRY(cid.toBytes())`, `aBatch->remove(Buffer(...))` — stages exactly the block-bytes key; body contains zero `removeNode`/`DeleteCIDBlock`/`sealed` tokens
- `RemoveProcessedTransactionEntry` public beside `GetTransactionCID`: `unique_lock(tx_mutex_m)` + find + `tx_processed_m.erase`, debug-log on hit and on miss, success either way; body contains zero `RevertTransaction`/`RollBackPeerConfirmedNonce`/`DeleteTransaction`/`DeleteKey` tokens
- `EraseBlockStagingTest` proves on a real shared RocksDB: staged+committed removal returns NOT_FOUND for the block key while the seal stays present and `isResolved` stays true; an uncommitted (destroyed) batch leaves block and seal byte-identical including on a reopened DB handle; null batch rejected without touching bytes; sibling CID bytes identical
- Additive-only: zero production callers of either new method; `removeNode` caller count in src/ unchanged (3 owner lines)

## Task Commits

1. **Task 1: StageBlockRemoval (hpp decl + guarded impl)** - `2770444e4` (feat)
2. **Task 2: RemoveProcessedTransactionEntry (hpp decl + plain erase)** - `b93d2cfca` (feat)
3. **Task 3: crdt_erase_test.cpp fixture + 3 cases + CMake link + gitignore** - `b02a55051` (test)

## Files Created/Modified
- `src/crdt/graphsync_dagsyncer.hpp` — public `StageBlockRemoval` declaration beside `removeNode` with the block-key-home / seal-never-constructed / removeNode-dormant / caller-owns-commit Doxygen contract; adds `<storage/buffer_map_types.hpp>` include
- `src/crdt/impl/graphsync_dagsyncer.cpp` — implementation directly after `removeNode`: null-batch guard, `dagMutex_` lock, `cid.toBytes()` staging into the caller's batch
- `src/transaction/TransactionManager.hpp` — public `RemoveProcessedTransactionEntry` beside `GetTransactionCID` with the revert-free contract Doxygen
- `src/transaction/TransactionManager.cpp` — plain erase implementation directly after `RemoveTransactionFromProcessedMaps`
- `test/src/crdt/crdt_erase_test.cpp` — NEW: `EraseBlockStagingTest` fixture + `StagedRemovalDeletesBlockKeyOnly`, `InterruptedBatchLeavesEverythingPresent`, `NullBatchIsRejected`; raw seal key constructed only in the assertion helper with the datastore_rocksdb.cpp:48-56 citation
- `test/src/crdt/CMakeLists.txt` — `crdt_erase_test.cpp` in `addtest(crdt_test ...)`; `ipfs-lite-cpp::ipfs_datastore_rocksdb` in `target_link_libraries` (crdt_graphsync_dagsyncer arrives transitively PUBLIC via crdt_datastore — no explicit addition needed)
- `.gitignore` — ignores `supergenius_crdt_erase_test_block_staging/`

## Decisions Made
- Doxygen wording deliberately avoids the bare tokens `"sealed"` and `removeNode` so the plan's repo-wide grep gates stay mechanically green — the citation (datastore_rocksdb.cpp lines, "the immediate node-removal override directly above") carries the same information without breaking the gate (see key-decisions)
- `RemoveProcessedTransactionEntry` uses the `outcome::result<void>` signature (matching `RemoveTransactionFromProcessedMaps`) over a bool-found variant — both neighbors return `outcome::result<void>`; found/missed is observable via debug log
- Fixture mirrors globaldb.cpp:322-329 exactly: SuperGNUS `rocksdb::create(path)` first, then `ipfs_lite::rocksdb::create(db->getDB())` → `RocksdbDatastore` → never-started `GraphsyncDAGSyncer(ipfsStore, nullptr, nullptr)`; `StopSync` verified null-guarded so the destructor is inert
- Reopen assertion re-creates both facades on the same path after resetting all handles — proves the uncommitted batch left nothing durably written

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Header Doxygen initially broke the plan's own grep gates**
- **Found during:** Task 1 verification
- **Issue:** The first Doxygen draft contained `<cid> + "sealed"` and `removeNode` verbatim; the acceptance criteria require `grep '"sealed"' src/` == 0 and an unchanged `removeNode` grep baseline
- **Fix:** Reworded the contract ("the vendored seal(), datastore_rocksdb.cpp:48-56", "the immediate node-removal override directly above") — same citations, no bare tokens; both gates verified green
- **Files modified:** src/crdt/graphsync_dagsyncer.hpp
- **Commit:** `2770444e4`

**2. [Rule 3 - Blocking] EXPECT_OUTCOME_TRUE macro double-.value() compile errors**
- **Found during:** Task 3 first build
- **Issue:** The macro already binds its variable to `expr.value()`; my `.value().toHex()` chains did not compile (7 errors)
- **Fix:** Dropped the redundant `.value()` on the four macro-declared variables
- **Files modified:** test/src/crdt/crdt_erase_test.cpp
- **Commit:** `b02a55051`

**3. [Process] TDD RED delivered as a mutation check instead of a pre-implementation RED commit**
- **Found during:** Task 3 (tdd="true")
- **Issue:** The plan's task order commits Task 1 (implementation) before Task 3 (tests), so a genuine compile-RED was structurally pre-empted — the tests pass against already-committed code
- **Fix:** Demonstrated the tests CAN fail: temporarily mutated the staging to an empty key → `StagedRemovalDeletesBlockKeyOnly` FAILED (true RED observed) → reverted via file-scoped checkout → 3/3 green again
- **Files modified:** none net (mutation reverted; working tree clean at commit)
- **Verification:** RED run output captured; green re-verified twice after revert

**4. [Hygiene] Ignored the new suite's runtime RocksDB directory**
- **Found during:** Task 3
- **Issue:** Test runs leave `supergenius_crdt_erase_test_block_staging/` untracked (same as 16-01)
- **Fix:** Added the directory to `.gitignore` beside the 16-01 entry
- **Files modified:** .gitignore
- **Commit:** `b02a55051`

---

**Total deviations:** 4 auto-fixed (1 gate-preserving doc reword, 1 compile fix, 1 TDD-ordering process adaptation, 1 hygiene)
**Impact on plan:** None on scope or behavior — all acceptance criteria green.

## Issues Encountered
- `clang-tidy` could not be run: the binary is not on PATH in this environment (16-01 additionally documented the compile_commands.json double-compiler-job abort). Compensated with warning-clean rebuilds (`-Wall -Wextra` effective through the project's warning settings): zero diagnostics on both changed production TUs and the new test TU
- Whole-file `clang-format` remains unusable as an absolute gate on PRE-EXISTING files (local clang-format 19.1.7 vs the repo-canonical formatter version — 16-01 finding); compensated by diffing clang-format output and confirming zero hunks touch added lines in the four production files; the new test file was clang-format-formatted wholesale (format-clean)
- Pre-existing warnings in `src/coinprices/PriceHttpClient.cpp` surfaced during the transaction target build — unrelated dependency TU, out of scope (not touched, not fixed)

## Verification Results
- `crdt_test --gtest_filter='EraseBlockStaging*'`: 3/3 PASSED, run twice consecutively on fresh DBs
- Full `crdt_test`: 30/30 PASSED (27 pre-existing + 3 new; no regression)
- Awk-extracted `StageBlockRemoval` body: `dagMutex_` present, `toBytes` present, `removeNode|DeleteCIDBlock|sealed` count 0
- Awk-extracted `RemoveProcessedTransactionEntry` body: `tx_mutex_m` present, `tx_processed_m.erase` present, forbidden-token count 0
- Repo gates: `grep -rn '"sealed"' src/` == 0 matches; `grep -rn "removeNode" src/` == exactly 3 owner lines (hpp decl, cpp def, cpp delegation)
- `genius_transaction_objs` and `crdt_graphsync_dagsyncer` rebuild with zero diagnostics; no file deletions in any task commit; no stray untracked files (runtime DB dirs now ignored)
- CMake pre-PR checklist walked for the CMakeLists edit: no new targets, no artifact routing, no interpreter discovery, no option wiring — source-list append + one link library on an existing test target only

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 16-03 (GlobalDB atomic compose) can now stage all three erase levels into ONE batch: `CrdtDatastore::StageEraseElement` (CRDT entries, from 16-01), `GraphsyncDAGSyncer::StageBlockRemoval` (raw block bytes, this plan), and the tx→CID map entry — then commit once
- 18's TxPruner calls `TransactionManager::RemoveProcessedTransactionEntry` for tracked-map cleanup and owns the wider stage+commit critical section (Pitfall 7)
- 16-04 extends `test/src/crdt/crdt_erase_test.cpp` (the D-C6 fixture file)

## TDD Gate Compliance
- Plan-level type is `execute` (not `tdd`); Task 3 alone is marked `tdd="true"`, but the plan's task order lands the implementation (Task 1) before the tests (Task 3), making a compile-RED structurally impossible without undoing Task 1's commit
- RED was delivered as a mutation check: staging an empty key made `StagedRemovalDeletesBlockKeyOnly` FAIL; reverting restored 3/3 green — the tests demonstrably exercise the real staging behavior
- Commit order on the branch is feat(Task1) → feat(Task2) → test(Task3); the classic test-before-feat sequence does not appear in this plan's history by construction (documented here rather than fabricated)

## Self-Check: PASSED

All 7 claimed files exist on disk; all 3 task commit hashes (2770444e4, b93d2cfca, b02a55051) present in git log.

---
*Phase: 16-erasure-primitives-bookkeeping-hardening*
*Completed: 2026-10-15*
