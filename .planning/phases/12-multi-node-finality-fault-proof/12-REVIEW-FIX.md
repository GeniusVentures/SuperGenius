---
phase: 12-multi-node-finality-fault-proof
fixed_at: 2026-09-01T18:20:00Z
review_path: .planning/phases/12-multi-node-finality-fault-proof/12-REVIEW.md
iteration: 1
findings_in_scope: 9
fixed: 9
skipped: 0
status: all_fixed
---

# Phase 12: Code Review Fix Report

**Fixed at:** 2026-09-01T18:20:00Z
**Source review:** .planning/phases/12-multi-node-finality-fault-proof/12-REVIEW.md
**Iteration:** 1
**Scope:** critical_warning (CR-01, CR-02, WR-01..WR-07; IN-* findings skipped per scope)

**Summary:**
- Findings in scope: 9
- Fixed: 9
- Skipped: 0

## Fixed Issues

### CR-01: `CreateProposalState` mutates shared consensus maps without `proposals_mutex_`

**Status:** fixed
**Files modified:** `src/blockchain/Consensus.cpp`
**Commit:** 2b1a8e47
**Applied fix:** Wrapped the `proposals_.emplace(...)` and `slot_states_[...]` mutations in a
`std::lock_guard lock( proposals_mutex_ )` scope, exactly as the review suggested. Verified the
only caller (`HandleCertificate`, pubsub receive thread) holds no lock beforehand, so the new
non-recursive lock cannot deadlock; `FetchProposalState` and `ValidateCertificateBestProposal`
lock/unlock around it, never nested.

### CR-02: Unchecked `end()` dereference of `tx_processed_m.find()` on conflicting-transaction paths

