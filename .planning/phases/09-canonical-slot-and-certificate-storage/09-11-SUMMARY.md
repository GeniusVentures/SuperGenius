---
phase: 09-canonical-slot-and-certificate-storage
plan: 11
subsystem: consensus
tags: [certificate-store, crdt, fail-closed, fault-injection]

requires:
  - phase: 09-09
    provides: Typed certificate read classification and private reader seam
provides:
  - Shared NOT_FOUND-only slot and winner-index preflight for local and replicated certificate ingress
  - Symmetric and asymmetric corruption and I/O fault matrices
affects: [phase-10-vote-locks, certificate-finality, crdt-replication]

tech-stack:
  added: []
  patterns:
    - Typed optional durable preflight where only exact NOT_FOUND becomes absence
    - Friend-only failure and publish-observer seams

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_certificate_store_test.cpp

key-decisions:
  - "Both slot and winner-index reads complete before either ingress path classifies durable state."
  - "Any integrity failure dominates operational failure for local typed error reporting; remote ingress rejects both classes."

patterns-established:
  - "Certificate write preflight: present, absent, integrity error, or storage error through one shared reader."

requirements-completed: [CERT-02, CERT-03]

duration: 15 min
completed: 2026-07-24
---

# Phase 09 Plan 11: Fail-Closed Certificate Preflight Summary

**Local submission and replicated certificate ingestion now share one typed preflight that treats only exact `NOT_FOUND` as absence and rejects every other read failure before mutation**

## Performance

- **Duration:** 15 min
- **Started:** 2026-07-24T15:06:54Z
- **Completed:** 2026-07-24T15:21:38Z
- **Tasks:** 1
- **Files modified:** 3

## Accomplishments

- Added a shared typed certificate-record preflight helper that converts exact datastore `NOT_FOUND` into absence while preserving integrity and operational errors.
- Made `SubmitCertificate` perform both slot and winner-index reads and return a typed failure before persistence, callbacks, or publication if either read fails.
- Made `FilterCertificateDelta` use the same two-key preflight and terminally reject corruption and I/O failures instead of approving or requesting a dependency retry.
- Added complete seven-row local and remote fault matrices, including symmetric and asymmetric failures and CRDT delivery checks that prove rejected records remain invisible.

## Task Commits

Each task was committed atomically:

1. **Task 1: Unify certificate preflight and add local/remote failure matrices** - `6ccdf3a5` (fix)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Declares the typed preflight helper and private publish-attempt observer seam.
- `src/blockchain/Consensus.cpp` - Applies shared fail-closed preflight semantics to local and replicated certificate ingress.
- `test/src/blockchain/consensus_certificate_store_test.cpp` - Covers all seven read-result combinations for both ingress paths and verifies no failed-path side effects.

## Decisions Made

- Both exact durable keys are read once before classifying the preflight result, so local and replicated ingress have symmetric behavior even for asymmetric failures.
- If simultaneous failures include corruption, local submission reports `IntegrityError`; otherwise it reports `StorageError`.
- Replicated corruption and operational failures are terminal rejection because durable occupancy is unknown and must not be treated as a missing dependency.

## Verification

- Built `consensus_certificate_store_test` and `certificate_compatibility_test`.
- The exact two-test list guard found 2 tests, and both fail-closed fault-matrix tests passed.
- Full `consensus_certificate_store_test` passed.
- Full `certificate_compatibility_test` passed all 17 tests.
- Source audit confirmed both ingress paths call `ReadCertificatePreflightRecord` and do not call `db_->Get` directly for preflight.
- `git diff --check` passed.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The local gossip service does not loop publications back to its own subscriber when no peers are connected. The publication assertion therefore uses a private friend-only observer immediately before the real `Publish` call, keeping the test deterministic without changing production behavior.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Certificate ingress now fails closed consistently and is ready for the remaining Phase 09 gap plans.
- Plan 09-12 can proceed with the shared preflight invariant protected by the new fault matrices.

## Self-Check: PASSED

- Summary and all three modified source files exist.
- Task commit `6ccdf3a5` is present in git history.
- Required focused and full regression suites passed.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-24*
