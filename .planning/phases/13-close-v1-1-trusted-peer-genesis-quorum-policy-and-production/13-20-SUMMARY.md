---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 20
subsystem: trusted-peer startup
tags: [trusted-peer, burn-config, securecrdt, globaldb, restart, tdd, cpp]

requires:
  - phase: 13-14
    provides: production first-boot composition and trusted-peer genesis startup state machine
  - phase: 13-17
    provides: typed activation-result contract and observable trust activation failures
provides:
  - production-owned deterministic burn-v1/value-100 approval by eligible current peers
  - restart-safe CRDT rediscovery and passive activation of retained initial-burn candidates
  - actionable genesis and burn activation failure propagation from startup Refresh
affects: [trusted-peer-genesis, burn-config, startup-readiness, restart-recovery]

tech-stack:
  added: []
  patterns: [verified-member automatic signing boundary, serialized callback refresh worker, authoritative CRDT rediscovery]

key-files:
  created: []
  modified:
    - src/account/TrustStartupController.hpp
    - src/account/TrustStartupController.cpp
    - test/src/startup/trust_first_boot_e2e_test.cpp

key-decisions:
  - "Automatic signing is limited to a verified current member and the exact deterministic BootstrapOnly burn-v1/value-100 candidate."
  - "SecureCrdt callbacks queue serialized worker Refresh operations so activation writes never reenter the GlobalDB callback thread."
  - "Failed candidate IDs are suppressed only for the current controller while their authoritative CRDT records remain available to a reconstructed controller."

patterns-established:
  - "Startup-owned initial burn: verified member initiation and passive candidate activation share one idempotent Refresh path."
  - "Failure-safe callback delivery: callbacks schedule serialized work; Refresh returns and emits actionable typed activation errors."

requirements-completed: [BOOT-04, BURN-03, TEST-01]

duration: 22 min
completed: 2026-08-13
---

# Phase 13 Plan 20: Production Initial-Burn Startup Summary

**Production startup now contributes and recovers deterministic burn-v1/value-100 approvals for eligible peers, passively activates retained quorum after restart, and exposes every actionable genesis or burn activation failure.**

## Performance

- **Duration:** 22 min
- **Started:** 2026-08-13T17:12:24Z
- **Completed:** 2026-08-13T17:34:06Z
- **Tasks:** 2 TDD tasks
- **Files modified:** 3

## Accomplishments

- Moved initial-burn progress into production `TrustStartupController::Refresh`, with verified current-policy membership and exact burn-v1/value-100 boundaries around automatic local approval.
- Added deduplicated pending-candidate discovery and passive activation so a controller reconstructed over retained GlobalDB/SecureCrdt state reaches `ConfirmedReady` without resubmission or an operator/admin nudge.
- Preserved non-member and below-quorum states as quiet waiting outcomes while returning and emitting candidate identity, version, hash, and typed context for actionable genesis and burn activation failures.
- Added production-composition regressions covering two eligible peers, a signer-free non-member, failed durable burn commit, restart recovery, preloaded genesis/burn failures, and genuine pending controls without direct activation hooks.

## Task Commits

1. **Task 1 RED: Production initial-burn recovery counterexample** - `3ca867bc` (test)
2. **Task 1 GREEN: Production-owned initial-burn startup** - `243c77b8` (fix)
3. **Task 2 RED: Preloaded Refresh failure counterexamples** - `fcde40c4` (test)
4. **Task 2 GREEN: Actionable Refresh activation failure propagation** - `5f5b7dbf` (fix)
5. **Task 2 test hardening: Thread-safe asynchronous event assertions** - `cadf2027` (test)

## Files Created/Modified

- `src/account/TrustStartupController.hpp` - Retains local signer eligibility, failed-candidate suppression, and serialized callback-refresh worker state.
- `src/account/TrustStartupController.cpp` - Initiates exact initial burn, rediscovers and activates retained candidates, queues callback refreshes, and propagates typed activation failures.
- `test/src/startup/trust_first_boot_e2e_test.cpp` - Exercises production composition, restart recovery, no-sign/pending controls, and preloaded genesis/burn failure contracts.

## Decisions Made

