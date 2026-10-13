---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 15
subsystem: burn-config restart authority
tags: [burn-config, trust-state, restart, quorum, cpp]

requires:
  - phase: 13-13
    provides: verified BootstrapOnly versus PeerQuorum burn-authorization classification
  - phase: 13-14
    provides: runtime readiness gates aligned with durable initial-burn authority
provides:
  - restart publication based exclusively on TrustStateStore-verified PeerQuorum classification
  - historical-authorizer regression across a current-policy burn-threshold increase
  - explicit BootstrapOnly restart coverage preserving non-economic state
affects: [13-18, trust-startup, BurnConfig, restart-verification]

tech-stack:
  added: []
  patterns: [verified-classification consumption, historical-authorizer authority]

key-files:
  created: []
  modified:
    - src/account/BurnConfig.cpp
    - test/src/startup/trust_restart_test.cpp

key-decisions:
  - "BurnConfig restart publication consumes TrustStateStore's verified PeerQuorum classification and never reconstructs authority from proof cardinality or the current policy threshold."

patterns-established:
  - "Verifier/consumer boundary: durable verification classifies historical authority once; runtime consumers publish only from that explicit classification."

requirements-completed: [BOOT-04, BURN-02, BURN-03, TEST-01]

duration: 4min
completed: 2026-08-13
---

# Phase 13 Plan 15: Historical Burn Authority Restart Summary

**Peer-quorum burn authority verified under its historical policy now remains economically ready after later policy threshold increases, while BootstrapOnly state remains unpublished.**

## Performance

- **Duration:** 4 min
- **Started:** 2026-08-13T14:56:42Z
- **Completed:** 2026-08-13T15:00:33Z
- **Tasks:** 1 TDD task
- **Files modified:** 2

## Accomplishments

- Replaced `BurnConfig::NewProduction`'s bootstrap-signature and proof-cardinality heuristic with the explicit `BurnAuthorizationKind::PeerQuorum` result from verified durable state.
- Added `HistoricalBurnProofRemainsReadyAfterCurrentThresholdIncrease`, proving a two-signature burn v1 authorized under policy v1 remains ready after policy v2 raises the current burn threshold to three.
- Proved restart returns `ConfirmedReady`, economic value 100, and unchanged policy-v2 and burn-v1 hashes.
- Added explicit BootstrapOnly restart coverage proving it remains `WaitingForInitialBurn` and its confirmed-value provider stays unready.

## Task Commits

1. **Task 1 RED: Historical-authorizer restart counterexample** - `5c3cad47` (test)
2. **Task 1 GREEN: Trust verified burn authorization on restart** - `179d6c97` (fix)

## Files Created/Modified

- `src/account/BurnConfig.cpp` - Publishes restart burn state only when the durable verified snapshot is classified `PeerQuorum`.
- `test/src/startup/trust_restart_test.cpp` - Covers threshold evolution, exact durable-head preservation, value restoration, and BootstrapOnly non-readiness.

## Decisions Made

- Treated `ConfirmedTrustSnapshot::burn_authorization` as the sole restart-readiness authority because `TrustStateStore::LoadAndVerify` already validates each burn record against its persisted historical authorizing policy.
- Kept live candidate activation quorum evaluation unchanged; only restart publication stopped reinterpreting historical proof cardinality.

## TDD Gate Compliance

- **RED (`5c3cad47`):** the focused run passed the baseline and BootstrapOnly cases but failed `HistoricalBurnProofRemainsReadyAfterCurrentThresholdIncrease` because startup returned `WaitingForInitialBurn` and economic readiness was false.
- **GREEN (`179d6c97`):** the same focused cases passed after `NewProduction` consumed the verified `PeerQuorum` classification.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The restart fixture binds an ephemeral local listener, which the restricted sandbox rejects. Focused and full test executions passed with local-listener permission.

## Verification

- Build: `cmake --build build/OSX/Release --target trust_restart_test -j8` - PASS.
- Required focused command: `trust_restart_test --gtest_filter='*HistoricalBurnProofRemainsReadyAfterCurrentThresholdIncrease*:*OmittedTrustConfigRestoresIdenticalDurableAuthority*'` - PASS (2/2).
- BootstrapOnly focused coverage: `trust_restart_test --gtest_filter='*BootstrapOnlyBurnRemainsUnready*'` - PASS (included in the 3/3 GREEN focused run).
- Complete binary: `trust_restart_test` - PASS (6/6).
- `BurnConfig::NewProduction` scan for `burn_proof.*size` or `policy.burn_threshold` - no matches. The remaining threshold comparison in `TryActivateBurnCandidate` is the required live-candidate quorum gate, not restart projection.
- Verified classification link scan found `BurnAuthorizationKind::PeerQuorum` in `NewProduction` and the restart assertions.

## Known Stubs

None. The optional diagnostic manifest and callbacks used by the tests are intentional API inputs; no mock or empty data is published as runtime trust state.

## User Setup Required

None - no external service configuration or dependency installation required.

## Next Phase Readiness

- CR-02 is closed for policy-threshold increases and historical-authorizer burn chains.
- Plan 13-18 can include this restart regression in the exact security closure gate.

## Self-Check: PASSED

- Both modified implementation/test files and this summary exist.
- RED commit `5c3cad47` and GREEN commit `179d6c97` exist in repository history.
- Focused and full restart verification passed, with durable policy and burn hashes asserted unchanged.
- No tracked file deletion or plan-created untracked artifact remains.
- Threat-surface scan found no new endpoint, authentication path, external file access, schema, package, or network surface.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-13*
