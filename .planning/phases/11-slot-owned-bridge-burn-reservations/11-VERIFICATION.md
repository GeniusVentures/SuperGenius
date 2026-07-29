---
phase: 11-slot-owned-bridge-burn-reservations
verified: 2026-07-29T21:02:00Z
status: passed
score: "12/12 must-have truth clusters verified"
re_verification: true
requirements:
  BURN-01: satisfied
  BURN-02: satisfied
  BURN-03: satisfied
  BURN-04: satisfied
  BURN-05: satisfied
gaps: []
human_verification: []
---

# Phase 11 Verification: Slot-Owned Bridge Burn Reservations

## Result

Phase 11 passes. All five normative requirements, all Plan 11-01 through 11-12 must-have truth clusters, their substantive artifacts, and their key links are present in current production code and deterministic tests.

Plans 11-10 and 11-11 established the exact durable `CONSUMED` completion gate and consumed-derived terminal safety. Plan 11-12 closes the two gaps from the prior verification: live terminal replay now requires exact authoritative identity, and the required composed production test now performs a genuine certified mint, physically deletes a persisted application artifact, and proves permanent fail-closed recovery across repeated same-store reconstruction.

**Score: 12/12 must-have truth clusters verified.**

## Goal Achievement

| # | Consolidated must-have truth | Status | Current-code and test evidence |
|---|---|---|---|
| 1 | The focused harness uses real same-path storage, deterministic clocks/barriers, scoped workers, and active CTest registration. | VERIFIED | `test/src/blockchain/CMakeLists.txt` registers `consensus_burn_reservation_test`; the exact current CTest discovery found 9/9 phase targets. The focused harness uses fixed clocks, predicate barriers, scoped workers, and same-path reopen helpers. |
| 2 | One strict direct-local reciprocal record binds one canonical slot to one exact burn outpoint and generation; malformed, partial, aliased, or conflicting state fails closed. | VERIFIED | `ConsensusLocalState.proto:86-112`; canonical record and reciprocal validation in `ConsensusStateStore.cpp:679-817`; atomic create/join and random generation in `ConsensusStateStore.cpp:821-883`. |
| 3 | Reservation/certificate/vote reconciliation completes before live startup, restores exact protection without ephemeral candidates, and fails closed on durable contradiction. | VERIFIED | `Consensus.cpp:232-290` installs the built-in resolver and completes `RestoreLocalState` before subscription/timer/recovery/replay. `Consensus.cpp:329-520` strict-scans and cross-checks reservations, certificates, votes, processes, outpoints, and finality. |
| 4 | Semantic validation is side-effect-free, while every approved mint persists admission before candidate visibility or voting. | VERIFIED | All initial and retry approval paths call `AdmitProposalResources` before `ContinueProposalAfterSubject` (`Consensus.cpp:1339-1387`, `2008-2009`, `3564-3565`, `3655-3656`). Store/descriptor failure returns before activation. |
| 5 | Same-burn contenders share one generation and proposal rejection, failure, replacement, timeout, revert, or cleanup cannot release it. | VERIFIED | `CreateOrJoinBurnReservation` joins only an exact slot/outpoint and only extends the horizon. Candidate-controlled fields are absent from the owner record. Mint-v2 is excluded from rollback in `TransactionManager.cpp:1985-2007` and `4878-4894`; deletion exists only in whole-slot reconciliation. |
| 6 | Certificate observation establishes exact `FINALIZED_PENDING_APPLICATION` before process work, handler invocation, or cleanup; transient failures retain the exact winner. | VERIFIED | `FinalizeSlot` persists via `FinalizeBurnReservation` before `PutPendingProcess` and `ProcessFinalizedCertificate` (`Consensus.cpp:3091-3125`). Certificate-only synthesis and restart retry use the same finality identity. |
| 7 | Finalized application carries the exact shared store, rejects a different datastore object, and uses one serialization gate with store -> UTXO persistence -> UTXO state lock order. | VERIFIED | Immutable handle and shared store are passed through `TransactionManager.cpp:1866-1927`; `ConsensusStateStore.cpp:1061-1093` rejects different shared-object identity and holds the store gate across participant staging/commit; UTXO locks follow at `UTXOManager.cpp:1037-1040`. |
| 8 | Winner outputs, canonical application, physical bridge-input consumption, and reservation `CONSUMED` commit in one batch; replay verifies the full exact artifact set. | VERIFIED | `ApplyFinalizedReservationBatch` stages the reservation transition into the participant batch (`ConsensusStateStore.cpp:1061-1093`). `ApplyMintEffectsAtomically` validates/stages outputs, bridge input, and application and commits that same batch (`UTXOManager.cpp:941-1180`). Partial or different artifacts return `state_not_recoverable`. |
| 9 | Handler success is advisory until an exact live durable `CONSUMED` reread; missing/unreadable/final-pending state cannot complete, clean, wake, or retire work. | VERIFIED | `ProcessFinalizedCertificate` captures the exact handle, rereads the strict record, and gates the only resource `MarkComplete` path on exact state/outpoint/generation/digest/proposal/winner (`Consensus.cpp:3136-3377`). False-success tests remain pending across restart. |
| 10 | Post-consumption contradiction advances monotonically to exact `CONSUMED_SAFETY_ERROR`, preserving reciprocal/finality identity and preventing release, recreate, reconsume, or alternate winner. | VERIFIED | Explicit enum in `ConsensusLocalState.proto:93`; strict validation and `CONSUMED -> CONSUMED_SAFETY_ERROR` transition in `ConsensusStateStore.cpp:679-721`, `951-989`; deletion accepts only `RESERVED` (`ConsensusStateStore.cpp:1034-1058`). |
| 11 | Live terminal idempotence requires exact authoritative identity; mismatch fails closed without mutation, handler, completion, cleanup, wake, or certificate-work retirement. | VERIFIED | `Consensus.cpp:3141-3218` first binds the process to the normalized certificate, derives the canonical certificate outpoint, performs a strict reciprocal read, and compares slot, outpoint, generation, certificate digest, proposal ID, and winner ID before terminal `AlreadyFinalized`. Mismatch returns `StorageFailure`; therefore the `MarkDone` condition at `Consensus.cpp:3125-3129` is unreachable. Both Plan 11-12 live tests pass. |
| 12 | Only a provably abandoned uncertified exact generation releases after strict candidate/vote horizons and exact certificate absence; finality, ABA, shutdown, terminal recovery, and repeated ingress cannot reopen protection. | VERIFIED | `ReconcileBurnReservations` requires `now >` persisted/live horizons, exact certificate `NotFound`, vote retirement, and final recheck (`Consensus.cpp:1389-1548`). `DeleteReservedBurnReservation` checks exact generation, state, horizon, and reciprocal identity. Race/shutdown tests and the composed repeated-restart terminal test pass. |

