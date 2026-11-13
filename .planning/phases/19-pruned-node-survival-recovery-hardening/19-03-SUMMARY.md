---
phase: 19-pruned-node-survival-recovery-hardening
plan: 03
subsystem: crdt
tags: [retry-ladder, isresolved-guard, zero-fetch, sc-5-audit, cert-fallback, account-messenger, outcome-result]

# Dependency graph
requires:
  - phase: 18-risk-consumer-hardening
    provides: the 18-01 isResolved broadcast guard (three-way shape mirrored) + Get/ResetRootFetchAttemptsForTest counter seam
provides:
  - RetryDueFailedRoots isResolved guard — pruned/sealed roots are terminal on the retry ladder, fetch-attempt counter never moves for them
  - ForTest seams on CrdtDatastore (ladder seeding + ladder-count observables) for SURV-01's restart-matrix assertions
  - Verify-only proof families: StalledDeltaPruned (local-only, zero fetch, bounded give-up) + PrunedSubject (cert-embedded fallback approves after pruned erase)
  - account_messenger_test target — SC-5's messenger row proven over real pubsub with an empty signed BlockResponse
affects: [19-04 (GATE-05 marker consult), 19-06 (SURV-01 restart matrix rides the guard + counter discipline), SC-5]

# Tech tracking
tech-stack:
  added: [] # zero new dependencies
  patterns: [evidence-first terminal skip extended to the retry ladder (guard BEFORE the counter site), responder sign_ callback as the public capture point for outgoing response bytes, one-direction loopback dial for two-messenger pubsub tests]

key-files:
  created:
    - test/src/account/account_messenger_test.cpp
  modified:
    - src/crdt/crdt_datastore.hpp
    - src/crdt/impl/crdt_datastore.cpp
    - test/src/crdt/globaldb_erase_test.cpp
    - test/src/account/transaction_manager_certificate_fallback_test.cpp
    - test/src/account/CMakeLists.txt

key-decisions:
  - "Guard placement clears the ladder entry on the resolved branch (Pitfall-7 planner discretion): terminal means gone, not forever-skipped; the isResolved error branch leaves the entry re-armed (fail-safe, never enqueue on unknown state)"
  - "No deterministic natural path puts a sealed root on the retry ladder post-18-01 (the broadcast intake guard blocks sealed CIDs from ever enqueueing), so the planner-sanctioned ForTest seeding seam drives the mid-flight state; the retry pass itself is the real DAG-worker-loop pass"
  - "AccountMessenger test shape: two real-pubsub messengers (HandleBlockLikeRequest is PRIVATE and InterfaceMethods has no response-send callback — both plan assumptions were wrong); the responder's sign_ callback receives the serialized BlockResponse bytes, giving the plan's capture assertion through public wiring with zero production change"
  - "Stalled-delta bounded give-up pinned by driving the remaining attempts through the same locked scheduler the real pass calls (natural give-up takes 300s+ of backoff); the zero-fetch window spans real worker passes"

patterns-established:
  - "Ladder-state seeding: static ForTest members taking CrdtDatastore& that call the locked scheduler under dagWorkerMutex_ — the exact state a natural failure produces, reached deterministically"
  - "Empty-response proof discipline: assert presence (parties, request id, timestamp) AND absence (payload) on the captured wire object, plus exactly-once invocation counts on the audited lookup leg"

requirements-completed: [] # SURV-01 restart-matrix proof + GATE-05 marker consult are 19-06/19-04 deliverables; this plan closes their SC-5 audit rows (see Deviations 3)

# Metrics
duration: 34min
completed: 2026-10-23
---

# Phase 19 Plan 03: SC-5 Recovery-Path Audit Closures Summary

**RetryDueFailedRoots now treats a pruned/sealed root as terminal — the isResolved guard fires before the fetch-attempt counter site, clears the ladder entry, and burns zero attempts — while the three already-safe consumers (stalled deltas, cert-embedded fallback, AccountMessenger serving) gained pinning tests: zero-fetch local-only retry, Approve-from-certificate after a pruned subject, and an empty signed BlockResponse over real pubsub.**

## Performance

- **Duration:** 34 min (19:14–19:48 UTC)
- **Tasks:** 3
- **Files modified:** 5 (1 created)

