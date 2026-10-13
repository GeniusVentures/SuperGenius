---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: "07"
subsystem: trust-startup
tags: [genesis, securecrdt, globaldb, restart, rollback, tamper, cpp]

requires:
  - phase: 13-02
    provides: verified durable trust snapshots and rollback-safe successor commits
  - phase: 13-05
    provides: production trusted-peer and burn activation under durable policy
  - phase: 13-06
    provides: reusable production GlobalDB networking composition
  - phase: 13-09
    provides: reviewed one-shot genesis ceremony and protected-key lifecycle
provides:
  - restricted first-boot trust state machine with live networking and fail-closed economics
  - production-network genesis ceremony transition through durable TPR and BurnConfig v1 confirmation
  - persisted restart authority with structured config, network, corruption, rollback, missing-state, and fork diagnostics
affects: [13-08, 13-10, GeniusNode, TrustStateStore, startup-security]

tech-stack:
  added: []
  patterns:
    - durable trust state gates account transaction-service initialization
    - persisted canonical authority wins over mutable diagnostic configuration
    - replicated rollback and fork observations alert without replacing last-known-good state

key-files:
  created:
    - src/account/TrustStartupController.hpp
    - src/account/TrustStartupController.cpp
    - test/src/startup/trust_first_boot_e2e_test.cpp
    - test/src/startup/trust_restart_test.cpp
    - test/src/startup/trust_tamper_e2e_test.cpp
  modified:
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - src/account/CMakeLists.txt
    - src/crdt/globaldb/GlobalDbNetworkComposition.hpp
    - src/crdt/globaldb/GlobalDbNetworkComposition.cpp
    - src/trustedpeer/TrustStateStore.hpp
    - src/trustedpeer/TrustStateStore.cpp
    - src/trustedpeer/TrustedPeerRegistry.hpp
    - src/trustedpeer/TrustedPeerRegistry.cpp
    - test/src/startup/CMakeLists.txt

key-decisions:
  - "TrustStartupController owns the fresh, initial-burn, ready, and fatal startup states and advances only from independently verified durable records."
  - "Mutable trust fields are diagnostic after confirmation: canonical peer ordering avoids false conflicts, real conflicts alert, and network mismatch remains fatal."
  - "The first-boot gate connects independent production GlobalDB compositions and uses the reviewed GenesisCeremony path before protected-key deletion."

patterns-established:
  - "Networking and SecureCrdt remain alive in waiting states, while successor approval and BurnConfig-dependent economics return not-ready state."
  - "Remote reviewed-genesis activation binds the exact candidate core, bootstrapper signature, manifest fingerprint, and durable authorization bytes."

requirements-completed: [BOOT-03, BOOT-04, POLICY-01, SCRDT-04, TPR-01, BURN-01, BURN-03, TEST-01]

duration: 12min
completed: 2026-08-12
---

# Phase 13 Plan 07: Restricted Trust Startup and Persisted Restart Authority Summary

**GeniusNode now keeps production GlobalDB synchronization alive while trust is unconfirmed, enables economics only after durable TPR and BurnConfig v1 quorum, and preserves verified last-known-good authority across restart, tamper, rollback, and fork conditions.**

## Performance

- **Duration:** 12 min resumed execution (prior task work preserved)
- **Started:** 2026-08-12T16:50:20Z
- **Completed:** 2026-08-12T17:01:55Z
- **Tasks:** 3
- **Files modified:** 15

## Accomplishments

- Added a node-scoped startup controller and observable GeniusNode states for fresh trust wait, initial burn wait, confirmed readiness, and fatal trust mismatch without blocking the I/O context.
- Proved the reviewed ceremony across two independent production `GlobalDbNetworkComposition` instances on the real SecureCrdt topic, including identical durable fingerprint publication and confirmation-gated 0600 key-file removal.
- Restored persisted policy and burn heads with mutable trust configuration omitted or conflicting, while network mismatch and corrupt local state fail closed.
- Kept durable last-known-good state unchanged when replicated state is missing, older, or a same-version fork, with exact structured event codes.
- Completed platform whole-archive/force-load and CTest working-directory integration for all startup targets.

## Task Commits

