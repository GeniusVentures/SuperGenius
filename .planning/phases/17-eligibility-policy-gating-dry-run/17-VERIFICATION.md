---
phase: 17-eligibility-policy-gating-dry-run
verified: 2026-10-19T19:40:00Z
status: human_needed
score: 9/10 must-haves verified
overrides_applied: 0
gaps:
  - truth: "The dry-run timer cycle survives transient evaluation errors (WR-02 contract): a non-permanent Evaluate() failure WARNs and falls through to the reschedule, keeping the 10-minute cadence alive — the cycle only evaluates and INFO-summarizes"
    status: failed
    reason: >-
      On a transient Evaluate() error (e.g. std::errc::operation_canceled from
      GetCRDTHeadList when ActiveCRDTDataStore() is momentarily null across a
      GlobalDB restart/generation switch), RunPruneDryRunCycle logs the WARN at
      GeniusNode.cpp:4969 and then falls through to `const auto &result =
      evaluation.value();` at :4973 on an ERRORED outcome::result. Boost.Outcome's
      wide_value_check throws bad_result_access for a result without a value; the
      exception escapes the asio async_wait completion handler into the bare
      `ctx->run()` io-thread loop (GeniusNode.cpp:380-385 has no try/catch),
      terminating the node. The WR-02 fix documented in 17-REVIEW-FIX.md ("warns
      and falls through to the reschedule block, keeping the 10-minute cadence
      alive") is falsified in source: the INFO-summary block sits between the WARN
      and the reschedule and unconditionally dereferences the errored result.
      Reachable in production exactly under the transient conditions CR-01/WR-02
      were written for. No unit seam exists (GeniusNode is not unit-constructible),
      so the phase suites cannot catch it.
    artifacts:
      - path: "src/account/GeniusNode.cpp"
        issue: "Lines 4953-4986: transient-error branch falls through into evaluation.value() on an errored outcome; throws bad_result_access out of the io-thread handler instead of rescheduling"
    missing:
      - "Guard the summary emission so it only runs on evaluation success (wrap the result/eligible/erasable_blocks/INFO block in `if ( !evaluation.has_error() )`, or skip to the reschedule on the transient path) — the WARN + reschedule behavior WR-02 documented then holds"
      - "Re-verify the WR-02 claim in 17-REVIEW-FIX.md / 17-REVIEW.md after the fix lands"
deferred:
  - truth: "Marker production writes (the /prune/tx/ write side of D-04)"
    addressed_in: "Phase 18"
    evidence: "Phase 18 goal: 'The TxPruner service performs the first real erasure'; SC-1 ordered erase flow — the marker write joins the Phase-16 erase WriteBatch (by design this phase delivers format + lookup only)"
  - truth: "Real erasure / TxPruner service replacing the dry-run timer"
    addressed_in: "Phase 18"
    evidence: "Phase 18 goal and SC-4: 'GeniusNode constructs/starts/stops the pruner per config, and default-off config erases nothing'"
  - truth: "PRUNED-vs-NOT_FOUND answer surfacing to history queries (SURV-02)"
    addressed_in: "Phase 19"
    evidence: "17-CONTEXT.md deferred section: 'Queryable dry-run/operator report — Phase 18 (RECL-04)'; SURV-02 mapped to Phase 19 in REQUIREMENTS.md traceability"
---

# Phase 17: Eligibility, Policy & Gating (Dry-Run) Verification Report

**Phase Goal:** The pruning policy evaluator decides erasability correctly on live data in dry-run — never erasing in production — and the two correctness dependencies of the first real prune (multi-element block gate, own-frontier survival fix) are implemented and proven before any production erasure exists.
**Verified:** 2026-10-19T19:40:00Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

Must-haves merged from ROADMAP SC-1..SC-5 (non-negotiable) plus the six PLAN frontmatter truth sets. SC-1's "priority vs maxHeight" wording was read through the CONTEXT.md D-02 supersession (MOST-RECENT SPENDER's priority depth — spend recency, never tx creation-age priority).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | SC-1/PRUNE-01: evaluator reports EXACTLY the truly-eligible set on a live fixture — every output genuinely consumed AND most-recent spend beyond the window (maxHeight − max spender priority ≥ N) | ✓ VERIFIED | `PruneEligibility.cpp` Evaluate/EvaluateCandidate (:229-495): trailing-slash enumeration (:252), D-C2 discriminator (:413), existence-before-priority (:430→:458), max-over-outputs (:467), single window home (:485-486). `ExactEligibleSet_ThreeWayWindowAndSpendDiscriminators` asserts sorted-hash set equality plus per-item reasons; ran green in this session (22/22) |
| 2 | SC-1 counterexample (D-02 binding): an ancient-creation tx spent recently is NEVER eligible — the window reads only spender priority | ✓ VERIFIED | Source-structural: the only `GetKeyPriority` call is on the SPENDER's resolved path (:458); the candidate's own priority is never read. Test A2 pins counterexample → SPEND_WITHIN_WINDOW; 17-04 mutation 2 documented the binding (creation-age regression fails the exact-set assertion) |
| 3 | SC-2/PRUNE-03: delta block erasable only when EVERY element AND tombstone key is allowlist-conformant and in the eligible set — parsed from the block itself at evaluation time | ✓ VERIFIED | `EvaluateDeltaBlockErasable` (:328-368) consumes `GetLocalDeltaKeys` (both lists per 17-02 contract, impl crdt_datastore.cpp:2320-2340), per-key allowlist + eligible-set membership, parse failure fails closed DESERIALIZE_FAILED. BlockGate group 5/5 green incl. TombstoneKeyHoldsBlockNotErasable (Pitfall-4 closure; 17-05 mutation check binds it) |
| 4 | SC-3/GATE-04: own-frontier pruned + cert present → previous_hash resolves from cert slot; cold-start nonce chain correct; fail-closed "" negative; cert-rung == CRDT-scan | ✓ VERIFIED | `GetRegisteredOutgoingPreviousHash` (TransactionManager.cpp:1688-1728): type restriction dropped (guard now `has_error() \|\| !value()` only), all four binding checks verbatim, fail-closed "". 5 CertSlotPreviousHash tests drive the REAL Phase-16 `EraseTransactionEntry`; case 4 captures the rung directly via friend accessor + scan equivalence; 28/28 green this session |
| 5 | SC-4/GATE-01: fails closed — Light-only construction (Full/Archive refuse); one allowlist home (`/bc-<net>/tx/` via TRANSACTION_BASE_FORMAT base); out-of-prefix keys rejected and ctest-asserted | ✓ VERIFIED | `New()` rejects non-Light with operation_not_permitted (PruneEligibility.cpp:108-114); `IsAllowlistedTxKey` is the single home (:119-133, trailing slash); `FailClosedGates_ConstructionRefusesFullAndArchive` + `_AllowlistRejectsCraftedKeys` (cert slot, reg path, /bc-999/, slashless txfoo, proof) green |
| 6 | SC-5/GATE-06: pruning knobs beside node_type in sgns_config.json, default off, resolved values INFO-logged; absent keys behave exactly as today; enabled-without-window fail-closes (incl. window=0, WR-03) | ✓ VERIFIED | LoadSgnsConfig pruning block entirely inside `HasMember("pruning") && IsObject()` guard (GeniusNode.cpp:478-488) — absent path executes zero new statements; delegation to `PruningConfig::FromJSON` (single parse home); WR-03 zero-rejection at read (PruneEligibility.cpp:84-88); PruningConfig tests 6/6 green incl. both WR-03 regressions |
| 7 | 17-01 carrier: spent_by_txid populated at all four production spend paths and both ConsumeUTXOs branches; survives bucket rewrite + LoadUTXOs restart; revert resets to nullopt; public GetOutPointSpender | ✓ VERIFIED | UTXOManager.cpp:291 (genuine branch), :311 (synthesized initializer), :392 (revert reset), :787-796 (accessor, shared_lock); persist/restore :936-943/:1052-1056. All four call sites pass the spender with WR-01 guarded parses (TransactionManager.cpp:2643-2652 transfer, :2671-2682 migration, :2739-2745 mint-v2, :2905-2917 escrow). 4 SpentByTxid tests green in the 34/34 suite |
| 8 | 17-02 exposures: per-key priority read chain GlobalDB→CrdtDatastore→CrdtSet; GetLocalDeltaKeys (elements AND tombstones) at both levels; GetLocalDeltaKeyValues precedent byte-untouched; all read-only | ✓ VERIFIED | crdt_datastore.cpp:2260-2262 (`return set_->GetPriority`), :2320-2340 (both lists); globaldb.cpp:902 (GetKeyPriority), :1015-1023 (GetLocalDeltaKeys); `git diff cd2c7faa7..HEAD -- impl/crdt_datastore.cpp` shows 0 deleted lines (precedent body untouched); 4/4 passthrough tests green on a real store |
| 9 | Dry-run NEVER erases: evaluator and node wiring contain zero erase/stage/write tokens; EraseTransactionEntry production callers still zero; the only raw write is the test-only marker writer | ✓ VERIFIED | grep EraseEntry\|EraseTransactionEntry\|PutRaw\|StageErase\|StageBlock\|DeleteKey\|DeleteTransaction on PruneEligibility.cpp = exactly 1 PutRaw inside `WritePrunedEntryForTest` (:215-227, no-production-caller Doxygen); GeniusNode.cpp prune surface = 0 matches; WritePrunedEntryForTest src/ callers = definition only |
| 10 | 17-06/WR-02: the dry-run cycle is a config-gated repeating timer that only evaluates and INFO-summarizes; transient evaluation errors WARN and reschedule (10-min cadence survives); dry_run=false starts nothing | ✗ FAILED | Config gating, per-tick snapshot construction (CR-01), and the dry_run=false WARN+return all hold (GeniusNode.cpp:4862-4911, 4913-4943). BUT the WR-02 transient-error fall-through dereferences the errored outcome: WARN at :4969 → `evaluation.value()` at :4973 → bad_result_access thrown out of the bare `ctx->run()` io thread (:380-385) → process termination instead of reschedule. Falsifies the documented fix contract in 17-REVIEW-FIX.md |

