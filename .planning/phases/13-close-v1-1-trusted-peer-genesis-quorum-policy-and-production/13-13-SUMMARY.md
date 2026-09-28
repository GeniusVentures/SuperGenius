---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 13
subsystem: trusted-peer persistence
tags: [rocksdb, trust-state, burn-config, quorum, sequencing, cpp]

requires:
  - phase: 13-02
    provides: synchronous verified trust-state persistence and durable successor entry points
  - phase: 13-05
    provides: exact candidate-core authorization bytes and peer-proof replacement of deterministic burn v1
provides:
  - explicit BootstrapOnly versus PeerQuorum classification for verified durable burn state
  - durable rejection of policy-v2 and burn-v2 bypasses before initial burn peer confirmation
  - exact burn-v1/value-100 peer-proof replacement as the sole transition out of BootstrapOnly
affects: [13-14, trust-startup, BurnConfig, TrustedPeerRegistry]

tech-stack:
  added: []
  patterns: [verified authorization classification, transition-mutex sequencing guard, exact same-version proof replacement]

key-files:
  created: []
  modified:
    - src/trustedpeer/TrustStateStore.hpp
    - src/trustedpeer/TrustStateStore.cpp
    - test/src/trustedpeer/trust_state_store_test.cpp
    - test/src/securecrdt/securecrdt_candidate_race_test.cpp
    - test/src/trustedpeer/operator_approval_test.cpp

key-decisions:
  - "Burn readiness is classified from the verified authorization path stored with the current burn head, never from proof count or the current policy threshold."
  - "While burn v1 is BootstrapOnly, both successor APIs reject before normal validation and writes; only an identical burn-v1/value-100 canonical candidate-core with current-policy burn quorum may replace its proof."

patterns-established:
  - "Durable sequencing guard: reload under transition_mutex_, classify authority, reject forbidden transitions before inspecting successor version, links, or proof."
  - "Restart-stable readiness: return BurnAuthorizationKind in ConfirmedTrustSnapshot and include it in snapshot equality."

requirements-completed: [BOOT-04, BURN-03, TEST-01]

duration: 10min
completed: 2026-08-13
---

# Phase 13 Plan 13: Durable Initial-Burn Sequencing Summary

**Verified burn authority now gates durable policy and later-burn advancement, with only an exact peer-quorum replacement of burn v1/value 100 able to unlock successor commits.**

## Performance

- **Duration:** 10 min
- **Started:** 2026-08-13T13:58:45Z
- **Completed:** 2026-08-13T14:08:28Z
- **Tasks:** 1 TDD task
- **Files modified:** 5

## Accomplishments

- Added `BurnAuthorizationKind::BootstrapOnly` and `PeerQuorum` to verified snapshots, derived from the authorization and proof path that `LoadAndVerify` already cryptographically validates.
- Added `INITIAL_BURN_NOT_CONFIRMED` guards to both durable successor entry points before version, link, proof, or write processing.
- Restricted the BootstrapOnly burn path to an identical canonical burn-v1/value-100 record carrying a canonical BurnConfig `CandidateCore` and valid current-policy burn quorum.
- Proved rejected policy-v2 and burn-v2 attempts preserve byte-identical verified heads across reopen, while exact burn-v1 proof replacement unlocks the previously rejected valid successors.

## Task Commits

1. **Task 1 RED: Initial-burn sequencing counterexamples** - `95ab81a5` (test)
2. **Task 1 GREEN: Durable initial-burn sequencing enforcement** - `0b82f058` (fix)
3. **Compatibility: Durable policy race establishes peer-confirmed burn** - `28d2d173` (test)
4. **Compatibility: Operator policy activation establishes peer-confirmed burn** - `164405d2` (test)

## Files Created/Modified

- `src/trustedpeer/TrustStateStore.hpp` - Public burn-authorization classification and typed sequencing failure.
- `src/trustedpeer/TrustStateStore.cpp` - Verified authorization classification plus pre-write policy and burn transition guards.
- `test/src/trustedpeer/trust_state_store_test.cpp` - Direct-store bypass, reopen-preservation, exact replacement, and recovery regressions.
- `test/src/securecrdt/securecrdt_candidate_race_test.cpp` - Existing policy race now establishes the required peer-confirmed initial burn before racing valid successors.
- `test/src/trustedpeer/operator_approval_test.cpp` - Existing successful policy-activation fixture now establishes peer-confirmed initial burn first.