Each task was committed atomically, preserving the prior executor's completed RED/GREEN work:

1. **Task 1 RED: First-boot trust controller contract** - `df13ed63` (test)
2. **Task 1 GREEN: Restricted trust startup states** - `4d5e9775` (feat)
3. **Task 2 RED: Restart and tamper diagnostic contracts** - `9a8a43ab` (test)
4. **Task 2 GREEN: Persisted authority and fail-closed tamper behavior** - `3c11549c` (feat)
5. **Task 2 fix: Canonical restart peer diagnostics** - `6f8beb83` (fix)
6. **Task 1 security completion: Production genesis network path** - `d2295d74` (test)
7. **Task 3: Startup target wiring and gate** - `081f02a3` (chore)

## Files Created/Modified

- `src/account/TrustStartupController.hpp/.cpp` - Explicit durable trust startup states, readiness gates, config comparison, remote snapshot observation, and structured events.
- `src/account/GeniusNode.hpp/.cpp` - Node-visible trust states and startup dispatch before TransactionManager construction.
- `src/trustedpeer/TrustedPeerRegistry.hpp/.cpp` - Exact reviewed-genesis candidate activation from replicated approvals.
- `src/trustedpeer/TrustStateStore.hpp/.cpp` - Candidate-core authorization-byte verification for remote genesis while retaining legacy manifest-byte compatibility.
- `src/crdt/globaldb/GlobalDbNetworkComposition.hpp/.cpp` - Read-only running interface address used to bootstrap a separate production composition.
- `test/src/startup/trust_first_boot_e2e_test.cpp` - Real two-composition ceremony, protected-key cleanup, waiting-state networking, and BurnConfig v1 readiness proof.
- `test/src/startup/trust_restart_test.cpp` - Omitted config, canonical peer ordering, per-field conflicts, and fatal network mismatch proof.
- `test/src/startup/trust_tamper_e2e_test.cpp` - Missing/older/fork replicated state and corrupt-local-store fail-closed proof.
- `src/account/CMakeLists.txt`, `test/src/startup/CMakeLists.txt` - Production source, exact test target, platform force-load, and test working-directory wiring.

## Decisions Made

- Kept startup transitions asynchronous and observable: waiting states leave network/CRDT ownership intact but do not construct economic transaction services.
- Compared configured peers canonically because ordering is not authority; only actual member-set changes produce `TRUST_CONFIG_CONFLICT`.
- Exposed only the running composition's public interface address so the E2E can use the same bootstrap mechanism as production rather than injecting CRDT writes between stores.

## TDD Gate Compliance

- Task 1 has RED `df13ed63` before GREEN `4d5e9775`; the final production-path completion remains test-only because it strengthens evidence without changing trust semantics.
- Task 2 has RED `9a8a43ab` before GREEN `3c11549c`; the canonical-order regression was added with its bug fix in `6f8beb83`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Bind remotely replicated genesis to exact candidate authorization bytes**
- **Found during:** Task 1
- **Issue:** The startup callback needed to activate a reviewed genesis received over CRDT, but the existing store accepted only the ceremony's direct manifest-byte proof path.
- **Fix:** Added exact candidate-ID/core/bootstrapper validation and persisted the authorization bytes covered by the replicated signature, while retaining strict compatibility for existing manifest-byte records.
- **Files modified:** `src/trustedpeer/TrustedPeerRegistry.hpp`, `src/trustedpeer/TrustedPeerRegistry.cpp`, `src/trustedpeer/TrustStateStore.hpp`, `src/trustedpeer/TrustStateStore.cpp`
- **Verification:** Real two-composition first-boot E2E and `trust_state_store_test` pass.
- **Committed in:** `4d5e9775`

**2. [Rule 1 - Bug] Avoid false trust conflicts from peer ordering**
- **Found during:** Task 2 verification
- **Issue:** Persisted peers are canonicalized, but diagnostic configuration could contain the identical set in a different order and incorrectly emit `TRUST_CONFIG_CONFLICT`.
- **Fix:** Canonically sorted configured peers before comparison and added an explicit reordered-peer restart test.
- **Files modified:** `src/account/TrustStartupController.cpp`, `test/src/startup/trust_restart_test.cpp`
- **Verification:** `trust_restart_test` passes 4/4, including unchanged durable heads and no conflict event for reordered peers.
- **Committed in:** `6f8beb83`

