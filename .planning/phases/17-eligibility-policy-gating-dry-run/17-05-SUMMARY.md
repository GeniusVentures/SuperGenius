---
phase: 17-eligibility-policy-gating-dry-run
plan: 05
subsystem: transaction
tags: [prune-eligibility, block-gate, pruned-marker, dry-run, prune-03, d-04, c++17]

# Dependency graph
requires:
  - phase: 17-eligibility-policy-gating-dry-run (17-02)
    provides: GlobalDB::GetLocalDeltaKeys (elements AND tombstones, parse-at-call-time, strictly local) — the block gate's enumeration input
  - phase: 17-eligibility-policy-gating-dry-run (17-04)
    provides: PruneEligibility::Evaluate + PruneReason/PruneCandidateResult/PruneEvaluationResult contracts — the evaluator this plan extends
provides:
  - PRUNE-03 block gate EvaluateDeltaBlockErasable + PruneEvaluationResult::block_reports (erasable ⟺ every element AND tombstone key eligible + allowlist-conformant, parsed from the block itself)
  - D-04 marker contract: /prune/tx/<hash> key layout, SGTransaction::PrunedTransactionRecord proto shape, tri-state LookupPrunedEntry (PRUNED / NOT_FOUND / VALUE_PRESENT_INVARIANT_VIOLATION) on the node-local raw surface — Phase 18 writes it in the erase batch, Phase 19 answers SURV-02 from it
  - SPENDER_PRUNED_WINDOW_SATISFIED evidence code wired into Evaluate (explicit, never-silent window satisfaction for a pruned spender)
