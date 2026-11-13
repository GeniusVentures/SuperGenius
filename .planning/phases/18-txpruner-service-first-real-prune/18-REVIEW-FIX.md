---
phase: 18-txpruner-service-first-real-prune
fixed_at: 2026-10-21T23:45:40Z
review_path: .planning/phases/18-txpruner-service-first-real-prune/18-REVIEW.md
iteration: 1
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 18: Code Review Fix Report

**Fixed at:** 2026-10-21T23:45:40Z
**Source review:** 18-REVIEW.md (reviewed 2026-10-21T23:09:00Z)
**Iteration:** 1
**Fix scope:** critical_warning (CR-01, WR-01..03 — the 5 Info items are out of scope, untouched)

**Summary:**
- Findings in scope: 4
- Fixed: 4 (CR-01 flagged "requires human verification" per the logic-fix protocol)
- Skipped: 0

All fixes were made and committed inside an isolated worktree on `gsd-reviewfix/18-72438` and fast-forwarded onto `gsd/phase-18-txpruner-service-first-real-prune` per finding, so each commit was built and tested in the warm `build/OSX/Release` tree at its own HEAD.

## Fixed Issues

### CR-01: Use-after-move of `transaction_hash` made the MintFunds catch-path rollback a silent no-op

**Status:** fixed — **requires human verification** (logic-error-class fix; syntax, build, and a driving regression test all pass, but the semantic correctness of the rollback ordering deserves an owner's eye before phase close)

**Files modified:** `src/transaction/TransactionManager.cpp`, `src/transaction/TransactionManager.hpp`, `test/src/account/transaction_manager_certificate_fallback_test.cpp`
**Commit:** `08421f54a` — `fix(18): CR-01 capture MintFunds burn reservation id before the try block`

**Applied fix:**
- `const std::string burn_reservation_id = transaction_hash;` captured beside `rollback_inputs`, BEFORE the try block (TransactionManager.cpp:1037-1044), with a comment stating the move-into-`FillDAGStruct` failure mode.
- The catch handler's `RemoveBurnUTXOs(rollback_inputs, ...)` call and both error logs now use `burn_reservation_id` (previously the moved-from, always-empty parameter — the exact `held_by_other` skip the reviewer traced).
- The FAILED-transition call site (~:5095, pre-fix line numbering) was verified correct and left untouched, as the review directed.

**Regression test (new):** `CertificateFallbackTest.BurnCleanup_MintFundsCatchPathRemovesBurnUtxoAfterPostMoveThrow`. No natural throw site exists inside the try (`MintTransactionV2::New`, `MakeSignature`, `EnqueueTransaction` all non-throwing for a valid fixture), so the fix adds the `SetMintFundsPostMoveExceptionHookForTest` seam — same ForTest-hook pattern as the two existing `TransactionManager` seams — invoked after `MakeSignature`, i.e. strictly after the move. The test drives `MintFunds` through its own insert+claim, throws from the seam, and asserts the burn entry is REMOVED (memory: `GetOutPointState == nullopt`; disk: no `/utxo/<owner>/<hash>:0` record) and re-delivery is unblocked (`TryReserveOutpoint` returns `kNotClaimable` on the clean slate). The moved-from-empty-id failure mode fails every assertion (the entry survives as RESERVED).

### WR-01: RemoveBurnUTXOs rollback silently swallowed repair-store failures

**Files modified:** `src/account/UTXOManager.cpp`
**Commit:** `4bda1178a` — `fix(18): WR-01 log failed RemoveBurnUTXOs rollback repair stores`

**Applied fix:** each `StoreUTXOsLocked( repaired )` in the rollback repair loop now checks its result and logs `ERROR` with the owner and the error value ("persisted state may diverge from memory"). The no-throw contract of the rollback path is unchanged — the repair failure is logged, not propagated; the original store error is still the returned failure.
**Note:** the reviewer-flagged pre-existing sibling at UTXOManager.cpp:357-360 (consume-rollback path, outside the phase diff) was deliberately left untouched — out of this finding's scope.

### WR-02: Prune marker record named the local-network path for cross-network erases

**Files modified:** `src/transaction/PruneEligibility.hpp`, `src/transaction/PruneEligibility.cpp`, `src/transaction/TransactionManager.hpp`, `src/transaction/TransactionManager.cpp`, `test/src/account/prune_eligibility_test.cpp`
**Commit:** `b0fad55b6` — `fix(18): WR-02 build prune marker from the resolved tx path`

**Applied fix:** `BuildPruneMarkerEntry` now takes `(tx_hash, resolved_tx_path)` and sets `record.set_tx_path(resolved_tx_path)` — no local-network formatting inside the builder. Marker construction moved INTO `EraseTransactionEntryWithMarker` (parameter changed from the prebuilt `extra_entry` pair to `bool stage_pruned_marker`): the marker is built AFTER `ResolveTransactionPathAndCID`, from the SAME `path` the erase targets — single resolution, so no pre/post-resolution divergence window (a TOCTOU where the marker could pair with a different network's path than the erased one). Both drivers updated; header docs (including the D-C1 contract and the `@param` block) rewritten to state the resolved-path requirement.
**Tests:** F1/F2/F3 call sites updated to pass the resolved path (byte-identity property preserved); two NEW tests:
- `PrunedEraseDriver_MarksTheResolvedCrossNetworkPath` — end-to-end: value + tx-to-CID entry seeded ONLY under the TEST_NET path; `EraseTransactionEntryPruned` resolves the foreign network, erases there, and the marker's record names the foreign path. Fails pre-fix (record would name the never-existent local path).
- `PruneMarker_CrossNetworkRecordCarriesResolvedPath` — unit: marker key home unchanged (`/prune/tx/<hash>`), and a surviving value at the resolved foreign path now trips `VALUE_PRESENT_INVARIANT_VIOLATION` through the real probe (pre-fix behavior: silent PRUNED through a vacuously absent local path).

### WR-03: "first real cycle in 1 min" operator log contradicted the settle-window semantics

**Files modified:** `src/account/GeniusNode.cpp`
**Commit:** `56a73ab05` — `fix(18): WR-03 correct first-real-cycle schedule in prune timer log`

**Applied fix:** chose the message fix (the review's option (a)) because the stamp semantics ARE what the plan intended — settle-from-gate-hold (`TickGateSettled` stamps on the first tick where the gate holds). The real-arm log now reads: "first real cycle on the first tick after the {settle}s settle window has elapsed from the gate hold (≈11 min at the default cadence), then every 10 min (max {} erases per cycle)". The dry-run arm is untouched — its "first cycle in 1 min" is accurate (it does not pass through the tick gate). The TickGate tests were inspected and already encode exactly the settle-from-gate-hold semantics, so no test alignment was needed (the review's conditional "align the TickGate test if it encoded the wrong expectation" did not fire).

## Verification Evidence

**Affected filters (per fix, run at that fix's HEAD in the warm `build/OSX/Release` tree):**

| Filter | Trigger | Result |
|---|---|---|
| `transaction_manager_certificate_fallback_test` | CR-01 | Passed (5.31s); new test `[ OK ] 41 ms` via targeted gtest run |
| `utxo_manager_test` | WR-01 | Passed (2.38s) |
| `prune_eligibility_test` | WR-02 | Passed (3.29s); both new WR-02 tests `[ OK ]` 70 ms / 48 ms |
| `tx_pruner_test` | WR-03 | Passed (34.00s) |

**Consolidated gate at final HEAD (`56a73ab05`, all four binaries rebuilt first):** `ctest` 4/4 passed — `utxo_manager_test`, `transaction_manager_certificate_fallback_test` (5.36s), `prune_eligibility_test` (3.20s), `tx_pruner_test` (32.92s); 100% tests passed, 0 failed.

**Tooling gates (real, per the phase constraints):**
- `git-clang-format` (llvm@22, changed-lines only) run before every commit; formatter adjustments (one test-file line wrap, the `BuildPruneMarkerEntry` signature wrap) applied and committed — all four commits are format-clean against the repo `.clang-format`.
- `clang-tidy` (llvm@22, single-arch filtered compile db at `/tmp/clangtidy-db-18`) over the four changed TUs: ZERO warnings on any new or edited line. The 547 warnings referencing these files are pre-existing repo-wide findings (the known naming-config noise — the repo `.clang-tidy` naming values are rejected by this tool version, so defaults fire on the repo's own conventions — plus duplicate-include/style findings in untouched regions). The only in-range hits (`TransactionManager.cpp:1046` `txId` naming, `:4372` value-param) are pre-existing lines shifted into the search window by added comment lines; neither is on changed code.
- Builds clean (no new warnings in the compiled TUs; `TransactionManager.cpp`, `PruneEligibility.cpp`, `UTXOManager.cpp`, `GeniusNode.cpp`, plus both modified test TUs).

## Skipped Issues

None — all four in-scope findings were fixed.

## Out-of-scope notes

- The 5 Info findings (IN-01..IN-05) were not touched (fix_scope = critical_warning).
- CR-01's commit status is "fixed: requires human verification" in this report per the fix protocol for logic-error-class findings.

---

_Fixed: 2026-10-21T23:45:40Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
