---
phase: 16-erasure-primitives-bookkeeping-hardening
plan: 03
subsystem: database
tags: [crdt, globaldb, graphsync-dagsyncer, rocksdb, bufferbatch, erasure, transaction-manager, pruning, atomic-write-batch]

# Dependency graph
requires:
  - phase: 16-erasure-primitives-bookkeeping-hardening (plan 01)
    provides: CrdtDatastore::StageEraseElement forwarder + public IsRootCIDPendingOrActive guard query
  - phase: 16-erasure-primitives-bookkeeping-hardening (plan 02)
    provides: GraphsyncDAGSyncer::StageBlockRemoval block-key staging; TransactionManager::RemoveProcessedTransactionEntry revert-free tracked-map erase
provides:
  - GlobalDB::EraseEntry — the ONE narrow generic erase API: four fail-closed guards (all-topic head scan, pending/active root job, LRU cache, isResolved) before any staging, three owner stagings into ONE BufferBatch, single commit, post-commit seal re-assert
  - GlobalDB::m_dagSyncer — concrete GraphsyncDAGSyncer member assigned in Init (base-interface doubles untouched)
  - GlobalDB::SetEraseInterruptHookForTest + static hook — D-03 test-only interruption seam (abort without committing)
  - TransactionManager::EraseTransactionEntry — the tx→CID map deletion lifecycle driver (D-C4): resolve CID, atomic GlobalDB erase, revert-free tracked-map cleanup
affects: [16-04 (behavioral proof on the D-C6 fixture), 18 (TxPruner calls EraseTransactionEntry), PRUNE-02, GATE-03]

# Tech tracking
tech-stack:
  added: []  # no new libraries — pure composition of the 16-01/16-02 staging primitives behind the GlobalDB facade
  patterns:
    - guard-ensemble-then-single-commit erase API (guards inside the API so no caller can bypass them; key-layout knowledge stays in owner modules, the facade only composes)
    - static test-only interruption hook that aborts before commit so the dying unique_ptr batch proves all-or-nothing (extends GraphsyncDAGSyncer::SetBlacklistBackoffTimeoutForTest precedent)

key-files:
  created: []
  modified:
    - src/crdt/globaldb/globaldb.hpp
    - src/crdt/globaldb/globaldb.cpp
    - src/crdt/crdt_datastore.hpp
    - src/transaction/TransactionManager.hpp
    - src/transaction/TransactionManager.cpp

key-decisions:
  - "All four guards run inside EraseEntry BEFORE any staging and reject INVALID_PARAMETERS fail-closed — a caller cannot bypass them because the API takes only key+cid, no trust parameters (D-C5, T-16-08/09)"
  - "The head guard scans GetCRDTHeadList's heads of EVERY topic, never a single-topic IsHead — a CID that is another topic's head is still in use (Pitfall 3a)"
  - "The interruption hook aborts by early return WITHOUT commit — the uncommitted unique_ptr batch dies with the function, preserving all-or-nothing (T-16-10); production never invokes the hook"
  - "BOOST_OUTCOME_TRY used over the research sketch's plain OUTCOME_TRY — the file's nine pre-existing macros are all the BOOST form; no competing local style"
  - "RemoveProcessedTransactionEntry runs after the committed erase as logged best-effort bookkeeping — propagating its failure would misreport a successful atomic erase as failed (the CRDT/block/map bytes are already gone)"

patterns-established:
  - "Facade-owned atomic compose: owner modules stage into a caller-owned batch (16-01/16-02 pattern); GlobalDB::EraseEntry owns the single commit and the post-commit seal re-assert (T-16-12)"

requirements-completed: [PRUNE-02, GATE-03]

# Metrics
duration: 40min
completed: 2026-10-15
---

# Phase 16 Plan 03: GlobalDB Atomic Erase Compose + Map Lifecycle Summary

**GlobalDB::EraseEntry — the one guarded one-batch atomic two-level erase behind the facade (four fail-closed guards, three owner stagings, single sync commit, post-commit seal re-assert, test-only interrupt seam) — plus TransactionManager::EraseTransactionEntry completing the tx→CID map's deletion lifecycle (resolve CID, atomic erase, revert-free tracked-map cleanup) with zero production callers**

## Performance

- **Duration:** 40 min (started 2026-10-15T19:35:32Z, completed 2026-10-15T20:15:56Z; ~20 min of it the full-tree rebuild the globaldb.hpp edit cascaded into)
- **Tasks:** 3
- **Files modified:** 5 (4 planned + src/crdt/crdt_datastore.hpp for the Rule 3 access fix)

