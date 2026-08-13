---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
reviewed: 2026-08-13T20:23:41Z
depth: deep
diff_base: 0a8f1b60
diff_head: 63fa3afe
files_reviewed: 12
files_reviewed_list:
  - src/account/TrustStartupController.cpp
  - src/account/TrustStartupController.hpp
  - src/trustedpeer/TrustStateStore.cpp
  - src/trustedpeer/TrustStateStore.hpp
  - test/src/account/account_management_test.cpp
  - test/src/blockchain/node_startup_test.cpp
  - test/src/multiaccount/CMakeLists.txt
  - test/src/multiaccount/multi_account_sync.cpp
  - test/src/multiaccount/policy_lifetime_multi_account_test.cpp
  - test/src/startup/trust_first_boot_e2e_test.cpp
  - test/src/trustedpeer/trust_state_store_test.cpp
  - test/src/trustedpeer/trustedpeerregistry_quorum_test.cpp
findings:
  critical: 3
  warning: 1
  info: 0
  total: 4
status: issues_found
---

# Phase 13: Code Review Report

**Reviewed:** 2026-08-13T20:23:41Z
**Depth:** deep
**Files Reviewed:** 12 changed files, plus their production dependency call chains
**Status:** issues_found

## Summary

The second gap-closure range correctly repairs the narrow defects recorded as CR-05, CR-06, CR-07, and WR-06, but it does not close Phase 13. Three release-blocking lifecycle paths remain reachable. First, the new refresh implementation returns as soon as burn v1 is ready, making every later passive burn candidate unreachable. Second, policy activation only consumes an in-memory callback queue; an already-retained quorum is never rediscovered after controller reconstruction. Third, persisted-ready startup queues a same-state transition while the original transaction-initialization transition is still running, creating and starting two distinct `TransactionManager` instances. The historical-storage fixture that exposed the latter crash was changed to create fresh storage, so the green gate does not exercise the failing restart path.

The exact TrustStateStore focused cases passed 2/2. The exact three trust-first-boot cases passed 3/3 when run with listener permission. Those results validate the repaired cases but do not reach the blockers below.

### Prior finding reassessment

| Prior finding | Disposition | Evidence |
|---|---|---|
| CR-05: initial burn not attempted | Closed for initial burn | `Refresh()` now discovers and activates pending burn candidates at `TrustStartupController.cpp:363-442`; the focused restart test passes. CR-08 below is a distinct successor-burn regression caused by the new readiness return. |
| CR-06: passive policy activation absent | Closed for live callback delivery only | The registry callback enqueues policy candidates at `TrustStartupController.cpp:146-155`, and `Refresh()` activates them at `:260-319`. CR-09 below remains for restart/retry of retained candidates. |
| CR-07: torn TrustStateStore reads | Closed | Public `LoadAndVerify()` takes `transition_mutex_` at `TrustStateStore.cpp:442-446`, while commit paths use the unlocked helper while already holding that mutex. The two exact concurrency cases pass. |
| WR-06: refresh hides activation failures | Closed for activation attempts | Genesis, policy, and burn activation errors are emitted and returned at `TrustStartupController.cpp:224-243`, `:277-299`, and `:396-417`; the focused activation-failure case passes. |

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-08 [BLOCKER]: Burn-ready nodes return before processing passive burn successors

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:321-325`

**Issue:** `Refresh()` checks `burn_config_->IsEconomicallyReady()`, sets `ConfirmedReady`, and returns before the burn discovery/activation block at lines 327-442. Once burn v1 has committed, every subsequent registry callback requests a refresh, but every such refresh exits at this guard. A passive node can therefore retain a quorum-certified burn v2 (or later) indefinitely without committing it. This is not a theoretical ordering edge: all successor processing is structurally unreachable whenever the current burn configuration is ready. The previous callback path activated a received burn candidate directly; moving activation into `Refresh()` in this closure range introduced the regression. It violates the phase's passive successor-convergence requirement and can leave peers enforcing different economic policy.

The lifetime fixture does not cover this path. It calls `LocalTrustAdmin::ProposeBurn()` on the same node at `test/src/multiaccount/policy_lifetime_multi_account_test.cpp:285-290`; that operator path synchronously invokes activation and bypasses passive receipt.

**Fix:** Move discovery and activation of retained burn successors before the ready-state return. Keep automatic signing restricted to `BootstrapOnly`, but always allow verification/commit of a quorum-ready successor. Add a three-peer test in which A proposes, B approves, and passive C commits burn v2 solely from registry delivery, with no `LocalTrustAdmin` or direct activation call on C.

### CR-09 [BLOCKER]: A retained policy quorum is never replayed after controller restart

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:260-319`

