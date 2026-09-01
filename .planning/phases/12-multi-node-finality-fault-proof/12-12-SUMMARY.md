---
phase: 12-multi-node-finality-fault-proof
plan: "12"
subsystem: testing
tags: [consensus, filtercertificate, putconvergentimmutable, crdt, gtest, fixture-repair]

# Dependency graph
requires:
  - phase: 12-multi-node-finality-fault-proof (UAT Test 1 + .planning/debug/same-burn-canonical-finality.md)
    provides: "Elimination-grade root cause: repeated same-key plain-Put writes to the convergent-immutable slot key made the visible existing certificate a merge/scheduling artifact"
provides:
  - "Deterministic same-burn FilterCertificate regression: the canonical slot key is written exactly once per direction through the production PutConvergentImmutable path into a per-direction fresh node GlobalDB"
  - "ConsensusPendingLifecycleTestAccess::WriteConvergentCertificateAtKey production-path slot write helper"
  - "STATE.md deferred follow-up rows for the two CRDT hardening items (equal-priority overwrite guard; self-created-write DAG head advancement)"
affects: [phase-12-verification, uat-gap-closure, crdt-hardening-followup]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Fixture authority writes mirror the production call shape (PutConvergentImmutable, empty topic) instead of plain last-write-wins Put"
    - "One fresh node db per verify direction so no assertion can depend on CRDT merge ordering"
    - "Readback guard (GetCertificateBySlot) localizes silent CRDT write loss at the write site"

key-files:
  created: []
  modified:
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - .planning/STATE.md

key-decisions:
  - "Per-direction dedicated node db (indices 10/11/12) instead of distinct slot keys: ValidateCertificateKey binds every certificate to the one canonical key, so fresh dbs are the only collision-free isolation"
  - "The shared signed registry update validates (Approve) on all three node managers, so the plan's fallback (fresh dedicated nodes at indices 30/31/32) was not needed"
  - "Readback goes through public GetCertificateBySlot(canonical slot id), not the /cert/-prefixed key, because GetCertificateBySlot prepends the certificate base path itself"

patterns-established:
  - "Convergent-immutable records are only ever written through the convergent-immutable path in fixtures, exactly once per db"

requirements-completed: [TEST-01, TEST-06]

# Metrics
duration: 36min
completed: 2026-09-01
---

# Phase 12 Plan 12: UAT Test 1 Gap Closure Summary

**Same-burn FilterCertificate regression repaired so each verify direction writes the convergent-immutable certificate slot exactly once through PutConvergentImmutable into its own fresh node db, with the two CRDT hardening directions durably deferred in STATE.md.**

## Performance

- **Duration:** 36 min
- **Started:** 2026-09-01T18:08:22Z
- **Completed:** 2026-09-01T18:44:00Z
- **Tasks:** 2
- **Files modified:** 2 (test fixture + STATE.md) plus this SUMMARY

## Accomplishments

- Added `ConsensusPendingLifecycleTestAccess::WriteConvergentCertificateAtKey` (test/src/blockchain/consensus_pending_lifecycle_test.cpp), mirroring `WriteLiveCertificate` and the production `SubmitCertificate` call shape (`db_->PutConvergentImmutable({key}, value, {})`, Consensus.cpp:2115). `WriteCertificateAtKey` (plain Put) is retained unchanged for the legacy-key test that legitimately needs it.
- Rewrote `verify_order` in `FilterCertificateTreatsSameMintAlternatesAsNormalAndDifferentMintQuorumsAsFaults` to take a node index: direction 1 (first vs second) uses node 10's db, direction 2 (second vs first) node 11's, direction 3 (first vs same-mint alternate) node 12's. Each direction writes the slot key exactly once, adds a cross-manager Approve guard, and adds a `GetCertificateBySlot` readback guard that names the expected certificate. FilterCertificate's exercise and every hash-ordering assertion are semantically unchanged.
- The UAT Test 1 blocker root cause (same-priority `CrdtSet::SetValue` last-merge-wins overwrite ordering deciding which existing certificate is visible) is structurally removed from the test: no direction reads a slot that another direction wrote.
- Recorded the two CRDT hardening items as explicit deferred follow-ups in `.planning/STATE.md` (category `crdt-hardening`, both `Deferred`, deferred at "Phase 12 UAT gap closure 12-12").

## Task Commits