**Score:** 9/10 truths verified

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Marker production writes (D-04 write side) | Phase 18 | Phase 18 goal "TxPruner Service & First Real Prune"; SC-1 ordered erase flow |
| 2 | Real erasure / TxPruner service | Phase 18 | Phase 18 SC-4 config-gated pruner wiring |
| 3 | PRUNED-vs-NOT_FOUND answer surfacing (SURV-02) | Phase 19 | SURV-02 mapped to Phase 19; 17-CONTEXT deferred section |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/transaction/PruneEligibility.hpp` | contracts: PruneReason (10 codes), result structs, PruningConfig, evaluator class (min 120 lines) | ✓ VERIFIED | 452 lines; all contracts Doxygen-documented incl. CID_UNRESOLVED (WR-05) and marker tri-state |
| `src/transaction/PruneEligibility.cpp` | Evaluate() implementation: enumeration, window, gates (min 150 lines) | ✓ VERIFIED | 507 lines; all review fixes WR-03/WR-04/WR-05 present in source |
| `test/src/account/prune_eligibility_test.cpp` | SC-1 exact-set + SC-4 fail-closed + config + block-gate + marker suites (min 250 lines) | ✓ VERIFIED | 1110 lines, 22 tests (verified via --gtest_list_tests), all green |
| `test/src/crdt/crdt_priority_delta_keys_test.cpp` | passthrough tests on a real store (min 80 lines) | ✓ VERIFIED | 292 lines, 4 tests green |
| `src/account/UTXOManager.hpp/.cpp` | ConsumeUTXOs 4th param + GetOutPointSpender + spent_by_txid population | ✓ VERIFIED | Decl :201/:417; population :291/:311; reset :392; accessor :787-796 |
| `src/crdt/crdt_datastore.hpp` + `impl/crdt_datastore.cpp` | GetPriority + GetLocalDeltaKeys declarations/impl | ✓ VERIFIED | :276/:312 decls; :2260/:2320 impls; precedent untouched |
| `src/crdt/globaldb/globaldb.hpp/.cpp` | GetKeyPriority + GetLocalDeltaKeys facade | ✓ VERIFIED | :330-371 decls; :902/:1015 impls |
| `src/account/proto/SGTransaction.proto` | PrunedTransactionRecord message | ✓ VERIFIED | :70-76, has_* pair style, additive |
| `src/account/GeniusNode.hpp/.cpp` | pruning config member + timer + cycle + config block | ✓ VERIFIED | hpp :33/:1169/:1179/:1278-1291; cpp :473-488, :1157, :4862-5007, shutdown cancels :2456-2460/:2545-2548 — one gap inside the cycle body (truth 10) |
| `test/src/account/utxo_manager_test.cpp` | Candidate-B unit tests (min 80 lines) | ✓ VERIFIED | 833 lines; 4 SpentByTxid TEST_F cases; 34/34 green |
| `test/src/account/transaction_manager_certificate_fallback_test.cpp` | SC-3 proof family (min 100 lines) | ✓ VERIFIED | 1680 lines; 5 CertSlotPreviousHash cases via real EraseTransactionEntry; 28/28 green |
| `.planning/REQUIREMENTS.md` | D-02-authorized out-of-scope-row amendment | ✓ VERIFIED | :70 amended row + :117 footer note; exactly one row changed |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| TransactionManager.cpp (4 call sites) | ConsumeUTXOs 4th param | explicit spender hash | ✓ WIRED | Post-WR-01 shape: guarded `fromReadableString` parse then `<tx>id.value()` passed — all four sites read directly (:2649, :2679, :2738, :2916). Plan-time awk pattern no longer matches by design (WR-01 moved the parse out of the call statement) |
| ConsumeUTXOs | UTXOEntry::spent_by_txid | assignment both branches | ✓ WIRED | :291 assignment + :311 initializer; revert reset :392 |
| PruneEligibility::Evaluate | QueryKeyValues(base + "tx/") | trailing-slash enumeration | ✓ WIRED | PruneEligibility.cpp:252 |
| window check | GetKeyPriority(spender path) + GetCRDTHeadList maxHeight | spend_height = max over outputs | ✓ WIRED | :458 + :239-245; existence established first via ResolveTransactionPathAndCID (:430) |
| block gate | GlobalDB::GetLocalDeltaKeys(cid) | elements AND tombstones vs eligible ∩ allowlist | ✓ WIRED | :338-364 |
| LookupPrunedEntry | raw surface "/prune/tx/" + value presence | node-local GetRaw/QueryKeyValues | ✓ WIRED | :142-213 with WR-04 path validation |
| GeniusNode::LoadSgnsConfig | PruningConfig::FromJSON | single-parse-home delegation | ✓ WIRED | GeniusNode.cpp:480 |
| dry-run cycle | PruneEligibility::Evaluate | per-tick construction + INFO summary | ✓ WIRED (with truth-10 defect on the error path) | :4953-4986 |
| cert rung | GetCertificateBySlot | generalized rung 3 | ✓ WIRED | TransactionManager.cpp:1706 |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| PruneEligibility::Evaluate | PruneEvaluationResult | live GlobalDB QueryKeyValues/GetKeyPriority/GetLocalDeltaKeys on real stores | Yes (real-store fixtures; per-key priorities pinned in test) | ✓ FLOWING |
| GetOutPointSpender | spent_by_txid | ConsumeUTXOs at 4 production spend paths; persisted/ restored via snapshot | Yes (round-trip test survives rewrite+restart) | ✓ FLOWING |
| dry-run INFO summary | result counts | Evaluate() on live members under SnapshotAccountServices | Yes (happy path); transient-error path broken (truth 10) | ⚠️ STATIC on error path |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| SC-1/SC-4/PRUNE-03/marker + review regressions | `./build/OSX/Release/test_bin/prune_eligibility_test` | 22/22 PASSED (binary built 15:32 > last fix commit 15:27 local — post-fix tree) | ✓ PASS |
| spent_by_txid carrier (17-01) | `./build/OSX/Release/test_bin/utxo_manager_test` | 34/34 PASSED | ✓ PASS |
| cert-slot generalization (17-03) | `./build/OSX/Release/test_bin/transaction_manager_certificate_fallback_test` | 28/28 PASSED | ✓ PASS |
| CRDT read exposures (17-02) | `./build/OSX/Release/test_bin/crdt_test --gtest_filter='*PriorityPassthrough*:*DeltaKeys*'` | 4/4 PASSED | ✓ PASS |
| Full post-fix ctest | `cmake --build build/OSX/Release -j8 && ctest --test-dir build/OSX/Release --output-on-failure -j8` | exit 0 — full rebuild green + entire suite green (1129.66s real, incl. requires_foundry 9 tests/475.94s); matches 17-06's documented 152/152 pre-fix run | ✓ PASS |

### Probe Execution

Step 7c: SKIPPED — no `scripts/*/tests/probe-*.sh` exist and no probe is declared in any PLAN/SUMMARY for this phase.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|---------------------|----------|
| PRUNE-01 | 17-01, 17-02, 17-04 | genuine-consumed AND depth beyond finality window | ✓ SATISFIED | Truths 1, 2, 7, 8; checkbox ticked at phase close (this verification) |
| PRUNE-03 | 17-02, 17-05 | block erased only when every carried element eligible (parse at prune time) | ✓ SATISFIED | Truth 3; checkbox ticked |
| GATE-01 | 17-04, 17-05 | Light-only + /bc-<netid>/tx/ allowlist | ✓ SATISFIED | Truth 5; checkbox ticked |
| GATE-04 | 17-03 | previous_hash survives own-frontier pruning (cert-slot) | ✓ SATISFIED | Truth 4; checkbox ticked |
| GATE-06 | 17-04, 17-06 | config knobs beside node_type, default off, INFO-logged | ✓ SATISFIED | Truth 6; checkbox ticked |

Orphaned requirements: none — REQUIREMENTS.md traceability maps exactly these five IDs to Phase 17. REQUIREMENTS.md checkboxes and traceability rows ticked per the documented phase-close convention (all six executors deferred to phase close; substance confirmed above).

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/account/GeniusNode.cpp | 4969-4973 | WR-02 transient-error fall-through dereferences errored `evaluation.value()` — throws bad_result_access out of bare `ctx->run()` io thread | 🛑 Blocker (gap, truth 10) | Node termination under transient GlobalDB restart during a dry-run tick; falsifies the documented review-fix contract |
| src/account/GeniusNode.cpp | 487 | IN-01: logs fabricated `finality_window=0` when unset | ℹ️ Info | Deferred review finding, out of fix scope — cosmetic log ambiguity |
| src/transaction/PruneEligibility.cpp + GeniusNode.cpp | 320 / 4979 | IN-02: duplicate per-cycle INFO summaries (evaluator + node) | ℹ️ Info | Deferred review finding — DRY/log noise |
| src/account/GeniusNode.cpp | 4926-4963 | IN-03: whole-store evaluation synchronous on io-pool thread | ℹ️ Info | Deferred review finding — scaling note for Phase 18 |
| src/crdt/globaldb/globaldb.cpp | 902 vs 1015 | IN-04: sibling facades return different errors for the same null-datastore failure | ℹ️ Info | Deferred review finding — API consistency |
| src/transaction/TransactionManager.cpp | 1293, 2748, 2952 | pre-existing unchecked `.value()` on fromReadableString (out of phase scope, flagged by WR-01 fixer for follow-up) | ℹ️ Info | Pre-existing; not introduced by this phase |

Debt-marker gate: zero TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER hits in the phase-created files and the phase-modified prune/timer/cert-rung regions (the crdt_datastore.cpp:2307 TODO is the pre-existing precedent's own comment inside the untouched GetLocalDeltaKeyValues body).

### Human Verification Required

#### 1. CR-01 concurrency audit confirmation (source audit passed; recommend human concurrence)

**Test:** Review `RunPruneDryRunCycle` (GeniusNode.cpp:4913-4943) and `SnapshotAccountServices` against the Phase-13 generation-switch paths.
**Expected:** The snapshot-under-lifecycle_mutex read plus held shared_ptrs is sound; specifically confirm the documented invariant that `tx_globaldb_` is init-once (InitDatabase) and reset only after io drain, so its unlocked read in the cycle cannot race.
**Why human:** Concurrent lifecycle behavior has no unit seam; the invariant claim spans call sites this verification read but did not execute.

#### 2. Dry-run timer end-to-end on a live node

**Test:** Run a light node with `{"pruning": {"enabled": true, "finality_window": 100, "dry_run": true}}` and observe the 1-minute-then-10-minute INFO summary cycle; also start with the pruning object absent and diff log output against a pre-phase-17 node.
**Expected:** Per-cycle "Prune dry-run: N candidate(s)..." line; absent-config node produces zero new log lines and identical behavior (SC-5).
**Why human:** Requires a running node process; no unit surface exists for GeniusNode.

### Gaps Summary

One gap blocks clean phase close. The phase's substantive deliverables — the eligibility evaluator (SC-1 with the D-02 spend-recency semantics structurally enforced), the multi-element block gate (SC-2), the cert-slot own-frontier fix (SC-3), the fail-closed gating (SC-4), and the config surface (SC-5) — are all implemented, wired, and suite-proven on the post-review-fix tree (22/22 + 34/34 + 28/28 + 4/4 rerun in this session against binaries newer than the last fix commit). The never-erase invariant holds by grep-proof and by construction.

The single failure is a one-block logic defect introduced by the WR-02 review-fix commit (120fffba2): the transient-error branch in `RunPruneDryRunCycle` falls through into the INFO-summary block, which calls `evaluation.value()` on the errored result — throwing `bad_result_access` off the unprotected io thread (`ctx->run()`, GeniusNode.cpp:382-385) and terminating the node, instead of rescheduling as the fix report claims. It is exactly the scenario WR-02 was written to fix (operation_canceled during a GlobalDB restart), it is reachable only in production runtime (no unit seam), and the fix is a one-line guard around the summary block. The 4 Info-tier review findings (IN-01..IN-04) remain deferred as documented and are non-blocking; pre-existing unchecked `.value()` sites flagged by the fixer are out of phase scope.

---

_Verified: 2026-10-19T19:40:00Z_
_Verifier: Claude (gsd-verifier)_

## Gap Resolution (2026-10-19)

**Gap 1 (WR-02 transient fall-through crash) — RESOLVED.** The summary block is now guarded by `if ( !evaluation.has_error() )` (fix commit follows the review-fix series); the transient path WARNs and skips to the reschedule, exactly the documented WR-02 contract. Verified: genius_node rebuild clean, git-clang-format clean on the fix, prune_eligibility/utxo_manager/certificate_fallback/crdt suites 4/4 green post-fix. Remaining: the two human_verification items below (CR-01 concurrency concurrence review; live-node dry-run timer observation).
