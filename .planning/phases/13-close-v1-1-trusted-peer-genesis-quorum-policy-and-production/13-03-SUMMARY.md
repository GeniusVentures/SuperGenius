---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 03
subsystem: secure-crdt policy ownership
tags: [securecrdt, registry, quorum, node-isolation, cpp]

# Dependency graph
requires:
  - phase: 09-securecrdt-layer
    provides: SecureCrdt write/read quorum wrapper and the original policy registry contract
  - phase: 10-trustedpeerregistry
    provides: TrustedPeerRegistry production policy owner
  - phase: 11-burnconfig-quorum-wiring
    provides: BurnConfig production policy owner and live cache refresh
provides:
  - One independently synchronized SecureCrdtRegistry per SecureCrdt instance
  - Node-local TrustedPeerRegistry and BurnConfig registration with fail-closed duplicate handling
  - Two-node same-key quorum and teardown isolation regression coverage
affects: [13-04, 13-05, 13-07, 13-09, SecureCrdt candidate policies, GeniusNode policy lifetime]

# Tech tracking
tech-stack:
  added: []
  patterns: [instance-scoped policy registry, injected registry ownership, owner-token compare-and-remove]

key-files:
  created: []
  modified:
    - src/securecrdt/SecureCrdtRegistry.hpp
    - src/securecrdt/SecureCrdt.hpp
    - src/securecrdt/SecureCrdt.cpp
    - src/trustedpeer/TrustedPeerRegistry.hpp
    - src/trustedpeer/TrustedPeerRegistry.cpp
    - src/account/BurnConfig.hpp
    - src/account/BurnConfig.cpp
    - test/src/securecrdt/securecrdt_registry_test.cpp
    - test/src/securecrdt/securecrdt_quorum_fixture.hpp
    - test/src/securecrdt/securecrdt_quorum_gate_test.cpp

key-decisions:
  - "SecureCrdt creates a fresh registry by default and accepts shared_ptr injection only for explicit composition or tests."
  - "A registry never replaces an existing same-instance pattern: Register returns false and production factories fail with file_exists."
  - "Owner-token unregister operations execute only against the registry owned by the same SecureCrdt instance."

patterns-established:
  - "Node policy lookup: every SecureCrdt resolve/enumeration flows through its registry_ member."
  - "Production ownership: TrustedPeerRegistry and BurnConfig use secure_crdt_->Registry() for both registration and teardown."

requirements-completed: [SCRDT-04, BURN-02, TEST-01]

# Metrics
duration: 27min
completed: 2026-08-12
---

# Phase 13 Plan 03: Instance-Scoped SecureCrdt Policy Registry Summary

**Each SecureCrdt now owns an isolated policy registry, so co-located nodes can govern identical CRDT keys with distinct signer sets without replacement or teardown collisions.**

## Performance

- **Duration:** 27 min
- **Started:** 2026-08-12T13:52:20Z
- **Completed:** 2026-08-12T14:19:15Z
- **Tasks:** 3
- **Files modified:** 10

## Accomplishments

- Replaced the function-local static registry map and mutex with ordinary per-instance state protected by a shared mutex.
- Added SecureCrdt registry ownership, optional injection, and accessors; all legacy resolve and filter-enumeration paths use the owned registry.
- Migrated TrustedPeerRegistry, BurnConfig, and legacy SecureCrdt fixtures to node-local registration and owner-token teardown.
- Added unit and live quorum regressions proving identical patterns retain distinct signer sources, values, signatures, and lifecycle behavior across two in-process nodes.

## Task Commits

Each task was committed atomically, with Task 1 split into its required TDD gates:

1. **Task 1 RED: Add failing registry isolation regression** - `67968165` (test)
2. **Task 1 GREEN: Convert registry and SecureCrdt ownership to instance state** - `c8c8805b` (feat)
3. **Task 2: Bind production policy owners to node registries** - `b3c63ce9` (feat)
4. **Task 3: Migrate fixtures and prove cross-node quorum isolation** - `f98bbab2` (test)