1. **Task 1: Write the certificate slot key through the production convergent-immutable path, one fresh node db per verify direction** - `e08288c3` (test)
2. **Task 2: Prove no regression across the lifecycle and multi-node suites and record the CRDT hardening deferrals** - this plan's docs commit (docs)

## Files Created/Modified

- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - new production-path write helper; per-direction node-db `verify_order` with Approve + readback guards; three call sites updated to `verify_order(0/1/2, ...)`
- `.planning/STATE.md` - two `crdt-hardening` Deferred Items rows plus the multi-node evidence environment blocker (see Issues Encountered)

## Decisions Made

- **One fresh node db per direction rather than distinct slot keys.** `ValidateCertificateKey` binds each certificate to the single canonical `/cert/mint-v2:...` key (asserted equal for first/second at test line 1264), so distinct keys are not viable; the three fixture nodes already own independent GlobalDB dirs and ports (multi-validator-10/11/12, 54011-54013), giving collision-free isolation with no fixture growth.
- **No fallback node construction was needed.** The plan required stopping and falling back to fresh dedicated nodes (indices 30/31/32) if the shared registry update failed to validate on a cross-manager. All three directions approved (`Check::Approve`) on their own manager, guarded by an `ASSERT_EQ` inside `verify_order`, so the fallback path stayed unused.
- **CRDT hardening deferred, not fixed here.** Production authority for this key is already protected by `PutConvergentImmutable`'s reserved priority and lowest-hash convergence (Phase 10 decision); the gap truth does not depend on general plain-Put merge semantics; and changing CRDT merge behavior ripples across every CRDT-dependent suite. Both items require a separately scoped plan with full-suite regression evidence, consistent with the existing Phase 12 blocker discipline.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Readback guard queried the prefixed key instead of the canonical slot id**
- **Found during:** Task 1 (first focused run)
- **Issue:** The plan's step 2d read `manager->GetCertificateBySlot( key )` with the `/cert/mint-v2:...` key. `GetCertificateBySlot` internally prepends `CERTIFICATE_BASE_PATH_KEY` itself (Consensus.cpp:3808), so the lookup targeted `/cert//cert/mint-v2:...` and returned NOT_FOUND — all three readback guards failed although every write succeeded.
- **Fix:** Read back through the canonical slot id from the parsed certificate (`GetSlotKey( parsed_existing.proposal() )`, the same call the unchanged sibling test uses) and additionally assert `GetExpectedCertificateSlotKey( parsed_existing ) == key` so the written key and the read slot stay provably the same record. (`CERTIFICATE_BASE_PATH_KEY` is private, so the public `GetExpectedCertificateSlotKey` was used for the consistency assertion.)
- **Files modified:** test/src/blockchain/consensus_pending_lifecycle_test.cpp
- **Verification:** Focused test passes 5/5 consecutive runs with the readback guard active.
- **Committed in:** `e08288c3`

**2. [Rule 3 - Blocking] Unqualified type name in the global-scope test body**
- **Found during:** Task 1 (first compile)
- **Issue:** `ConsensusManager::Certificate parsed_existing;` does not compile in the test body (the access class is inside `namespace sgns`, the TEST_F body is not).
- **Fix:** Qualified as `sgns::ConsensusManager::Certificate`.
- **Files modified:** test/src/blockchain/consensus_pending_lifecycle_test.cpp
- **Verification:** Target builds clean.
- **Committed in:** `e08288c3`

---

**Total deviations:** 2 auto-fixed (1 bug, 1 blocking)
**Impact on plan:** Both fixes are local to the fixture the plan already modifies; no scope change, no production source touched.

## Issues Encountered

**External artificial CPU load blocked the multi-node three-consecutive-serial evidence.**

