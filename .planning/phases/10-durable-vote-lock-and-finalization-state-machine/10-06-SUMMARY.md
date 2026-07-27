---
phase: 10-durable-vote-lock-and-finalization-state-machine
plan: 06
subsystem: consensus-safety
tags: [consensus, certificates, safety, rocksdb, recovery]

requires:
  - phase: 10-durable-vote-lock-and-finalization-state-machine
    provides: Unified durable certificate finalization and exact-winner retry from Plan 10-05
provides:
  - Durable canonical evidence for valid competing certificates from every ingress source
  - Atomic conflict-evidence and slot-safety persistence with first-winner identity preservation
  - Restart-restored slot-local proposal, vote, aggregation, certificate, and publication gates
  - Critical diagnostics and unique canonical conflict-pair metrics
affects: [phase-10, consensus-recovery, certificate-finalization, safety-observability]

tech-stack:
  added: []
  patterns:
    - Fully validate both certificates before classifying an occupied-slot conflict
    - Canonicalize only the evidence key while preserving authoritative and incoming direction
    - Restore durable safety state before replay or network participation

key-files:
  created:
    - .planning/phases/10-durable-vote-lock-and-finalization-state-machine/10-06-SUMMARY.md
  modified:
    - src/blockchain/impl/proto/ConsensusLocalState.proto
    - src/blockchain/ConsensusStateStore.hpp
    - src/blockchain/ConsensusStateStore.cpp
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_vote_journal_test.cpp
    - test/src/blockchain/consensus_finalization_test.cpp
    - test/src/blockchain/consensus_certificate_store_test.cpp

key-decisions:
  - "Conflict records use a digest-sorted pair for one stable key while explicit authoritative/incoming fields preserve first-winner direction."
  - "The unique-pair metric counts durable evidence keys and is initialized from persisted conflict records on restart."
  - "Safety restoration includes both proposal IDs so vote publication can stop before either proposal is reconstructed in memory."

patterns-established:
  - "Validate-before-classify: invalid or dependency-stalled traffic cannot create safety evidence."
  - "Evidence-before-stop: one RocksDB batch commits conflict metadata and authoritative safety identity before local gates activate."

requirements-completed: [CERT-07]

duration: 44 min
completed: 2026-07-27
---

# Phase 10 Plan 06: Durable Certificate Conflict Safety Summary

**Every valid competing certificate path now records one canonical, durable safety event before suppressing only the affected slot, while preserving exact retries of the original winner.**

## Performance

- **Duration:** 44 min
- **Started:** 2026-07-27T17:29:00Z
- **Completed:** 2026-07-27T18:13:00Z
- **Tasks:** 1
- **Files modified:** 8

## Accomplishments

- Routed Local, PubSub, CRDT, and Recovery conflicts through full structural, signature, registry, quorum, and deterministic-byte validation before evidence classification.
- Persisted certificate-free evidence metadata and authoritative slot safety in one RocksDB batch, with a canonical sorted digest-pair key, directional identities, first/cumulative source tracking, and duplicate observation counts.
- Preserved the first durable certificate as authority, rejected all competing application and publication, and retained the original winner's idempotent application retry.
- Restored safety slots and both conflict proposal IDs before replay, then gated proposal admission, vote publication, vote aggregation, certificate creation, pending certificate processing, and replay only for affected slots.
- Added critical structured diagnostics and one unique-pair metric recovered from persisted evidence.
- Added all-source, invalid-before-classification, CRDT pre-merge rejection, canonical deduplication, unrelated-slot, original-retry, and restart-restoration coverage.

## Task Commits

1. **Task 1: Persist valid certificate conflicts and enforce durable slot-local safety stops** — `07b2fa41` (feat)

## Files Created/Modified

- `src/blockchain/impl/proto/ConsensusLocalState.proto` — adds first-source and directional identity metadata without certificate payload bytes.
- `src/blockchain/ConsensusStateStore.hpp/.cpp` — returns the canonical merged evidence record and atomically deduplicates it with safety state.
- `src/blockchain/Consensus.hpp/.cpp` — records all-ingress conflicts, restores safety, emits diagnostics/metrics, and enforces slot-local participation gates.
- `test/src/blockchain/consensus_vote_journal_test.cpp` — covers reversed-order deduplication, first-source preservation, atomic safety evidence, and restart replay suppression.
- `test/src/blockchain/consensus_finalization_test.cpp` — covers Local/PubSub/Recovery conflict convergence, slot-local gates, no second publication, and original-winner retry.
- `test/src/blockchain/consensus_certificate_store_test.cpp` — proves invalid replicated traffic creates no evidence and valid CRDT conflicts reject before merge.

## Decisions Made

- Canonical evidence identity and first-winner direction are separate: sorting makes the pair idempotent, while explicit fields preserve authority.
- Duplicate observations merge only source bits, last-seen time, and count; they cannot rewrite first source or either directional identity.
- A conflict persistence failure remains a storage failure, never an in-memory-only safety classification.
- Safety gates do not globally halt consensus and do not block exact recovery/application of the authoritative certificate.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Extended the durable evidence schema and store contract**
- **Issue:** The plan's four-file list could not preserve first source, explicit authoritative/incoming direction, or return the merged record needed for exact metrics and diagnostics.
- **Fix:** Extended the local-state schema and state store, then adapted the focused journal tests.
- **Verification:** Reversed and repeated observations produce one sorted evidence key and preserve first source and direction.

**2. [Rule 1 - Bug] Corrected repeated-observation fixture semantics**
- **Issue:** The repeated test observation retained the earlier first-source bit while its per-call source bitset represented another ingress.
- **Fix:** Each observation carries its own single first/source bit; the merged record retains the original first source.
- **Verification:** The focused canonical conflict-pair test passes.

---

**Total deviations:** 2 auto-fixed issues (1 missing critical capability, 1 test bug).
**Impact on plan:** Both changes are required to prove durable first-winner evidence and exact deduplication; no certificate bytes or unrelated production subsystem changed.

## Issues Encountered

- Combined real-CRDT fixture runs can retain orphan GraphSync jobs after interruption and wait during teardown. Isolated finalization, CRDT, and evidence-store acceptance cases complete and pass; a restart-focused invocation reached the existing teardown wait after exercising its body.

## Verification

- All three modified consensus test targets build successfully.
- `ValidConflictIsCanonicalDurableSlotLocalAndOriginalWinnerCanRetry` passes 1/1.
- `CRDTConflictValidatesBeforeRecordingAndRejectsBeforeMerge` passes 1/1.
- `ConflictPairIsSortedDeduplicatedAndBatchedWithSafety` passes 1/1.
- Static inspection confirms CRDT structural validation precedes occupied-slot classification and the evidence schema stores metadata only.
- `git diff --check` passes; protected pre-existing dirty paths remain unstaged and unmodified.

## User Setup Required

None.

## Next Phase Readiness

- CERT-07 is implemented across every certificate ingress and restored before participation.
- Plan 10-07 can run the bounded full Phase 10 regression and concurrency gate, including a fresh-process restart fixture.
- No implementation blocker remains for Plan 10-07.

## Self-Check: PASSED

- Implementation commit `07b2fa41` exists and all eight implementation/test files are present.
- Focused local/recovery, CRDT, and evidence-store acceptance tests pass.
- Protected pre-existing dirty paths remain unstaged and uncommitted.

---
*Phase: 10-durable-vote-lock-and-finalization-state-machine*
*Completed: 2026-07-27*
