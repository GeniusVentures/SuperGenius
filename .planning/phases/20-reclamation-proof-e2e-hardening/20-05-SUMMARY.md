---
phase: 20-reclamation-proof-e2e-hardening
plan: "05"
subsystem: test
tags: [txpruner, sc-4, mesh-retention, full-node, archive-node, real-pubsub, compaction, gate-02, reclamation, erasenodecollection]

# Dependency graph
requires:
  - phase: 20-reclamation-proof-e2e-hardening plan 01
    provides: GlobalDB::CompactStorage + GetApproximateDiskUsageBytes (the light's whole-DB rewrite + size mutation check)
  - phase: 20-reclamation-proof-e2e-hardening plan 04
    provides: the sibling suites on this same file (RealPrune_PostCompaction seeding shape, the 256KB padded-owner recipe, the settled-cycle discipline)
  - phase: 19-pruned-node-survival-recovery-hardening plan 07
    provides: the EraseNodeCollection mesh recipe (makeNode loopback-advertise, one-direction dial, bounded per-instance waits, the Case-2 subscribe-before-dial lesson)
provides:
  - MeshRetention suite — SC-4 closed: two GlobalDB-only peers (nominally full and archive) in the light's real-pubsub mesh hold byte-identical 100% of the transaction history (values, per-tx /crdt/ entries, CID blocks, head map) across BOTH the light's erase cycles AND its whole-DB compaction cycles, non-vacuously (pre-prune per-peer polls + light-side mutation checks) and stably (8x sweep)
  - The AddMeshPeer fixture helper — the EraseNodeCollection recipe as reusable fixture members on the TxPrunerTest stack (peers fixture-owned before any wait that can fail, torn down through sgns::test::stopGlobalDBNode ahead of the fixture's own chain)
affects: [] # final plan of the phase and milestone

# Tech tracking
tech-stack:
  added: [] # zero new dependencies
  patterns:
    - "union-baseline round 2: a second seed/prune/compact round baselines the UNION of round-1 and round-2 keys/tx-paths/CIDs, so its checkpoints prove CUMULATIVE retention (round-1 history still byte-identical after a second erase+compaction pair), not round-2 isolation"
    - "peers-are-fixture-owned-before-the-first-failing-wait: the peer pushes into mesh_peers_ immediately after db->Start()+AddListenTopic, so even a dial-timeout failure path tears every resource down through the committed helper"

key-files:
  created: []
  modified:
    - test/src/account/tx_pruner_test.cpp

key-decisions:
  - "Peer teardown follows the COMMITTED fixture order (pinned at execution per the plan's instruction): sgns::test::stopGlobalDBNode — ShutdownNow drains the CRDT workers, pubsub Stop joins the networking thread, db released under the reactor keepalive — ahead of the fixture's own teardown chain (blockchain Stop -> tm/blockchain/account reset), not the older responder shape"
  - "Round 2 RUNS (the plan left it conditional): it costs ~1.9s on this fixture (peers already up), so the criterion's cycles-plural is evidenced rather than argued"
  - "Round-2 baselines carry the UNION of both rounds' surfaces plus an explicit cumulative-retention pin (every round-1 value/block byte-identical inside the round-2 baseline) — a strictly stronger proof than a second isolated round"
  - "Peer baseline = values + per-tx /crdt/ element entries + CID blocks + head map, each read through the peer's own public API (Get/QueryRaw/GetRaw/GetCRDTHeadList); the light-local raw tx->CID map entries are deliberately NOT a peer surface (raw datastore puts never publish — the peers can never hold them)"
  - "The peer spin carries a topic-mesh belt beyond the connected-pair wait (getPeerCount(full topic) >= 1) so every seeding publish flows to a live mesh, and the two waits are split (connectedness, then mesh) for precise failure messages"
  - "/crdt/work/ is excluded from peer CRDT baselines exactly as the fixture helper excludes it — pending-publish plumbing whose presence depends on drain timing, not on any prune action"

patterns-established:
  - "Non-vacuous mesh retention: prove the baseline is real BEFORE the mutation exists (bounded per-peer polls of every value/block/head + non-empty baseline pins), then prove byte-identity across the mutation on BOTH sides (peers identical; the mutator's store provably lacks the erased keys)"

requirements-completed: [GATE-02]

# Metrics
duration: 35min
completed: 2026-10-24
---

# Phase 20 Plan 05: Mesh Retention (SC-4) Summary

**Two GlobalDB-only peers (nominally full and archive) in the light node's real-pubsub mesh provably hold 100% of the transaction history — byte-identical across BOTH of the light's erase cycles AND both of its whole-DB compaction cycles — with non-vacuous pre-prune baselines and an 8x stability sweep; SC-4 and the phase's last success criterion close.**

## Performance

- **Duration:** 35 min (18:44-19:19 UTC)
- **Tasks:** 2
- **Files modified:** 1 (test/src/account/tx_pruner_test.cpp only)

## Accomplishments
- **Task 1 (02fd7a75d):** the mesh fixture + the erase half. `AddMeshPeer` spins GlobalDB-only peers on the 19-07 EraseNodeCollection recipe (loopback-advertised `makeNode` wiring, pubsub-context scheduler, graphsync Network) subscribed to kSeedTopic BEFORE the one-direction dial (the Case-2 announce-at-connect lesson), torn down through `sgns::test::stopGlobalDBNode` ahead of the fixture's own chain. The MeshRetention suite seeds RealPrune's ~256KB shape on the light, bounded-waits until BOTH peers hold every value, every block, and the converged head (per-instance public-surface polls, 25s deadline), pins the baselines non-empty, then runs the light's settled prune (erased==2). CHECKPOINT 1: both peers byte-identical on all four surfaces (values, per-tx /crdt/ entries, blocks, head map) while the light's erased keys are locally absent (value gone, map gone, marker PRUNED, blocks gone).
- **Task 2 (a89dd5aee):** the compaction half + the second cycle. CHECKPOINT 2: ONE whole-DB `CompactStorage(nullptr)` on the light through the production surface with the size-drop mutation check, then both peers re-snapshotted byte-identical to the SAME pre-prune baselines; light-side correctness across prune+compact (markers PRUNED, erased blocks gone, every seal marker survived GATE-03, unspent sibling byte-identical on every level). ROUND 2 (union baselines + cumulative-retention pin): two fresh candidates on the same mesh, second prune (erased==2) + second compaction (size drops again), both peer checkpoints re-proven on the union surfaces, round-1 markers still PRUNED after the second compaction. Stability: 8x consecutive MeshRetention greens, full binary 41/41 twice, ctest target green.

## Task Commits

Each task was committed atomically:

1. **Task 1: mesh fixture + peer baselines + prune-round retention** - `02fd7a75d` (test)
2. **Task 2: compaction checkpoint + second cycle + stability sweeps** - `a89dd5aee` (test)

## Files Created/Modified
- `test/src/account/tx_pruner_test.cpp` — three additions: the `crdt/crdt_options.hpp` / `testutil/globaldb_node_shutdown.hpp` / `base/sgns_version.hpp` includes; the `MeshPeer` struct + `mesh_peers_` member + `AddMeshPeer` helper beside the 19-06 responder members with the peer teardown loop first in the fixture destructor; the MeshRetention section (PeerHistory + the five section-local peer-read helpers + the TEST_F) between SpendRace and CrashFixedPoint. No existing test or suite touched.

## Decisions Made
- **Peer teardown order pinned to the committed tree** (the plan instructed pinning at execution): `stopGlobalDBNode` — ShutdownNow -> pubsub Stop -> db reset under the reactor keepalive — executed BEFORE the fixture's own teardown chain, not the responder's member-by-member shape. The fixture comment documents the order in place.
- **Round 2 runs** (~1.9s incremental — cheap on this fixture; the plan's conditional resolved to yes), and its baselines are UNION baselines with an explicit cumulative-retention pin rather than a second isolated round.
- **Role-gate non-duplication honored:** the peers carry a one-line comment pointing at `TickGate_NonLightRolesRefusedAtConstruction`; the Full/Archive factory refusal is not re-asserted (research Open Q3).
- **GATE-02 marked complete** (the plan frontmatter's requirement): the mesh suite is its network-side carrier; the local-side carrier landed in 20-04.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] The one-direction dial call was lost in the peer-spin restructuring**
- **Found during:** Task 1 first run (both waits timed out; no "Adding new peer" in the logs)
- **Issue:** The comment said "ONE-direction dial" but the restructured helper (peers pushed into `mesh_peers_` before the wait so failure paths own their resources) no longer contained the `AddPeers` call itself — nothing ever dialed the light, so connectedness could never become CONNECTED. Diagnosed by splitting the single conjunction wait into connectedness + topic-mesh stages (kept: clearer failures) and confirming the dial address was valid while the dial never fired.
- **Fix:** `live.pubsub->AddPeers( { pubs_->GetInterfaceAddress() } )` restored before the waits; green from the first run afterwards.
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Committed in:** 02fd7a75d

---

**Total deviations:** 1 auto-fixed (Rule 1 — ordinary new-test authoring iteration; no plan-mandated behavior changed)
**Impact on plan:** None on scope or assertions; single declared file touched.

## Verification Evidence
- Task 1: `--gtest_filter='TxPrunerTest.MeshRetention*'` PASSED twice consecutively (2.5s / 2.4s); full binary 41/41 green; post-format rerun green.
- Task 2: filter PASSED (~4.3s with round 2); **8/8 consecutive sweep greens**; full binary 41/41 PASSED twice consecutively (58.9s / 58.9s); `ctest -R tx_pruner_test` green (59.2s).
- Sleep audit: no new `sleep_for` sites — every wait is an `ASSERT_WAIT_FOR_CONDITION` bounded poll (25s propagation, 10s dial/mesh deadlines), per the 19-07 recipe.
- clang-format (llvm@22 git-clang-format vs each task's base): fixes applied in Task 1 (Task 2 was clean on first pass after); clang-tidy: not applicable (addtest targets disable tidy; test-only plan).
- Post-commit checks: no file deletions in either task commit; no untracked files left behind.
- The one compile warning (`tx_pruner_test.cpp` sign-compare in `FindUtxoRecord`) is pre-existing code, shifted by the insertions — out of scope, not introduced here.

## Issues Encountered
None beyond the deviation above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 20 (and milestone v1.2) is complete: 5/5 plans executed. The phase gate (full suite + the dedicated reclamation gate target) is the next step before /gsd:verify-work.

## Known Stubs
None — no placeholder logic landed.

## Threat Flags
None — the plan's register is mitigated as written: T-20-0501 (vacuous retention) by the bounded pre-prune per-peer polls + non-empty baseline pins + the light-side mutation checks, T-20-0502 (flaky mesh green) by the 19-07 recipe verbatim (one-direction dial, loopback advertise, subscribe-before-dial) + bounded per-instance waits + the direct deterministic CompactStorage + the 8x sweep, T-20-0503 (compaction propagating as change) by CHECKPOINT 2 byte-identity against the SAME pre-prune baselines in BOTH rounds, T-20-0504 (test interference) by the fixture-owned peers + the committed teardown order + the full-binary double-run. No security-relevant surface beyond the register (test-only changes; zero registry installs).

## Self-Check: PASSED

- test/src/account/tx_pruner_test.cpp exists on disk (git-clean after both commits).
- Task commits verified: `02fd7a75d`, `a89dd5aee` present on `gsd/phase-20-reclamation-proof-e2e-hardening`.

---
*Phase: 20-reclamation-proof-e2e-hardening*
*Completed: 2026-10-24*
