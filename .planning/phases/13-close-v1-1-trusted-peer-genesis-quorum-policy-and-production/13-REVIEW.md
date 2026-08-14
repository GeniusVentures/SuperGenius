---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
reviewed: 2026-08-14T18:11:59Z
depth: standard
diff_base: 49cb0177
files_reviewed: 7
files_reviewed_list:
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/TrustStartupController.cpp
  - src/account/TrustStartupController.hpp
  - test/src/multiaccount/multi_account_sync.cpp
  - test/src/multiaccount/policy_lifetime_multi_account_test.cpp
  - test/src/startup/trust_first_boot_e2e_test.cpp
findings:
  critical: 4
  warning: 0
  info: 0
  total: 4
status: issues_found
---

# Phase 13: Code Review Report

**Reviewed:** 2026-08-14T18:11:59Z
**Depth:** standard, with cross-file lifecycle and security call-chain tracing
**Files Reviewed:** 7
**Status:** issues_found

## Summary

Plans 13-23 through 13-26 close the four specifically targeted findings: CR-08, CR-09, CR-10, and WR-07 are no longer reachable by their former call chains. Burn and policy candidates are now authoritatively rediscovered before readiness, the persisted-ready callback no longer re-enters transaction initialization, and the new fixtures genuinely exercise passive A/B-to-C economics and same-path historical restart. The independent `PolicyV2BeforeInitialBurnCannotStrandStartup` case also still proves that local successor signing is rejected before economic readiness and succeeds only afterward.

The release evidence is accurately reported as 15/15 focused cases, exact 25/25 CTest/JUnit name equality, and five passive-lifetime repetitions. Sanitizer status is correctly `NOT_RUN`, not PASS. Those green executions do not cover four remaining reachable source defects: unsynchronized account/manager ownership during account selection, mutation of the signing key behind an immutable trusted-peer signer identity, silent loss of transient refresh failures, and controller destruction from its own raw-pointer worker callback.

The two protected untracked paths, `2026-08-05T18:32:01.3210770Z Current run` and `supergenius_atomic_transaction_test/`, remained untouched.

### Requested finding reassessment

| Prior finding | Disposition | Evidence |
|---|---|---|
| CR-08 | **CLOSED** | `TrustStartupController::Refresh` lists burn candidates at `TrustStartupController.cpp:377-394`, deterministically sorts/deduplicates at `:402-407`, activates at `:408-453`, and only then publishes readiness at `:455`. Automatic signing remains restricted to durable `BootstrapOnly` plus verified local membership at `:347-352`; successor processing never calls the signing hook. Foreign callbacks enqueue at `:156-164`, while self callbacks do not race explicit local activation. |
| CR-09 | **CLOSED** | Every verified refresh calls `ListPendingPolicyCandidates` at `TrustStartupController.cpp:266-283`, merges it with the callback queue, sorts/deduplicates at `:289-297`, preserves `false` as pending, and returns typed actionable errors at `:300-321`. Controller-local suppression is reset by reconstruction. The reconstruction test reopens the same retained CRDT/store and performs no new write or admin/direct activation inside `trust_first_boot_e2e_test.cpp:1375-1398`. Candidate storage remains bounded upstream to 32 active candidates per predecessor, so the reviewed merge does not introduce an unbounded candidate-memory attack. |
| CR-10 | **CLOSED for the original persisted-ready re-entry** | The trust callback only schedules transaction initialization from the two waiting states at `GeniusNode.cpp:866-897`, then revalidates captured epoch and source under the lifecycle mutex at `:899-920`. Persisted-ready construction already in `INITIALIZING_TRANSACTIONS` is ignored. Replacement ownership is stopped/reset before `TransactionManager::New` at `:989-1031`, with generation checks on state and slot-hash callbacks. The historical test reuses the exact `historical_base_path` and asserts one construction/start at `multi_account_sync.cpp:470-560`. CR-11 below is a separate uncovered concurrency path. |
| WR-07 | **CLOSED** | `PassiveBurnSuccessorChangesPayEscrowWithoutReceiverAdmin` switches C's real account/manager, then uses only A's proposal and B's approval and observes C's durable provider plus real `PayEscrow` inside `policy_lifetime_multi_account_test.cpp:378-455`. `PersistedHistoricalTrustAndTransactionsRestartWithSingleManagerOwnership` reopens the same trust/transaction path and verifies historical and new transactions at `multi_account_sync.cpp:470-560`. |

### Security invariants retained

- Durable commit still precedes policy/burn cache publication (`TrustedPeerRegistry::TryActivatePolicyCandidate` and `BurnConfig::TryActivateBurnCandidate` publish only after successful store commit).
- `false` still denotes authenticated below-quorum pending; malformed, wrong-head, corrupt, and commit failures remain errors.
- Receive/list paths do not sign successors, and initial automatic burn signing is limited to the exact deterministic BootstrapOnly record and a current member.
- Candidate authorization continues to bind exact version, predecessor, authorizing policy, signer membership, canonical bytes, and quorum. No new RPC, topic, or implicit approval surface was added.

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-11 [BLOCKER]: Account selection and public consumers race lifecycle-owned shared pointers

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:2474-2522`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:1996-2025`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:3322-3330`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:3368-3375`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:3580-3618`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.hpp:795-807`

