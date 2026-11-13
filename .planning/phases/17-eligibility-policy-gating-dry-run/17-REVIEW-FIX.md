---
phase: 17-eligibility-policy-gating-dry-run
fixed_at: 2026-10-19T18:29:52Z
review_path: .planning/phases/17-eligibility-policy-gating-dry-run/17-REVIEW.md
iteration: 1
findings_in_scope: 6
fixed: 6
skipped: 0
status: all_fixed
---

# Phase 17: Code Review Fix Report

**Fixed at:** 2026-10-19T18:29:52Z
**Source review:** .planning/phases/17-eligibility-policy-gating-dry-run/17-REVIEW.md
**Iteration:** 1
**Branch:** gsd/phase-17-eligibility-policy-gating-dry-run (advanced a96096f83 → 1056b2924, 8 commits)

**Summary:**
- Findings in scope: 6 (CR-01, WR-01..WR-05; the 4 Info findings were out of scope and are marked Deferred in 17-REVIEW.md)
- Fixed: 6
- Skipped: 0
- Fix commits: eb1a23adf, c93ed120f, 120fffba2, fe68cba4b, 79f95961b, 10f6371a2 (+ e8166799b style, 1056b2924 docs)

## Fixed Issues

### CR-01: Dry-run cycle reads replaceable members without the lifecycle lock

**Status:** fixed: requires human verification (concurrency logic; no unit seam)
**Files modified:** `src/account/GeniusNode.cpp`, `src/account/GeniusNode.hpp`
**Commit:** eb1a23adf
**Applied fix:** `RunPruneDryRunCycle()` now takes a consistent
`SnapshotAccountServices()` snapshot and builds the evaluator from
`*snapshot.manager` / `snapshot.account->GetUTXOManager()`; the snapshot
shared_ptrs are held for the whole cycle so the account (and its
UTXOManager, which the evaluator references by reference) outlive the
evaluation even if a generation switch reassigns the members mid-cycle.
The skip-and-reschedule guard semantics are preserved
(`!tx_globaldb_ || !snapshot.account || !snapshot.manager` → debug log +
reschedule). `tx_globaldb_` is deliberately read without the lifecycle
lock, with the init-once (`InitDatabase()`, before the timer exists) /
reset-only-after-io-drain (`ReleaseRuntimeMembersAfterIoStopped()`)
invariant documented at the site. Header contract documentation extended.

### WR-01: Unchecked `.value()` on `fromReadableString(GetHash())` at four call sites

**Status:** fixed: requires human verification (error-path logic)
**Files modified:** `src/transaction/TransactionManager.cpp`
**Commit:** c93ed120f (format follow-up e8166799b)
**Applied fix:** All four sites (ParseTransferTransaction,
ParseMintTransaction migration branch, ParseMintTransaction mint-v2 branch,
ParseEscrowTransaction) now check the
`base::Hash256::fromReadableString(...)` result; on error they log
"\<Type\> \<hash\> has malformed data_hash" and return
`outcome::failure(...)` — never throwing through the CRDT/consensus
processing stack. The mint-v2 branch merges the previously duplicated
hash parse at :2679/:2715 into one guarded parse.
**Deliberately left (pre-existing, out of phase-17 scope):** unchecked
`.value()` at :1293 (PayEscrow), :2748 (legacy mint in
ParseMintTransaction), :2952 (RevertMintTransaction) — flagged for a
follow-up phase. The reviewer's suggested `HashFromTxHash` helper was not
introduced: with only the four new sites converted there is no second use
yet (DRY — no helper beside the remaining three pre-existing copies).

### WR-02: Transient evaluation error permanently kills the dry-run timer

**Status:** fixed: requires human verification (timer logic; no unit seam)
**Files modified:** `src/account/GeniusNode.cpp`
**Commit:** 120fffba2
**Applied fix:** Only the permanent policy error
(`std::errc::invalid_argument` — the fail-closed no-window contract) logs
ERROR and stops the cycle. Every other evaluation error (e.g.
`operation_canceled` when `ActiveCRDTDataStore()` is momentarily null
across a GlobalDB restart/generation switch) logs WARN and falls through
to the reschedule block, keeping the 10-minute cadence alive. The
construction-failure path correctly still stops (a config that cannot
construct the evaluator will not heal by retrying).

### WR-03: `finality_window: 0` accepted while `enabled: true`

