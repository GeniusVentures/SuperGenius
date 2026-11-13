---
phase: 16-erasure-primitives-bookkeeping-hardening
plan: 04
subsystem: database
tags: [crdt, globaldb, graphsync-dagsyncer, rocksdb, bufferbatch, erasure, pruning, testing, ci-gate]

# Dependency graph
requires:
  - phase: 16-erasure-primitives-bookkeeping-hardening (plan 01)
    provides: CrdtSet::StageEraseElement four-family staging + public IsRootCIDPendingOrActive
  - phase: 16-erasure-primitives-bookkeeping-hardening (plan 02)
    provides: GraphsyncDAGSyncer::StageBlockRemoval + the EraseBlockStagingTest real-store fixture file
  - phase: 16-erasure-primitives-bookkeeping-hardening (plan 03)
    provides: GlobalDB::EraseEntry guard ensemble + SetEraseInterruptHookForTest seam
provides:
  - EraseAtomicityTest — one-batch three-owner all-or-nothing proof on a shared temp DB, including destroy-before-commit and DB-reopen (SC-2 substrate)
  - EraseGuardTest — pending/active guard true-then-false on a stalled real external root job; LRU membership input pinned (SC-3)
  - GlobalDBEraseTest + GlobalDBEraseInterruptTest — SC-1/SC-2/SC-3 end-to-end through the real facade on real pubsub nodes
  - EraseSurfaceGateTest — CI-enforced source-scan bans (sealed literal, removeNode dormancy, tombstone-free erase bodies)
  - CustomDagSyncer optional root-fetch stall (opt-in, default-inert) — the D-03 stall vehicle
affects: [phase-exit verification (/gsd:verify-work), 18 (TxPruner inherits the proven suites), GATE-03, PRUNE-02]

# Tech tracking
tech-stack:
  added: []  # no new libraries — std::filesystem walker + existing gtest/ctest machinery only
  patterns:
    - runtime source-scan gate (compile-definition source root + std::filesystem walk; renamed/missing definitions FAIL, never bypass)
    - stalled-root-job guard proof (mirror-loopback receiver + opt-in DAG-worker fetch stall pins activeRootCID_)

key-files:
  created:
    - test/src/crdt/globaldb_erase_test.cpp
  modified:
    - test/src/crdt/crdt_erase_test.cpp
    - test/src/crdt/crdt_custom_dagsyncer.hpp
    - test/src/crdt/crdt_custom_dagsyncer.cpp
    - test/src/crdt/CMakeLists.txt
    - .gitignore

key-decisions:
  - "The pending/active guard is proven on the RECEIVER of a mirrored publish, not on the putting node: a self-created Put job lives in selfCreatedJobList_ and never enters pendingRootQueue_/activeRootCID_, so stalling the sender would leave the guard false forever — the external root path (HandleCIDBroadcast -> EnqueueRootCID -> SeedNextExternalRoot -> CreateRootJob -> getNode) is the machinery the guard actually protects"
  - "The stall lives at CustomDagSyncer::getNode — the DAG worker's fetch-a-root entry, reached with activeRootCID_ already set, so the stalled worker holds the guard true while holding no dagWorkerMutex_ (no poll deadlock); default path is a single relaxed atomic load"
  - "The gate's source root uses PROJECT_ROOT, not the plan's CMAKE_SOURCE_DIR literal: this repo drives CMake from build/OSX/ (out-of-root driver), where CMAKE_SOURCE_DIR/build/OSX/src does not exist — PROJECT_ROOT is the repo root set by CommonCompilerOptions.cmake"
  - "Raw CRDT-entry assertions scan QueryRaw('/crdt/') for keys containing the element-key needle: HierarchicalKey always carries the leading slash, and GlobalDB's CRDT namespace is 'crdt' — needle-matching sidesteps exact per-family layout knowledge while still proving raw-level erasure"
  - "Public GetJobStatus replaces the plan's WaitForJob wait: WaitForJob is protected on CrdtDatastore (16-03 moved only IsRootCIDPendingOrActive/StageEraseElement public); GetJobStatus is public and equivalent for completion polling"

patterns-established:
  - "Mutation-verified RED for already-committed behavior (16-02 precedent): sabotage EraseEntry's map-entry removal -> end-to-end case fails at the map absent-after assert -> revert -> green"

requirements-completed: [PRUNE-02, GATE-03]

# Metrics
duration: 71min
completed: 2026-10-15
---

