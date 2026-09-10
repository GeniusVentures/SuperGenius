---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 12
subsystem: trusted-peer quorum policy
tags: [canonical-codec, quorum-policy, multisig, sha256, cpp]

# Dependency graph
requires:
  - phase: 13-01
    provides: Canonical trust codec, bounded public-key normalization, and SHA-256 identity helper
  - phase: 09-securecrdt-layer
    provides: SecureCrdt error contract and quorum-policy registry consumers
provides:
  - Canonical versioned quorum-policy state binding network, predecessor, authorizer, peers, and thresholds
  - Exact strict-majority membership and two-thirds BurnConfig threshold validators
  - Current-policy-only successor linkage and self-authorization regression coverage
affects: [13-02, 13-04, 13-05, 13-07, 13-08, 13-09]

# Tech tracking
tech-stack:
  added: []
  patterns: [domain-separated canonical policy bytes, exact overflow-safe integer floors, current-snapshot successor authorization]

key-files:
  created:
    - src/trustedpeer/QuorumPolicy.hpp
    - src/trustedpeer/QuorumPolicy.cpp
    - test/src/trustedpeer/quorum_policy_test.cpp
  modified:
    - src/securecrdt/QuorumThresholdValidation.hpp
    - src/trustedpeer/TrustedPeerRegistry.cpp
    - src/account/BurnConfig.cpp
    - src/trustedpeer/CMakeLists.txt
    - test/src/trustedpeer/CMakeLists.txt

key-decisions:
  - "Policy bytes bind both the expected predecessor hash and authorizing policy hash as distinct canonical 32-byte fields."
  - "Membership uses M/2+1 while BurnConfig uses M-M/3; bounds are checked before either floor."
  - "ValidatePolicySuccessor hashes the confirmed current snapshot and requires both candidate links to equal that hash."

patterns-established:
  - "Policy identity: normalize and validate the model, encode domain-separated bounded bytes, then hash only canonical bytes."
  - "Policy transition: structural successor checks use the current confirmed snapshot; quorum evaluation uses current peers and threshold."

requirements-completed: [POLICY-01, VALID-01, TPR-02, BURN-01]

# Metrics
duration: 12min
completed: 2026-08-12
---

# Phase 13 Plan 12: Exact Quorum Policy and Current-Authorizer Summary

**Versioned policy bytes now bind every signer and threshold decision while exact integer floors and dual current-hash links prevent unsafe thresholds and proposed-set self-authorization.**

## Performance

- **Duration:** 12 min
- **Started:** 2026-08-12T14:23:09Z
- **Completed:** 2026-08-12T14:34:42Z
- **Tasks:** 1 TDD task
- **Files modified:** 8

## Accomplishments

- Added a bounded canonical quorum-policy model with full-consumption decoding, canonical peer ordering, policy hashing, and field-binding regression coverage.
- Replaced percentage-based quorum validation with exact `M/2+1` membership and `M-M/3` burn floors across the complete `M=0,1,2,3,4,100,101` boundary matrix.
- Enforced exact successor versioning, network continuity, expected-predecessor linkage, and authorizing-policy linkage against the confirmed current policy hash.
- Proved that signatures from newly proposed peers can satisfy the proposed signer set yet remain unauthorized under the current policy.

## Task Commits

The TDD task was committed atomically by gate:

1. **RED: Add failing quorum-policy contract tests** - `6f050140` (test)
2. **GREEN: Implement canonical quorum-policy rules** - `76f5fde2` (feat)

## Files Created/Modified

