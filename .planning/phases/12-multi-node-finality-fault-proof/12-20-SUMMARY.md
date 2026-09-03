---
phase: 12-multi-node-finality-fault-proof
plan: 20
subsystem: evidence-gate/gate-entry-rule
tags: [evidence-gate, gate-entry-check, no-reroll, withheld-series, round-4, option-a, round-5-scope, honest-stop]
requires:
  - round4-traces/hypothesis-verdict.md verdict token (DISPROVEN at :9, single occurrence)
  - 12-18/12-19 focused evidence (round4-traces/restart-focused-{1,2,3}.log, round4-traces/fallback-restart-{1,2}.log)
  - 12-15-SUMMARY.md verdict table (Verdicts 1-3; the gate-eligible PublisherLoss characterization)
  - Developer Option-A decision (2026-09-03, commit 633d6ff1) — round-5 scope before this gate re-attempt
provides:
  - The round-4 gate-entry verdict: NO attributed fix carries (clause (a) and clause (c) both FAIL), so the three-run series is withheld with the no-reroll evidence budget unspent
  - The round-5 re-entry contract: Option A repair plan (test-file only, no wait bound relaxed) with all-green focused evidence, then this gate re-attempts under the no-reroll rule
affects:
  - Round-5 planning (Option A: readiness-gated re-advertisement OR pre-restart CheckCertificateForSlot wait creating a true surviving replica)
  - The 12-14 evidence-gap blocker (stays OPEN)
  - TEST-01..TEST-06 completion marking (stays unmarked)
tech-stack:
  added: []
  patterns: []
key-files:
  created:
    - .planning/phases/12-multi-node-finality-fault-proof/12-20-SUMMARY.md
  modified:
    - .planning/STATE.md
    - .planning/ROADMAP.md
key-decisions:
  - "Gate-entry check FIRED on clauses (a) and (c): no attributed fix from round 4 exists (12-18 seam hypothesis DISPROVEN; 12-19 re-publication repair necessary-but-insufficient at 1 pass / 1 fail with its own honest STOP), and RestartAtVote block-3 stays live-unrepaired on the final source beyond the pre-declared gate-eligible PublisherLoss residual — the series was withheld, the round's no-reroll budget is unspent, and the STOP branch recorded per the plan's own Step-1 rule (mirroring 12-17)."
  - "Requirements TEST-01..TEST-06 were NOT marked complete and plan counters were not advanced (12-17 no-counters precedent): the gate evidence does not exist, and marking on a withheld series would soften the outcome."
metrics:
  duration: 8m
  completed: 2026-09-03
---

# Phase 12 Plan 20: Round-4 Gap Closure Part 3 (Evidence Gate Re-attempt) Summary

**STOPPED AT GATE-ENTRY (plan-faithful branch): the Task-1 gate-entry check fired before run A — round 4 produced NO attributed fix (clause (a) FAIL), and a second live-unrepaired signature beyond the gate-eligible PublisherLoss residual stands (clause (c) FAIL) — so the three-run series was never started, the no-reroll evidence budget is unspent, and round 5 (Option A repair, then this gate) is the routed continuation. This is not a plan failure; it is the outcome the plan's own rule prescribes.**

## Gate-Entry Check (Task 1, Step 1) — RULE FIRES

The rule: re-entry into the single-shot series is legitimate ONLY through a new attributed fix produced by 12-18/12-19, verified on disk before ANY run. All inputs below were verified directly this session (files read and grepped, not trusted from summaries).

### Clause (a) — exactly one round-4 attributed fix with all-green focused evidence: FAIL (primary)