# Phase 16 Plan 04: Erase Verification Suites + Enforcement Gate Summary

**Three real-store suites (substrate atomicity, GlobalDB end-to-end + interruption crash injection, guard inputs) plus a ctest source-scan gate that turns the phase's seal/removeNode/tombstone "never" rules into CI-enforced facts — SC-1..SC-5 all green, full 151-test suite green (one unrelated foundry flake re-verified in isolation)**

## Performance

- **Duration:** 71 min (started 2026-10-15T20:19:47Z, completed 2026-10-15T21:31:15Z; ~45 min of it the SC-5 full-suite run)
- **Tasks:** 3
- **Files:** 6 (5 planned + .gitignore)

## Accomplishments
- SC-2 substrate: one BufferBatch staging {CrdtSet four families + block key + raw map key} over ONE shared temp RocksDB commits all-or-nothing — `EraseAtomicityTest.OneBatchErasesAllThreeLevels` proves present-before/absent-after on every level, seal survival, and sibling byte-identity on both other levels; `InterruptedBatchIsAllOrNothing` destroys the batch before commit and re-opens the DB to prove nothing was durably written
- SC-3 inputs: `EraseGuardTest.PendingOrActiveGuardTracksRootJob` proves IsRootCIDPendingOrActive true while a real external root job is stalled inside the receiver's DAG worker and false after completion (value applied, HasKey true); `LRUCacheMembershipIsGuardInput` pins the D-C5 LRU signal
- SC-1/SC-2/SC-3 facade: `GlobalDBEraseTest.EraseEntryRemovesAllLevelsEndToEnd` (three-part assertions + negative control + head-intact), `InterruptedEraseIsAllOrNothing` parameterized over steps 1/2/3 (everything present after every aborted compose), `HeadCidIsRejected` and `UnresolvedCidIsRejected` (exact INVALID_PARAMETERS value, all levels unchanged)
- SC-4 gate: `EraseSurfaceGateTest` scans src/ at test runtime — no quoted seal literal anywhere, `removeNode(` confined to its two owner files, and the five tracked erase-surface function bodies carry none of the banned tombstone/revert tokens; missing or renamed definitions FAIL the gate; all three cases demonstrated to fail on injected violations and then reverted
- SC-5: full `ctest --test-dir build/OSX/Release` — 150/151 passed; the single failure (`bridge_race_fault_partition_test`, requires_foundry, external Sepolia fork) passed its isolated re-run and is unrelated to this test-only diff (logged in deferred-items.md)
- `CustomDagSyncer` stall is opt-in and default-inert: full `CrdtDatastore*` 11/11 and whole `crdt_test` 37/37 green

## Task Commits

Tasks 1 and 2 are TDD (RED then GREEN/mutation-verified); Task 3 is a gate + verification task:

1. **Task 1 RED: failing EraseAtomicity/EraseGuard suites** - `ef10d7e6c` (test; compile-RED observed on the missing stall members)
2. **Task 1 GREEN: CustomDagSyncer root-fetch stall + green suites** - `f63330187` (test; includes .gitignore runtime-dir entries)
3. **Task 2: globaldb_erase_test.cpp + CMake join** - `acdd005e6` (test; mutation-verified RED: sabotaged map-entry removal made the end-to-end case fail, then reverted)
4. **Task 3: EraseSurfaceGateTest + SUPERGNUS_ERASE_GATE_SRC** - `210e72daf` (test; violation injections demonstrated, full suite run recorded here)

**Plan metadata:** see final docs commit

## Files Created/Modified
- `test/src/crdt/crdt_erase_test.cpp` — EraseAtomicityTest (2 cases), EraseGuardTest (2 cases), EraseSurfaceGateTest (3 cases) + the recursive source walker/body extractor; 16-02's EraseBlockStagingTest untouched
- `test/src/crdt/crdt_custom_dagsyncer.hpp` / `.cpp` — dtor, ArmRootFetchStall/ReleaseRootFetchStall, WaitAtRootFetchStallIfArmed gate at getNode
- `test/src/crdt/globaldb_erase_test.cpp` — NEW: single-node EraseNodeCollection (addNode recipe), GlobalDBEraseTest (3 cases), GlobalDBEraseInterruptTest (parameterized over steps 1/2/3); map-entry seed carries the StoreTransactionCID shape citation, seal key the vendored-contract comment
- `test/src/crdt/CMakeLists.txt` — globaldb_erase_test.cpp appended to globaldb_integration_test; `target_compile_definitions(crdt_test PRIVATE SUPERGNUS_ERASE_GATE_SRC="${PROJECT_ROOT}/src")` after the crdt_test block
- `.gitignore` — the four new suite runtime dirs