- `src/trustedpeer/QuorumPolicy.hpp` - Versioned policy model, canonical identity API, and successor validation contract.
- `src/trustedpeer/QuorumPolicy.cpp` - Bounded canonical encoding/decoding, SHA-256 policy identity, validation, and current-linked successor checks.
- `src/securecrdt/QuorumThresholdValidation.hpp` - Exact reusable membership and burn floor/bounds validators.
- `src/trustedpeer/TrustedPeerRegistry.cpp` - Explicit membership-floor validator selection.
- `src/account/BurnConfig.cpp` - Explicit two-thirds burn-floor validator selection.
- `src/trustedpeer/CMakeLists.txt` - Quorum policy production source registration.
- `test/src/trustedpeer/quorum_policy_test.cpp` - Exact boundaries, malformed decode, hash binding, successor, and self-authorization coverage.
- `test/src/trustedpeer/CMakeLists.txt` - Focused `quorum_policy_test` target registration.

## Decisions Made

- Encoded the predecessor and authorizer identities as separate length-prefixed 32-byte hash fields. A successor must bind both even though policy transitions require both to identify the same current policy.
- Kept operator/model input peer-order tolerant through canonicalization, while requiring decoded wire bytes to contain strictly canonical sorted peer order.
- Treated `version == 0`, empty/over-cap signer sets, non-lowercase linkage hashes, and every zero/oversized/below-floor threshold as invalid before identity or successor evaluation.

## TDD Cycle

- **RED:** Registered the new source/test targets and compiled seven contract tests against deliberate stubs; all seven failed on the missing floor, canonicalization, hashing, and successor behavior.
- **GREEN:** Implemented the canonical policy model and exact validators; expanded the suite to eight tests and reached 8/8 passing.
- **REFACTOR:** Applied project formatting to the new policy/test code and removed accidental formatting churn from existing production files; no separate behavior-neutral commit was needed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical Functionality] Wired production owners to policy-specific validators**

- **Found during:** Task 1 GREEN integration
- **Issue:** Removing the ambiguous percentage helper exposed that both `TrustedPeerRegistry` and `BurnConfig` still selected one generic validator, allowing the burn path to retain the weaker membership policy.
- **Fix:** Routed `TrustedPeerRegistry` through `ValidateMembershipQuorumThreshold` and `BurnConfig` through `ValidateBurnQuorumThreshold`.
- **Files modified:** `src/trustedpeer/TrustedPeerRegistry.cpp`, `src/account/BurnConfig.cpp`
- **Verification:** `burnconfig_test` passed 4/4; the Phase 13 wave CTest gate passed 3/3.
- **Committed in:** `76f5fde2`

---

**Total deviations:** 1 auto-fixed (1 Rule 2). **Impact on plan:** The additional two call-site edits are required for the explicit validator split to govern production behavior; no new feature scope or dependency was added.

## Issues Encountered

- Network-backed BurnConfig and trusted-peer threshold tests cannot open local GossipPubSub listeners inside the restricted sandbox. They passed when rerun with approved local-listener access; no code workaround was introduced.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- Plans 02, 04, and 05 can consume one deterministic policy identity and exact current-authorizer contract for persistence, candidate validation, and atomic activation.
- Production TPR and BurnConfig constructors now select their explicit safety policy instead of an ambiguous shared percentage helper.
- No blockers or known stubs remain.

## Verification

- `cmake --build build/OSX/Release --target quorum_policy_test trustedpeer -j8 && build/OSX/Release/test_bin/quorum_policy_test` - PASS (8/8 tests).
- `build/OSX/Release/test_bin/burnconfig_test` - PASS (4/4 tests with local-listener access).
- `ctest --test-dir build/OSX/Release --output-on-failure -R 'genesis_manifest|quorum_policy|trustedpeerregistry_threshold'` - PASS (3/3 tests).
- Percentage-floor scan returned no `51` or `0.51` implementation in the threshold/policy sources.

## Self-Check: PASSED

- All eight created/modified implementation and test files exist.
- TDD commits `6f050140` and `76f5fde2` exist in git history in RED-then-GREEN order.
- No tracked files were deleted and no plan-introduced generated/untracked files remain.
- Stub scan found no TODO, FIXME, placeholder, or hardcoded empty runtime values in the delivered policy code.
- Threat-surface scan found no new endpoint, authentication path, file-access boundary, schema, package, or dependency surface beyond the plan's canonical-policy trust boundary.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-12*
