---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 01
subsystem: trusted-peer identity
tags: [canonical-codec, genesis, sha256, trust-policy, cpp]

# Dependency graph
requires:
  - phase: 10-trustedpeerregistry
    provides: Genesis-seeded trusted-peer model and 128-hex public-key convention
provides:
  - Bounded map-free canonical trust codec with big-endian integer and length-prefixed byte primitives
  - Deterministic reviewed genesis manifest bytes and SHA-256 fingerprint verification
  - Golden and malformed-input contracts for peer normalization, full-consumption decoding, and field tampering
affects: [13-02, 13-04, 13-05, 13-07, 13-09, 13-12]

# Tech tracking
tech-stack:
  added: []
  patterns: [domain-separated canonical bytes, bounded full-consumption decoding, lowercase sorted public-key normalization]

key-files:
  created:
    - src/trustedpeer/CanonicalTrustCodec.hpp
    - src/trustedpeer/CanonicalTrustCodec.cpp
    - src/trustedpeer/GenesisManifest.hpp
    - src/trustedpeer/GenesisManifest.cpp
    - test/src/trustedpeer/genesis_manifest_test.cpp
  modified:
    - src/trustedpeer/CMakeLists.txt
    - test/src/trustedpeer/CMakeLists.txt

key-decisions:
  - "Genesis identity hashes the exact SGNS_TRUST_GENESIS_V1 schema with fixed-width big-endian integers and explicit key lengths/counts."
  - "Manifest input accepts mixed-case hex for operator convenience, but identity bytes normalize to sorted lowercase fixed 64-byte keys and decoded bytes must already be canonical."
  - "Initial manifests require policy version 1, burn value 100, and the locked majority/two-thirds threshold floors."

patterns-established:
  - "Canonical writer/reader: bounds are checked before copying and decoders require full byte consumption."
  - "Trust identity: validate and canonicalize the model before encoding, then hash only canonical bytes with crypto::sha256."

requirements-completed: [BOOT-02, TPR-01, BURN-03]

# Metrics
duration: 10min
completed: 2026-08-12
---

# Phase 13 Plan 01: Canonical Genesis Identity Summary

**Domain-separated canonical genesis bytes now bind the network, bootstrapper, ordered peers, policy thresholds, and initial burn value into one reproducible SHA-256 trust fingerprint.**

## Performance

- **Duration:** 10 min
- **Started:** 2026-08-12T13:39:54Z
- **Completed:** 2026-08-12T13:49:30Z
- **Tasks:** 1
- **Files modified:** 7

## Accomplishments

- Added reusable bounded big-endian writer/reader primitives, fixed 64-byte public-key conversion, lowercase normalization, and the single canonical SHA-256 helper.
- Added `GenesisManifest` validation, canonical encoding, decoding, fingerprint derivation, and expected-fingerprint verification with fail-closed handling for malformed or non-canonical bytes.
- Added a fixed golden fingerprint plus executable coverage for peer-order invariance, empty/duplicate/malformed/over-cap peers, unknown versions, truncated/overflow/trailing input, and every reviewed-field tamper.

## Task Commits

The TDD task was committed atomically by gate:

1. **RED: Define and prove canonical genesis identity** - `4ff7b1f7` (test)
2. **GREEN: Implement canonical genesis identity** - `c67da1bf` (feat)

## Files Created/Modified

- `src/trustedpeer/CanonicalTrustCodec.hpp` - Public bounded writer/reader, key normalization, limits, domain, and fingerprint API.
- `src/trustedpeer/CanonicalTrustCodec.cpp` - Big-endian codec, checked reader, public-key conversion, sorted deduplication, and SHA-256 implementation.
- `src/trustedpeer/GenesisManifest.hpp` - Reviewed genesis model and canonical identity contract.
- `src/trustedpeer/GenesisManifest.cpp` - Initial-policy validation, canonical byte encoding/decoding, round-trip enforcement, and fingerprint verification.
- `src/trustedpeer/CMakeLists.txt` - Production source and existing SHA target wiring.
- `test/src/trustedpeer/genesis_manifest_test.cpp` - Golden, boundary, malformed, and tamper test suite.
- `test/src/trustedpeer/CMakeLists.txt` - Focused `genesis_manifest_test` target.

## Decisions Made

- Used a fixed 21-byte domain prefix followed by encoding version, network ID, length-prefixed 64-byte bootstrapper, policy version, peer count and length-prefixed peer keys, both thresholds, and initial burn basis points.
- Required strict increasing peer order during decode, making duplicate and non-canonical encoded peer sequences invalid even though operator input is normalized before encoding.
- Enforced `M/2+1` membership and `M-M/3` burn floors in the initial manifest so unsafe genesis thresholds never acquire an identity.

## TDD Cycle

- **RED:** Eight tests compiled and ran; six failed on the intentionally unimplemented canonicalization/fingerprint path while rejection-only cases passed.
- **GREEN:** Implemented the minimum codec and manifest behavior; all eight tests and the CTest registration pass.
- **REFACTOR:** Applied project formatting and removed signed/unsigned span-size warnings without changing behavior.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The existing CMake cache did not initially know the new target. Reconfiguring with `cmake -S build/OSX -B build/OSX/Release` regenerated the build graph; no source or dependency workaround was needed.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- `CanonicalTrustCodec` is ready for candidate and quorum-policy schemas in Plans 04 and 12.
- `GenesisManifest` provides the canonical bytes and fingerprint verification needed by trust persistence, startup, and the one-shot ceremony plans.
- No blockers or known stubs remain.

## Verification

- `cmake --build build/OSX/Release --target genesis_manifest_test -j8 && build/OSX/Release/test_bin/genesis_manifest_test` - PASS (8/8 tests).
- `ctest --test-dir build/OSX/Release --output-on-failure -R 'genesis_manifest'` - PASS (1/1 test).
- Invariant scan found `SGNS_TRUST_GENESIS_V1`, `MAX_TRUSTED_PEERS`, and `initial_burn_basis_points` in the delivered source.

## Self-Check: PASSED

- All five created files exist.
- Task commits `4ff7b1f7` and `c67da1bf` exist in git history.
- No tracked files were deleted and no plan-introduced untracked/generated files remain.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-12*