## Plan 11-12 Gap Closure

### Exact live terminal identity

Verified against current code, not summary claims:

- `ProcessFinalizedCertificate` first proves the durable process digest, proposal ID, and winner ID equal the currently normalized authoritative certificate (`Consensus.cpp:3141-3151`).
- For mint-v2 it derives the canonical outpoint from the certified subject, loads the reservation through the strict reciprocal reader, and forms the expected identity (`Consensus.cpp:3189-3205`).
- The shared predicate compares slot, source chain, burn hash, receipt-log index, reciprocal-protected generation, certificate digest, proposal ID, and winner ID (`Consensus.cpp:3154-3164`).
- Only an exact `SAFETY_ERROR` or `CONSUMED_SAFETY_ERROR` returns terminal `AlreadyFinalized`. A readable identity mismatch logs actionable expected/observed evidence and returns `StorageFailure`; outpoint or reciprocal-generation corruption fails during strict read before the shortcut.
- `FinalizeSlot` retires certificate work only for `Applied` or exact `AlreadyFinalized`, so mismatch `StorageFailure` cannot call `MarkDone` (`Consensus.cpp:3125-3129`).
- `LiveTerminalExactIdentityDuplicateIsIdempotentWithoutHandlerCleanupOrWake` covers both terminal enums. `LiveTerminalIdentityMismatchFailsClosedAndCannotMarkCertificateWorkDone` covers certificate digest, proposal ID, winner ID, canonical outpoint, and reciprocal generation, including byte-stability and pending-work assertions (`consensus_burn_reservation_test.cpp:1790-1948`).

### Composed certified-mint corruption recovery

Verified as a genuine production composition:

- `TransactionManager::New` registers the production application callback (`TransactionManager.cpp:164-183`). The callback reaches `ApplyConfirmedMintV2`, passes the immutable shared-store handle, and composes `ApplyFinalizedReservationBatch` with `UTXOManager::ApplyMintEffectsAtomically` (`TransactionManager.cpp:1866-1927`).
- `CertifiedMintPersistedArtifactCorruptionRecoversToConsumedSafetyAndNeverRetries` creates a valid two-validator certificate and delivers it through production `FinalizeSlot` (`transaction_manager_pending_lifecycle_test.cpp:1220-1279`). It verifies the exact `CONSUMED` reservation and reciprocal index, canonical application, winner output, physically consumed input, and raw persisted UTXO bytes.
- The test physically removes the canonical bridge-application key via the real datastore while holding UTXO persistence/state locks (`transaction_manager_pending_lifecycle_test.cpp:308-324`, `1280-1286`); it does not mock the reader or substitute a synthetic handler.
- Two reconstructions close and rebuild TransactionManager, Blockchain, ConsensusManager, account, and UTXO state over the same RocksDB (`transaction_manager_pending_lifecycle_test.cpp:812-837`, `1296`, `1336`). Real handler registration drives exactly one contradiction attempt through UTXOManager -> TransactionManager -> consensus, producing exact `CONSUMED_SAFETY_ERROR`.
- The second reconstruction, explicit recovery/reconciliation, and duplicate live certificate ingress keep the handler count at one and leave process non-complete, waiters unwoken, cleanup at zero, the application artifact absent, input/output bytes unchanged, and release/create-join/alternate-winner finalization rejected (`transaction_manager_pending_lifecycle_test.cpp:1304-1385`).

