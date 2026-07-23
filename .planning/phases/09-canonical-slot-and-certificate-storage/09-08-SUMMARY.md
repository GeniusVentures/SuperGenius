---
phase: 09-canonical-slot-and-certificate-storage
plan: 08
subsystem: crdt-consensus-certificates
tags: [crdt-filter, dependency-retry, tombstones, bounded-fairness]

requires:
  - phase: 09-07
    provides: Strict canonical certificate normalization and complete runtime certificate namespace guard
provides:
  - Tri-state CRDT delta filtering that distinguishes approval, terminal rejection, and retryable dependency absence
  - Element-and-tombstone namespace filtering before merge with unrelated-field preservation
  - Bounded, deadline-driven external-root dependency retry with snapshot fairness and complete cleanup
  - Missing-registry certificate retry while malformed, conflicting, and tombstoned pairs reject terminally
affects: [09-09-dependency-journal, 10-finalization-state-machine, crdt-replication]

tech-stack:
  added: []
  patterns:
    - Reject outranks RetryDependency, which outranks Approve, across matching namespace filters
    - Park the exact unprocessed external root while releasing active scheduler ownership
    - Capture the ordinary-root FIFO count once when a retry becomes due; post-snapshot arrivals wait
    - Validate registry-independent certificate structure and identity before classifying missing registry state as retryable

key-files:
  created: []
  modified:
    - src/crdt/crdt_data_filter.hpp
    - src/crdt/impl/crdt_data_filter.cpp
    - src/crdt/crdt_datastore.hpp
    - src/crdt/impl/crdt_datastore.cpp
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/crdt/crdt_datastore_test.cpp
    - test/src/blockchain/consensus_certificate_store_test.cpp

key-decisions:
  - "A retryable external root remains pending and cache-retained but is not the active root, allowing its dependency to run."
  - "Due retry fairness includes only the ordinary roots already queued at the due transition; later arrivals cannot extend the gate."
  - "Only absence of the exact canonical registry CID is retryable; certificate shape, signature, identity, pair, conflict, and tombstone failures are terminal."
  - "Certificate namespace rejection strips matching elements and tombstones before merge while preserving unrelated tombstones exactly."

patterns-established:
  - "External delta decision: inspect complete delta -> combine decisions by severity -> sanitize only on Reject -> run element filters only on Approve."
  - "Dependency retry lifecycle: park pending source -> wait until monotonic deadline -> bounded FIFO snapshot -> re-evaluate -> process once or clean completely."

requirements-completed:
  - CERT-02
  - CERT-03

duration: 24 min
completed: 2026-07-23
---

# Phase 09 Plan 08: Canonical Certificate Dependency Retry Summary

**Canonical certificate pairs now survive registry/certificate propagation reordering without becoming visible early, while certificate tombstones and invalid pairs remain terminally filtered before CRDT merge.**

## Performance

- **Duration:** 24 min
- **Started:** 2026-07-23T21:29:34Z
- **Completed:** 2026-07-23T21:53:53Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments

- Replaced boolean delta-filter decisions with `Approve`, `Reject`, and `RetryDependency`, including deterministic multi-filter precedence and dependency propagation.
- Made namespace delta filters match both elements and tombstones, sanitize every matching repeated-field entry on rejection, and preserve unrelated elements and exact-ID tombstones for normal CRDT merge.
- Added a retained-root scheduler that leaves stalled work pending and cached, releases external-root ownership, waits on exact monotonic deadlines, and retries on the 1/2/4/8/16/30-second schedule.
- Bounded parked work at 256 roots globally, 32 roots per dependency, ten minutes, and eight stalled evaluations, with deduplication, named counters, terminal cleanup, and shutdown draining.
- Added due-time FIFO snapshot fairness so only roots already queued at the due transition can precede the retry; later arrivals cannot starve it or cause unevaluated TTL eviction.
- Changed certificate filtering so registry-independent invalidity and occupied-pair conflicts reject before registry access, while only an absent parseable canonical registry CID returns `RetryDependency`.
- Proved exact-ID slot and index tombstones are stripped before merge, unrelated tombstones retain normal behavior, and authoritative lookup remains restart-safe.

