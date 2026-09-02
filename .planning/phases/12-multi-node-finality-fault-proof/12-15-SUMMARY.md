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
  - SameBurn first-wait predicate closure over the durable bridge-marker boundary (four HasBridgeMarker terms added, mirroring the in-file post-restart wait); 3/3 focused SameBurn passes on the stabilized build
  - RestartAtVote residual-mechanism record: certificate-CID graphsync route loss + blacklist of the reused host identity after restart, with no surviving re-publication (WR-02 parked-hold excluded 4/4); 3/3 focused passes and one green full-serial confirmation run
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
  - "SameBurn verdict: confirmed check-then-act race in 3 independent failing runs (marker always lands within the same second AFTER the assertion) — wait-predicate repair authorized and applied; 3/3 focused passes."
  - "RestartAtVote verdict: WR-02 parked-hold EXCLUDED (old peer-one teardown ~1s before same-root reopen in 4/4 failing runs); residual mechanism is certificate-CID graphsync route loss with blacklisting of the reused host identity and no surviving re-publication — WR-02 production shutdown-path repair WITHHELD (authorization gate failed on both clauses); the review's contract violation stays open for a separately scoped decision."
  - "PublisherLoss child-readiness: characterization only, no repair (12-08/D-18/D-19/D-25 discipline); both preserved strikes share the same first-failing readiness boundary."
  - "One full serial confirmation run executed and preserved exactly as it came out (Passed, 211.67s, pre-run 1-min load 1.40) — no rerolls."
requirements-completed: [TEST-01, TEST-04]
metrics:
  duration: 25m
  completed: 2026-09-02
---

# Phase 12 Plan 15: Multi-Node Finality Fault Suite Flakiness Attribution and Stabilization Summary

Per-signature attribution verdicts built from the 11 preserved 12-14 evidence logs: the SameBurn signature is a confirmed test check-then-act race (fixed by closing the wait predicate over the durable marker boundary), the RestartAtVote signature is a certificate-CID graphsync route loss after restart (WR-02 parked-hold excluded 4/4, so the production shutdown-path repair is withheld and the residual mechanism recorded for a scoped decision), and the PublisherLoss child-readiness signature is characterized but not repaired per the standing twice-reproduced discipline; verification: 6/6 focused runs green (3 SameBurn + 3 RestartAtVote) plus one honest full-serial pass (211.67s) at load 1.40.

## Signature attribution (Task 1)

### Verdict 1 — SameBurnContention HasBridgeMarker (:2098; :2081 in pre-12-14 line numbering): CONFIRMED test check-then-act race — repair AUTHORIZED and applied

Mechanism: the first convergence wait (multi_node_finality_fault_test.cpp:2088-2094, pre-edit) required only `CheckCertificateForSlot(passive)` and `MintEffects==1` on all four peers. `mint_effects_for_test_` increments at TransactionManager.cpp:5518 — BEFORE the tracking-table effects-applied update and `PersistBridgeExecutedMarker`'s datastore put at :5532. The wait could therefore pass while the winner's marker write was still in flight; the immediately-following loop asserted `HasOnlyWinnerOutput`/`HasBridgeMarker` and failed. In every failing instance the marker then landed within the same second — a race in the test wait, not a durability defect.

Failing-run evidence (3 independent instances, both build orders, identical sub-second ordering):

1. `/tmp/p12_14_full_c.log` — failure dump at log-line 1335, 13:40:42. Failing peer `4b514df6`, winner `2f42ca23`. That peer logged "did not consume every burn input" + "Created tokens (mint-v2), amount 42" immediately before the failure (mint effects applied); its "ChangeTransactionState: Tracking entry confirmed tx=2f42ca23" (log-line ~1359) and "OnConsensusCertificate: Standalone validator confirmed tx 2f42ca23" (~1360) land ~25 log lines AFTER the failure block, same second 13:40:42.
2. `/tmp/p12_14_full_b_load_failed.log` — failure dump at log-line 1340, 12:27:49. Failing peer `55cc1c8a`, winner `07b5babf`. "Tracking entry confirmed" + "Standalone validator confirmed" land after the failure dump, same second 12:27:49.
3. `/tmp/p12_14_ab_oldorder_2.log` (pre-12-14 control build) — failure dump at log-line 1355, 12:49:57, reported at source line :2081. Failing peer `33cb1c23`, winner `32c17aa0`. Same ordering: confirmation lines after the failure dump, same second. The :2081/:2098 pair is ONE assertion at two line-number generations (commit b9ad7d2b added 17 lines above it), not two different checks.