## Accomplishments
- `GlobalDB::EraseEntry(key, cid)` on the GetRaw family: null-checks `ActiveCRDTDataStore()`/`ActiveDataStore()`/`m_dagSyncer` → `GLOBALDB_NOT_STARTED`, then the four guards each rejecting `INVALID_PARAMETERS` before any staging — all-topic `GetCRDTHeadList` scan, `IsRootCIDPendingOrActive`, `IsCIDInCache`, `isResolved`-must-be-true — then `store->batch()`, the three stagings (`StageEraseElement`, `StageBlockRemoval`, raw map-key `remove`), the test-hook abort points after each staging, ONE `commit()`, and the post-commit `isResolved` re-read that logs "seal marker vanished after erase" and fails loudly
- `GlobalDB` gained the concrete `m_dagSyncer` member (forward-declared in the header, assigned in `Init` at the dagSyncer creation site) — the base `DAGSyncer` interface and test doubles untouched (Pitfall 8)
- `SetEraseInterruptHookForTest` + static `erase_interrupt_hook_for_test_` member, doc-commented in the `SetBlacklistBackoffTimeoutForTest` style ("No production caller may invoke this method"); a `true` return at step 1/2/3 aborts without committing
- `TransactionManager::EraseTransactionEntry(tx_hash)`: `globaldb_m` null-check (`std::errc::bad_file_descriptor`, StoreTransactionCID's shape) → `GetTransactionCID` → `CID::fromString` → static `GetTransactionPath` key → `globaldb_m->EraseEntry` → logged best-effort `RemoveProcessedTransactionEntry` → success
- Zero production callers anywhere in the new surface (SC-5); full default build green; `crdt_test` 30/30 and `globaldb_integration_test` green

## Task Commits

1. **Task 1: GlobalDB::EraseEntry + interrupt seam + m_dagSyncer member (incl. Rule 3 access fix)** - `0539b679a` (feat)
2. **Task 2: TransactionManager::EraseTransactionEntry lifecycle driver** - `fe0981854` (feat)
3. **Task 3: Zero-production-caller gate + regression build** - verification-only, no diff (all four inventories, full build, both test suites green), matching the 16-01 Task 3 precedent

## Files Created/Modified
- `src/crdt/globaldb/globaldb.hpp` — `<functional>` include, `GraphsyncDAGSyncer` forward declaration, public `EraseEntry` beside the GetRaw family with the D-01/D-C2/D-C5 contract Doxygen, public static `SetEraseInterruptHookForTest`, private `m_dagSyncer` member + static hook member
- `src/crdt/globaldb/globaldb.cpp` — `m_dagSyncer = dagSyncer;` at the Init creation site; static hook definition, setter, and `EraseEntry` directly after `QueryRaw` (GetRaw lifecycle-wrapper neighborhood)
- `src/crdt/crdt_datastore.hpp` — [Rule 3] `IsRootCIDPendingOrActive` + `StageEraseElement` moved from the protected tail to the public section (declaration + Doxygen moved verbatim; no behavior change)
- `src/transaction/TransactionManager.hpp` — public `EraseTransactionEntry` after `RemoveProcessedTransactionEntry` with the D-C4/D-C2 lifecycle Doxygen
- `src/transaction/TransactionManager.cpp` — implementation directly after `RemoveProcessedTransactionEntry`

## Decisions Made
- The raw bookkeeping entry is staged as `batch->remove(Buffer{}.put(key))` with the caller-built key — the identical key shape `StoreTransactionCID` writes; GlobalDB never concatenates domain keys (D-01)
- Guard-rejection logs include the CID via the established `cid.toString().value()` precedent (graphsync_dagsyncer.cpp); the post-commit failure log carries the plan's exact "seal marker vanished after erase" wording
- `OUTCOME_TRY` from the research sketch replaced with `BOOST_OUTCOME_TRY` repo-wide form (the file's existing nine macros are all BOOST)
- Tracked-map cleanup after the committed erase is checked and warn-logged rather than propagated or silently discarded — an `outcome::result<void>` must not be dropped, and a post-commit bookkeeping miss must not misreport the erase as failed

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] 16-01's forwarders had landed `protected`, blocking the compose**
- **Found during:** Task 1 first build
- **Issue:** `CrdtDatastore::IsRootCIDPendingOrActive` and `StageEraseElement` were declared in the `protected:` tail (beside `WaitForJob`), but the plan's interface contract requires them public and `GlobalDB` is not a friend — `'StageEraseElement' is a protected member` compile errors
- **Fix:** Moved both declarations with their Doxygen verbatim from the protected tail to the end of the public section (access widening only; zero behavior change, no friend additions)
- **Files modified:** src/crdt/crdt_datastore.hpp
- **Commit:** `0539b679a`

**2. [Process] Task 3 is verification-only — no third commit**
- **Found during:** Task 3
- **Issue:** All Task 3 gates (four grep inventories, full build, crdt_test, globaldb_integration_test, `"sealed"` gate) passed on the Task 1+2 diffs with zero additional code needed
- **Fix:** Recorded results here; no empty commit created (16-01 Task 3 precedent)

---

**Total deviations:** 2 auto-fixed (1 Rule 3 blocking access fix, 1 process)
**Impact on plan:** None on scope or behavior — the access fix is exactly what the plan's `<interfaces>` block specified as the prerequisite contract.

## Issues Encountered
- `clang-tidy` not runnable: binary not on PATH in this environment (16-01/16-02 additionally documented the compile_commands.json double-compiler-job abort). Compensated with full warning capture on the rebuilt `crdt_globaldb` and `genius_transaction_objs` targets: zero diagnostics on both changed production TUs
- Whole-file `clang-format` remains unusable as an absolute gate on pre-existing files (local 19.1.7 vs repo-canonical formatter; the unmodified header shows drift at lines 108-109). Compensated by diffing clang-format output restricted to added line ranges: clean on all five files
- Full default build takes ~18 min because the globaldb.hpp edit cascades into most downstream targets — expected, one-time within this plan
- Pre-existing warnings surfaced in unrelated test TUs (`HttpStubServer.cpp`, `registration_transaction_test.cpp`, `node_startup_test.cpp`, `price_facade_test.cpp` lambda-capture / missing-initializer) — out of scope, not touched

## Verification Results
- Plan's Task 1 awk gate PASSED: extracted `EraseEntry` body has exactly 1 `commit()`, exactly 2 `isResolved`, all four guard calls before the first `StageEraseElement`, `StageBlockRemoval` present, and 0 occurrences of `DeleteKey|DeleteTransaction|removeNode|DeleteCIDBlock|deleteHook`
- Plan's Task 2 awk gate PASSED: extracted `EraseTransactionEntry` body contains `GetTransactionCID`, `EraseEntry`, `RemoveProcessedTransactionEntry`, and 0 occurrences of `DeleteTransaction|DeleteKey|RevertTransaction|removeNode`
- Task 3 inventories exact: `EraseEntry` → hpp decl + cpp def + the single call inside `EraseTransactionEntry`; `EraseTransactionEntry` → decl + def only; `RemoveProcessedTransactionEntry` → decl + def + the one intra-module composition call; `StageEraseElement` → owner decl/def pairs + forwarder + the EraseEntry call; `StageBlockRemoval` → owner decl/def + the EraseEntry call
- `grep -rn '"sealed"' src/` → 0 matches (repo gate held)
- `cmake --build build/OSX/Release` full default build → exit 0, zero warnings in changed files
- `ctest -R "crdt_test|globaldb_integration_test"` → 2/2 PASSED; direct `crdt_test` run → 30/30 PASSED — the `globaldb_integration_test` pass covers the m_dagSyncer retention teardown concern (no shutdown-ordering fix needed; `ShutdownNow`'s existing broadcaster/crdt-datastore release sequence keeps the DAGSyncer alive past both Stop calls exactly as before)
- No tracked-file deletions in either task commit; untracked runtime RocksDB dirs in the repo root predate this plan (left alone)

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 16-04 (behavioral proof) can drive `GlobalDB::SetEraseInterruptHookForTest` to abort at steps 1/2/3 and assert all-or-nothing on the D-C6 real-store fixture, and exercise the four guard rejections plus the post-commit seal re-assert end-to-end through `EraseEntry` / `EraseTransactionEntry`
- Phase 18's TxPruner is the only future production caller of `EraseTransactionEntry`; eligibility/ordering policy and the wider stage+commit critical section (Pitfall 7) remain Phase 17/18 scope

## Threat Flags

None — no security-relevant surface beyond the plan's threat model. Register dispositions all implemented: T-16-08/09 (guards unconditional, inside the API, before staging), T-16-10 (single batch + commit; hook proves all-or-nothing in 16-04), T-16-11 (function-scoped token gate green on both bodies), T-16-12 (post-commit isResolved re-read fails loudly).

## Self-Check: PASSED

All 5 claimed files exist on disk and are modified in this plan's commits; both task commit hashes (0539b679a, fe0981854) present in git log.

---
*Phase: 16-erasure-primitives-bookkeeping-hardening*
*Completed: 2026-10-15*
