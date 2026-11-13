---
phase: 18-txpruner-service-first-real-prune
plan: 07
subsystem: transaction
tags: [sc-2, crash-consistency, fixed-point, containment-gates, sanctioned-callers, full-suite, phase-gate, c++17]

# Dependency graph
requires:
  - phase: 18-txpruner-service-first-real-prune (18-06)
    provides: the TxPrunerTest fixture + seeding/snapshot helpers, both interrupt seams, the TransactionManagerPendingLifecycleTestAccess per-binary seam
  - phase: 18-txpruner-service-first-real-prune (18-03, 18-04)
    provides: EraseTransactionEntryPruned, BuildPruneMarkerEntry, the compose seam steps, RunPruneCycle's dry_run gate, TickGateSettled's three conjuncts
provides:
  - test/src/account/tx_pruner_test.cpp CrashFixedPoint suite — the SC-2 seam-matrix proof: 8 kill points across BOTH seams, each reopened on the same DB path and converged to the uninterrupted end state, then clean-node no-op
  - test/src/crdt/crdt_erase_test.cpp in its Phase-18 sanctioned-caller-set shape — exact-list caller greps, zero-PutRaw erase path, dry-run body pin, real-cycle dry_run==false pin, tick-gate conjunct pin (extractFullFunctionBody machinery addition)
  - The phase-gate verification record: full build clean, full ctest 154/154 (SC-4 zero-change arm), format/tidy/stub/inventory sweep
affects: [19 SURV-02 (PRUNED-answer consumers inherit the pinned surface), phase close /gsd:verify-work, UAT manual staging demo]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Reopen-on-same-path crash proof: the kill tears the WHOLE stack down in the fixture destructor's own order (blockchain -> tm -> account -> db ShutdownNow) and rebuilds GlobalDB + account + manager on the SAME db path over the surviving pubsub host (the Phase-16 restartNodePendingStart wiring) — every fixed-point assertion reads what survived on disk, not memory"
    - "Function-scope capture discipline for seam hooks: a [&] lambda stored in a static std::function outlives its arming block, so visit counters/flags MUST live at test-function scope — block-scope captures dangle and silently mutate into wrong-slot references (the bug this plan's first run caught)"
    - "Brace-counting full-body extraction (extractFullFunctionBody) beside the trim-based original: the original stops at the first NESTED closing brace alone on its line, so source-ORDER pins (gate before erase, mode gate before service call) are impossible on its prefix — the full variant exists for the order-sensitive gates only"

key-files:
  created: []
  modified:
    - test/src/account/tx_pruner_test.cpp
    - test/src/crdt/crdt_erase_test.cpp
    - src/transaction/TxPruner.cpp
    - src/account/DevMintInputValidator.cpp
    - src/base/DevMintPolicy.hpp
    - test/src/account/dev_mint_policy_test.cpp
    - test/src/account/prune_eligibility_test.cpp

key-decisions:
  - "The crash matrix is the UNION of both seams' interrupt points (8 cases: cycle AfterEvaluation/BeforeErase-first/BeforeErase-between/BeforeStatus/AfterStatus + compose step 1/2/3), not a cross product — a cycle-level abort prevents the compose seam from ever firing, so only the union is meaningful"
  - "The compose-step kills fire on the SECOND erase (the first erase's three step visits pass): true partial progress — one tx fully committed, the next batch killed at the requested step, the cycle continuing past the per-tx error (the sanctioned log-and-continue contract); killing the first erase only re-proves the 16-03 F3 all-or-nothing case"
  - "A crash BETWEEN two same-block erases legitimately strands the surviving sibling (value present, shared block gone): the BLOCK_ALREADY_ERASED-completable state — asserted as legal, completed by the convergence run; the ILLEGAL shapes (value-without-map-entry, marker-plus-value, both-erased-with-block-present) are the failures"
  - "The fork-based true-kill case is absent-WITH-NOTE (in the suite's doc comment and here): the fixture's pubsub host/asio pool carry live threads whose locks do not survive fork() (research assumption A3) and GlobalDB has no pubsub-less construction — the seam-simulated matrix is THE gate (research open-question 2 default, plan-sanctioned)"
  - "RunUntilCycleRuns discriminates a RUNNING cycle by examined>0 (RunSettledCycle's watermark discriminator is unusable: seam-interrupted cycles deliberately never set the watermark)"
  - "Sanctioned-caller gates are exact FILE-SET lists (owner files excluded, the residual set must equal the enumerated expectation) — comment mentions inside sanctioned files are legal; any new file reaching the surface fails the gate"
  - "The tick-gate conjunct pin reads TWO bodies: RunCycle must call TickGateSettled before its first erase token, and TickGateSettled's body must contain all three conjunct tokens (State::READY / HasMissingTxHashes / settle_seconds) — the conjuncts live in the helper the gate section calls"

