---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
reviewed: 2026-08-13T15:31:27Z
depth: deep
diff_base: 0c1679d7
diff_head: 0db1cb7f
files_reviewed: 23
files_reviewed_list:
  - src/account/BurnConfig.cpp
  - src/account/GeniusNode.cpp
  - src/account/TransactionManager.cpp
  - src/account/TrustStartupController.cpp
  - src/account/TrustStartupController.hpp
  - src/securecrdt/CMakeLists.txt
  - src/securecrdt/SecureCrdt.cpp
  - src/securecrdt/SecureCrdt.hpp
  - src/trustedpeer/TrustStateStore.cpp
  - src/trustedpeer/TrustStateStore.hpp
  - src/trustedpeer/TrustedPeerRegistry.cpp
  - src/trustedpeer/genesis_tool/LocalTrustAdmin.cpp
  - test/src/account/burnconfig_policy_e2e_test.cpp
  - test/src/securecrdt/CMakeLists.txt
  - test/src/securecrdt/securecrdt_candidate_race_test.cpp
  - test/src/securecrdt/securecrdt_candidate_test.cpp
  - test/src/securecrdt/securecrdt_quorum_fixture.hpp
  - test/src/securecrdt/securecrdt_quorum_gate_test.cpp
  - test/src/startup/trust_first_boot_e2e_test.cpp
  - test/src/startup/trust_restart_test.cpp
  - test/src/trustedpeer/operator_approval_test.cpp
  - test/src/trustedpeer/trust_genesis_tool_test.cpp
  - test/src/trustedpeer/trust_state_store_test.cpp
findings:
  critical: 3
  warning: 1
  info: 0
  total: 4
status: issues_found
---

# Phase 13: Code Review Report

**Reviewed:** 2026-08-13T15:31:27Z
**Depth:** deep
**Files Reviewed:** 23
**Range:** `0c1679d7..0db1cb7f` (including `f3184b60`)
**Status:** issues_found

## Summary

The gap-closure changes correctly repair the four blockers in the prior review: durable initial-burn sequencing is enforced, restart consumes historical burn authorization, `PayEscrow` uses exact overflow-safe arithmetic, and legacy SecureCrdt signatures are restricted to canonical current members with bounded retention. The pending-versus-error contract correction in `f3184b60` also matches current activation behavior, and the reviewed fixture teardown paths are null-safe.

The current production composition nevertheless still has three ship-blocking lifecycle/concurrency defects. Normal nodes never create or retry the deterministic initial-burn candidate after genesis, policy quorum is never activated on passive receiving nodes, and `LoadAndVerify()` can report valid concurrently advancing state as corrupt because its multi-key read is not synchronized or snapshotted. One additional refresh path still suppresses actionable activation failures.

The two focused durable sequencing regressions passed in this review environment. The focused network-backed arithmetic test could not bind its listener under sandbox restrictions; Plan 13-18 records a prior listener-capable 25/25 gate, but that gate does not exercise the missing production calls identified below.

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-05 [BLOCKER]: Production startup never initiates or retries deterministic burn v1

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:115-166,179-237` (also `/Users/henriqueklein/gnus/SGNUS/src/account/BurnConfig.cpp:256-304` and `/Users/henriqueklein/gnus/SGNUS/test/src/startup/trust_first_boot_e2e_test.cpp:181-229`)

**Issue:** After the genesis callback commits TPR genesis, the controller only calls `Refresh()`. `Refresh()` loads the new snapshot and moves to `WaitingForInitialBurn`, but it never calls `BurnConfig::OnTrustedPeerGenesisConfirmed()`. There is no production caller of that method anywhere under `src/`. The CLI is not a fallback: `LocalTrustAdmin::ProposeBurn()` delegates to `ProposeBurnCandidate()`, which rejects every proposal while `IsEconomicallyReady()` is false. Consequently, if all honest nodes follow the production path, no node writes the first deterministic burn approval and the network remains economically unavailable indefinitely. Restart is also unable to retry or activate an already stored initial candidate because the pending list is in-memory and `Refresh()` neither lists candidates nor calls the genesis-burn hook. The E2E passes only because line 211 invokes `OnTrustedPeerGenesisConfirmed()` directly from the test.

**Fix:** When `Refresh()` observes a verified `BootstrapOnly` snapshot, have each eligible current peer idempotently call `OnTrustedPeerGenesisConfirmed()`, then call `TryActivateBurnCandidate()` on the returned ID so an already-present quorum is recovered after restart. Treat a non-member local account as an expected no-sign condition, but emit/return real submission or activation failures. Add a production-composition test that performs the ceremony without directly accessing `BurnConfig`, proves the initial candidate appears, reaches peer quorum, and survives a restart between approval persistence and durable activation.

### CR-06 [BLOCKER]: Replicated policy quorum never activates on passive nodes

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:114-176` (also `/Users/henriqueklein/gnus/SGNUS/src/trustedpeer/genesis_tool/LocalTrustAdmin.cpp:35-95` and `/Users/henriqueklein/gnus/SGNUS/test/src/startup/trust_first_boot_e2e_test.cpp:237-249`)

