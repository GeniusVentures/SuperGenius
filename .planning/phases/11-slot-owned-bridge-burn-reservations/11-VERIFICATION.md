---
phase: 11-slot-owned-bridge-burn-reservations
verified: 2026-07-29T18:29:38Z
status: gaps_found
score: "11/12 must-have truth clusters verified"
re_verification: true
requirements:
  BURN-01: satisfied
  BURN-02: satisfied
  BURN-03: satisfied
  BURN-04: satisfied
  BURN-05: satisfied
gaps:
  - id: terminal-live-identity
    severity: warning
    truth: "Only an exact identity-matched terminal reservation may idempotently suppress the authoritative winner"
    status: failed
    reason: "ProcessFinalizedCertificate short-circuits any same-slot SAFETY_ERROR or CONSUMED_SAFETY_ERROR before comparing the reservation's certificate digest, proposal ID, winner ID, and outpoint with the current authoritative certificate."
  - id: composed-consumed-corruption-evidence
    severity: warning
    artifact: test/src/blockchain/consensus_burn_reservation_test.cpp
    status: failed
    reason: "The Plan 11-11 recovery test uses a synthetic handler and a reservation-only batch; it does not perform a genuine production mint, corrupt a persisted application/UTXO artifact, and drive UTXOManager -> TransactionManager -> consensus recovery as required."
---

# Phase 11 Verification: Slot-Owned Bridge Burn Reservations

## Result

Phase 11's five normative BURN requirements are implemented in the current code, including the two prior blockers closed by Plans 11-10 and 11-11. Exact resource-handler success is now gated on a durable identity-matched `CONSUMED` reread, and post-consumption contradictions can advance monotonically to `CONSUMED_SAFETY_ERROR` without release, cleanup, wake, or retry after restart.

The phase nevertheless remains `gaps_found` because one explicit Plan 11-11 truth and one advertised Plan 11-11 artifact/acceptance criterion are not met. The three advisory code-review warnings were checked independently: WR-01 and WR-03 prevent complete must-have verification; WR-02 does not invalidate BURN-01..05 or a Phase 11 must-have.

## Goal Achievement

| # | Consolidated must-have truth | Status | Current-code evidence |
|---|---|---|---|
| 1 | The focused harness uses real same-path storage, deterministic clocks/barriers, scoped workers, and active CTest registration. | VERIFIED | `consensus_burn_reservation_test` is registered and the current exact nine-target discovery found it once. The focused post-gap slice ran without sleeps or detached workers. |
| 2 | One strict direct-local reciprocal record binds a canonical slot to one exact burn outpoint and generation; malformed or conflicting state fails closed. | VERIFIED | `ConsensusLocalState.proto:86-111`; strict validation/reciprocal scans in `ConsensusStateStore.cpp:679-817`; atomic create/join and random generation in `ConsensusStateStore.cpp:821-883`. |
| 3 | Reservation/certificate/vote reconciliation completes before subscriptions, timer, recovery, and vote replay; exact final protection is restored without ephemeral candidates. | VERIFIED | `ConsensusManager::New` calls `EnsureBuiltinSlotKeyHandlers` and `RestoreLocalState` before all live startup events (`Consensus.cpp:230-290`). Restore cross-checks canonical outpoint, certificate, vote horizon, terminal identity, and reciprocal state (`Consensus.cpp:329-505`). |
| 4 | Semantic validation remains pure and every approved mint persists resource admission before active candidate visibility or voting. | VERIFIED | All initial/retry paths enter `AdmitProposalResources`; it calls `CreateOrJoinBurnReservation` before continuation (`Consensus.cpp:1339-1387`, `2008`, `3527`, `3618`). Pending/rejected and store-failure tests pass. |
| 5 | Same-burn contenders share one generation and proposal rejection, failure, replacement, timeout, or cleanup cannot release it. | VERIFIED | Candidate-controlled identity is absent from the durable owner. `CreateOrJoinBurnReservation` joins only exact slot/outpoint and monotonically extends the horizon. Reservation deletion exists only in whole-slot reconciliation. Contender and cleanup-isolation tests pass. |
| 6 | Certificate observation establishes exact `FINALIZED_PENDING_APPLICATION` before process work, handler invocation, or cleanup; transient failure keeps the same winner retryable. | VERIFIED | `FinalizeSlot` calls `FinalizeBurnReservation` before `PutPendingProcess` and `ProcessFinalizedCertificate` (`Consensus.cpp:3070-3125`). Restart synthesis and exact-winner retry are implemented and covered. |
| 7 | Finalized application carries the exact live shared store, rejects same-path/different-object storage, and serializes through one store gate with store -> persistence -> UTXO-state lock order. | VERIFIED | Immutable shared handle in `Consensus.hpp`; shared-object identity and store-gated participant in `ConsensusStateStore.cpp:1061-1093`; UTXO locks in `UTXOManager.cpp:1037-1040`. |
| 8 | Winner outputs, canonical application record, physical bridge-input consumption, and reservation `CONSUMED` commit in one batch; exact replay validates every artifact. | VERIFIED | `ApplyFinalizedReservationBatch` stages `CONSUMED` into the participant batch; `ApplyMintEffectsAtomically` stages outputs/input/application and commits that same batch (`ConsensusStateStore.cpp:1061-1093`, `UTXOManager.cpp:941-1180`). Exact replay rejects missing/conflicting artifacts. |
| 9 | `Applied`/`AlreadyApplied` is advisory until an exact durable `CONSUMED` reread; missing/unreadable/final-pending state cannot complete, clean, wake, or mark work done. | VERIFIED | `ProcessFinalizedCertificate` captures exact slot/outpoint/generation/certificate/proposal/winner identity, rereads at `Consensus.cpp:3254`, restores pending for absence/final-pending, and reaches `MarkComplete`, cleanup, and dependency wake only after exact `CONSUMED` (`Consensus.cpp:3254-3339`). All three Plan 11-10 exact tests pass. |
| 10 | An exact contradiction after physical consumption advances monotonically to consumed-terminal safety while preserving reciprocal and finality identity; release/reconsume/recreate are rejected. | VERIFIED | Explicit `CONSUMED_SAFETY_ERROR` enum and strict validator; `MarkBurnReservationSafetyError` performs exact generation/finality matching and `CONSUMED -> CONSUMED_SAFETY_ERROR`, preserving the reciprocal key (`ConsensusStateStore.cpp:950-989`). Store transition/idempotency tests pass. |
| 11 | Terminal replay/suppression is exact-identity matched, and restart recovery has composed production-path evidence from artifact corruption through terminal persistence. | FAILED | The live terminal shortcut at `Consensus.cpp:3162-3171` checks only the enum, not certificate/proposal/winner identity. The named recovery test at `consensus_burn_reservation_test.cpp:1625-1704` uses a synthetic handler and reservation-only commit, not the advertised production corruption path. |
| 12 | Only a provably abandoned uncertified generation releases after strict candidate/vote horizons and exact certificate absence; finality, ABA races, and shutdown cannot unlock current protection. | VERIFIED | `ReconcileBurnReservations` requires strict `now >`, exact `NotFound`, active-vote retirement, candidate recheck, and current generation/horizon; `DeleteReservedBurnReservation` deletes only exact `RESERVED` reciprocal records (`Consensus.cpp:1389-1548`, `ConsensusStateStore.cpp:1034-1058`). Race and shutdown tests pass. |