## Decisions Made

- Classified peer authority from the exact historical authorizing policy and persisted authorization bytes already verified by `LoadAndVerify`; proof cardinality is not a readiness signal.
- Kept compatibility with authenticated legacy bootstrap records while requiring new BootstrapOnly-to-PeerQuorum replacement writes to use canonical BurnConfig candidate-core bytes.
- Applied the gate inside `TrustStateStore` under `transition_mutex_`, making it authoritative for every caller and restart-safe regardless of higher-level callback ordering.

## TDD Gate Compliance

- **RED (`95ab81a5`):** both named counterexamples compiled and failed because policy v2 and burn v2 committed while burn v1 remained bootstrap-authorized.
- **GREEN (`0b82f058`):** both counterexamples passed with typed rejection, unchanged reopen state, exact peer-proof replacement, and successful retry.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Preserved the durable policy-race contract under the new prerequisite**
- **Found during:** Overall verification
- **Issue:** The existing durable race fixture attempted policy successors directly after bootstrap genesis, so both racers correctly received `INITIAL_BURN_NOT_CONFIRMED` instead of exercising winner/stale behavior.
- **Fix:** Peer-confirmed the exact initial burn before starting the policy race.
- **Files modified:** `test/src/securecrdt/securecrdt_candidate_race_test.cpp`
- **Verification:** `securecrdt_candidate_race_test --gtest_filter='*ExactlyOneStoreBackedPolicyWinnerSurvivesReopen*'` passes.
- **Committed in:** `28d2d173`

**2. [Rule 1 - Bug] Preserved the operator policy-activation success contract**
- **Found during:** Overall verification
- **Issue:** The existing successful operator policy-activation fixture also attempted policy v2 while burn v1 remained bootstrap-only.
- **Fix:** Added exact current-policy burn-v1 peer confirmation before the test's policy activation.
- **Files modified:** `test/src/trustedpeer/operator_approval_test.cpp`
- **Verification:** `operator_approval_test --gtest_filter='*CurrentPolicyQuorumCommitsBeforePublishingSuccessor*'` passes with local listener permission.
- **Committed in:** `164405d2`

---

**Total deviations:** 2 auto-fixed bugs.
**Impact on plan:** Both changes update pre-existing success fixtures to satisfy the new durable prerequisite without changing production scope or weakening either original contract.

## Issues Encountered

- Network-backed operator tests cannot bind local listeners inside the restricted sandbox; the focused compatibility test passed when rerun with local listener permission.

## Verification

- Plan command: `cmake --build build/OSX/Release --target trust_state_store_test -j8 && build/OSX/Release/test_bin/trust_state_store_test --gtest_filter='*PolicySuccessorRejectedUntilInitialBurnPeerConfirmed*:*BurnV2RejectedUntilInitialBurnPeerConfirmed*:*Restart*'` - PASS (2/2 selected tests; no existing test names matched `*Restart*`).
- Full durable-store suite: `build/OSX/Release/test_bin/trust_state_store_test` - PASS (11/11).
- Downstream compile gate: `trust_state_store_test`, `burnconfig_policy_e2e_test`, `operator_approval_test`, and `securecrdt_candidate_race_test` targets build successfully.
- Durable race compatibility: focused test PASS (1/1).
- Operator activation compatibility: focused test PASS (1/1) with local listener permission.
- Required symbol scan found the public classification, typed error, durable guards, and assertions.

## Known Stubs

None. Empty default authorization/committer values are pre-existing optional API defaults and do not flow to UI or replace runtime trust data.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- Plan 13-14 can consume `burn_authorization` directly when restoring economic readiness instead of reinterpreting proof count against the current policy.
- No blocker remains for the durable CR-01 sequencing boundary.

## Self-Check: PASSED

- All five modified implementation/test files and this summary exist.
- RED, GREEN, and both compatibility commits exist in repository history.
- No tracked file deletion or plan-created untracked artifact remains.
- Threat-surface scan found only the planned durable policy/burn transition boundary; no new endpoint, authentication path, external file access, schema, package, or network surface was introduced.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-13*
