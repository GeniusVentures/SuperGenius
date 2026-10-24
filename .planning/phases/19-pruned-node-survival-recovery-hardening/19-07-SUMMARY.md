---
phase: 19-pruned-node-survival-recovery-hardening
plan: 07
subsystem: crdt
tags: [surv-02, surv-03, d-04, pruned-peer-refusal, cid-failure-accounting, failover, fresh-node-bootstrap, pruned-mesh, real-pubsub, erasenodecollection]

# Dependency graph
requires:
  - phase: 19-pruned-node-survival-recovery-hardening (19-03)
  provides: the proven EraseNodeCollection N-node topology extensions in globaldb_erase_test.cpp (entryApplied helper, RefusalFailover/PrunedMesh suites) + the fetch-attempt/ladder observables this plan's assertions lean on
provides:
  - SURV-03 closed end to end: a fresh light bootstraps history two pruned lights no longer hold, served by the full node through its serving-topic broadcast (Pitfall-6 wiring asserted as a precondition), exact-zero ladder rides, zero-fetch mesh join
  - SURV-02's PEER half closed: a pruned server's clean refusal is CID-accounted (never a connection blacklist) and the requester fails over to a serving peer and still routes to the refused peer for data it holds
affects: [19-05 (owns SURV-02's LOCAL half -- tri-state lookup), SURV-02, SURV-03, SC-4]

# Tech tracking
tech-stack:
  added: [] # zero new dependencies
  patterns: [hermetic route-walk proof (ordered AddRoute + one direct getNode) beside an ordering-free mesh E2E, serving-topic route determinism (the full node as the sole broadcast source a fresh node can see), block-discard between proof phases so the broadcast intake re-processes a directly-fetched CID]
key-files:
  created: []
  modified:
    - test/src/crdt/globaldb_erase_test.cpp
key-decisions:
  - "The plan's sequenced-broadcast premise (trigger the pruned light's head broadcast, then the full node's, and read the refusal off the retry ladder between them) is not reproducible on this gossip layer: grafting relays EVERY cached seeding publish at once (the Case-1 message-cache mechanism), so which route registers first for the pruned CID is a cache race. The refusal half is instead proven hermetically: ordered AddRoute (the registration broadcasts drive via AddMultiCIDInfo) with the pruned peer FIRST, one direct getNode -- the production route-walk entry -- which deterministically consults the light, takes its storage-deterministic refusal, records the per-CID failure, and fails over to the full node"
  - "PrunedMesh uses a serving topic the full node alone populates (its put on BOTH topics links the mesh history, so one serving-topic broadcast carries the whole pruned DAG beneath it): the plan's single-shared-topic shape is inherently order-nondeterministic -- a light-owned first route starves the pruned history into the 8-attempt ladder (the Pitfall-6 failure mode, made flaky rather than avoided). The fresh node subscribes the serving topic first (sole route source = the full node), then joins the mesh topic with all blocks present (proving the no-storm property). All four nodes share the mesh topic; the serving topic is the Redelivery suite's own two-topic pattern (kTracerTopic precedent)"
  - "A5 record (observed, no guessed constants): the pruned server's refusal is storage-deterministic -- GetNodeFromMerkleDAG(entryA) on the pruned node is asserted failing (the erased block-bytes key IS the responder's MerkleDagService storage key) -- and the requester's classification is CID-specific (IsOnBlackList false throughout plus the light still serving its fresh data). The exact on-wire status was not directly observable from the test; transport logs interleaved 'RS_FULL_CONTENT but no blocks received' warnings on failed requests. Behavior asserted per T-19-0703"
  - "Counter semantics discovered: EnqueueRootCID dedups pending/active roots and counts only queue-reaching enqueues, and the process-global fetch-attempt counter includes cross-node fetches (e.g. a light fetching the full node's serving delta) -- exact-count assertions therefore use the per-instance ladder observables; the counter carries bounds only"

patterns-established:
  - "Two-part peer-proof: hermetic route-walk (ordered public AddRoute + direct getNode, zero pubsub races) for the mechanism, ordering-free mesh flow for the E2E -- each asserts what it alone can prove deterministically"
  - "Block-discard between proof phases: DeleteCIDBlock (the same call RetryDueFailedRoots makes before a refetch) drops a directly-fetched LRU block so the broadcast intake's HasBlock check does not silently skip the CID's delta merge"

requirements-completed: [SURV-03] # SURV-02's peer half closed here; its LOCAL half (tri-state lookup) is 19-05's deliverable -- left open per the 19-03/19-04 sibling precedent

# Metrics
duration: 82min
completed: 2026-10-23
---

# Phase 19 Plan 07: Peer Refusal/Failover + Pruned-Mesh E2E Summary

**A pruned server's clean refusal is proven CID-accounted and failover-ready through a hermetic ordered-route getNode walk (pruned light first, full node second -- the light is never blacklisted and still serves its fresh data), and a fresh light bootstraps a two-pruned-light mesh through the full node's serving-topic broadcast with exactly zero ladder rides and a zero-fetch mesh join.**

## Performance

- **Duration:** 82 min (20:50-22:12 UTC)
- **Tasks:** 2
- **Files modified:** 1 (test/src/crdt/globaldb_erase_test.cpp: 899 -> 1290 lines)

## Accomplishments
- RefusalFailover suite (3 real pubsub nodes on EraseNodeCollection): a deaf pruned light (seal retained, value/block/raw-map gone, responder-side dag lookup asserted failing) and a full server holding everything; the requester proves the SURV-02 peer half in three parts -- hermetic ordered-route getNode (consult -> refusal -> RecordCIDFailure -> failover; IsOnBlackList false; zero root enqueues), mesh E2E (fresh requester converges on all three deltas including the pruned CID; ladder drains to baseline; attempts bounded; light's terminal state untouched), and the functional not-blacklisted leg (the light's fresh put still routes to it and completes)
- PrunedMesh suite (4 real pubsub nodes): two lights that both prune the history + one full node + a fresh node added after the prune; the full node's serving-topic broadcast is asserted-wired BEFORE the fresh node's fetch window (head exists, delivery path counted), the fresh node converges on A / L1_RETIRE / F_SERVE with the ladder exactly at baseline and attempts in [1,4], and joining the pruned mesh afterwards with all blocks present fetches NOTHING across a 6s quiet window (counter exactly 0, lights' seals and absent blocks unchanged)
- Both suites double-run green plus an 8x stability sweep each (the pubsub delivery nondeterminism that shaped the design is documented below); full globaldb_integration_test green via ctest (17 tests, 83s)
- No production code changed -- the plan's own success criterion ("No new production mechanism")

## Task Commits

Each task was committed atomically:

1. **Task 1: RefusalFailover suite (SURV-02 peer half)** - `fa0193012` (test)
2. **Task 2: PrunedMesh suite (SURV-03 / D-04)** - `250d74246` (test)

## Files Created/Modified
- `test/src/crdt/globaldb_erase_test.cpp` -- +391 lines: file-header scope line, graphsync_dagsyncer include, the shared `entryApplied` helper on the Redelivery family base, the RefusalFailover suite (class + 3-part test), the PrunedMesh suite (class + 3-phase test); no existing test touched

## Decisions Made
- Hermetic refusal proof over broadcast sequencing (see key-decisions): the refusal, its per-CID classification, and the failover are each deterministic only without the gossip cache in the loop
- Serving-topic topology for PrunedMesh: determinism required that the fresh node's ONLY route source be the full node; the mesh topic still carries all four nodes (joined post-convergence for the no-storm proof)
- Bounded waits instead of point-in-time EXPECTs for anything that races worker scheduling: ladder drain and head finalization (root jobs finalize after value merge; a failed sibling root's head update is lost with its job and the rescue head supersedes it -- asserted as the converged head)
- The refused peer's re-consultation timing (4s CID_FAILURE_SUPPRESSION vs 5s retry base, verified in source) is deliberately not asserted: the retried walk either skips the light (suppression window) or re-refuses and moves on -- both are the must-have behavior
- L2 (listening light) needs no retire put of its own: hearing L1's retire put moves its head past the prune target, so both lights erase the same target with their seals already in place

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] The plan's controlled-broadcast-order premise cannot be reproduced on this gossip layer**
- **Found during:** Task 1 first runs (2 of 6 runs flaky-red in each direction)
- **Issue:** "wire the broadcasts before the fetch window" in a controlled L-then-F order assumes the requester learns heads only from those triggers. In reality the topic graft relays every CACHED seeding publish at once (the Case-1 message-cache-on-join mechanism), so the pruned CID's first route is whichever cached publish the cache delivers first -- sometimes the light (refusal, as wanted), sometimes an early serving route (no refusal ever observed), and late block deliveries occasionally rescue the would-be refusal mid-wait
- **Fix:** Split the proof. Part 1 (hermetic): routes registered in order through the public AddRoute with the pruned light FIRST, then one direct getNode -- the production route-walk entry -- proving consult/refusal/CID-accounting/failover deterministically (the responder's miss is storage-deterministic, asserted in-test). Part 2 (mesh): the E2E convergence asserted ordering-free. Every must-have truth is still proven; only the wiring shape changed
- **Files modified:** test/src/crdt/globaldb_erase_test.cpp
- **Verification:** 8/8 consecutive greens + double-run + full ctest target green
- **Committed in:** `fa0193012`

**2. [Rule 3 - Blocking] A directly-fetched block silences the broadcast intake for that CID**
- **Found during:** Task 1 part-2 runs (all values stuck absent after part 1)
- **Issue:** part 1's getNode leaves the pruned CID's block in the requester's LRU; the mesh intake's HasBlock check then treats the CID as present, never queues its root job, and its CRDT delta never merges -- the value never lands
- **Fix:** discard the LRU copy between the parts via the public DeleteCIDBlock (the same call RetryDueFailedRoots makes before a refetch), so the mesh flow processes the pruned CID through the real broadcast intake like any fresh node
- **Files modified:** test/src/crdt/globaldb_erase_test.cpp
- **Verification:** convergence green from the first run after the fix
- **Committed in:** `fa0193012`

**3. [Rule 3 - Blocking] PrunedMesh on one shared topic is inherently order-nondeterministic**
- **Found during:** Task 2 design (mapped before implementation from Task 1's cache findings)
- **Issue:** with all four nodes on one topic, the fresh node's first route for the history head belongs to whichever cached publish arrives first; a light-owned route serves the head but starves the PRUNED links under it (the lights refuse, no other route exists) -- the Pitfall-6 ladder ride appears as a coin-flip instead of a prevented failure
- **Fix:** the full node owns a serving topic (its dual-topic put links the mesh history under one serving head); the fresh node subscribes the serving topic FIRST (the full node is the only possible route source there -- the Pitfall-6 wiring asserted as a precondition), then joins the mesh topic after convergence proving the no-storm property. All four nodes DO share the mesh topic; the serving topic follows the suite's established kTracerTopic two-topic pattern
- **Files modified:** test/src/crdt/globaldb_erase_test.cpp
- **Verification:** 8/8 greens; counter exactly in [1,4] with the ladder exactly at baseline; Phase C exactly zero fetches
- **Committed in:** `250d74246`

**4. [Rule 1 - Observability] Per-PeerInfo wire-status accounting is not publicly observable (A5)**
- **Found during:** Task 1 log analysis
- **Issue:** HasRecentCIDFailure/GetRequestStatus are private; the plan's "per-PeerInfo accounting if observable" leg needed an observable
- **Fix:** the classification is proven through its public consequences: IsOnBlackList(light) stays false through the refusal (a connection-failure classification would have blacklisted it -- the exact CID-vs-connection distinction), and the light still serves its fresh data afterwards; the refusal's storage-side determinism is asserted directly (GetNodeFromMerkleDAG fails on the pruned node). Observed transport behavior recorded (see key-decisions); no status constant guessed
- **Files modified:** test/src/crdt/globaldb_erase_test.cpp
- **Verification:** all green runs assert both consequences
- **Committed in:** `fa0193012`

---

**Total deviations:** 4 auto-fixed (3 Rule 3 blocking, 1 Rule 1 observability)
**Impact on plan:** All must-have truths proven; every deviation changed HOW the tests wire the scenario, never WHAT they assert. No assertion was weakened to fit a guess.

## Issues Encountered
- The global fetch-attempt counter dedups (EnqueueRootPID pending/active check) and includes cross-node enqueues, so its exact value wobbles with delivery timing (observed 1-3 in PrunedMesh Phase B); per-instance ladder counts are the exact observable and carry the load-bearing assertions
- Cognitive-complexity 112/113 on the two new test bodies (readability warning class; the file's existing tests already fire at 44-66 and the 19-02/19-04 summaries document acceptance for scenario-shaped test bodies); clang-format clean via git-clang-format on changed lines; clang-tidy findings on new lines are the file's established idiom classes (internal-linkage, C-style constexpr char[] topic arrays, unchecked operator[] on getNodes())
- A failed sibling root's head update is lost with its failed job (pendingHeadsByRootCID_ erase on failure); the rescue root supersedes it as the converged head -- a product behavior this test documents rather than fights

## Threat Mitigations (per plan threat_model)
- **T-19-0701** (failover storm): hermetic walk asserts zero root enqueues; mesh window asserts attempts bounded (<=8 RefusalFailover, <=4 PrunedMesh Phase B) and the ladder drains to baseline -- DONE
- **T-19-0702** (route starvation / Pitfall 6): PrunedMesh asserts the serving head exists and the delivery path is up BEFORE the broadcast-triggered fetch window, and the fresh node's only route source is the full node; ladder stays exactly at baseline (a starved ladder ride would leave entries) -- DONE
- **T-19-0703** (guessed wire constant): no status constant asserted; behavior (refusal classified CID-specific, never blacklist) + storage-deterministic miss asserted; observed transport behavior recorded in key-decisions -- DONE
- **T-19-0704** (flaky multi-node green): every deterministic-by-construction assertion is exact; delivery-timing-dependent bounds carry slack; 8x sweeps + double-runs + full ctest green; deterministic teardown order untouched (the destructor path is shared) -- DONE
- **T-19-07SC** (supply chain): zero registry installs; test-only plan -- DONE

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- SURV-03 is closed; the phase's E2E criterion (SC-4) is proven on four real pubsub nodes
- SURV-02's peer half is closed here; its LOCAL half (tri-state "history pruned" answers, D-03) remains 19-05's deliverable -- mark SURV-02 complete only after 19-05 lands
- The `entryApplied` helper and the ordered-AddRoute hermetic pattern are available for any future peer-behavior proof on this topology

## Self-Check: PASSED

The one modified file exists (test/src/crdt/globaldb_erase_test.cpp, 1290 lines -- artifact min_lines 120 satisfied); both task commits (fa0193012, 250d74246) present in git log; `*RefusalFailover*` and `*PrunedMesh*` filters green twice consecutively; full globaldb_integration green via ctest; no production file touched (git show --stat on both commits).

---
*Phase: 19-pruned-node-survival-recovery-hardening*
*Completed: 2026-10-23*
