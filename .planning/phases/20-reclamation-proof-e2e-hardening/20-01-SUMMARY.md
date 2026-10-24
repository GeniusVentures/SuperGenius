---
phase: 20-reclamation-proof-e2e-hardening
plan: "01"
subsystem: database
tags: [rocksdb, compaction, globaldb, prune-config, reclamation]

# Dependency graph
requires:
  - phase: 19-pruned-node-survival-recovery-hardening
    provides: GlobalDB erase surface + test fixture family (EraseNodeCollection) the smoke rides
provides:
  - GlobalDB::CompactStorage(const std::atomic<bool>*) — the ONE home of the whole-DB manual-compaction recipe (bottommost force flag, allow_write_stall=false, canceled passthrough, nullptr/nullptr whole-DB bounds)
  - GlobalDB::GetApproximateDiskUsageBytes() — the ONE home of the whole-DB size measurement (0xFF sentinel range, files+memtables flags)
  - PruningConfig::compact_threshold_mb (default 16, WR-03 degenerate-0 rejection) + GeniusNode resolved-values INFO extension
affects: [20-02 TxPruner trigger, 20-03 reclamation gate, 20-04 identity suites, 20-05 mesh]

# Tech tracking
tech-stack:
  added: []  # no new libraries — vendored RocksDB 9.4 surface only
  patterns:
    - "owner-vended compaction surface: options/bounds/flags knowledge encoded exactly once behind an outcome::result facade"
    - "rapidjson Slice borrows, never copies — sentinel strings must outlive the Slice they feed"

key-files:
  created: []
  modified:
    - src/crdt/globaldb/globaldb.hpp
    - src/crdt/globaldb/globaldb.cpp
    - test/src/crdt/globaldb_erase_test.cpp
    - src/transaction/PruneEligibility.hpp
    - src/transaction/PruneEligibility.cpp
    - src/account/GeniusNode.cpp
    - test/src/account/prune_eligibility_test.cpp

key-decisions:
  - "Post-shutdown fail-close uses Error::GLOBALDB_NOT_STARTED (the EraseEntry family precedent); the pre-set-cancel skip maps to std::errc::operation_canceled so callers can log it once, never as an error"
  - "The vendored non-const canceled pointer is bridged via const_cast with a NOLINTNEXTLINE (repo precedent, sha256.cpp) — the public contract keeps the caller's atomic const because we never write it"
  - "MB stays the single config representation for compact_threshold_mb; threshold bytes derive at the 20-02 comparison site — no bytes variant added"

patterns-established:
  - "Sentinel Slice lifetime rule: rocksdb::Slice(std::string(...)) dangles — name the string first"

requirements-completed: []  # RECL-01 spans plans 20-01/20-02/20-04 (trigger + Light-only enforcement land later); closing it after the substrate alone would be premature — the last carrier plan marks it

# Metrics
duration: 24min
completed: 2026-10-24
---

# Phase 20 Plan 01: Compaction Substrate Summary

**Owner-vended GlobalDB compaction + whole-DB size surface (the one recipe home) plus the pruning.compact_threshold_mb knob with WR-03 zero-rejection and the resolved-values INFO extension.**

## Performance

- **Duration:** 24 min
- **Started:** 2026-10-24T16:36:47Z
- **Completed:** 2026-10-24T17:00:41Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- `GlobalDB::CompactStorage(const std::atomic<bool> *canceled)`: whole-DB manual compaction with the locked recipe (bottommost force flag — the engine default compacts nothing without a filter; `allow_write_stall=false`; `exclusive_manual_compaction` left false; canceled passthrough). Fail-closed on `ActiveDataStore()` (GLOBALDB_NOT_STARTED after ShutdownNow); pre-set cancel → `std::errc::operation_canceled` (distinct clean-skip); other non-OK → `Error::ROCKSDB_IO` with a WARN carrying the status string.
- `GlobalDB::GetApproximateDiskUsageBytes()`: sentinel-range (16×0xFF high bound — the sizes API has no unbounded semantics; `["","")` is the EMPTY range) whole-DB measurement counting files AND memtables. Both facts encoded exactly once.
- One-home gates machine-checked and green: `grep "CompactRange(" src/` → zero callers outside globaldb.cpp; `kForce` count in globaldb.cpp == 1; `INCLUDE_MEMTABLES` count in src/ == 1.
- `PruningConfig.compact_threshold_mb` (default 16) parses as the exact settle_seconds twin (present ∧ IsUint64 ∧ >0); degenerate 0 (compact-every-cycle write-amp DoS, V5) rejected at read time, member keeps 16. Four new FromJSON cases (C11–C14) pin absent→16, explicit 8→8, zero→16, ill-typed→16.
- GeniusNode resolved-values INFO now carries `compact_threshold_mb={}` (GATE-06).

## Task Commits

Each task was committed atomically:

1. **Task 1: GlobalDB compaction surface — CompactStorage + GetApproximateDiskUsageBytes** - `40d423594` (feat)
2. **Task 2: PruningConfig compact_threshold_mb knob + FromJSON tests + GeniusNode resolved-values INFO** - `0d4313eaa` (feat)

**Plan metadata:** (see final docs commit)

