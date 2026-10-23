---
phase: 19-pruned-node-survival-recovery-hardening
plan: 01
subsystem: database
tags: [utxo, rocksdb, spend-evidence, preserve-clear, outcome-result]

# Dependency graph
requires:
  - phase: 17-eligibility-policy-gating-dry-run
    provides: spent_by_txid spend-recency carrier (ConsumeUTXOs/GetOutPointSpender) the preserve-clear keeps alive
provides:
  - UTXOManager::ClearUnspentKeepConsumed(address) — owner-vended consumed-preserving clear (memory + disk)
  - KeepConsumed unit suites incl. raw /utxo/ prefix disk assertions and the SetUTXOs wipe negative control
  - The D-01 substrate 19-04's checkpoint-mismatch rebuild swaps in for SetUTXOs
affects: [19-04 (rebuild guard + crosscheck), 19-06 (restart matrix), GATE-05]

# Tech tracking
tech-stack:
  added: [] # zero new dependencies — existing UTXOManager/rocksdb surfaces only
  patterns: [owner-vended preserve clear (SetUTXOs lock/snapshot/rollback shape with a CONSUMED survivor predicate), raw /utxo/ prefix read in tests as the anti-vacuous disk check]

key-files:
  created: []
  modified:
    - src/account/UTXOManager.hpp
    - src/account/UTXOManager.cpp
    - test/src/account/utxo_manager_test.cpp

key-decisions:
  - "Preserve is unconditional (D-01 adopted default): no pruned-evidence gate; consumed evidence is never wrong to keep"
  - "SetUTXOs wipes only index-resident entries — ConsumeUTXOs leaves CONSUMED entries orphaned from address_outpoints_; the real-world wipe path is post-restart (LoadUTXOs → SetUTXOs), so the negative control seeds through a reload"

patterns-established:
  - "Preserve-clear discipline: mutate utxo_outpoints_ only through the owner's unique_lock + snapshot-rollback + StoreUTXOsLocked shape; no second /utxo/ writer"
  - "Three-part disk proof for clear semantics: present-before on the raw prefix, survivor-still-present with spent_by_txid bytes, cleared-keys-gone; NOT_FOUND is the only tolerated query error"

requirements-completed: [GATE-05]

# Metrics
duration: 17min
completed: 2026-10-23
---

# Phase 19 Plan 01: UTXOManager Consumed-Preserving Clear Summary

**Owner-vended `ClearUnspentKeepConsumed` in UTXOManager keeps CONSUMED records (with `spent_by_txid`) alive in memory and on the raw `/utxo/` prefix while dropping READY/RESERVED entries and reservations — the D-01 substrate that makes `PutUTXO`'s duplicate check a by-construction resurrection blocker.**

## Performance

- **Duration:** 17 min
- **Started:** 2026-10-23T18:23:28Z
- **Completed:** 2026-10-23T18:40:xxZ
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- `UTXOManager::ClearUnspentKeepConsumed(address)` under `utxos_mutex_`: erases READY/RESERVED entries + their `local_reservations_`, keeps every `UTXO_CONSUMED` entry (spender binding intact) in `utxo_outpoints_` and the address index, persists only via `StoreUTXOsLocked` with the SetUTXOs snapshot-rollback-on-store-failure shape
- `SetUTXOs` body byte-identical (diff is pure addition) — existing callers keep wipe-everything semantics
- KeepConsumed suites prove evidence survival in memory AND on disk (raw RocksDB prefix read, parsed `UTXOEntryRecord.spent_by_txid()` bytes matched), reservations dropped (`kAlreadyReserved` → `kNotClaimable` discriminator), genuine-vs-tombstone both surviving and both blocking re-creation via `PutUTXO`
- Full `utxo_manager_test` binary green (37 tests) — pre-existing suites are the negative control

## Task Commits

Each task was committed atomically:

1. **Task 1: UTXOManager::ClearUnspentKeepConsumed — the consumed-preserving clear** - `fda039d46` (feat)
2. **Task 2: KeepConsumed unit suites (memory + disk + negative control)** - `e896b3d77` (test)

## Files Created/Modified
- `src/account/UTXOManager.hpp` — Doxygen-documented `ClearUnspentKeepConsumed` declaration
- `src/account/UTXOManager.cpp` — implementation after `SetUTXOs` (which is untouched); +3 lines of pre-existing format drift normalized in `TryReserveOutpoint`/`VerifyParameters` for the format gate
- `test/src/account/utxo_manager_test.cpp` — `ReadRawUtxoRecords` helper + 3 KeepConsumed tests; Doxygen file header added; pre-existing format drift normalized (~30 lines across old tests) for the format gate