- Required a verified `BootstrapOnly` snapshot plus exact membership in the verified current policy before automatic local signing; membership absence remains an expected signer-free branch, and neither policy nor later burn successors are auto-signed.
- Kept CRDT storage authoritative across faults and restarts. The controller removes a failed ID from its in-process retry set to prevent a tight loop, but reconstruction rediscovers the retained candidate and can retry after the injected fault is removed.
- Serialized callback-driven `Refresh` work on a controller-owned worker because activation can write through GlobalDB; callback-thread reentrancy would otherwise deadlock or stall delivery.

## TDD Gate Compliance

- **Task 1 RED (`3ca867bc`):** The focused production-composition test failed with zero initial-burn approvals before startup owned initiation.
- **Task 1 GREEN (`243c77b8`):** The focused production recovery test passed after verified-member initiation, candidate rediscovery, and callback-safe activation were implemented.
- **Task 2 RED (`fcde40c4`):** The preloaded failure regression failed because repeated `Refresh` retried the same actionable candidate in-process instead of preserving the suppression/error contract.
- **Task 2 GREEN (`5f5b7dbf`):** Preloaded genesis and burn failures were emitted and returned, genuine pending/no-sign remained quiet, and reconstruction retried from authoritative CRDT state.
- **Test hardening (`cadf2027`):** Atomic event counters removed an asynchronous assertion data race without changing production behavior.

## Verification

- Task 1 focused command: PASS - 1 selected, 1 executed, 1 passed.
- Task 2 combined focused command after final test hardening: PASS - 2 selected, 2 executed, 2 passed in 12.713 seconds.
- Complete `trust_first_boot_e2e_test`: PASS - 2 selected, 2 executed, 2 passed in 12.199 seconds.
- Direct-hook scan: PASS - no test call to `OnTrustedPeerGenesisConfirmed` or `TryActivateBurnCandidate` remains.
- Production-link scan: PASS - `TrustStartupController.cpp` contains `OnTrustedPeerGenesisConfirmed`, `ListPendingBurnCandidates`, and `TryActivateBurnCandidate` links.
- Build target: PASS - `trust_first_boot_e2e_test` compiled successfully after all changes.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Deferred callback Refresh to a serialized controller worker**
- **Found during:** Task 1 (production initial-burn recovery)
- **Issue:** Calling `Refresh` directly from a SecureCrdt/GlobalDB callback could perform another CRDT write on the callback-delivery thread, causing reentrant delivery to hang.
- **Fix:** Callback handlers now queue one serialized worker refresh; controller teardown unregisters callbacks, signals the worker, and joins it safely.
- **Files modified:** `src/account/TrustStartupController.hpp`, `src/account/TrustStartupController.cpp`
- **Verification:** Focused production recovery and full end-to-end tests complete with bounded forward progress.
- **Committed in:** `243c77b8`

**2. [Rule 1 - Bug] Made asynchronous activation event assertions race-free**
- **Found during:** Task 2 (preloaded Refresh activation failures)
- **Issue:** The regression read a callback-written event vector without synchronization after callback work moved to the controller worker.
- **Fix:** Replaced unsynchronized vector-count reads with atomic event counters while retaining mutex-protected context capture.
- **Files modified:** `test/src/startup/trust_first_boot_e2e_test.cpp`
- **Verification:** The combined focused filter and complete target pass repeatedly after the change.
- **Committed in:** `cadf2027`

---

**Total deviations:** 2 auto-fixed (1 blocking issue, 1 correctness bug)
**Impact on plan:** Both fixes were required for callback-safe production progress and deterministic test evidence; automatic signing and activation scope remain exactly as planned.

## Issues Encountered

- The localhost production-composition tests require ephemeral listener permission in the managed sandbox; rerunning the same test binaries with the approved listener permission succeeded. No code or dependency workaround was needed.

## Known Stubs

None. Empty callback defaults in the controller interface are intentional optional hooks, not runtime placeholders.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- CR-05 is closed with production-only initiation, retained-approval restart recovery, and signer-free non-member behavior.
- WR-06 is closed for preloaded and callback-driven genesis/burn activation outcomes; only genuine pending and no-sign branches remain quiet.
- Plan 13-21 may proceed with no Plan 13-20 blocker.

## Self-Check: PASSED

- All three modified source/test files and this summary exist.
- Task 1 RED `3ca867bc` precedes GREEN `243c77b8`; Task 2 RED `fcde40c4` precedes GREEN `5f5b7dbf` in repository history.
- Focused and complete end-to-end verification passed after the final test-hardening commit.
- No tracked deletion, package addition, endpoint, authentication path, schema change, known stub, or unplanned trust boundary was introduced.
- Both pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-13*
