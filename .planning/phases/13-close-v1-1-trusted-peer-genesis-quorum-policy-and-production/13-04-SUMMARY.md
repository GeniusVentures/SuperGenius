---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: "04"
subsystem: secure-crdt-governance-transport
tags: [securecrdt, candidate, multisig, authorization, concurrency, cpp]
requires:
  - phase: 13-02
    provides: crash-safe verified trust snapshot and current authority
  - phase: 13-03
    provides: instance-scoped SecureCrdt registry resolution
  - phase: 13-12
    provides: canonical trust policy encoding
provides:
  - bounded content-addressed candidate and approval codecs
  - shared local and remote candidate authorization gate
  - concurrent candidate coexistence, deduplication, and stale audit isolation
affects: [13-05, 13-06, 13-07, 13-09, TrustedPeerRegistry, BurnConfig]
tech-stack:
  added: []
  patterns:
    - self-contained signed approval records
    - shared ingress validator for local and remote writes
    - bounded predecessor-scoped candidate index
key-files:
  created:
    - src/securecrdt/SecureCrdtCandidate.hpp
    - src/securecrdt/SecureCrdtCandidate.cpp
    - test/src/securecrdt/securecrdt_candidate_test.cpp
    - test/src/securecrdt/securecrdt_candidate_race_test.cpp
  modified:
    - src/securecrdt/SecureCrdt.hpp
    - src/securecrdt/SecureCrdt.cpp
    - src/securecrdt/SecureCrdtRegistry.hpp
    - src/securecrdt/CMakeLists.txt
    - test/src/securecrdt/CMakeLists.txt
key-decisions:
  - "Candidate approval records repeat the exact canonical core bytes and bind version, content hash, and signer in the storage key."
  - "A CandidateAuthorizationSource snapshot supplies the same live context to local submission and remote filtering."
  - "Current-only listing gates activation by the live predecessor while the bounded audit view retains stale records."
requirements-completed: [SCRDT-04, TPR-02, BURN-01, POLICY-01, VALID-01, TEST-01]
duration: 22m
completed: 2026-08-12
---

# Phase 13 Plan 04: Authenticated Candidate Transport Summary

Bounded canonical candidate approvals now share one live authorization path for local and remote SecureCrdt ingress, with deterministic race-safe coexistence and stale-context audit isolation.

## Performance

- **Duration:** 22 minutes
- **Started:** 2026-08-12T14:53:21Z
- **Completed:** 2026-08-12T15:15:30Z
- **Tasks:** 3
- **Files modified:** 9

## Accomplishments

- Added strict canonical codecs for candidate cores, identifiers, approval records, and storage keys with exact size, count, and active-byte limits.
- Routed both local submissions and remote CRDT writes through one authorization gate that checks live domain context, signer membership, exact-core signatures, deduplication, and resource caps.
- Added deterministic concurrency coverage proving two candidates can coexist, duplicate approvals are rejected, signer sets stay isolated, and predecessor changes separate the active view from bounded audit history.

## Task Commits

Each task was committed atomically with repository hooks enabled:

1. **Task 1 RED: Candidate codec contracts** - `ff7456f7` (test)
2. **Task 1 GREEN: Bounded candidate codecs** - `74f6fe14` (feat)
3. **Task 2 RED: Candidate authorization contracts** - `1535efa9` (test)
4. **Task 2 GREEN: Shared authorization gates** - `746eae4b` (feat)
5. **Task 3: Candidate race isolation proof** - `149e0b6b` (test)

## Files Created/Modified

- `src/securecrdt/SecureCrdtCandidate.hpp` - Candidate limits, identifiers, canonical records, keys, and codec contracts.
- `src/securecrdt/SecureCrdtCandidate.cpp` - Strict encoding, decoding, hashing, parsing, and key-binding validation.
- `src/securecrdt/SecureCrdt.hpp` - Candidate submission, query, callback, and authorization interfaces.
- `src/securecrdt/SecureCrdt.cpp` - Shared live authorization gate and bounded candidate persistence/query behavior.
- `src/securecrdt/SecureCrdtRegistry.hpp` - Instance-scoped candidate authorization source registration.
- `src/securecrdt/CMakeLists.txt` - Candidate codec compilation.
- `test/src/securecrdt/securecrdt_candidate_test.cpp` - Codec, bound, local/remote authorization, callback, and resource-limit coverage.
- `test/src/securecrdt/securecrdt_candidate_race_test.cpp` - Barrier-driven coexistence, deduplication, signer isolation, and stale-context coverage.
- `test/src/securecrdt/CMakeLists.txt` - Candidate test targets.

## Decisions Made

- Approval records carry the exact candidate core bytes instead of reconstructing signed content from mutable state; the key independently binds encoding version, core hash, and signer identity.
- `CandidateAuthorizationSource` exposes a current immutable authorization snapshot without coupling `SecureCrdt` to the concrete trust-state store.
- The default candidate listing returns only entries matching the current predecessor, while an explicit audit listing retains valid bounded records from earlier predecessors.

## TDD Gate Compliance

- Task 1 followed RED/GREEN: seven codec, key, and bound contracts failed before the implementation and passed afterward.
- Task 2 followed RED/GREEN: local/remote authorization, callback, and resource-limit contracts failed against the stubs and passed after the shared gate was implemented.
- Task 3 was a test-only proof of behavior established by Task 2. Its first configured run passed, so no artificial production change was introduced solely to manufacture a RED state.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- CMake required reconfiguration before newly declared test targets became available.
- Candidate network tests open local listeners, which the workspace sandbox denies; the same Release verification commands passed with approved local-listener access.

## Verification

- `securecrdt_candidate_test --gtest_filter='*Codec*:*Key*:*Bounds*'`: 7/7 passed.
- `securecrdt_candidate_test --gtest_filter='*Authorization*:*LocalRemote*:*Callback*:*Limit*'`: 3/3 passed.
- `ctest --test-dir build/OSX/Release --output-on-failure -R 'securecrdt_candidate|securecrdt_registry|securecrdt_quorum'`: 5/5 passed.
- Modified-file stub scan found no TODO, FIXME, placeholder, or hardcoded empty UI-flow values.
- No new RPC, pubsub topic, consensus proposal, vote, or certificate surface was introduced.

## Known Stubs

None.

## Self-Check: PASSED

- All four created files exist.
- All five task commits are present in repository history.
- Overall focused Release verification passed.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-12*
