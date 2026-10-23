---
phase: 19-pruned-node-survival-recovery-hardening
plan: 04
subsystem: transaction
tags: [gate-05, checkpoint-rebuild, preserve-clear, own-replay, spend-evidence, pruned-marker, outcome-result]

# Dependency graph
requires:
  - phase: 19-pruned-node-survival-recovery-hardening (19-01)
  provides: UTXOManager::ClearUnspentKeepConsumed — the consumed-preserving clear this plan's mismatch branch swaps in
provides:
  - GATE-05's checkpoint-rebuild half closed: mismatch clear preserves CONSUMED evidence, the rebuild never resurrects a spent output (construction + durable-record belt), the spender's own replay is recognized (no false double-spend reject), and locally-pruned creating txs are skipped with zero recovery requests
  - TransactionManager::CrosscheckRebuiltUtxos + UTXOManager::ReadDurableSpendEvidence (the durable-record consult)
  - The RebuildGuard suite family (5 cases + negative control, double-run green)
affects: [19-06 (restart matrix rides the preserve-clear + marker consult on the real StartCore boot), GATE-05 (complete), SURV-01]

# Tech tracking
tech-stack:
  added: [] # zero new dependencies
  patterns: [durable-record belt consult (a READY registry entry can never carry its spender — only the persisted /utxo/ record can name one), own-replay discriminator keyed on spender-of-record == replaying tx hash over consumed inputs, evidence-first PRUNED skip at the missing-set insert]
key-files:
  created: []
  modified:
    - src/transaction/TransactionManager.cpp
    - src/transaction/TransactionManager.hpp
    - src/account/UTXOManager.cpp
    - src/account/UTXOManager.hpp
    - test/src/account/tx_pruner_test.cpp

key-decisions:
  - "The crosscheck consults the DURABLE /utxo/ record, not the in-memory registry: PutUTXO initializes spent_by_txid to nullopt and RestoreConsumedUTXOs resets it, so the plan's map-based GetOutPointSpender consult on a READY outpoint is structurally vacuous (no reachable state fires it); a new owner-vended UTXOManager::ReadDurableSpendEvidence reads one record so registry/disk divergence (the threat model's one-corrupted-record class) is observable"
  - "Mismatch induction is the plan-sanctioned divergent-checkpoint class, not tamper-free advance-past-checkpoint: consensus approval checkpoints are POST-apply (every certified spend re-converges the latest-checkpoint pointer to local state), and a db restart re-delivers the cert topic which re-checkpoints again — the deterministic arm deletes the spender's change record (input CONSUMED evidence + checkpoint rooted over the change survive) and boots a fresh TM over the LIVE db (no restart, no redelivery)"
  - "Own-replay proceeds through ConsumeUTXOs's already-consumed branch, which rewrites the genuine entry as an amount-0 tombstone with the same spender — evidence degrades genuine->tombstone on re-consume but state (CONSUMED), spender binding, and re-creation blocking all survive; the NoResurrection case (spender pruned) keeps the genuine entry intact"
  - "The foreign-spender arm (T-19-0402) is driven through ChangeTransactionState(CONFIRMED) — the exact production parse path — because a certified conflicting spend cannot be seeded (the approval path demotes the conflict before a certificate exists)"

patterns-established:
  - "Durable-evidence belt: enumerate rebuilt-READY outpoints and consult the persisted record through the owner's accessor; WARN with the genuine/tombstone distinction (IsOutPointGenuinelyConsumed semantics applied to the record) and revert through ConsumeUTXOs with the recorded spender so the evidence is restored, not destroyed (DeleteUTXO would wipe it)"
  - "Deterministic mismatch arming on a cert-driven stack: fresh TransactionManager over the live db (fresh tracked map, durable records untouched) instead of a db reopen whose topic redelivery re-converges the checkpoint pointer"

requirements-completed: [GATE-05]

# Metrics
duration: 53min
completed: 2026-10-23
---

# Phase 19 Plan 04: Checkpoint-Rebuild Guard (GATE-05) Summary

