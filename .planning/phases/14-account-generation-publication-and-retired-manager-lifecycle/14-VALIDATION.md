---
phase: 14
slug: account-generation-publication-and-retired-manager-lifecycle
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-08-18
---

# Phase 14 — Validation Strategy

> Per-phase validation contract for account-generation publication and retired-manager lifecycle safety.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest/GoogleMock (project-resolved version) |
| **Config files** | `test/src/multiaccount/CMakeLists.txt`, `test/src/account/CMakeLists.txt` |
| **Build command** | `cmake --build build/OSX/Release --target multi_account_test account_management_test -j8` |
| **Quick run command** | `build/OSX/Release/test_bin/multi_account_test --gtest_filter='MultiAccountTest.AccountGeneration*:MultiAccountTest.RetiredManager*'` |
| **Full suite command** | `ctest --test-dir build/OSX/Release -R '^(multi_account_test|account_management_test)$' --output-on-failure` |
| **Estimated runtime** | ~5 minutes for both complete scoped binaries; focused cases should remain below 60 seconds each |

---

## Sampling Rate

- **After every task commit:** Build the touched target and run the exact new GoogleTest filter.
- **After every plan wave:** Run both complete scoped binaries through CTest.
- **Before `$gsd-verify-work`:** Both binaries, exact case enumeration, five deterministic lifetime repetitions, and target-proven TSan when configured must be green.
- **Max feedback latency:** 60 seconds for a focused case; long network fixtures must use deterministic barriers and report their explicit bound.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 14-W0-01 | 14-02 | 2 | D-01/D-02 | T14-STALE-GEN | Acceptance returns a generation before readiness; ready/failure events carry that generation. | integration | `account_management_test --gtest_filter='AccountManagement.SelectAccountReturnsGenerationBeforeReadyEvent'` | ❌ W0 | ⬜ pending |
| 14-W0-02 | 14-02 | 2 | D-03/D-04 | T14-PARTIAL-PUB | Switching rejects account calls and overlapping selection with `SWITCH_IN_PROGRESS`. | deterministic concurrency | `account_management_test --gtest_filter='AccountManagement.SwitchInProgressRejectsAccountCallsAndOverlap'` | ❌ W0 | ⬜ pending |
| 14-W0-03 | 14-01 | 1 | D-05/D-06 | T14-RETIRED-MUTATE | Admission closure is linearizable: admitted work finishes and losing calls return `MANAGER_RETIRED` without mutation. | deterministic concurrency | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationAdmissionBoundaryIsLinearizable'` | ❌ W0 | ⬜ pending |
| 14-W0-04 | 14-02 | 2 | D-07 | T14-CROSS-GEN | Replacement initialization starts only after the old admitted set is terminal. | deterministic concurrency | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationDrainsBeforeReplacementInitialization'` | ❌ W0 | ⬜ pending |
| 14-W0-05 | 14-14 | 14 | D-08 | T14-DRAIN-DOS | Injected drain timeout fails unavailable without cancelling durable old work or starting replacement. | deterministic timer | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationDrainTimeoutFailsClosedWithoutCancellation'` | ❌ W0 | ⬜ pending |
| 14-W0-06 | 14-14 | 14 | D-09/D-12 | T14-PARTIAL-PUB | Every replacement failure cleans partial owners before one generation-tagged failure event. | parameterized integration | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationInitializationFailureCleansBeforeEvent*'` | ❌ W0 | ⬜ pending |
| 14-W0-07 | 14-02, 14-14 | 2, 14 | D-10/D-11 | T14-STALE-OWNER | No automatic retry or old republish; a new explicit selection is required for recovery. | integration | `account_management_test --gtest_filter='AccountManagement.FailedSwitchRequiresExplicitRecovery:AccountManagement.ConfiguredIdentityDoesNotPublishUnavailableGeneration'` | ❌ W0 | ⬜ pending |
| 14-W0-08 | 14-01, 14-05 | 1, 5 | D-13 | T14-RETIRED-MUTATE | Every retained-manager mutation surface rejects with `MANAGER_RETIRED` and leaves counters/reservations/queue unchanged. | unit/integration | `multi_account_test --gtest_filter='MultiAccountTest.RetiredManagerRejectsEveryMutationEntryPoint'` | ❌ W0 | ⬜ pending |
| 14-W0-09 | 14-01, 14-05 | 1, 5 | D-14/D-15 | T14-MISATTRIBUTION | Retired diagnostics are immutable and accepted terminal outcomes arrive exactly once under the old generation. | deterministic integration | `multi_account_test --gtest_filter='MultiAccountTest.RetiredManagerDiagnosticsAreImmutable:MultiAccountTest.RetiredManagerDeliversAcceptedTerminalOutcomeWithOldGeneration'` | ❌ W0 | ⬜ pending |
| 14-W0-10 | 14-02, 14-13 | 2, 13 | D-16 | T14-STATUS-RACE | Processing status is switching, unavailable, or replacement-ready and never reads the retired owner. | deterministic concurrency | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationProcessingStatusTracksLifecycle'` | ❌ W0 | ⬜ pending |
| 14-W0-11 | 14-02 | 2 | CR-01 | T14-PARTIAL-PUB | Every account-service snapshot contains all ready owners or none; stale completion cannot publish a newer generation. | deterministic concurrency | `multi_account_test --gtest_filter='MultiAccountTest.ConcurrentSelectAccountSnapshotsAndCatchupCallbacksStayGenerationConsistent:MultiAccountTest.AccountGenerationRejectsStaleBlockchainCompletion'` | ⚠️ strengthen + add | ⬜ pending |