## Decisions Made
- Preserve is unconditional (plan's adopted open-question default) — no pruned-evidence gate; logging only records kept/dropped counts
- CONSUMED survivors also lose any lingering `local_reservations_` entry (matches ConsumeUTXOs's own erase-on-consume; a consumed outpoint is not reservable)
- Negative-control seeding goes through a reload, not a live ConsumeUTXOs (see Deviations 2) — this is the shape `InitializeUTXOs` actually clears

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Map key type mismatch in test helper**
- **Found during:** Task 2 (first build)
- **Issue:** `Buffer::toString()` returns `std::string_view`, not `std::string` — `std::map<std::string, ...>::operator[]` rejected it
- **Fix:** `parsed[std::string( key.toString() )]`
- **Files modified:** test/src/account/utxo_manager_test.cpp
- **Verification:** clean build, suites green
- **Committed in:** `e896b3d77` (part of task commit)

**2. [Rule 1 - Bug] Negative control seeded the wrong index shape**
- **Found during:** Task 2 (first test run — wipe assertions failed)
- **Issue:** `ConsumeUTXOs` removes the outpoint from `address_outpoints_`, so a live-consumed entry is orphaned from the index and `SetUTXOs({}, addr)` — which walks only the index — leaves it in `utxo_outpoints_` (and `SnapshotAddressUTXOsLocked` re-persists it). The documented wipe-everything semantics hold for the **index-resident** shape, which is what the real path produces: `LoadUTXOs` indexes every persisted record (CONSUMED included) before `InitializeUTXOs`'s mismatch branch clears
- **Fix:** Negative control reloads into a fresh manager first, asserts the CONSUMED entry is index-resident with its spender, then `SetUTXOs({}, owner)` and asserts full wipe (memory + raw prefix empty)
- **Files modified:** test/src/account/utxo_manager_test.cpp
- **Verification:** `*KeepConsumed*` filter passes twice consecutively; full binary green
- **Committed in:** `e896b3d77` (part of task commit)
- **Note for 19-04:** the main suite's reload leg covers exactly the index-resident clear; the orphan nuance is in-memory-only and cannot survive a restart

**3. [Rule 3 - Blocking] clang-format normalized pre-existing drift in both touched C++ files**
- **Found during:** Task 1 & 2 verification (format gate)
- **Issue:** `clang-format --dry-run --Werror` fails on the whole changed file when it carries older drift — 3 lines in `UTXOManager.cpp` (`TryReserveOutpoint`, `VerifyParameters`), ~30 lines across old tests in `utxo_manager_test.cpp`
- **Fix:** `clang-format -i` on both files (whitespace/alignment only, no logic); required to satisfy the acceptance criterion "clang-format --dry-run --Werror clean on all three changed files"
- **Files modified:** src/account/UTXOManager.cpp, test/src/account/utxo_manager_test.cpp
- **Verification:** `--dry-run --Werror` clean on all three changed files; full rebuild + tests green after
- **Committed in:** `fda039d46`, `e896b3d77` (noted in both commit messages)

---

**Total deviations:** 3 auto-fixed (2 Rule 1 bugs, 1 Rule 3 blocking)
**Impact on plan:** All fixes required for the gates to pass. No scope creep; `SetUTXOs` remains byte-identical.

## Issues Encountered
- clang-tidy compile-db filter: the memory's sed recipe (`s/ -arch x86_64 //g`) glues `-std=c++17` to the remaining `-arch arm64`; corrected to `s/ -arch x86_64//g`. With the corrected filter, clang-tidy ran clean-of-Required on the changed lines: the only new-line finding is `misc-const-correctness` on the `lock` local — identical to the file-wide established idiom (fires on `SetUTXOs` itself at :491 and 20+ siblings); naming-config warnings are known noise per repo memory; test targets have clang-tidy auto-disabled via `addtest`

## Threat Mitigations (per plan threat_model)
- **T-19-0101** (spent evidence survives): raw `/utxo/` prefix assertions match `spent_by_txid` bytes after the clear (main suite, both legs) — DONE
- **T-19-0102** (no stale READY survivor): READY/RESERVED gone in memory and on disk; reservation drop proven via `TryReserveOutpoint` outcome change — DONE
- **T-19-0103** (single writer shape): implementation persists only through `StoreUTXOsLocked`; key_link regex `ClearUnspentKeepConsumed[\s\S]*StoreUTXOsLocked` verified against the source — DONE

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 19-04 can swap `SetUTXOs(std::vector<GeniusUTXO>{}, address)` for `ClearUnspentKeepConsumed(address)` at the `InitializeUTXOs` mismatch branch (TransactionManager.cpp:~3265); `PutUTXO`'s duplicate check is now load-bearing for preserved CONSUMED outpoints, and the `IsOutPointGenuinelyConsumed`/`GetOutPointSpender` crosscheck reads evidence the clear no longer destroys
- Wave-1 siblings 19-02/19-03 (TM, crdt_datastore) are untouched by this plan's files
- KeepConsumed suites double-run green — SURV-01 restart-matrix assertions can build on the raw-prefix read helper idiom

## Self-Check: PASSED

All 4 files exist; both task commits (fda039d46, e896b3d77) present in git log; `ClearUnspentKeepConsumed` present in both production files; test artifact 1085 lines (min 60 satisfied).

---
*Phase: 19-pruned-node-survival-recovery-hardening*
*Completed: 2026-10-23*
