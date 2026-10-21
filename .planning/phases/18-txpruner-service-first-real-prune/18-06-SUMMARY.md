---
phase: 18-txpruner-service-first-real-prune
plan: 06
subsystem: transaction
tags: [txpruner-tests, sc-1, sc-4, sc-5, sweep-reprunes, stranding, compose-lock, prune-status, recl-03, recl-04, c++17]

# Dependency graph
requires:
  - phase: 18-txpruner-service-first-real-prune (18-04)
    provides: TxPruner::New/RunCycle/GetPruneStatus, PruneCycleStats/PruneStatus contracts, SetCycleInterruptHookForTest seam steps
  - phase: 18-txpruner-service-first-real-prune (18-03)
    provides: EraseTransactionEntryPruned, LookupPrunedEntry tri-state, BLOCK_ALREADY_ERASED evidence, GlobalDB erase seam, MakeComposeLock
  - phase: 17-eligibility-policy-gating-dry-run (17-04, 17-05)
    provides: the seeding recipes (real-publish-path candidates, batched shared blocks) and the evaluation/block-gate surfaces
provides:
  - test/src/account/tx_pruner_test.cpp — 14 service-level suites: RealPrune (SC-1 three-part + byte-identity), Idempotent, SweepReprunes (D-02b), SharedBlockBudget (K1-defer/K2-take + BLOCK_ALREADY_ERASED completion), TickGate (SC-4 service half), Observability (RECL-03), PruneStatus (RECL-04 watermark/concurrency), ComposeLock (D-01 non-interleaving + erase-time guard refusal)
  - The reusable TxPrunerTest fixture (CRDTFixture family, both seams cleared in the dtor, snapshot helpers for /utxo//cert//heads/seal byte-identity)
affects: [18-07 containment gates (the service suites they pin), 19 SURV-02 (PRUNED-answer consumers), PRUNE-05, RECL-03, RECL-04]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Deterministic compose-lock race via the PRODUCTION seams: the cycle is held inside EraseEntry (GlobalDB erase seam step 1 — guards passed, staged, uncommitted, lock held) while the racer's PutElems blocks on the owner-vended mutex; the blocked-merge observation is bounded and positive (EXPECT the racer NOT done while held), and the final state is asserted as a legal serialization, never a specific interleaving"
    - "Watermark-settled cycle helper: a fresh TxPruner's first non-skipped cycle is detected by the watermark turning set (gate skips leave the status untouched), so settle waits poll RunCycle under ASSERT_WAIT_FOR_CONDITION instead of sleeping past the clock"
    - "Byte-identity snapshots exclude the transient /crdt/work/ broadcaster-queue family: those entries are pending-publish plumbing drained asynchronously by pubsub, not record bytes — their presence is drain-timing, not prune behavior"

key-files:
  created:
    - test/src/account/tx_pruner_test.cpp
  modified:
    - test/src/account/CMakeLists.txt

key-decisions:
  - "The gtest suite is TxPrunerTest, not TxPruner: `using namespace sgns` + the sgns::TxPruner service make a global fixture named TxPruner an ambiguous name at every TEST_F site — the plan's `TxPruner.*` filter form was traded for the codebase's XxxTest fixture convention (filters are TxPrunerTest.*)"
  - "The tick gate's READY input is driven through the EXISTING TransactionManagerPendingLifecycleTestAccess friend seam (per-binary definition carrying ChangeState only): no production header change, and StartCore's async nonce path is network-timed (not deterministic); pending_lifecycle green on the same tree proves zero interference"
  - "ComposeLock proves non-interleaving with a POSITIVE blocked-merge observation (the racer provably cannot finish while the compose lock is held) plus the legal-serialization assertion; the guard re-check half is proven by the head-guard case — evaluation and the block gate accept a tx whose delta is the live topic head, the compose refuses at erase time, and the refusal is never torn (value AND marker both pre-erase)"
  - "The resurrected (add-wins) state after a merge-won race is marker-PLUS-value — LookupPrunedEntry reports VALUE_PRESENT_INVARIANT_VIOLATION exactly as the SweepReprunes deliberate resurrection does; the next cycle's re-erase is the closure, matching D-02b"

patterns-established:
  - "Service-suite fixture with dual-seam teardown: TxPruner::SetCycleInterruptHookForTest(nullptr) AND GlobalDB::SetEraseInterruptHookForTest(nullptr) cleared FIRST in the dtor (T-18-0604), before the blockchain-first asio teardown order"

requirements-completed: [PRUNE-04, PRUNE-05, RECL-03, RECL-04]

# Metrics
duration: 15min
completed: 2026-10-21
---

# Phase 18 Plan 06: TxPruner Service Suites Summary

**The TxPruner service is proven on real stores: one cycle erases exactly the spent-beyond-window set (three-part per-tx assertions; /utxo/, cert/seed records, DAG heads, and every seal byte-identical; the unspent sibling untouched), post-drain cycles are no-ops, resurrected add-wins entries are re-erased by the next cycle (the sweep), the K-budget defers shared-block groups whole and completes stranded siblings through BLOCK_ALREADY_ERASED evidence, the tick gate refuses per conjunct (non-Light at construction, non-READY, settle-not-elapsed), the RECL-03/RECL-04 fields match fixture actions with a monotonic watermark that interrupted cycles never advance and concurrent readers never deadlock — and the D-01 compose lock holds: a concurrent PutElems on the same key provably blocks for the entire held erase window and the final state is always a legal serialization, never torn.**

## Performance

- **Duration:** 15 min (21:16:53Z – 21:31:24Z)
- **Tasks:** 3 (fixture+RealPrune+Idempotent / SweepReprunes+SharedBlockBudget+TickGate / Observability+PruneStatus+ComposeLock)
- **Files:** 2 (1 created, 1 modified)
- **Test cases:** 14 (all double-run green; full binary 15.3s per run)

## Accomplishments

### Task 1: Fixture + RealPrune (SC-1) + Idempotent (99692f635)

- `TxPrunerTest : CRDTFixture` — the prune_eligibility_test stack verbatim (real GlobalDB + pubsub + temp dir, Light TransactionManager, MemorySecureStorage; NO GeniusNode), seeding through the real publish path on kSeedTopic; BOTH interrupt seams cleared first in the dtor; fresh per-test DB paths (UniqueFixturePath).
- **RealPrune:** 4-candidate fixture (ancient spender, 2 spent-beyond-window eligibles each in own block, unspent sibling) + cert-shaped and seed non-tx records; ONE settled cycle → examined=4/eligible=2/erased=2; per erased tx: CRDT value gone (all four element families), raw map entry gone, marker PRUNED naming the tx path, block bytes gone; `/utxo/` prefix, cert + seed CRDT footprints, full head map + maxHeight, and ALL eight seals byte-identical; unspent sibling byte-identical on every level (value, map, block, CRDT footprint) with marker NOT_FOUND; watermark == maxHeight.
- **Idempotent:** full drain (settled cycle + repeats to erased==0), snapshot (markers/utxo/heads/absences/sibling), one more cycle → examined=2>0, erased=0, eligible=0, every snapshot unchanged, markers still PRUNED.

### Task 2: SweepReprunes + SharedBlockBudget + TickGate (7f593b137)

- **SweepReprunes (D-02b):** full erase → re-publish the same element through the real topic + re-wire the map entry → resurrected state VISIBLE (value present; LookupPrunedEntry == VALUE_PRESENT_INVARIANT_VIOLATION) → next cycle re-erases with examined=3/eligible=1/erased=1 (fresh evaluation, no cached set) and exactly ONE marker entry (no duplicate harm).
- **SharedBlockBudget:** two eligibles in ONE delta; K=1 → deferred WHOLE (erased=0, held_blocks=1, both values present, both markers NOT_FOUND); K=2 (fresh instance) → both erase together (block gone ONCE, both markers PRUNED). Missing-block arm: sanctioned plain erase of sibling A deletes the shared block → a FRESH Evaluate() carries BLOCK_ALREADY_ERASED on the absent block's report (the checker-adjusted assertion) → the cycle completes B (erased=1, marker PRUNED); A has NO marker (the two drivers stay distinguishable).
- **TickGate:** Full + Archive refused at TxPruner::New with operation_not_permitted (IsPermanentStop true); non-READY (CREATING) skips zeroed with status untouched then erases at READY; large settle (3600s) skips twice then a small-settle pruner on the SAME fixture erases (the skip was the settle window); empty missing set pinned at the accessor level (a non-empty set has no public seam — not faked, per plan).

### Task 3: Observability + PruneStatus + ComposeLock (4acef8ab8)

- **Observability (RECL-03):** every counter equals the fixture actions; generation tag on stats and status.last_cycle; watermark == observed maxHeight; last_cycle field-by-field equal to the returned stats (duration included); a second invocation's tag (kTestGeneration+2) travels; NO bytes-reclaimed field exists on either struct (the structural half of "bytes reclaimed: not measured until Phase 20").
- **PruneStatus (RECL-04):** seam-interrupted cycle (kInterruptBeforeErase) leaves watermark AND last_cycle untouched though it evaluated (examined>0); the next completed cycle advances the watermark monotonically to the current head; a reader thread performs 200+ GetPruneStatus reads DURING a seam-held cycle, releases it, and the raced cycle completes — no deadlock, every snapshot a coherent full copy.
- **ComposeLock (D-01 capstone):** the cycle held INSIDE the erase compose (GlobalDB seam step 1: guards passed, CRDT staged, commit pending, lock held) — a concurrent PutElems re-add on the SAME key provably does NOT complete within a 300ms bounded window (blocked on the owner-vended mutex); after release the erase commits first and the merge serializes after it: marker committed + add-wins value present (the legal merge-won serialization; NEVER value-absent-without-marker); the next cycle re-erases after the map re-wire. Head-guard case: a tx whose delta is the live topic head passes evaluation and the block gate but the compose REFUSES at erase time (errors=1, erased=0, value AND marker both in pre-erase state — never torn), the cycle still completes and advances the watermark, and the next cycle erases cleanly once headship retires.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] The gtest suite could not be named `TxPruner`**
- **Found during:** Task 1 first build
- **Issue:** The plan's verify commands filter `TxPruner.RealPrune*` — but a fixture class named TxPruner at global scope is ambiguous with `sgns::TxPruner` under the file's `using namespace sgns;` (every TEST_F site fails to compile; qualifying inside TEST_F stringizes to "::TxPruner" and breaks the filter anyway).
- **Fix:** Suite/fixture named `TxPrunerTest` (the codebase's XxxTest convention); filters are `TxPrunerTest.*`. Suite-name prefixes (RealPrune_, Idempotent_, ...) keep the plan's suite taxonomy.
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Commit:** 99692f635

**2. [Rule 3 - Blocking] No deterministic public seam reaches State::READY**
- **Found during:** Task 1 fixture design
- **Issue:** The tick gate requires TransactionManager::State::READY; the fixture's manager stays CREATING and StartCore's route to READY runs through network-timed nonce checks.
- **Fix:** Per-binary definition of the EXISTING friend seam `TransactionManagerPendingLifecycleTestAccess` (namespace sgns, ChangeState only) — zero production changes; transaction_manager_pending_lifecycle_test green proves the reuse interferes with nothing.
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Commit:** 99692f635

**3. [Rule 1 - Flaky assertion] Transient /crdt/work/ entries made cert byte-identity timing-dependent**
- **Found during:** Task 1 first run (the only RealPrune failure; the erase itself was correct)
- **Issue:** The pubsub broadcaster drains its work-journal entries asynchronously, so a BEFORE snapshot could capture a pending-publish entry that disappears milliseconds later for reasons unrelated to pruning.
- **Fix:** SnapshotCrdtEntriesContaining excludes the `/crdt/work/` family (transient broadcaster plumbing); the durable element families (value/priority/set-bit) remain fully compared.
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Commit:** 99692f635

**4. [Rule 3 - Cosmetic] Plan filter commands use the `TxPruner.` prefix**
- **Found during:** Task 1 verification
- **Issue:** `--gtest_filter='TxPruner.RealPrune*'` matches 0 tests with the TxPrunerTest suite (consequence of Deviation 1).
- **Fix:** All runs use `TxPrunerTest.*` filters; the full binary is the authoritative gate (14/14 twice).

**5. [Rule 2 - As planned alternative] RECL-03 bytes-reclaimed statement asserted structurally**
- **Found during:** Task 3
- **Issue:** Capturing the TxPruner INFO line would need a runtime log sink (brittle); the plan sanctions "capture via the logger OR assert the equivalent PruneStatus fields".
- **Fix:** The suites pin the structural half — PruneStatus/PruneCycleStats carry NO bytes field at all, so no fabricated number can exist; the INFO line's "not measured until Phase 20" wording is pinned by the 18-04 implementation and its header contract.

## Verification Evidence

- `cmake --build build/OSX/Release --target tx_pruner_test` — clean, zero errors/warnings (after each task).
- Task 1: `TxPrunerTest.RealPrune* + TxPrunerTest.Idempotent*` — 2/2, twice consecutively.
- Task 2: `TxPrunerTest.SweepReprunes* + SharedBlock* + TickGate*` — 7/7, twice consecutively.
- Task 3: `TxPrunerTest.Observability* + PruneStatus* + ComposeLock*` — 5/5; then the FULL binary 14/14 twice consecutively (15.3s per run) and once via ctest.
- No-interference selections: `ctest -R "prune_eligibility|certificate_fallback"` 2/2 passed; `ctest -R "crdt_test|globaldb_integration|transaction_manager_pending_lifecycle"` 3/3 passed.
- clang-format (llvm@22, repo config): `clang-format --dry-run --Werror` clean after each task's `-i` application; CMakeLists block copied verbatim from the prune_eligibility registration.
- clang-tidy: test targets are excluded from the tidy gate by the build system (`disable_clang_tidy` in the addtest macro, cmake/functions.cmake:27) — the documented 18-05 precedent; not run per that exclusion.
- No file deletions in any commit; untracked scratch DB dirs under the repo root are pre-existing runtime artifacts (present before this plan's commits).

## Threat Mitigations Landed

| Threat | Disposition |
|--------|-------------|
| T-18-0601 (vacuous green: asserting presence instead of absence) | mitigated — three-part assertions per erased tx plus block-bytes absence; byte-identity snapshots of /utxo/, cert/seed records, heads, and seals; unspent-sibling negative control; double-run everywhere |
| T-18-0602 (status trusted over state) | mitigated — Observability/PruneStatus assertions bind the status to the store (marker PRUNED iff value absent; watermark == the live maxHeight) |
| T-18-0603 (race passing by scheduling luck) | mitigated — the compose window is held deterministically via the production seams; the blocked-merge observation is positive and bounded; the invariant (never torn) is asserted, not a specific interleaving |
| T-18-0604 (seams leaking between cases) | mitigated — both interrupt seams cleared FIRST in the fixture destructor; fresh per-test DB paths; every suite double-run green |

## Self-Check: PASSED

- Files exist: test/src/account/tx_pruner_test.cpp, test/src/account/CMakeLists.txt (modified), 18-06-SUMMARY.md — all FOUND.
- Commits exist on branch: 99692f635 (fixture + RealPrune + Idempotent), 7f593b137 (SweepReprunes + SharedBlockBudget + TickGate), 4acef8ab8 (Observability + PruneStatus + ComposeLock) — all FOUND via `git log`.
- No file deletions in any of the three commits (`git diff --diff-filter=D` empty across HEAD~3..HEAD).
