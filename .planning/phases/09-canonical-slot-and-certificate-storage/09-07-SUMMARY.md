---
phase: 09-canonical-slot-and-certificate-storage
plan: 07
subsystem: consensus-certificates
tags: [canonical-protobuf, strict-votes, crdt-filter, namespace-guard]

requires:
  - phase: 09-03
    provides: Canonical slot-keyed certificate pair storage and replay semantics
  - phase: 09-04
    provides: Fail-closed canonical lookup and compatibility checks
provides:
  - One strict certificate normalizer shared by creation, submission, replication, callbacks, and lookup
  - Deterministic canonical certificate bytes with recursive unknown-field rejection and derived redundant fields
  - Complete runtime `/cert/` namespace rejection for legacy, malformed, extra, duplicate, and tombstoned records
affects: [09-08-crdt-tombstone-filtering, 09-09-dependency-journal, 10-finalization-state-machine]

tech-stack:
  added: []
  patterns:
    - Parse, recursively reject unknowns, validate every vote, derive fields, sort bytewise, then deterministic-serialize
    - Remote authoritative bytes must already equal normalized deterministic bytes; remote normalization never rewrites input
    - Namespace delta guards validate complete atomic record families while callbacks remain authoritative-record-only

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_certificate_store_test.cpp

key-decisions:
  - "Every serialized certificate vote is validity-critical: duplicates, mismatches, unknown/inactive voters, signing failures, and bad signatures reject the entire certificate."
  - "Canonical certificate timestamp is the maximum included vote timestamp; total and approved weights are registry-derived rather than caller-trusted."
  - "The live delta filter owns the complete `/cert/` namespace and rejects any certificate tombstone directly; shared CRDT tombstone matching/removal remains serialized in Plan 09-08."

patterns-established:
  - "Canonical certificate boundary: validate all semantics -> normalize full vote set -> deterministic serialize -> exact byte compare."
  - "Certificate namespace boundary: exactly one canonical slot element plus one canonical transaction index element, with no certificate tombstones."

requirements-completed:
  - CERT-01
  - CERT-02
  - CERT-03
  - COMP-02

duration: 21 min
completed: 2026-07-23
---

# Phase 09 Plan 07: Canonical Certificate and Runtime Namespace Guard Summary

**Authoritative certificates now have one deterministic wire representation, and the live CRDT boundary rejects every noncanonical or non-v2 `/cert/` mutation before merge.**

## Performance

- **Duration:** 21 min
- **Started:** 2026-07-23T20:56:32Z
- **Completed:** 2026-07-23T21:17:23Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments

- Added a shared strict normalization seam across certificate creation, submission, replication filtering, element filtering, receive callbacks, validation, and authoritative lookup.
- Recursively rejects protobuf unknown fields and rejects every invalid serialized vote instead of silently omitting it from quorum evaluation.
- Canonicalizes the full unique valid vote set by raw voter-ID bytes, derives weights and maximum vote timestamp, and requires exact deterministic bytes on remote and stored authoritative records.
- Removed certificate wall-clock timestamp fallback and made local submission persist normalized deterministic bytes.
- Registered the delta filter for the complete `/cert/` namespace while leaving new-element callbacks restricted to exact authoritative slot records.
- Added adversarial regressions for unknown fields, redundant-field changes, vote order, duplicates, wrong proposals, inactive/unknown voters, bad signatures, appended invalid votes, legacy/malformed runtime keys, restart safety, and certificate tombstones.

## Task Commits

Each task was committed atomically:

1. **Task 1: Normalize and byte-validate every authoritative certificate**
   - `be33b60b` — canonical normalization, strict vote validation, exact byte checks, and adversarial certificate tests
2. **Task 2: Reject every non-v2 runtime key in the complete certificate namespace**
   - `68a9db2a` — full namespace routing, tombstone rejection, runtime legacy/malformed tests, and restart regression

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Declares normalization results and the complete certificate namespace route.
- `src/blockchain/Consensus.cpp` - Implements recursive unknown rejection, strict normalization, deterministic byte enforcement, strict tallying, and full delta namespace validation.
- `test/src/blockchain/consensus_certificate_store_test.cpp` - Adds two-validator canonical controls and adversarial wire, vote, namespace, tombstone, and restart coverage.

## Decisions Made

- Missing registry state is the only normalizer outcome that stalls; every malformed or semantically invalid certificate rejects terminally.
- Valid non-approving votes remain in the canonical vote set but add no approved weight.
- Local callers may provide noncanonical vote order or redundant fields because submission normalizes them; replicated records must already contain the exact canonical bytes.
- Certificate tombstones are rejected by the certificate callback now, while Plan 09-08 owns making tombstone-only deltas trigger namespace filters and removing matched tombstones before CRDT merge.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The inactive-validator regression initially used a nonexistent `INACTIVE` enum value; the registry's actual non-active state is `SUSPENDED`, which preserves the intended production-path assertion.

## Known Stubs

None introduced.

## User Setup Required

None - no external service configuration required.

## Verification

- Prescribed target build — PASS: `consensus_certificate_store_test`.
- Task 1 focused adversarial filter — PASS: 5/5 prescribed tests; expanded strict-tally filter with inactive-voter coverage passed 6/6.
- Task 2 runtime namespace filter — PASS: 3/3.
- Full `consensus_certificate_store_test` after both task commits — PASS: 19/19.
- Adjacent `consensus_pending_lifecycle_test` — PASS: 7/7.
- Adjacent `certificate_compatibility_test` — PASS: 11/11.
- Adjacent `consensus_vote_slot_test` — PASS: 3/3.
- `git diff --check` — PASS before both task commits.

## Next Phase Readiness

- Plan 09-08 can extend CRDT delta-filter matching/removal to tombstones without changing certificate acceptance semantics.
- Plan 09-09 can use the normalizer's dependency-only `Check::Stalled` result for journal replay behavior.
- No blockers.

## Self-Check: PASSED

- All three modified plan files exist.
- Task commits `be33b60b` and `68a9db2a` are present.
- Required normalization, complete namespace routing, and tombstone rejection symbols are present in production code.
- All prescribed, full focused, and adjacent verification passes after implementation.
- Protected user-owned dirty and untracked paths remain unstaged and untouched.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-23*
