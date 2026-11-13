---
phase: 18-txpruner-service-first-real-prune
plan: 03
subsystem: crdt
tags: [d-01, compose-lock, wr-02, prune-marker, batch-join, pruning-config, missing-block-evidence, c++17]

# Dependency graph
requires:
  - phase: 16-erasure-primitives-bookkeeping-hardening
    provides: GlobalDB::EraseEntry guarded compose, StageEraseElement/StageBlockRemoval, seal retention (GATE-03)
  - phase: 18-txpruner-service-first-real-prune (18-01, 18-02)
    provides: wave-1 landed shapes (D-02 guard, marker contract context)
provides:
  - Owner-vended compose critical section (CrdtSet::MakeComposeLock) — WR-02 closed by construction for every EraseEntry caller
  - EraseEntry staged-extra-entry parameter — the /prune/tx/<hash> marker joins the erase WriteBatch (value XOR marker, one commit)
  - PruneEligibility::BuildPruneMarkerEntry — the production marker buffers (one key home, one record shape)
  - PruningConfig max_erases_per_cycle (100) / settle_seconds (60) — degenerate zeros rejected at read time (WR-03 rule)
  - BLOCK_ALREADY_ERASED gate evidence — the absent-block stranding edge completes instead of stranding; corrupt blocks still fail closed
  - GlobalDB::HasLocalBlock / GraphsyncDAGSyncer::HasRawBlockBytes — byte-level block presence (the absent-vs-corrupt discriminator)
  - TransactionManager::HasMissingTxHashes (tick-gate input) + EraseTransactionEntryPruned (the production pruned-erase driver, one resolution home)
affects: [18-04 TxPruner service (composes all of these), 18-06 ComposeLock suite, PRUNE-04]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Owner-vended lock: the mutex owner returns a scoped guard (unique_lock on the private recursive mutex_) instead of exposing the mutex — the API shape makes the critical section impossible to forget and closes WR-02 for every caller"
    - "Byte-presence discrimination: parse success (GetLocalDeltaKeys/HasBlock) cannot tell absent from corrupt; RocksdbDatastore::contains on the CID::toBytes() key can — the discriminator lives beside StageBlockRemoval (the one block-key home)"
    - "Affirmative-evidence reason family: BLOCK_ALREADY_ERASED joins SPENDER_PRUNED_WINDOW_SATISFIED as non-failure reasons (HasFailureReason excludes both) — verdicts are explicit, never silent passes and never fail-closed strandings"

key-files:
  created: []
  modified:
    - src/crdt/crdt_set.hpp
    - src/crdt/impl/crdt_set.cpp
    - src/crdt/crdt_datastore.hpp
    - src/crdt/impl/crdt_datastore.cpp
    - src/crdt/globaldb/globaldb.hpp
    - src/crdt/globaldb/globaldb.cpp
    - src/crdt/graphsync_dagsyncer.hpp
    - src/crdt/impl/graphsync_dagsyncer.cpp
    - src/transaction/PruneEligibility.hpp
    - src/transaction/PruneEligibility.cpp
    - src/transaction/TransactionManager.hpp
    - src/transaction/TransactionManager.cpp
    - test/src/account/prune_eligibility_test.cpp

key-decisions:
  - "The compose lock is vended as a returned unique_lock (not an exposed mutex, not a locking EraseEntry overload): a guard object IS the critical-section API; the raw mutex_ stays private (D-01/T-18-0305)"
  - "The absent-block discriminator is BYTE presence (RocksdbDatastore::contains via the new GraphsyncDAGSyncer::HasRawBlockBytes), not HasBlock — HasBlock re-parses, so a corrupt block would read as absent and the discrimination (G2) would be unsound"
  - "BLOCK_ALREADY_ERASED fires only when the absent CID is named by an otherwise-ELIGIBLE candidate (the gate's input contract made explicit at the API level) — an absent block tied to a non-eligible candidate stays fail-closed (G3)"
  - "Degenerate config zeros (K=0, settle=0) are rejected at read time by keeping the safe defaults (100/60) — unlike finality_window there is no absent-value fail-close because the defaults are safe; the window's enabled-without-window fail-close is untouched"
  - "The marker record carries the tx path named by the hash (the WR-04 shape); the optional cid/height record fields stay unset in the production pair — the plan's one-argument BuildPruneMarkerEntry shape"
  - "The two erase drivers share ONE private helper (EraseTransactionEntryWithMarker(tx_hash, extra_entry)); the plain variant passes nullopt and is byte-identical, the pruned variant stages the marker pair"

