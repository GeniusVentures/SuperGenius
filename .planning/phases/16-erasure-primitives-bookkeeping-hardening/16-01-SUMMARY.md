---
phase: 16-erasure-primitives-bookkeeping-hardening
plan: 01
subsystem: database
tags: [crdt, rocksdb, bufferbatch, erasure, pruning, pruned-node]

# Dependency graph
requires:
  - phase: 16-erasure-primitives-bookkeeping-hardening (planning)
    provides: 16-PATTERNS.md caller-owned-batch idiom, key-layout map, pitfalls register
provides:
  - CrdtSet::StageEraseElement — stages all four CRDT key families (/s elems, /t tombs, /k/<key>/v, /k/<key>/p) into a caller-owned BufferBatch; never commits, never fires deleteHookFunc_
  - CrdtDatastore::StageEraseElement — public forwarder to the owning CrdtSet
  - CrdtDatastore::IsRootCIDPendingOrActive — now public; pure pending/active root-job query for erase guards
  - EraseElementStagingTest — fresh-DB proof of four-family erase, no-hook, sibling byte-identity, null-batch and uncommitted-batch safety
affects: [16-03 (GlobalDB atomic compose), 18 (TxPruner critical section), PRUNE-02]

# Tech tracking
tech-stack:
  added: []  # no new libraries — composition of existing BufferBatch/query primitives
  patterns:
    - caller-owned-batch erase staging (scan prefixes with trailing '/' for sibling-prefix safety; stage removes for all four families unconditionally)

key-files:
  created: []
  modified:
    - src/crdt/crdt_set.hpp
    - src/crdt/impl/crdt_set.cpp
    - src/crdt/crdt_datastore.hpp
    - src/crdt/impl/crdt_datastore.cpp
    - test/src/crdt/crdt_set_test.cpp

key-decisions:
  - "Prefix scans for elems/tombs carry a trailing '/' so a sibling key whose name merely starts with the target (key vs key2) is never matched — the repo's bare-prefix scan would catch it and require the IsTopLevel filter dance"
  - "ValueKey/PriorityKey removes staged unconditionally (absent-key remove is a harmless no-op) — asymmetric partial erase is the listed anti-pattern that skews add-wins resolution"
  - "deleteHookFunc_ must never fire from the erase path: it drives TransactionManager::ProcessDeletion -> RevertTransaction, reverting genuine spends (RESEARCH Pitfall 1)"

patterns-established:
  - "Stage-not-commit: CrdtSet stages under mutex_, caller (GlobalDB compose in 16-03) owns the single commit outside the lock; the wider stage+commit critical section is deferred to Phase 18's TxPruner (Pitfall 7)"

requirements-completed: [PRUNE-02]

# Metrics
duration: 13min
completed: 2026-10-15
---

# Phase 16 Plan 01: CRDT Entry Erase Staging Summary

**CrdtSet batch-staging erase primitive that removes all four CRDT key families inside a caller-owned RocksDB batch (no hooks, no self-commit), plus the two public CrdtDatastore exposures (IsRootCIDPendingOrActive guard, StageEraseElement forwarder) the 16-03 GlobalDB compose needs**

## Performance

- **Duration:** 12 min (started 2026-10-15T18:56:37Z, completed 2026-10-15T19:08:38Z)
- **Tasks:** 3
- **Files modified:** 6 (5 planned + .gitignore)

## Accomplishments
- `CrdtSet::StageEraseElement` stages removes for every stored element entry under `ElemsPrefix`, every tombstone entry under `TombsPrefix`, and unconditionally for `ValueKey`/`PriorityKey` — all into the caller's `BufferBatch`, under `mutex_`, with zero `commit(` and zero `deleteHookFunc_` references in the body
- `CrdtDatastore::IsRootCIDPendingOrActive` moved from private to public (beside `WaitForJob`); `IsRootCIDPendingOrActiveLocked` stays private; forwarder `CrdtDatastore::StageEraseElement` null-checks `set_` then delegates
- `EraseElementStagingTest` proves on a fresh temp RocksDB: four-family erase, delete-hook flag stays false, sibling value byte-identical, null batch rejected without staging, uncommitted batch is a no-op
- Additive-only diff: no production callers of either new method; full `crdt_test` 27/27 green, filter green on two consecutive fresh runs (D-C6)

## Task Commits

Each task was committed atomically (Task 1 is TDD — RED then GREEN):

1. **Task 1: CrdtSet::StageEraseElement (RED)** - `21138327d` (test)
2. **Task 1: CrdtSet::StageEraseElement (GREEN)** - `228b16493` (feat)
3. **Task 2: CrdtDatastore guard + forwarder** - `6790a2f68` (feat)
4. **Task 3: Fresh-DB unit tests** - artifact landed in the RED commit `21138327d`; verification-only thereafter (no additional diff, no separate commit)

**Plan metadata:** see final docs commit

## Files Created/Modified
- `src/crdt/crdt_set.hpp` — public `StageEraseElement` declaration beside the batch-mode `SetValue`, with the no-hook / caller-commits / all-four-families Doxygen contract
- `src/crdt/impl/crdt_set.cpp` — staging implementation: null-batch guard, `mutex_` lock, trailing-slash prefix scans, staged removes for all four families
- `src/crdt/crdt_datastore.hpp` — `IsRootCIDPendingOrActive` made public beside `WaitForJob`; `StageEraseElement` forwarder declared
- `src/crdt/impl/crdt_datastore.cpp` — one-line-style forwarder with `set_` null guard, placed beside the existing `set_` delegations
- `test/src/crdt/crdt_set_test.cpp` — `EraseElementStagingTest` fixture + 3 cases
- `.gitignore` — ignores the new suite's runtime RocksDB dir (`supergenius_crdt_set_test_erase_element_staging/`)