**Issue:** The controller registers candidate callbacks only for `trusted-peer-genesis` and `burn-config`; there is no callback for the `trusted-peer` policy domain. `LocalTrustAdmin` activates a policy only in the process that executes `ProposePolicy()` or `Approve()`. Other nodes can receive all quorum approvals but never call `TryActivatePolicyCandidate()`, so their durable policy remains old. Those nodes then authorize different signers, reject new-policy candidates, and violate the requirement that the first candidate reaching current-policy quorum becomes effective. The E2E hides this omission by submitting the second signature directly and then calling `admin.Approve()` again solely to trigger local activation.

**Fix:** Register and unregister a `trusted-peer` callback alongside the genesis and burn callbacks. It must call `TryActivatePolicyCandidate()` without signing, treat `false` as ordinary pending quorum, emit `TRUST_ACTIVATION_FAILED` only for errors, and refresh the controller after a successful commit. Add a multi-node test where only two operators approve, while a third passive node receives the approvals and independently persists the same policy head without an extra local admin call.

### CR-07 [BLOCKER]: Multi-record trust verification can observe a torn concurrent transition

**File:** `/Users/henriqueklein/gnus/SGNUS/src/trustedpeer/TrustStateStore.cpp:436-656,746-813,816-920`

**Issue:** Writers serialize with `transition_mutex_` and atomically batch each individual transition, but public `LoadAndVerify()` takes no lock and performs many independent RocksDB reads without a database snapshot. A reader can load policy head v1 and its history, pause while a writer commits policy v2 and then burn v2 authorized by policy v2, and resume by loading burn head v2. Its in-memory policy map lacks v2, so lines 636-643 return `INVALID_BURN_PROOF` for fully valid durable state. `TrustStartupController::Refresh()` translates that transient result into `FatalMismatch`/`TRUST_LOCAL_STATE_CORRUPT`. This is a correctness and availability failure under normal callback/admin concurrency, not actual disk corruption.

**Fix:** Give verification one consistent view. Either hold the same mutex for the entire public read or use a RocksDB snapshot/read transaction. Because commit methods already hold `transition_mutex_` and call verification, refactor an internal `LoadAndVerifyUnlocked()` (or snapshot-based helper): public `LoadAndVerify()` acquires the lock, while commit paths call the helper under their existing lock. Add a deterministic barrier test that interleaves a reader between policy-history and burn-head reads while two valid transitions commit; the reader must return either the complete old snapshot or the complete new snapshot, never a corruption error.

## Warnings

### WR-06 [WARNING]: Refresh-time activation failures are still silently discarded

**File:** `/Users/henriqueklein/gnus/SGNUS/src/account/TrustStartupController.cpp:179-237`

**Issue:** Plan 13-14 established `false = valid pending` and `error = actionable failure`, but `Refresh()` discards the result of genesis activation at lines 200-202 and ignores every error from pending burn activation at lines 228-235 before returning success. A commit failure or corrupt candidate encountered during refresh can therefore be represented merely as a waiting state, with no `TRUST_ACTIVATION_FAILED` event and no error returned to startup. Callback-time reporting does not cover candidates already present before callback registration or retries after restart.

**Fix:** Preserve the activation result contract in `Refresh()`: ignore only a typed pending/no-approval outcome, emit `TRUST_ACTIVATION_FAILED` and return actionable activation/store errors, and remove or quarantine invalid pending IDs. Add fault-injection tests that preload candidates before controller construction and force `COMMIT_FAILED` during refresh, asserting both the event and failed refresh result.

---

_Reviewed: 2026-08-13T15:31:27Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: deep_
