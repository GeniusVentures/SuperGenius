---
phase: 18-txpruner-service-first-real-prune
verified: 2026-10-21T21:30:00Z
status: human_needed
score: 10/12 must-haves verified
overrides_applied: 0
gaps:
  - truth: "The containment gates are green at final HEAD (sanctioned-caller-set enumeration pins the erase surface) and the full default ctest suite passes"
    status: failed
    reason: >-
      The WR-02 review-fix commit b0fad55b6 rewrote PruneEligibility.hpp's
      BuildPruneMarkerEntry doc block and dropped the word "EraseEntry" from
      that header. EraseSurfaceGateTest.SanctionedCallerSetsAreExact (which
      scans the live source tree) still expects
      "transaction/PruneEligibility.hpp" inside the EraseEntry caller set
      (compose_set, test/src/crdt/crdt_erase_test.cpp:985-988), so the gate
      fails at final HEAD: found {PruneEligibility.cpp, TransactionManager.cpp,
      TransactionManager.hpp} vs expected 4 files. The REVIEW-FIX consolidated
      run covered only 4 ctest selections (utxo_manager,
      certificate_fallback, prune_eligibility, tx_pruner) and did not include
      crdt_test, so the regression slipped through. Production behavior is
      unaffected (no unsanctioned caller appeared — a doc mention was
      removed), but the phase's own gates-not-promises discipline leaves a
      red containment gate and a non-green full suite at phase close.
    artifacts:
      - path: "test/src/crdt/crdt_erase_test.cpp"
        issue: "compose_set (EraseEntry expected caller set) still lists transaction/PruneEligibility.hpp whose EraseEntry doc mention was removed by WR-02 commit b0fad55b6"
      - path: "src/transaction/PruneEligibility.hpp"
        issue: "No longer mentions EraseEntry after the WR-02 D-C1 doc rewrite (grep confirms 0 occurrences at HEAD vs 1 at 0408dfd6d)"
    missing:
      - "Update compose_set in SanctionedCallerSetsAreExact to the actual sanctioned 3-file set (drop transaction/PruneEligibility.hpp), or restore an EraseEntry contract-doc mention in PruneEligibility.hpp — whichever the owner prefers; then re-run crdt_test (expect 47/47)"
deferred:
  - truth: "bytes-reclaimed measurement in the per-cycle observability output"
    addressed_in: "Phase 20"
    evidence: "ROADMAP SC-5: 'the bytes-reclaimed field completes with Phase 20 compaction'; Phase 20 holds RECL-01/GATE-02 (reclamation proof deliberately last). The INFO line states 'bytes reclaimed: not measured until Phase 20' and PruneStatus carries no bytes field by design (TxPruner.hpp Doxygen)."
human_verification:
  - test: "Live staging grow-then-prune demo"
    expected: "A light node on staging: D-03-enabled dev-mint seeds prunable history, the minted output is spent, the window elapses, the TxPruner cycle fires, the tx disappears at both storage levels (CRDT value + block), the /prune/tx/<hash> marker is present, the unspent set and disk stay intact. Bytes-reclaimed claim stays Phase 20."
    why_human: "Requires a live staging deployment with real consensus and wall-clock settle/finality windows; cannot be exercised from the repo test harness."
  - test: "CR-01 MintFunds catch-path rollback fix (logic-class review)"
    expected: "Owner reviews commit 08421f54a: burn_reservation_id is captured before the try block and the catch path rolls back under it. The regression test BurnCleanup_MintFundsCatchPathRemovesBurnUtxoAfterPostMoveThrow drives a real post-move throw through the SetMintFundsPostMoveExceptionHookForTest seam and asserts memory + disk removal and re-delivery unblocked (passes, 52ms)."
    why_human: "The fix protocol flags logic-error-class fixes for owner sign-off; the seam-driven throw is a surrogate for the natural exception sites (none exist for a valid fixture)."
---

# Phase 18: TxPruner Service & First Real Prune Verification Report

**Phase Goal:** The `TxPruner` service performs the first real erasure on a light node — an idempotent, crash-consistent cycle that survives re-delivery and resurrection — wired into `GeniusNode` behind default-off config, with per-cycle observability and queryable status.
**Verified:** 2026-10-21T21:30:00Z (at branch HEAD d2fc6644b; phase code commits 706a142ad^..d2fc6644b)
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