**The checkpoint-mismatch clear now preserves CONSUMED spend evidence (ClearUnspentKeepConsumed swap), the rebuild recognizes the spender's OWN replay over preserved evidence (GetOutPointSpender == own hash per consumed input) so legitimate change outputs survive, every rebuilt-READY outpoint is belt-checked against its durable /utxo/ record (the belt's WARN+revert branch proven firing via the mandatory tampered-record induction), and locally-pruned creating txs skip recovery with zero requests — GATE-05's rebuild half closes by construction, never by refusing to boot.**

## Performance

- **Duration:** 53 min (19:53-20:46 UTC)
- **Tasks:** 3
- **Files modified:** 5

## Accomplishments
- InitializeUTXOs mismatch branch swaps `SetUTXOs({}, address)` for 19-01's `ClearUnspentKeepConsumed(address)`; WARN names the preserved evidence; zero SetUTXOs calls remain in the function (grep-gated)
- The missing-set insert consults `PruneEligibility::LookupPrunedEntry` BEFORE touching `missing_tx_hashes_` (mutex taken only on the insert path): PRUNED => INFO skip ("creating tx pruned locally ... not relevant"); NOT_FOUND / VALUE_PRESENT_INVARIANT_VIOLATION / lookup error keep the 18-landed bounded recovery verbatim — never guess
- ParseTransferTransaction's double-spend branch gains the own-replay discriminator: when consumed inputs exist and EVERY consumed input's spender-of-record equals this tx's hash, the replay proceeds to re-apply consume+produce; the outputs-present heuristic stays first for the normal path; any other spender-of-record still rejects with the existing ERROR (the INFO "Recognized own rebuild replay" line fires in the OwnReplay test)
- `CrosscheckRebuiltUtxos` runs after a mismatch rebuild only (fresh-account boots skip it): every rebuilt-READY outpoint of the account is checked against its durable /utxo/ record through the new `UTXOManager::ReadDurableSpendEvidence`; a consumed record WARNs (genuine/tombstone distinction) and reverts the outpoint via ConsumeUTXOs with the recorded spender; count returned and logged; errors logged and skipped, never swallowed
- RebuildGuard family (5 tests, double-run green; full tx_pruner_test 27/27; lifecycle backstops `*MissingCreatingTx*:*ConflictingSpend*` green; full lifecycle binary 22/22; utxo_manager_test 37/37 after the accessor addition)
- Mismatch/own-replay/belt scenarios seed through the REAL stack: certified producer + certified spender (genesis registry + self-signed certificate recipe from the pending-lifecycle binary), effects applied by certificate delivery, bounded waits for the applied state

## Task Commits

Each task was committed atomically:

1. **Task 1: mismatch-branch preserve-clear swap + PRUNED marker consult** - `519b60f81` (feat)
2. **Task 2: own-replay recognition + durable-evidence rebuild crosscheck** - `339030135` (feat)
3. **Task 3: RebuildGuard suite family** - `f3abcc4b7` (test)

## Files Created/Modified
- `src/transaction/TransactionManager.cpp` — the four coordinated edits (clear swap + WARN, marker consult, own-replay discriminator, crosscheck + mismatch_rebuild wiring)
- `src/transaction/TransactionManager.hpp` — private `CrosscheckRebuiltUtxos` declaration with the D-01 doc
- `src/account/UTXOManager.cpp/.hpp` — `DurableSpendEvidence` struct + `ReadDurableSpendEvidence` (exact-key record read, NOT_FOUND-or-empty => nullopt, malformed spent_by_txid => error)
- `test/src/account/tx_pruner_test.cpp` — +473 lines: certificate seeding recipe, MakeSpender, RebuildArm + mismatch arming, raw-record helpers, the per-binary access class extended (RunInitializeUTXOs / RunCrosscheckRebuiltUtxos / ChangeTransactionState), 5 RebuildGuard tests

