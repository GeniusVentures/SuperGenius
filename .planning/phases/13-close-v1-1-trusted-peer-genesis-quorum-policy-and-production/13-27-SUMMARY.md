---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 27
subsystem: account lifecycle and trusted-peer signing
tags: [generation-snapshot, account-switch, callback-lifetime, immutable-signer, tdd]

requires:
  - phase: 13-25
    provides: serialized lifecycle transitions and single-manager restart ownership
  - phase: 13-26
    provides: exact prior security closure gate through T13-G25
provides:
  - coherent generation-owned account and transaction-manager publication
  - stale catch-up and bridge callback rejection across account switches
  - immutable node-scoped trusted-peer signer address/key binding
  - exact before-readiness, after-readiness, and concurrent-switch regressions
affects: [phase-13-verification, milestone-v1.1-audit, account-selection, trusted-peer-policy]

tech-stack:
  added: []
  patterns: [unpublish-drain-publish, strong-owner generation snapshot, immutable signer pair]

key-files:
  created:
    - .planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-27-SUMMARY.md
  modified:
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - test/src/multiaccount/multi_account_sync.cpp
    - test/src/multiaccount/policy_lifetime_multi_account_test.cpp
    - .planning/STATE.md
    - .planning/ROADMAP.md

key-decisions:
  - "Account and TransactionManager owners are copied only as one lifecycle-mutex-protected generation snapshot; a switching generation is unavailable."
  - "SelectAccount invalidates publication before draining watcher/services outside the lifecycle lock, then publishes one complete replacement pair."
  - "Trusted-peer approval identity is one immutable node-scoped address/signing authority captured together for the controller lifetime."

patterns-established:
  - "Async ownership gate: capture strong owners plus generation, then recheck the complete tuple under lifecycle_mutex_ immediately before side effects."
  - "Identity pinning: labels and signing callbacks originate from the same immutable owner, never from mutable selected-account state."

requirements-completed: [TPR-02, BURN-02, BURN-03, TEST-01]

duration: 36min
completed: 2026-08-14
---

# Phase 13 Plan 27: Generation-Safe Account Lifetime and Immutable Trust Signer Summary

**Account selection now unpublishes and drains stale account-bound work before atomically publishing a coherent replacement generation, while trusted-peer approvals remain bound to one immutable node authority across every transaction-account switch.**

## Performance

- **Duration:** 36 min
- **Started:** 2026-08-14T20:58:19Z
- **Completed:** 2026-08-14T21:34:39Z
- **Tasks:** 2
- **Files modified:** 4 product/test files plus this summary and tracking metadata

## Accomplishments

- Added `AccountServiceSnapshot` and a monotonic account-service generation under the existing recursive lifecycle mutex, returning typed manager-unavailable behavior while a switch is in progress.
- Changed `SelectAccount` to invalidate publication, stop/join the old catch-up watcher and account-bound services outside the lifecycle lock, configure the replacement, and publish one complete account/manager generation.
- Routed public account/transaction reads and catch-up, bridge-init, processing, and manager callback consumers through coherent snapshots with pre-side-effect generation checks.
- Added immutable `NodeTrustSigner` ownership so the trusted-peer signer label and signing authority are captured together once and never resolved through the selected transaction account.
- Added exact concurrent CR-11 and before/after-readiness CR-12 regressions, including canonical approval signature verification under the pinned identity and rejection under the replacement identity.

## Task Commits

1. **Task 1: RED — expose concurrent ownership and mutable-signer failures** - `c5b2d1fe` (test)
2. **Task 2: GREEN — publish coherent account generations and pin the trust signer pair** - `1482114d` (feat)

## Verification Evidence

### RED gate

Each new case ran as its own process and produced one reviewed XML failure before production changed:

```text
MultiAccountTest.ConcurrentSelectAccountSnapshotsAndCatchupCallbacksStayGenerationConsistent
  FAIL: CR-11 generation mismatch
PolicyLifetimeMultiAccountTest.ActiveTrustSignerSurvivesAccountSwitchBeforeInitialBurnReadiness
  FAIL: CR-12 pinned signer mismatch
PolicyLifetimeMultiAccountTest.ActiveTrustSignerSurvivesAccountSwitchAfterReadiness
  FAIL: CR-12 pinned signer mismatch
cr11_red_reason=PASS cr12_before_red_reason=PASS cr12_after_red_reason=PASS
```

### GREEN build and focused gate

The freshly linked Release targets built successfully. The exact focused gate passed:

