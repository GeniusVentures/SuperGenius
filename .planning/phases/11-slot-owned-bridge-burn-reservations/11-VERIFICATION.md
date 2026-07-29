---
phase: 11-slot-owned-bridge-burn-reservations
verified: 2026-07-29T13:31:53Z
status: gaps_found
score: 8/10 must-haves verified
overrides_applied: 0
gaps:
  - truth: "A successful certified mint cannot complete or clean up until the exact reservation is durably Consumed"
    status: failed
    reason: "Consensus trusts ApplicationDisposition::Applied without rereading the reservation, so it can mark the process COMPLETE and run cleanup while the burn remains FinalizedPendingApplication and mint effects are absent."
    artifacts:
      - path: src/blockchain/Consensus.cpp
        issue: "ProcessFinalizedCertificate marks COMPLETE for Applied/AlreadyApplied without proving the exact generation and finality identity reached CONSUMED."
      - path: src/account/TransactionManager.cpp
        issue: "Finalized-handle fallback deserialization/hash failures can return Applied without invoking the shared atomic mint batch."
      - path: test/src/blockchain/consensus_burn_reservation_test.cpp
        issue: "SharedStoreApplicationHandlePrecedesHandlerAndCleanup deliberately returns Applied without consuming and does not assert a consumed postcondition."
    missing:
      - "Reread and verify the exact reservation is CONSUMED before MarkComplete, Applied lifecycle, cleanup, or dependency wake."
      - "Classify finalized-handle embedded-mint decode/hash failures as Irreconcilable rather than Applied."
      - "Add integrated tests proving false Applied cannot complete/clean up and malformed/hash-mismatched finalized mints cannot bypass consumption."
  - truth: "Every irreconcilable exact-winner contradiction reaches durable terminal SafetyError and stops futile retry"
    status: failed
    reason: "A contradiction found after CONSUMED maps to Irreconcilable, but MarkBurnReservationSafetyError only accepts FinalizedPendingApplication; the transition fails and certificate work remains retryable instead of terminal."
    artifacts:
      - path: src/blockchain/ConsensusStateStore.cpp
        issue: "MarkBurnReservationSafetyError rejects identity-matched CONSUMED records."
      - path: src/blockchain/Consensus.cpp
        issue: "The irreconcilable path treats the rejected terminal transition as StorageFailure."
      - path: test/src/account/utxo_manager_test.cpp
        issue: "Consumed artifact contradictions are detected only at the UTXO layer; no end-to-end terminal-state assertion exists."
    missing:
      - "Add a monotonic identity-matched CONSUMED-to-terminal-safety transition, or an equivalent durable contradiction state that preserves consumed/finality facts."
      - "Add restart/recovery coverage proving consumed artifact corruption becomes terminal and is not retried."
---

# Phase 11: Slot-Owned Bridge Burn Reservations Verification Report

**Phase Goal:** Align bridge UTXO reservation and consumption with the canonical consensus slot so competing proposals cannot unlock or reuse a burn.
**Verified:** 2026-07-29T13:31:53Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

