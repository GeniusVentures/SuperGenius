---
phase: 17-eligibility-policy-gating-dry-run
plan: 04
subsystem: database
tags: [prune-eligibility, spend-recency-window, dry-run, allowlist, pruning-config, c++17]

# Dependency graph
requires:
  - phase: 17-eligibility-policy-gating-dry-run (17-01)
    provides: GetOutPointSpender + spent_by_txid carrier (the evaluator's spend signal)
  - phase: 17-eligibility-policy-gating-dry-run (17-02)
    provides: GlobalDB::GetKeyPriority read chain (the evaluator's depth input, with the 0-for-missing caveat)
provides:
  - Read-only PRUNE-01 evaluator PruneEligibility::Evaluate() — per-candidate structured verdicts (PruneReason codes), the ONE home of the spend-recency window arithmetic
  - PruningConfig + FromJSON parse/validate contract (GATE-06 policy half; enabled-without-window fail-closes) for 17-06 GeniusNode wiring
  - SC-1 exact-set proof (incl. the owner's ancient-creation/recent-spend counterexample) and SC-4 fail-closed proof (Full/Archive refuse; 5 crafted out-of-prefix keys rejected)
affects: [18-txpruner (reuses Evaluate() verbatim as its work-list source), 17-eligibility-policy-gating-dry-run 17-06 (PruningConfig consumer)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Existence-before-priority: ResolveTransactionPathAndCID establishes the spender's tx->CID entry BEFORE GetKeyPriority, because a missing key reads priority 0 and would satisfy the window vacuously (T-17-0403, mutation-verified)"
    - "Pure evaluator over committed state: enumeration + verdicts only; zero write/erase/stage tokens, enforced by grep gate"
    - "Priority layout in tests: put order on a real topic sets each key's CRDT priority; the LAST put defines maxHeight, so 'recent spender' = last put and 'ancient spender' = put far earlier"
tech-patterns: []

key-files:
  created:
    - src/transaction/PruneEligibility.hpp
    - src/transaction/PruneEligibility.cpp
    - test/src/account/prune_eligibility_test.cpp
  modified:
    - src/transaction/CMakeLists.txt
    - src/transaction/TransactionManager.hpp
    - test/src/account/CMakeLists.txt

key-decisions:
  - "friend PruneEligibility on TransactionManager (TransactionConsensusHandler precedent): the plan's interface list called GetMonitoredNetworkIDs and ResolveTransactionPathAndCID public, but both are private — friendship keeps the public contract small instead of widening it"
  - "The window arithmetic lives ONLY in EvaluateCandidate/Evaluate (one home): maxHeight − max(spender priorities) ≥ N, reading the SPENDER's priority never the candidate's — mutation 2 (creation-age regression) makes the counterexample eligible and the exact-set assertion fails, proving the semantics bind"
  - "Empty produced-outputs (has_value but empty dst vector, e.g. 0-output spenders) fail closed as NO_OUTPUTS — vacuous all-outputs-spent must not make a tx eligible (Rule 2)"
  - "Evaluate() itself fails closed (invalid_argument) when no finality_window is configured: a windowless report could never prove anything beyond-window"
  - "SPENDER_UNRESOLVED covers both an unresolvable spender entry AND a failed priority read after resolution — kept the plan's 7 codes rather than adding an 8th; doc comment says so"
  - "PRUNE-01/GATE-01 checkboxes NOT ticked in REQUIREMENTS.md by this plan (17-01/02/03 phase-close precedent): tick at phase close"

patterns-established:
  - "Structured eligibility verdict: per-candidate PruneReason codes recorded not dropped; eligible ONLY with an empty reasons vector (repudiability, D-03)"

requirements-completed: []  # PRUNE-01/GATE-01 delivered here; checkboxes ticked at phase close per 17-01..03 precedent (see Decisions)

# Metrics
duration: 26min
completed: 2026-10-19
---

# Phase 17 Plan 04: D-03 Pure Structured-Result Eligibility Evaluator Summary

**PruneEligibility evaluates the /bc-\<net\>/tx/ namespace read-only — every output genuinely consumed (D-C2) AND the MOST-RECENT spend beyond the finality window (D-02: maxHeight − max spender priority ≥ N, spender existence established BEFORE any priority read) — returning per-candidate reason codes, with Light-only construction, a single allowlist home, and the GATE-06 PruningConfig contract**

## Performance

- **Duration:** 26 min
- **Started:** 2026-10-19T15:05:22Z
- **Completed:** 2026-10-19T15:31:00Z
- **Tasks:** 3
- **Files modified:** 6 (3 created)

## Accomplishments
- `PruneEligibility.hpp` (282 lines): scoped `PruneReason` (7 fail-closed codes, per-value docs in the GlobalDB::Error style), `PruneCandidateResult` / `PruneEvaluationResult` (named-evidence discipline + EligibleCount), `PruningConfig::FromJSON` (per-key type-checked reads; enabled-without-finality_window forces disabled with a fixed disabled_reason string), `PruneEligibility` with `New()` (rejects Full/Archive with operation_not_permitted — GATE-01) and `IsAllowlistedTxKey` (THE one allowlist home: exact `GetBlockChainBase(net) + "tx/"` startswith over `GetMonitoredNetworkIDs()` only, trailing slash mandatory — D-C7)
- `Evaluate()` (read-only): per-network `QueryKeyValues(base + "tx/")` WITH the trailing slash (the slashless QueryTransactions :2477 form is the documented anti-pattern); query/key errors log-and-continue, the scan never aborts. Per candidate: allowlist → deserialize (fail ⇒ DESERIALIZE_FAILED) → outputs (nullopt/empty ⇒ NO_OUTPUTS) → per output genuine-consumption (D-C2) → spender recorded → `ResolveTransactionPathAndCID` existence BEFORE `GetKeyPriority` (T-17-0403) → spend_height = MAX spender priority (most-recent binds, Pitfall 3) → window `maxHeight − spend_height ≥ N`. Zero writes; read-only grep gate = 0 matches
- `prune_eligibility_test.cpp` (583 lines, 9 tests, target registered copying the certificate-fallback link set): (A) ExactEligibleSet — 11 candidates seeded through the real topic Put with real per-key priorities; eligible set asserted EXACTLY the one ancient-spent tx via sorted-hash set equality; the owner's counterexample (ANCIENT creation + RECENT spend) is ineligible SPEND_WITHIN_WINDOW — creation-age regression is structurally impossible (the window never reads the candidate's own priority); split-time max-binds with `spend_height == priority(S_R)` pinned; tombstone/unspent/no-spender/ghost-spender/no-output cases each carry their explicit reason; (B) FailClosedGates — Full/Archive construction refuses, windowless Evaluate fails, five crafted keys rejected (cert slot, reg path, /bc-999/, slashless txfoo, proof) and a monitored-net tx key accepted; garbage-value case proves DESERIALIZE_FAILED + scan-continues (T-17-0405); (C) PruningConfig — absent-object defaults, full parse, fail-closed disable with the fixed reason, ill-typed per-key defaults
- Mutation checks (all fail as required, all reverted before commit): (1) flipping the expected eligible set fails the exact-set assertion (the fixture CAN fail); (2) making the window read the candidate's own creation priority makes the counterexample eligible — the assertion fails, proving D-02 binds; (3) dropping the spender-existence gate makes the ghost-spender candidate vacuously eligible (priority 0 satisfies the window) — the assertion fails, proving T-17-0403's mitigation is load-bearing
- Full target green in 5+ consecutive runs on fresh per-test temp DBs (9/9)

## Task Commits

Each task was committed atomically:

1. **Task 1: Contracts — reason enum, result structs, PruningConfig, evaluator skeleton** - `ca68392e9` (feat)
2. **Task 2: Evaluate() — enumeration, genuine-spend, spend-recency window** - `e77c4fa10` (feat)
3. **Task 3: SC-1 exact-set fixture + SC-4 fail-closed + config tests** - `3b7d960eb` (test)
4. (follow-up) canonical clang-format over the whole plan diff - `2c4795d44` (style)
5. (follow-up) T-17-0405 garbage-value fail-closed case - `19df08956` (test)

## Files Created/Modified
- `src/transaction/PruneEligibility.hpp` — NEW: all contracts; Doxygen throughout; class documented pure/read-only (D-C6), Phase-18 TxPruner named as the reuse seam
- `src/transaction/PruneEligibility.cpp` — NEW: FromJSON, New (Light gate), IsAllowlistedTxKey, Evaluate + private EvaluateCandidate (the one window home)
- `src/transaction/TransactionManager.hpp` — `friend class PruneEligibility;` beside TransactionConsensusHandler (same-module production-collaborator precedent), with a comment naming what the evaluator reads
- `src/transaction/CMakeLists.txt` — PruneEligibility.cpp appended to genius_transaction_objs
- `test/src/account/prune_eligibility_test.cpp` — NEW: fixture copying the CertificateFallbackTest stack (incl. the load-bearing teardown order) + 9 tests
- `test/src/account/CMakeLists.txt` — addtest(prune_eligibility_test) copying the :230-251 link set

## Decisions Made
- **Friendship over public-widening:** the plan's interfaces block listed `GetMonitoredNetworkIDs` (:570) and `ResolveTransactionPathAndCID` (:879) as public; the access-specifier map shows both PRIVATE (public sections are 73-415 and 902-1031). `TransactionConsensusHandler` (:425) is the existing same-module production-collaborator friend, so `friend class PruneEligibility` follows precedent while keeping the public contract small (hard invariant 6)
- **EvaluateCandidate as a private const member, not a file-local free function:** friendship does not extend into anonymous-namespace helpers — the free-function shape did not compile. The member keeps the friend model intact and drops an 8-parameter signature
- **Priority units are identical, not skewed:** empirically `GetKeyPriority` and `GetCRDTHeadList().second` share units (the last put's key priority EQUALS maxHeight — diff 0, not 1). The fixture asserts the invariant (LT window for S_R, GE window for S_E) rather than absolute heights, so a bumped maxHeight is a loud failure, never a silently-wrong verdict
- **Test seeds the tx→CID map with the exact StoreTransactionCID shapes directly** (raw `datastore->put(path_key, cid_string)`, verified against :4388-4411) instead of adding another test-friend accessor — identical bytes, no further header churn
- **Round-timer parking not needed:** no certificates exist in this fixture, so ConsensusManager round ticks have no work to dispatch; the fixture's invariant assertions (above) would catch any unexpected head write loudly
- **requirements.mark-complete deliberately not run** (phase-close tick precedent from 17-01/02/03)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Plan's interface list wrong about access: two required members are private**
- **Found during:** Task 1
- **Issue:** `GetMonitoredNetworkIDs` and `ResolveTransactionPathAndCID` are private; the evaluator (a separate class) cannot call them as the plan assumed. Duplicating either would violate the one-owner/never-duplicate invariants.
- **Fix:** one-line `friend class PruneEligibility;` in TransactionManager.hpp beside the existing same-module collaborator friend (TransactionConsensusHandler), with a comment naming exactly what the evaluator reads.
- **Files modified:** src/transaction/TransactionManager.hpp
- **Commit:** ca68392e9

**2. [Rule 3 - Blocking] Free-function helper could not use the friend grant**
- **Found during:** Task 2
- **Issue:** the per-candidate evaluation was first written as an anonymous-namespace free function; C++ friendship does not cover file-local helpers, so the private `ResolveTransactionPathAndCID` call did not compile.
- **Fix:** moved it into the class as private const member `EvaluateCandidate` (header declaration + `base/buffer.hpp` include); behavior identical.
- **Files modified:** src/transaction/PruneEligibility.hpp, src/transaction/PruneEligibility.cpp
- **Commit:** e77c4fa10

**3. [Rule 2 - Correctness] Vacuous-eligibility hole for empty produced-outputs**
- **Found during:** Task 2
- **Issue:** the plan's NO_OUTPUTS reason covered only GetProducedUTXOs nullopt; a tx with an EMPTY outputs vector would pass "every output genuinely consumed" vacuously and could become eligible with zero spend evidence.
- **Fix:** `!outputs.has_value() || outputs->empty()` both fail closed as NO_OUTPUTS. Exercised by both 0-output spenders in the exact-set fixture.
- **Files modified:** src/transaction/PruneEligibility.cpp
- **Commit:** e77c4fa10

**4. [Rule 2 - Correctness] Windowless Evaluate had no defined behavior**
- **Found during:** Task 2
- **Issue:** the plan gates construction on NodeType only; nothing defined Evaluate() under a config without finality_window — an implicit "everything eligible" or silent crash were both possible.
- **Fix:** Evaluate() returns `outcome::failure(std::errc::invalid_argument)` (fail closed, WARN logged); covered by FailClosedGates_EvaluateWithoutWindowFails.
- **Files modified:** src/transaction/PruneEligibility.cpp
- **Commit:** e77c4fa10

**5. [Rule 2 - Test gap] T-17-0405 (garbage value) left unexercised**
- **Found during:** post-Task-3 review of the threat register
- **Issue:** DESERIALIZE_FAILED was the only reason code no test exercised; the threat register's mitigation (fail closed, scan continues) was implemented but unproven.
- **Fix:** FailClosedGates_GarbageValueReportsDeserializeFailed — malformed bytes at a real tx path: DESERIALIZE_FAILED, no crash, and a well-formed eligible peer beside it still evaluates eligible.
- **Files modified:** test/src/account/prune_eligibility_test.cpp
- **Commit:** 19df08956

### Documented protocol notes (no auto-fixes beyond the above)

**6. [TDD-ordering note] Task 3 RED phase vacuous by plan design**
- **Found during:** Task 3 (tdd="true")
- **Issue:** the tests bind the Task 1-2 contracts, and the plan's own task order mandates those commits first — a failing-tests-first commit has no tree to compile against (same as 17-01/02/03 Task 3).
- **Resolution:** single test(17-04) commit after implementation. Non-vacuity is carried by THREE mutation checks, all performed and all failing as required, then reverted (working tree verified clean vs HEAD before the follow-up commits): flipped expectation (exact-set fails), creation-age window regression (counterexample becomes eligible — fails), dropped existence gate (ghost spender vacuously eligible — fails).

---

**Total deviations:** 4 auto-fixed (2 Rule 3 blocking, 2 Rule 2 correctness) + 1 post-task Rule 2 test gap + 1 protocol note
**Impact on plan:** Local — every landed shape satisfies the plan's acceptance criteria; the friend line and member-helper are the minimal compilable forms of the plan's own interface list.

## Verification Log

| Gate | Result |
|------|--------|
| Task 1: build genius_transaction_objs | exit 0, zero warnings — PASS |
| Task 1: `grep -c "PruneReason\\|PruningConfig\\|IsAllowlistedTxKey"` PruneEligibility.hpp | 12 (>=3 tokens across the surface) — PASS |
| Task 1: header/artifact min_lines | 282 hpp / 274 cpp / 583 test (min 120/150/250) — PASS |
| Task 1: format gate (changed lines) | no `+` hunks — PASS |
| Task 1: tidy gate PruneEligibility.cpp (clang-tidy 22, single-arch filtered db) | 0 findings in the file (only generated-proto/system-header noise) — PASS |
| Task 2: build genius_transaction_objs | exit 0 — PASS |
| Task 2: input-API grep (GetKeyPriority/ResolveTransactionPathAndCID/GetOutPointSpender/IsOutPointGenuinelyConsumed) | 4 (each exactly once) — PASS |
| Task 2: read-only grep (EraseEntry\\|EraseTransactionEntry\\|PutRaw\\|StageErase\\|StageBlock\\|DeleteKey\\|DeleteTransaction) | 0 matches — PASS |
| Task 2: trailing slash `+ "tx/"` present (allowlist + enumeration) | lines 98/131 (post-format) — PASS |
| Task 2: max-over-outputs (`priority.value() > spend_height.value()`) + window (`max_height - spend_height.value() >= window`) | present — PASS |
| Task 3: build prune_eligibility_test | exit 0 (after `cmake -S build/OSX -B build/OSX/Release` regen — out-of-root driver) — PASS |
| Task 3: `--gtest_filter='*ExactEligibleSet*:*FailClosed*:*PruningConfig*'` | all PASSED — PASS |
| Task 3: full suite double-run on fresh fixtures | 8/8 twice, then 9/9 twice after the T-17-0405 case (5+ consecutive green runs total) — PASS |
| Task 3: set-equality assertion in source | sorted eligible_hashes vs sorted expected_hashes, EXPECT_EQ (:350 pre-style-commit) — PASS |
| Task 3: mutation checks (3) | each fails the suite as required; reverted; `git diff` clean vs HEAD before follow-up commits — PASS |
| Plan: format gate over the WHOLE plan diff (vs 6e594bb34) | initially 3 regions non-canonical → applied, rebuilt, re-greened, committed as style(17-04); final check PASS — PASS |
| Plan: tidy gate (final) | 0 file findings on PruneEligibility.cpp — PASS |
| Plan: stub scan (TODO/FIXME/placeholder/coming soon) on the 3 created files | 0 — PASS |
| Plan: post-commit deletion check on all 5 commits | 0 tracked deletions; no new untracked files beyond pre-existing supergenius_* runtime dirs — PASS |
| Build-system pre-PR checklist (both CMakeLists edits) | source-list append to an existing OBJECT lib + addtest block copying :230-251 verbatim (per-platform whole-archive options identical): no artifact routing, interpreter discovery, shell commands, submodule gates, default-off opt-ins, standalone-dir cross-links, generated sources, or spec regeneration touched — PASS |

## Threat Model Disposition

- **T-17-0401 (Tampering, allowlist bypass):** mitigated — exact `base + "tx/"` startswith over monitored networks only; FailClosedGates_AllowlistRejectsCraftedKeys enumerates all five crafted keys and accepts exactly the real form.
- **T-17-0402 (Elevation, NodeType-default-Light as consent):** mitigated — NodeType is the role gate only (B1/B2 refuse Full/Archive); PruningConfig default-disabled is the consent gate; FromJSON fail-closes enabled-without-window (C3).
- **T-17-0403 (Tampering, missing-key priority-0):** mitigated — ResolveTransactionPathAndCID precedes GetKeyPriority on the spender path; ghost-spender candidate ineligible SPENDER_UNRESOLVED; mutation 3 proves the gate is load-bearing.
- **T-17-0404 (Repudiation, unexplained decisions):** mitigated — every ineligible candidate carries reason codes; candidates.size() == 11 asserted (nothing dropped); eligible only with empty reasons.
- **T-17-0405 (DoS, malformed stored value):** mitigated — DESERIALIZE_FAILED ineligible, scan continues, eligible peer still found; proven by FailClosedGates_GarbageValueReportsDeserializeFailed.
- **T-17-0406 (dry-run writing anything):** mitigated — read-only grep gate 0 matches; the evaluator exposes no mutating API at all.

## Issues Encountered
None blocking. One environment note: `cmake --build --target prune_eligibility_test` required an explicit `cmake -S build/OSX -B build/OSX/Release` regen because this repo drives CMake from the out-of-root build/OSX driver (Phase-16 memory note); invoking a brand-new target directly does not trigger ZERO_CHECK regen.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 18's TxPruner can call `PruneEligibility::Evaluate()` verbatim for its work list; the per-candidate `cid` field carries the CID its erase flow needs (17-05 refines SPENDER_UNRESOLVED into pruned-marker semantics).
- 17-06 (GeniusNode wiring) consumes `PruningConfig::FromJSON` for the `pruning` sgns_config.json object; the fixed disabled_reason string is the operator-facing WARN text.
- No blockers. Wave-2 sibling 17-05 has no file overlap with this plan.

## Self-Check: PASSED

- Files exist: src/transaction/PruneEligibility.hpp, src/transaction/PruneEligibility.cpp, test/src/account/prune_eligibility_test.cpp — FOUND
- Commits exist: ca68392e9, e77c4fa10, 3b7d960eb, 2c4795d44, 19df08956 — FOUND in git log
- All task acceptance criteria re-run and passing (see Verification Log)

---
*Phase: 17-eligibility-policy-gating-dry-run*
*Completed: 2026-10-19*