- During Task 2's multi-node verification the machine sat at load 15-20 on 8 cores. The source is a leftover artificial load generator from the 2026-09-01 same-burn diagnosis session: a zsh loop that spawned 8 `while :; do :; done` busy subshells and ran the focused lifecycle test 20 times (`/tmp/sameburn_load_run_1..20.log`, all complete at 14:34 local). Its cleanup `kill $(jobs -p)` missed about six busy-loop orphans (PIDs 84963, 84964, 84965, 84967, 84968, 84970, parent 84959), which have been spinning since.
- The executor's attempt to terminate only those six orphaned spin loops was denied by the permission system ("Interfere With Workloads"), so the load could not be cleared and the denial was respected.
- Multi-node full-suite attempts under that load: 5 attempts, 1 complete green pass (13/13, run 2 of the final series) and 4 intermittent failures. Every failure is a timing readiness gate, never an assertion about finality state: `multi_node_finality_fault_test.cpp:1119` (5s libp2p topology formation), `:2077/:2080/:2096` (20s passive-recipient recovery), and the collector child exiting nonzero on its own readiness gate. The inner scenario `FinalityFaultNetwork.PublisherLossAfterPersistenceUsesDeterministicFailover` also passed standalone under load 17.9.
- Attribution that this is environmental, not the plan's change: the `multi_node_finality_fault_test` binary was not recompiled by this plan (mtime 12:10 local, before the first fixture edit at 15:08); this plan touches only `consensus_pending_lifecycle_test.cpp`; the identical binary passed three strictly serial full 13/13 runs on 2026-08-31 (logs preserved at `/private/tmp/phase12-11-normal-final-{1,2,3}.log`); and the Phase 12 UAT recorded publisher loss and real-route/process ownership as passing on this binary.
- Resolution: recorded as a blocker in `.planning/STATE.md` with the re-run instruction. After the orphaned load loops are cleared, re-running `ctest --test-dir build/OSX/Release --output-on-failure --timeout 300 -R '^multi_node_finality_fault_test$'` three times serially is expected to reproduce the 2026-08-31 result. No fixture or production repair is authorized by these load failures (STATE.md Plan 12-08 discipline: repair requires two matching fresh failures with direct fixture lifecycle proof, and these failures carry an identified external cause).

## Verification

1. **Focused regression, five consecutive runs:** `ConsensusPendingLifecycleTest.FilterCertificateTreatsSameMintAlternatesAsNormalAndDifferentMintQuorumsAsFaults` passed 5/5 consecutive runs on `build/OSX/Release` (logs `/tmp/p12_focus_run{1..5}.log`).
2. **FilterCertificate group:** `ConsensusPendingLifecycleTest.FilterCertificate*` passed 2/2, including the unchanged production-path sibling `FilterCertificateRejectsHigherHashOccupiedSlotBeforeCrdtApply`.
3. **Lifecycle full CTest target, three consecutive serial runs:** passed 3/3, each `100% tests passed, 0 tests failed out of 1`, ~44 s per run (logs `/tmp/p12_lifecycle_full{1,2,3}.log`).
4. **Multi-node full CTest target:** build verified unchanged; 1 complete green pass (13/13, `/tmp/p12_multinode_pass2.log`) plus 4 load-attributed intermittent failures (see Issues Encountered). **The three-consecutive-serial gate for this target is not yet satisfied** and is recorded as a STATE.md blocker pending removal of the leftover artificial load.
5. **Static no-src-change gate:** `git status --porcelain -- src/` was empty at every task gate; the Task 1 commit (`e08288c3`) touches only `test/src/blockchain/consensus_pending_lifecycle_test.cpp`.
6. **STATE.md deferrals:** `grep -c 'crdt-hardening' .planning/STATE.md` returns 2.

## Known Stubs

None. No placeholder values, no unwired data sources, no TODO/FIXME markers were introduced.

## User Setup Required

None for the plan's deliverables. One manual cleanup is needed to finish the remaining evidence gate: terminate the six orphaned busy-loop shells listed above (they belong to a finished diagnosis run and are only burning CPU), then re-run the three serial multi-node passes.

## Next Phase Readiness

- The UAT Test 1 blocker is closed at its diagnosed root cause: the repaired regression no longer performs repeated same-key conflicting plain-Put writes to a convergent-immutable record, so its outcome is deterministic under any CRDT merge/scheduling order.
- FilterCertificate semantics, production consensus/CRDT sources, and every other lifecycle test are unchanged and green across three serial full-suite passes.
- Outstanding before the Phase 12 UAT can be re-run clean: the multi-node three-consecutive-serial evidence pass blocked by the leftover artificial load (STATE.md blocker), and the two `crdt-hardening` deferred follow-ups need their own separately scoped plan.

## Self-Check: PASSED

- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` modified and contains `WriteConvergentCertificateAtKey`: FOUND
- `.planning/STATE.md` contains 2 `crdt-hardening` rows: FOUND
- Commit `e08288c3` present in git log: FOUND
- This SUMMARY file present in the plan directory: FOUND

---
*Phase: 12-multi-node-finality-fault-proof*
*Completed: 2026-09-01*