```text
multi_account_test focused: 2 tests, 2 passed
policy_lifetime_multi_account_test focused: 3 tests, 3 passed
```

The focused names included all three new cases, the historical single-manager ownership regression, and the passive burn-successor lifetime regression. Both CR-12 cases reached their real approval paths: burn-v1 became durable at 100 before readiness, and the exact successor became durable after readiness while the replacement manager retained the confirmed provider.

### Complete affected binaries

```text
multi_account_test: 6 tests, 6 passed (1 unrelated pre-existing disabled test)
policy_lifetime_multi_account_test: 3 tests, 3 passed
```

No build tree contained target-specific `-fsanitize=thread` instrumentation for `multi_account_test`; the optional exact TSan execution is therefore `NOT_RUN`, not PASS. Plan 13-29 retains mechanical sanitizer accounting.

## Files Created/Modified

- `src/account/GeniusNode.hpp` - Defines the coherent account-service snapshot, generation state, callback generation, and immutable trust-signer owner.
- `src/account/GeniusNode.cpp` - Implements snapshot consumers, unpublish/drain/publish switching, stale callback rejection, and pinned trust-controller signing.
- `test/src/multiaccount/multi_account_sync.cpp` - Adds the barrier-driven concurrent selection/read/catch-up regression and generation observations.
- `test/src/multiaccount/policy_lifetime_multi_account_test.cpp` - Adds active-member signer regressions before and after readiness with exact canonical signature checks.
- `.planning/STATE.md` and `.planning/ROADMAP.md` - Advance only Plan 13-27 while Plans 13-28 and 13-29 remain incomplete.

## Decisions Made

- A snapshot is valid only when its account owner, manager owner, and generation all still match the published tuple; generation equality alone is insufficient.
- Blocking watcher/service drains happen outside `lifecycle_mutex_`, with the switching flag preventing public or asynchronous consumers from observing partial teardown.
- The trust signer retains its original account signing capability even after that account is deconfigured as a transaction service; account selection never migrates trust identity.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Published catch-up callback ownership with the complete manager generation**
- **Found during:** Task 2 focused CR-11 run
- **Issue:** A reader could observe the newly published manager generation before watcher startup updated the callback-owner diagnostic, briefly yielding generation versus callback-owner `0`.
- **Fix:** Publish the callback-owner generation at the same complete account/manager publication point; watcher startup retains the same generation.
- **Files modified:** `src/account/GeniusNode.cpp`
- **Commit:** `1482114d`

**2. [Rule 3 - Blocking test synchronization] Waited for exact successor replication before operator-B approval**
- **Found during:** Task 2 focused CR-12 after-readiness run
- **Issue:** The new test attempted operator-B approval before B had received A's exact successor candidate, so the production approval correctly returned unavailable.
- **Fix:** Added the same explicit `ReadCandidateApprovals` replication barrier used by existing policy tests, without sleeps as a correctness condition or weaker assertions.
- **Files modified:** `test/src/multiaccount/policy_lifetime_multi_account_test.cpp`
- **Commit:** `1482114d`

---

**Total deviations:** 2 auto-fixed correctness/synchronization issues.
**Impact on plan:** Both fixes strengthen the intended generation and real-network approval evidence; no assertion, quorum rule, or production path was weakened.

## Issues Encountered

- Listener-backed tests cannot bind inside the default sandbox. All RED and GREEN evidence used the real network fixtures with approved local listener access; no mocks replaced production paths.

## Known Stubs

None introduced. Pre-existing TODOs outside the changed test/production hunks are unrelated to this plan and do not block CR-11 or CR-12.

## Threat Flags

None. The changed account-selection and trusted-peer signing trust boundaries are exactly the planned T13-G26, T13-G27, and T13-G28 mitigations; no new endpoint, key input, topic, package, schema, or administrative surface was added.

## User Setup Required

None - no package installation, key migration, or external configuration is required.

## Next Phase Readiness

- CR-11 and CR-12 now have exact RED/GREEN production-path evidence and complete affected-binary coverage.
- Plans 13-28 and 13-29 remain intentionally incomplete for the remaining closure work and cumulative mechanical gate.

## Self-Check: PASSED

- Summary and all four declared product/test files exist.
- RED commit `c5b2d1fe` and GREEN commit `1482114d` exist in repository history.
- Exact focused results are 2/2 plus 3/3, and complete affected binaries are 6/6 plus 3/3.
- TSan absence is recorded honestly as `NOT_RUN`.
- The two protected pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-14*
