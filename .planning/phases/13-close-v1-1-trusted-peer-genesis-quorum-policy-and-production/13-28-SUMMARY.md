---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 28
subsystem: trusted-peer startup refresh dispatch
tags: [bounded-retry, boost-asio, system-executor, weak-owner, tdd]

requires:
  - phase: 13-27
    provides: generation-safe account ownership and immutable trusted-peer signer identity
provides:
  - typed refresh-stage and outcome classification
  - bounded transient discovery retry with exact exponential delays and duplicate coalescing
  - owner-safe asynchronous dispatch independent of controller lifetime
  - autonomous no-write recovery and exact exhaustion diagnostics
affects: [phase-13-verification, plan-13-29, milestone-v1.1-audit, trusted-peer-policy]

tech-stack:
  added: []
  patterns: [weak-owner dispatch state, serialized system-executor strand, classified bounded retry]

key-files:
  created:
    - .planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-28-SUMMARY.md
  modified:
    - src/account/TrustStartupController.hpp
    - src/account/TrustStartupController.cpp
    - src/account/GeniusNode.cpp
    - test/src/startup/trust_first_boot_e2e_test.cpp
    - .planning/STATE.md
    - .planning/ROADMAP.md

key-decisions:
  - "Refresh dispatch state owns serialization, retry timing, cancellation, and diagnostics independently; it locks the weak controller only across one Refresh attempt."
  - "Only policy and burn discovery failures are transient; activation failures remain actionable and controller-scoped candidate suppression is preserved."
  - "One refresh cycle is exactly attempts 1 through 7 with 100/200/400/800/1600/3200ms delays; duplicate requests coalesce and exhaustion returns the dispatcher to idle."

patterns-established:
  - "Owner-safe async dispatch: executor state may outlive the controller, but never retains a strong controller reference across callbacks or timers."
  - "Retry observability: scheduled and exhausted events carry stage, error category/message, attempt, retry count, and delay fields."

requirements-completed: [POLICY-01, TPR-02, BURN-03, TEST-01]

duration: 34min
completed: 2026-08-14
---

# Phase 13 Plan 28: Typed Bounded Refresh Retry and Owner-Safe Dispatch Summary

**Trusted-peer refresh now retries only transient policy/burn discovery failures through an owner-independent serialized dispatcher, with exact bounded backoff, coalescing, typed events, and safe final-owner release from inside dispatched callbacks.**

## Performance

- **Duration:** 34 min
- **Started:** 2026-08-14T21:39:26Z
- **Completed:** 2026-08-14T22:13:18Z
- **Tasks:** 2
- **Files modified:** 4 product/test files plus this summary and tracking metadata

## Accomplishments

- Replaced the raw controller-owned refresh thread, condition variable, and joining destructor with a `boost::asio::system_executor` strand whose state retains only a weak controller reference.
- Added typed refresh stages and success/transient/actionable/fatal dispositions so only policy and burn discovery failures enter retry; candidate activation failures retain existing actionable suppression semantics.
- Implemented one initial attempt plus six retries with exact 100/200/400/800/1600/3200ms delays, in-cycle duplicate coalescing, one terminal exhaustion event, and an idle state with no eighth attempt.
- Added typed `TRUST_REFRESH_RETRY_SCHEDULED` and `TRUST_REFRESH_RETRY_EXHAUSTED` production diagnostics and GeniusNode labels with stage/error/attempt/delay context.
- Proved policy and burn convergence without CRDT writes, administrative activation, direct activation, or manual `Refresh()` inside the marked recovery windows.
- Proved the last external controller owner can be reset from the dispatched retry-event callback, followed by weak expiration and dispatcher drain without self-join, detach, termination, or use-after-free.

## Task Commits

1. **Task 1: RED — expose transient retry, exhaustion, and owner lifetime failures** - `0ecc17e2` (test)
2. **Task 2: GREEN — classify bounded refresh retries and dispatch independently of controller ownership** - `7377dc94` (feat)

## Verification Evidence

### RED gate

The four new cases ran independently before the implementation and produced only their reviewed diagnostics:

```text
TransientPolicyListingRetriesWithoutNewWrite
  FAIL: CR-13 policy transient retry missing
TransientBurnListingRetriesWithoutNewWrite
  FAIL: CR-13 burn transient retry missing
TransientRefreshRetryExhaustionIsCappedCoalescedAndIdle
  FAIL: CR-13 retry exhaustion cap missing
WorkerCallbackCanReleaseLastControllerOwnerSafely
  FAIL: CR-14 callback owner unsafe (child signal 6: self-join)
policy_red_reason=PASS burn_red_reason=PASS exhaustion_red_reason=PASS owner_red_reason=PASS
```

### GREEN focused gate

The exact seven-case plan command passed after a fresh Release link:

