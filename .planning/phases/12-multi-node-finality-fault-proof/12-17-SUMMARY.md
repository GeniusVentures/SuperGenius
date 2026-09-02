---
phase: 12-multi-node-finality-fault-proof
plan: 17
subsystem: evidence-gate/gate-entry-risk
tags: [evidence-gate, gate-entry-check, no-reroll, go-no-go, restart-at-vote, graphsync-blacklist, risk-assessment]
requires:
  - 12-15 per-signature attribution verdict table and its preserved confirmation run (/tmp/p12_15_confirm_1.log)
  - 12-16 honest full-run record (/tmp/p12_16_full_1.log) and its attribution section
provides:
  - The gate-entry check verdict for round 3, built from the plan's own rule over 12-15's verdict table and both preserved full-run records, with the predicted-failure analysis (signature, citations, expected strike point, quantified risk) the developer's go/no-go requires
  - An unspent round-3 no-reroll evidence budget (zero gate runs executed; no /tmp/p12_17_triple_* logs exist because the series was never started)
affects:
  - The developer go/no-go decision this summary routes (burn the budget on a predicted failure vs. fund a scoped repair of the RestartAtVote residual first)
  - Any future scoped plan for post-restart certificate re-publication / graphsync blacklist expiry
tech-stack:
  added: []
  patterns: []
key-files:
  created:
    - .planning/phases/12-multi-node-finality-fault-proof/12-17-SUMMARY.md
  modified:
    - .planning/STATE.md
key-decisions:
  - "Gate-entry check FIRED: the RestartAtVote signature that struck in 12-16's preserved full run is recorded live and unrepaired in 12-15's verdict table (repair withheld, both authorization clauses failed), so the plan's own Task-1 STOP branch applies — the three-run series was NOT started and the round's single no-reroll evidence budget is preserved."
  - "The two authorized repairs do not address the striking mechanism: 12-15's SameBurn fix is a different test's wait predicate and 12-16's CR-01/CR-02/WR-01 are teardown-order changes outside the failing mid-test recovery path (12-16's own attribution: '12-16's changes are not in the failing path')."
metrics:
  duration: 20m
  completed: 2026-09-02
---

# Phase 12 Plan 17: Final Evidence Gate (Three Consecutive Serial Passes) Summary

**STOPPED AT GATE-ENTRY (plan-faithful branch): the Task-1 gate-entry check fired — a signature recorded live and unrepaired in 12-15's verdict table struck again in 12-16's preserved full run on the final build — so the three-run series was never started, the no-reroll evidence budget is unspent, and an explicit go/no-go is routed to the developer. This is not a plan failure; it is the outcome the plan's own rule prescribes.**

## Gate-Entry Check (Task 1, step 1) — RULE FIRES

The rule: record GO only if every signature that struck in the consulted runs was (a) repaired and focused-verified this round, or (b) attributed to recorded load contamination of a preserved run. Any striking signature recorded live and unrepaired → do NOT start the series; write the predicted-failure analysis and STOP, routing a developer go/no-go. All inputs below were verified directly this session (logs read, not trusted from summaries).

### Consulted inputs

| Input | Outcome | Signature(s) that struck |
|-------|---------|--------------------------|
| 12-15 verdict table (12-15-SUMMARY.md "Signature attribution") | Verdict 1 SameBurn: repaired + focused-verified (clause a satisfied for its signature). Verdict 2 RestartAtVote: repair WITHHELD — authorization gate failed BOTH clauses. Verdict 3 PublisherLoss child-readiness: characterized, NOT repaired — the one residual the plan declares gate-eligible without a go/no-go. | n/a (this is the attribution record) |
| 12-15 Task 3 honest full run (/tmp/p12_15_confirm_1.log, verified) | **Passed, 211.67s**, 100% tests passed, 0 failed, pre-run 1-min load 1.40 | NONE — all three targets green in the same run |
| 12-16 Task 3 honest full run (/tmp/p12_16_full_1.log, verified) | **FAILED — CTest Timeout, 300.06s**, pre-run 1-min load 1.58 (below the 2.0 contamination threshold) | **RestartAtVote** — FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce |

### Clause evaluation of the striking signature (RestartAtVote, in 12-16's run)

