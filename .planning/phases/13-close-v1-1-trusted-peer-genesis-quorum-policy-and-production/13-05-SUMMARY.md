---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: "05"
subsystem: trust-policy-activation
tags: [securecrdt, trusted-peer, burn-config, quorum, rocksdb, cpp]

requires:
  - phase: 13-02
    provides: synchronous verified trust-state transitions and durable-head race control
  - phase: 13-04
    provides: authenticated bounded content-addressed candidate transport
provides:
  - explicit operator-approved trusted-peer policy activation under durable current authority
  - deterministic peer-quorum BurnConfig v1/value 100 activation and readiness gate
  - persist-before-cache policy and burn publication with stale-race isolation
  - backward-compatible decoding of original Phase 13 trust records
affects: [13-06, 13-07, 13-08, 13-09, TrustedPeerRegistry, BurnConfig, TransactionManager]

tech-stack:
  added: []
  patterns:
    - explicit candidate-core signing with current-policy authorization
    - durable transition before cache and callback publication
    - node-scoped confirmed burn value provider

key-files:
  created:
    - test/src/trustedpeer/operator_approval_test.cpp
    - test/src/account/burnconfig_policy_e2e_test.cpp
  modified:
    - src/trustedpeer/TrustedPeerRegistry.hpp
    - src/trustedpeer/TrustedPeerRegistry.cpp
    - src/trustedpeer/TrustStateStore.hpp
    - src/trustedpeer/TrustStateStore.cpp
    - src/account/BurnConfig.hpp
    - src/account/BurnConfig.cpp
    - test/src/securecrdt/securecrdt_candidate_race_test.cpp
    - test/src/trustedpeer/CMakeLists.txt
    - test/src/account/CMakeLists.txt

key-decisions:
  - "Production TPR uses separate genesis and successor candidate domains while legacy single-slot APIs remain available for compatibility."
  - "Trust records persist the exact candidate-core authorization bytes so restart verification checks the same bytes operators approved."
  - "Burn v1 uses a domain-separated genesis anchor and replaces the bootstrap placeholder proof only after current burn quorum approves."
  - "Economic readiness and cached burn publication share a node-scoped confirmed-value provider updated only after durable commit."

patterns-established:
  - "Receive-only candidate callbacks never invoke a signer; propose and approve are the only non-genesis signing paths."
  - "Every activation re-reads the durable snapshot, validates exact links, commits under the store mutex, reloads, and then publishes."

requirements-completed: [BOOT-03, POLICY-01, SCRDT-04, TPR-01, TPR-02, BURN-01, BURN-02, BURN-03, TEST-01]

duration: 23min
completed: 2026-08-12
---

# Phase 13 Plan 05: Trusted-Peer and Burn Policy Activation Summary

**Explicit candidate-core approvals now activate trusted-peer and burn successors only under the durable current policy, with deterministic burn genesis and commit-before-cache publication.**

## Performance

- **Duration:** 23 min
- **Started:** 2026-08-12T15:18:30Z
- **Completed:** 2026-08-12T15:41:34Z
- **Tasks:** 3
- **Files modified:** 11

## Accomplishments

- Added a production TrustedPeerRegistry path that exposes no authority before reviewed genesis commits, separates receive-only candidate discovery from explicit signing, and activates exact policy successors through the crash-safe store.
- Added deterministic BurnConfig version-one/value-100 candidate generation after TPR genesis, current burn-threshold approval, explicit later proposals, stale old-policy rejection, and durable readiness/cache publication.
- Extended trust-state records to retain the exact candidate-core bytes signatures cover while preserving decoding and verification of the original Phase 13-02 layout.
- Proved concurrent policy successors yield one durable winner and a stale loser, including after reopening the store.

## Task Commits

1. **Task 1 RED: Policy activation contracts** - `c60804b4` (test)
2. **Task 1 GREEN: Durable trusted-peer activation** - `35d2d15f` (feat)
3. **Task 2 RED: Burn policy activation contracts** - `ad2bba3b` (test)
4. **Task 2 GREEN: Policy-bound BurnConfig activation** - `c9f98afd` (feat)
5. **Task 3: Policy activation verification gate** - `7f5ddc6f` (chore)
6. **Final compatibility fix: Legacy trust record loading** - `334066bf` (fix)

## Files Created/Modified

- `src/trustedpeer/TrustedPeerRegistry.hpp/.cpp` - Reviewed genesis submission, pending policy inbox, explicit propose/approve, current-policy activation, and durable cache publication.
- `src/trustedpeer/TrustStateStore.hpp/.cpp` - Candidate authorization-byte persistence, domain-separated burn genesis anchor, peer-proof burn v1 finalization, and legacy record decoding.
- `src/account/BurnConfig.hpp/.cpp` - Deterministic burn genesis, explicit successor APIs, economic readiness, shared confirmed-value provider, and persist-before-callback publication.
- `test/src/trustedpeer/operator_approval_test.cpp` - Pre-genesis, receive-only, deduplication, self-authorization, exact-link, activation, and durable-publication coverage.
- `test/src/securecrdt/securecrdt_candidate_race_test.cpp` - Barrier-driven durable policy winner/stale loser and reopen proof.
- `test/src/account/burnconfig_policy_e2e_test.cpp` - Genesis ordering, threshold, idempotency, explicit successor, policy-staleness, mixed-signature, and commit-failure coverage.
- `test/src/trustedpeer/CMakeLists.txt`, `test/src/account/CMakeLists.txt` - Exact CTest registration for both new targets.