Authorization: check-then-act ordering confirmed in 3 failing runs (gate required >= 2). REPAIR 1 applied (Task 2).

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

## What Was Done (Tasks 2 and 3)

### Task 2: Apply the authorized stabilizations — commit `60e449bb`

- REPAIR 1 applied: the SameBurnContention first wait (multi_node_finality_fault_test.cpp:2088-2095) now also requires `HasBridgeMarker( first, *winner ) && HasBridgeMarker( second, *winner ) && HasBridgeMarker( third, *winner ) && HasBridgeMarker( passive, *winner )`, mirroring the in-file post-restart wait exactly. Existing MintEffects and passive-certificate terms and the 20s bound unchanged; the second wait byte-identical to before. Diff: +3/-1 lines, one file.
- REPAIR 2 WITHHELD with the Task-1 evidence recorded above; `TransactionManager::Stop()` and `ConsensusManager::Close()` are untouched (verified post-edit: both still show the bare notifies). Consequences honored: no shutdown-path change, so the plan's "broader-consumer rebuild note" and the lifecycle/cert-fallback regression runs (Task 3 step 3) are moot — see Deviations.
- Rebuild: `multi_node_finality_fault_test`, `consensus_pending_lifecycle_test`, `transaction_manager_certificate_fallback_test` all compile clean (binary confirmed relinked at build/OSX/Release/test_bin/).

### Task 3: Focused verification plus one honest full-serial confirmation run (no code changes)

Port preflight before direct invocation: `lsof -nP -iTCP:54631..54634 -sTCP:LISTEN` returned no listeners (exit 1, no output).

| Run | Test | Result | Elapsed | 1-min load before | Log |
|-----|------|--------|---------|-------------------|-----|
| 1 | SameBurnContention focused | PASSED | 17.19s | 1.61 | /tmp/p12_15_sameburn_1.log |
| 2 | SameBurnContention focused | PASSED | 17.68s | 1.86 | /tmp/p12_15_sameburn_2.log |
| 3 | SameBurnContention focused | PASSED | 17.17s | 1.68 | /tmp/p12_15_sameburn_3.log |
| 4 | RestartAtVote focused | PASSED | 53.95s | 1.78 | /tmp/p12_15_restart_1.log |
| 5 | RestartAtVote focused | PASSED | 55.48s | 1.51 | /tmp/p12_15_restart_2.log |
| 6 | RestartAtVote focused | PASSED | 54.95s | 1.10 | /tmp/p12_15_restart_3.log |
| 7 | Full serial suite (`ctest -R '^multi_node_finality_fault_test$'`) | **Passed, 211.67s** | 211.67s | 1.40 | /tmp/p12_15_confirm_1.log |