- **Clause (a) — repaired and focused-verified this round: NOT SATISFIED.** 12-15 Verdict 2's authorization decision failed both clauses ("Clause 1 fails (0/4 — excluded above). Clause 2 fails: a competing mechanism is CONFIRMED, not excluded"), so REPAIR 2 (WR-02 notify-under-paired-mutex) is WITHHELD. Additionally, 12-15's own analysis states the withheld repair could not have stabilized this signature anyway ("fixing it cannot stabilize this signature") — the confirmed mechanism needs certificate re-publication / blacklist-expiry handling, which no authorized repair provides.
- **Clause (b) — attributed to recorded load contamination: NOT SATISFIED.** The 12-16 run started at 1-min load 1.58, under the load-2 discipline threshold; 12-16's attribution is the pre-attributed certificate-CID graphsync route-loss race, not contamination.
- **Residual-mechanism exclusion: NOT SATISFIED.** Neither authorized repair addresses it: 12-15's SameBurn fix is a wait-predicate closure in a different test case, and 12-16's CR-01/CR-02/WR-01 teardown propagation is outside the failing path (12-16's attribution: "multi_node_finality_fault_test.cpp is untouched by this plan, the failing waits are mid-test recovery predicates").

**Verdict: a striking signature is recorded live and unrepaired → the STOP branch applies. The series was not started.**

## Predicted-Failure Analysis (why the series is expected to fail if run now)

**Signature:** RestartAtVote block-3 certificate reconvergence — FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce, the per-run race between certificate propagation to second/third/passive peers and the barrier-entered signal that triggers RestartPeer killing the only cert-CID provider.

**Verdict-table citation (12-15-SUMMARY.md, Verdict 2):** "WR-02 parked-hold EXCLUDED (4/4); residual mechanism = certificate-CID graphsync route loss + blacklist of the reused host identity, no surviving re-publication — repair WITHHELD." Mechanism identical in all four 12-14 failing runs: pre-restart exactly ONE /cert/mint-v2 callback (on the old peer-one); after restart the fetchers hit "CANNOT_CONNECT. Blacklisting peer and trying fallback" on the dead, reused host identity, then "No usable route candidates left for CID"; zero cert callbacks in the 25s window (GossipPubSub does not replay pre-link publications and no recovery path re-publishes a completed certificate).

**Post-attribution confirmation it is live (12-16, final build generation — verified on-log this session):**

- line 4358 (19:38:27): "Request failed for CID QmPZELYSEeCH7ykpK9SuNmqHhJyQqBkP9jUQprm6woVPh8 ... CANNOT_CONNECT. Blacklisting peer and trying fallback."
- line 4360: "No usable route candidates left for CID QmPZELYS..."
- lines 5537-5569 (19:39:50): the same blacklist dead-end repeated for a second CID (QmYgRH8...)
- wait-timeout cascade: lines 4406 (20s "surviving peers retained the accepted certificate before receiver restart"), 4672 (25s), 5063 (20s), 5731 (25s) — ~90s of waits that exhaust the 300s CTest budget so the process is killed before gtest prints FAILED (12-16's run: Timeout, 300.06s).

**Expected strike point if the series were run:** run A, B, or C — any of the three — inside RestartAtVote, seconds after the post-restart graphsync fetches begin (12-16: first CANNOT_CONNECT ~25s after test start), producing a CTest Timeout rather than a gtest FAILED line. Because a Timeout consumes a full 300s run slot and the no-reroll rule makes any single strike fail the entire series, the series result is determined by the first unlucky race loss.

**Quantified risk (recorded numbers, cited):** RestartAtVote residual ~4/8 full-run failures on the pre-12-15 build (12-15 Findings); 1/1 strike in 12-16's single full run on the final build generation. PublisherLoss child-readiness adds ~18%/run un-repaired (gate-eligible, see quote below). At ~50% RestartAtVote and ~18% PublisherLoss per-run risk, the probability of three consecutive passes is roughly (0.5 x 0.82)^3 ≈ 7% — consistent with 12-14's recorded "~12% per attempt at the observed ~50% per-run pass rate." SameBurn, the one repaired signature, no longer contributes.

## PublisherLoss Child-Readiness — 12-15's Characterization (quoted per the plan's requirement)

The plan requires quoting 12-15's characterization before any run A; it is quoted here for the record even though the series was not started (12-15-SUMMARY.md, Verdict 3):

> "Strikes carrying full preserved dumps ... Both strikes share the SAME first-failing readiness boundary: `:1871 evidence->Origin() = "child-writer-probe-or-nonqualifying"` (expected "real-socket-publisher-loss"), followed by `Classification = "invalid_or_partial_blocked"` and `CountWeight = 0`. The underlying child-death causes differ — strike 1: `ChildStatus = "normal-exit-nonzero"`; strike 2: `ChildStatus = "abnormal-signal"` with `FooterStatus = "missing-or-ambiguous-footer"` and `ControlStatus = "invalid-or-possible-frame-size"` [sic: invalid-or-missing-frame-size]. Common boundary: a launched scenario child failed to produce a qualifying terminal evidence record; the death mode is heterogeneous. In the 11 preserved round-2 full runs this test failed in exactly these 2 ... (~18% per-run contribution to suite risk) ... no repair is attempted here; this characterization feeds 12-17's gate risk assessment."

This residual remains gate-eligible WITHOUT a go/no-go (the developer funded the gate knowing it) — but it compounds the predicted-failure probability above; it is not the trigger of this stop.

## What Was and Was Not Executed

**Executed (Task 1, step 1 only):** the gate-entry check — direct reads of 12-15-SUMMARY.md's verdict table, /tmp/p12_15_confirm_1.log, /tmp/p12_16_full_1.log (including its mechanism lines), 12-16-SUMMARY.md's attribution, 12-14-SUMMARY.md's no-reroll/load precedent, and STATE.md's standing records. Zero test executions, zero builds, zero source changes.

**Deliberately NOT executed (STOP branch):** Task 1 steps 2-7 (rebuild, crash baseline marker /tmp/p12_17_ips_baseline, the three gate runs /tmp/p12_17_triple_{a,b,c}.log, crash absence assertion, per-log case confirmation) and all of Task 2 (sibling suite matrix, STATE.md gate-resolution note). No /tmp/p12_17_* log or marker exists; the no-reroll evidence budget is untouched. Task 2's resolution note is replaced by the STATE.md blocker entry recording this stop (below).

## Developer Go/No-Go (routed by this stop)

**The question:** burn round 3's single-shot three-run evidence budget on a series the verdict table predicts will fail (~7-12% pass odds), or return to attribution/repair first?

| Option | Content | Consequence |
|--------|---------|-------------|
| **NO-GO — repair first (recommended)** | Fund a scoped repair plan for the confirmed residual mechanism: post-restart certificate re-publication (or graphsync blacklist expiry / surviving-replica guarantee) so second/third/passive can converge after RestartPeer kills the sole CID provider. Then re-attempt this gate — a NEW attributed fix is the only legitimate re-entry under the no-reroll rule. | The budget is spent only when the prediction says it can succeed; the plan's must-have (three consecutive passes) becomes reachable rather than a ~7-12% lottery. WR-02 itself is NOT the fix (12-15: it "cannot stabilize this signature"). |
| GO — run anyway | Developer explicitly accepts the prediction; execute Task 1 steps 2-7 and Task 2 verbatim. | ~88-93% chance the series FAILS on a RestartAtVote Timeout (or a PublisherLoss child-readiness strike), the budget is spent, and the outcome re-records what 12-14/12-15/12-16 already established. |
| Re-scope the gate | Redefine the evidence standard (e.g., accept focused-run evidence). | Contradicts the phase's own 12-12/12-14 standard; not recommended — soften nothing. |

**Recommendation: NO-GO — repair first.** The gate-entry rule exists in this plan precisely so the round's evidence budget is never spent on a verdict-table-predicted failure; the mechanism, expected strike point, and re-publication gap are all already attributed, so the repair target is well-defined.

## Deviations from Plan

None — the stop is the plan's own Task-1 STOP branch, executed exactly as written (the plan's <done> criterion: "the series is never started over a verdict-table-predicted failure without the developer's explicit go").

## Must-Have Truth Status

| Truth | Status |
|-------|--------|
| Gate-entry check consults 12-15's verdict table before run A; a live unrepaired signature routes a developer go/no-go BEFORE the series | **MET** — this document is that record; the series was not started |
| Three consecutive serial full-suite passes on the final build with zero new crash reports | **NOT EVALUATED — withheld by the gate-entry rule** (honest state: the evidence does not exist; the 12-14 evidence-gap blocker stays open pending the developer's decision) |
| Restart exact-once and publisher-loss green inside all three gate logs | **NOT EVALUATED** — no gate logs exist |
| Sibling suite matrix fresh on the final build | **NOT EVALUATED** — Task 2 not executed (depends on the gate outcome it records) |
| STATE.md carries the honest round-3 record | MET — a dated 12-17 entry under Blockers/Concerns records the stop, the preserved budget, and the pending go/no-go; no other STATE.md content changed |

## Known Stubs

None — no code was written; this is an evidence/decision artifact.

## Threat Flags

None. T-12-17-01 (evidence integrity) is honored in its strongest form: the no-reroll budget was not spent on a predicted failure, exactly zero gate runs exist, and every claim above cites a preserved log or verdict-table line verified this session. T-12-17-02: STATE.md records the stop accurately and touches no historical note or Deferred Items row. T-12-17-03: no runs were executed, so no load contamination applies.

## Self-Check: PASSED

- Files: .planning/phases/12-multi-node-finality-fault-proof/12-17-SUMMARY.md (created, commit 74a8653b) and .planning/STATE.md (12-17 gate-entry STOP blocker entry, decision entry, position line, and session record) both present on disk.
- Commit 74a8653b verified in git log.
- Budget-preservation proof: `ls /tmp/p12_17_*` returns no matches — no gate log, no crash baseline marker exists; zero test runs were executed by this plan.
- STATE.md integrity: grep -c "Deferred" unchanged at 7 (no Deferred Items row or milestone field altered); historical blocker notes intact (the 12-17 note is appended after the 12-16 update, nothing rewritten).