**3. [Rule 2 - Missing Critical] Replace the local-only first-boot fixture with the required production ceremony path**
- **Found during:** Resumed acceptance audit after Task 2
- **Issue:** The initial test directly submitted approvals to one test GlobalDB and did not prove separate production networking, `GenesisCeremony`, or success-only key removal.
- **Fix:** Connected two independent `GlobalDbNetworkComposition` instances using the running interface address, invoked `GenesisCeremony` with a protected 0600 key file, and verified remote durable activation plus BurnConfig readiness.
- **Files modified:** `src/crdt/globaldb/GlobalDbNetworkComposition.hpp`, `src/crdt/globaldb/GlobalDbNetworkComposition.cpp`, `test/src/startup/trust_first_boot_e2e_test.cpp`
- **Verification:** `trust_first_boot_e2e_test` passes over the actual libp2p/GlobalDB/SecureCrdt transport.
- **Committed in:** `d2295d74`

**4. [Rule 3 - Blocking] Run all startup tests from the configured test binary directory**
- **Found during:** Task 3
- **Issue:** Only `startup_wiring_test` inherited the working directory containing the configured runtime file; the new startup targets needed the same deterministic CTest environment.
- **Fix:** Applied the binary working directory to all three new trust targets without changing the existing platform force-load graph.
- **Files modified:** `test/src/startup/CMakeLists.txt`
- **Verification:** Focused startup CTest gate passes 5/5 with all exact targets discovered.
- **Committed in:** `081f02a3`

---

**Total deviations:** 4 auto-fixed (1 bug, 2 missing critical functionality, 1 blocking issue).
**Impact on plan:** The fixes complete the required production trust boundary and deterministic test environment without adding a package, RPC, administration endpoint, or alternate transport.

## Issues Encountered

- Network-backed startup tests require permission to open ephemeral local libp2p listeners; all required gates passed in the approved listener environment.
- The first production-composition test run exposed the test process's required libp2p logging initialization; the established test logger initializer is now invoked before either composition starts.

## Verification

- `cmake --build build/OSX/Release --target trust_first_boot_e2e_test genius_node -j8` - PASS.
- `trust_first_boot_e2e_test` - PASS (1/1), including two real compositions, durable TPR transition, protected-key deletion, BurnConfig v1/value 100, and READY.
- `trust_restart_test` - PASS (4/4), including omitted config, reordered peers, each trust-field conflict, and fatal network mismatch.
- `trust_tamper_e2e_test` - PASS (2/2), including missing/old/fork replication and corrupt local state.
- Focused startup CTest gate - PASS (5/5: startup wiring, first boot, restart, tamper, and node startup).
- Plan-wide `genesis_manifest|trust_state_store|trust_first_boot|trust_restart|trust_tamper|startup` gate - PASS (7/7).
- `rg -n "sleep_for|GTEST_SKIP|DISABLED_" test/src/startup/trust_*` - PASS with zero matches.

## Known Stubs

None. Default-empty callbacks in the controller API are intentional optional observers, not data flowing to runtime trust or UI output. Pre-existing sleeps/TODOs elsewhere in `GeniusNode.cpp` were not introduced or modified by this plan.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- Plan 13-08 can reuse the node-scoped confirmed burn provider and startup readiness gate across account selection.
- Plan 13-10 can document the exact ceremony, restart-conflict, and software rollback boundary now proven by executable tests.
- No blocker remains from Plan 13-07.

## Self-Check: PASSED

- All five created controller/test files and all ten modified integration files exist.
- All seven Plan 13-07 task/fix commits exist in repository history in valid RED/GREEN order.
- Every task acceptance command and the overall plan verification gate exits 0.
- Stub/bypass scan found no plan-introduced stub, skip, disabled test, or fixed sleep.
- Threat-surface scan found only the planned startup-config, replicated-CRDT, and protected local-key boundaries; no unplanned endpoint, auth path, package, schema, or remote-control surface was introduced.
- No tracked files were deleted, and both unrelated pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-12*