**Status:** fixed: requires human verification (new fallback semantics on a previously-UB path)
**Files modified:** `src/account/TransactionManager.cpp`
**Commit:** a3cd3f38
**Applied fix:** Both sites now check for `end()` and fall back to a value scan by conflicting tx
hash (the review's suggested fallback), since the tracked key namespace depends on the network the
entry was recorded under. Site 1 (`ProcessIncomingTransaction` conflict path): if the entry is
truly absent, it is treated as not-CONFIRMED and the existing `ChangeTransactionState(VERIFYING)`
fall-through applies. Site 2 (`OnConsensusCertificate`): if the entry is truly absent, unlocks and
returns `Check::Approve` ("nothing local to arbitrate") per the review's suggestion. Added
`#include <algorithm>` for `std::find_if`.

### WR-01: `CheckTransactionValidity` promotes a copy, never the tracked entry

**Status:** fixed
**Files modified:** `src/account/TransactionManager.cpp`
**Commit:** 897d71ed
**Applied fix:** Changed `for ( auto [key, tracked] : tx_processed_m )` to
`for ( auto &[key, tracked] : ... )` so `tracked.status = CONFIRMED` mutates the tracked entry
under the already-held `std::unique_lock<std::shared_mutex>`.

### WR-02: `QueryTransactions` tests the wrong variable — error branch is dead code

**Status:** fixed
**Files modified:** `src/account/TransactionManager.cpp`
**Commit:** b0de5fa0
**Applied fix:** The second `if ( !transaction_key.has_value() )` (always false after the guarded
`continue`) now tests `process_result.has_error()` and logs the error message, so
`FetchAndProcessTransaction` failures are no longer silently dropped.

### WR-03: SIZE-01 pre-publish size gate runs after the CRDT commit and mid-proposal loop

**Status:** fixed: requires human verification (side-effect ordering change)
**Files modified:** `src/account/TransactionManager.cpp`
**Commit:** 3ba8ad79
**Applied fix:** Added a pre-publish validation pass at the top of `SendTransactionItem` — embedded
size gate plus UTXO commitment/witness buildability for the whole batch — before any `Put`/`Commit`
or proposal side effect, so a batch can never be partially published. The builders are const/pure
and safe to call twice; the post-commit gate is kept as documented defense-in-depth (it can no
longer trigger for a pre-validated batch).

### WR-04: `FilterProof` verification short-circuited — all incoming proofs accepted

**Status:** fixed (behavior-preserving option; residual recorded below)
**Files modified:** `src/account/TransactionManager.cpp`, `src/account/TransactionManager.hpp`
**Commit:** 422d8b73
**Applied fix:** Per the conservative-fix directive, ingress behavior is unchanged (all proofs
accepted). The unreachable `valid_proof = true; break;` short-circuit, the dead
`IBasicProof::VerifyFullProof` block, and the dead tombstoning branch were deleted rather than
shipped as dead code, with a TODO documenting what re-enabling verification requires. Header doc
updated to match reality ("do not ship both" option from the review).

### WR-05: `active_vote_announcements_for_test_` grows unbounded, unlocked, in the production vote loop

**Status:** fixed
**Files modified:** `src/blockchain/Consensus.cpp`, `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
**Commit:** 0fb45d5b
**Applied fix:** The capture is now bounded (at 1024 entries the oldest half is dropped) and
guarded by `fault_test_mutex_`; the friend test accessors `ActiveVoteAnnouncements` (now returns a
copy under the lock) and `ClearActiveVoteAnnouncements` synchronize on the same mutex, removing the
data race with the round-timer thread. Production behavior is otherwise unchanged.

### WR-06: Migration branch of `ParseMintTransaction` ignores `PutUTXO`/`ConsumeUTXOs` results

**Status:** fixed
**Files modified:** `src/account/TransactionManager.cpp`
**Commit:** 2bc5455e
**Applied fix:** Both calls in the `MigrationTransaction` branch are wrapped with
`BOOST_OUTCOME_TRY`, matching the `MintTransactionV2` branch, so storage failures during migration
application propagate instead of leaving partial UTXO state.

### WR-07: mint-v2 CONFIRMED retry re-applies parse effects; idempotency relies on an ignored bool

**Status:** fixed: requires human verification (state-machine change on retry path)
**Files modified:** `src/account/TransactionManager.hpp`, `src/account/TransactionManager.cpp`
**Commit:** caf34458
**Applied fix:** Added `bool effects_applied` to `TrackedTx` (defaults false; existing aggregate
initializations unaffected). In the mint-v2 `CONFIRMED` branch, a retry after a failed
`PersistBridgeExecutedMarker` now observes `effects_applied == true` and skips re-parsing, so
`mint_effects_for_test_` stays exactly 1 and the already-CONSUMED burn outpoint is not re-consumed.
The flag is set only AFTER `ParseTransaction` succeeds, so a transient parse failure remains
retryable. In `ParseMintTransaction` (mint-v2 branch) the `ConsumeUTXOs` result bool is now
inspected and warns on partial consumption instead of being discarded. The Phase 12 invariants
(one canonical slot, one authoritative certificate, exactly one Mint effect, persist-before-
advertise, exact-once recovery) are preserved and exercised by the multi-node fault test.

## Skipped Issues

None — all in-scope findings were fixed.

## Verification

- Per-fix Tier 1 (re-read of each edited region) performed; C++ has no per-file syntax gate, so
  Tier 3 fallback applied per finding, followed by the orchestrator-mandated build/test gate.
- Full incremental build of all affected targets via
  `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test consensus_slot_key_test
  transaction_manager_certificate_fallback_test utxo_manager_test multi_node_finality_fault_test -j8`:
  clean.
- Focused suites on the final fixed state:
  `ctest -R '^(consensus_pending_lifecycle_test|consensus_slot_key_test|transaction_manager_certificate_fallback_test|utxo_manager_test)$' --output-on-failure --timeout 300`
  — 4/4 passed (ran twice, both clean).
- Full `multi_node_finality_fault_test`: passed on the final fixed state (exit 0). During the
  session it failed intermittently (rotating test cases) under back-to-back full-suite runs.
  An A/B against the pre-fix baseline (sources at 88f0d7df rebuilt into the same build dir) showed
  the baseline also fails intermittently (e.g. `SameBurnContentionUsesOneCanonicalSlotAndExactMint`
  failed on baseline code), while the fixed code passes isolation runs and the final full run.
  Conclusion: pre-existing environment/load sensitivity of the suite, not a regression from these
  fixes; the verifier phase should re-run it on a quiet machine for the final call.

## Residuals / Notes for Humans

1. **WR-04 (protocol decision):** Proof verification on the CRDT proof-filter ingress path remains
   disabled by design — all incoming proofs are accepted. Enabling `IBasicProof::VerifyFullProof`
   enforcement would change protocol behavior materially (possible tombstoning of currently
   accepted proofs) and requires the reputation/penalty path; it was deliberately not enabled per
   the conservative-fix directive.
2. **WR-07 (cross-restart semantics):** `effects_applied` is in-process state. Across a process
   restart, exact-once mint application is still guaranteed by the persisted bridge-executed
   marker boundary, not by the flag; the flag only removes the within-process re-parse on
   marker-write retry that the finding identified. `ConsumeUTXOs` partial consumption now warns
   instead of silently proceeding; the amount-0 outpoint rebuild inside `UTXOManager::ConsumeUTXOs`
   for genuinely missing outpoints is unchanged (behavior preserved).
3. **WR-03:** Post-commit UTXO commitment/witness build failures in the proposal loop remain
   theoretically possible (they are pre-validated, so practically unreachable); the post-commit
   gate was intentionally kept as defense-in-depth.
4. **WR-05:** The observation buffer holds at most 1024 entries and drops the oldest half when
   full; any future test expecting more than 512 announcements between clears would observe
   truncation (no current test does).
5. IN-01..IN-08 were out of scope (`fix_scope: critical_warning`) and are untouched.

---

_Fixed: 2026-09-01T18:20:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
