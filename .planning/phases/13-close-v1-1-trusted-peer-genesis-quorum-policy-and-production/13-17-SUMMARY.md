---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 17
subsystem: securecrdt
tags: [cpp, crdt, multisig, authorization, storage-bounds, gtest]

requires:
  - phase: 13-03
    provides: instance-scoped SecureCrdt policy registries and isolated quorum sources
  - phase: 13-04
    provides: canonical current-policy authorization pattern for signed candidate ingress
provides:
  - canonical current-member authorization for legacy local and remote signature children
  - authorized-set-bounded legacy signature retention with stale and outsider pruning
  - partial-initialization-safe teardown for SecureCrdt network fixtures
affects: [13-18, SecureCrdt, legacy-quorum, securecrdt-tests]

tech-stack:
  added: []
  patterns: [single signer-snapshot ingress validation, exact hierarchical-key binding, ownership-guarded fixture teardown]

key-files:
  created: []
  modified:
    - src/securecrdt/SecureCrdt.hpp
    - src/securecrdt/SecureCrdt.cpp
    - src/securecrdt/CMakeLists.txt
    - test/src/securecrdt/CMakeLists.txt
    - test/src/securecrdt/securecrdt_quorum_gate_test.cpp
    - test/src/securecrdt/securecrdt_quorum_fixture.hpp
    - test/src/securecrdt/securecrdt_candidate_test.cpp
    - test/src/securecrdt/securecrdt_candidate_race_test.cpp

key-decisions:
  - "One validated signer-set snapshot governs membership, retained-child pruning, the storage bound, and quorum evaluation for each legacy operation."
  - "Remote legacy signatures must bind to the exact base/sig/canonical-address key before cryptographic verification or persistence."

patterns-established:
  - "Legacy signature ingress: validate the bounded canonical signer snapshot, validate exact current membership, prune retained state, then verify and persist."
  - "Fixture teardown follows pointer ownership and treats storage-factory/path cleanup as idempotent after partial setup."

requirements-completed: [TEST-01]

duration: 13min
completed: 2026-08-13
---

# Phase 13 Plan 17: Current-Member-Bounded Legacy Signature Summary

**Legacy SecureCrdt signatures now require exact canonical current membership on both ingress paths, while retained children remain bounded to that same signer snapshot.**

## Performance

- **Duration:** 13 min
- **Started:** 2026-08-13T14:22:11Z
- **Completed:** 2026-08-13T14:35:00Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments

- Rejected cryptographically valid outsider and noncanonical signer addresses before local `Put`, and rejected outsider CRDT elements before remote retention.
- Bound exact `base/sig/<address>` children to one validated current signer snapshot, pruning malformed, stale, and unauthorized children before local add or quorum evaluation.
- Preserved quorum semantics by evaluating only unique retained current members from the same snapshot used for cleanup and authorization.
- Made all directly exercised SecureCrdt fixtures safe when setup stops before `node_` or `secure_crdt_` construction.

## Task Commits

1. **Task 1 RED: Legacy outsider signature counterexamples** - `2e9d2772` (test)
2. **Task 1 GREEN: Current-member authorization and retention bound** - `1c77d845` (fix)
3. **Task 2: Partial-safe SecureCrdt fixture teardown** - `fe9798a5` (test)
4. **Verification support: Shared SecureCrdt test codec linkage** - `2c049dac` (chore)

## Files Created/Modified

- `src/securecrdt/SecureCrdt.hpp` - Adds typed unauthorized-signer and signature-limit errors plus private legacy validation helpers.
- `src/securecrdt/SecureCrdt.cpp` - Enforces canonical current membership, exact child-key binding, pruning, retention bounds, and single-snapshot quorum evaluation.
- `src/securecrdt/CMakeLists.txt` - Links the existing address-validation implementation used by the new ingress gate.
- `test/src/securecrdt/CMakeLists.txt` - Makes the existing canonical codec dependency available to every network-backed SecureCrdt regression target.
- `test/src/securecrdt/securecrdt_quorum_gate_test.cpp` - Adds local/remote valid-outsider and signer-set shrink regressions, plus partial-safe teardown.
- `test/src/securecrdt/securecrdt_quorum_fixture.hpp` - Guards shared fixture cleanup after partial setup.
- `test/src/securecrdt/securecrdt_candidate_test.cpp` - Guards callback/domain cleanup and resource reset after partial setup.
- `test/src/securecrdt/securecrdt_candidate_race_test.cpp` - Guards domain cleanup and resource reset after partial setup.

