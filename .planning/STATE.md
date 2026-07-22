---
gsd_state_version: 1.0
milestone: v2.0
milestone_name: Slot-Scoped Consensus Finality
current_phase: 09
status: planning
last_updated: "2026-07-22T21:34:36.258Z"
last_activity: 2026-07-22 — Phase 9 context gathered
progress:
  total_phases: 4
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# State: SuperGenius — Slot-Scoped Consensus Finality

**Last updated:** 2026-07-22
**Milestone:** v2.0 — Slot-Scoped Consensus Finality
**Current Phase:** 09

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-07-22)

**Core value:** At most one valid certificate may finalize a canonical consensus slot.
**Current focus:** Phase 9 — Canonical Slot and Certificate Storage

## Current Position

Phase: 09 (Canonical Slot and Certificate Storage) — not started
Plan: —
Status: Phase 9 context gathered — ready to plan
Last activity: 2026-07-22 — Phase 9 context gathered

## Roadmap Snapshot

| Phase | Name | Status | Requirements |
|-------|------|--------|--------------|
| 9 | Canonical Slot and Certificate Storage | ○ pending | SLOT-01..04, CERT-01..04, COMP-01..02 |
| 10 | Durable Vote Lock and Finalization State Machine | ○ blocked by 9 | CERT-05..07, VOTE-01..07 |
| 11 | Slot-Owned Bridge Burn Reservations | ○ blocked by 10 | BURN-01..05 |
| 12 | Consensus Race and Compatibility Verification | ○ blocked by 9-11 | TEST-01..06 |

## Key Decisions

- Certificates continue signing exact proposals but are stored authoritatively by canonical slot.
- Transaction-hash certificate lookup remains through a verified secondary index.
- v2.0 is a clean state break; no legacy certificate migration or dual-read support.
- Validators durably record one signature per slot before publishing it.
- Candidate selection may change only before the validator's irreversible slot vote.
- A valid certificate overrides local vote preference and finalizes the slot before proposal cleanup.
- Bridge reservations are owned by the canonical burn slot, not an individual proposal.

## Notes

- The milestone is based on `bridge_race_single_burn_test` evidence in `src/account/log_bridge_race.txt`.
- Existing planning artifacts were archived under `.planning/milestones/pre-v2.0-phases/`.
- External research was skipped; requirements derive from the repository-level investigation.
- Broader Phase 8 fault injection and fuzzing remain deferred future work.

## Operator Next Steps

- Run `$gsd-discuss-phase 9` after approval.
