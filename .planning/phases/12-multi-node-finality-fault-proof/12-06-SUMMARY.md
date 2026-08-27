---
phase: 12-multi-node-finality-fault-proof
plan: "06"
subsystem: testing
tags: [finality, multi-node, real-socket, diagnosis, blocked]

requires:
  - phase: 12-05
    provides: isolated same-root active-vote restart proof
provides:
  - retained fresh-versus-prefix diagnosis for late, restart, and publisher-loss scenarios
  - explicit no-repair handoff for later scoped evidence gates
affects: [phase-12-verification, finality-fault-proof]

key-files:
  created:
    - .planning/phases/12-multi-node-finality-fault-proof/12-06-DIAGNOSIS.md
    - .planning/phases/12-multi-node-finality-fault-proof/12-07-HANDOFF.md
    - .planning/phases/12-multi-node-finality-fault-proof/12-06-SUMMARY.md
  modified: []

key-decisions:
  - "Plan 12-06 is terminally documented, not successful: its evidence prohibits a fixture or protocol repair."
  - "Later work must use a separately scoped root-cause gate for each non-prefix classification."

requirements-completed: []
requirements-blocked: [TEST-02, TEST-03, TEST-04, TEST-05]

completed: 2026-08-26
status: blocked-terminal
---

# Phase 12 Plan 06: Fresh-versus-Prefix Diagnosis Summary

**Plan 12-06 completed its diagnostic mandate and stopped without a repair. Its retained evidence rules out a general fixture workaround and leaves Phase 12 blocked pending separately scoped investigations.**

## Accomplishments

- Recorded the retained 18-run fresh-versus-prefix matrix in `12-06-DIAGNOSIS.md` without changing production, CMake, timeouts, assertions, or protocol behavior.
- Classified late contention as `inconclusive`, because its ordered-prefix failures occurred earlier in audit/contention rather than in the late scenario itself.
- Classified restart as `fresh-production` after one fresh run failed at the Mint-marker recovery boundary following valid listeners, RocksDB roots, and real peer topology.
- Classified publisher loss as `pre-topology-failure`: two fresh runs passed, while one failed at `ConnectAndWaitForPeers` before selected-publisher persistence or the fault barrier.
- Created the mandatory no-repair handoff consumed by Plans 12-07 and 12-08.

## Task Commits

1. **Tasks 1–2: Retain diagnosis and terminal no-repair handoff** — `7ca304e8` (docs)

## Verification

- Build of `multi_node_finality_fault_test` passed before the matrix runs.
- The complete matrix and terminal labels are retained in `12-06-DIAGNOSIS.md`.
- `git diff HEAD -- src` was empty for the blocked branch.
- `git diff --check` and structural diagnosis/handoff validation passed.

## Terminal Disposition

`PHASE_12_STATUS=BLOCKED`

No source repair is authorized by Plan 12-06. The restart boundary was later investigated by Plan 12-07, whose three fresh passive-diagnostic runs passed and also authorized no production repair. Publisher readiness is separately scoped by Plan 12-08; late-contender diagnostics remain separate.

## Deviations from Plan

The original blocked branch intentionally produced a diagnosis and handoff rather than a `SUMMARY.md`, so GSD's resume inventory continued to treat the plan as incomplete. This summary is a truthful closeout only: it introduces no new evidence or behavior and does not change the plan's blocked disposition.

---
*Phase: 12-multi-node-finality-fault-proof*
*Completed: 2026-08-26 — terminal blocked diagnosis*