The roadmap success criteria and all nine PLAN frontmatter contracts were consolidated without reducing scope.

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | Validation is side-effect-free and semantic approval durably creates/joins the reservation before candidate visibility or voting. | ✓ VERIFIED | `AdmitProposalResources` calls the subject descriptor then `CreateOrJoinBurnReservation` before `ContinueProposalAfterSubject` can activate the candidate (`Consensus.cpp:1333-1380`). `AdmissionPersistsBeforeCandidateVisibility` and `AdmissionStoreFailureLeavesNoCandidateOrReservation` cover ordering/failure. |
| 2 | Same-burn contenders across proposal/account identity share one canonical slot/outpoint generation. | ✓ VERIFIED | Reciprocal slot/outpoint records and idempotent join live in `ConsensusStateStore.cpp:818-880`; candidate identity is absent from the record schema. `BurnReservationStoreCreatesReciprocalAndJoinsOneGeneration` and `ContendersJoinOneGenerationAcrossCandidateIdentities` pass. |
| 3 | Rejection, failure, replacement, and losing cleanup cannot release slot-owned protection held by another candidate, vote, or certificate. | ✓ VERIFIED | `ReleaseProposalAdmission` removes only orphaned proposal state (`Consensus.cpp:1312-1331`); reservation deletion exists only in whole-slot reconciliation. `CleanupCallbacksCannotReleaseSharedReservation` and pending-lifecycle regressions pass. |
| 4 | Reciprocal durable reservations restore and reconcile before live consensus side effects, failing closed on malformed or contradictory state. | ✓ VERIFIED | Startup recomputes slot/outpoint identity, cross-checks certificate finality, synthesizes certificate-only protection, and extends vote horizons (`Consensus.cpp:412-494`). Restart/corruption/startup-order tests pass. |
| 5 | Authoritative certificate observation durably establishes `FinalizedPendingApplication` before handler invocation or cleanup. | ✓ VERIFIED | `FinalizeSlot` persists `FinalizeBurnReservation` at `Consensus.cpp:3073-3090` before creating process work and invoking `ProcessFinalizedCertificate` at line 3119. `RestartCertificateReconcileCreatesFinalProtectionBeforeHandler`, `SharedStoreApplicationHandlePrecedesHandlerAndCleanup`, and write-failure coverage pass. |
| 6 | Exact-winner failures retry across restart; every irreconcilable contradiction becomes durable terminal `SafetyError` and stops retry. | ✗ FAILED | Retry and pre-consumption safety-error cases pass, but a consumed-artifact contradiction maps to `Irreconcilable` and then fails because `MarkBurnReservationSafetyError` accepts only `FINALIZED_PENDING_APPLICATION` (`ConsensusStateStore.cpp:947-976`). The process returns `StorageFailure`, not terminal safety. |
| 7 | Finalized application receives the exact live shared `ConsensusStateStore`; datastore object identity and one serialization gate are enforced. | ✓ VERIFIED | The immutable handle owns `shared_ptr<ConsensusStateStore>` (`Consensus.hpp:272-284`); consensus passes `state_store_` (`Consensus.cpp:3206-3220`). `ApplyFinalizedReservationBatch` rejects a distinct shared datastore object even at the same path and holds the store gate (`ConsensusStateStore.cpp:1048-1080`). Identity and competing-writer tests pass. |
| 8 | Winner outputs, application record, bridge-input consumption, and reservation `CONSUMED` transition commit in one batch, and consensus only completes after that postcondition. | ✗ FAILED | The normal path is genuinely one-batch (`TransactionManager.cpp:1917-1928`, `ConsensusStateStore.cpp:1073-1080`, `UTXOManager.cpp:1012+`). However, `ProcessFinalizedCertificate` trusts `Applied` and calls `MarkComplete`/cleanup without rereading `CONSUMED` (`Consensus.cpp:3206-3277`); the focused handler test demonstrates this accepted hollow success. |
| 9 | Uncertified abandonment requires strict passage beyond candidate/vote horizons, exact certificate `NotFound`, current generation/horizon, and reciprocal one-batch deletion; admission/finality/ABA races cannot delete current protection. | ✓ VERIFIED | `ReconcileBurnReservations` enforces strict `now >`, live-candidate/vote checks, two authoritative certificate checks, and expected generation/horizon deletion (`Consensus.cpp:1383-1537`; `ConsensusStateStore.cpp:1020-1045`). Horizon, lookup uncertainty, admission, finality, and fresh-generation tests pass. |
| 10 | Shutdown wakes and drains owned reconciliation so no reservation mutation occurs after manager destruction. | ✓ VERIFIED | `Close` sets closing/stop flags, wakes timer/slot waiters, joins the timer, and waits for active leases to reach zero (`Consensus.cpp:607-634`). `ShutdownPausedReconciliationDrainsWithoutMutation` passes. |

