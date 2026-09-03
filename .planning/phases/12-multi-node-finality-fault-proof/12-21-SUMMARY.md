---
phase: 12-multi-node-finality-fault-proof
plan: 21
subsystem: multi-node-finality-fault-proof
tags: [round-5, gap-closure, option-a, restart-recovery, gossip-mesh, wr-06, graduation]
requires:
  - "12-19 (re-publication block in restart-mint; Option A decision record)"
  - "12-20 (gate-entry clauses and round-5 re-entry contract)"
provides:
  - "Composed Option A repair in RestartAtVote restart-mint block (graduated attributed fix, 3/3 focused all-green)"
  - "WR-06 closed: clamped getBackoffTimeout exponent at both branches"
  - "Clause-(a) input for 12-22's gate re-attempt (three all-green focused logs exist)"
affects:
  - "12-22 (gate-entry clause (a) and (c) inputs now satisfied for RestartAtVote)"
tech-stack:
  added: []
  patterns:
    - "pre-restart retention wait creating surviving replicas before publisher restart (block-2 shape, distinct message per leg)"
    - "readiness gate on gossip-topic degree >= 2 forcing a connected 4-peer mesh before re-advertisement"
    - "exponent clamp (failures < 16 ? failures : 16) before 1ULL << exponent"
key-files:
  created:
    - .planning/phases/12-multi-node-finality-fault-proof/round5-traces/option-a-restart-1.log
    - .planning/phases/12-multi-node-finality-fault-proof/round5-traces/option-a-restart-2.log
    - .planning/phases/12-multi-node-finality-fault-proof/round5-traces/option-a-restart-3.log
  modified:
    - test/src/blockchain/multi_node_finality_fault_test.cpp
    - src/crdt/impl/graphsync_dagsyncer.cpp
    - .planning/STATE.md
decisions:
  - "Option A implemented as the COMPOSED repair (both named sub-shapes), not either/or — each attacks one of the two 12-19-attributed mechanisms and both stay inside the developer's named shapes"
  - "Readiness gate threshold >= 2 topic peers on every peer — degree-2 minimum on 4 peers forces a connected mesh (disconnected component of size k caps degree at k-1)"
  - "Passive included in the pre-restart retention wait alongside the named second/third, following the block-2 precedent (wait on every non-restarted peer)"
  - "IN-08 deliberately left in place — round-5 production-change budget is the WR-06 clamp alone"
metrics:
  duration: 9m
  completed: 2026-09-03T17:24:46Z
---

# Phase 12 Plan 21: Round-5 Option A Composed Repair + WR-06 Clamp Summary

Implemented the developer-funded Option A decision verbatim as the composed repair for the RestartAtVote block-3 reconvergence race — a pre-restart CheckCertificateForSlot retention wait creating surviving certificate-CID replicas plus a consensus-topic readiness gate (>= 2 peers on every peer, forcing a connected mesh) before the existing 12-19 re-publication — and graduated it 3/3 focused all-green as the attributed fix 12-22's gate re-attempt carries; WR-06's unbounded shift exponent clamped at both getBackoffTimeout branches in the same build.

## What Was Done

### Task 1: Option A verbatim in the restart-mint block (commit 97bb5f6d, +45/-0)

**Insertion A** — between `EXPECT_FALSE( HasBridgeMarker( network.first, *winner ) );` and `RestartPeer( network.first );`: an `ASSERT_WAIT_FOR_CONDITION` mirroring block 2's shape exactly (`[&]` lambda, `std::chrono::seconds( 20 )`, `nullptr` trailing arg) requiring `CheckCertificateForSlot( slot )` on `network.second`, `network.third`, AND `network.passive`, with the distinct message "surviving peers retained the accepted certificate before publisher restart". The wait runs while `first` is still alive and routable, so recipients' CRDT CID fetches from `first` succeed — proving AND creating >= 3 surviving replicas before the boot-window blacklist can erase the restarted publisher's route (the route_count=1 mechanism 12-19 attributed). Comment cites the Option A decision (2026-09-03, STATE.md:143, commit 633d6ff1), the 12-19 attribution, and the Consensus.cpp SubmitCertificate :2126-2152 ordering evidence (verified in-source: convergent-immutable CRDT put on `consensus_datastore_topic_` at ~:2127 and `Publish( message )` at ~:2147 both precede the mint-effects barrier).

**Insertion B** — between `ConnectPeers( Peers( network ) )` and the 12-19 re-publication comment: an `ASSERT_WAIT_FOR_CONDITION` whose range-for predicate requires `peer->pubsub->getPeerCount( sgns::MultiNodeFinalityFaultTestAccess::ConsensusTopic( peer->consensus ) ) >= 2` for every peer in `Peers( network )`, bounded `std::chrono::seconds( 10 )` — a NEW bound for a NEW wait; no existing bound (15s/20s/25s/5s) touched — with the message "consensus-topic mesh re-formed with at least two topic peers on every peer before certificate re-advertisement". Connectivity argument (in the comment): with four peers, minimum topic-degree 2 forces a connected mesh — a disconnected component of size k permits maximum degree k-1 (2+2 split caps degree at 1, 3+1 split leaves the singleton at degree 0) — so the predicate cannot hold on a partitioned mesh; `ConnectPeers` proves only >= 1 topic peer plus libp2p links, exactly the race 12-19 attributed. The existing 12-19 re-publication block and its comment stayed byte-identical and execute immediately after the gate passes.

