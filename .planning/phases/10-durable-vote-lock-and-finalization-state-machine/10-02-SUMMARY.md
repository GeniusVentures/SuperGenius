---
phase: 10-durable-vote-lock-and-finalization-state-machine
plan: 02
subsystem: consensus-persistence
tags: [consensus, rocksdb, protobuf, vote-journal, crash-safety]

requires:
  - phase: 10-durable-vote-lock-and-finalization-state-machine
    provides: Focused same-path RocksDB vote-journal harness from Plan 10-01
provides:
  - Strict versioned node-local vote, process, conflict, and safety records
  - Exact signed-vote and outbound-envelope byte persistence across restart
  - Atomic retirement and conflict-evidence-plus-safety transitions
affects: [phase-10, consensus-startup, vote-publication, finalization, conflict-handling]

tech-stack:
  added: []
  patterns:
    - Direct RocksDB private consensus namespaces with no CRDT publication
    - Canonical protobuf decode plus key/value/identity relationship validation
    - Mutex-serialized exact-idempotent lifecycle mutations

key-files:
  created:
    - src/blockchain/impl/proto/ConsensusLocalState.proto
    - src/blockchain/ConsensusStateStore.hpp
    - src/blockchain/ConsensusStateStore.cpp
  modified:
    - src/blockchain/impl/CMakeLists.txt
    - test/src/blockchain/consensus_vote_journal_test.cpp

key-decisions:
  - "Validator-private consensus state uses only /consensus/local/v2 direct RocksDB keys, with the validator hash included in vote keys."
  - "Only exact RocksDB NOT_FOUND represents absence; all query, decode, canonicalization, identity, and relationship failures return typed errors."
  - "Conflict evidence and SafetyViolation are one batch, and safety authority must identify one member of the canonical digest/proposal pair."

patterns-established:
  - "Strict restore: protobuf version, enum, unknown fields, canonical bytes, key identity, nested vote/envelope, and cross-record relationships all fail closed."
  - "Durable lifecycle: retirement commits before a greater generation can occupy the vote key; publication metadata cannot alter signed bytes."

requirements-completed: [VOTE-01, VOTE-02, VOTE-03, VOTE-07]

duration: 9 min
completed: 2026-07-27
---

# Phase 10 Plan 02: Strict Consensus Local State Summary

**A direct-RocksDB consensus state store now preserves exact validator vote bytes and crash-safe lifecycle, processing, and safety records while failing closed on every malformed or contradictory durable input.**

## Performance

- **Duration:** 9 min
- **Started:** 2026-07-27T15:22:24Z
- **Completed:** 2026-07-27T15:31:27Z
- **Tasks:** 1
- **Files modified:** 5

## Accomplishments

- Added a private v2 protobuf schema for durable active/retired votes, certificate processing, canonical conflict evidence, and slot safety state.
- Implemented typed direct-RocksDB reads, strict scans, exact-idempotent writes, byte-preserving publication updates, durable retirement, and processing lifecycle transitions.
- Committed conflict evidence and SafetyViolation together in one RocksDB batch without storing certificate payloads.
- Expanded the focused journal suite to 13 deterministic tests covering reopen, namespace isolation, byte identity, conflicts, lifecycle transitions, corruption, unknown fields, and query failures.

## Task Commits

Each task was committed atomically:

1. **Task 1: Implement strict direct-RocksDB consensus local state** — `6ba21e45` (feat)

## Files Created/Modified

- `src/blockchain/impl/proto/ConsensusLocalState.proto` — defines explicit-version private durable record encodings.
- `src/blockchain/ConsensusStateStore.hpp` — exposes typed vote, process, conflict, and safety operations.
- `src/blockchain/ConsensusStateStore.cpp` — implements strict canonical validation and serialized RocksDB transitions.
- `src/blockchain/impl/CMakeLists.txt` — generates and links the local-state protobuf and store.
- `test/src/blockchain/consensus_vote_journal_test.cpp` — verifies exact bytes, reopen, direct namespaces, lifecycle atomicity, and fail-closed corruption behavior.

## Decisions Made

- Validator identity is bound to vote storage keys through a SHA-256 lowercase hash, while the full validator ID remains cross-checked in the value and signed vote.
- Exact active-vote idempotency compares all signature-critical bytes and validation context while allowing separately persisted publication metadata.
- Process and safety scans validate cross-record authoritative digest/proposal relationships rather than accepting individually well-formed contradictions.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The repository root has no top-level `CMakeLists.txt`, so an explicit `cmake -S .` probe failed; the existing configured build tree regenerated automatically during the required target build and completed successfully.

## Verification

- `cmake --build build/OSX/Release --target consensus_vote_journal_test -j2` passes.
- `build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1` passes all 13 tests.
- Raw direct namespace inspection finds local records while the CRDT logical query remains empty.
- Static audit finds all vote/process/conflict/safety v2 key builders and no `GlobalDB::Put` call in the store.
- `git diff --check` passes for every Plan 10-02 implementation file.

## User Setup Required

None.

## Next Phase Readiness

- Plan 10-03 can construct and strictly scan this store before ConsensusManager subscription or publication side effects.
- The raw stored envelope and publication metadata APIs are ready for exact restart replay integration.
- No blocker remains.

## Self-Check: PASSED

- The implementation commit exists and all five planned files are present.
- Every task acceptance criterion and the plan-level focused verification command pass.
- Protected pre-existing dirty paths remain unmodified and unstaged.

---
*Phase: 10-durable-vote-lock-and-finalization-state-machine*
*Completed: 2026-07-27*