## Decisions Made
- The pending/active guard test had to be a two-node mirror loopback: analysis of the worker loop showed self-created Put jobs (selfCreatedJobList_) never register in pendingRootQueue_/activeRootCID_ that IsRootCIDPendingOrActive reads — only external roots (broadcast -> EnqueueRootCID -> SeedNextExternalRoot) do. The receiver of a mirrored publish therefore carries the real "stalled root job" the guard exists for; the test documents this in-line
- getNode chosen as the stall point over addNode/fetchGraph: CreateRootJob calls it with activeRootCID_ already set and no dagWorkerMutex_ held, so the guard remains pollable while stalled and the job cannot complete behind the gate
- The interruption cases clear the hook explicitly after each case AND the node collection's destructor clears it again — a seam left armed by a failed assertion must never leak into later tests (T-16-15)
- Heads/Guard test seeds Put with a real topic ({kTopic} + AddBroadcastTopic/AddListenTopic): an empty-topics Put creates a node with no destinations, so UpdateCRDTHeads never records it — no head, no markResolved seal. The integration-file topic recipe is load-bearing
- `countCrdtRawEntries` needle-matches QueryRaw("/crdt/") instead of enumerating family layouts — proves raw-level CRDT erasure without the test owning key-layout knowledge that belongs to CrdtSet
- Gate scans textually (comments included) deliberately: 16-02/16-03 already keep even Doxygen token-free in src/, so any textual occurrence is a real drift signal

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Gate source root could not use CMAKE_SOURCE_DIR**
- **Found during:** Task 3 CMake wiring
- **Issue:** The plan specifies `SUPERGNUS_ERASE_GATE_SRC="${CMAKE_SOURCE_DIR}/src"`, but this repo drives CMake from `build/OSX/CMakeLists.txt` (out-of-root driver), so CMAKE_SOURCE_DIR is `<repo>/build/OSX` and `${CMAKE_SOURCE_DIR}/src` does not exist — the gate would scan nothing and vacuously pass
- **Fix:** Used `PROJECT_ROOT` (repo root, set in CommonCompilerOptions.cmake, inherited into the test subdir scope); comment in CMakeLists.txt records why
- **Files modified:** test/src/crdt/CMakeLists.txt
- **Commit:** `210e72daf`

**2. [Rule 3 - Blocking] WaitForJob is protected; plan's wait vehicle unusable**
- **Found during:** Task 1 guard-test design
- **Issue:** The plan's interface block lists `CrdtDatastore::WaitForJob(cid)` as public; it is protected (16-03's access fix moved only IsRootCIDPendingOrActive and StageEraseElement public)
- **Fix:** Poll the public `GetJobStatus(cid) == JobStatus::COMPLETED` via assertWaitForCondition — same completion signal, public surface
- **Files modified:** test/src/crdt/crdt_erase_test.cpp
- **Commit:** `ef10d7e6c`

