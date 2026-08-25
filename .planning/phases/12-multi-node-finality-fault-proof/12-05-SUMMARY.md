---
phase: 12-multi-node-finality-fault-proof
plan: "05"
subsystem: testing
tags: [consensus, rocksdb, crdt, pubsub, restart, fault-injection]

requires:
  - phase: 12-04
    provides: real-socket restart fixture with durable-state assertions
provides:
  - lifecycle snapshots proving direct active-vote RocksDB survival across same-root peer recreation
  - an isolated pre-broadcast TEST-04 vote boundary that cannot be finalized by remote validators before restart
affects: [phase-12-verification, consensus-regression-tests, finality-fault-proof]

tech-stack:
  added: []
  patterns: [stage-labelled read-only durable-store diagnosis, isolated pre-broadcast restart boundary]

key-files:
  created: [.planning/phases/12-multi-node-finality-fault-proof/12-05-SUMMARY.md]
  modified:
    - test/src/blockchain/multi_node_finality_fault_test.cpp
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp

key-decisions:
  - "The durable one-vote record is not a RocksDB/recovery defect: it survives every same-root lifecycle stage when no certificate can finalize the slot."
  - "The vote-boundary restart fixture isolates the owner until after its strict direct-record recovery assertion, then reconnects and re-submits through public ingress."

patterns-established:
  - "When testing a pre-finality durable lock, isolate the owner before the fault so a valid remote certificate cannot legitimately release the lock first."

requirements-completed: [TEST-04]

duration: 16m
completed: 2026-08-25
---

# Phase 12 Plan 05: Active-Vote Restart Boundary Summary

**The restart proof now isolates the pre-broadcast vote owner and proves its exact direct RocksDB vote record survives manager close, same-root reopen, and recovery before network finality begins.**

## Accomplishments

- Added read-only, stage-labelled lifecycle snapshots for the direct `/consensus/vote/<slot>` record, including raw byte digest, certificate status, and recovered proposal identity.
- Proved the record remains intact through `ConsensusManager::Close`, manager ownership release, same-root `GlobalDB` reopen, and `RecoverActiveVotes`.
- Corrected TEST-04's fixture ordering: the owner remains disconnected until the original strict post-restart record assertion passes, then reconnects and uses public `SubmitProposal` to finish normal finality.

## Task Commits

1. **Tasks 1–2: Diagnose the lifecycle boundary and repair the fixture-only cause** - `963c9ced` (test)

## Verification

- `cmake --build build/OSX/Release --target multi_node_finality_fault_test consensus_pending_lifecycle_test --parallel 4` — passed.
- `FinalityFaultNetwork.ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary` — passed over real sockets; no `ACTIVE_VOTE_RED` label was emitted because all stages preserved the exact record.
- `FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` — passed over real sockets in 54.5s.
- `consensus_pending_lifecycle_test --gtest_filter='*ActiveVote*:*Restart*:*Certificate*'` — 14/14 passed.
- `ctest --test-dir build/OSX/Release --output-on-failure --timeout 300 -R '^multi_node_finality_fault_test$'` — 6/6 passed in 134.3s.
- Phase 12 anti-shortcut scan and `git diff --check` — passed.

## Deviations from Plan

The planned diagnostic expected three intentionally failing runs. The read-only diagnostic instead passed consistently: once the vote owner was disconnected after registry persistence, no peer could create the matching certificate that would correctly remove its active-vote record. This established that the prior failure was a fixture race, not a production RocksDB or recovery bug. The only repair was therefore the test-only isolation ordering; consensus semantics, CRDT authority, and PubSub cleanup-only behavior remain unchanged.

## Next Phase Readiness

The clean serial four-peer target now passes all six Phase 12 scenarios, including the previously blocked TEST-04 durable vote restart boundary.

---
*Phase: 12-multi-node-finality-fault-proof*
*Completed: 2026-08-25*