affects: [18-txpruner (marker write joins the erase WriteBatch; block_reports consulted before block deletion), 19-pruned-node-survival (SURV-02 reads LookupPrunedEntry), 17-eligibility-policy-gating-dry-run 17-06 (dry-run summary reports candidates AND block verdicts)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Affirmative-evidence reason code: every PruneReason is a failure except SPENDER_PRUNED_WINDOW_SATISFIED — eligible verdicts may carry exactly that one, keeping satisfied-window decisions explicit (D-03 repudiability extended, not weakened)"
    - "Distinct-CID block reports: batched commits put several eligible transactions into ONE block, so Evaluate gates each distinct CID once — one BlockGateResult per block, not per candidate"
    - "Test-only raw write containment: exactly one PutRaw in PruneEligibility.cpp, provably inside WritePrunedEntryForTest's body by the awk line-bracket (namespace-indented bodies defeat sed-range proofs in this repo)"
tech-patterns: []

key-files:
  created: []
  modified:
    - src/account/proto/SGTransaction.proto
    - src/transaction/PruneEligibility.hpp
    - src/transaction/PruneEligibility.cpp
    - test/src/account/prune_eligibility_test.cpp

key-decisions:
  - "SPENDER_PRUNED_WINDOW_SATISFIED is the sole non-failure reason code: the 17-04 'eligible iff reasons empty' invariant is refined to 'eligible iff no FAILURE reason' (HasFailureReason), so a marker-satisfied window is explicit without repudiating the evidence"
  - "A pruned-spender output contributes effective spend height 0 (the earliest DAG height): max-over-outputs is preserved (0 never displaces a resolved height) and the window check still requires max_height >= N — fail closed below it"
  - "block_reports deduplicated per DISTINCT eligible CID (batched commits share one block); the plan's 'for each ELIGIBLE candidate's cid' is realized as one gate run per distinct cid"
  - "Value presence in LookupPrunedEntry goes through the CRDT-layer QueryKeyValues with exact-key match (the plan's ':148-157 QueryKeyValues value-presence' note) — both reads of the PRUNED verdict share one resolution path (T-17-0503)"
  - "Task-3 RED phase vacuous by plan design (Tasks 1-2 land the contracts first, the plan's own order); non-vacuity carried by the elements-only mutation check (A4 fails) and block-presence assertions"
  - "PRUNE-03/GATE-01 checkboxes NOT ticked in REQUIREMENTS.md by this plan (17-01..04 phase-close precedent): tick at phase close"

patterns-established:
  - "Block gate consumes the block as the source of truth: GetLocalDeltaKeys at evaluation time (elements AND tombstones), 'what this node applied' never used as an oracle; parse failure fails closed in-band (DESERIALIZE_FAILED), never a crash and never an outcome::failure"

requirements-completed: []  # PRUNE-03/GATE-01 delivered here; ticked at phase close per 17-01..04 precedent (see Decisions)

# Metrics
duration: 27min
completed: 2026-10-19
---

# Phase 17 Plan 05: Block Gate + Pruned-Marker Contract Summary

**A delta block is now judged erasable only when every element AND tombstone key it carries (parsed from the block itself via GetLocalDeltaKeys) is allowlist-conformant and inside the eligible set, reported per distinct CID in block_reports; the /prune/tx/&lt;hash&gt; marker record round-trips through the node-local raw surface with a loud PRUNED/NOT_FOUND/invariant-violation tri-state; and a spender whose history was pruned counts as window-satisfied with an explicit evidence code — zero production writes anywhere**

## Performance

- **Duration:** 27 min
- **Started:** 2026-10-19T15:39:38Z
- **Completed:** 2026-10-19T16:06:30Z
- **Tasks:** 3
- **Files modified:** 4 (0 created)

## Accomplishments
- `SGTransaction.proto`: `message PrunedTransactionRecord { string tx_path; string cid; bool has_erased_at_height + uint64 erased_at_height; }` beside UTXOEntryRecord in its has_*/payload pair style (height is a best-effort diagnostic at Phase-18 write time); vendored protoc pipeline untouched, message purely additive
- `PruneEligibility` marker contract: `PRUNE_MARKER_PREFIX = "/prune/tx/"` (CRDTWorkJournal node-local prefix discipline — same shared RocksDB, direct datastore writes, never broadcast), `BuildPruneMarkerKey`, `enum class PrunedLookup` tri-state, `LookupPrunedEntry` (marker read via GetRaw; malformed payload is an ERROR result, never a silent PRUNED; value presence via the CRDT-layer QueryKeyValues exact-key match the evaluator itself enumerates; marker∧value logs ERROR and returns VALUE_PRESENT_INVARIANT_VIOLATION — never guesses), and `WritePrunedEntryForTest` carrying the "@note No production caller may invoke this method" Doxygen in the SetEraseInterruptHookForTest style — the ONLY raw write in the file, provably inside the test writer's body by the awk line-bracket gate
- `EvaluateDeltaBlockErasable` (PRUNE-03/D-C3): parses the block at evaluation time via `GetLocalDeltaKeys` (elements AND tombstones); every carried key must be `IsAllowlistedTxKey`-true AND in the eligible set (matched by tx path); misses report OUT_OF_PREFIX / NON_ELIGIBLE_SIBLING; parse failure fails closed in-band as DESERIALIZE_FAILED (T-17-0501/T-17-0504)
- `Evaluate()` integration: `PruneEvaluationResult` gained `std::vector<BlockGateResult> block_reports` — one verdict per DISTINCT eligible CID (batched commits deduplicated); tx eligibility and block erasability stay SEPARATE verdicts (a held block never flips a candidate's flag); per-cycle INFO now reports `{candidates} {eligible} {erasable}/{blocks} blocks erasable` for the 17-06 dry-run summary
- Spender-pruned refinement (D-02/B spec point 5): when `ResolveTransactionPathAndCID` fails for a spender, `LookupPrunedEntry` decides — PRUNED ⇒ the output's window counts as SATISFIED with the explicit `SPENDER_PRUNED_WINDOW_SATISFIED` evidence code (effective spend height 0, max-binding preserved); NOT_FOUND or invariant violation ⇒ SPENDER_UNRESOLVED ineligible (fail closed). The 17-04 eligible-iff-reasons-empty invariant is refined to eligible-iff-no-FAILURE-reason via `HasFailureReason` — the new code is the sole non-failure reason
- Tests (9 new, 1057-line file): BlockGate group seeds REAL batched `AtomicTransaction` blocks on the fixture topic and asserts block presence + exact carried-key set BEFORE gating (Pitfall 11) — all-eligible multi-element block erasable, live-sibling block held (NON_ELIGIBLE_SIBLING), out-of-prefix key rejected (OUT_OF_PREFIX), TOMBSTONE of a non-eligible key holds the block (Pitfall-4 closure), garbage CID fails closed; PruneMarker group — round-trip preserves tx_path/cid/height pair, no-marker-no-value NOT_FOUND, marker+value VALUE_PRESENT_INVARIANT_VIOLATION (never PRUNED), spender-pruned refinement driven through the REAL Phase-16 `EraseTransactionEntry` flow with the no-marker SPENDER_UNRESOLVED control
- Mutation check: an elements-only gate (GetLocalDeltaKeyValues projection swapped in) FAILS BlockGate_TombstoneKeyHoldsBlockNotErasable — the A4 assertion binds the Pitfall-4 gap closure; reverted, tree verified clean
- Full target 18/18 green across repeated runs on fresh per-test temp DBs (9 pre-existing 17-04 tests + 9 new — no regressions from the PruneReason extensions)

## Task Commits

Each task was committed atomically:

1. **Task 1: Marker record proto + key/lookup contract** - `a807e088d` (feat)
2. **Task 2: Block-parse gate + spender-pruned window refinement** - `e7971d5f2` (feat)
3. **Task 3: SC-2 block-gate suite + marker contract tests** - `b8f88c930` (test)

## Files Created/Modified
- `src/account/proto/SGTransaction.proto` — PrunedTransactionRecord message (additive; has_* pair style, local file style preserved — .proto is outside clang-format per plan)
- `src/transaction/PruneEligibility.hpp` — PrunedLookup enum (Phase 19 SURV-02 answer source documented), marker statics (prefix, key builder, tri-state lookup, test-only writer with the no-production-caller note), NON_ELIGIBLE_SIBLING + SPENDER_PRUNED_WINDOW_SATISFIED reason codes, BlockGateResult, PruneEvaluationResult::block_reports, EvaluateDeltaBlockErasable declaration, evidence-code doc refinements
- `src/transaction/PruneEligibility.cpp` — marker implementations (static MarkerLogger for the static members), EvaluateDeltaBlockErasable, Evaluate block-report integration with distinct-CID dedup, spender-pruned refinement with HasFailureReason-qualified eligibility, templated RecordReason over both verdict structs
- `test/src/account/prune_eligibility_test.cpp` — +480 lines: SeedTransactionsInOneBlock / SeedBlockWithOutOfPrefixKey / SeedBlockWithTombstone / PutSeedValue / SeedTxCidEntry helpers, BlockGateResult HasReason + FindBlockReport, Group D (5 tests), Group E (4 tests)

## Decisions Made
- **Evidence-code semantics:** the plan requires "eligible with SPENDER_PRUNED_WINDOW_SATISFIED" while 17-04 pinned "eligible only with an empty reasons vector" — reconciled by classifying the new code as the sole affirmative-evidence reason (HasFailureReason treats everything else as failure). The 17-04 tests stay green unchanged (their eligible verdict carries no evidence code), and the satisfied-window decision remains auditable (D-03).
- **Effective spend height 0 for pruned-spender outputs:** a pruned spender's own window elapsed by construction (it passed the same gate before erasure), so its spend is at least as old as any surviving height; 0 is the earliest representable height, preserves max-over-outputs, and keeps the window check honest (max_height >= N still required — fail closed on shallow chains).
- **One report per distinct CID:** two eligible transactions batched into one block produce ONE BlockGateResult (the plan's per-candidate phrasing would duplicate identical verdicts for the shared block); BlockGateResult.cid identifies the block.
- **Value presence via QueryKeyValues exact-key match:** the raw surface cannot answer CRDT-value presence (values live at /crdt/k/<key>/v under CrdtSet's layout ownership), so the lookup queries with the record's tx_path and compares the exact key string — the same resolution path the evaluator enumerates (T-17-0503).
- **Neutral out-of-prefix key in A3:** "/prune-test/out-of-prefix" instead of a /cert/ or reg/ key — storing garbage under live subsystem namespaces in the fixture risks consensus/registration scans picking it up; the allowlist rejection itself is prefix-agnostic and already proven for crafted cert/reg keys in 17-04 B3.
- **Erase ordering in B4:** the driver topic put lands BEFORE both EraseTransactionEntry calls so no erased CID is a topic head (the Phase-16 guard would reject the erase and leave the tx→CID entry resolvable, voiding the test).

## Deviations from Plan

None - plan executed exactly as written.

### Documented protocol notes (no auto-fixes)

**1. [TDD-ordering note] Task 3 RED phase vacuous by plan design**
- **Found during:** Task 3 (tdd="true")
- **Issue:** the tests bind the Task 1-2 contracts, and the plan's own task order mandates those commits first — a failing-tests-first commit has no tree to compile against (same as 17-01/02/04 Task 3).
- **Resolution:** single test(17-05) commit after implementation. Non-vacuity carried by the elements-only mutation check (A4 fails: the tombstone-holding block would wrongly pass) and by the block-presence + exact-carried-key assertions that fail against any vacuously-empty gate input.

**Total deviations:** 0 auto-fixed, 1 protocol note documented
**Impact on plan:** None — every acceptance criterion landed as specified.

## Verification Log

| Gate | Result | Notes |
|------|--------|-------|
| Task 1: build genius_transaction_objs | exit 0, zero warnings (after one in-flight namespace-qualification fix) | PASS |
| Task 1: grep counts (PrunedTransactionRecord\|PRUNE_MARKER_PREFIX\|LookupPrunedEntry hpp / proto) | 5 / 1; generated pb.h carries the message | PASS |
| Task 1: WritePrunedEntryForTest callers in src/ | only PruneEligibility.{hpp,cpp} (definition + Doxygen) | PASS |
| Task 1: format gate (changed C++ lines) | canonical after apply; .proto deliberately excluded (plan: not clang-format territory) | PASS |
| Task 1: tidy gate (clang-tidy 22, single-arch filtered db) | no new substantive findings; MarkerLogger const fix applied; residual findings all pre-existing 17-04 lines / degraded-config noise | PASS |
| Task 2: build genius_transaction_objs | exit 0, zero warnings | PASS |
| Task 2: grep GetLocalDeltaKeys\|NON_ELIGIBLE_SIBLING\|SPENDER_PRUNED_WINDOW_SATISFIED (cpp / hpp) | 5 / 7 | PASS |
| Task 2: PutRaw count in PruneEligibility.cpp | exactly 1 | PASS |
| Task 2: awk line-bracket containment | s=197 (WritePrunedEntryForTest def), p=208 (PutRaw), e=211 (next member def) — contained; re-verified post-format | PASS |
| Task 2: format + tidy gates | canonical; no new substantive findings (optional-access + use-auto findings eliminated by restructuring the integration loop) | PASS |
| Task 3: build prune_eligibility_test | exit 0, zero warnings | PASS |
| Task 3: `--gtest_filter='*BlockGate*:*PruneMarker*'` | 9/9 PASSED | PASS |
| Task 3: full target double-run | 18/18 PASSED twice (and again post-format, post-mutation-revert) | PASS |
| Task 3: mutation check (elements-only gate) | BlockGate_TombstoneKeyHoldsBlockNotErasable FAILS as required; reverted; `git status` clean of stray changes before commit | PASS |
| Task 3: marker-writer callers in src/ post-tests | zero outside PruneEligibility.{hpp,cpp} | PASS |
| Task 3: format gate (test file) | canonical | PASS |
| Plan: whole-diff format gate vs 7908e79be (all three code files) | "clang-format did not modify any files" | PASS |
| Plan: write-token scan PruneEligibility.cpp (Erase\|Put\|Stage\|Delete families) | exactly 1 (the contained PutRaw) | PASS |
| Plan: post-commit deletion check (all 3 commits) | 0 tracked deletions each | PASS |
| Plan: no new CMake targets / link changes | none needed — existing targets only | PASS |
| Build-system pre-PR checklist walk | additive proto message + in-place source extension only; no build wiring touched | PASS |

## Threat Model Disposition

- **T-17-0501 (Tampering, gate missing tombstone keys):** mitigated — the gate consumes GetLocalDeltaKeys (both lists by 17-02 contract); TombstoneKeyCounts/A4 proves a tombstone-of-non-eligible-key block is rejected, and the mutation check proves the test binds (an elements-only gate fails it).
- **T-17-0502 (Tampering, forged/malformed marker):** mitigated — ParseFromArray failure is an ERROR outcome, never a silent PRUNED; NOT_FOUND stays the no-marker default.
- **T-17-0503 (Tampering/invariant, marker+value coexistence):** mitigated — VALUE_PRESENT_INVARIANT_VIOLATION tri-state with an ERROR log; B3 asserts the enum (and non-PRUNED); both reads share one resolution path.
- **T-17-0504 (Tampering, non-tx key smuggled into a block):** mitigated — per-key IsAllowlistedTxKey conformance inside the gate; A3 proves OUT_OF_PREFIX on a real committed block.
- **T-17-0505 (Elevation, marker write escaping the test boundary):** mitigated — Doxygen no-production-caller note; PutRaw count is exactly 1 and the awk line-bracket proves it lives inside WritePrunedEntryForTest; src/ grep shows zero other callers; the production write remains Phase 18's batch participant.

## Issues Encountered
None blocking. One environment note carried from 17-04: building targets directly works because all targets already existed this plan (no new-target cmake regen needed).

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 17-06 (dry-run wiring) can report both verdict families from one `PruneEvaluationResult`: candidates (PRUNE-01) + block_reports (PRUNE-03) — the INFO summary already prints the erasable-block count.
- Phase 18's TxPruner writes the marker via the same WriteBatch as EraseTransactionEntry (WritePrunedEntryForTest's serialization shape is the reference) and consults block_reports before deleting any block.
- Phase 19's SURV-02 answers "history pruned" from LookupPrunedEntry's PRUNED-vs-NOT_FOUND distinction.
- No blockers. Wave-3 sibling 17-06 is unaffected (no file overlap — it touches GeniusNode wiring).

## Self-Check: PASSED

- Files exist: src/account/proto/SGTransaction.proto, src/transaction/PruneEligibility.hpp, src/transaction/PruneEligibility.cpp, test/src/account/prune_eligibility_test.cpp — FOUND
- Commits exist: a807e088d, e7971d5f2, b8f88c930 — FOUND in git log
- All task acceptance criteria re-run and passing (see Verification Log)

---
*Phase: 17-eligibility-policy-gating-dry-run*
*Completed: 2026-10-19*