**Score:** 8/10 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `src/blockchain/impl/proto/ConsensusLocalState.proto` | Versioned reciprocal lifecycle records | ✓ VERIFIED | `BurnReservationRecord` and `BurnReservationOutpointIndex` exist at lines 86 and 111 with Reserved, FinalizedPendingApplication, Consumed, and SafetyError states. No schema drift was reported or observed. |
| `src/blockchain/ConsensusStateStore.hpp/.cpp` | Strict local identity, transitions, shared batch gate, conditional release | ✓ SUBSTANTIVE + WIRED | Used by startup, admission, finality, application, and reconciliation. Reciprocal writes/deletes are atomic and direct local storage. |
| `src/blockchain/Consensus.hpp/.cpp` | Slot lifecycle orchestration and exact-store application handle | ⚠ PARTIAL | Admission, restart, finality, abandonment, ABA, and shutdown wiring are substantive; completion lacks the `CONSUMED` postcondition and consumed contradictions cannot reach terminal safety. |
| `src/blockchain/impl/Blockchain.cpp` | Canonical resolver/handler forwarding before consensus restoration | ✓ VERIFIED | Built-in slot handling is installed on the construction path and startup tests cover fresh registration ordering. |
| `src/account/TransactionManager.hpp/.cpp` | Descriptor extraction and exact certified mint disposition | ⚠ PARTIAL | Normal finalized mint path uses the exact shared handle and batch; finalized-handle fallback failures can incorrectly report `Applied`. |
| `src/account/UTXOManager.hpp/.cpp` | Atomic certified mint effects and exact replay validation | ✓ VERIFIED | Applies outputs, canonical application record, bridge consumption, and staged reservation consumption under the participant batch; exact replay and contradiction checks are substantive. |
| `test/src/blockchain/consensus_burn_reservation_test.cpp` | Deterministic store/admission/restart/finality/application/abandonment/race/shutdown evidence | ⚠ PARTIAL | 32 focused tests exist and key tests pass. Missing assertions expose the two cross-component gaps rather than closing them. |
| Phase 10 compatibility targets | Prior vote/finality/certificate/UTXO behavior remains green | ✓ VERIFIED | Exact eight-target discovery and execution evidence passed 8/8. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| Semantic approval | Durable reservation | Descriptor then `CreateOrJoinBurnReservation` before candidate activation | ✓ WIRED | All initial/retry paths converge on `AdmitProposalResources`. |
| Canonical slot | Exact burn outpoint | Reciprocal strict records and recomputed mint slot | ✓ WIRED | Conflicts, aliases, corrupt halves, and generation mismatch fail closed. |
| Certificate authority | Finalized protection | `FinalizeBurnReservation` before process/handler/cleanup | ✓ WIRED | Certificate-only restart synthesis is also wired. |
| ConsensusManager | TransactionManager | Immutable handle carrying the exact `state_store_` and finality identity | ✓ WIRED | No path-based reconstruction, raw pointer, or weak store authority. |
| Shared store gate | UTXO atomic effects | Same datastore object and caller batch | ✓ WIRED | Normal application stages `CONSUMED` in the same physical batch. |
| Handler success | Process COMPLETE and proposal cleanup | `ApplicationDisposition` | ✗ NOT SAFELY WIRED | There is no exact `CONSUMED` reread/postcondition before completion. |
| Reconciliation | Conditional deletion | Strict horizons, exact `NotFound`, generation/horizon recheck | ✓ WIRED | Admission, finality, ABA, and shutdown interleavings are serialized. |

### Data-Flow Trace