- Mechanical gate: all six focused logs assert zero `[  FAILED` lines — FOCUSED_RUNS_ZERO_FAILED echoed.
- Full-serial confirmation: exactly one run executed, outcome preserved verbatim (Passed; 100% tests passed, 0 failed). No rerun was performed for any recorded outcome. Post-run 1-min load read 2.15 (driven by the suite's own CPU during the run; 5-min average 1.82, no external spike — the 12-14 deviation-3 load discipline was applied: pre-run 1-min load 1.40 < 2).
- Which signature struck in the full run: NONE — all three targets green in the same run, for the first time in a preserved detailed-attribution session since the review-fix generation. SameBurn's pass is consistent with the predicate closure; RestartAtVote's pass is a race win (the unfixed cert-propagation race remains a per-run risk in full-suite order — see Findings).

## Deviations from Plan

### Withheld repair (plan-authorized STOP branch, not an auto-fix)

**1. [Task 1D STOP branch] REPAIR 2 (WR-02 notify-under-paired-mutex) withheld — authorization gate failed on both clauses**
- **Found during:** Task 1 signature-B attribution
- **Issue:** The plan pre-authorized the WR-02 repair iff the parked-hold signature appeared in >= 2 failing runs OR all competing mechanisms were excluded with the source violation confirmed. Evidence: 0/4 failing runs show a parked hold (old peer-one teardown always ~1s before same-root reopen), and a competing mechanism is positively confirmed (cert-CID graphsync CANNOT_CONNECT → blacklist of the reused host identity → "No usable route candidates left", zero cert callbacks in the 25s window, identical 4/4).
- **Action:** Repair not applied; Stop()/Close() verified untouched; the contradiction recorded per the plan's Task 1D instruction ("STOP — do not apply the contradicted repair; record the contradiction"). The WR-02 contract violation remains open under 12-REVIEW WR-02 for a separately scoped decision.
- **Files modified:** none (withheld)
- **Commit:** none (withheld)

**2. [Rule 3 - Procedure] Task 3 step 3 (lifecycle/cert-fallback regression runs) skipped as moot**
- **Found during:** Task 3
- **Issue:** The step exists to cover the production shutdown-path change from REPAIR 2; with REPAIR 2 withheld there is no production change for those binaries to regress against.
- **Action:** Both targets were still REBUILT as part of Task 2's compile verification (clean); the runtime re-runs were skipped. Recorded rather than silently dropped.
- **Files modified:** none
- **Commit:** none (procedure)

Otherwise the plan was executed exactly as written; every executed test outcome above is preserved with no rerolls.

## Must-Have Truth Status

| Truth | Status |
|-------|--------|
| Every intermittent failure signature has a written attribution verdict backed by preserved-log citations | MET — three verdicts above, each citing log filename + timestamp + log-line |
| SameBurn wait predicate covers the exact durable boundary it asserts (bridge marker on all four peers) | MET — four HasBridgeMarker terms added mirroring :2111-2112; 3/3 focused passes |
| TransactionManager::Stop() and ConsensusManager::Close() notify each CV while holding its paired mutex (WR-02 closed) | **NOT MET — intentionally withheld**: the plan's authorization gate failed (parked-hold excluded 0/4; competing mechanism confirmed); recorded per Task 1D instead of applied |
| Focused SameBurnContention and RestartAtVote runs pass three consecutive times each; one full serial run executed with outcome preserved honestly | MET — 3/3 + 3/3 focused (zero FAILED asserted mechanically); one full-serial Passed (211.67s) preserved at /tmp/p12_15_confirm_1.log, no rerolls |

## Findings

- The SameBurn signature is fully closed: a pure test-side check-then-act race between the MintEffects counter (TransactionManager.cpp:5518) and the marker write (:5532), with no durability defect (the marker always landed the same second in all 3 failing instances).
- The RestartAtVote signature is a per-run propagation race that is NOT fixed by this plan: the certificate must reach second/third/passive before RestartPeer kills the only CID provider. In failing runs the fetchers' graphsync requests land after the host dies, the (reused) host identity is blacklisted, and no surviving replica or re-publication path exists — the 25s wait then cannot converge. Focused runs (3/3 here) and full runs can both win the race; 12-17's formal gate carries this residual per-run risk, quantified by the 12-14 ledger at roughly 4/8 full-run failures on the pre-12-15 build (the mechanism is untouched by this plan's test-only change).
- The missed-wakeup window WR-02 describes is real in source but was never observed: every barrier wakeup in the preserved runs was prompt. A future scoped decision should weigh the notify-under-paired-mutex contract closure (with the plan's verified deadlock-safety analysis) against the evidence that the observed failures need cert re-publication/blacklist-expiry handling instead.
- The PublisherLoss child-readiness intermittence stands (2/11 preserved full runs, ~18%): same first-failing readiness boundary across strikes, heterogeneous child-death modes — exactly the profile the 12-08 discipline holds un-repaired without two matching fully-attributed fresh failures.

## Known Stubs

None — the shipped change is real test behavior (wait predicate over a durable boundary); no placeholders, no unwired data.

## Threat Flags

None — no new security surface. T-12-15-01 (deadlock from notify-under-lock) is moot: the repair it assessed was withheld. T-12-15-02 (evidence integrity) honored: single recorded full run, mechanical zero-FAILED assertion, no rerolls. T-12-15-03: the predicate extension waits on the exact durable boundary already asserted at :2100 with no timeout increase. T-12-15-04: manual lsof port preflight performed before direct invocation (no listeners).

## Self-Check: PASSED

- Files: test/src/blockchain/multi_node_finality_fault_test.cpp (modified, commit 60e449bb) and .planning/phases/12-multi-node-finality-fault-proof/12-15-SUMMARY.md (created, commit 65cceb89) both present.
- Commits 65cceb89 (Task 1 attribution) and 60e449bb (Task 2 repair) verified in git log; Task 3 intentionally modifies no source file (verification runs, logs preserved under /tmp/p12_15_*).
- Evidence logs present: /tmp/p12_15_sameburn_{1,2,3}.log, /tmp/p12_15_restart_{1,2,3}.log, /tmp/p12_15_confirm_1.log.
