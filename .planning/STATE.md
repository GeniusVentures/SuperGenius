---
gsd_state_version: 1.0
milestone: v2.0
milestone_name: Slot-Scoped Consensus Finality
current_phase: 09
status: verifying
last_updated: "2026-07-24T15:51:52.777Z"
last_activity: 2026-07-24 -- Completed Plan 09-13 endpoint-local receipt-status disagreement handling
progress:
  total_phases: 4
  completed_phases: 1
  total_plans: 13
  completed_plans: 13
  percent: 25
---

# State: SuperGenius — Slot-Scoped Consensus Finality

**Last updated:** 2026-07-24
**Milestone:** v2.0 — Slot-Scoped Consensus Finality
**Current Phase:** 09

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-07-22)

**Core value:** At most one valid certificate may finalize a canonical consensus slot.
**Current focus:** Phase 09 — canonical-slot-and-certificate-storage

## Current Position

Phase: 09 (canonical-slot-and-certificate-storage) — VERIFYING
Plan: 13 of 13
Status: Phase complete — ready for verification
Last activity: 2026-07-24 -- Completed Plan 09-13 endpoint-local receipt-status disagreement handling

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

- Run the post-gap Phase 09 code review and independent phase verification.

## Performance Metrics

| Phase | Plan | Duration | Notes |
|-------|------|----------|-------|
| 09 | 01 | 16 min | 2 tasks, 9 files |
| 09 | 02 | 35 min | 3 tasks, 31 files |
| 09 | 03 | 36 min | 3 tasks, 10 files |
| Phase 09 P04 | 18 min | 2 tasks | 9 files |
| Phase 09 P05 | 14 min | 2 tasks | 6 files |
| Phase 09 P06 | 9 min | 1 tasks | 5 files |
| Phase 09 P07 | 21 min | 2 tasks | 3 files |
| Phase 09 P08 | 24 min | 2 tasks | 8 files |
| Phase 09 P09 | 2h 16m | 2 tasks | 7 files |
| Phase 09 P10 | 16 min | 2 tasks | 10 files |
| Phase 09 P11 | 15 min | 1 tasks | 3 files |
| Phase 09 P12 | 16 min | 2 tasks | 5 files |
| Phase 09 P13 | 6 min | 1 tasks | 3 files |

## Decisions

- [Phase 09]: Certificate storage uses one deterministic slot payload and winner-to-slot index in a single CRDT batch. — The slot is authoritative while the index preserves transaction-hash lookup without duplicate handlers.
- [Phase 09]: Certificate replication validates the complete slot/index delta before element filters or merge. — Partial, malformed, mismatched, and conflicting sibling records must fail atomically while unrelated namespaces remain unaffected.
- [Phase 09]: Protocol v2.0 startup rejects every non-exact key under /cert/ before consensus side effects. — Legacy transaction-keyed state cannot be safely mixed, migrated in place, or dual-read by the v2 certificate store.
- [Phase 09]: Authoritative slot lookup validates certificate payload and derived slot before compatibility hash lookup can return it. — The slot record is authoritative; index metadata cannot bypass complete certificate validation.
- [Phase 09]: Full subjects check canonical slot finality while string hashes remain exact winner lookups. — Losing candidates must observe finalized shared resources without receiving the winner certificate under their own hash.
- [Phase 09]: External mint identities must be canonical unsigned-decimal chains plus nonzero 64-character lowercase hexadecimal burn hashes before mutation. — Slot aliases and invalid sources must fail before UTXO or queue state changes.
- [Phase 09]: Only explicit supergenius and supergenius_chain identifiers bypass external receipt verification. — Empty or unknown chain metadata cannot classify a claim as local.
- [Phase 09]: RPC receipts contribute weight only when their transaction hash exactly matches the requested burn hash. — A valid-looking receipt for another transaction cannot prove the mint input.
- [Phase 09]: Catch-up publishes a chunk only after all enabled v1/v2 queries and receipt dependencies validate. — Partial publication would let a later failure expose burns while preserving a retry cursor.
- [Phase 09]: Catch-up receipt caches are scoped to one poll attempt. — A transient null receipt must be fetched again when the failed chunk retries.
- [Phase 09]: Receipt-local ordinals narrow through one uint64_t-to-uint32_t helper at the production boundary. — Direct UINT32_MAX coverage proves overflow handling without impractical receipt fixtures.
- [Phase 09]: Every serialized certificate vote is validity-critical; invalid or duplicate votes reject the certificate. — Silently skipping attacker-controlled votes permits alternate accepted encodings and weakens canonical quorum semantics.
- [Phase 09]: Certificate timestamp and weights are derived during normalization, and canonical votes sort by raw voter-ID bytes. — Derived redundant fields and total ordering produce one deterministic authoritative protobuf representation.
- [Phase 09]: The live delta filter owns the complete /cert/ namespace and rejects certificate tombstones directly. — Runtime legacy or malformed records must not survive to poison restart; shared tombstone matching and removal remains in Plan 09-08.
- [Phase 09]: External delta filters use Approve, Reject, and RetryDependency decisions across both elements and tombstones. — Terminal namespace attacks must be sanitized before merge while a missing dependency preserves the complete original delta for later validation.
- [Phase 09]: Missing canonical registry content parks the exact pending source under bounded deadline retry with due-time FIFO snapshot fairness. — Dependency roots already queued may run first, but later arrivals cannot starve the retry or force TTL cleanup without another evaluation.
- [Phase 09]: Only true database NOT_FOUND is certificate absence. — Corruption maps to IntegrityError and every other or unknown read failure maps conservatively to StorageError.
- [Phase 09]: Certificate index-to-slot relationship validation stays separate from raw read classification. — A dangling index is IntegrityError while operational slot failures propagate unchanged.
- [Phase 09]: Only previous-certificate NotFound creates an admission dependency. — Integrity and operational errors reject, and producer-witness validation rejects every non-success read.
- [Phase 09]: Certificate failure injection remains private and friend-only. — Production APIs remain unchanged while real consumers receive exact datastore outcomes in focused tests.
- [Phase 09]: Only exact datastore NOT_FOUND permits bridge burn evaluation to continue. — All other executed-record read errors fail before any UTXO, reservation, persistence, or queue mutation.
- [Phase 09]: Catch-up commits only Processed or durably AlreadyHandled chunks. — Retry and callback exceptions preserve the failed chunk cursor and uncommitted dedup tuples.
- [Phase 09]: Certificate ingress uses one typed two-key preflight. — Both exact slot and winner-index reads complete before classification; only exact NOT_FOUND is absence, while corruption and operational failures fail closed before mutation.
- [Phase 09]: Replicated preflight failures are terminal rejection. — Unknown durable occupancy cannot be approved or converted into a missing-dependency retry, regardless of whether the read failed from corruption or I/O.
- [Phase 09]: Reject sanitation preserves retry barriers for retained namespaces; distinct retained dependencies fail closed. — Reject authority is namespace-local and must not choose or erase another namespace dependency.
- [Phase 09]: CRDT RequestClose returns one shared completion barrier while external CancelAndCloseNow waits and joins. — Worker-originated close cannot self-wait, but external owners require a truthful completed-and-drained boundary.
- [Phase 09]: Missing or failed receipt status is endpoint-local and contributes zero weight; later exact endpoints may still reach quorum. — One stale or malicious provider cannot veto independent receipts, while unsuccessful endpoints never help satisfy the 75-weight threshold.
