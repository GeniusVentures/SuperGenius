---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 21
subsystem: trusted-peer policy activation
tags: [trusted-peer, securecrdt, globaldb, passive-activation, callback-lifecycle, tdd, cpp]

requires:
  - phase: 13-20
    provides: serialized production callback refresh worker and first-burn startup convergence
provides:
  - signer-free receive-side activation of quorum-confirmed trusted-peer policy candidates
  - typed passive policy commit failures with durable-head preservation
  - weak, owner-safe trusted-peer callback registration and queued-worker teardown
affects: [trusted-peer-policy, production-convergence, callback-lifecycle, phase-13-verification]

tech-stack:
  added: []
  patterns: [passive quorum re-derivation, weak callback ownership, durable-commit-before-refresh]

key-files:
  created: []
  modified:
    - src/account/TrustStartupController.cpp
    - test/src/startup/trust_first_boot_e2e_test.cpp

key-decisions:
  - "Only remotely authored trusted-peer callback records enter passive activation; the explicit local admin path retains responsibility for its own synchronous activation result."
  - "Policy candidates share the existing serialized callback worker, and controller teardown joins queued work before owner-safe callback removal."

patterns-established:
  - "Passive policy convergence: callback receipt queues candidate identity, production validation re-derives quorum, durable success reloads state, and pending/error never refresh derived state."
  - "Lifecycle-safe callback teardown: weak capture prevents expired-owner entry, the worker is stopped and joined, then all three owned domains are symmetrically unregistered."

requirements-completed: [POLICY-01, TPR-02, TEST-01]

duration: 25 min
completed: 2026-08-13
---

# Phase 13 Plan 21: Passive Policy Activation Summary

**Production receivers now durably converge on quorum-confirmed trusted-peer policies without signing or a third administrative action, while pending, commit-failure, and teardown outcomes remain explicit and safe.**

## Performance

- **Duration:** 25 min
- **Started:** 2026-08-13T17:47:47Z
- **Completed:** 2026-08-13T18:11:41Z
- **Tasks:** 1 TDD task
- **Files modified:** 2

## Accomplishments

- Registered the `trusted-peer` candidate callback beside genesis and burn callbacks with the same weak controller capture and owner token.
- Re-derived quorum through `TryActivatePolicyCandidate` without any proposal, approval, submission, or signing path; durable success reloads the trusted snapshot, authenticated pending stays quiet, and commit failure emits typed candidate context without advancing state.
- Added a three-node production regression proving identical passive policy-v2 durability after exactly operator A's proposal and operator B's single approval, with zero passive signatures and no passive admin/direct activation hook.
- Proved pending, injected durable-commit failure, callback unregister, post-destruction delivery, and surviving-node progress behavior.

## Task Commits

1. **Task 1 RED: Passive policy activation counterexample** - `f8fceef5` (test)
2. **Task 1 GREEN: Replicated policy activation on passive nodes** - `b0c3eb94` (fix)

## Files Created/Modified

- `src/account/TrustStartupController.cpp` - Queues remote policy candidate receipts, re-derives quorum and commits on the serialized worker, reports typed failures, reloads only after durable success, and unregisters all owned callbacks safely.
- `test/src/startup/trust_first_boot_e2e_test.cpp` - Exercises three production nodes, exactly two operator decisions, pending/error/success branches, zero passive signing, and teardown delivery.

## Decisions Made

- Ignored callback records authored by the controller's own signer for policy activation. `LocalTrustAdmin` already owns the explicit local decision and synchronous activation result; receive-side work is reserved for replicated records and cannot race the admin call into a second activation attempt.
- Reused the Plan 13-20 serialized refresh worker rather than writing through GlobalDB on its callback-delivery thread. Policy candidates are separated by domain from burn candidates and durable policy state is reloaded only after activation returns true.
- Stopped and joined the worker before owner-token callback removal. Weak capture rejects new entry once destruction begins, while joining guarantees queued work cannot outlive controller dependencies.

## TDD Gate Compliance

- **RED (`f8fceef5`):** The focused production regression compiled and failed because both explicit policy approvals propagated while the passive node's durable policy remained at v1; no `trusted-peer` production callback was registered.
- **GREEN (`b0c3eb94`):** The focused two-test filter passed after passive activation and lifecycle handling were implemented, and the complete three-test executable passed unchanged.

## Verification

- Build: PASS - `trust_first_boot_e2e_test` compiled successfully.
- Focused filter: PASS - 2 selected, 2 executed, 2 passed in 16.345 seconds.
- Complete `trust_first_boot_e2e_test`: PASS - 3 selected, 3 executed, 3 passed in 21.970 seconds.
- Registration scan: PASS - `trusted-peer-genesis`, `trusted-peer`, and `burn-config` are registered and owner-safely unregistered.
- Passive authority scan: PASS - the production callback contains no proposal, approval, submission, or signing call; the test constructs no passive admin and invokes no direct policy activation hook.
- Change hygiene: PASS - `git diff --check` reported no whitespace errors and no tracked file was deleted.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Stabilized the injected TrustStateStore fault-hook captures**
- **Found during:** Task 1 GREEN verification
- **Issue:** The test helper's asynchronous commit hook captured a helper-call reference parameter by reference, leaving a dangling reference after the helper returned and crashing when the v3 fault was enabled.
- **Fix:** Captured a stable pointer to each test-lifetime atomic failure flag and the optional attempt counter by value.
- **Files modified:** `test/src/startup/trust_first_boot_e2e_test.cpp`
- **Verification:** LLDB identified the invalid atomic access; the focused and complete binaries subsequently passed without a crash.
- **Committed in:** `b0c3eb94`

---

**Total deviations:** 1 auto-fixed correctness bug
**Impact on plan:** The fix only made the planned asynchronous fault injection lifetime-safe; production scope and acceptance behavior were unchanged.

## Issues Encountered

- Production-composition tests require ephemeral local listener permission in the managed sandbox. The focused and complete commands passed when rerun with the approved listener permission.
- The first GREEN run exposed a synchronous self-authored callback race with `LocalTrustAdmin::Approve`; limiting passive activation to replicated records preserved the explicit admin result and eliminated the duplicate activation attempt.

## Known Stubs

None. No placeholder, mock runtime data, or deferred activation path was introduced.

## User Setup Required

None - no dependency, external service, configuration, transport, topic, or RPC was added.

## Next Phase Readiness

- CR-06 is closed: a passive third production node persists the quorum policy immediately after the two required operator decisions with zero passive signing.
- POLICY-01 and TPR-02 now converge through replicated approvals alone, including observable commit failure and safe destroyed-owner behavior.
- Plan 13-22 can perform final integrated closure without a Plan 13-21 blocker.

## Self-Check: PASSED

- Both modified source/test files and this summary exist.
- RED commit `f8fceef5` precedes GREEN commit `b0c3eb94` in repository history.
- Focused and complete end-to-end verification passed after the final implementation.
- No tracked deletion, package addition, endpoint, authentication path, schema change, known stub, or unplanned trust boundary was introduced.
- Both pre-existing protected untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-13*
