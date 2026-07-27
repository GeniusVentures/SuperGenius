---
phase: 10-durable-vote-lock-and-finalization-state-machine
status: issues_found
depth: standard
files_reviewed: 21
findings:
  critical: 0
  warning: 4
  info: 0
  total: 4
reviewed_commits: b11e50bd^..53c5aba8
---

# Phase 10 Code Review

## Summary

Phase 10 establishes the intended durable ordering for the normal path: selection freezes before signing, exact signed bytes are persisted before publication, finality authority is persisted before the processing marker and handler, cleanup follows successful application, and the broad proposal mutex is released around signer, storage, pubsub, registry-finalization, and production handler calls. Strict startup restoration, conflict evidence batching, CRDT callback deferral, and exact vote replay are well covered.

Four edge-case defects remain. None overwrites an authoritative certificate or permits a second published local vote in the reviewed normal paths, but two can permanently suppress participation or shutdown, one can create false durable safety evidence, and one can defer certificate application until restart after a local journal-write failure.

## Findings

### WR-10-001 — A stale live conflict permanently safety-stops a finalized slot

- **Severity:** Warning
- **Location:** `src/blockchain/Consensus.cpp:2447`
- **Failure scenario:** A slot already has its authoritative certificate. After the competing certificate's proposal/vote acceptance horizon has expired, a peer replays that structurally valid competing certificate through pubsub or local submission. The node records durable conflict evidence and enters `SafetyViolation`, even though the same certificate would be rejected by the live first-observation timestamp rules if the slot were empty.
- **Evidence:** `FinalizeSlot` applies `ValidateCertificateForFirstObservation` only in the `!authority_existed` branch at lines 2467-2469. The occupied-slot branch at lines 2447-2458 compares deterministic bytes and immediately calls `RecordCertificateConflict` after structural validation. `HandleCertificate` routes live pubsub input to this path, while `LiveCertificateHorizonChecksProposalAndVotesButStructuralReplayIsTimeless` covers live validation and timeless exact replay but not a stale *different* live certificate against existing authority. This also conflicts with the vote-retirement premise that an expired signature can no longer contribute to a certificate accepted by current live rules.
- **Actionable fix:** Separate exact authoritative replay from new conflicting observation. Keep exact stored/recovery replay timeless, but for `Local` and `PubSub` conflicts require `ValidateCertificateForFirstObservation(normalized, now) == Approve` before recording safety evidence. Define explicitly whether a CRDT conflict represents historical durable authority and add source-specific stale-conflict tests.

### WR-10-002 — Signing or vote-store failure leaves a false `Voted` state with no durable record

- **Severity:** Warning
- **Location:** `src/blockchain/Consensus.cpp:1164`
- **Failure scenario:** The signer transiently fails, protobuf serialization fails, or `PutActiveVote` returns an operational error. The slot transitions from `SigningPublishing` to `Voted` and receives a `durable_proposal_id`/generation even though no active vote record exists. Later candidates are retained only as late diagnostics, replay finds no record, and retirement cannot advance because `GetVote` is empty. The validator therefore never votes in that slot again until restart.
- **Evidence:** The failure branches at lines 1164-1176, 1184-1198, and 1228-1241 all assign `Lifecycle::Voted`; only the success path has actually stored a durable record. `ReplayDurableVote` requires `GetVote` to return an active record, and the retirement path likewise requires one. The focused test `VoteStoreFailurePublishesNothingAndNeverSignsAnotherCandidate` asserts the current permanent suppression, so the gap is encoded rather than untested.
- **Actionable fix:** Introduce an explicit failed-signing/failed-persistence lifecycle with bounded retry and diagnostics, or restore `Selecting` with a fixed non-extended deadline when no signature bytes were produced. For a signed-but-unpersisted vote, fail the slot closed explicitly and define restart behavior; do not label it durable or silently recover to a state that can sign a competitor without a documented proof that the signature never escaped. Add signer-failure, serialization-failure, and transient-store-recovery tests.

### WR-10-003 — `Close()` is not safe when invoked from a leased callback

- **Severity:** Warning
- **Location:** `src/blockchain/Consensus.cpp:493`
- **Failure scenario:** A registered certificate handler, pubsub callback, CRDT callback, or timer-driven handler decides to shut down the consensus manager and calls the public `Close()` method synchronously. If called from an ordinary leased callback, `Close()` waits for `active == 0` while the caller itself owns an activity lease, causing a self-deadlock. If the callback is running on `round_timer_`, line 495 attempts to join the current thread and can throw `std::system_error`/terminate before the activity drain.
- **Evidence:** `FinalizeSlot` acquires an activity lease and invokes the copied external certificate handler at line 2706. `Close()` unconditionally joins `round_timer_` at lines 493-495 and then waits for the global active count to reach zero at lines 497-499. The existing `CloseWaitsForBlockedHandlerAndDrainsBeforeDestruction` test calls `Close()` from a separate thread; it does not cover reentrant shutdown from the handler itself.
- **Actionable fix:** Make callback-initiated shutdown asynchronous or make `Close` owner-aware: never self-join, and do not wait for the caller's own lease while it is on the stack. A two-phase `RequestClose()` plus externally joined `Close()` is the simplest contract. Add deterministic tests that invoke shutdown from a certificate handler and from timer-owned work.

### WR-10-004 — CRDT callback deferral can lose live application work on a journal write failure

- **Severity:** Warning
- **Location:** `src/blockchain/Consensus.cpp:3512`
- **Failure scenario:** The CRDT slot/index merge commits, then the synchronous new-element callback calls `MarkSeen`/`MarkStalled` while RocksDB returns an error for either local journal write. The APIs return `void` and their internal writes discard errors, so the callback cannot report or retry the failure. The authoritative certificate is present, but `RecoverPendingCertificateWork` has no unfinished entry and the transaction handler is not run until a process restart rescans slot authority.
- **Evidence:** Lines 3512-3513 rely exclusively on two unobservable journal mutations to schedule post-merge finalization. `CRDTWorkJournal::MarkSeen` and `MarkStalled` return `void`, and `PutEntryUnlocked` failures are not propagated. The real-CRDT regression proves reentrancy is avoided when the journal succeeds, but has no fault injection for the scheduling write.
- **Actionable fix:** Give the work-journal mutations an error-returning contract and handle failure in `CertificateReceived` with a critical diagnostic plus an in-memory retry signal that the owned timer can reconcile by scanning committed slot authority. Alternatively, make the CRDT merge atomically establish recoverable work. Add a journal-write-failure test proving application retries without restart and without reentrant `GlobalDB::Put`.

## Verification

- `consensus_finalization_test`: 8/8 passed.
- `consensus_vote_journal_test`: 31/31 passed.
- Phase implementation provenance was checked from `b11e50bd` through `53c5aba8`; the two user-owned `GeniusNode.cpp` logger-level edits were excluded from findings.
- No source or test files were modified, and no commit was created.