**Status:** fixed (regression-tested)
**Files modified:** `src/transaction/PruneEligibility.cpp`,
`src/transaction/PruneEligibility.hpp`,
`test/src/account/prune_eligibility_test.cpp`
**Commit:** fe68cba4b
**Applied fix:** `PruningConfig::FromJSON` rejects zero at READ time
(`GetUint64() > 0` guard) — a degenerate window is treated exactly like an
absent one, so the existing enabled-without-window fail-close covers
`{ "enabled": true, "finality_window": 0 }`. `kWindowRequiredMessage`
extended to "pruning.enabled requires pruning.finality_window > 0; pruning
disabled (fail closed)". Header contract updated.
**Tests:** `PruningConfig_EnabledWithZeroWindowFailsClosed` (enabled+0 →
disabled, window unset, reason names finality_window),
`PruningConfig_ZeroWindowWhileDisabledStaysUnset` (0 on a disabled request
is simply ignored, no fail-close — pins the rule's scope).

### WR-04: Pruned-marker lookup trusts `record.tx_path()` with no consistency check

**Status:** fixed (regression-tested)
**Files modified:** `src/transaction/PruneEligibility.cpp`,
`src/transaction/PruneEligibility.hpp`,
`test/src/account/prune_eligibility_test.cpp`
**Commit:** 79f95961b
**Applied fix:** `LookupPrunedEntry` validates the decoded record before
any query: the record's `tx_path` must be non-empty AND end with the
requested `tx_hash` (the hash is the path's terminal segment). Mismatch or
empty payload → `outcome::failure(std::errc::bad_message)` with an ERROR
log — never a silent PRUNED (which would grant
SPENDER_PRUNED_WINDOW_SATISFIED off a corrupt marker) and never a
whole-store `QueryKeyValues("")` prefix scan. Header contract updated.
**Tests:** `PruneMarker_MismatchedPathIsError` (well-formed marker naming
ANOTHER tx's path → error), `PruneMarker_EmptyPayloadIsError` (default
constructed record → error).

### WR-05: Eligible candidate with unresolved CID silently skips the block gate

**Status:** fixed: requires human verification (no unit seam — see note)
**Files modified:** `src/transaction/PruneEligibility.cpp`,
`src/transaction/PruneEligibility.hpp`
**Commit:** 10f6371a2
**Applied fix:** The block-gate loop no longer silently `continue`s past an
eligible candidate whose `candidate.cid` is nullopt: it emits an explicit
`BlockGateResult` with the sentinel cid `<unresolved for <tx path>>`,
`erasable=false`, reason `CID_UNRESOLVED` (new PruneReason enum value,
added with Doxygen before SPENDER_PRUNED_WINDOW_SATISFIED), an ERROR log,
and pushes it into `block_reports`. Nothing disappears from the report the
Phase-18 consumer consults. `BlockGateResult::cid` contract documented.
**Test note:** a regression test was attempted but the exact production
fail state is unconstructible through the test fixture:
`GetTransactionCID` resolves via identity (`ResolveTransactionPathAndCID`
reconstructs the same path the enumeration read, and B4 empirically proves
erase removes keys from both the enumeration and resolution surfaces
symmetrically), so an ENUMERATED candidate can never fail CID resolution
in the fixture. The attempted test failed on its own wrong assumption and
was removed; the fix is source-audit + compile-gate verified like
CR-01/WR-01/WR-02.

## Skipped Issues

None — all six in-scope findings were fixed.

## Test and Tooling Evidence

- **Build:** `gmake -j8 prune_eligibility_test utxo_manager_test
  transaction_manager_certificate_fallback_test` — exit 0 on the final
  committed tree.
- **prune_eligibility_test:** 22/22 PASSED (18 pre-existing + 4 new
  regression tests; the 5th attempted test removed, see WR-05).
- **utxo_manager_test:** 34/34 PASSED (WR-01's file).
- **transaction_manager_certificate_fallback_test:** 28/28 PASSED
  (WR-01's file).
- **clang-format gate (llvm@22, homebrew):** `git-clang-format --diff` over
  all session-changed lines vs base a96096f83 — clean ("clang-format did
  not modify any files" after applying; deviations found and fixed in
  PruneEligibility.cpp and TransactionManager.cpp; the latter landed as
  style commit e8166799b per the no-history-rewriting rule).
- **clang-tidy gate (llvm@22):** run over the four changed TUs
  (PruneEligibility.cpp, TransactionManager.cpp, GeniusNode.cpp,
  prune_eligibility_test.cpp) on a single-arch (x86_64) filtered
  compile_commands.json — zero findings in any changed TU (only
  thirdparty header noise and one pre-existing config-format quirk).
- **Suite runs are per-finding-gate scoped, not a full test suite** (full
  verification remains the verifier phase's job).

## Notes for the Verifier / Human Review

- CR-01, WR-01, WR-02, WR-05 are logic/concurrency fixes with no unit
  seam — they carry "requires human verification" flags and passed the
  source audit, compile gate, and the three affected suites.
- WR-03 and WR-04 are test-backed.
- All work ran in an isolated worktree on temp branch
  gsd-reviewfix/17-9256 and was fast-forwarded onto the user's branch in
  the transactional cleanup; the worktree and temp branch are gone.

---

_Fixed: 2026-10-19T18:29:52Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