## Accomplishments
- The one production change: `RetryDueFailedRoots` (crdt_datastore.cpp) gains the 18-01 three-way isResolved guard BEFORE `DeleteCIDBlock`/`EnqueueRootCID` (Pitfall 7 — the counter can never tick for a sealed root); resolved ⇒ INFO terminal-skip + ladder entry cleared; isResolved error ⇒ error log + entry stays re-armed (fail-safe)
- Guard test: pruned root on the ladder → entry goes terminal within the retry-pass horizon, fetch-attempt delta exactly 0, `assertNothingResurrected` holds; negative control: an unseen root still enqueues and the counter advances, entry keeps its ladder slot
- StalledDeltaPruned: pruned delta block on the stalled ladder → zero fetch attempts across real `RetryStalledDeltas` passes (local-only pinned), bounded give-up at MAX_FAILED_ROOT_RETRIES, seal retained, nothing resurrected
- PrunedSubject: subject erased via `EraseTransactionEntryPruned` (PRUNED marker verified via `LookupPrunedEntry`) → redelivered certificate Approves through the cert-embedded fallback, subject tracked CONFIRMED from the embedded copy, CRDT value stays erased
- `account_messenger_test` (new ctest target): two real-loopback-pubsub messengers; the responder's failing `get_transaction_cid_` yields a signed EMPTY BlockResponse (responder/requester/request_id/timestamp set, `blocks_size()==0`), exactly one lookup call, requester cycle settles its callback once with a bounded error — no crash, no hang
- Combined ctest sweep `globaldb_integration|certificate_fallback|account_messenger_test` 3/3 green; `crdt_test` EraseSurfaceGate 7/7 green (containment untouched)

## Task Commits

Each task was committed atomically:

1. **Task 1: RetryDueFailedRoots isResolved guard + guard test + negative control** - `c7f50b7fb` (feat)
2. **Task 2: StalledDeltaPruned + PrunedSubject verify-only tests** - `4c85023c4` (test)
3. **Task 3: account_messenger_test new target + CMake registration** - `099cc61c9` (test)

## Files Created/Modified
- `src/crdt/crdt_datastore.hpp` — 4 public ForTest members (2 ladder-seeding statics, 2 ladder-count getters), all documented "No production caller may invoke this"
- `src/crdt/impl/crdt_datastore.cpp` — the guard in RetryDueFailedRoots + the ForTest definitions beside the counter accessors
- `test/src/crdt/globaldb_erase_test.cpp` — 3 new tests (guard, control, stalled-pruned); file now 899 lines (min 60 satisfied)
- `test/src/account/transaction_manager_certificate_fallback_test.cpp` — PrunedSubject case + PruneEligibility include
- `test/src/account/account_messenger_test.cpp` — NEW (229 lines), Doxygen file header, single-leg scope documented
- `test/src/account/CMakeLists.txt` — copied registration block (addtest + link libs + platform link options)

## Decisions Made
- Terminal skip CLEARS the ladder entry (Pitfall-7 discretion) — observable via `GetFailedRootRetryCountForTest`, so "goes terminal" is non-vacuous; the error branch does not (retry the evidence check later, never enqueue on unknown state)
- ForTest seeding seams instead of friend access or a natural race: after 18-01's intake guard, no deterministic two-node wiring can place a sealed root on the ladder — the TOCTOU window (fetch fails, seal appears through another root's walk) is real in production but not reproducible without timing races
- AccountMessenger proven through the public surface over real pubsub rather than a friend seam in the production header — keeps "production AccountMessenger files untouched" true
- Stalled-delta cap driven through `ScheduleStalledDeltaRetryLocked` itself (the same locked scheduler every natural failed pass calls); the zero-fetch claim rides real worker passes over an 8s window (no shorter observable exists for "a pass ran")

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Plan's test-driving assumptions for AccountMessenger were wrong**
- **Found during:** Task 3 read-first (AccountMessenger.hpp)
- **Issue:** The plan asserted `HandleBlockLikeRequest` is public and that InterfaceMethods has a "response-send callback" — it is PRIVATE (hpp:396) and no such callback exists (the response goes through `pubsub_->Publish`); the methods-wired direct-call shape was unreachable as written
- **Fix:** Two-messenger test over real loopback pubsub; the responder's `sign_` callback (which receives the serialized BlockResponse bytes) is the public capture point. Plan's intent (empty signed response, no crash, no error propagation) fully proven; production files untouched
- **Files modified:** test/src/account/account_messenger_test.cpp
- **Verification:** test passes twice; ctest green
- **Committed in:** `099cc61c9`