patterns-established:
  - "extractFullFunctionBody (brace-counting, balanced-{}-in-strings caveat documented) for order-sensitive source pins; the trim-based extractFunctionBody stays for the token-ban gates"

requirements-completed: [PRUNE-04, PRUNE-05]

# Metrics
duration: 73min
completed: 2026-10-21
---

# Phase 18 Plan 07: Crash Fixed Point + Sanctioned-Caller Gates + Phase Gate Summary

**SC-2 is closed at every interrupt point of both seams — 8 kill cases (every cycle-level position and every compose step, killed mid-progress after a committed erase), each torn down and REOPENED on the same database path, converge to exactly the uninterrupted run's end state with the clean-node re-run a no-op — and the destructive surface is now enumeration-pinned (exact caller-file sets, zero PutRaw in the erase path, dry-run body token-clean, the real cycle gated by dry_run==false before its erase path, and all three tick-gate conjuncts source-pinned before any erase), with the full default suite 154/154 green (SC-4's zero-change arm).**

## Performance

- **Duration:** 73 min (21:34:20Z – 22:47:20Z)
- **Tasks:** 3 (crash matrix / gate rework / phase gate)
- **Files:** 7 modified (2 test files for the plan's scope + 5 files in the Task-3 format-gate fix)
- **Test cases added:** 8 crash-matrix cases + 4 gate tests (gate suite now 7)

## Accomplishments

### Task 1: CrashFixedPoint seam matrix (9bd2a07f4)

- **The matrix (TxPrunerCrashFixedPointTest, ValuesIn over 8 kill points):** cycle seam {AfterEvaluation, BeforeErase-invocation-1, BeforeErase-invocation-2 (between erases), BeforeStatus, AfterStatus} + GlobalDB compose seam {step 1, 2, 3 — each killing the SECOND erase's compose after the first fully committed}.
- **Per case:** mixed seeding (ancient spender, one-tx-block eligible, TWO eligibles in ONE shared block, unspent control) → arm the seam (kill really fired — `killed` asserted; watermark-set == expected completion shape) → **ReopenStackOnSamePath** (fixture-dtor teardown order; rebuilt GlobalDB/scheduler/graphsync-network over the live pubsub host on the SAME db path; account reloads /utxo/ via LoadUTXOs; manager re-driven READY) → (a) immediate fixed point: per eligible tx value⟺map-entry, never marker-plus-value, erased⟹PRUNED marker, present⟹NOT_FOUND; own-block bytes live and die with their tx; both-shared-erased⟹block gone; stranded sibling = the legal BLOCK_ALREADY_ERASED-completable state; spender/unspent/heads/utxo untouched → (b) convergence on a fresh pruner to the UNINTERRUPTED end state (all three eligibles value-gone + CRDT-footprint-gone + map-gone + PRUNED, blocks gone once, exactly 3 markers, heads/maxHeight/seals/utxo byte-identical) → (c) clean-node no-op (examined>0, erased==0, nothing changes).
- **RunUntilCycleRuns:** examined>0 discriminator (interrupted cycles never set the watermark, so RunSettledCycle's watermark wait cannot be used with a kill armed).
- **True-kill fork case: absent-with-note** (suite doc comment + Deviations) — research A3/plan-sanctioned.
- Matrix double-run green (2×17.5s); full binary 22/22 (33s).

### Task 2: EraseSurfaceGate sanctioned-caller rework (fa33263c5)

- **baseBannedTokens += PutRaw** — exactly zero PutRaw in the erase path (T-17-0406's count shift): the marker stages via the batch's put, never a raw direct write.
- **Tracked bodies extended to every Phase-18 mutation surface:** EraseTransactionEntryPruned + the shared EraseTransactionEntryWithMarker compose helper, CrdtSet::MakeComposeLock (staging tokens banned), PruneEligibility::BuildPruneMarkerEntry, TxPruner::RunCycle, DevMintBurnlessPermitted (GetChainId/GetSrcAddress/GetNonce/GetHash/GetUTXOParameters banned — reads only GetNetworkID).
- **SanctionedCallerSetsAreExact (exact-list greps):** EraseTransactionEntryPruned outside its owner == exactly {TxPruner.cpp, TxPruner.hpp}; BuildPruneMarkerEntry outside its owner == exactly {TransactionManager.cpp, TransactionManager.hpp}; EraseEntry outside GlobalDB == exactly {PruneEligibility pair, TransactionManager pair}; WritePrunedEntryForTest == ZERO production files.
- **DryRunCycleBodyStaysTokenClean:** the 17-06 manual inventory as a permanent gate (erase/staging/marker/tx_pruner_ tokens banned from RunPruneDryRunCycle's FULL body).
- **RealCycleIsPinnedToRealMode:** RunPruneCycle's `pruning_config_.dry_run` mode gate precedes `tx_pruner_->RunCycle(` in source order (T-18-0404).
- **TxPrunerTickGateConjunctsPrecedeErases (checker W4):** RunCycle invokes TickGateSettled before its first erase token; TickGateSettled's body carries ALL THREE conjuncts (State::READY, HasMissingTxHashes, settle_seconds).
- **extractFullFunctionBody:** brace-counting full-body variant — the trim-based original truncates at nested closers, making order pins impossible (the LookupPruneSignatures-class known failure mode, fixed at the machinery level).
- **Mutation-verified non-vacuous (17-03 discipline):** stray EraseTransactionEntryPruned caller in GeniusNode.cpp → caller-set gate FAILS; PutRaw inside BuildPruneMarkerEntry → token-ban gate FAILS; deleting the HasMissingTxHashes conjunct → conjunct pin FAILS; all reverted, suite green. (First mutation attempt renamed the token to a superstring — find() still matched; the honest mutation DELETES the conjunct, which fails as required.)

### Task 3: Phase gate (0408dfd6d — format fix; verification-only otherwise)

- **Full default build:** clean, zero errors; zero NEW warnings on phase-changed files (remaining warnings are pre-existing, unrelated test files: HttpStubServer, registration_transaction_test, node_startup_test, price_facade_test).
- **FULL ctest: 154/154 PASS, 0 failed** (1151.89s ≈ 19.2 min; the foundry-family tests — the known environmental flake — passed in the full run, no isolated re-run needed). SC-4's zero-change arm: default-off config erases nothing anywhere with the phase's code compiled in.
- **Format sweep:** whole-file canonical check on every phase-changed C++ file found non-canonical lines THE PHASE ADDED in 5 files (the per-task range-limited git-clang-format gates had blessed them in isolation) — canonicalized in 0408dfd6d; the 18-07 files themselves whole-file clean; legacy files' residual violations are pre-existing (out of scope, 17-06 discipline).
- **clang-tidy (llvm@22, filtered single-arch db):** TxPruner.cpp ZERO findings; DevMintInputValidator's findings (nodiscard/naming) match the copied-precedent class — identical findings on the pre-existing GeniusInputValidator twin; test targets excluded from the tidy gate by the build system (disable_clang_tidy in the addtest macro — the documented 18-05 precedent); 18-07's own changes are test-only.
- **Stub scan over the phase diff:** zero findings (one comment-word false positive: "format placeholders" in a machinery doc comment).
- **Final inventories (sanctioned-caller form, recorded):** INV1 EraseTransactionEntryPruned outside owner = exactly TxPruner.{cpp,hpp}; INV2 BuildPruneMarkerEntry outside owner = exactly TransactionManager.{cpp,hpp}; INV3 EraseEntry outside GlobalDB = exactly the 4 sanctioned files; INV4 WritePrunedEntryForTest = zero outside its owner; INV5 PutRaw in src/transaction+src/crdt = 3 occurrences, all outside the erase path (the test-only writer's own body + the GlobalDB::PutRaw API decl/def); INV6 DeleteKey/DeleteTransaction in src/transaction = the 6 pre-existing legacy tombstone sites, zero in the pruning surface.
- **Manual-only item stays queued for UAT:** the live grow-then-prune demo on staging (D-03-enabled dev-mint → spend → cycle → tx gone at both levels, marker present, disk intact; the bytes claim stays Phase 20).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Seam-hook lambdas captured block-scope locals that dangled**
- **Found during:** Task 1 first run (4 of 8 cases failed: the kills fired at wrong points or never)
- **Issue:** The `[&]` lambdas stored into the static seam hooks captured `else`-block locals whose lifetime ended when the arming block closed — before RunCycle invoked the hooks; the dangling references silently read reused stack slots (fire_at printed 1 instead of 4).
- **Fix:** Every captured local hoisted to test-function scope (the arming comment now states the discipline).
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Commit:** 9bd2a07f4

**2. [Rule 3 - Blocking] Source-order pins were impossible on the trim-based extractFunctionBody**
- **Found during:** Task 2 first run (RealCycle/TickGate pins failed: bodies truncated at the first nested closing brace)
- **Issue:** extractFunctionBody stops at any line trimming to "}" — the first nested block's closer — so positions near a long body's end (the erase path) are unreachable; the checker's known wrapped-signature failure mode, but structural here.
- **Fix:** extractFullFunctionBody (brace-counting, balanced-{}-in-strings caveat documented) used by the three order-sensitive gates; the trim-based original stays for the token-ban gates (unchanged 16/17 behavior).
- **Files modified:** test/src/crdt/crdt_erase_test.cpp
- **Commit:** fa33263c5

**3. [Rule 3 - Cosmetic] Comment false positives in two banned-token sets**
- **Found during:** Task 2 verification
- **Issue:** BuildPruneMarkerEntry's body legitimately mentions WritePrunedEntryForTest in its twin-ship comment; RunPruneDryRunCycle's body legitimately references TxPruner in a comment.
- **Fix:** The BuildPruneMarkerEntry extra set dropped the token (the zero-production-caller grep pins the real invariant); the dry-run ban uses the member name `tx_pruner_` (any service USE goes through the member).
- **Files modified:** test/src/crdt/crdt_erase_test.cpp
- **Commit:** fa33263c5

**4. [Rule 2 - Gate failure] Task 3 format sweep: phase-added lines non-canonical in 5 files**
- **Found during:** Task 3 format gate
- **Issue:** Whole-file canonical formatting on every phase-changed file flagged lines the PHASE added (TxPruner.cpp's gate_holds alignment, DevMintInputValidator.cpp's signature wrap, DevMintPolicy.hpp's array init, dev_mint_policy_test.cpp/prune_eligibility_test.cpp alignments) — the per-task range-limited git-clang-format gates had blessed them in isolation.
- **Fix:** clang-format -i on the 5 files (whitespace only); affected binaries rebuilt and green (gate 7/7, tx_pruner_test 22/22, prune_eligibility+dev_mint_policy ctest 2/2).
- **Files modified:** src/transaction/TxPruner.cpp, src/account/DevMintInputValidator.cpp, src/base/DevMintPolicy.hpp, test/src/account/dev_mint_policy_test.cpp, test/src/account/prune_eligibility_test.cpp
- **Commit:** 0408dfd6d

**5. [Rule 3 - Plan filter] The plan's `TxPruner.CrashFixedPoint*` filter matches nothing**
- **Found during:** Task 1 verification (the standing 18-06 Deviation-1 consequence)
- **Issue:** The suite is TxPrunerTest/TxPrunerCrashFixedPointTest per the codebase convention.
- **Fix:** All runs use `*CrashFixedPoint*`; the full binary is the authoritative gate (22/22).

## Verification Evidence

- Task 1: matrix 8/8 twice consecutively (17.5s per run); full tx_pruner_test binary 22/22 (33s); clang-format clean.
- Task 2: EraseSurfaceGate 7/7 twice consecutively; full crdt_test binary 47/47 (21s); mutation checks A/B/C each FAILED the gate and the reverted tree is green; clang-format clean (4 pre-existing file violations normalized as incidental).
- Task 3: full build clean; **full ctest 154/154, 0 failed** (1151.89s); tidy findings as recorded above; stub scan zero; inventories INV1-INV6 recorded.
- No file deletions in any commit; all mutation probes reverted (`git status` clean on src/).

## Threat Mitigations Landed

| Threat | Disposition |
|--------|-------------|
| T-18-0701 (partial-erase state surviving a crash) | mitigated — the 8-case matrix × reopen assertions on both completeness directions + convergence-to-undisturbed-endstate + clean-node no-op; the fork case absent-with-note per the plan's sanctioned default |
| T-18-0702 (an unsanctioned production erase/marker caller appearing later) | mitigated — exact-list caller-set gates, mutation-verified non-vacuous in both directions (stray caller fails; conjunct deletion fails) |
| T-18-0703 (the real arm erasing in dry-run mode) | mitigated — dual pins: dry-run full-body token cleanliness + the real cycle's dry_run mode gate preceding its erase path in source order |
| T-18-0704 (verification claims without runs) | mitigated — every gate result recorded above with counts; the full-suite number is 154/154 from the run, not an estimate |

## Self-Check: PASSED

- Files exist: test/src/account/tx_pruner_test.cpp, test/src/crdt/crdt_erase_test.cpp, the 5 format-fix files, 18-07-SUMMARY.md — all FOUND.
- Commits exist on branch: 9bd2a07f4 (crash matrix), fa33263c5 (gate rework), 0408dfd6d (format gate fix) — all FOUND via `git log`.
- No file deletions in any commit (`git diff --diff-filter=D` empty across a82d4f51c..HEAD).
