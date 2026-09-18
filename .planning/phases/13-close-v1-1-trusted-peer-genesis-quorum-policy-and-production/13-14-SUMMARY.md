---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 14
subsystem: trust startup and local administration
tags: [trusted-peer, burn-config, quorum, startup, diagnostics, cpp]

requires:
  - phase: 13-13
    provides: durable initial-burn sequencing and BootstrapOnly versus PeerQuorum classification
provides:
  - successor authorization only in ConfirmedReady/economically-ready state
  - pending-versus-failed activation semantics for policy and burn candidates
  - structured TRUST_ACTIVATION_FAILED startup diagnostics
affects: [13-15, trust-startup, local-trust-admin, production diagnostics]

tech-stack:
  added: []
  patterns: [readiness preflight before signing, false-means-pending activation, structured async failure events]

key-files:
  created: []
  modified:
    - src/account/TrustStartupController.hpp
    - src/account/TrustStartupController.cpp
    - src/account/BurnConfig.cpp
    - src/account/GeniusNode.cpp
    - src/trustedpeer/TrustedPeerRegistry.cpp
    - src/trustedpeer/genesis_tool/LocalTrustAdmin.cpp
    - test/src/startup/trust_first_boot_e2e_test.cpp
    - test/src/trustedpeer/trust_genesis_tool_test.cpp

key-decisions:
  - "Policy successor authorization is available only in ConfirmedReady, and local policy signing preflights BurnConfig economic readiness."
  - "TryActivatePolicyCandidate and TryActivateBurnCandidate return false only for authenticated below-quorum candidates; validation, link, corruption, and commit failures remain errors."
  - "Startup activation failures carry the candidate domain, version, content hash, and typed error through TRUST_ACTIVATION_FAILED."

patterns-established:
  - "No-write readiness guard: reject forbidden policy operations before adding a local signature."
  - "Activation result contract: true means durable activation, false means valid pending quorum, error means actionable failure."

requirements-completed: [BOOT-04, BURN-03, TEST-01]

duration: 13min
completed: 2026-08-13
---

# Phase 13 Plan 14: Runtime Initial-Burn Ordering Summary

**Runtime and local-admin policy operations now remain locked until peer-confirmed burn readiness, while burn-v1 recovery stays usable and real activation failures are observable.**

## Performance

- **Duration:** 13 min
- **Started:** 2026-08-13T14:40:31Z
- **Completed:** 2026-08-13T14:53:25Z
- **Tasks:** 1 TDD task
- **Files modified:** 8

## Accomplishments

- Restricted `CanApproveSuccessors()` and local policy proposal/approval to fully confirmed economic readiness without adding a gate to burn operations.
- Made activation results distinguish a valid below-quorum pending candidate from wrong-head, corruption/proof, and durable commit failures.
- Propagated local activation failures and emitted structured `TRUST_ACTIVATION_FAILED` events from asynchronous startup callbacks before refresh.
- Proved a policy-v2 attempt before initial burn changes no durable head, burn-v1 still reaches `ConfirmedReady`, and policy advancement then succeeds.
- Preserved receive/list no-auto-sign behavior and exact content-addressed approval targeting under the new prerequisite.

## Task Commits

1. **Task 1 RED: Runtime initial-burn ordering counterexamples** - `58f3adac` (test)
2. **Task 1 GREEN: Runtime/admin readiness gates and failure visibility** - `c7aa11a1` (fix)
3. **Compatibility: Existing admin success fixtures establish burn readiness** - `631f48ed` (test)

## Files Created/Modified

- `src/account/TrustStartupController.hpp` - Added the activation-failure event contract.
- `src/account/TrustStartupController.cpp` - Restricted successor authorization and emitted structured callback failures.
- `src/account/BurnConfig.cpp` - Returned `false` for authenticated burn candidates below quorum.
- `src/account/GeniusNode.cpp` - Labeled the new production diagnostic as `TRUST_ACTIVATION_FAILED`.
- `src/trustedpeer/TrustedPeerRegistry.cpp` - Returned `false` for authenticated policy candidates below quorum.
- `src/trustedpeer/genesis_tool/LocalTrustAdmin.cpp` - Added economic-readiness preflights and propagated activation errors.
- `test/src/startup/trust_first_boot_e2e_test.cpp` - Added no-stranding and callback failure-visibility production-path regressions.
- `test/src/trustedpeer/trust_genesis_tool_test.cpp` - Added initial-burn gate, burn recovery, pending, commit-failure, list/no-sign, and exact-target coverage.