**2. [Rule 3 - Blocking] No deterministic path reaches the private retry ladders — seam extended to the header**
- **Found during:** Task 1 design (files_modified list omitted crdt_datastore.hpp)
- **Issue:** `RetryDueFailedRoots`/`ScheduleFailedRootRetryLocked`/both ladders are private, and post-18-01 no natural wiring places a sealed root on the ladder; the plan's sanctioned "static ForTest invocation seam" REQUIRES a header declaration the frontmatter's file list did not include
- **Fix:** 4 ForTest members added to crdt_datastore.hpp beside the existing counter accessors (seeding statics + count getters); the retry pass under test is the real worker-loop pass, never seam-invoked
- **Files modified:** src/crdt/crdt_datastore.hpp
- **Verification:** guard/control tests green twice; counter deltas exact
- **Committed in:** `c7f50b7fb`

**3. [Rule 1 - Process] Requirements left open despite frontmatter listing**
- **Found during:** state updates (same shape as 19-02's deviation 2)
- **Issue:** `requirements: [SURV-01, GATE-05]` — SURV-01's proof (restart matrix, zero pruned-CID fetch attempts across restart) is 19-06's deliverable; GATE-05's marker-consult half is 19-04's. This plan closes their SC-5 audit rows (one failure mode removed, consumers pinned) — sibling precedent: 19-02 left SURV-01 open
- **Fix:** Neither marked complete; both recorded as *affects*
- **Files modified:** none (state files kept truthful)

---

**Total deviations:** 3 auto-fixed (2 Rule 3 blocking, 1 Rule 1 process)
**Impact on plan:** None on the must-haves — all five truths proven; the two Rule-3 fixes changed HOW the tests drive the code, not WHAT they prove.

## Issues Encountered
- Namespace qualification: `PruneEligibility` lives directly in `sgns` (the `namespace crdt` block at PruneEligibility.hpp:38 only forward-declares GlobalDB) and `PrunedLookup` is a namespace-level enum, not a class member — fixed to the prune_eligibility_test.cpp qualification on first build error
- clang-format: pre-existing drift in crdt_datastore.cpp/hpp (e.g. PutConvergentImmutableKey block) predates this branch (18-01 left it too); `git-clang-format --diff HEAD` is clean on every changed line — drift left out of scope. The new test file was normalized with clang-format -i before commit
- clang-tidy (filtered single-arch db): only established-idiom findings on new lines (`modernize-use-scoped-lock` + `misc-const-correctness` on `std::lock_guard lock` — identical class fires file-wide on 20+ siblings, documented as accepted in 19-01); naming-config warnings are the known repo noise; no findings on the hpp additions
- cmake target discovery: `account_messenger_test` needed a reconfigure through the out-of-root driver (`cmake -S build/OSX -B build/OSX/Release`) — repo drives CMake from build/OSX (Phase-16 lesson)

## Threat Mitigations (per plan threat_model)
- **T-19-0301** (fail-open guard / fetch storm): guard textually precedes BOTH DeleteCIDBlock and EnqueueRootCID (key_link `isResolved[\s\S]{0,600}(DeleteCIDBlock|EnqueueRootCID)` satisfied); zero-delta counter assertion is the guard test's core claim; isResolved error branch logs + continues — DONE
- **T-19-0302** (stalled-delta regression): the pinned test fails if anyone adds a fetch to RetryStalledDeltas (a pruned block on the ladder + real passes + counter delta 0) — DONE, no code change
- **T-19-0303** (messenger surface): empty signed BlockResponse pinned with presence+absence assertions; bounded requester outcome; no production change — DONE
- **T-19-0304** (test vacuity): negative control (unseen root still retries, counter advances); counter brackets reset before every window; pre-erase CRDT-lookup precondition makes PrunedSubject's prune non-vacuous — DONE
- **T-19-03SC** (supply chain): zero registry installs — DONE

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 19-06's restart matrix can assert `GetRootFetchAttemptsForTest` delta 0 across restart windows knowing the retry ladder can no longer wobble it for pruned roots; the ladder-count observables (`GetFailedRootRetryCountForTest`/`GetStalledDeltaRetryCountForTest`) and the seeding seams are available for its scenarios
- 19-04's marker consult (GATE-05) is untouched by this plan — disjoint files (TransactionManager vs crdt_datastore)
- SURV-01's remaining failure modes (missing-tx requests) close with 19-04's marker consult

## Self-Check: PASSED

All 6 modified/created files exist; all 3 task commits (c7f50b7fb, 4c85023c4, 099cc61c9) present in git log; `isResolved` precedes `DeleteCIDBlock`/`EnqueueRootCID` in RetryDueFailedRoots; `GetRootFetchAttemptsForTest` used in globaldb_erase_test.cpp; `PrunedSubject` present in the cert test; account_messenger_test.cpp is 229 lines and registered in ctest.

---
*Phase: 19-pruned-node-survival-recovery-hardening*
*Completed: 2026-10-23*
