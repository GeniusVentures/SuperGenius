---
phase: 12-multi-node-finality-fault-proof
plan: 15
subsystem: test-stability/evidence-attribution
tags: [flakiness-attribution, check-then-act-race, graphsync-blacklist, missed-wakeup, uat-gap-closure, evidence-gate, no-reroll]
requires:
  - 12-14 preserved full-suite evidence logs (/tmp/p12_14_full_{a,b,c}.log, _a_rerun_failed, _b_load_failed, _ab_neworder_{4,5}, _ab_oldorder_{1,2,3})
  - 12-REVIEW.md WR-02 mechanism statement
  - SameBurn first-wait predicate and HasBridgeMarker helper (multi_node_finality_fault_test.cpp)
provides:
  - Per-signature attribution verdicts for the three intermittent full-suite failure signatures, each backed by preserved-log citations (log filename + timestamp + log-line)
  - SameBurn first-wait predicate closure over the durable bridge-marker boundary (four HasBridgeMarker terms added, mirroring the in-file post-restart wait)
  - RestartAtVote residual-mechanism record: certificate-CID graphsync route loss + blacklist of the reused host identity after restart, with no surviving re-publication (WR-02 parked-hold excluded 4/4)
  - PublisherLoss child-readiness characterization (same first-failing readiness boundary across strikes; heterogeneous child-death causes) for 12-17 gate risk assessment
affects:
  - 12-17 formal three-consecutive-serial-pass gate (per-run risk quantification inputs)
  - Any future scoped plan for post-restart certificate re-publication / graphsync blacklist expiry
tech-stack:
  added: []
  patterns:
    - wait on the exact durable boundary being asserted (no check-then-act gap between a poll predicate and the immediately-following assertions)
key-files:
  created:
    - .planning/phases/12-multi-node-finality-fault-proof/12-15-SUMMARY.md
  modified:
    - test/src/blockchain/multi_node_finality_fault_test.cpp
key-decisions:
  - "SameBurn verdict: confirmed check-then-act race in 3 independent failing runs (marker always lands within the same second AFTER the assertion) — wait-predicate repair authorized and applied."
  - "RestartAtVote verdict: WR-02 parked-hold EXCLUDED (old peer-one teardown ~1s before same-root reopen in 4/4 failing runs); residual mechanism is certificate-CID graphsync route loss with blacklisting of the reused host identity and no surviving re-publication — WR-02 production shutdown-path repair WITHHELD (authorization gate failed on both clauses)."
  - "PublisherLoss child-readiness: characterization only, no repair (12-08/D-18/D-19/D-25 discipline); both preserved strikes share the same first-failing readiness boundary."
requirements-completed: [TEST-01, TEST-04]
metrics:
  duration: pending
  completed: 2026-09-02
---

# Phase 12 Plan 15: Multi-Node Finality Fault Suite Flakiness Attribution and Stabilization Summary

Per-signature attribution verdicts built from the 11 preserved 12-14 evidence logs: the SameBurn signature is a confirmed test check-then-act race (fixed by closing the wait predicate over the durable marker boundary), the RestartAtVote signature is a certificate-CID graphsync route loss after restart (WR-02 parked-hold excluded 4/4, so the production shutdown-path repair is withheld and the residual mechanism recorded for a scoped decision), and the PublisherLoss child-readiness signature is characterized but not repaired per the standing twice-reproduced discipline.

*Task 1 (attribution) committed below; Task 2 (authorized repair) and Task 3 (focused + one honest full-serial confirmation run) outcomes are appended in the final revision of this file.*

## Signature attribution (Task 1)

### Verdict 1 — SameBurnContention HasBridgeMarker (:2098; :2081 in pre-12-14 line numbering): CONFIRMED test check-then-act race — repair AUTHORIZED and applied

Mechanism: the first convergence wait (multi_node_finality_fault_test.cpp:2088-2094) requires only `CheckCertificateForSlot(passive)` and `MintEffects==1` on all four peers. `mint_effects_for_test_` increments at TransactionManager.cpp:5518 — BEFORE the tracking-table effects-applied update and `PersistBridgeExecutedMarker`'s datastore put at :5532. The wait can therefore pass while the winner's marker write is still in flight; the immediately-following loop (:2095-2099) asserts `HasOnlyWinnerOutput`/`HasBridgeMarker` and fails. In every failing instance the marker then lands within the same second — a race in the test wait, not a durability defect.

