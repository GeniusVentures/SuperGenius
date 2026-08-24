---
phase: 11-convergent-certificate-consumption-mint-recovery
plan: "04"
subsystem: consensus
tags: [c++17, consensus, rocksdb, crdt, mint-v2, gtest]
requires:
  - phase: 09-durable-one-vote-finality
    provides: "durable exact active votes and deterministic contender selection"
  - phase: 10-authoritative-slot-certificate-publication
    provides: "canonical /cert/<slot> publication through convergent immutable CRDT writes"
provides:
  - "Three-store honest-validator proof that one same-slot Mint winner alone has a 2-of-3 quorum"
  - "Shared serialized SHA-256 ordering for local and remote occupied-slot certificate ingress"
  - "Critical diagnostic for verified, different-Mint same-slot quorum equivocation"
affects: [certificate-ingress, mint-finality, phase-12-fault-proof]
tech-stack:
  added: []
  patterns:
    - "Certificate ingress compares lowercase SHA-256 serialized bytes before CRDT apply."
    - "Different Mint hashes are only operational equivocation diagnostics, not recovery authority."
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
key-decisions:
  - "Reused CRDT's existing lower serialized SHA-256 winner rule in both Consensus ingress paths."
  - "Kept byte-distinct same-Mint certificates normal; only verified different Mint hashes trigger critical equivocation logging."
requirements-completed: [CERT-05, MINT-01]
duration: 17min
completed: 2026-08-24
---

# Phase 11 Plan 04: Convergent Certificate Ingress Summary

**Three isolated validators now prove one durable normal Mint vote each, while occupied certificate ingress converges by the same serialized SHA-256 ordering and reports real Mint equivocation without adding recovery authority.**

## Performance

- **Duration:** 17 min
- **Started:** 2026-08-24T17:37:31Z
- **Completed:** 2026-08-24T17:54:00Z
- **Tasks:** 2/2
- **Files modified:** 2

## Accomplishments

- Added a three-node GlobalDB/RocksDB regression with one signed active-validator registry, 2-of-3 quorum, normal Phase 9 vote publication, and restart replay of exact stored votes.
- Made `SubmitCertificate` and `FilterCertificate` use one lowercase SHA-256 serialized-value comparator, rejecting only a losing remote candidate before CRDT apply.
- Added an isolated overlapping-validator 2-of-3 Mint fault regression and critical log guarded by decoded Mint V2 transaction hashes; reordered same-Mint certificate votes remain normal.

## Task Commits

1. **Task 1: Prove honest multi-validator same-slot Mint contention yields one quorum certificate** — `debcdbe6` (`test`)
2. **Task 2 RED: Cover occupied certificate ordering** — `b3377cff` (`test`)
3. **Task 2: Align occupied-slot certificate ingress and surface Mint equivocation** — `a629c3f2` (`fix`)

## Files Created/Modified

- `src/blockchain/Consensus.cpp` — shares occupied-slot serialized hashing, validates existing certificate evidence, and logs verified differing-Mint equivocation.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — adds isolated three-store validator, occupied-slot ordering, same-Mint alternate, and explicit double-sign fault regressions.

## Decisions Made

- Kept generic CRDT merge, TransactionManager, journals, schemas, PubSub receiver behavior, and certificate authority unchanged.
- The equivocation log does not claim to roll back or recover an already-applied Mint.

## TDD Gate Compliance

- RED commit present: `b3377cff`; its occupied-slot filter regression failed before the implementation because `FilterCertificate` admitted the candidate.
- GREEN commit present after RED: `a629c3f2`; focused ordering and multi-validator tests pass.

## Verification

- `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test --parallel 4` — passed.
- `consensus_pending_lifecycle_test --gtest_filter='*MultiValidator*SameSlot*'` — passed (1/1).
- `consensus_pending_lifecycle_test --gtest_filter='*FilterCertificate*'` — passed (2/2), including both ordering directions for the isolated equivocation pair.
- `ctest --test-dir build/OSX/Release -R '^consensus_pending_lifecycle_test$' --output-on-failure` started the target but did not emit a completion result in this runner; direct full-suite execution likewise stalled during pre-existing shutdown behavior after starting 26 tests. Focused test binaries completed successfully.
- `git diff --check` — passed.

## Deviations from Plan

None - plan executed as specified. The test-owned multi-node factory required non-fatal GoogleTest diagnostics because fatal assertions cannot return from a value-returning factory; this was an implementation detail within the planned harness.

## Issues Encountered

- Full CTest/direct suite completion was not observable in this environment due to the existing lifecycle target shutdown stall; focused tests completed and passed.

## User Setup Required

None.

## Next Phase Readiness

- Phase 12 can use the normal honest-vote proof and the isolated equivocation diagnostic without treating a consensus fault as Mint rollback/recovery authority.

## Self-Check: PASSED

- `src/blockchain/Consensus.cpp` and `test/src/blockchain/consensus_pending_lifecycle_test.cpp` exist.
- Task commits `debcdbe6`, `b3377cff`, and `a629c3f2` exist in git history.

---
*Phase: 11-convergent-certificate-consumption-mint-recovery*
*Completed: 2026-08-24*