## Files Created/Modified
- `src/crdt/globaldb/globaldb.hpp` — public declarations beside EraseEntry, full Doxygen; `<atomic>` include added
- `src/crdt/globaldb/globaldb.cpp` — the two implementations after EraseEntry; the one recipe home
- `test/src/crdt/globaldb_erase_test.cpp` — `CompactStorageSmokeTest` (derived from the GlobalDBEraseTest family so the pinned `CompactStorageSmoke*` filter matches)
- `src/transaction/PruneEligibility.hpp` — member + FromJSON @details extension
- `src/transaction/PruneEligibility.cpp` — the twin parse block
- `src/account/GeniusNode.cpp` — resolved-values INFO extension
- `test/src/account/prune_eligibility_test.cpp` — C11–C14

## Decisions Made
- **Fail-close code choice:** `Error::GLOBALDB_NOT_STARTED` (EraseEntry family) over `std::errc::operation_canceled` (QueryRaw family) — the compaction surface is a heavier owner-vended surface, matching the family the planner assigned; documented in the header contract.
- **const_cast on canceled:** plan-mandated (vendored 9.4 option is `std::atomic<bool>*` while the contract stays const); suppressed with the specific check name per repo precedent, comment states why.
- **Requirement closure deferred:** RECL-01 is carried by 20-01/20-02/20-04; `requirements-completed` stays empty so traceability stays honest (the scheduled trigger and Light-only proof are 20-02/20-04 scope).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Sentinel Slice dangling pointer made post-compaction size read 0**
- **Found during:** Task 1 (smoke test first run)
- **Issue:** The 0xFF sentinel was built as `const auto sentinel = Slice( std::string( 16, '\xFF' ) )` — rocksdb::Slice BORROWS the string buffer (no copy), so the bound dangled after the temporary died; the smoke test caught it (`size_after` read 0 vs 19 expected).
- **Fix:** Keep the sentinel as a named local `const std::string sentinel_high( 16, '\xFF' )` that outlives the Range, with a comment stating the borrow rule (the research example's original shape).
- **Files modified:** src/crdt/globaldb/globaldb.cpp
- **Verification:** `CompactStorageSmoke*` green; full `globaldb_integration` ctest green (83s).
- **Committed in:** 40d423594

**2. [Rule 3 - Blocking] Smoke-test filter needed a suite named CompactStorageSmoke\***
- **Found during:** Task 1 verification
- **Issue:** A `TEST_F(GlobalDBEraseTest, CompactStorageSmoke...)` does not match the plan-pinned `--gtest_filter='CompactStorageSmoke*'` (the filter matches the `Suite.Test` string from the start).
- **Fix:** Host the test on `class CompactStorageSmokeTest : public GlobalDBEraseTest` — same fixture family (inherits SeedEntry/helpers/teardown), suite name matches the pinned filter.
- **Files modified:** test/src/crdt/globaldb_erase_test.cpp
- **Verification:** filter runs 1/1 green.
- **Committed in:** 40d423594

---

**Total deviations:** 2 auto-fixed (1× Rule 1, 1× Rule 3)
**Impact on plan:** Both were correctness/testability fixes inside the planned scope. No scope creep.

## Verification Evidence

- `cmake --build build/OSX/Release --target globaldb_integration_test` — clean.
- `test_bin/globaldb_integration_test --gtest_filter='CompactStorageSmoke*'` — 1/1 passed.
- `ctest -R globaldb_integration --output-on-failure` — 1/1 suites passed (83.15s, full regression green).
- One-home greps — 0 / 1 / 1 as specified above.
- `cmake --build --target prune_eligibility_test` + `--gtest_filter='*PruningConfig*'` — 14/14 (10 existing + 4 new); `ctest -R prune_eligibility` — passed.
- `cmake --build --target genius_node` — clean.
- clang-format (llvm@22 git-clang-format vs HEAD) — clean on all 7 files.
- clang-tidy (llvm@22, single-arch-filtered compile db, unity TU for globaldb; direct TU for PruneEligibility.cpp/GeniusNode.cpp) — zero findings on new code lines. The const_cast is NOLINT-suppressed with rationale. The new FromJSON parse block carries the same `pro-bounds` operator[] warnings as its three pre-existing sibling blocks (plan mandates the exact twin block; the check is in the Recommended group and the neighbors define the convention); the new config member carries the same naming-style warning as all 20 sibling members of PruningConfig (struct convention, Recommended group). No Required findings introduced.

## Issues Encountered
None beyond the two deviations above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 20-02 (TxPruner trigger) can call `CompactStorage(&member_atomic)` + `GetApproximateDiskUsageBytes()` and read `config.compact_threshold_mb` — both contracts are in place with fail-close semantics, the clean-skip error channel, and the one-home guarantees machine-checked.
- The pre-existing TxPruner Doxygen hooks (`CompactRange` without parens at TxPruner.cpp:284, hpp:65/:117) retire in 20-02 as planned; the paren-form grep gate stays green until then.

## Self-Check: PASSED

- All 7 modified files exist on disk (git-clean after commits).
- Task commits verified: `40d423594` (Task 1), `0d4313eaa` (Task 2) present on `gsd/phase-20-reclamation-proof-e2e-hardening`.

---
*Phase: 20-reclamation-proof-e2e-hardening*
*Completed: 2026-10-24*