## Decisions Made

- Used the existing activation APIs' boolean channel for the expected pending state instead of translating proof errors in each caller. This keeps corrupt or otherwise invalid quorum attempts observable.
- Applied readiness checks before local signing so rejected policy operations produce no candidate write and no signature side effect.
- Kept burn-v1 approval callable while `BootstrapOnly`; only policy operations receive the economic-readiness preflight.

## TDD Gate Compliance

- **RED (`58f3adac`):** focused tests compiled and failed because `WaitingForInitialBurn` authorized successors, local admin signed policy candidates, and `COMMIT_FAILED` was discarded.
- **GREEN (`c7aa11a1`):** the same ordering, burn-recovery, pending, local failure, and callback failure tests passed.
- **Compatibility (`631f48ed`):** existing policy-success tests now establish peer-confirmed burn first while retaining their original no-auto-sign and exact-candidate assertions.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical functionality] Labeled the new event in the production diagnostic consumer**
- **Found during:** Task 1 GREEN build
- **Issue:** Adding `TRUST_ACTIVATION_FAILED` without updating the `GeniusNode` event switch produced an exhaustive-switch warning and would log the event as `TRUST_CONFIG_CONFLICT`.
- **Fix:** Added the explicit production diagnostic label.
- **Files modified:** `src/account/GeniusNode.cpp`
- **Verification:** Focused rebuild completed without the enum warning; the callback regression received the distinct event.
- **Committed in:** `c7aa11a1`

**2. [Rule 1 - Bug] Updated existing admin success fixtures for the durable burn prerequisite**
- **Found during:** Full named regression gate
- **Issue:** Two prior policy-success tests attempted local policy operations while burn v1 remained bootstrap-only, which is no longer a valid success setup under D-13.
- **Fix:** Peer-confirmed initial burn before those policy scenarios and asserted immediate durable activation after quorum.
- **Files modified:** `test/src/trustedpeer/trust_genesis_tool_test.cpp`
- **Verification:** Both compatibility tests pass; the complete tool suite and named CTest gate pass.
- **Committed in:** `631f48ed`

---

**Total deviations:** 2 auto-fixed (1 missing critical diagnostic mapping, 1 compatibility bug).
**Impact on plan:** Both fixes are required to expose the new failure event correctly and preserve existing tests under the planned readiness invariant; no unrelated behavior changed.

## Issues Encountered

- Network-backed tests require local listener access in the managed sandbox. Focused and full gates passed when run with that permission.

## Verification

- Exact plan command: PASS — both targets built; `PolicyV2BeforeInitialBurnCannotStrandStartup` passed 1/1; focused admin initial-burn/activation-failure tests passed 2/2.
- Callback failure regression: PASS 1/1 — below-quorum approval emitted no event, forced durable commit failure emitted `TRUST_ACTIVATION_FAILED`, and readiness remained waiting.
- Named CTest gate: PASS 3/3 binaries in 23.46 seconds (`trust_genesis_tool_test`, `operator_approval_test`, `trust_first_boot_e2e_test`).
- Full tool suite within CTest: PASS 12/12, including receive/list no-sign and exact-candidate approval coverage.

## Known Stubs

None. The scan found only pre-existing optional callback defaults, test lambdas, and unrelated legacy TODO/default values; no plan change introduced a stub or mock production data path.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- Runtime and administrative sequencing now mirrors Plan 13-13's durable guard.
- No blocker remains for downstream integration and final Phase 13 verification.

## Self-Check: PASSED

- All eight implementation/test files and this summary exist.
- RED, GREEN, and compatibility commits exist in repository history.
- No tracked file deletion or plan-created untracked runtime artifact remains.
- Threat-surface scan found no new endpoint, authentication path, external file access, schema, package, or network surface; the only new surface is the planned diagnostic event.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-13*