Failing-run evidence (3 independent instances, both build orders, identical sub-second ordering):

1. `/tmp/p12_14_full_c.log` — failure dump at log-line 1335, 13:40:42. Failing peer `4b514df6`, winner `2f42ca23`. That peer logged "did not consume every burn input" + "Created tokens (mint-v2), amount 42" immediately before the failure (mint effects applied); its "ChangeTransactionState: Tracking entry confirmed tx=2f42ca23" (log-line ~1359) and "OnConsensusCertificate: Standalone validator confirmed tx 2f42ca23" (~1360) land ~25 log lines AFTER the failure block, same second 13:40:42.
2. `/tmp/p12_14_full_b_load_failed.log` — failure dump at log-line 1340, 12:27:49. Failing peer `55cc1c8a`, winner `07b5babf`. "Tracking entry confirmed" + "Standalone validator confirmed" land after the failure dump, same second 12:27:49.
3. `/tmp/p12_14_ab_oldorder_2.log` (pre-12-14 control build) — failure dump at log-line 1355, 12:49:57, reported at source line :2081. Failing peer `33cb1c23`, winner `32c17aa0`. Same ordering: confirmation lines after the failure dump, same second. The :2081/:2098 pair is ONE assertion at two line-number generations (commit b9ad7d2b added 17 lines above it), not two different checks.

Authorization: check-then-act ordering confirmed in 3 failing runs (gate required >= 2). REPAIR 1 applied (Task 2): the first wait now also requires `HasBridgeMarker( first/second/third/passive, *winner )`, mirroring the in-file post-restart wait at :2111-2112; timeout unchanged at 20s; second wait byte-identical.

### Verdict 2 — RestartAtVote block-3 certificate reconvergence (:2471-2476): WR-02 parked-hold EXCLUDED (4/4); residual mechanism = certificate-CID graphsync route loss + blacklist of the reused host identity, no surviving re-publication — repair WITHHELD

Parked-hold scoring (old peer-one teardown vs same-root reopen; POSITIVE required > ~5s), all four failing runs:

| Run | Old peer-one teardown | Same-root reopen | Gap | Score |
|-----|----------------------|------------------|-----|-------|
| /tmp/p12_14_full_c.log | `~TransactionManager: Metrics [89c5c3a9]` + `~GlobalDB CALLED` + `GlobalDB shutdown finished` 13:42:03 (log-lines 5240/5256/5258) | `Opening database .../restart-mint-validator-one/rocksdb` 13:42:04 (5319) | ~1s | NEGATIVE |
| /tmp/p12_14_full_a_rerun_failed.log | close 12:35:33 (5252) | reopen 12:35:34 (5315) | ~1s | NEGATIVE |
| /tmp/p12_14_full_b_load_failed.log | close 12:29:08 (5265) | reopen 12:29:09 (5328) | ~1s | NEGATIVE |
| /tmp/p12_14_ab_neworder_4.log | close 13:01:04 (5262) | reopen 13:01:05 (5320) | ~1s | NEGATIVE |

The 30-second parked barrier waiter never occurred: the old peer's TransactionManager and GlobalDB were always fully destroyed before the same-root reopen, so WR-02's missed-wakeup → held-GlobalDB → same-root contention chain is excluded as the cause of this signature.

Residual mechanism — identical in all four failing runs:

