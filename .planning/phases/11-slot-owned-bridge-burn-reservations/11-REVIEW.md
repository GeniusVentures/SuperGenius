---
status: issues_found
files_reviewed: 18
critical: 0
warning: 3
info: 0
total: 3
depth: standard
phase: 11-slot-owned-bridge-burn-reservations
reviewed_at: 2026-07-29
---

# Phase 11 Code Review

## Result

The two prior phase-blocking gaps are closed in the current code. Resource-bearing `Applied` and `AlreadyApplied` dispositions now require an exact durable `CONSUMED` reread before completion and cleanup, and an exact post-consumption contradiction can advance monotonically to `CONSUMED_SAFETY_ERROR` while retaining reciprocal protection. The focused post-gap regression slice passed 7/7.

Three warnings remain. None currently permits release or reuse of a finalized burn, but one weakens exact terminal identity handling, one leaves same-process handler ownership unrecoverable, and one leaves the promised cross-component corruption recovery path untested.

## Findings

### WR-01 — Terminal burn state short-circuits without matching the authoritative certificate identity

**Severity:** Warning

**Evidence:**

- `src/blockchain/Consensus.cpp:3148-3150` verifies the process record against the current certificate, but `src/blockchain/Consensus.cpp:3162-3171` treats any readable `SAFETY_ERROR` or `CONSUMED_SAFETY_ERROR` record at the slot as terminal without comparing its source-chain/outpoint, certificate digest, proposal ID, or winner ID to the current certificate.
- `src/blockchain/ConsensusStateStore.cpp:679-704` validates each burn record structurally, but does not and cannot cross-check it against the certificate/process record. Startup performs that separate finality comparison at `src/blockchain/Consensus.cpp:421-448`; the live finalization path does not.
- `test/src/blockchain/consensus_burn_reservation_test.cpp:1685-1704` covers restart and duplicate delivery only for an exactly matching terminal record; there is no live-path mismatch test.

**Impact:** A runtime-corrupted or independently written terminal record for the canonical slot can suppress the exact authoritative winner, mark certificate work done through the caller's `AlreadyFinalized` path, and install `SafetyViolation` without recording the identity contradiction. This remains fail-closed for burn reuse, but violates the phase's exact-winner/identity contract and can permanently hide recoverable authoritative work behind unrelated terminal data.

**Suggested remedy:** Before the terminal short-circuit, compare the decoded burn outpoint plus certificate digest, proposal ID, and winner ID against the process/certificate identity. Treat any mismatch as an integrity/storage failure or persist a separately defined identity-conflict safety record; do not return `AlreadyFinalized` for a merely same-slot terminal enum. Add a focused live-delivery mismatch test.

### WR-02 — Expired TransactionManager callbacks permanently occupy the application registration

**Severity:** Warning

**Evidence:**

- `src/account/TransactionManager.cpp:163-186` requires exclusive certificate-application registration before construction can succeed.
- `src/blockchain/Consensus.cpp:842-855` uses `try_emplace`, so an existing callback cannot be replaced, while `src/account/TransactionManager.cpp:354-389` destroys the manager without unregistering its consensus callbacks.
- `src/blockchain/Blockchain.hpp:184-202` exposes application, cleanup, and resource registration but only the combined certificate unregister at line 191; it exposes no cleanup/resource unregister facade. The consensus-layer removals at `src/blockchain/Consensus.cpp:897-925` therefore are not available to `TransactionManager` through `Blockchain`.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp:792-809` proves the retained weak callback returns `owner_dead`, but never constructs a replacement manager against the same live blockchain.

**Impact:** Destroying and recreating `TransactionManager` while retaining `Blockchain` leaves an expired application handler in the registry, so the replacement's required registration fails and `TransactionManager::New` returns null. Cleanup/resource callbacks also remain tied to the expired owner. This is a same-process lifecycle outage, even though the weak capture prevents use-after-free.

**Suggested remedy:** Use generation/token-based handler ownership and unregister only the matching owner's complete registration set during teardown and partial-construction rollback. Add a reset-and-recreate test that verifies one live application, subject, resource, and cleanup handler remains.

### WR-03 — The consumed-artifact recovery test does not exercise the advertised end-to-end path

**Severity:** Warning

**Evidence:**

- `test/src/blockchain/consensus_burn_reservation_test.cpp:1653-1666` directly commits only the reservation transition through `ApplyFinalizedReservationBatch` and then has a synthetic handler return `Irreconcilable`; it does not create mint/application/UTXO artifacts, corrupt one, or invoke the production `TransactionManager` handler.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp:978-1003` separately proves the production handler maps a mocked missing application read to `Irreconcilable`, but stops before `ProcessFinalizedCertificate`, terminal persistence, close/reopen, and retry suppression.
- `test/src/account/utxo_manager_test.cpp:961-989` separately covers the four artifact-corruption classifications, but does not drive them through TransactionManager and consensus recovery.

**Impact:** Each component is covered in isolation, but no test proves the critical composed path `UTXOManager -> TransactionManager -> ProcessFinalizedCertificate -> CONSUMED_SAFETY_ERROR -> restart suppression`. A wiring, error-code translation, handle-identity, or restart-order regression between those seams could pass all current tests. This falls short of the explicit Plan 11-11 end-to-end recovery acceptance criterion.

**Suggested remedy:** Build the consensus recovery test with the real TransactionManager application callback, perform a genuine atomic mint, corrupt one persisted artifact, leave the process pending, reopen the same datastore, and assert one production handler attempt followed by durable consumed-terminal safety and zero later retries/cleanup/wakes.

## Verification performed

- Reviewed exactly the 18 requested production, schema, build, and test files at standard depth, plus the requested Phase 11 planning/review/verification context.
- Ran the seven focused post-gap tests covering false success, exact consumed completion, consumed-terminal transitions/idempotency, and restart suppression; all 7 passed.
- No source or test files were edited and no commits were created. Pre-existing dirty paths were preserved.