**Issue:** Policy processing consumes only `pending_policy_candidates_`, populated by live callbacks at lines 146-155. `TrustedPeerRegistry` already exposes durable discovery through `ListPendingPolicyCandidates()` (`src/trustedpeer/TrustedPeerRegistry.cpp:459-472`), but the controller never calls it. A repository-wide call-site trace found only `LocalTrustAdmin.cpp:24`, which is an operator listing action; no startup, refresh, or automatic activation path invokes it. Callback registration does not replay existing CRDT records.

Consequently, if a passive peer retains quorum approvals and then reconstructs its controller before the TrustStateStore commit completes—or after the injected commit failure that the new tests exercise—the candidate is absent from the new in-memory queue and can remain uncommitted forever unless another write or operator action happens. The test at `test/src/startup/trust_first_boot_e2e_test.cpp:877-957` destroys the failed passive controller but never reconstructs it and retries its retained candidate, so it does not cover restart replay. This breaks restart convergence and durable retry guarantees.

**Fix:** On every startup/refresh, call `ListPendingPolicyCandidates()` for the current predecessor, merge/deduplicate those records with the callback queue, and attempt eligible candidates in deterministic order. In-process suppression may avoid a tight retry loop, but reconstruction must rediscover retained candidates. Add a test that retains quorum, injects a pre-commit/commit failure, reconstructs the controller without adding another registry write, and observes durable policy advancement.

### CR-10 [BLOCKER]: Persisted-ready startup re-enters transaction initialization and starts two managers

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:843-862`

**Related:** `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:946-966`, `/Users/henriqueklein/gnus/SGNUS/src/account/GeniusNode.cpp:630-633`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:180`, `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:321-325`, `/Users/henriqueklein/gnus/SGNUS/src/transaction/TransactionManager.cpp:398-418`

**Issue:** On a persisted-ready restart, `TrustStartupController::New()` synchronously calls `Refresh()` at line 180. `Refresh()` emits `ConfirmedReady`; the GeniusNode callback posts `StateTransition(INITIALIZING_TRANSACTIONS)` even though GeniusNode is already executing that same transition to construct the controller. `StateTransition()` has no same-state/re-entry guard at lines 630-633. The original transition then constructs and starts one manager at lines 946-966; the queued same-state transition replaces it with another manager and calls `StartCore()` again.

`core_started_` at `TransactionManager.cpp:398-418` is per instance, so it cannot guard two constructions. This is not merely redundant allocation. Each constructor registers GlobalDB and blockchain callbacks; duplicate GlobalDB registration return values are discarded, and destruction of the displaced first manager can unregister shared callbacks and clear account hooks now needed by the replacement. The two startup tasks can also overlap historical transaction initialization. The multi-account fixture repair encountered an LLDB crash specifically with a historical transaction database plus the trust-ready callback; the static call chain above makes that reported failure reachable, although this review did not independently reproduce the crash under LLDB.

The re-entry mechanics were introduced earlier in Phase 13 (`GeniusNode.cpp` blame points to the trust callback change), not by production files in `0a8f1b60..63fa3afe`. However, persisted-trust restart is a Phase 13 requirement, and this closure range changed the failing fixture to avoid persisted storage (WR-07). It therefore remains a Phase 13 release blocker even though the second-gap production patch did not itself create or worsen the underlying re-entry.

**Fix:** Do not post `INITIALIZING_TRANSACTIONS` from the controller callback while that state is already in progress. Add serialized, idempotent state transitions (or explicitly gate this callback to the waiting states), and ensure an existing transaction manager is stopped before replacement. Add a historical-database restart test that asserts exactly one manager construction/start and stable ownership of GlobalDB/account callbacks; run that test under ASan/TSan as well as the normal gate.

## Warnings

### WR-07 [WARNING]: The repaired fixtures bypass both production paths that need lifecycle coverage

**File:** `/Users/henriqueklein/gnus/SGNUS/test/src/multiaccount/multi_account_sync.cpp:389-405`

**Related:** `/Users/henriqueklein/gnus/SGNUS/test/src/multiaccount/policy_lifetime_multi_account_test.cpp:285-290`

**Issue:** The multi-account restart now resets the client and constructs a new node without the former storage path, so it boots fresh rather than reopening the historical trust/transaction database that exposed CR-10. Separately, the policy-lifetime test proposes burn on the same node through `LocalTrustAdmin`, bypassing the passive callback/refresh path broken by CR-08. These fixtures can pass while the required restart and passive-convergence behaviors are nonfunctional, so the advertised broad/lifetime gate is not reliable evidence for those requirements.

**Fix:** Restore a persisted-storage restart case after fixing CR-10, and make the lifetime burn step originate on a different peer so the observed node can advance only through passive registry receipt. Retain fresh-storage coverage as a separate test rather than substituting it for restart coverage.

---

_Reviewed: 2026-08-13T20:23:41Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: deep_