**Issue:** The new `lifecycle_mutex_` protects only `StateTransition`. `SelectAccount` does not take it while it stops/moves `transaction_manager_`, swaps `account_`, and starts a new blockchain transition. At the same time, public methods copy those exact `shared_ptr` objects without synchronization (`GetAddress` and `GetTransactionManager`), and the catch-up watcher thread dereferences both members directly. `ShutdownAccountBoundServices` does not stop `catchup_watcher_`; that happens only during full destruction. Concurrent read and write of the same non-atomic `std::shared_ptr` object is a C++ data race/undefined behavior, even if the pointee itself is reference counted. A production-default catch-up callback can therefore observe a torn owner, dereference an account/manager being replaced, or submit work for the wrong selected account. Public transaction calls racing `SelectAccount` have the same defect. The new serialization and manager generation counters do not cover these readers.

This is phase-blocking because account selection is a promised live lifecycle operation and the failure mode includes use-after-free, wrong-account mutation, and process corruption.

**Fix:** Put account selection, account/manager publication, and every asynchronous/public snapshot behind one ownership protocol. Either copy members under `lifecycle_mutex_` (without holding it across blocking `Stop`/join operations) or use atomic `shared_ptr` publication plus a separate serialized switch state. Stop and join the catch-up watcher before replacing account-bound services, then reconstruct it with the new generation. Make callbacks capture an account/manager snapshot and generation rather than reading mutable members. Add a stress test that calls `SelectAccount` while transaction queries and catch-up callbacks run; execute it under TSan when instrumentation is available.

### CR-12 [BLOCKER]: SelectAccount changes the key behind a fixed trusted-peer signer identity

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:817-826`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:2474-2509`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:53-58`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:98-110`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:347-352`, `/Users/henriqueklein/gnus/SGNUS/src/account/BurnConfig.cpp:240-253`

**Issue:** Controller construction permanently records `account_->GetAddress()` as `local_signer_address_`, but the sign callback resolves `self->account_` at invocation time. Phase 13 intentionally retains the controller, registry, and BurnConfig across `SelectAccount`; the selected account is then replaced at lines 2501-2508. Any later signature is consequently stored under the original trusted-peer address but produced by the new account's private key. SecureCrdt signature verification rejects that mismatch.

The most direct failure is an account switch while the durable state is still `BootstrapOnly`: the next `OnTrustedPeerGenesisConfirmed` call labels the deterministic burn-v1 approval as the old current member but signs it with the new account, so initial burn quorum can be stranded. The same mismatch breaks any retained explicit administration path that uses these production policy objects. The current lifetime test switches only passive C after burn-v1 readiness, so zero-signature passive convergence cannot detect this active-member failure.

This is phase-blocking because the node-scoped policy lifetime contract cannot safely retain an account-dependent signer whose key changes independently of its recorded identity.

**Fix:** Give GeniusNode an immutable, node-scoped trust signer (preferably a dedicated key) whose address and signing callback are captured together for the controller lifetime. If the selected account is intentionally the trust identity, forbid account switching while it is a current signer or perform an explicit quorum-authorized identity transition; never dynamically read a different account behind a fixed signer label. Add an active-member test that switches accounts while waiting for initial burn and another after readiness, proving signatures verify under the intended immutable trust identity.

### CR-13 [BLOCKER]: A transient retained-candidate listing failure is silently consumed with no retry

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:170-184`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:266-270`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:377-380`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:534-541`

**Issue:** The worker clears `refresh_requested_` before calling `Refresh` and discards the returned result. Policy and burn discovery both return datastore/authorization errors directly without emitting an event or requeueing work. If the refresh triggered by the final quorum approval encounters one transient listing/read error, there may be no later CRDT write and therefore no later callback. The now-quorum-approved retained successor remains durable in CRDT but is never attempted until an operator happens to call `Refresh` or reconstructs the controller. This recreates the liveness class CR-08/CR-09 were meant to close and also makes the typed error contract invisible to operators.

An immediate retry loop would itself create a denial-of-service risk; the defect is the absence of any bounded retry/backoff and observability, not merely the lack of a tight retry.

**Fix:** Inspect the worker result. On transient discovery/infrastructure errors, emit a typed controller event and schedule a bounded exponential-backoff retry while retaining the coalesced refresh request. Keep candidate validation/commit failures under the existing candidate-scoped suppression policy. Add deterministic fault injection where the first policy and burn listing fails after the last approval and succeeds without a new write.

### CR-14 [BLOCKER]: Releasing the last controller owner from a callback destroys it on its raw-pointer worker

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:170-200`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:515-531`

**Issue:** The refresh thread captures only `instance.get()`. `SetState` and `Emit` invoke caller-owned callbacks synchronously from `Refresh`, including when `Refresh` runs on that worker. A callback is permitted to drop the last external `shared_ptr` (for example, an owner responding to a fatal/activation state by resetting the controller). The destructor then executes on `refresh_worker_` and calls `join()` on the current thread, which throws `resource_deadlock_would_occur`; because destructors are non-throwing, the process terminates. Simply detaching is not safe: after the callback returns, the raw-pointer worker continues through `Refresh` and line 182 and accesses the already-destroyed object.

The GeniusNode callback happens to retain its controller today, but `TrustStartupController::New` exposes these callbacks as a public ownership boundary, and teardown/error callbacks are exactly where owner release is expected. This is a reachable crash/use-after-free and is phase-blocking for callback lifetime safety.

**Fix:** Remove the raw controller pointer from the worker lifetime. Move queue/stop state into a separately owned worker object, or dispatch refresh jobs through an executor whose cancellation/drain lifetime is independent of the controller. Ensure callbacks can release the controller without causing self-join and that no job touches controller members after the final owner is released. Add a test whose state/event callback releases the last controller reference from the worker and prove clean destruction without detach, terminate, or post-destruction access.

---

_Reviewed: 2026-08-14T18:11:59Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard with lifecycle/security cross-file tracing_