- Pre-restart, exactly ONE `/cert/mint-v2` CRDTCallbackManager callback occurs in the whole block — on the old peer-one (canonical C: 13:42:03, log-line 5237). The certificate NEVER arrived on second, third, or passive at any timestamp (their last observed cert-ingress: none in the run).
- The old peer-one's in-flight certificate processing is aborted by the restart itself ("OnConsensusCertificate: Failed to change transaction state to CONFIRMED ... Operation canceled", "Failed to process certificate ... error=Operation canceled", canonical C 13:42:03) — expected for the barrier-armed handler woken by `stopped_`.
- At the reopen second, the peers still missing the certificate try to graphsync-fetch the cert DAG CID from the OLD (dead) publisher host: "Request failed for CID <cert-node> from peer 12D3Koo... with connection error CANNOT_CONNECT (or CONNECTION_ERROR). Blacklisting peer and trying fallback" followed by "No usable route candidates left for CID" — canonical C 13:42:04 (log-lines 5339-5341), a_rerun 12:35:34, b_load 12:29:09, neworder_4 13:01:05. The restarted peer-one reuses the SAME libp2p host identity (same db root), which the fetchers had just blacklisted; no surviving peer holds the CID, so no fallback exists.
- Zero `/cert/mint-v2` callbacks on any peer during the entire 25s wait window (4/4 runs): no re-publication fires after the restart (GossipPubSub does not replay publications made before a peer link exists, and no recovery path re-publishes an already-completed certificate).
- The recreated peer-one converges via its own durable readback ("Tracking entry confirmed tx=..." + "Standalone validator confirmed", canonical C 13:42:04) — so the three peers failing :1260 are second, third, and passive, each with MintEffects==0.
- Passing contrast: /tmp/p12_14_ab_oldorder_2.log completed RestartAtVote green — TWO cert callbacks fired before the old host closed (12:51:17, log-lines 5348/5371; close 12:51:18 at 5405): the propagation race was won before the only provider died. This is a per-run race between cert propagation to the other three peers and the barrier-entered signal that triggers RestartPeer.
- Green full-run logs (/tmp/p12_14_full_a.log, _b.log, _ab_neworder_5.log) are ctest summary-only (361 bytes) — no convergence-latency distribution extractable; the detailed passing contrast above stands in.

Authorization decision for REPAIR 2 (WR-02): the plan's gate was "parked-hold evidenced in >= 2 failing runs OR all competing mechanisms excluded while the source-level contract violation is confirmed". Clause 1 fails (0/4 — excluded above). Clause 2 fails: a competing mechanism is CONFIRMED, not excluded. The WR-02 contract violation itself is real in source (unlocked `notify_all` in TransactionManager::Stop, TransactionManager.cpp:321-322, and ConsensusManager::Close, Consensus.cpp:158-159, against the 30s barrier waiters at TransactionManager.cpp:2111-2126 / Consensus.cpp:1343-1356 and the weak_ptr last-strong-reference handler at TransactionManager.cpp:128-149), but it never manifested in any preserved run and fixing it cannot stabilize this signature. Per Task 1D and the twice-reproduced-repair discipline (12-07/12-08), REPAIR 2 is WITHHELD: no production shutdown-path change ships from this plan. The contract violation stays open under 12-REVIEW WR-02 for a separately scoped decision. Per the Task-2 action's own instruction ("if Task 1 attributed RestartAtVote to something other than the parked hold and WR-02, record it and leave the suite alone"): no RestartAtVote test change, no timeout change, no barrier change, no mint-retry-path change was made.

### Verdict 3 — PublisherLoss child-readiness (:1871 pre-shift / :1888 current): characterized, NOT repaired (12-08 / D-18 / D-19 / D-25 discipline upheld)

Strikes carrying full preserved dumps (both on the pre-fix control build order): `/tmp/p12_14_ab_oldorder_1.log` (FAILED at log-line 119, window 12:42-12:49, 42702 ms) and `/tmp/p12_14_ab_oldorder_3.log` (FAILED at log-line 131, window 12:52-12:55, 33222 ms). Both strikes share the SAME first-failing readiness boundary: `:1871 evidence->Origin() = "child-writer-probe-or-nonqualifying"` (expected "real-socket-publisher-loss"), followed by `Classification = "invalid_or_partial_blocked"` and `CountWeight = 0`. The underlying child-death causes differ — strike 1: `ChildStatus = "normal-exit-nonzero"`; strike 2: `ChildStatus = "abnormal-signal"` with `FooterStatus = "missing-or-ambiguous-footer"` and `ControlStatus = "invalid-or-missing-frame-size"`. Common boundary: a launched scenario child failed to produce a qualifying terminal evidence record; the death mode is heterogeneous.

In the 11 preserved round-2 full runs this test failed in exactly these 2 (both pre-fix control order; ~18% per-run contribution to suite risk). The deferred-items ledger's count of 3 includes a strike whose detailed log was not preserved in this round's set (STATE.md separately records one 12-12-era low-load strike). Per the standing discipline (three isolated publisher-loss runs previously passed readiness; D-25 requires two matching fresh fully-attributed failures with direct fixture lifecycle proof before repair authorization), no repair is attempted here; this characterization feeds 12-17's gate risk assessment.

## Task 2 and Task 3 outcomes

*(appended in the final revision after the repair and verification runs complete)*