**3. [Rule 1 - Bug] Present-before elems assertion was vacuously wrong**
- **Found during:** Task 1 GREEN first run
- **Issue:** Seeding via `CrdtSet::SetValue` writes value/priority but never the /s/ elems family, so `countKeysWithPrefix(ElemsPrefix) == 1` failed 0 vs 1
- **Fix:** Seed via `CreateDeltaToAdd` + `PutElems` (the exact write shape a merged delta performs, mirroring 16-01's fixture)
- **Files modified:** test/src/crdt/crdt_erase_test.cpp
- **Commit:** `f63330187`

**4. [Rule 1 - Bug] GlobalDB raw CRDT scan missed the leading slash**
- **Found during:** Task 2 first run
- **Issue:** QueryRaw prefix "crdt/" matched nothing — HierarchicalKey normalization gives raw keys a leading slash ("/crdt/s/...")
- **Fix:** Prefix "/crdt/"; needle match proves family erasure
- **Files modified:** test/src/crdt/globaldb_erase_test.cpp
- **Commit:** `acdd005e6`

**5. [Process] TDD RED delivered as mutation checks where implementation pre-dates the plan**
- **Found during:** Tasks 1 and 2 (tdd="true")
- **Issue:** Both tasks test behavior committed in 16-01..16-03, so a genuine pre-implementation RED is structurally impossible (16-02 precedent)
- **Fix:** Task 1 RED is a compile-RED on the genuinely-new stall members (observed, committed); Task 2 RED is a mutation check — sabotaging EraseEntry's map-entry removal made `EraseEntryRemovesAllLevelsEndToEnd` fail at the map absent-after assert, then reverted to green
- **Verification:** RED outputs observed; both suites green twice after

**6. [Hygiene] Runtime RocksDB dirs of the new suites ignored**
- **Found during:** Task 1
- **Issue:** Same as 16-01/16-02: fresh-DB suites clean leftovers at start, never after
- **Fix:** Four new entries in .gitignore
- **Files modified:** .gitignore
- **Commit:** `f63330187`

---

**Total deviations:** 6 auto-fixed (2 Rule 3 blocking, 2 Rule 1 bugs, 1 TDD process, 1 hygiene)
**Impact on plan:** None on scope — every acceptance criterion is green.

## Issues Encountered
- `clang-tidy` not runnable (binary not on PATH; compile_commands.json double-compiler-job abort — 16-01 finding, on record in deferred-items.md). All changes are test-target files; compensated with warning-count-zero rebuilds of both touched targets
- Whole-file `clang-format` unusable as an absolute gate on pre-existing files (local 19.1.7 vs repo-canonical formatter); every touched file was verified format-clean on ADDED lines only, and the two files created/wholesale-formatted in this plan (globaldb_erase_test.cpp, crdt_erase_test.cpp) are fully format-clean
- SC-5 full-suite run: `bridge_race_fault_partition_test` (requires_foundry) SEGFAULTed once at 28.6s; isolated re-run passed (67.6s). Unrelated external-RPC fault-injection flake — logged in deferred-items.md, not fixed (scope boundary)

## Verification Results
- `crdt_test --gtest_filter='Erase*'`: 13/13 PASSED (ElementStaging 3 + BlockStaging 3 + Atomicity 2 + Guard 2 + SurfaceGate 3), run twice on fresh DBs
- Full `crdt_test`: 37/37 PASSED (stall default-inert; `CrdtDatastore*` 11/11 inside it)
- `globaldb_integration_test` filter run: 6/6 PASSED (3 fixture + 3 parameterized interrupt steps), twice; `ctest -R globaldb_integration_test` green twice (53s each)
- Gate negative controls: injected `"sealed"` literal -> SealKeyHasNoProductionHome FAILED; injected `removeNode(` outside owner files -> RemoveNodeStaysDormant FAILED (reported file:line); injected `DeleteKey` comment inside StageEraseElement body -> EraseSurfaceContainsNoTombstones FAILED; all reverted, 3/3 green again
- Mutation RED: EraseEntry map-entry removal sabotage -> end-to-end case FAILED at the map absent-after assert; reverted -> 6/6 green
- Full suite `ctest --test-dir build/OSX/Release`: 150/151 passed in-run; the single failure passed its isolated re-run (see Issues) — every erase/gate test green in the full run
- No tracked-file deletions in any task commit; no stray untracked files (new runtime dirs ignored)

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 16 is complete (4/4 plans): the erasure primitives (16-01/16-02), the guarded one-batch compose (16-03), and the verification/gate evidence (16-04) are all in place with zero production callers — the pruner stays unwired
- Phase 18's TxPruner inherits ready-made suites: extend EraseAtomicityTest for wider critical sections, call EraseTransactionEntry and reuse the GlobalDB interruption seam for prune-window crash injection
- The gate (SC-4) now enforces the tombstone ban and seal unaddressability on every future change to the five tracked functions; renames break the gate loudly by design

## TDD Gate Compliance
- Task 1: RED `test(16-04)` commit `ef10d7e6c` (compile failure on missing ArmRootFetchStall observed and recorded) followed by GREEN `f63330187` — classic sequence intact
- Task 2: implementation pre-dates the plan (16-03 commits); RED delivered as a documented mutation check (map-entry sabotage -> observable failure -> revert), matching the 16-02 precedent — no fabricated test-before-feat history
- Task 3: not a tdd task (gate + verification)

## Self-Check: PASSED

All 6 claimed files exist on disk; all 4 task commit hashes (ef10d7e6c, f63330187, acdd005e6, 210e72daf) present in git log.

---
*Phase: 16-erasure-primitives-bookkeeping-hardening*
*Completed: 2026-10-15*
