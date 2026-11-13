---
phase: 17-eligibility-policy-gating-dry-run
plan: 02
subsystem: database
tags: [crdt, priority-read, delta-keys, tombstones, globaldb, read-only-passthrough, c++17]

# Dependency graph
requires:
  - phase: 16-erasure-primitives-bookkeeping-hardening
    provides: empty-topics Put lesson (seed via real topics) this test fixture follows
provides:
  - Public per-key CRDT priority read chain GlobalDB::GetKeyPriority -> CrdtDatastore::GetPriority -> CrdtSet::GetPriority (17-04 spend-recency window reads the spender's priority here)
  - Public keys-only delta enumeration GlobalDB::GetLocalDeltaKeys / CrdtDatastore::GetLocalDeltaKeys covering elements AND tombstones, parse-at-call-time, strictly local (17-05 multi-element block gate reads a block's carried keys here)
affects: [17-eligibility-policy-gating-dry-run (17-04 window evaluator, 17-05 block gate)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Sibling-not-mutation exposure: a documented-gap precedent (elements-only GetLocalDeltaKeyValues) stays byte-identical; the new coverage lands as a sibling so existing callers' result sets cannot change"
    - "Facade passthrough chain with null-guard: GlobalDB level copies GetCRDTHeadList's ActiveCRDTDataStore null-check shape returning operation canceled"
tech-patterns: []

key-files:
  created:
    - test/src/crdt/crdt_priority_delta_keys_test.cpp
  modified:
    - src/crdt/crdt_datastore.hpp
    - src/crdt/impl/crdt_datastore.cpp
    - src/crdt/globaldb/globaldb.hpp
    - src/crdt/globaldb/globaldb.cpp
    - test/src/crdt/CMakeLists.txt

key-decisions:
  - "GetLocalDeltaKeys is a SIBLING of GetLocalDeltaKeyValues, not a mutation: the elements-only precedent is byte-for-byte untouched (0 deleted lines in impl/crdt_datastore.cpp) so no existing caller's result set can change"
  - "CrdtDatastore::GetLocalDeltaKeys mirrors the precedent's non-const signature: GetDeltaFromNode is a non-const member, so the plan's const would not compile; GlobalDB::GetLocalDeltaKeys IS const (delegates through the shared_ptr pointee)"
  - "PriorityPassthrough test compares GetKeyPriority with GetCRDTHeadHeight of the Put-returned CID: CreateDAGNode records the delta's height as both the head priority and the key priority, so the head height IS the priority the Put recorded -- no formula duplication"
  - "PRUNE-01/PRUNE-03 NOT ticked in REQUIREMENTS.md by this plan: this plan delivers only the read exposures; the window logic is 17-04's evaluator and the block gate is 17-05 (17-01 phase-close precedent)"

patterns-established:
  - "Read-exposure contract: read-only passthroughs at both facade levels, parse-at-call-time (the block is the source of truth), strictly local (GetNodeWithoutRequest), fail-closed on unparseable CID input"

requirements-completed: []  # Contribution only — PRUNE-01 window logic lands in 17-04, PRUNE-03 block gate in 17-05; tick at phase close (see Decisions)

# Metrics
duration: 15min
completed: 2026-10-19
---

# Phase 17 Plan 02: CRDT Read-Exposure (Priority + Delta Keys) Summary

**A caller above the CRDT facades can now read a key's per-key CRDT priority (GlobalDB::GetKeyPriority, with the missing-key-reads-as-0 caveat documented at both levels) and enumerate a delta block's carried keys — elements AND tombstones — keys-only via GlobalDB::GetLocalDeltaKeys, without touching the network or the elements-only precedent**

## Performance

- **Duration:** 15 min
- **Started:** 2026-10-19T14:16:10Z
- **Completed:** 2026-10-19T14:30:55Z
- **Tasks:** 3
- **Files modified:** 6 (1 created)

## Accomplishments
- `CrdtDatastore::GetPriority(aKey) const` exposes the owning `set_->GetPriority` beside GetHeadList; `GlobalDB::GetKeyPriority(key) const` copies GetCRDTHeadList's exact null-guard shape (`operation canceled` when the datastore is gone) and delegates — the read chain 17-04's spend-recency window needs
- Doxygen at BOTH levels documents the load-bearing caveat verbatim from crdt_set.cpp:498-519 — a key with no priority record reads as success with value 0, so 0 never implies existence (T-17-0203 mitigation)
- `CrdtDatastore::GetLocalDeltaKeys(cid_string)` copies the precedent's parse path exactly (CID::fromString → `dagSyncer_->GetNodeWithoutRequest` → `GetDeltaFromNode(*node, true)`), then collects `key()` from BOTH `delta.elements()` AND `delta.tombstones()`, values discarded — the PRUNE-03 exposure: a block carrying a tombstone for a live key is now visible to the block gate
- `GlobalDB::GetLocalDeltaKeys(cid_string) const` mirrors GetLocalDeltaKeyValues' null-guard + error-logging shape at the facade
- The elements-only precedent `GetLocalDeltaKeyValues` is byte-for-byte untouched (additive-only diff, 0 deleted lines) — sibling chosen over mutation so existing callers' result sets cannot change
- Four tests on a real single-node store (EraseNodeCollection fixture wiring, real topic seeding per the Phase-16 empty-topics lesson): put-priority passthrough vs the head height the Put recorded, missing-key-tolerates-as-0, 2-element block + tombstone block exact-key enumeration, garbage-CID fail-closed
- Full crdt_test suite 43/43 green twice consecutively on fresh per-test DBs (39 pre-existing + 4 new)

## Task Commits

Each task was committed atomically:

1. **Task 1: GetPriority passthrough pair (CrdtDatastore + GlobalDB)** - `47f3cffb3` (feat)
2. **Task 2: GetLocalDeltaKeys sibling (elements AND tombstones, keys-only)** - `be2a5f9e8` (feat)
3. **Task 3: Passthrough tests on a real store + crdt_test registration** - `2c2aac51a` (test)

## Files Created/Modified
- `src/crdt/crdt_datastore.hpp` — GetPriority declaration with Doxygen (missing-key-returns-0 caveat); GetLocalDeltaKeys declaration with Doxygen (parse-at-call-time, strictly local, covers both lists, precedent untouched)
- `src/crdt/impl/crdt_datastore.cpp` — GetPriority one-line delegation after GetHeadList; GetLocalDeltaKeys implementation appended after the untouched precedent (additive-only)
- `src/crdt/globaldb/globaldb.hpp` — GetKeyPriority + GetLocalDeltaKeys declarations in the public query block
- `src/crdt/globaldb/globaldb.cpp` — GetKeyPriority (GetCRDTHeadList null-guard shape) + GetLocalDeltaKeys (GetLocalDeltaKeyValues error-logging shape)
- `test/src/crdt/crdt_priority_delta_keys_test.cpp` — NEW (292 lines): CrdtPriorityDeltaKeysTest + SingleNodeCollection fixture + 4 TEST_F cases
- `test/src/crdt/CMakeLists.txt` — crdt_priority_delta_keys_test.cpp appended to addtest(crdt_test ...); crdt_globaldb added to the link list (Rule 3, see Deviations)

## Decisions Made
- **Priority-recording oracle for test 1:** rather than hardcoding a height (brittle) or re-deriving maxHeight+1 (duplicating the formula), the test reads `GetCRDTHeadHeight(put_cid, topic)` — CreateDAGNode (:1568-1571) sets the delta priority to height+1 and ProcessJobIteration records that same `delta.priority()` as the head's priority (:1071-1078), so the head height IS the priority the Put recorded.
- **Garbage-CID choice for test 4:** `"not-a-cid"` — verified in the vendored libp2p MultibaseCodecImpl that a leading 'n' is not a multibase prefix (`UNSUPPORTED_BASE`), guaranteeing CID::fromString fails before any block access (T-17-0201 fail-closed disposition proven).
- **Fixture teardown** drops the erase-test's `SetEraseInterruptHookForTest(nullptr)` clear: that seam is erase-specific and never armed by these tests; the load-bearing order (ShutdownNow → db.reset → pubsub->Stop) is preserved exactly.
- **Requirements checkboxes deferred to phase close** (17-01 precedent): this plan's frontmatter carries PRUNE-01/PRUNE-03, but the window semantics are 17-04's and the block gate is 17-05's; `requirements.mark-complete` deliberately not run.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] CrdtDatastore::GetLocalDeltaKeys is non-const, not const**
- **Found during:** Task 2
- **Issue:** The plan specifies `const`, but the copied parse path calls `GetDeltaFromNode(*node, true)` — a non-const member (crdt_datastore.hpp:368). A const member function cannot call it; the plan as written does not compile.
- **Fix:** Mirrored the precedent `GetLocalDeltaKeyValues`' non-const signature (it is non-const for exactly this reason). GlobalDB::GetLocalDeltaKeys IS const as planned — it delegates through the shared_ptr pointee, which is never const.
- **Files modified:** src/crdt/crdt_datastore.hpp, src/crdt/impl/crdt_datastore.cpp
- **Commit:** be2a5f9e8

**2. [Rule 3 - Blocking] crdt_test needed the crdt_globaldb link line**
- **Found during:** Task 3
- **Issue:** The plan registers the test in addtest(crdt_test ...) claiming "no link changes", but crdt_test links only crdt_datastore — the GlobalDB facade symbols (GlobalDB::New, Put, Remove, GetKeyPriority, GetLocalDeltaKeys, GetCRDTHeadHeight) live only in the crdt_globaldb library; the plan's own fixture and assertions cannot link without it.
- **Fix:** Added `crdt_globaldb` to target_link_libraries(crdt_test ...) with a comment naming the reason. Still no new target, no new external library.
- **Files modified:** test/src/crdt/CMakeLists.txt
- **Commit:** 2c2aac51a

### Documented protocol notes (no auto-fixes beyond the two above)

**3. [TDD-ordering note] Task 3 RED phase vacuous by plan design**
- **Found during:** Task 3 (tdd="true")
- **Issue:** The tests exercise APIs landed in Tasks 1-2, and the plan's own task order mandates those commits first — a failing-tests-first commit has no tree to compile against. 
- **Resolution:** Single `test(17-02)` commit after implementation. Non-vacuity is carried by the assertions themselves: the tombstone size==1 assert fails against any elements-only implementation (the exact gap being closed), the missing-key success+value-0 assert pins the documented caveat, and the garbage-CID assert fails if parse ever degrades to empty-success.
- **Impact:** None on correctness or gates.

---

**Total deviations:** 2 auto-fixed (Rule 3), 1 protocol note documented
**Impact on plan:** Minimal and local — both were compile/link blockers in the plan's literal text; the landed shape satisfies every acceptance criterion's intent.

## Verification Log

| Gate | Result | Notes |
|------|--------|-------|
| Task 1: build crdt_globaldb | exit 0, zero warnings | PASS |
| Task 1: `grep -c GetKeyPriority` globaldb.hpp / globaldb.cpp | 1 / 1 | PASS |
| Task 1: `grep -c "set_->GetPriority"` impl/crdt_datastore.cpp | 2 (1 pre-existing PutKeyLocal + 1 new) | PASS |
| Task 1: format gate (git-clang-format --diff HEAD, changed lines) | "did not modify any files" | PASS |
| Task 1: tidy gate crdt_datastore.cpp + globaldb unity TU (clang-tidy 22, single-arch filtered db) | 0 findings each | PASS |
| Task 2: build crdt_globaldb | exit 0 | PASS |
| Task 2: GetLocalDeltaKeys present in all 4 files | yes | PASS |
| Task 2: `git diff impl/crdt_datastore.cpp \| grep -c '^-[^-]'` | 0 (additive-only; precedent body untouched) | PASS |
| Task 2: format gate | PASS | PASS |
| Task 3: build crdt_test | exit 0 | PASS |
| Task 3: `--gtest_filter='*PriorityPassthrough*:*DeltaKeys*'` | 4/4 PASSED | PASS |
| Task 3: full suite double-run | 43/43 PASSED twice (~20s each) | PASS |
| Task 3: format gate (test file added lines) | no `+` hunks | PASS |
| Plan: additive-only vs plan start (cd2c7faa7) on impl/crdt_datastore.cpp | 0 deleted lines; GetLocalDeltaKeyValues region 0 hits in diff | PASS |
| Plan: no new CMake targets / external libs | one source append + one link line on the existing target | PASS |
| Build-system pre-PR checklist walk | no artifact routing, interpreter, shell, submodule, default-off, standalone-dir, generated-source, or spec-regeneration surface touched (pure source-list/link append) | PASS |

## Threat Model Disposition

- **T-17-0201 (Tampering, cid_string input — mitigate):** CID::fromString validates before any block access; proven fail-closed by DeltaKeysFailClosedOnUnknownCid ('not-a-cid' → error, never empty-success).
- **T-17-0202 (Information disclosure — accept):** both exposures are read-only passthroughs over locally committed data; GetNodeWithoutRequest is strictly local (no new network surface introduced).
- **T-17-0203 (Tampering, 0-for-missing ambiguity — mitigate):** Doxygen on both levels documents the caveat; the 17-04 evaluator must establish spender existence via ResolveTransactionPathAndCID before trusting a priority read; the semantics are pinned by PriorityPassthroughToleratesMissingKey.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 17-04 (spend-recency window) can read the spender's CRDT priority: `globaldb->GetKeyPriority(key)` with the documented existence caveat, paired with 17-01's GetOutPointSpender.
- 17-05 (multi-element block gate) can enumerate a block's carried keys — elements AND tombstones — via `globaldb->GetLocalDeltaKeys(cid_string)` and reject any block carrying a tombstone for a live key.
- No blockers. Wave-1 sibling 17-03 is unaffected (no file overlap).

## Self-Check: PASSED

- Files exist: src/crdt/crdt_datastore.hpp, src/crdt/impl/crdt_datastore.cpp, src/crdt/globaldb/globaldb.hpp, src/crdt/globaldb/globaldb.cpp, test/src/crdt/crdt_priority_delta_keys_test.cpp, test/src/crdt/CMakeLists.txt — FOUND
- Commits exist: 47f3cffb3, be2a5f9e8, 2c2aac51a — FOUND in git log
- All task acceptance criteria re-run and passing (see Verification Log)

---
*Phase: 17-eligibility-policy-gating-dry-run*
*Completed: 2026-10-19*