## Task Commits

Each task was committed atomically:

1. **Task 1: Add terminal-versus-dependency decisions to CRDT delta filtering**
   - `a2fd1973` — tri-state filtering, bounded retained-root retry scheduling, cleanup, observability, and CRDT regressions
2. **Task 2: Route stalled canonical certificate pairs through source-CID retry**
   - `3a384109` — missing-registry retry classification, pre-registry terminal checks, and certificate tombstone/restart regressions

## Files Created/Modified

- `src/crdt/crdt_data_filter.hpp` - Declares tri-state filter decisions and complete-delta results.
- `src/crdt/impl/crdt_data_filter.cpp` - Matches both delta fields, combines decisions, and strips rejected namespaces.
- `src/crdt/crdt_datastore.hpp` - Declares retry lifecycle records, limits, statistics, and deterministic test seams.
- `src/crdt/impl/crdt_datastore.cpp` - Implements retained-root parking, deadline waits, snapshot fairness, bounded retries, cleanup, and shutdown drain.
- `src/blockchain/Consensus.hpp` - Changes the certificate namespace filter to return a tri-state result.
- `src/blockchain/Consensus.cpp` - Separates terminal certificate invalidity from missing-registry dependency stalls.
- `test/src/crdt/crdt_datastore_test.cpp` - Covers rejection/tombstone sanitation, retained retry, exactly-once merge, backoff/attempt eviction, and shutdown.
- `test/src/blockchain/consensus_certificate_store_test.cpp` - Covers retry classification controls and exact-ID tombstone stripping with restart-safe lookup.

## Decisions Made

- Stalled roots remain in the DAGSyncer cache as pending, unprocessed sources; cache absence is not used as evidence of a safe stall.
- A due retry captures ordinary queue precedence immediately even when another ordinary root is active, and later arrivals never enlarge that snapshot.
- TTL becomes expiry-pending when a due evaluation is scheduler-delayed; cleanup happens only if the guaranteed evaluation stalls again.
- Registry-independent key derivation and occupied-pair conflict checks run before registry loading so malformed or conflicting pairs cannot be mislabeled retryable.

## Deviations from Plan

- The shared `crdt_data_filter` target carries the dependency CID as its canonical string form to avoid introducing the datastore/CID dependency into that lower-level target. `ConsensusManager` validates the string as a CID and `CrdtDatastore` converts it back to the typed CID before parking.
- The receiver behavior is verified across two complementary boundaries rather than one heavyweight two-`GlobalDB` fixture: the CRDT external-root test proves pending/cache-retained retry and exactly-once merge after dependency readiness, while the certificate test proves that only exact missing-registry state emits that dependency decision. The production path composes those same seams.

## Issues Encountered

- The original test wording expected a stalled source to be absent from the DAGSyncer cache, which conflicts with retaining the exact source for retry. Verification instead asserts cache presence, pending job state, absent merged state, and unresolved processing.

## Known Stubs

None introduced.

## User Setup Required

None - no external service configuration required.

## Verification

- Prescribed target build — PASS: `consensus_certificate_store_test` and `crdt_test`.
- Focused CRDT dependency/rejection filters — PASS: 4/4.
- Focused certificate registry/tombstone filters — PASS: 3/3.
- Full `consensus_certificate_store_test` — PASS: 21/21, including all tombstone cases.
- Full `crdt_test` — PASS: 24/24.
- `git diff --check` — PASS.

## Next Phase Readiness

- Plan 09-09 can layer durable dependency journaling on the explicit retry result without changing certificate validation semantics.
- The bounded scheduler and shutdown lifecycle are ready for reuse by other dependency-aware CRDT namespaces.
- No blockers.

## Self-Check: PASSED

- All eight modified plan files exist.
- Task commits `a2fd1973` and `3a384109` are present.
- Tri-state filtering, tombstone sanitation, missing-registry retry, bounded backoff, snapshot fairness, eviction counters, and shutdown cleanup are present in production code.
- Both complete target suites pass after implementation, including the full certificate tombstone suite.
- Protected user-owned dirty and untracked paths remain unstaged and untouched.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-23*
