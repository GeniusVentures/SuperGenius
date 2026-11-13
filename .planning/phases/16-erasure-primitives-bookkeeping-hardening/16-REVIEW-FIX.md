---
phase: 16-erasure-primitives-bookkeeping-hardening
fixed_at: 2026-10-15T22:17:29Z
review_path: .planning/phases/16-erasure-primitives-bookkeeping-hardening/16-REVIEW.md
iteration: 1
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 16: Code Review Fix Report

**Fixed at:** 2026-10-15T22:17:29Z
**Source review:** .planning/phases/16-erasure-primitives-bookkeeping-hardening/16-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 4 (CR-01, WR-01, WR-02, WR-03; IN-01..IN-04 out of scope)
- Fixed: 4
- Skipped: 0

## Fixed Issues

### CR-01: EraseTransactionEntry resolves the CID across all monitored networks but erases the local-network path only

**Status:** fixed: requires human verification (logic change; compile-verified, no runtime test in this environment)
**Files modified:** `src/transaction/TransactionManager.cpp`, `src/transaction/TransactionManager.hpp`
**Commit:** 55e80f64c
**Applied fix:** New private helper `TransactionManager::ResolveTransactionPathAndCID(tx_hash)` owns the monitored-network scan (moved verbatim out of `GetTransactionCID`, single home per the no-duplication invariant) and returns the `{map key, CID}` pair resolved from the SAME network. `GetTransactionCID` now delegates to it; `EraseTransactionEntry` uses the pair so `GlobalDB::EraseEntry` and `RemoveProcessedTransactionEntry` both address the key the CID was actually found under. The local-network case is behavior-identical (the loop still scans the local network first). Header Doxygen for `EraseTransactionEntry` updated to state the paired resolution.

### WR-01: StageEraseElement's prefix scan over-erases hierarchical descendants and is unvalidated against the empty key

**Status:** fixed: requires human verification (logic change; runtime-verified by new regression tests)
**Files modified:** `src/crdt/impl/crdt_set.cpp`, `src/crdt/crdt_set.hpp`, `test/src/crdt/crdt_set_test.cpp`
**Commits:** 176de6520 (fix + tests), 3335739fa (test seeding correction)
**Applied fix:** `StageEraseElement` now rejects an empty `aKey` with `std::errc::invalid_argument` before staging (empty key degenerated the scans to the whole elems/tombs namespaces), and each scanned entry is filtered through `HierarchicalKey(id).IsTopLevel()` — the same discipline as `CreateDeltaToRemove` / `InElemsNotTombstoned` — so entries of descendant keys (`a/b` when erasing `a`) are never swept asymmetrically. Header contract documents both rules. Regression tests added: `EraseElementStagingTest.DescendantKeyFamiliesSurviveParentErase` and `EraseElementStagingTest.EmptyKeyIsRejected`. Follow-up commit 3335739fa corrected the tests' seeding to `CreateDeltaToAdd` + `PutElems` (`SetValue` writes only value/priority records, no elem entries; the product fix itself was already covered by the unchanged, still-passing `StagedEraseRemovesAllFamilies`).

### WR-02: EraseEntry's guards are check-then-act with no window protection; header contract states them as absolute

**Status:** fixed (documentation only, as scoped — no locking added)
**Files modified:** `src/crdt/globaldb/globaldb.hpp`, `src/crdt/globaldb/globaldb.cpp`
**Commit:** d043b86f4
**Applied fix:** `EraseEntry` Doxygen and the in-code guard comment now state the guards are check-then-act and best-effort under concurrency (a CID becoming a head / sync-job root / LRU-cached after its check but before the commit is erased anyway), and that the critical section closing the window is owned by the caller (Phase 18's TxPruner, RESEARCH Pitfall 7) — mirroring the compose-window caveat already present on `CrdtSet::StageEraseElement`.

### WR-03: Post-commit seal re-check failure returns a guard-equivalent error although the erase already committed

**Status:** fixed (error-semantics change; compile-verified, negative path not runtime-tested)
**Files modified:** `src/crdt/globaldb/globaldb.hpp`, `src/crdt/globaldb/globaldb.cpp`
**Commit:** 46cd906ad
**Applied fix:** Added `GlobalDB::Error::POST_COMMIT_ERASE_ANOMALY` (with category message "Erase committed, but the post-commit seal re-check failed (data was erased)"). The post-commit `isResolved` re-check now returns that distinct value instead of `Error::INVALID_PARAMETERS`, so callers can distinguish "nothing erased" (guard rejections) from "erased, then an out-of-band anomaly". Header `@return` documents that this value implies a committed erase.

## Skipped Issues

None — all in-scope findings were fixed.

## Verification

- **Erase suites (crdt_test), double-run:** 15/15 PASSED per run, two consecutive runs (`EraseElementStaging*`, `EraseBlockStaging*`, `EraseAtomicityTest*`, `EraseGuardTest*`, `EraseSurfaceGateTest*` — includes the two new WR-01 regression tests).
- **GlobalDB erase suite (globaldb_integration_test), double-run:** `GlobalDBEraseTest.*` 3/3 PASSED per run, two consecutive runs.
- **CR-01 compile verification:** the exact `TransactionManager.cpp` compile command from the worktree build (`compile_commands.json`) re-run as `-fsyntax-only` — PASSED (exit 0), covering the new helper, the header declaration, and all dependent headers.
- **Transaction tests (transaction_manager_pending_lifecycle_test / transaction_manager_certificate_fallback_test):** NOT run. They require the `genius_node_test` closure (src/account + src/blockchain + ProofSystem/SGProcessingManager/evmrelay submodules + zkLLVM), a multi-hour cold build in this worktree. CR-01 is instead compile-verified at full flag fidelity (above); the fixed method ships unwired (Phase 18 TxPruner is its production caller), so no runtime behavior changed for existing callers.
- **Build state:** `crdt_test` and `globaldb_integration_test` fully rebuilt from the fixed tree; configure reused the shared prebuilt thirdparty/zkLLVM products (no thirdparty rebuilds).

## Environment notes

- Fixes were developed in an isolated git worktree (branch `gsd-reviewfix/16-13833`, fast-forwarded to `gsd/phase-16-erasure-primitives-bookkeeping-hardening` at cleanup).
- `cmake/functions.cmake` carries an UNCOMMITTED local change (protoc `-I` uses a REALPATH'd project root) so protobuf generation works under a `/tmp`-symlinked worktree; it is a build-environment workaround only, is not part of the branch, and does not affect the main tree (whose paths have no symlink ambiguity). If a worktree under `/tmp` is needed again, this patch (or an equivalent) is required for proto targets.
- REVIEW.md frontmatter and finding statuses were updated to reflect the fixes; the four Info findings remain open, out of fix scope.

---

_Fixed: 2026-10-15T22:17:29Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
