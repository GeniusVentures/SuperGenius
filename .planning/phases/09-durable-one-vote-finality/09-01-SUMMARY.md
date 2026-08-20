---
phase: 09-durable-one-vote-finality
plan: 01
subsystem: consensus
tags: [c++17, protobuf, rocksdb, consensus, gtest]
requires:
  - phase: 08-canonical-slot-certificate-binding
    provides: canonical slot identity and certificate-binding ingress validation
provides:
  - private per-slot ActiveVoteRecord persistence before local vote publication
  - deterministic two-second candidate freeze with the existing generic comparator
  - validated exact-vote recovery and bounded replay without vote replacement
affects: [09-02-durable-certificate-release, 10-authoritative-slot-certificate-publication]
tech-stack:
  added: []
  patterns:
    - local RocksDB durability state remains outside CRDT and PubSub envelopes
    - persist-or-load accepts only byte-identical active-vote records
    - replay parses and verifies the original stored signed vote bytes
key-files:
  created: []
  modified:
    - src/blockchain/impl/proto/Consensus.proto
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
key-decisions:
  - Keep ActiveVoteRecord local-only and absent from ConsensusMessage.
  - Freeze approved candidates under the existing manager mutex before selecting a winner.
  - Retain active-vote records and the no-revote lock after the replay deadline expires.
patterns-established:
  - Direct datastore writes are synchronous local safety state; CRDT APIs are not used for active votes.
  - Stored vote bytes are parsed and signature-validated before any recovery announcement.
requirements-completed: [VOTE-01, VOTE-02, VOTE-03]
metrics:
  duration: 45m
  completed: 2026-08-20
  tasks_completed: 2
  files_modified: 4
---

# Phase 9 Plan 01: Durable One-Vote Finality Summary

The validator now freezes a two-second candidate set, durably stores one exact signed vote per canonical slot before publishing, and can replay only that validated vote after restart.

## Accomplishments

- Added local-only `ActiveVoteRecord` protobuf storage for canonical slot, complete proposal, exact signed vote, and absolute replay deadline.
- Replaced immediate self-voting with bounded candidate admission, frozen generic winner selection, local RocksDB persistence, and exact replay scheduling.
- Added deterministic lifecycle coverage using `MemorySecureStorage` for freeze ranking, write failure, collision/corruption rejection, exact retry/restart, and expiry lock retention.

## Task Commits

1. **Task 1: Establish local active-vote record contracts and deterministic lifecycle test seams** — `b135167e` (`test`)
2. **Task 2: Freeze generic candidates and persist/recover one exact vote before publication** — `bb39f2b3` (`test`), `67d46824` (`feat`)

## Files Created/Modified

- `src/blockchain/impl/proto/Consensus.proto` — private active-vote record envelope, excluded from network messages.
- `src/blockchain/Consensus.hpp` — private freeze, durability, recovery, and test-seam contracts.
- `src/blockchain/Consensus.cpp` — direct local persistence, fail-closed decode, timed replay, and slot arbitration.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — deterministic real-signature lifecycle regressions.

## Decisions Made

- A candidate is not eligible after its slot's fixed monotonic deadline even if due work has not yet run.
- An existing record must byte-match the entire newly proposed record; collisions do not overwrite or publish.
- Expiration halts retries but intentionally leaves both durable data and the in-memory no-revote lock for later certificate-finality handling.

## Verification

- `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test consensus_slot_key_test --parallel 4` — passed.
- `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` — passed, 2/2 tests in 13.00s.
- `git diff --check` — passed.
- `git diff --quiet HEAD -- src/account/MintTransactionV2.cpp` — passed; canonical Mint slot construction was unchanged.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 1 - Bug] Updated legacy immediate-vote assertions in the pending lifecycle fixture**
   - **Found during:** Task 2 focused test run.
   - **Issue:** The pre-existing test used a dummy, non-verifiable signer and expected an immediate local vote; this contradicts the new persist-and-verify-before-publication safety boundary.
   - **Fix:** Kept its pending-retry checks and asserted no usable vote until the new real-signature active-vote tests force the frozen deadline.
   - **Files modified:** `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
   - **Commit:** `bb39f2b3`

## Known Stubs

None.

## Next Phase Readiness

Plan 02 can add certificate-driven release only at its post-commit durable acceptance boundary. This plan deliberately does not delete active-vote records on certificate receipt, parsing, or volatile proposal cleanup.

## Self-Check: PASSED

- All four implementation/test artifacts exist.
- Task commits `b135167e`, `bb39f2b3`, and `67d46824` are present in history.