## Decisions Made
- Trailing-slash prefix scans (`ElemsPrefix(aKey).GetKey() + "/"`) instead of the repo's bare-prefix + `IsTopLevel` filter: matches only true children of the exact key, so a sibling named `key2` can never be swept when erasing `key` (the plan's must-have "untouched sibling is byte-identical" is enforced structurally, not just by the control test)
- Added the `dataStore_ == nullptr` guard beside the plan-specified null-batch guard, matching every other public `CrdtSet` method (a `CrdtSet(nullptr, ns)` instance would otherwise segfault in the query)
- Tombstone seeded in the test via the public `PutTombs` API, hook registered only after seeding (PutTombs legitimately fires the hook) — the erase itself is what the flag proves silent on
- Test asserts `GetPriority == 0` (not failure) for the absent priority key — `GetPriority` returns success with 0 on NOT_FOUND by design; raw `contains` checks carry the strict absence proof for `/k/<key>/v` and `/k/<key>/p`

## Deviations from Plan

### Auto-fixed Issues

**1. [Process] TDD ordering moved Task 3's artifact into Task 1's RED commit**
- **Found during:** Task 1 (`tdd="true"`)
- **Issue:** Plan ordered implementation (Task 1) before tests (Task 3), but the TDD flow mandates failing tests first; the tests ARE Task 3's deliverable
- **Fix:** Wrote the full `EraseElementStagingTest` suite as RED (build failed on the missing member — verified), then implemented GREEN. Task 3 reduced to its verification gate (double-run, full suite, grep checks) — all green, no additional diff
- **Files modified:** test/src/crdt/crdt_set_test.cpp (in `21138327d`)
- **Verification:** RED compile failure observed; 3/3 green after GREEN; 27/27 full suite
- **Committed in:** `21138327d`

**2. [Hygiene] Ignored the test suite's runtime RocksDB directory**
- **Found during:** Task 1 GREEN
- **Issue:** Test runs leave `supergenius_crdt_set_test_erase_element_staging/` untracked in the repo root (fresh-DB suites clean leftovers at start, never after)
- **Fix:** Added the directory pattern to `.gitignore`
- **Files modified:** .gitignore
- **Committed in:** `228b16493`

---

**Total deviations:** 2 auto-fixed (1 TDD process ordering, 1 generated-artifact hygiene)
**Impact on plan:** None on scope or behavior — both are commit/verification mechanics.

## Issues Encountered
- Whole-file `clang-format --dry-run` cannot be used as a gate in this environment: the local clang-format 19.1.7 reports 4404 violation lines on the UNMODIFIED HEAD copy of `crdt_set.cpp` (repo-canonical formatter is a different version). Compensated by diffing clang-format output restricted to the added line ranges: clean on all four changed source files.
- `clang-tidy` could not be run at all: `build/OSX/Release/compile_commands.json` carries two compiler jobs per TU (x86_64 + arm64 universal entries), and clang-tidy aborts with "expected exactly one compiler job"; the build system itself clears `CXX_CLANG_TIDY` on targets (`cmake/functions.cmake:3-4`), so nothing build-enforced was skipped. Compensated with strict `-Wall -Wextra` rebuilds of `crdt_set`, `crdt_datastore`, `crdt_globaldb`, and `crdt_test`: zero diagnostics.

## Verification Results
- `crdt_test --gtest_filter='EraseElementStaging*'`: 3/3 PASSED, run twice on fresh DBs
- Full `crdt_test`: 27/27 PASSED (no regression to existing CrdtSet/CrdtDatastore/AtomicTransaction tests)
- Plan's awk gate: the WaitForJob-to-private range contains exactly one `IsRootCIDPendingOrActive` declaration; private block retains `IsRootCIDPendingOrActiveLocked`
- `grep -n "deleteHookFunc_" src/crdt/impl/crdt_set.cpp`: only the pre-existing ctor/copy/PutTombs/SetDeleteHook sites (lines 61, 91, 843, 847, 887) — none inside `StageEraseElement`
- StageEraseElement body greps: `mutex_`, `ElemsPrefix`, `TombsPrefix`, `ValueKey`, `PriorityKey` present; `commit(` and `deleteHookFunc_` count 0
- No external `src/` callers of either new symbol (additive-only diff confirmed by grep)

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 16-03 (GlobalDB atomic compose) can call `CrdtDatastore::StageEraseElement(batch, key)` and `IsRootCIDPendingOrActive(cid)` through the public surface, staging CRDT-entry removes into the same single WriteBatch as the raw block and tx→CID map entry
- The compose-window contract (staging under `mutex_`, commit outside it) is documented on the declaration — Phase 18's TxPruner owns defining the wider critical section (Pitfall 7)
- 16-02 (DAGSyncer block staging) is independent and unblocked

## TDD Gate Compliance
- RED gate: `test(16-01)` commit `21138327d` exists and was observed failing to compile on the missing `StageEraseElement` member
- GREEN gate: `feat(16-01)` commit `228b16493` follows it, with 3/3 tests passing
- No REFACTOR needed (implementation is minimal; the one style iteration — named scan-prefix locals — landed inside the GREEN commit before any commit was made)

## Self-Check: PASSED

All 6 claimed files exist on disk; all 3 task commit hashes (21138327d, 228b16493, 6790a2f68) present in git log.

---
*Phase: 16-erasure-primitives-bookkeeping-hardening*
*Completed: 2026-10-15*
