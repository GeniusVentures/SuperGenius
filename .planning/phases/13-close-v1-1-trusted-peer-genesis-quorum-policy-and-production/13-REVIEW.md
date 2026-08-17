---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
reviewed: 2026-08-17T14:18:27Z
depth: deep
files_reviewed: 7
files_reviewed_list:
  - src/account/GeniusNode.hpp
  - src/account/GeniusNode.cpp
  - src/account/TrustStartupController.hpp
  - src/account/TrustStartupController.cpp
  - test/src/multiaccount/multi_account_sync.cpp
  - test/src/multiaccount/policy_lifetime_multi_account_test.cpp
  - test/src/startup/trust_first_boot_e2e_test.cpp
findings:
  critical: 4
  warning: 2
  info: 0
  total: 6
status: issues_found
---

# Phase 13: Code Review Report

**Reviewed:** 2026-08-17T14:18:27Z
**Depth:** deep
**Files Reviewed:** 7
**Status:** issues_found

## Summary

The 13-27/13-28 implementation was traced across account selection, transaction-manager replacement, public transaction submission, bridge/catch-up startup, immutable trust signing, refresh retry dispatch, timer cancellation, controller destruction, and the new regressions. Four release-blocking correctness/lifetime defects remain: account selection exposes a partial generation and admits overlapping switches, already-snapshotted callers can submit into a stopped manager, bridge owners are still published with unsynchronized `shared_ptr` access, and refresh notifications arriving late in an active attempt are discarded. Two additional defects weaken dispatcher drain behavior and allow the CR-11 regression to pass the partial state it is meant to reject.

The accepted whole-disk/all-anchor rollback boundary was preserved and is not reported as a finding.

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01 [BLOCKER]: SelectAccount publishes an account/null-manager generation before replacement initialization completes

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:2557-2560`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:692-753`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:1016-1028`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:3387-3395`

**Issue:** `ShutdownAccountBoundServices(true)` has already moved/reset `transaction_manager_` when `SelectAccount` stores the replacement `account_`. `StateTransition(INITIALIZING_BLOCKCHAIN)` only starts the blockchain; transaction initialization is deferred through the blockchain callback and another `io_` post. Nevertheless, line 2560 clears `account_service_switching_` immediately. During that asynchronous window, `SnapshotAccountServices()` returns a non-null replacement account, a null manager, and the new generation. This contradicts the claimed sole complete-pair publication at line 1028 and makes `GetAddress()` advertise a selected account whose services do not exist yet.

The premature clear also admits another `SelectAccount` while the first replacement blockchain is still starting. Blockchain completion callbacks carry no account-service generation, only a weak node, and accept any current `INITIALIZING_BLOCKCHAIN` state. A callback from the first replacement can therefore initialize transactions for the second replacement, followed by the second callback replacing that manager again. The result is partial snapshots, duplicate manager ownership cycles, and callbacks associated with the wrong switch.

**Fix:** Keep `account_service_switching_` true until the complete account/manager pair is published at line 1028. Give each blockchain start/retry callback the account-service generation and expected `Blockchain*`, and reject it unless both still match under `lifecycle_mutex_`. On every terminal initialization failure, explicitly fail the switch and publish a well-defined unavailable/error state rather than clearing the flag with a partial tuple.

### CR-02 [BLOCKER]: A strong snapshot prevents UAF but still permits transactions to be queued after its manager is stopped

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:2998-3016`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:2848-2861`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:2013-2041`, `/Users/henriqueklein/gnus/SGNUS/src/account/TransactionManager.cpp:343-357`, `/Users/henriqueklein/gnus/SGNUS/src/account/TransactionManager.cpp:571-591`, `/Users/henriqueklein/gnus/SGNUS/src/account/TransactionManager.cpp:1120-1139`

**Issue:** Public operations copy a coherent strong account/manager tuple, but they never revalidate or lease that generation before mutating it. A caller can snapshot the old ready manager, then `SelectAccount` invalidates the generation, calls `TransactionManager::Stop()`, deconfigures the account, and releases node ownership. The caller's strong pointer keeps the manager alive. `Stop()` sets only `stopped_`; it does not change `State::READY`. `TransferFunds()` therefore still passes its state check, reserves old-account UTXOs, and `EnqueueTransaction()` appends to the queue without checking `stopped_`. `MintFunds()` has the same race and can persist/reserve a bridge input in the retired account. The API reports a submitted transaction that a stopped manager will never process, potentially stranding reservations/funds.

The same ownership gap remains in the public inline `GetProcessingStatus()` (`GeniusNode.hpp:535-540`): it reads/dereferences `processing_service_` while account switching resets that same non-atomic `shared_ptr` at `GeniusNode.cpp:1974-1997`, which is a C++ data race and possible use-after-free.

**Fix:** Add a generation-scoped operation lease. Submission must either revalidate and execute its short mutation section under lifecycle serialization, or acquire a per-generation reader count that `SelectAccount` drains before `Stop()` and account deconfiguration. Independently make every `TransactionManager` mutator reject `stopped_` under the same submission lock used by `Stop()`, including a final check before reservation/enqueue. Snapshot processing-service ownership under a mutex (or atomically) before public dereference. Add deterministic barriers around snapshot, stop, reservation, and enqueue to prove retired generations cannot accept work.

### CR-03 [BLOCKER]: Bridge provider and watcher publication still races SelectAccount teardown

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:3631-3652`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:1979-1985`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:2015-2020`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:2516-2528`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:3751-3759`