## Decisions Made
- Belt consult is disk-based through an owner-vended accessor (see key-decisions): the plan's letter (map-based GetOutPointSpender) cannot fire in ANY reachable state — verified exhaustively against PutUTXO/ConsumeUTXOs/RestoreConsumedUTXOs/StoreUTXOSnapshot semantics before redesigning
- The belt's reversion is ConsumeUTXOs-with-the-recorded-spender (restores evidence), not DeleteUTXO (would destroy the durable record the belt just found)
- Marker consult takes no lock on the skip path; the insert path keeps the existing scoped lock_guard discipline
- GATE-05 marked complete (boot half 18-landed with regression; rebuild half + marker consult proven here); SURV-01 stays open for 19-06's restart matrix

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] The plan's crosscheck consult is structurally vacuous**
- **Found during:** Task 2 design (verified against source before writing)
- **Issue:** "for every gap-filled READY outpoint consult GetOutPointSpender" can never fire: PutUTXO initializes `spent_by_txid` to nullopt on every creation, ConsumeUTXOs sets it only on the READY/RESERVED->CONSUMED transition, RestoreConsumedUTXOs resets it, and StoreUTXOsLocked's prefix-replace keeps disk==memory — a READY registry entry NEVER carries a spender, in any reachable or tamperable state. The checker's MANDATORY belt induction would have been untestable as written
- **Fix:** The crosscheck consults the DURABLE /utxo/ record through a new owner-vended `UTXOManager::ReadDurableSpendEvidence` (the threat register's own boundary: "one corrupted /utxo/ record must not re-authorize a spent output"); reads stay through UTXOManager accessors (no raw map/store access from TM). The belt test tampers ONE record to CONSUMED-with-spender and proves the real WARN+revert branch fires
- **Files modified:** src/account/UTXOManager.cpp, src/account/UTXOManager.hpp (beyond the plan's files_modified), src/transaction/TransactionManager.cpp
- **Verification:** CrosscheckBelt green twice (reverted count 1, outpoint CONSUMED, spender binding restored); normal-flow rebuilds log the belt's find-nothing debug line
- **Committed in:** `339030135`

**2. [Rule 3 - Blocking] PrunedLookup namespace qualification (the 19-03 lesson, hit again)**
- **Found during:** Task 1 first build
- **Issue:** `PruneEligibility::PrunedLookup` does not exist — the enum is namespace-level in `sgns` (PruneEligibility.hpp:115); only LookupPrunedEntry is a class member
- **Fix:** `PrunedLookup::PRUNED` / `PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION` unqualified
- **Files modified:** src/transaction/TransactionManager.cpp
- **Verification:** clean build
- **Committed in:** `519b60f81`

**3. [Rule 3 - Blocking] The plan's mismatch induction cannot fire through certificate-driven spends**
- **Found during:** Task 3 first runs
- **Issue:** (a) every certified approval writes a POST-apply checkpoint (TransactionConsensusHandler.cpp:297), so cert-driven spends re-converge the latest-checkpoint pointer to local state — "advance local state past the checkpoint" never diverges; (b) `ReopenStackOnSamePath` restarts the db, whose cert-topic redelivery re-applies AND re-checkpoints again (also legitimately re-creating the pruned spender's change through the never-pruned certificate — the 19-03 fallback), and (c) the redelivered certificates initially masked the mismatch entirely (case 2 passed vacuously until the "Recognized own rebuild replay" line was checked)
- **Fix:** The plan-sanctioned divergent-checkpoint induction: after the certified arm applies, DELETE the spender's change record (the corrupted/lost-record class) — input CONSUMED evidence and the checkpoint rooted over the change survive, so local root != checkpoint root deterministically — and boot a FRESH TransactionManager over the LIVE db (fresh tracked map, no restart, no redelivery; InitializeUTXOs reloads the index-resident shape from disk). The real-boot db-restart leg stays 19-06's
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Verification:** the mismatch WARN, the own-replay INFO, and the belt WARN all fire in their cases; family double-run green
- **Committed in:** `f3abcc4b7`

**4. [Rule 3 - Process] Task 1's first commit swept whole-file clang-format drift**
- **Found during:** Task 1 commit (146/116 diff for two edits)
- **Issue:** running `clang-format -i` on the whole file normalized ~30 pre-existing drift hunks into the task commit (scope pollution; 19-03 left such drift out of scope)
- **Fix:** redid the commit using `git clang-format` (changed-lines-only); Tasks 2-3 staged-then-formatted with a worktree re-stage (git-clang-format formats the worktree while the index holds the pre-format blob); final state: `git-clang-format --diff HEAD` clean, diffs scoped
- **Files modified:** none beyond the task files
- **Verification:** scoped diffs (49/7, 243/5, 473/1); format gate clean
- **Committed in:** `519b60f81` (redone), `339030135`, `f3abcc4b7`

---

**Total deviations:** 4 auto-fixed (3 Rule 3 blocking, 1 Rule 3 process)
**Impact on plan:** All must_haves truths proven. The two harness deviations changed HOW the tests arm the code, not WHAT they prove; the belt redesign (deviation 1) was forced by the plan's own non-vacuity requirement.

## Issues Encountered
- clang-tidy on new lines: `misc-const-correctness`/`modernize-use-scoped-lock` on lock_guards (the documented file-wide established idiom, identical class on pre-existing siblings), narrowing + const_cast in the fromSpan decode (byte-identical to LoadUTXOs's own idiom), `bugprone-unchecked-optional-access` on the crosscheck's guarded `evidence.value()` (has_error->continue guard precedes it; the same analyzer false-positive fires on the pre-existing checkpoint_result.value() line), cognitive-complexity 53 on InitializeUTXOs (the marker consult's nesting; TickOnce/SendTransactionItem already exceed the threshold per 19-02). No Required-correctness findings
- Harness discoveries documented in tests: live-consumed entries are orphaned from the address index (the 19-01 finding) — case 3 re-indexes via LoadUTXOs before DeleteUTXO; a topic-head delta refuses erase (GATE-03) — case 4a advances the head with a plain seed Put first; the fixture-held ValidatorRegistry keeps pre-reopen db handles alive (RocksDB lock) — irrelevant in the final TM-only shape but noted for any future reopen-with-registry test
- `db->query()` returns a map (not a vector) and `Hash256::fromSpan` needs the mutable-span idiom — matched the in-file precedent on first fix

## Threat Mitigations (per plan threat_model)
- **T-19-0401** (resurrect via mismatch/corrupted record): preserved CONSUMED evidence + PutUTXO duplicate check by construction (NoResurrection: outpoint stays CONSUMED, raw spent_by_txid disk proof) + the durable-record belt (CrosscheckBelt: tampered record detected, outpoint reverted, count 1) — DONE
- **T-19-0402** (bypass via false own-replay): the discriminator requires spender-of-record == replaying tx hash for EVERY consumed input; the foreign arm still rejects (ChangeTransactionState(CONFIRMED) errors, output never credited) — DONE
- **T-19-0403** (unbounded re-request for pruned refs): marker consult => HasMissingTxHashes false for the pruned arm; the unmarked control keeps the bounded insert — DONE
- **T-19-0404** (vacuous crosscheck): the belt consult reads the record the clear preserved (disk-level assertion in case 1 proves the evidence path); the MANDATORY induction fires the belt branch for real — DONE
- **T-19-0405** (repudiation): every skip WARNs/INFOs naming the pruned ref; the belt WARN names outpoint, spender, and genuine-vs-tombstone — DONE
- **T-19-04SC** (supply chain): zero registry installs — DONE

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 19-06's restart matrix boots the REAL StartCore on a pruned node knowing: the mismatch clear preserves evidence, the marker consult zeroes doomed requests, the own-replay fix keeps legit spenders' replays green, and RetryDueFailedRoots (19-03) keeps the fetch counter still for pruned roots
- SURV-02/19-05 surface unchanged by this plan (tri-state lookup is 19-05's)
- The `ReadDurableSpendEvidence` accessor is available for any future disk-vs-registry consistency check

## Self-Check: PASSED

All 5 modified files exist; all 3 task commits (519b60f81, 339030135, f3abcc4b7) present in git log; `ClearUnspentKeepConsumed` called from exactly the mismatch branch (grep: 1 call site); zero SetUTXOs calls in InitializeUTXOs (comment-only mention); `LookupPrunedEntry` reused with no hand-built marker keys; test artifact 2370 lines (min 120 satisfied); family + full binary green double-run.

---
*Phase: 19-pruned-node-survival-recovery-hardening*
*Completed: 2026-10-23*