**Guards (all pass):** `git diff -U0 | grep -cE "^-[^-]"` = 0 (insertions only); new-message grep counts 1 each; block-2 "...before receiver restart" message count still 1; `PutConvergentImmutable` count 0 (CERT-02); exactly one `SubmitCertificate(` call site at the existing :2514-anchored position, textually after the readiness gate (CERT-05); build 100% with zero warnings on the file (verified via forced recompile).

### Task 2: WR-06 clamp — the only production change of round 5 (commit 0e99efa3, +12/-4)

Both `getBackoffTimeout` branches (ever-connected 5000/30000; never-connected 10000/1800000) now declare `const uint64_t exponent = failures < 16 ? failures : 16;` and compute `base_ms * ( 1ULL << exponent )`; both `return std::min( timeout, max_ms );` unchanged. The first site carries the WR-06 rationale comment (failures increments without bound via AddToBlackList, entries never erased, resets only on successful connection → UB at failures >= 64; caps reached by 2^6 and 2^8, so clamp-at-16 preserves every reachable behavior). The test-only override short-circuit at the top of the function is untouched (RestartAtVote's 100 ms seam); constants verified unchanged at the same sites. **IN-08 deliberately left in place** — the dead `TIMEOUT_SECONDS`/`MAX_FAILURES` constants remain: round-5's production-change budget is the clamp alone per the gap-closure directive.

**Guards (all pass):** `1ULL << failures` count 0; `1ULL << exponent` count 2; clamp expression count 2; `blacklist_backoff_override_ms_for_test_.load` count 1, textually before both branches; build exits 0.

### Task 3: Graduation series — 3/3 ALL GREEN → GRADUATED ATTRIBUTED FIX (commit cad2109d)

`round5-traces/` created (round4-traces/ untouched, nothing overwritten). Port preflight `lsof -nP -iTCP:54601-54634 -sTCP:LISTEN` empty before every run. Load discipline satisfied outright — every pre-run 1-min load below 2 (no deviation to record, unlike 12-18/12-19); loads recorded in each log header.

| Run | Pre-run 1-min load | Outcome | Wall | Log |
|-----|--------------------|---------|------|-----|
| 1 | 1.33 | OK | 55.01s | round5-traces/option-a-restart-1.log |
| 2 | 1.23 | OK | 55.66s | round5-traces/option-a-restart-2.log |
| 3 | 1.27 | OK | 54.98s | round5-traces/option-a-restart-3.log |

- Every run contains `[       OK ] FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` and zero `Timed out waiting for condition` lines — the 12-20 clause-(a) standard (1 pass / 1 fail graduates nothing; 12-19's precedent).
- Wall times sit at the historical green mark (~55s vs the ~98s failure cascades) in all three runs.
- Exact-once safety in every run: zero `AssertSingleDurableMint`/`AssertOneLiveMintEffect` failures.
- Zero new crash reports: the newest `multi_node_finality_fault_test` .ips is still 2026-09-02-125541 (the pre-round-4 baseline); zero crash reports of any binary today.
- Zero `/tmp` paths anywhere in the round-5 evidence; all logs committed under round5-traces/.
- **VERDICT: the composed Option A repair is a GRADUATED ATTRIBUTED FIX** — this is the attributed fix the 12-22 gate re-attempt carries, per 12-20's re-entry contract. The STOP branch was not taken.

STATE.md carries the dated 12-21 entry (composed shape with the connectivity argument, WR-06 clamp with the IN-08 scope note, the three outcomes with loads and wall times, the graduated verdict) and the Session Continuity block was updated.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Guard conflict] Comment wording adjusted twice to satisfy the plan's mechanical grep gates**
- **Found during:** Task 1 and Task 2
- **Issue:** The plan requires comments citing `PutConvergentImmutable`'s broadcast (Task 1) and the `1ULL << failures` UB expression (Task 2), but the same plan's acceptance greps require `grep -c "PutConvergentImmutable"` on the test file = 0 and `grep -c "1ULL << failures"` on graphsync_dagsyncer.cpp = 0 — the literal tokens inside comments tripped both gates.
- **Fix:** Reworded the comments to preserve the full citation substance without the literal tokens: "the certificate's convergent-immutable CRDT put broadcasting on consensus_datastore_topic_" and "shifting 1ULL by the raw failure count is UB". No code semantics changed.
- **Files modified:** test/src/blockchain/multi_node_finality_fault_test.cpp, src/crdt/impl/graphsync_dagsyncer.cpp
- **Commit:** folded into 97bb5f6d / 0e99efa3 (fixed before each task's commit)

No other deviations. Load discipline needed no deviation (all pre-run loads below 2). The STOP branch was not exercised.

## Evidence Locations

- round5-traces/option-a-restart-1.log — run 1 OK (load header, 55.01s)
- round5-traces/option-a-restart-2.log — run 2 OK (load header, 55.66s)
- round5-traces/option-a-restart-3.log — run 3 OK (load header, 54.98s)
- STATE.md dated 12-21 entry — verdict and full record

## Verification Results

- Task 1 automated verify: PASS (diff gate 0 deleted lines; both message counts 1; PutConvergentImmutable 0; build 100%)
- Task 2 automated verify: PASS (old shift 0, clamped shift 2, clamp expression 2; build 100%)
- Task 3 automated verify: PASS (3/3 OK lines, 0 timeouts, 0 FAILED; STATE.md 12-21 entries present)
- Safety invariants across every run: no duplicate mint, no new CRDT-write site, no new crash reports

## Self-Check: PASSED

All four created files exist on disk; all three task commits (97bb5f6d, 0e99efa3, cad2109d) verified in git log.
