---
status: clean
files_reviewed: 18
critical: 0
warning: 0
info: 0
total: 0
depth: standard
phase: 11-slot-owned-bridge-burn-reservations
reviewed_at: 2026-07-29
---

# Phase 11 Code Review

## Result

No in-scope correctness, security, concurrency/lifetime, state-monotonicity, strict-identity, work-journal, or test-validity findings remain at standard depth.

The prior blocking findings are resolved in current production code and are covered by focused regressions. Exact live terminal replay cannot complete merely because a same-slot record has a terminal enum, and the composed corruption test now drives a genuine certified mint through Consensus, TransactionManager, and UTXOManager before physical persisted-artifact deletion and same-store restart recovery.

## Prior finding disposition

### WR-01 — Exact live terminal identity: resolved

- `src/blockchain/Consensus.cpp:3141-3150` first binds the durable process to the normalized certificate digest, proposal ID, and winner ID.
- `src/blockchain/Consensus.cpp:3152-3216` derives the expected mint outpoint from the authoritative certificate, obtains the generation only through the strict slot/reciprocal read, and requires exact slot, outpoint, generation, certificate digest, proposal ID, and winner ID before returning terminal `AlreadyFinalized`.
- `src/blockchain/ConsensusStateStore.cpp:679-817` validates canonical slot/outpoint structure and reciprocal generation. Canonical outpoint or reciprocal-generation corruption therefore fails before terminal idempotence.
- A readable terminal finality mismatch returns `StorageFailure` before handler invocation, process completion, cleanup, dependency wake, or certificate-work completion. Exact terminal delivery returns `AlreadyFinalized`, installs `SafetyViolation`, performs no handler/cleanup/wake, and permits only the caller's intentional work-journal retirement.
- `LiveTerminalExactIdentityDuplicateIsIdempotentWithoutHandlerCleanupOrWake` covers both `SAFETY_ERROR` and `CONSUMED_SAFETY_ERROR`; `LiveTerminalIdentityMismatchFailsClosedAndCannotMarkCertificateWorkDone` covers digest, proposal, winner, canonical-outpoint, and reciprocal-generation contradictions with byte-stability assertions.

### WR-03 — Composed production corruption/recovery coverage: resolved

- `CertifiedMintPersistedArtifactCorruptionRecoversToConsumedSafetyAndNeverRetries` constructs a valid two-validator certificate and delivers it through production `FinalizeSlot` with the application callback registered by `TransactionManager::New`.
- `src/account/TransactionManager.cpp:1865-1929` passes the exact finalized handle into the shared-store batch gate and calls `UTXOManager::ApplyMintEffectsAtomically`; `src/account/UTXOManager.cpp:1012-1180` stages winner outputs, physical bridge-input consumption, and the canonical application record, then commits the same batch that already contains the reservation's `CONSUMED` transition.
- The test verifies the exact `CONSUMED` record and reciprocal index, winner output, consumed bridge input, canonical application, and raw UTXO records; then removes the physical application key under production persistence/state lock order rather than mocking the reader.
- Two same-RocksDB reconstructions use real TransactionManager registration/recovery. The first production attempt observes `CONSUMED` plus the missing application, maps `state_not_recoverable` to `Irreconcilable`, and advances monotonically to exact `CONSUMED_SAFETY_ERROR`. The second restart, explicit recovery/reconciliation, and duplicate live ingress leave the handler count unchanged and produce no completion, cleanup, dependency wake, release, remint, or alternate-winner finalization.

### WR-02 — Expired weak callbacks: not an in-scope Phase 11 finding

The callback remains insert-only and an expired owner cannot be replaced while the same Blockchain/ConsensusManager remains alive. That is a legitimate broader same-process availability concern. It is not a defect against this phase's explicit contract: Plan 11-06 requires once-only registration with weak TransactionManager ownership, Plan 11-12 explicitly excludes callback replacement/unregistration, and BURN-01 through BURN-05 do not require TransactionManager recreation over a retained Blockchain. Weak capture prevents use-after-free, while the composed Phase 11 restart closes the entire ownership graph before reconstructing production objects. This item is deferred rather than counted as a Phase 11 finding.

## Safety and lifecycle assessment

- Finalized burn state advances only `FINALIZED_PENDING_APPLICATION -> CONSUMED -> CONSUMED_SAFETY_ERROR` (or pre-consumption `SAFETY_ERROR`); terminal states cannot return to `RESERVED` or be conditionally deleted.
- Handler `Applied`/`AlreadyApplied` remains advisory until an exact durable `CONSUMED` reread. Missing, unreadable, final-pending, or mismatched state cannot reach `MarkComplete`, proposal cleanup, or dependency wake.
- Exact terminal duplicates may retire certificate work without retry; mismatched terminal identity returns `StorageFailure`, so `FinalizeSlot` cannot call `MarkDone`.
- The shared object-identity gate and lock order remain store gate -> UTXO persistence -> UTXO state. The Plan 11-12 production change adds no callback-under-gate, new release path, registration lifecycle change, or schema weakening.
- Consumed corruption retains the reciprocal reservation and exact finality identity. Reconciliation, create/join, conditional deletion, duplicate delivery, and alternate-winner finalization cannot release or reapply the burn.

## Verification performed

- Reviewed the 18 Phase 11 production, schema, build, and focused test files identified by phase summaries, plus `11-VERIFICATION.md`, `11-12-PLAN.md`, `11-12-SUMMARY.md`, and the prior review.
- Built `consensus_burn_reservation_test` and `transaction_manager_pending_lifecycle_test`.
- Exact discovery found the two Plan 11-12 live-terminal tests and the one composed production recovery test once each.
- Focused execution passed 2/2 live-terminal tests and 1/1 composed recovery test.
- `git diff --check` passed.
- No source or test files were edited, and no commit was created. Pre-existing dirty paths were preserved.