**Score: 11/12 must-have truth clusters verified.**

## Plans 11-10 and 11-11

### Plan 11-10 — durable `CONSUMED` completion gate

Verified. The post-handler durable reread precedes the only resource-bearing `MarkComplete` path and compares state, slot, source chain, burn hash, receipt index, generation, certificate digest, proposal ID, and winner ID. Missing/unreadable/final-pending restores the process to `PENDING`; readable mismatch becomes irreconcilable. `ClearProposalSlot` and `WakePendingDependency` remain after successful completion only. Finalized-handle decode, missing embedded data, deserialization, and hash-binding failures return `Irreconcilable`, while no-handle legacy behavior remains compatible.

### Plan 11-11 — consumed-terminal safety

Partially verified. The store transition itself is monotonic, exact, reciprocal-preserving, non-releasable, and restart-readable. Startup cross-checks terminal reservation identity against the authoritative certificate and excludes terminal slots from restored handler scheduling. Exact matching duplicate ingress also suppresses later handlers, cleanup, wake, release, and remint.

Two Plan 11-11 contracts remain open:

1. The live `ProcessFinalizedCertificate` terminal short-circuit accepts a same-slot terminal enum without matching its finality identity to the current certificate/process. It then returns `AlreadyFinalized`, allowing the caller to mark certificate work done. This violates the plan truth that mismatched finality cannot succeed and only exact terminal replay is idempotent. It remains fail-closed for burn reuse, so it does not negate BURN-03.
2. `ConsumedArtifactContradictionRecoveryPersistsTerminalSafetyAndStopsRetry` does not start from a genuine `UTXOManager` atomic mint or corrupt a stored application/output/input artifact, and it does not invoke the production `TransactionManager` callback. The component tests separately cover UTXO contradiction classification and production-handler mapping, but the explicit composed restart-recovery artifact promised by Plan 11-11 is absent.

## Artifact and Key-Link Audit