patterns-established:
  - "Seam-interrupted staged-extra-entry proof: the F3 matrix (steps 1/2/3) proves value AND marker both stay in the pre-erase state — the value-XOR-marker invariant is never observable mid-flight"

requirements-completed: []  # PRUNE-04 completes with the TxPruner cycle (18-04); this plan lands the mutation substrate it composes

# Metrics
duration: 64min
completed: 2026-10-21
---

# Phase 18 Plan 03: Compose Lock, Marker Join, Gate Evidence, Pruned-Erase Driver Summary

**The Phase-16 erase substrate hardened into its final Phase-18 shape: the erase compose now runs inside one owner-vended CrdtSet critical section (WR-02 closed by construction), the /prune/tx marker joins the erase WriteBatch (value XOR marker in one commit), budget/settle config knobs exist fail-closed, an absent delta block is erase-COMPLETING evidence for an otherwise-eligible candidate (never a stranding), and TransactionManager exposes the tick-gate missing-set input plus the production pruned-erase driver with one resolution home.**

## Performance

- **Duration:** 64 min (19:05:59Z – 20:09:33Z)
- **Tasks:** 3 (Task 1 behavior-preserving hardening; Tasks 2-3 TDD RED/GREEN)
- **Files modified:** 13 (0 created)

## Accomplishments

### Task 1: D-01 owner-vended compose lock — behavior-preserving (800a17e5e)

- `CrdtSet::MakeComposeLock()`: public, returns an owning `std::unique_lock<std::recursive_mutex>` on the private `mutex_`; mutex stays private (grep: no accessor exposes it).
- `CrdtDatastore::MakeComposeLock()` passthrough: non-owning guard on an absent set; callers fail closed on `!owns_lock()`.
- `GlobalDB::EraseEntry`: the vend is acquired as the compose's FIRST act and held across guards, staging, the single commit, and the post-commit seal re-assert (the PutElems discipline; StageEraseElement's internal lock_guard is reentrant under the hold — staging code unchanged).
- WR-02 contract note in globaldb.hpp rewritten: the window is CLOSED by construction; per-tx hold scope and bounded-hold discipline documented.
- Behavior-preserving proof: crdt_test + globaldb_integration + prune_eligibility green with ZERO test edits.

### Task 2: Marker batch-join + PruningConfig keys (9a59cec42 RED, ce127bcc3 GREEN)

- `PruneEligibility::BuildPruneMarkerEntry(tx_hash)`: production twin of WritePrunedEntryForTest returning the key/value buffer pair (BuildPruneMarkerKey + PrunedTransactionRecord with the WR-04 tx-path shape); one key home, zero copied key construction.
- `GlobalDB::EraseEntry(key, cid, extra_entry = nullopt)`: optional opaque staged pair joins the SAME batch after the raw-map removal, before the single commit; existing two-arg callers unchanged.
- `PruningConfig`: `max_erases_per_cycle` (100) and `settle_seconds` (60) with HasMember+IsUint64 reads; degenerate zeros rejected at read time (defaults retained); ill-typed members keep defaults.
- Tests C7-C10 (config) + F1 (byte-identity vs WritePrunedEntryForTest) + F2 (one-commit atomicity: value gone, block gone, marker PRUNED) + F3 (seam matrix steps 1/2/3: both sides untouched).