## Decisions Made

- Kept the pre-existing TPR and BurnConfig factories as compatibility adapters while introducing production factories whose authority comes only from TrustStateStore.
- Stored candidate-core authorization bytes alongside canonical state bytes because SecureCrdt approvals sign the complete candidate context, not merely the payload.
- Allowed the original signed-record layout and bootstrap burn predecessor only as a narrowly verified legacy path; all new burn genesis candidates use the domain-separated anchor.

## TDD Gate Compliance

- Task 1 RED failed on the missing production registry, operator approval, and activation APIs before implementation; Task 1 GREEN passed five operator tests plus the store-backed race test.
- Task 2 RED failed on the missing production BurnConfig, readiness, candidate, and activation APIs before implementation; Task 2 GREEN passed all three burn-policy E2E tests.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Persist exact candidate authorization bytes in the trust store**
- **Found during:** Task 1
- **Issue:** SecureCrdt approvals sign the complete candidate core, while the existing store verified proofs against payload bytes; activation could not preserve or revalidate the operator-approved context after restart.
- **Fix:** Extended signed trust records and commit APIs with independently bound authorization bytes, then revalidated their candidate kind, network, version, links, and payload on load.
- **Files modified:** `src/trustedpeer/TrustStateStore.hpp`, `src/trustedpeer/TrustStateStore.cpp`
- **Verification:** operator approval, trust-state store, race, and burn-policy suites pass.
- **Committed in:** `35d2d15f`

**2. [Rule 2 - Missing Critical] Finalize deterministic burn v1 with peer quorum**
- **Found during:** Task 2
- **Issue:** The prior store wrote burn v1 with the bootstrap proof, but the plan requires initial trusted peers to approve the domain-separated burn candidate before economic readiness.
- **Fix:** Added the burn genesis anchor and a same-state v1 proof replacement path that accepts only current burn-threshold candidate-core signatures.
- **Files modified:** `src/trustedpeer/TrustStateStore.hpp`, `src/trustedpeer/TrustStateStore.cpp`
- **Verification:** genesis readiness remains false at one approval and becomes true only after exact quorum.
- **Committed in:** `35d2d15f`, `c9f98afd`

**3. [Rule 1 - Bug] Preserve original trust-record compatibility**
- **Found during:** Final compatibility review
- **Issue:** Adding authorization bytes changed the signed-record body while retaining the Phase 13 record namespace, which would reject stores written by the preceding plan.
- **Fix:** Added strict legacy decoding and limited the old burn predecessor to an authenticated bootstrap-only compatibility path.
- **Files modified:** `src/trustedpeer/TrustStateStore.cpp`
- **Verification:** 9/9 trust-state tests, 5/5 operator tests, and 3/3 burn-policy tests pass after the compatibility change.
- **Committed in:** `334066bf`

---

**Total deviations:** 3 auto-fixed (2 missing critical, 1 bug).
**Impact on plan:** All deviations were necessary to preserve exact approval semantics, deterministic burn readiness, and compatibility; no new network endpoint, package, or administration surface was added.

## Issues Encountered

- Local GlobalDB fixtures open ephemeral libp2p listeners, so focused and CTest verification required the approved unsandboxed test environment.
- The existing Release CMake cache required regeneration to discover the two new test targets.

## Verification

- `operator_approval_test` - PASS (5/5).
- `securecrdt_candidate_race_test` - PASS (2/2).
- `burnconfig_policy_e2e_test --gtest_filter='*Genesis*:*PolicyBinding*:*PersistBeforeCache*'` - PASS (3/3).
- Focused policy activation CTest gate - PASS (11/11, including both exact new target names).
- Overall `securecrdt|trustedpeer|burnconfig` compatibility gate - PASS (12/12).
- Disabled/skip/fixed-sleep scan - PASS; no matching bypass added.

## Known Stubs

None.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- Plan 13-06 can compose production GlobalDB networking around explicit TPR and BurnConfig candidate services.
- Plans 13-07 and 13-09 can restore verified policy state and drive the reviewed genesis/operator ceremony through the production APIs.
- Plan 13-08 can inject the node-scoped confirmed burn provider into replacement TransactionManager instances.

## Self-Check: PASSED

- Both new test files and all production API headers exist.
- All six Plan 13-05 commits exist in repository history.
- Both new targets are discovered by exact CTest name and all focused/legacy gates pass.
- No tracked files were deleted and the two unrelated pre-existing untracked paths remain untouched.
- Threat-surface scan found only the planned local trust-store and authenticated candidate boundaries; no unplanned endpoint, auth path, package, or external file-access surface was introduced.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-12*