- The verdict token: `round4-traces/hypothesis-verdict.md:9` carries exactly one verdict token in the file — `## VERDICT: DISPROVEN`. Clause (a)'s DISPROVEN branch therefore applies, which requires **`fallback-restart-1/2/3.log` all containing the OK line from 12-19's repair**.
- On disk:
  - `round4-traces/fallback-restart-1.log:2813` — `[       OK ] FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce (54748 ms)` (the only green run)
  - `round4-traces/fallback-restart-2.log:2985` — `[  FAILED  ] FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce (97854 ms)` — NOT an OK line
  - `round4-traces/fallback-restart-3.log` — **does not exist**, intentionally (12-19's own Step-4 STOP clause: "no run 3, no repair iteration beyond the directive's named shapes")
- Therefore NO round-4 attributed fix carries into the gate:
  - 12-18's seam is hypothesis-disproven, not a fix — `restart-focused-2.log:2943` FAILED (98020 ms) with the override provably live (hypothesis-verdict.md "Proof the Override Was Live").
  - 12-19's re-publication repair is necessary-but-insufficient — 1 pass / 1 fail, and the failing run carries the same signature on the final build (see clause (c) citations below).
- Clause (a) verdict: **FAIL** — the no-reroll re-entry condition (a new attributed fix) is unmet.

### Clause (b) — phase-target rebuild then scoped mtime assert: MOOT, not formally executed

- The clause's rebuild exists to prepare a series that clause (a) already forbids; per the 12-17 STOP precedent (zero builds, zero runs), no rebuild was performed and the series was withheld with it.
- Informational read-only status (recorded for completeness, not a gate pass): all four phase binaries postdate the newest round-4 source — `build/OSX/Release/test_bin/multi_node_finality_fault_test` mtime epoch 1788443858 (2026-09-03 10:57:38) > newest source `test/src/blockchain/multi_node_finality_fault_test.cpp` 1788443833 (10:57:13); `consensus_pending_lifecycle_test` binary 1788441529 > its source 1788441494; cert-fallback 1788441440 and smoke 1788441450 are round-4 rebuilds. Clause (b) would not have been the blocker.

### Clause (c) — no OTHER live-unrepaired signature beyond the pre-declared PublisherLoss residual: FAIL

Consulted 12-15's verdict table and every round-4 log, verified in current source:

- SameBurn (12-15 Verdict 1): repaired and source-verified — the HasBridgeMarker wait predicate covering all four peers is present in current source (`multi_node_finality_fault_test.cpp:2095`). CLOSED.
- CR-03: closed by 12-18 — `node.registry.reset()` between manager and db resets is present at both lifecycle teardown loops (`consensus_pending_lifecycle_test.cpp:1333`, `:2035`), with the ValidatorRegistry.hpp:530 co-ownership comment; `round4-traces/lifecycle-fixed.log` green. CLOSED.
- PublisherLoss child-readiness (12-15 Verdict 3): characterized, NOT repaired, ~18%/run — the ONE residual the gate declares eligible WITHOUT a new developer decision (developer-funded when the gate was first approved). Gate-eligible; not the trigger.
- **RestartAtVote block-3 reconvergence (12-15 Verdict 2's mechanism): LIVE-UNREPAIRED on the final source.** The clause's premise "RestartAtVote is covered by the attributed fix" is false — clause (a) established no fix exists, and the signature struck 12-19's run 2 on the current build: `fallback-restart-2.log` :2214/:2248/:2251 `CANNOT_CONNECT. Blacklisting peer` on the reused identity `12D3KooW...MKWVcg`, :2218/:2250 `No usable route candidates left for CID QmPQpcb...`, :2458 (25s `recreated Mint peer repaired its marker`) and :2874 (20s `Mint-boundary recovery stayed exact`) wait timeouts, :2985 FAILED. The developer's **Option A** decision (received 2026-09-03, recorded verbatim in 12-19-SUMMARY.md and STATE.md, commit 633d6ff1) is funded as **round-5 scope** and is NOT in the source — the restart-mint block carries only 12-19's single-shot re-publication (`SubmitCertificate` at `multi_node_finality_fault_test.cpp:2514`); no readiness-gated re-advertisement and no pre-restart CheckCertificateForSlot retention wait exists in that block.
- Clause (c) verdict: **FAIL** — a second live-unrepaired signature (RestartAtVote block-3) exists beyond the gate-eligible PublisherLoss residual.

**Gate-entry outcome: STOP — the series must not run.** Per the plan's Step 1: "If any of (a)-(c) fails: do NOT start the series — write the gate-entry STOP record into STATE.md (dated 12-20) with the failed clause, and end this task. This mirrors 12-17's own STOP branch." The dated STOP record is in STATE.md (commit 6c242ccb).

## Withheld-Series Analysis

Why withholding is correct under the plan's own rules:

1. **No-reroll integrity (T-12-20-01/05):** the series is the round's single evidence spend; the gate-entry rule admits re-entry ONLY through a new attributed fix. 12-18 ended in a DISPROVEN hypothesis (the seam landed, the fix theory did not); 12-19 ended in its own honest STOP (repair executed successfully but stabilized nothing — 1 pass / 1 fail ≈ the historical ~50% race). Running now would burn the budget on the exact predicted failure the rule exists to prevent, at roughly (0.5 x 0.82)^3 ≈ 7-12% series-pass odds (12-17's quantified risk, unchanged by round 4).
2. **The routed decision is already received:** the developer selected Option A (readiness-gated re-advertisement OR pre-restart CheckCertificateForSlot wait making a true surviving replica; test-file only; no existing wait bound relaxed) and funded it as round-5 scope BEFORE the 12-20 gate re-attempt (STATE.md 12-19 entry, commit 633d6ff1). The repair-first path is thus not pending — it is the funded next step; spending the gate budget before landing it would invert the developer's recorded ordering.
3. **Honest artifact handling:** every artifact the plan's steps assume for a running series was intentionally not produced (see below) rather than synthesized, softened, or partially run.

## What Was and Was Not Executed

**Executed (Task 1, Step 1 only):** the gate-entry check — direct reads/greps of hypothesis-verdict.md, fallback-restart-{1,2}.log, restart-focused-{1,2,3}.log, 12-15-SUMMARY.md's verdict table, current source (multi_node_finality_fault_test.cpp, consensus_pending_lifecycle_test.cpp), and a read-only informational mtime check of the four phase binaries. STATE.md STOP record written and committed (6c242ccb). Task 2's finalize-per-outcome collapsed into that record.

**Deliberately NOT executed (STOP branch):**

- Task 1 Steps 2-4: `round4-traces/crash-baseline.txt`, the three gate runs `round4-traces/gate-{a,b,c}.log`, the per-run xunit copies `round4-traces/gate-{a,b,c}.xml` — **intentionally not produced because the series was withheld** (the crash baseline exists only as the pre-series marker for a running series; the per-run xunit copies are defined only for executed runs). All verified absent; none synthesized.
- Task 2: the sibling matrix `round4-traces/sibling-matrix.log` — not run. The matrix's evidentiary value is "on the same build generation" as the gate series; with the series withheld it has no gate to accompany (12-17 precedent: no part of the series or the Task-2 sibling matrix runs from a stopped gate). No port preflight was needed (no run).
- Zero test executions, zero builds, zero source changes by this plan.

## Round-5 Re-Entry Requirements

1. A new scoped plan implements **Option A verbatim** (12-19-SUMMARY.md "Decision Received"): delay the re-advertisement until recipient gossip-topic readiness is observable, OR add a pre-restart CheckCertificateForSlot wait on second/third peers first (making a true surviving replica). Test-file change only; stays inside the directive's named shapes; must not relax any existing wait bound.
2. Focused evidence establishes it as an **attributed fix** — all-green focused RestartAtVote runs per the gate-entry clause (a) standard (the 12-19 precedent shows 1 pass / 1 fail is necessary-but-insufficient and must honestly STOP, not graduate to the gate).
3. Only then does this gate (crash baseline, three serial runs with per-run xunit copies, STOP-on-first-strike, sibling matrix, STATE resolution) re-attempt under the no-reroll rule carrying that fix.

## Honest State of the 12-14 Evidence-Gap Blocker

**STILL OPEN.** The three-consecutive-serial-pass evidence standard remains unmet and unattempted this round; the STATE.md blocker entry records the withheld series with full attribution (no attributed fix carried; clause (a)/(c) citations; budget unspent). VERIFICATION gaps 1 and 2 stay open; TEST-01..TEST-06 stay unmarked in REQUIREMENTS.md.

## Requirements Traceability

TEST-01..TEST-06 (listed in this plan's frontmatter) are **not marked complete**: the proving series was withheld, so none of the four fault-scenario cases carries new full-suite evidence from this plan. This is consistent with 12-19's stance (TEST-03/TEST-04 remain open inputs). Marking them on a withheld series would soften the outcome.

## Deviations from Plan

None — the stop is the plan's own Task-1 STOP branch, executed exactly as written (the plan's <done> criterion: "never a reroll, never a softened outcome"). Two faithful-handling notes for the record:

1. **Clause (b) not formally executed** (no rebuild + no formal mtime gate): the rebuild prepares a series clause (a) forbids; 12-17's STOP precedent ran zero builds. The read-only informational mtime status is recorded above so the clause's substance (build currency) is not silently dropped.
2. **Task 2 withheld with the series:** its sibling matrix and per-outcome finalization assume a running series; the 12-17 precedent withholds both at a gate-entry stop, and the STATE.md STOP record carries the finalization instead.

## Auth Gates

None.

## Must-Have Truth Status

| Truth | Status |
|-------|--------|
| Gate-entry rule re-evaluated on the record before any run, with a new attributed fix existing and focused-verified | **NOT MET — and that is the recorded finding**: the check ran exactly as written and falsified its own premise; clauses (a) and (c) FAIL with citations; this document and STATE.md (6c242ccb) are that record |
| Three consecutive serial full runs green with per-run loads, zero new crashes, four cases green per gate XML — OR honest stop at first strike | **NOT EVALUATED — withheld by the gate-entry rule** (honest state: the evidence does not exist; no run, no crash baseline, no xunit copy was produced; the budget is unspent) |
| Sibling matrix passes serially on the same build generation | **NOT EVALUATED** — Task 2 not executed (depends on the gate outcome it records) |
| STATE.md resolves the 12-14 evidence-gap blocker honestly | **MET (open-branch)** — recorded as still-open with the withheld series attributed by clause and citation; no softening |

## Known Stubs

None — no code was written; this is an evidence/decision artifact.

## Threat Flags

None. T-12-20-01 (no-reroll integrity) honored in its strongest form: zero gate runs exist, remaining runs cannot exist, no artifact synthesized. T-12-20-02 (environment) moot — no runs, so no load/port records needed; nothing hot was recorded. T-12-20-03 (crash-freedom) moot — no baseline exists because no run exists; absence documented, never faked. T-12-20-04 (evidence durability) honored — the STOP record lives in-repo (STATE.md, this SUMMARY); nothing in /tmp. T-12-20-05 (gate re-entry tampering) honored — the clause-by-clause check ran and was recorded before any run; the failed clauses forced the STOP exactly like 12-17. T-12-20-06 (per-run evidence substitution) moot — no xunit copies exist because no runs occurred.

## Self-Check: PASSED

- Created/modified files present on disk: 12-20-SUMMARY.md (this file), STATE.md (dated 12-20 STOP entry, decision, position, session), ROADMAP.md (12-20 row + progress). FOUND each.
- Task commit verified in git log: 6c242ccb ("docs(12-20): gate-entry check fires - round-4 series withheld, no attributed fix carries").
- Withheld-artifact proof: crash-baseline.txt, gate-{a,b,c}.log, gate-{a,b,c}.xml, sibling-matrix.log all verified absent under round4-traces/; zero /tmp/p12_20_* files exist; zero test runs and zero builds executed by this plan.
- 12-17's record untouched: `git status` on 12-17-SUMMARY.md and 12-17-PLAN.md shows no modification (this plan supersedes its gate re-attempt without editing it).
- STATE.md integrity: historical entries appended-only (the 12-20 STOP entry follows the 12-19 entry); Deferred Items untouched; metric row `| Phase 12 P20 | 8m | 2 tasks | 2 files |` recorded.
