---
gsd_state_version: 1.0
milestone: v2.0
milestone_name: Slot-Scoped Consensus Finality
current_phase: 09
status: executing
last_updated: "2026-07-23T20:19:14.550Z"
last_activity: 2026-07-23 -- Phase 09 planning complete
progress:
  total_phases: 4
  completed_phases: 0
  total_plans: 9
  completed_plans: 4
  percent: 0
---

# State: SuperGenius — Slot-Scoped Consensus Finality

**Last updated:** 2026-07-23
**Milestone:** v2.0 — Slot-Scoped Consensus Finality
**Current Phase:** 09

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-07-22)

**Core value:** At most one valid certificate may finalize a canonical consensus slot.
**Current focus:** Phase 9 — Canonical Slot and Certificate Storage

## Current Position

Phase: 09 (Canonical Slot and Certificate Storage) — EXECUTING
Plan: 4 of 9
Status: Ready to execute
Last activity: 2026-07-23 -- Phase 09 planning complete

## Roadmap Snapshot

| Phase | Name | Status | Requirements |
|-------|------|--------|--------------|
| 9 | Canonical Slot and Certificate Storage | ◆ in progress | SLOT-01..04, CERT-01..04, COMP-01..02 |
| 10 | Durable Vote Lock and Finalization State Machine | ○ pending | CERT-05..07, VOTE-01..07 |
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
- Canonical readable slot preimages are validated before SHA-256 produces the only operational slot ID.
- Registered slot-handler failures are terminal; only unregistered subject types use hashed canonical subject identity.
- Receipt-local position is the mandatory bridge event index; block-wide `logIndex` remains observation metadata.
- Bridge source validation decodes only the indexed receipt log and compares all event facts before endpoint weight contributes.

## Notes

- The milestone is based on `bridge_race_single_burn_test` evidence in `src/account/log_bridge_race.txt`.
- Existing planning artifacts were archived under `.planning/milestones/pre-v2.0-phases/`.
- External research was skipped; requirements derive from the repository-level investigation.
- Broader Phase 8 fault injection and fuzzing remain deferred future work.

## Operator Next Steps

- Verify Phase 09, then plan Phase 10.

## Performance Metrics

| Phase | Plan | Duration | Notes |
|-------|------|----------|-------|
| 09 | 01 | 16 min | 2 tasks, 9 files |
| 09 | 02 | 35 min | 3 tasks, 31 files |
| 09 | 03 | 36 min | 3 tasks, 10 files |
| Phase 09 P04 | 18 min | 2 tasks | 9 files |

## Decisions

- [Phase 09]: Certificate storage uses one deterministic slot payload and winner-to-slot index in a single CRDT batch. — The slot is authoritative while the index preserves transaction-hash lookup without duplicate handlers.
- [Phase 09]: Certificate replication validates the complete slot/index delta before element filters or merge. — Partial, malformed, mismatched, and conflicting sibling records must fail atomically while unrelated namespaces remain unaffected.
- [Phase 09]: Protocol v2.0 startup rejects every non-exact key under /cert/ before consensus side effects. — Legacy transaction-keyed state cannot be safely mixed, migrated in place, or dual-read by the v2 certificate store.
- [Phase 09]: Authoritative slot lookup validates certificate payload and derived slot before compatibility hash lookup can return it. — The slot record is authoritative; index metadata cannot bypass complete certificate validation.
- [Phase 09]: Full subjects check canonical slot finality while string hashes remain exact winner lookups. — Losing candidates must observe finalized shared resources without receiving the winner certificate under their own hash.
