---
status: issues_found
files_reviewed: 18
critical: 0
warning: 3
info: 0
total: 3
depth: standard
phase: 11-slot-owned-bridge-burn-reservations
---

# Phase 11 Code Review

## Scope and result

Reviewed the 18 listed production, schema, build, and test files at standard depth. The review traced reservation creation/join, reciprocal RocksDB records, startup restoration, post-validation admission, certificate finalization, shared-store application, one-batch UTXO consumption, abandonment/ABA checks, shutdown draining, and the associated tests.

The reciprocal slot/outpoint writes and deletions are batched, admission persists before candidate visibility, finalization persists burn protection before application and cleanup, the finalized application handle owns the exact `ConsensusStateStore`, UTXO/application/reservation consumption shares one physical batch, and reconciliation uses generation plus candidate-horizon checks. Three lifecycle gaps remain.

## Findings

### WR-01 — `Applied` is trusted without proving the burn reservation was consumed

**Severity:** Warning

**Evidence:**

- `src/blockchain/Consensus.cpp:3206-3225` accepts the application handler's disposition without rereading the durable reservation.
- `src/blockchain/Consensus.cpp:3257-3277` marks the certificate process `COMPLETE`, moves the slot to `Applied`, and clears proposals for every non-`Retryable`, non-`Irreconcilable` disposition.
- `src/account/TransactionManager.cpp:3383-3408` returns `ApplicationDisposition::Applied` when an embedded transaction cannot be deserialized or fails its hash binding, even when a finalized reservation handle is present. No UTXO, application record, or `Consumed` reservation transition occurs on those paths.
- `test/src/blockchain/consensus_burn_reservation_test.cpp:1237-1267` uses a handler that returns `Applied` without consuming the reservation. The test checks that the reservation is finalized before the handler, but does not assert the required postcondition after finalization.

**Impact:** A certified mint can be durably recorded as completely applied and proposal cleanup can run while its reservation remains `FINALIZED_PENDING_APPLICATION` and its mint effects are absent. The burn remains unavailable, so this is fail-closed against reminting, but the exact winner is no longer automatically retried and the durable process/reservation states contradict each other after restart. This violates the successful `FinalizedPendingApplication -> Consumed` lifecycle and the exact-winner retry contract.

**Suggested remedy:** Before `MarkComplete`, reread the exact reservation whenever `burn_reservation` is present and require the same generation/finality identity in `CONSUMED`. Treat a missing or still-final-pending postcondition as `Retryable` (or `Irreconcilable` when the handler explicitly reports invalid certified data). In `TransactionManager`, do not return `Applied` for certificate fallback decode/hash failures when `finalized_handle` is present; classify them as `Irreconcilable`. Add an integrated test where the application handler returns `Applied` without calling the shared-batch API and assert that process completion/cleanup is rejected, plus finalized-handle tests for malformed and hash-mismatched embedded mint data.

### WR-02 — A contradiction discovered after `Consumed` cannot enter durable `SafetyError`

**Severity:** Warning

**Evidence:**

- `src/account/UTXOManager.cpp:1025-1069` correctly returns `state_not_recoverable` when a consumed reservation's application record or exact UTXO artifacts are missing or inconsistent.
- `src/account/TransactionManager.cpp:3337-3343` maps that error to `ApplicationDisposition::Irreconcilable`.
- `src/blockchain/Consensus.cpp:3227-3243` then always calls `MarkBurnReservationSafetyError` for an irreconcilable mint.
- `src/blockchain/ConsensusStateStore.cpp:962-968` only permits that transition from `FINALIZED_PENDING_APPLICATION`; an existing `CONSUMED` reservation returns `Conflict`.
- `test/src/account/utxo_manager_test.cpp:836-881` verifies detection of a consumed-artifact contradiction only at the UTXO layer. It does not drive the disposition through `ProcessFinalizedCertificate` and verify a durable terminal state.

**Impact:** If restart/replay finds a consumed reservation with missing or conflicting application artifacts, the safety transition fails, `ProcessFinalizedCertificate` returns `StorageFailure`, and the process record remains retryable/processing rather than entering the required durable terminal safety-error state. The burn is still locked by `Consumed`, but the node repeatedly retries an irreconcilable operation and lacks the durable critical diagnostic required by D-13.

**Suggested remedy:** Permit an identity-matched monotonic `CONSUMED -> SAFETY_ERROR` transition (or add a distinct durable terminal contradiction state that preserves consumed/finality facts), updating only the slot record while retaining the reciprocal index. Add an end-to-end restart/replay test that corrupts/removes an applied artifact after consumption, invokes certificate recovery, and asserts durable terminal safety state with no further handler retries.

### WR-03 — Weak callbacks expire safely but permanently occupy handler registrations

**Severity:** Warning

**Evidence:**

- `src/account/TransactionManager.cpp:163-186` registers the application handler first and returns `nullptr` if the registration is already occupied.
- `src/blockchain/Consensus.cpp:836-849` intentionally rejects overwrite with `try_emplace`.
- `src/account/TransactionManager.cpp:354-389` destroys a manager without unregistering its subject, certificate/application, cleanup, or resource-admission handlers.
- `src/blockchain/Blockchain.hpp:170-202` exposes unregistering only for subject and certificate handlers; cleanup and resource-admission registrations have no corresponding facade removal API.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp:711-729` proves the retained weak callback returns `owner_dead`, but does not attempt to construct a replacement `TransactionManager` against the same live `Blockchain`/`ConsensusManager`.

**Impact:** Destroying a `TransactionManager` while keeping the blockchain alive leaves an expired application handler in the registry. A replacement manager cannot register and `TransactionManager::New` returns null. Cleanup handlers also accumulate expired lambdas, and resource admission remains bound to an expired owner. This turns safe weak ownership into a same-process restart/lifecycle outage.

**Suggested remedy:** Return ownership tokens from handler registration and unregister only the matching generation/token during teardown, so an old owner cannot erase a newer registration. At minimum, add symmetric cleanup/resource unregister facade methods and have `TransactionManager` unregister every successful registration in reverse order, including rollback of partial construction. Add a test that resets the manager, constructs a replacement on the same blockchain, and verifies exactly one live handler of each kind.

## Additional observations

- No source or test files were modified by this review.
- Pre-existing user changes in `src/account/GeniusNode.cpp` and `ProofSystem` were excluded as directed.
- Existing tests cover reciprocal corruption, creation failure atomicity, finality-before-handler ordering, datastore identity, competing-writer serialization, strict abandonment horizons, finality/admission ABA races, and paused reconciliation shutdown. The findings above are gaps in cross-component postconditions and owner replacement rather than failures of those covered contracts.