**Issue:** `InitializeAndStartBridge()` validates an account snapshot only at entry, then assigns and reads `rpc_endpoint_provider_`, `bridge_relayer_`, and `catchup_watcher_` without `lifecycle_mutex_`. Concurrently, `SelectAccount` resets the provider/relayer and moves the watcher during teardown. Concurrent read/write of the same ordinary `std::shared_ptr` object is undefined behavior.

There is also a concrete stale-publication interleaving: a switch can move an empty `catchup_watcher_` after bridge initialization took its snapshot but before line 3751. The stale initializer then installs and starts its watcher after teardown has passed the stop point. A later initializer overwrites that member without calling `stopWatching()` on the stale watcher. `MessagingWatcher` has a default destructor despite its thread using `this`, so this path can leave a live thread accessing a destroyed watcher in addition to the `shared_ptr` data race. Generation checks inside burn callbacks do not make owner publication or watcher-thread lifetime safe.

**Fix:** Build provider, relayer references, and watcher as locals. Under `lifecycle_mutex_`, revalidate the captured account/manager/generation and atomically publish the complete bridge-owner set; discard locals on mismatch. Ensure every previously published watcher is moved under that same lock and `stopWatching()` is called outside it before destruction. Do not start a watcher in a window where a switch can already have completed its stop pass; either start as part of the validated publication critical section or add a cancellation/start handshake. Exercise the real provider/watcher path with barriers at snapshot, publication, start, and account teardown under TSan.

### CR-04 [BLOCKER]: Coalesced refresh requests are unconditionally erased and can strand a newly replicated candidate

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:625-666`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:165-214`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:353-367`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:469-482`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:697-706`

**Issue:** Requests received while a dispatch is active set `coalesced_request = true`, but `FinishDispatch()` always clears that bit and never schedules a follow-up pass. This loses real work, not only duplicate notifications. For example, after a successful attempt copies `pending_policy_candidates_` at line 356, a foreign approval for a different policy candidate can arrive. Its callback appends the new ID and requests refresh; `RequestDispatch()` records only the coalesced bit because the current attempt is active. The current pass cannot see the ID it already copied, then returns success and `FinishDispatch()` erases the bit. The ID remains in `pending_policy_candidates_`, but with no later CRDT write there is no callback to process it. The same interleaving exists after the burn copy at line 471 and on the last actionable/exhausted attempt.

This violates passive recovery without new writes and can indefinitely strand an already replicated quorum-approved successor.

**Fix:** Track a monotonic request/dirty generation rather than a bit that is discarded. Each attempt records the generation it began processing; when it finishes, schedule one fresh attempt if the request generation advanced after the relevant snapshot, while preserving the seven-attempt budget for the completed failure cycle. Avoid false dirty increments by requesting from `enqueue_candidate` only when a new ID was actually inserted. Add a barrier test that submits a distinct policy and burn candidate after the corresponding pending-vector copy but before successful finish, then proves both activate without another write or manual refresh.

## Warnings

### WR-01 [WARNING]: Destruction can cancel a timer before its async wait is armed, leaving post-destruction dispatch work alive

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:783-793`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:240-258`, `/Users/henriqueklein/gnus/SGNUS/test/src/startup/trust_first_boot_e2e_test.cpp:116-121`

**Issue:** The production timer is stored in `dispatch->retry_timer` under the mutex, the mutex is released, and only then is `async_wait` installed. If the last controller owner is released on another thread in that gap, the destructor moves the timer and calls `cancel()` while it has no outstanding wait; the cancellation is a no-op. The dispatching thread subsequently arms the wait. Its handler retains `RefreshDispatchState` (including callbacks/hooks), fires as late as 3.2 seconds after controller destruction, and only then observes `stopped` and finishes. This is not a controller UAF, but it violates prompt cancellation/drain and can duplicate idle notifications. All retry tests replace the real timer with `schedule_retry`, so they cannot cover this production race.

**Fix:** Make timer publication and wait registration one mutex-protected operation so destruction cannot cancel between them. The handler is already strand-serialized, so it cannot run the next attempt inline before the current handler exits. Add a real-steady-timer test with a barrier between timer construction/publication and wait registration, release the controller, and assert the only completion is immediate `operation_aborted` with no delayed attempt or retained dispatch callback.

### WR-02 [WARNING]: The CR-11 regression explicitly skips the partial account generation and bypasses production catch-up behavior

**File:** `/Users/henriqueklein/gnus/SGNUS/test/src/multiaccount/multi_account_sync.cpp:693-701`

**Related:** `/Users/henriqueklein/gnus/SGNUS/test/src/multiaccount/multi_account_sync.cpp:83-90`, `/Users/henriqueklein/gnus/SGNUS/test/src/multiaccount/multi_account_sync.cpp:641-687`

**Issue:** The assertion loop says `if (!observed.account || !observed.manager) continue`, so the account/non-null plus manager/null tuple published by CR-01 is treated as acceptable and never checked. The selector also waits for `READY` before each next switch, and concurrent readers perform only getters/balance reads; they never hold a snapshot across manager stop and attempt a real transaction. Finally, the catch-up thread calls the private `ApplyIfCurrentAccountServices` helper directly instead of invoking a production watcher callback. The test can therefore pass while all three CR-11 production failures above remain reachable.

**Fix:** Assert that account and manager presence are identical for every non-switching snapshot, and add barriers that hold a real public transaction operation and real watcher/provider initialization across `SelectAccount`. Attempt a second selection during the first initialization and assert it remains rejected until complete-pair publication. Verify the retired manager receives no reservation/queue mutation and every stale watcher is stopped/joined.

---

_Reviewed: 2026-08-17T14:18:27Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: deep_