### Task 3: Absent-block evidence + accessor + driver (a4a1e5515 RED, 3ac75d485 GREEN)

- `PruneReason::BLOCK_ALREADY_ERASED`: second affirmative-evidence code; HasFailureReason excludes both evidence codes; BlockGateResult contract updated (erasable verdicts may carry the evidence).
- `GraphsyncDAGSyncer::HasRawBlockBytes(cid)`: byte-level presence via the kept `block_datastore_` member (RocksdbDatastore::contains — no parse); `GlobalDB::HasLocalBlock(cid_string)` passthrough.
- `EvaluateDeltaBlockErasable` absent-branch: parse failure + bytes-absent + eligible-candidate tie → ERASABLE verdict with BLOCK_ALREADY_ERASED and the soundness comment (block bytes vanish only through the sanctioned staged CID::toBytes() removal; the seal survives — GATE-03). Corrupt-but-present and non-eligible-tie stay DESERIALIZE_FAILED.
- `TransactionManager::HasMissingTxHashes()`: const, locks the (now mutable) missing-set mutex, reads emptiness only.
- `TransactionManager::EraseTransactionEntryPruned(tx_hash)`: stages BuildPruneMarkerEntry; the resolve→erase→cleanup sequence exists ONCE (private `EraseTransactionEntryWithMarker`; the plain variant delegates with nullopt — certificate_fallback green proves byte-identical behavior).
- Tests G1 (stranded sibling completes), G2 (corrupt block still fails closed — the discrimination), G3 (non-eligible tie not erasable), G4 (accessor), G5 (driver erases + marks atomically).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] MakeComposeLock definition added to impl/crdt_set.cpp (file not in the plan's list)**
- **Found during:** Task 1 first build (undefined-symbol link error)
- **Issue:** The plan declared the vend in crdt_set.hpp but omitted the implementation file; every CrdtSet method body lives in impl/crdt_set.cpp.
- **Fix:** 4-line definition added after StageEraseElement (declaration-only plan entry completed).
- **Files modified:** src/crdt/impl/crdt_set.cpp
- **Commit:** 800a17e5e

**2. [Rule 3 - Blocking] Absent-vs-corrupt discriminator needed a BYTE-presence check the plan did not name (HasBlock is unsound for it)**
- **Found during:** Task 3 design (before GREEN)
- **Issue:** Distinguishing an erased block from a corrupt one via HasBlock is unsound: HasBlock = parse success (GetNodeFromMerkleDAG) OR LRU hit, so a present-but-corrupt block answers false there too — the G2 discrimination case would misclassify corrupt as absent and grant BLOCK_ALREADY_ERASED.
- **Fix:** Byte-level presence via the vendored `IpfsDatastore::contains` (keys by the same CID::toBytes() StageBlockRemoval removes; no parse): new `GraphsyncDAGSyncer::HasRawBlockBytes` (keeps the ctor's `block_datastore_` beside the DAG service) behind `GlobalDB::HasLocalBlock`. Files beyond the plan's list: graphsync_dagsyncer.{hpp,cpp}.
- **Files modified:** src/crdt/graphsync_dagsyncer.hpp, src/crdt/impl/graphsync_dagsyncer.cpp, src/crdt/globaldb/globaldb.{hpp,cpp}
- **Commit:** 3ac75d485

**3. [Rule 1 - Bug] missing_tx_mutex_ made mutable for the const accessor**
- **Found during:** Task 3 GREEN build (const-method lock error)
- **Issue:** `HasMissingTxHashes() const` locks `missing_tx_mutex_`, which was non-mutable.
- **Fix:** `mutable std::mutex` (the dagMutex_ pattern).
- **Files modified:** src/transaction/TransactionManager.hpp
- **Commit:** 3ac75d485

**4. [Rule 2 - Missing functionality] BLOCK_ALREADY_ERASED gated on an eligible-candidate tie**
- **Found during:** Task 3 GREEN design
- **Issue:** The plan's absent-branch as literally worded ("the candidate is in the eligible set — the gate's existing input contract") is only implicit when Evaluate drives the gate; a direct gate call (the G3 case) could otherwise grant erase-completing evidence for a block tied only to NON-eligible candidates.
- **Fix:** The branch requires the absent CID to be named by an ELIGIBLE candidate in the passed evaluation (cheap any_of); otherwise fail-closed DESERIALIZE_FAILED.
- **Files modified:** src/transaction/PruneEligibility.cpp
- **Commit:** 3ac75d485

## TDD Gate Compliance

- Task 2 RED: `test(18-03)` 9a59cec42 (build failure on the missing PruningConfig members / BuildPruneMarkerEntry / 3-arg EraseEntry).
- Task 2 GREEN: `feat(18-03)` ce127bcc3 after 29/29 + globaldb_integration green.
- Task 3 RED: `test(18-03)` a4a1e5515 (build failure on the missing enumerator + two missing methods).
- Task 3 GREEN: `feat(18-03)` 3ac75d485 after 34/34 + certificate_fallback green.
- REFACTOR: none needed beyond the inline GREEN fixes (tests re-run green after each).

## Verification Evidence

- Task 1: `cmake --build ... crdt_globaldb crdt_test globaldb_integration_test prune_eligibility_test` clean; `ctest -R "crdt_test|globaldb_integration|prune_eligibility"` 3/3 passed, zero test edits.
- Task 2: prune_eligibility_test 29/29 twice (double-run); globaldb_integration passed.
- Task 3: prune_eligibility_test 34/34 twice; `ctest -R certificate_fallback` passed; crdt_test + globaldb_integration rebuilt and passed.
- clang-format: `git-clang-format --diff HEAD` clean at every commit (Task 3 needed one alignment fix applied via `-f`, re-verified clean).
- clang-tidy (llvm@22, filtered single-arch db): zero findings on every new symbol and changed line; remaining findings in changed FILES are pre-existing (whole-struct naming noise that equally flags the pre-existing PruningConfig members; tx_lock/aBatch lines untouched by this plan).
- Acceptance greps: mutex_ still private with only the MakeComposeLock pair public; BuildPruneMarkerKey has one production call site (BuildPruneMarkerEntry); the resolve→erase→cleanup sequence exists once (both drivers delegate); no file deletions in any commit.

## Threat Mitigations Landed

| Threat | Disposition |
|--------|-------------|
| T-18-0301 (spend revert interleaving between evaluation and erase, WR-02) | mitigated — the compose critical section closes the window by construction (guards, staging, commit all inside the vend); 18-06 proves non-interleaving |
| T-18-0302 (partial erase exposing value-without-marker or marker-without-value) | mitigated — the marker joins the ONE WriteBatch; F2/F3 prove one-commit atomicity and both-sides-untouched on interruption |
| T-18-0303 (BLOCK_ALREADY_ERASED erasing a tx whose block vanished non-sanctioned) | mitigated — byte-presence discrimination (corrupt ≠ absent), eligible-candidate tie required (G3), soundness comment in code |
| T-18-0304 (degenerate config K=0/settle=0) | mitigated — both zeros rejected at read time; defaults 100/60 (C9) |
| T-18-0305 (raw mutex exposure inviting misuse) | mitigated — only a scoped guard is vended; mutex_ stays private; per-tx hold scope documented in the rewritten contract |
| T-18-0306 (marker without erase breaking Phase-19 PRUNED answers) | mitigated — single-batch join is the API contract; F1 byte-identity keeps one key home; 17-landed tri-state lookup reports violations loudly |

## Self-Check: PASSED

- Files exist: all 13 modified files + 18-03-SUMMARY.md — all FOUND.
- Commits exist on branch: 800a17e5e, 9a59cec42, ce127bcc3, a4a1e5515, 3ac75d485 — all FOUND via `git log`.