## Artifacts and Key Links

| Artifact / key link | Status | Evidence |
|---|---|---|
| Versioned reciprocal slot/outpoint reservation schema | VERIFIED | Substantive states and identities in `ConsensusLocalState.proto`; generated and used by the strict store. |
| Strict local store transitions and reciprocal scan | VERIFIED | Create/join/finalize/consume/safety/delete paths all use direct RocksDB under one mutex; no CRDT reservation publication path. |
| Built-in slot resolver -> restore-before-live startup | VERIFIED | Resolver is ensured before `RestoreLocalState`; restoration precedes subscription, filters, timer, work recovery, and vote replay. |
| Approved proposal -> durable admission -> active candidate | VERIFIED | Initial and all retry paths converge on `AdmitProposalResources` before continuation. |
| Certificate authority -> finalized reservation -> exact handler | VERIFIED | Final reservation transition precedes process marker and handler; immutable handle carries exact shared state authority. |
| Shared store gate -> UTXO effects -> `CONSUMED` | VERIFIED | One datastore object and one participant-owned physical batch. |
| Handler result -> exact durable postcondition -> complete/cleanup/wake | VERIFIED | Resource completion side effects occur only after exact `CONSUMED`. |
| Live terminal -> authoritative process/certificate identity | VERIFIED | One exact predicate dominates both early terminal idempotence and post-handler terminal handling. |
| Persisted artifact corruption -> UTXOManager -> TransactionManager -> terminal consensus recovery | VERIFIED | Current composed test drives the full production callback and same-store reconstruction path. |
| Timer/recovery -> strict conditional abandonment | VERIFIED | Exact certificate absence, strict horizons, live candidate/vote evidence, state, generation, and reciprocal identity gate deletion. |

## Requirements Coverage

All Phase 11 IDs are defined in `.planning/REQUIREMENTS.md`, mapped to Phase 11, and supported by current code and automated evidence.

| Requirement | Status | Concrete evidence |
|---|---|---|
| BURN-01 | SATISFIED | Semantic validation performs no reservation mutation. Only post-Approve admission creates/joins the durable record, before candidate visibility; Pending/rejected/store-failure cases do not activate or vote. |
| BURN-02 | SATISFIED | The canonical slot/outpoint is the owner. Differently identified contenders join one unchanged generation and best-candidate changes do not release/reacquire. |
| BURN-03 | SATISFIED | Losing proposal failure, timeout, revert, and cleanup do not delete the slot reservation. Finalized, consumed, safety, and consumed-safety states cannot release; composed repeated restart proves no cleanup/release/remint/alternate winner. |
| BURN-04 | SATISFIED | Certificate observation persists exact final-pending protection before handler/cleanup. Winner outputs, canonical application, input consumption, and reservation `CONSUMED` share one batch; completion is gated on exact durable consumption. |
| BURN-05 | SATISFIED | Only whole-slot reconciliation deletes an uncertified exact `RESERVED` generation after strict candidate/vote horizons and exact certificate absence, with admission/finality/ABA/shutdown races covered. |

## WR-02 Scope Assessment

The expired weak-callback registration concern does not alter a normative Phase 11 requirement or must-have:

- Plan 11-06 explicitly requires once-only application registration with weak TransactionManager ownership; Plan 11-12 explicitly excludes callback replacement/unregistration.
- BURN-01 through BURN-05 govern reservation admission, shared ownership, no proposal-local release, certificate consumption, and safe whole-slot abandonment. None requires recreating TransactionManager while retaining the same live Blockchain/ConsensusManager registry.
- Weak capture prevents use-after-free. Production restart reconstruction closes the owning graph and creates a new consensus registry, which the composed test exercises twice.
- The retained-Blockchain/same-process replacement availability concern is therefore broader lifecycle work, not a Phase 11 reservation-safety gap. It does not permit release, reuse, cleanup, alternate winner, or remint.

## Automated Evidence

| Check | Current result |
|---|---|
| Build `consensus_burn_reservation_test` and `transaction_manager_pending_lifecycle_test` | PASS |
| Exact Plan 11-12 live-terminal discovery/execution | 2/2 passed |
| Exact composed production recovery discovery/execution | 1/1 passed |
| Exact Phase 11 CTest discovery | `Total Tests: 9` |
| Exact Phase 11 guarded execution | 9/9 passed in 155.38 s |
| Source/test edits by verifier | None |

The complete 11-node single-burn race and broader compatibility proof remain correctly assigned to Phase 12. No human-only check is needed for Phase 11.

---

_Verified: 2026-07-29T21:02:00Z_
_Verifier: GSD phase verifier_