| Flow | Source | Durable sink | Status |
|---|---|---|---|
| Admitted proposal burn identity | `TransactionManager` resource descriptor | Reciprocal reservation slot/outpoint records | ✓ FLOWING |
| Authoritative certificate identity | Normalized certificate digest/proposal/winner | `FinalizedPendingApplication` reservation plus process record | ✓ FLOWING |
| Certified mint effects | Immutable finalized handle + embedded mint | Outputs/application/input/`CONSUMED` in one RocksDB batch | ✓ FLOWING on normal path |
| Application completion | Handler disposition | Process COMPLETE / cleanup | ✗ HOLLOW POSTCONDITION — not bound to durable `CONSUMED` |
| Abandonment evidence | Clock, live candidates, durable vote, certificate lookup | Reciprocal delete batch | ✓ FLOWING |

## Requirements Coverage

Every BURN ID appears in PLAN frontmatter and in `.planning/REQUIREMENTS.md`; no Phase 11 requirement is orphaned.

| Requirement | Source Plans | Description | Status | Concrete Evidence |
|---|---|---|---|---|
| BURN-01 | 11-01, 11-02, 11-03, 11-04, 11-09 | Validation pure; admission reserves under canonical slot | ✓ SATISFIED | `AdmitProposalResources` ordering plus `PendingAndRejectedAdmissionRemainSideEffectFree`, admission persistence/failure tests, and restart restoration. |
| BURN-02 | 11-01, 11-02, 11-04, 11-08, 11-09 | Competing proposals share slot reservation | ✓ SATISFIED | Reciprocal create/join and monotonic horizon code; same-generation contender and stale-admission race tests. |
| BURN-03 | 11-01, 11-02, 11-04, 11-06, 11-07, 11-08, 11-09 | Losing proposal cannot release protected burn | ✓ SATISFIED | Proposal cleanup never deletes reservations; terminal states cannot be conditionally deleted; cleanup/finality/ABA tests pass. |
| BURN-04 | 11-01, 11-02, 11-03, 11-05, 11-06, 11-07, 11-09 | Certificate winner consumes before cleanup/reuse | ✗ BLOCKED | Finalized protection is persisted before cleanup and normal application is atomic, but consensus can mark complete and clean up on `Applied` without proving `CONSUMED`. |
| BURN-05 | 11-01, 11-02, 11-03, 11-04, 11-08, 11-09 | Ready only after whole-slot abandonment and vote expiry | ✓ SATISFIED | Strict horizons, exact certificate absence, vote retirement, generation/horizon conditional delete, race protection, and shutdown drain are implemented/tested. |

**Coverage:** 4/5 requirements satisfied

## Automated Check Evidence

| Check | Result | Interpretation |
|---|---|---|
| Exact Phase 11 `ctest -N` discovery | `Total Tests: 9` | Guard selects the intended nonzero target set. |
| Exact Phase 10 compatibility `ctest -N` discovery | `Total Tests: 8` | Guard selects every Phase 11 regression target except the new reservation target. |
| Supplied guarded Phase 11 execution | 9/9 passed in 145.05 s | Broad implementation/regression evidence is green. |
| Supplied guarded Phase 10 execution | 8/8 passed in 106.46 s | Compatibility evidence is green. |
| Independent focused spot-check | 11/11 passed in 11.731 s | Admission, contenders, cleanup, pre-handler finality, exact store identity, atomic serialization, abandonment, ABA/finality races, and shutdown all executed successfully. |
| Focused test discovery | 32 named tests | Store through shutdown behaviors are registered; no zero-test false green. |
| Schema check | no drift | Reservation protobuf and code agree; execution evidence reports no drift. |

Passing tests do not close the two gaps: `SharedStoreApplicationHandlePrecedesHandlerAndCleanup` explicitly returns `Applied` without consuming, and consumed-artifact contradiction coverage stops at `UTXOManager` rather than driving the terminal consensus transition.

## Advisory Review Warning Classification