| Artifact / key link | Status | Evidence |
|---|---|---|
| Protobuf reciprocal reservation schema and consumed-terminal state | VERIFIED | Versioned slot record and reciprocal outpoint index are substantive and used by strict reads/writes. |
| ConsensusStateStore strict scans, create/join/finalize/consume/safety/delete transitions | VERIFIED | All transitions are wired through the direct local store mutex; reciprocal create/delete is batched; final/safety states cannot release. |
| Built-in mint slot resolver -> restore-before-live-startup | VERIFIED | Installed before `RestoreLocalState`; startup aborts before subscribe/filter/timer/recovery/replay on strict restore failure. |
| Post-Approve admission -> durable reservation -> candidate continuation | VERIFIED | All observed initial and retry approval paths converge on `AdmitProposalResources`. |
| Certificate authority -> finalized reservation -> exact application handle | VERIFIED | Final reservation persistence precedes process/handler; handle carries the exact shared store and full identity. |
| Shared store gate -> UTXO outputs/application/input -> `CONSUMED` | VERIFIED | One physical participant-owned batch and exact replay checks. |
| Handler disposition -> exact `CONSUMED` -> process complete/cleanup/wake | VERIFIED | Plan 11-10 postcondition is correctly wired. |
| `CONSUMED` contradiction -> production handler -> terminal consensus recovery -> restart suppression | NOT VERIFIED | Component links exist, but the required end-to-end test substitutes a synthetic handler and reservation-only commit. |
| Live terminal record -> authoritative certificate identity | NOT SAFELY WIRED | Startup compares identity, but the live shortcut does not. |
| Timer/recovery -> strict conditional abandonment | VERIFIED | Exact certificate absence, horizons, live evidence, generation, and reciprocal state gate deletion. |

## Requirements Coverage

All Phase 11 requirement IDs are present in `.planning/REQUIREMENTS.md`, mapped to Phase 11, and marked Complete there.

| Requirement | Status | Concrete evidence |
|---|---|---|
| BURN-01 | SATISFIED | Validation has no reservation mutation; approved admission persists before candidate visibility; Pending/rejected/write-failure cases create no usable candidate or vote. |
| BURN-02 | SATISFIED | Same canonical slot/outpoint joins one unchanged generation independent of candidate/account identity; best-candidate changes do not release/reacquire. |
| BURN-03 | SATISFIED | Proposal-local failure/cleanup never deletes the reservation; finalized, consumed, safety, and consumed-safety states cannot release; terminal restart/reconciliation remains non-releasable. |
| BURN-04 | SATISFIED | Certificate observation durably finalizes protection before handler/cleanup; mint artifacts and `CONSUMED` are one batch; consensus completion/cleanup is gated on the exact durable consumed postcondition. |
| BURN-05 | SATISFIED | Only whole-slot reconciliation deletes an uncertified exact generation after strict candidate/vote horizons and exact certificate absence, with admission/finality/ABA/shutdown protection. |

The two open gaps concern a stricter explicit Plan 11-11 exact-terminal identity contract and its required composed verification artifact. Neither creates a release/reuse path, but both prevent the phase from receiving a fully passed must-have audit.

## Review Warning Classification

| Warning | Effect on verification |
|---|---|
| WR-01: live terminal short-circuit lacks authoritative identity match | BLOCKS one Plan 11-11 must-have truth and key link. Does not currently release/reuse the burn. |
| WR-02: expired TransactionManager callbacks occupy registrations | ADVISORY / NON-BLOCKING for Phase 11. The plans explicitly require once-only, weak-owner-safe registration and do not require same-process manager replacement. It is a lifecycle availability issue, not a BURN-01..05 reservation-safety failure. |
| WR-03: no advertised composed consumed-artifact recovery test | BLOCKS the Plan 11-11 test artifact and acceptance criterion. Component coverage is not equivalent to the required end-to-end path. |

## Automated Evidence

| Check | Current result |
|---|---|
| Build `consensus_burn_reservation_test`, `transaction_manager_pending_lifecycle_test`, `utxo_manager_test` | PASS |
| Seven focused Plan 11-10/11 post-gap tests | 7/7 passed in 7.629 s |
| Exact Phase 11 CTest discovery | `Total Tests: 9` |
| Exact Phase 11 guarded execution | 9/9 passed in 152.34 s |
| `git diff --check` | PASS |

The full gate confirms broad regression health but cannot prove an omitted composed scenario or repair the live identity-check omission.

## Gaps to Close

### Gap 1: require exact terminal identity before live short-circuit

Before returning `AlreadyFinalized` for `SAFETY_ERROR` or `CONSUMED_SAFETY_ERROR`, compare the durable outpoint, generation/finality fields, certificate digest, proposal ID, and winner ID with the current authoritative process/certificate identity. A mismatch must not be treated as idempotent terminal success or mark certificate work done. Add a live-delivery mismatch test.

### Gap 2: add the composed consumed-artifact recovery proof

Perform a genuine certified atomic mint through the real TransactionManager handler, leave process work pending, corrupt one durable application/output/input artifact, reopen the same datastore, and drive recovery through `UTXOManager -> TransactionManager -> ProcessFinalizedCertificate`. Assert one contradiction-detection attempt, durable exact `CONSUMED_SAFETY_ERROR`, and zero later handler calls, cleanup, dependency wake, release, remint, or alternate winner across another restart and duplicate ingress.

## Human Verification

None required. Both remaining gaps are deterministic code/test issues. The complete 11-node race remains explicitly assigned to Phase 12.

---

_Verified: 2026-07-29T18:29:38Z_
_Verifier: GSD phase verifier_