---

## Deterministic Barrier Requirements

- After operation admission and before first mutation: selection closes admission while the accepted call remains permitted.
- Before operation admission: selection wins and the call returns `MANAGER_RETIRED` without mutation.
- Before reservation and before enqueue: no retirement interleaving strands nonce/UTXO state or appends rejected work.
- After admission closes and before `SelectAccount()` returns: no post-acceptance old-manager admission succeeds.
- Before old terminal transition and drain-zero notification: replacement initialization remains unstarted.
- Injectable timeout trigger: failure does not depend on wall-clock sleeping.
- After asynchronous completion and before posted initialization: stale generation callbacks cannot advance a newer switch.
- Before ready publication: public account/manager snapshots remain jointly empty and status remains switching.
- After failure cleanup and before event delivery: callback observation sees no partial pending owner.

---

## Wave 0 Requirements

- [ ] Extend `MultiAccountTestAccess` with default-empty admission, reservation, enqueue, drain-zero, timeout, stale-callback, ready-publication, and failure-cleanup hooks.
- [ ] Strengthen `ConcurrentSelectAccountSnapshotsAndCatchupCallbacksStayGenerationConsistent` to require account/manager presence equality; remove its current partial-snapshot skip.
- [ ] Add the deterministic multi-account cases listed above to `test/src/multiaccount/multi_account_sync.cpp`.
- [ ] Add public acceptance, switching, failure, bootstrap-identity, and recovery cases to `test/src/account/account_management_test.cpp`.
- [ ] If a configured TSan build proves both scoped targets are instrumented at compile and link/runtime, run it; otherwise record `sanitizer_status=NOT_RUN`, never PASS.

---

## Manual-Only Verifications

All Phase 14 behaviors have automated verification. No manual-only acceptance criteria are permitted for the concurrency and lifecycle contract.

---

## Validation Sign-Off

- [x] All planned behaviors have an automated test command or explicit Wave 0 dependency.
- [x] Sampling continuity forbids three consecutive tasks without automated verification.
- [x] Wave 0 enumerates every currently missing regression and deterministic hook.
- [x] No watch-mode flags are used.
- [x] Focused feedback target is below 60 seconds; complete network fixtures may use a documented finite bound.
- [x] `nyquist_compliant: true` is set in frontmatter.

**Approval:** pending plan-checker verification