| Warning | Classification | Rationale |
|---|---|---|
| WR-01: `Applied` trusted without `Consumed` proof | **Phase-blocking goal gap** | It directly violates BURN-04, the roadmap's consume-before-cleanup criterion, and Plan 11-07's atomic-consumption completion contract. The code and existing focused test demonstrate the missing postcondition. |
| WR-02: post-`Consumed` contradiction cannot persist `SafetyError` | **Phase-blocking must-have gap** | The burn remains unavailable, so immediate reuse safety fails closed, but Plan 11-05 explicitly requires every irreconcilable winner/application contradiction to become durable terminal SafetyError and stop futile retry. Actual call-path evidence disproves that truth. |
| WR-03: expired weak callbacks block same-process TransactionManager replacement | **Real deferred/non-goal issue** | Registration occupancy and replacement outage are real, not a false positive. However, Phase 11 required once-only weak-owner registration and `ConsensusManager` reconciliation shutdown drain; it did not require replacing a destroyed `TransactionManager` while retaining the same live blockchain. Track separately without changing this phase score. |

## Anti-Patterns Found

| File | Pattern | Severity | Impact |
|---|---|---|---|
| `src/blockchain/Consensus.cpp` | Success disposition not verified against durable consumed state | 🛑 Blocker | Allows process completion/cleanup without certified mint consumption. |
| `src/blockchain/ConsensusStateStore.cpp` | Terminal transition excludes consumed contradiction | 🛑 Blocker | Leaves irreconcilable recovery in repeated storage-failure processing. |
| Phase-modified source/test set | Legacy TODO/sleep/detach matches outside Phase 11 logic | ℹ Info | `git blame` places sampled matches before Phase 11; no Phase-11 reservation test uses sleeps/detached workers, and no reservation replication path was found. |

## Human Verification Required

None — all Phase 11 contracts and both gaps are deterministically verifiable in code/tests. The complete 11-node race is explicitly Phase 12, not a Phase 11 human gate.

## Gaps Summary

Two material gaps remain. Neither currently unlocks a finalized burn: `FinalizedPendingApplication` and `Consumed` are both non-releasable. However, Phase 11 promises more than fail-closed non-reuse: successful certified application must durably consume before cleanup, and irreconcilable exact-winner state must become terminal rather than retry forever. Both promises are observably false on concrete call paths.

### Recommended Gap Plans

#### 11-10: Bind application completion to durable consumption

**Objective:** Prevent a certified mint from reaching process COMPLETE, Applied lifecycle, cleanup, or dependency wake unless its exact reservation generation/finality identity is durably `CONSUMED`.

1. After an `Applied`/`AlreadyApplied` resource-handler disposition, reread the exact reservation and require matching slot/outpoint/generation/certificate/proposal/winner plus `CONSUMED`; otherwise restore pending or enter safety based on the disposition/error.
2. Make finalized-handle embedded-mint decode/deserialization/hash failures return `Irreconcilable`, never `Applied`.
3. Add integrated false-`Applied`, malformed embedded mint, hash mismatch, retry/restart, and no-cleanup-before-consumed tests; rerun exact 9/8 guarded gates.

#### 11-11: Persist terminal safety after consumed-artifact contradiction

**Objective:** Convert exact-winner contradictions discovered after physical consumption into durable terminal safety state without weakening consumed/finality identity.

1. Add a monotonic identity-matched `CONSUMED -> SAFETY_ERROR` transition or equivalent durable terminal contradiction representation that retains reciprocal protection.
2. Drive missing/conflicting applied artifacts through `TransactionManager` and `ProcessFinalizedCertificate` during restart/recovery; assert critical terminal state, no cleanup/release, and no further handler retries.
3. Re-run store transition, atomic application, recovery, and exact Phase 11/Phase 10 compatibility gates.

The weak-callback replacement issue should be tracked as a later lifecycle/ownership task, not folded into these Phase 11 goal-closure plans.

---

_Verified: 2026-07-29T13:31:53Z_
_Verifier: the agent (gsd-verifier)_
