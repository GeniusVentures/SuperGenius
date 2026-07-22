---
gsd_state_version: 1.0
milestone: v2.0
milestone_name: Slot-Scoped Consensus Finality
current_phase: 11
status: ready_to_plan
last_updated: 2026-07-27T20:40:22.632Z
last_activity: 2026-07-27
progress:
  total_phases: 4
  completed_phases: 2
  total_plans: 23
  completed_plans: 23
  percent: 50
stopped_at: Phase 10 complete (7/7) — ready to discuss Phase 11
---

# State: SuperGenius — Slot-Scoped Consensus Finality

**Last updated:** 2026-07-27
**Milestone:** v2.0 — Slot-Scoped Consensus Finality
**Current Phase:** 11

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-07-27)

**Core value:** At most one valid certificate may finalize a canonical consensus slot.
**Current focus:** Phase 11 — slot owned bridge burn reservations

## Current Position

Phase: 11 (Slot-Owned Bridge Burn Reservations)
Plan: Not started
Status: Ready to plan
Last activity: 2026-07-27

## Roadmap Snapshot

| Phase | Name | Status | Requirements |
|-------|------|--------|--------------|
| 9 | Canonical Slot and Certificate Storage | ✓ complete | SLOT-01..04, CERT-01..04, COMP-01..02 |
| 10 | Durable Vote Lock and Finalization State Machine | ✓ complete | CERT-05..07, VOTE-01..07 |
| 11 | Slot-Owned Bridge Burn Reservations | ◆ ready to plan | BURN-01..05 |
| 12 | Consensus Race and Compatibility Verification | ○ blocked by 9-11 | TEST-01..06 |

## Key Decisions

- Certificates continue signing exact proposals but are stored authoritatively by canonical slot.
- Transaction-hash certificate lookup remains through a verified secondary index.
- v2.0 is a clean state break; no legacy certificate migration or dual-read support.
- Validators durably record one signature per slot before publishing it.
- Candidate selection may change only before the validator's irreversible slot vote.
- A valid certificate overrides local vote preference and finalizes the slot before proposal cleanup.
- Bridge reservations are owned by the canonical burn slot, not an individual proposal.
- Canonical readable slot preimages are validated before SHA-256 produces the only operational slot ID.
- Registered slot-handler failures are terminal; only unregistered subject types use hashed canonical subject identity.
- Receipt-local position is the mandatory bridge event index; block-wide `logIndex` remains observation metadata.
- Bridge source validation decodes only the indexed receipt log and compares all event facts before endpoint weight contributes.