```text
[==========] 7 tests from 1 test suite ran. (25786 ms total)
[  PASSED  ] 7 tests.
```

The focused gate includes all four new regressions plus retained-policy reconstruction, passive policy activation, and preloaded activation-error propagation. Deterministic observations prove attempts `1,2,3,4,5,6,7`, delays `100,200,400,800,1600,3200`, six scheduled events, one exhausted event, two coalesced duplicate requests, idle afterward, and no eighth attempt or extra approval write.

### Complete affected binary

```text
[==========] 9 tests from 1 test suite ran. (38021 ms total)
[  PASSED  ] 9 tests.
```

The owner-release case uses re-exec death-test isolation and passes both alone and after the listener-backed suite has initialized asynchronous runtime state.

## Files Created/Modified

- `src/account/TrustStartupController.hpp` - Defines typed refresh stages/dispositions, the deterministic test seam, and opaque owner-independent dispatch state.
- `src/account/TrustStartupController.cpp` - Implements serialized weak-owner dispatch, exact bounded retry/backoff/coalescing, typed events, cancellation, and post-callback drain behavior.
- `src/account/GeniusNode.cpp` - Maps both refresh retry event codes to stable operator-visible labels.
- `test/src/startup/trust_first_boot_e2e_test.cpp` - Adds autonomous policy/burn recovery, exact capped exhaustion, and final-owner callback lifetime regressions.
- `.planning/STATE.md` and `.planning/ROADMAP.md` - Advance only Plan 13-28 while Plan 13-29 remains incomplete.

## Decisions Made

- Dispatch state owns its strand, timer, test observations, and copied diagnostic callback but stores the controller only as `weak_ptr`; one attempt holds a temporary strong owner only while `RefreshClassified` executes.
- Retry policy is semantic rather than error-code broad: policy/burn discovery is transient, policy/burn activation is actionable, and durable/genesis/publication failure is fatal for the current cycle.
- Duplicate callback requests are intentionally absorbed into the active cycle; they do not create a pending eighth attempt or a second cycle after success/exhaustion.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Rechecked dispatcher stop state after an owner-releasing event callback**
- **Found during:** Task 2 focused owner-release run
- **Issue:** A retry-event callback could release the last controller owner and run its destructor, but the independent dispatcher could continue past the callback and arm a retry timer using its pre-callback state.
- **Fix:** Recheck the dispatch state's `stopped` flag immediately after the event callback and finish/drain without arming a timer when destruction occurred.
- **Files modified:** `src/account/TrustStartupController.cpp`, `test/src/startup/trust_first_boot_e2e_test.cpp`
- **Commit:** `7377dc94`

**2. [Rule 3 - Blocking test isolation] Re-executed the owner death-test child after asynchronous tests**
- **Found during:** Task 2 combined focused run
- **Issue:** The default fast death-test fork inherited an already initialized multithreaded `system_executor`, so the child could not schedule its dispatch when the owner case ran after the other listener-backed tests, despite passing alone.
- **Fix:** Use GoogleTest's `threadsafe` death-test style for this case, which re-executes a clean child while preserving the exact production-path lifetime assertions.
- **Files modified:** `test/src/startup/trust_first_boot_e2e_test.cpp`
- **Commit:** `7377dc94`

---

**Total deviations:** 2 auto-fixed correctness/test-isolation issues.
**Impact on plan:** The production fix closes a post-callback lifetime race, and re-exec isolation makes the owner proof stable without weakening its callback, weak-expiry, or drain assertions.

## Issues Encountered

- Listener-backed tests cannot bind inside the default sandbox. All RED and GREEN evidence used approved local listener access and the real GlobalDB/SecureCrdt production path; no fixture was weakened.

## Known Stubs

None introduced. Default-empty callbacks are intentional optional API/test-seam inputs; the unrelated pre-existing `GeniusNode.cpp` TODO is outside the changed hunks and does not block CR-13 or CR-14.

## Threat Flags

None. The dispatcher and retry behavior are the planned CR-13/CR-14 lifetime and availability closure; no endpoint, authentication input, file access pattern, package, schema, topic, or administrative surface was added.

## User Setup Required

None - no package installation, migration, secret, or external configuration is required.

## Next Phase Readiness

- CR-13 and CR-14 now have independent reviewed RED evidence, exact GREEN retry/lifetime evidence, and complete affected-binary coverage.
- Plan 13-29 remains intentionally incomplete for the cumulative exact-name, JUnit, sanitizer-aware, and repeated-lifetime final closure gate.

## Self-Check: PASSED

- Summary and all four declared product/test files exist.
- RED commit `0ecc17e2` and GREEN commit `7377dc94` exist in repository history.
- Exact focused results are 7/7 and the complete `trust_first_boot_e2e_test` binary is 9/9.
- No raw refresh worker, `detach`, or controller-owned joining thread remains in `TrustStartupController`.
- The two protected pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-14*