Consolidated from ROADMAP SC-1..SC-5 plus the seven PLAN must-have clusters, deduplicated.

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | SC-1: Light node with spent-beyond-window history physically erases eligible txs through the ordered flow; both levels gone; unspent set untouched | ✓ VERIFIED | `GlobalDB::EraseEntry` (src/crdt/globaldb/globaldb.cpp:798-911) composes value→CID→block-eligibility+delete→map-entry under the compose lock with the marker batch-joined before the single commit; `TxPruner::RunCycle` drives `EraseTransactionEntryPruned` per tx; spot-check `TxPrunerTest.RealPrune_ErasesEligibleAndLeavesEverythingElseByteIdentical` green (tx_pruner_test 22/22, run by verifier at HEAD) |
| 2 | SC-2: Crash at any step converges to the fixed point; clean-node re-run is a no-op | ✓ VERIFIED | `TxPrunerCrashFixedPointTest` 8 kill points (5 cycle-seam + 3 compose-seam steps), `ReopenStackOnSamePath` + convergence + clean-node no-op; full binary 22/22 PASSED at HEAD (32.7s) |
| 3 | SC-3: Re-delivery terminal — zero pruned-CID fetch attempts both delivery modes; sweep re-prunes resurrected entries | ✓ VERIFIED | D-02 guard verified in source: exactly one `isResolved` insertion in `HandleCIDBroadcast` ordered HasBlock→isResolved→IsCIDInCache (src/crdt/impl/crdt_datastore.cpp:727, trace "Pruned root ... terminal, skipping"); `GlobalDBEraseRedeliveryTest` 3/3 PASSED at HEAD (re-broadcast, restart head-exchange, negative control); `SweepReprunes_ResurrectedEntryIsReErasedNextCycle` green |
| 4 | SC-4: Tick gate = Light ∧ READY ∧ empty missing set ∧ settle elapsed; GeniusNode constructs/starts/stops per config; default-off erases nothing | ✓ VERIFIED | `TickGateSettled` (TxPruner.cpp:299-319) carries all three conjuncts with restart-on-reopen settle clock; `StartPruneTimerIfConfigured` enabled-guard is the first executable statement (GeniusNode.cpp:4881-4887); `RunPruneCycle` opens with the dry_run mode gate (GeniusNode.cpp:5080-5087); erase-token grep on GeniusNode.cpp = 0 matches (verifier-run); TickGate×4 suites green. Note: the full-suite instrument for the zero-change arm is currently red — see truth 11 (failure is a stale gate expectation, not an erase-behavior regression) |
| 5 | SC-5: Per-cycle observability log (examined/erased, watermark advance; bytes-reclaimed completes with Phase 20) + queryable PruneStatus | ✓ VERIFIED | RECL-03 INFO line in `RecordCompletedCycle` (TxPruner.cpp:285-296) with explicit "bytes reclaimed: not measured until Phase 20"; `PruneStatus`/`PruneCycleStats` carry watermark + last-cycle, no bytes field; `GetPruneStatus()` on TxPruner (mutex-guarded copy) + `GeniusNode::GetPruneStatus` delegation (GeniusNode.cpp:4281-4292); Observability + PruneStatus suites green (incl. watermark-untouched-on-interrupt, concurrent reader) |
| 6 | D-03 dev-mint policy: designated nets certify burn-less mints through full consensus; non-designated refuse regardless of proposer; both bypass legs net-scoped; compile-time set | ✓ VERIFIED | `kDevMintPermittedNets` built only from named constants (src/base/DevMintPolicy.hpp:31); parameterless `DevMintBurnlessPermitted()` reads only GetNetworkID(); both `LocalBypassEvidence` legs gated with default-invalid refusal (PublicChainInputValidator.cpp:64,82); `Register("dev",...)` + `kDevMintValidatorRegistered` anchor (TransactionManager.cpp:74-76); dev_mint_policy_test 11/11 PASSED at HEAD incl. real-quorum DesignatedNet + Refuses×2 |
| 7 | WR-02 closed by construction: erase compose inside one owner-vended CrdtSet critical section; marker joins the same WriteBatch (value XOR marker atomic) | ✓ VERIFIED | `MakeComposeLock` vended, mutex_ private (crdt_set.hpp:319,402); lock acquired as EraseEntry's first act, commit + seal re-assert inside the hold (globaldb.cpp:816-911); extra_entry staged via `batch->put` before the single commit; F2/F3 + `ComposeLock_*` suites green (racer provably blocked; never-torn assertion) |
| 8 | Config can bound erases per cycle (default 100) and set the settle window (default 60), fail-closed | ✓ VERIFIED | PruningConfig members with defaults 100/60 (PruneEligibility.hpp:241,247); degenerate zeros rejected at read time keeping safe defaults (PruneEligibility.cpp:97-108); config suites green in prune_eligibility_test 36/36 |
| 9 | No stranding: K-exhausted shared-block group defers whole; absent block completes an otherwise-eligible erase (BLOCK_ALREADY_ERASED); corrupt block still fails closed | ✓ VERIFIED | Group-or-defer structural (`group.size() > remaining_k` → `++held_blocks; continue`, TxPruner.cpp:183-190); BLOCK_ALREADY_ERASED evidence code excluded from failure reasons, fired only on eligible-candidate tie (PruneEligibility.cpp:406); SharedBlockBudget×2 + G1-G3 suites green |
| 10 | MintFunds consensus-reject cleanup: no dangling burn UTXO_BRIDGE, no lingering reservation; idempotent; negative control; CR-01 use-after-move fixed | ✓ VERIFIED | `RemoveBurnUTXOs` (UTXOManager.cpp:681) wired on both the FAILED mint-v2 leg (TransactionManager.cpp:5129) and the catch path (:1074); CR-01 fix in source: `burn_reservation_id` captured before the try (:1044) and used in the catch; WR-01 repair-failure ERROR log present (UTXOManager.cpp:754); certificate_fallback 32/32 + utxo_manager 34/34 PASSED at HEAD incl. `BurnCleanup_MintFundsCatchPathRemovesBurnUtxoAfterPostMoveThrow` (52ms, verifier-run) |
| 11 | Containment gates green at HEAD: sanctioned-caller-set enumeration pins the erase/marker surface | ✗ FAILED | `EraseSurfaceGateTest.SanctionedCallerSetsAreExact` FAILS at HEAD (verifier-run, crdt_test 46/47): WR-02 commit b0fad55b6 removed the "EraseEntry" doc mention from PruneEligibility.hpp (1 occurrence at 0408dfd6d → 0 at HEAD), but the gate's expected `compose_set` still lists that file (crdt_erase_test.cpp:985-988). Found 3 files vs expected 4 |
| 12 | Full default ctest suite green at final HEAD (SC-4 zero-change arm instrument) | ✗ FAILED | crdt_test 46/47 at HEAD — exactly the truth-11 failure, same root cause. The REVIEW-FIX consolidated run (4/4) omitted crdt_test. All phase-behavior binaries green at HEAD: tx_pruner 22/22, dev_mint 11/11, prune_eligibility 36/36, certificate_fallback 32/32, utxo_manager 34/34, globaldb_integration 21/21 |