## Decisions Made

- Resolved the signer-set source exactly once per operation so membership, cleanup, capacity, and quorum cannot observe different policy snapshots.
- Treated malformed, duplicate, empty, oversized, or noncanonical signer snapshots as authorization failures; unsafe snapshots never permit signature persistence.
- Broadened the legacy filter registration only across the registered `sig` subtree, then required exact key equality in the filter, so extra path segments are caught and rejected.
- Kept replacement of an existing authorized address admissible while refusing any new address once the current authorized-set bound is reached.

## TDD Gate Compliance

- **RED (`2e9d2772`):** both named counterexamples compiled and failed because local and remote valid outsiders persisted and retained children remained above the shrunken signer set.
- **GREEN (`1c77d845`):** the focused outsider, retention, and existing under-signed quorum tests passed after the production gate and cleanup implementation.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Restored canonical codec linkage for legacy SecureCrdt test targets**
- **Found during:** Task 1 RED and overall SecureCrdt verification
- **Issue:** `securecrdt` already referenced `CanonicalTrustCodec`, but legacy test targets did not link the providing library and failed with undefined codec symbols when rebuilt.
- **Fix:** Added the dependency to the shared network-backed SecureCrdt test link set and linked the address validator directly to `securecrdt`.
- **Files modified:** `src/securecrdt/CMakeLists.txt`, `test/src/securecrdt/CMakeLists.txt`
- **Verification:** All seven SecureCrdt targets rebuild and the full `ctest -R securecrdt` gate passes.
- **Committed in:** `2e9d2772`, `1c77d845`, `2c049dac`

**2. [Rule 1 - Bug] Made the mixed-case address counterexample deterministic**
- **Found during:** Task 1 GREEN verification
- **Issue:** Uppercasing the first address byte was a no-op when that byte was numeric, allowing the test input to remain canonical.
- **Fix:** Locate and uppercase an actual lowercase hexadecimal letter before asserting typed rejection.
- **Files modified:** `test/src/securecrdt/securecrdt_quorum_gate_test.cpp`
- **Verification:** Focused local outsider regression passes and returns `UNAUTHORIZED_SIGNER` for the mixed-case address.
- **Committed in:** `1c77d845`

---

**Total deviations:** 2 auto-fixed (1 blocking issue, 1 test bug).
**Impact on plan:** Both fixes were required to execute the planned regressions faithfully; no runtime feature or transport scope was added.

## Issues Encountered

- Network-backed fixtures require ephemeral listener permission. A restricted run deliberately reproduced setup failure and exited with the original `ASSERT_NE(node_, nullptr)` report without SIGSEGV; normal listener-capable verification then passed.

## Verification

- Focused Task 1 gate: 3/3 selected tests passed (`LocalOutsiderSignatureNeverPersists`, `RemoteOutsiderSignatureNeverReplicatesAndRetentionBoundTracksAuthorizedSet`, and `UnderSignedWriteNeverReportsQuorum`).
- Task 2 named suites: 3/3 CTest targets passed.
- Full SecureCrdt regression gate: 7/7 CTest targets passed in 25.27 seconds.
- Partial-setup probe: candidate fixture reported its original node setup assertion and completed teardown without a null dereference or SIGSEGV.
- `git diff --check`: passed.

## Known Stubs

None. The optional null registry and owner-token defaults are existing API defaults and do not supply runtime signer or UI data.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- CR-04 and bounded WR-04 are closed with executable local, remote, retention, quorum, and teardown evidence.
- Plan 13-18 can include these regressions in the exact final security closure gate; Plans 13-14 and 13-15 remain independently incomplete.

## Self-Check: PASSED

- All eight modified implementation/test files and this summary exist.
- RED `2e9d2772`, GREEN `1c77d845`, fixture `fe9798a5`, and link repair `2c049dac` exist in repository history in execution order.
- Focused, named-suite, full SecureCrdt, and partial-setup verification completed with the expected results.
- No tracked deletion or plan-created untracked artifact remains; both protected pre-existing untracked paths were left untouched.
- Threat-surface scan found only the planned legacy local/remote signature ingress boundary; no new endpoint, authentication path, schema, package, file-access surface, RPC, or topic was introduced.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-13*