## Files Created/Modified

- `src/securecrdt/SecureCrdtRegistry.hpp` - Instance map/mutex, non-static operations, and non-replacing registration result.
- `src/securecrdt/SecureCrdt.hpp` - Registry ownership, optional injection, and public accessors.
- `src/securecrdt/SecureCrdt.cpp` - Owned-registry construction plus instance lookup/enumeration routing.
- `src/trustedpeer/TrustedPeerRegistry.hpp/.cpp` - Checked registration and local owner-token teardown through the supplied SecureCrdt.
- `src/account/BurnConfig.hpp/.cpp` - Checked registration and local owner-token teardown through the supplied SecureCrdt.
- `test/src/securecrdt/securecrdt_registry_test.cpp` - Independent-registry signer snapshot and teardown coverage.
- `test/src/securecrdt/securecrdt_quorum_fixture.hpp` - Legacy quorum fixture registration moved onto its SecureCrdt instance.
- `test/src/securecrdt/securecrdt_quorum_gate_test.cpp` - Same-key, distinct-signer live quorum and peer-destruction regression.

## Decisions Made

- Kept registry injection optional: normal nodes always receive fresh state, while tests/composition can deliberately share a registry object when that ownership is explicit.
- Made duplicate registration fail closed instead of silently replacing the current owner. This preserves the established factory/outcome error pattern and prevents same-node policy hijacking.
- Kept legacy value/signature key matching and quorum semantics unchanged; candidate segment parsing remains assigned to Plan 04.

## TDD Cycle

- **RED:** Two registry objects registering the same pattern resolved the second signer source globally, and unregistering one removed the other; the new test failed on both assertions.
- **GREEN:** Ordinary registry members plus SecureCrdt ownership made all five registry tests pass, including concurrency and owner-token behavior.
- **REFACTOR:** No separate refactor commit was needed; the minimal implementation retained the existing entry and regex contracts.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- Network-backed tests cannot open local GossipPubSub listeners inside the restricted sandbox. They were rerun with approved local-listener access and passed; no code workaround was introduced.
- The first `genius_node` build rebuilt heavy precompiled dependencies but completed successfully.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- Plan 04 can add content-addressed candidate parsing on top of a node-local registry without cross-node policy collisions.
- TrustedPeerRegistry and BurnConfig keep their legacy behavior while now respecting the SecureCrdt/GeniusNode ownership boundary needed by later lifetime work.
- No blockers or known stubs remain.

## Verification

- `cmake --build build/OSX/Release --target securecrdt_registry_test securecrdt -j8 && build/OSX/Release/test_bin/securecrdt_registry_test` - PASS (5/5 tests).
- `cmake --build build/OSX/Release --target trustedpeer genius_node -j8` - PASS.
- `ctest --test-dir build/OSX/Release --output-on-failure -R 'trustedpeerregistry|burnconfig'` - PASS (4/4 tests).
- `cmake --build build/OSX/Release --target securecrdt_registry_test securecrdt_quorum_gate_test securecrdt_propose_sign_quorum_test -j8` - PASS.
- `ctest --test-dir build/OSX/Release --output-on-failure -R 'securecrdt_registry|securecrdt_quorum_gate|securecrdt_propose_sign_quorum'` - PASS (3/3 tests).
- `ctest --test-dir build/OSX/Release --output-on-failure -R 'securecrdt|trustedpeer|burnconfig'` - PASS (9/9 tests).
- Static scans found no process-global registry storage and no `SecureCrdtRegistry::` calls in migrated production/test scopes.

## Self-Check: PASSED

- All ten modified source/test files exist.
- Task commits `67968165`, `c8c8805b`, `b3c63ce9`, and `f98bbab2` exist in git history.
- No tracked files were deleted and no plan-introduced untracked/generated files remain.
- Stub scan found only intentional API defaults/owner-token initialization; no UI/runtime stubs were introduced.
- No new network endpoint, authentication path, file-access boundary, schema, dependency, or package surface was introduced.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-12*