**Score:** 10/12 truths verified

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | bytes-reclaimed field/measurement | Phase 20 | ROADMAP SC-5 sanctions the placeholder ("completes with Phase 20 compaction"); Phase 20 owns RECL-01/GATE-02 reclamation proof; implemented as an explicit "not measured until Phase 20" statement + absent struct field (by design) |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/transaction/TxPruner.hpp/.cpp` | Service: factory, RunCycle, PruneStatus, seam | ✓ VERIFIED | 344+401 lines, substantive; all cycle stages implemented; no GeniusNode include; no window arithmetic |
| `src/base/DevMintPolicy.hpp` | Compile-time designated-net policy | ✓ VERIFIED | Named constants only; parameterless predicate |
| `src/account/DevMintInputValidator.hpp/.cpp` | Production validator twin for chain id "dev" | ✓ VERIFIED | Register + net-gated ValidateWitness |
| `src/account/PublicChainInputValidator.cpp` | Both bypass legs net-scoped, default-invalid refusal | ✓ VERIFIED | 2 conjunct occurrences in LocalBypassEvidence |
| `src/crdt/crdt_set.hpp` + impl | Owner-vended compose lock, mutex private | ✓ VERIFIED | MakeComposeLock public; mutex_ private at :402 |
| `src/crdt/globaldb/globaldb.cpp` | EraseEntry under vend + optional staged marker entry | ✓ VERIFIED | Lock first act; extra_entry same-batch; single commit; seal re-assert |
| `src/transaction/PruneEligibility.{hpp,cpp}` | BuildPruneMarkerEntry(resolved path), config keys, BLOCK_ALREADY_ERASED | ✓ VERIFIED | WR-02 shape `(tx_hash, resolved_tx_path)`; defaults 100/60 fail-closed; evidence code |
| `src/transaction/TransactionManager.{hpp,cpp}` | HasMissingTxHashes + EraseTransactionEntryPruned, one resolution home | ✓ VERIFIED | Both delegate to EraseTransactionEntryWithMarker(tx_hash, stage_pruned_marker); marker built from the resolved path (post-WR-02) |
| `src/account/GeniusNode.{hpp,cpp}` | Real-cycle arm, GetPruneStatus, default-off wiring | ✓ VERIFIED | WARN-return replaced; enabled guard first statement; erase-token grep 0; RunPruneDryRunCycle body byte-identical (diff base vs HEAD: no changes) |
| `src/account/UTXOManager.{hpp,cpp}` | RemoveBurnUTXOs primitive | ✓ VERIFIED | Removal semantics with CONSUMED never touched; WR-01 repair logging |
| `test/src/account/tx_pruner_test.cpp` | 14 service suites + crash matrix | ✓ VERIFIED | 1925 lines; 22 tests incl. 8-point matrix; 22/22 green at HEAD |
| `test/src/account/dev_mint_policy_test.cpp` | Designated/refusal/bypass suites | ✓ VERIFIED | 828 lines; 11/11 green at HEAD |
| `test/src/crdt/crdt_erase_test.cpp` | EraseSurfaceGateTest sanctioned-caller shape | ⚠️ ORPHANED-EXPECTATION | Gate exists and 6/7 subtests green; SanctionedCallerSetsAreExact carries a stale expected set (see gap) |
| `.planning/todos/completed/genius-chain-witness-bypass-hardening.md` | Todo resolved-by-design | ✓ VERIFIED | Present in completed/ with resolution record |
| `.planning/todos/completed/mintfunds-leaks-burn-utxo-on-consensus-reject.md` | Todo closed with fix | ✓ VERIFIED | Present in completed/ |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| TxPruner.cpp RunCycle | PruneEligibility::Evaluate | per-cycle construction + verbatim Evaluate | ✓ WIRED | TxPruner.cpp:105-115 |
| TxPruner.cpp per-tx step | EraseTransactionEntryPruned | re-verify then erase | ✓ WIRED | TxPruner.cpp:193-203; re-verify precedes every erase call |
| GeniusNode.cpp timer tick (dry_run==false) | TxPruner::RunCycle | snapshot deps + generation | ✓ WIRED | GeniusNode.cpp:4948, 5119; supersession check before invoke |
| globaldb.cpp EraseEntry | CrdtSet mutex via vend | MakeComposeLock first act, commit inside hold | ✓ WIRED | globaldb.cpp:816 |
| TransactionManager EraseTransactionEntryWithMarker | BuildPruneMarkerEntry | resolved-path marker pair into EraseEntry | ✓ WIRED | TransactionManager.cpp:4333-4335 (WR-02 shape) |
| crdt_datastore.cpp HandleCIDBroadcast | isResolved | one insertion between HasBlock continue and IsCIDInCache | ✓ WIRED | crdt_datastore.cpp:727; ordering verified |
| PublicChainInputValidator.cpp | DevMintBurnlessPermitted | conjunct on both bypass legs | ✓ WIRED | :64 and :82 |
| DevMintInputValidator.cpp | IInputValidator registry | Register("dev") | ✓ WIRED | DevMintInputValidator.cpp:16 |
| TransactionManager.cpp | DevMintInputValidator::Register | kDevMintValidatorRegistered anchor | ✓ WIRED | TransactionManager.cpp:76 beside :74 |
| TransactionManager.cpp mint FAILED cleanup | RemoveBurnUTXOs | burn-hash reservation id | ✓ WIRED | :1074 (catch, burn_reservation_id) and :5129 (FAILED leg) |

### Data-Flow Trace (Level 4)

N/A — C++ service/store layer; data flow verified structurally via the test suites running against real GlobalDB/RocksDB/pubsub fixtures (tx_pruner_test, globaldb_erase_test operate on real stores, not mocks; three-part + byte-identity assertions).

### Behavioral Spot-Checks

Run by the verifier at final HEAD d2fc6644b (binaries are the fix-cycle rebuilds; the gates scan live source at runtime):

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| CR-01 regression (post-move throw drives catch-path cleanup) | `transaction_manager_certificate_fallback_test --gtest_filter='*MintFundsCatchPath*'` | 1/1 PASSED (52 ms) | ✓ PASS |
| WR-02 cross-network marker pair | `prune_eligibility_test --gtest_filter='*CrossNetwork*'` | 2/2 PASSED (107 ms) | ✓ PASS |
| Containment gates | `crdt_test --gtest_filter='*EraseSurfaceGate*'` | 6/7, SanctionedCallerSetsAreExact FAILED | ✗ FAIL |
| SC-3 re-delivery suite | `globaldb_integration_test --gtest_filter='*Redelivery*'` | 3/3 PASSED (1.5 s) | ✓ PASS |
| SC-1/2/4/5 service suites + crash matrix | `tx_pruner_test` (full) | 22/22 PASSED (32.7 s) | ✓ PASS |
| D-03 designated/refusal/bypass | `dev_mint_policy_test` (full) | 11/11 PASSED (29.4 s) | ✓ PASS |
| Substrate suites | `prune_eligibility_test` (full) | 36/36 PASSED | ✓ PASS |
| Burn-cleanup family | `transaction_manager_certificate_fallback_test` (full) | 32/32 PASSED | ✓ PASS |
| UTXO primitive | `utxo_manager_test` (full) | 34/34 PASSED | ✓ PASS |
| Unpruned-behavior + erase surface | `crdt_test` (full) | 46/47 — 1 failed (the gap) | ✗ FAIL |
| Re-delivery substrate | `globaldb_integration_test` (full) | 21/21 PASSED (1 disabled test is pre-existing: DISABLED_CommitFailsForNonexistentTopicTest in untouched globaldb_integration.cpp) | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| PRUNE-04 | 18-02 (enablement), 18-03, 18-04, 18-05, 18-06, 18-07 | Idempotent, crash-consistent prune cycle reaching a fixed point across crashes/restarts | ✓ SATISFIED | Truths 1, 2, 7, 8, 9, 10; tx_pruner_test 22/22 at HEAD; REQUIREMENTS.md marks Complete (18-04's mid-phase deferral resolved) |
| PRUNE-05 | 18-01, 18-06, 18-07 | Re-delivered/resurrected entries terminal + re-pruned; no re-fetch storm | ✓ SATISFIED | Truth 3: D-02 guard + Redelivery 3/3 + SweepReprunes green |
| RECL-03 | 18-04, 18-06 | Per-cycle observability log (examined/erased, bytes reclaimed, watermark advance) | ✓ SATISFIED | Truth 5: INFO line + Observability suite; bytes-reclaimed explicitly Phase-20-deferred per ROADMAP SC-5 (recorded as deferred, not a gap) |
| RECL-04 | 18-04, 18-06 | Prune status queryable (watermark + last-cycle stats) | ✓ SATISFIED | Truth 5: TxPruner::GetPruneStatus (tested, thread-safe) + GeniusNode::GetPruneStatus facade. Note (review IN-04): no RPC endpoint consumes the facade method yet — the queryable surface exists at code level as the requirement words it |

Orphaned requirements: none — REQUIREMENTS.md maps exactly PRUNE-04/05, RECL-03/04 to Phase 18, all claimed by plans.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| Phase diff (32 files, +6080/-107) | — | TBD/FIXME/XXX on added lines: none; TODO/HACK/PLACEHOLDER on src additions: none | — | Clean |
| src/transaction/TxPruner.cpp:286 | — | "not measured until Phase 20" | ℹ️ Info | Sanctioned design wording per SC-5, not a stub |

No file deletions in any phase commit (checked across the commit range).

### Human Verification Required

### 1. Live staging grow-then-prune demo

**Test:** On the staging net (333), enable pruning on a light node (dry_run=false, small finality window), seed prunable history via a D-03 dev-mint, spend the minted output, let the settle + finality windows elapse, and observe a full cycle.
**Expected:** The pruned tx disappears at both storage levels (CRDT value + raw block), the `/prune/tx/<hash>` marker is present, the unspent UTXO set and disk stay intact, the per-cycle INFO line reports examined/erased/watermark, and GetPruneStatus reports the watermark.
**Why human:** Requires a live staging deployment with real consensus and wall-clock windows; queued as the phase's manual UAT item per 18-VALIDATION/18-07.

### 2. CR-01 MintFunds catch-path rollback (logic-class sign-off)

**Test:** Owner review of commit 08421f54a plus the regression test `BurnCleanup_MintFundsCatchPathRemovesBurnUtxoAfterPostMoveThrow`.
**Expected:** `burn_reservation_id` is captured before the try block (TransactionManager.cpp:1044) and the catch path rolls back under it; the regression test (which drives a real post-move throw through the `SetMintFundsPostMoveExceptionHookForTest` seam after MakeSignature, i.e. strictly after the move) passes asserting memory + disk removal and re-delivery unblocked.
**Why human:** The fix protocol flags logic-error-class review findings for owner verification; the seam throw is a surrogate for natural exception sites (none exist on a valid fixture). The automated regression passes (verifier-run).

### Gaps Summary

One gap, one root cause, two truth-level manifestations (11 and 12): the WR-02 review-fix commit b0fad55b6 rewrote PruneEligibility.hpp's marker-contract documentation and incidentally removed the header's only "EraseEntry" mention. The 18-07 containment gate `EraseSurfaceGateTest.SanctionedCallerSetsAreExact` — which enumerates the sanctioned EraseEntry caller/mention file set by scanning live source — still expects `transaction/PruneEligibility.hpp` in that set and therefore fails at final HEAD (crdt_test 46/47). The post-fix consolidated verification ran only the four directly-affected ctest selections and did not include crdt_test, so the regression was not caught before submission.

Production behavior is unaffected: the found caller set ({PruneEligibility.cpp, TransactionManager.cpp, TransactionManager.hpp}) is a subset of the sanctioned expectation — no unsanctioned caller appeared; a documentation mention was removed. Every behavioral suite is green at HEAD (tx_pruner 22/22, dev_mint 11/11, prune_eligibility 36/36, certificate_fallback 32/32, utxo_manager 34/34, globaldb_integration 21/21). The fix is a one-line update of the gate's expected set (or restoring a doc mention), followed by a crdt_test re-run (expect 47/47) — but under the phase's own gates-not-promises discipline, a red containment gate at phase close is a gap, not a cosmetic note.

All other phase deliverables verified in source and by test execution: the TxPruner service cycle, GeniusNode default-off wiring with byte-identical dry-run arm, the D-02 re-delivery guard, D-03 dev-mint policy, the compose lock and marker batch-join, BLOCK_ALREADY_ERASED stranding closure, config knobs, the MintFunds cleanup with all four review fixes (CR-01, WR-01..03) confirmed in source and covered by passing regression tests.

---

_Verified: 2026-10-21T21:30:00Z_
_Verifier: Claude (gsd-verifier)_

## Gap Resolution (2026-10-21)

**Gap 1 (containment-gate stale expectation) — RESOLVED.** The WR-02 review-fix legitimately removed PruneEligibility.hpp's only EraseEntry reference (marker construction moved to TransactionManager); the gate's expected compose set was updated to the actual 3-file sanctioned set with a comment naming the cause. crdt_test 47/47 green at the fix commit. Production behavior was never affected — the gate correctly detected the set change; its expectation was stale.

Remaining human_verification items: (1) live staging grow-then-prune demo (now fully enabled: dry_run=false + dev-mint policy + corrected first-cycle timing); (2) CR-01 logic-fix sign-off (commit 08421f54a — automated regression passes; logic-class protocol).
