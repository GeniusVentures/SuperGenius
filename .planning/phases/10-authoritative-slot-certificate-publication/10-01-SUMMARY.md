---
phase: 10-authoritative-slot-certificate-publication
plan: "01"
subsystem: database
tags: [crdt, globaldb, sha256, convergence, cxx17]
requires:
  - phase: 08-canonical-slot-certificate-binding
    provides: canonical slot-bound certificate authority records
provides:
  - replicated convergent immutable GlobalDB write primitive
  - SHA-256 lowercase-hex deterministic winner selection for immutable values
  - local and two-node CRDT convergence regressions
affects: [certificate-publication, certificate-authority, phase-10-plans]
tech-stack:
  added: []
  patterns:
    - reserved CRDT priority preserves special merge semantics through local and remote delta application
    - immutable value conflicts select the lexicographically lowest SHA-256 lowercase-hex encoding
key-files:
  created: []
  modified:
    - src/crdt/globaldb/globaldb.hpp
    - src/crdt/globaldb/globaldb.cpp
    - src/crdt/crdt_datastore.hpp
    - src/crdt/impl/crdt_datastore.cpp
    - src/crdt/crdt_set.hpp
    - src/crdt/impl/crdt_set.cpp
    - test/src/crdt/crdt_datastore_test.cpp
    - test/src/crdt/globaldb_integration.cpp
key-decisions:
  - "Use a reserved UINT64_MAX delta priority so ordinary CRDT writes cannot supersede immutable authority records."
  - "Order immutable conflicts by base::hex_lower(crypto::sha2_256(serialized bytes)), not insertion order or local pre-read state."
patterns-established:
  - "GlobalDB special write APIs delegate through CrdtDatastore so the rule is carried in replicated deltas."
requirements-completed: [CERT-04]
duration: 10min
completed: 2026-08-21
---

# Phase 10 Plan 01: Convergent Immutable CRDT Write Summary

**A narrow GlobalDB immutable write path now makes every replica retain the lowest SHA-256 lowercase-hex certificate encoding for a contested slot.**

## Performance

- **Duration:** 10 min
- **Started:** 2026-08-21T17:00:00Z
- **Completed:** 2026-08-21T17:10:03Z
- **Tasks:** 2
- **Files modified:** 9

## Accomplishments

- Added `GlobalDB::PutConvergentImmutable` and `CrdtDatastore::PutConvergentImmutableKey` as the only new path requesting immutable CRDT semantics.
- Preserved a reserved immutable priority in the replicated delta and selected the lowest SHA-256 lowercase-hex serialized value in `CrdtSet` on both local writes and remote merges.
- Added idempotent replay, arrival-order, ordinary-write protection, and disconnected two-replica convergence regressions.

## Task Commits

Each task was committed atomically:

1. **Task 1: Specify convergent immutable storage behavior with local and two-replica regressions** - `158bac53` (test)
2. **Task 2: Implement a narrow convergent immutable CRDT delta and hash-ordered merge rule** - `3ef42a86` (feat)

## Files Created/Modified

- `src/crdt/globaldb/globaldb.hpp` and `src/crdt/globaldb/globaldb.cpp` - expose and delegate the narrow immutable write API.
- `src/crdt/crdt_datastore.hpp` and `src/crdt/impl/crdt_datastore.cpp` - create immutable-priority deltas without changing ordinary `PutKey` behavior.
- `src/crdt/crdt_set.hpp` and `src/crdt/impl/crdt_set.cpp` - reserve immutable priority and converge collisions by SHA-256 lowercase-hex order.
- `test/src/crdt/crdt_datastore_test.cpp` - cover absent writes, identical replay, bidirectional local ordering, and ordinary write protection.
- `test/src/crdt/globaldb_integration.cpp` - cover concurrent disconnected replica writes with A→B and B→A synchronization.
- `src/crdt/CMakeLists.txt` - links the existing `hasher` target required by the new CRDT merge implementation.

## Decisions Made

- The immutable operation uses a replicated reserved priority rather than caller-side read-then-write, so each replica runs the same conflict rule.
- Conflicting byte sequences are compared as lowercase hexadecimal SHA-256 digests; identical bytes remain idempotent.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Linked the existing hasher target to the CRDT set library**
- **Found during:** Task 2
- **Issue:** The required `crypto::sha2_256` call introduced an unresolved static-library dependency for CRDT tests.
- **Fix:** Added the existing `hasher` target to `src/crdt/CMakeLists.txt`.
- **Files modified:** `src/crdt/CMakeLists.txt`
- **Verification:** Focused CRDT and GlobalDB targets build and tests pass.
- **Committed in:** `3ef42a86`

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Required existing build linkage only; no package installation or protocol-scope expansion.

## Issues Encountered

- The sandbox blocks local TCP listeners, so the two-node integration regression was rerun with local network permission and passed in both synchronization orders.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Certificate publication can call the explicit immutable write API without assuming a local pre-read is a distributed compare-and-set.
- The convergent storage behavior is covered at the datastore and production GlobalDB replication layers.

## Self-Check: PASSED

- All eight planned CRDT and test artifacts plus this summary exist.
- Task commits `158bac53` and `3ef42a86` exist in Git history.

---
*Phase: 10-authoritative-slot-certificate-publication*
*Completed: 2026-08-21*
