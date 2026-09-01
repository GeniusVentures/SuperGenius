---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "02"
subsystem: auth
tags: [securecrdt, peerregistry, trustedpeer, quorum, cplusplus, cmake]

# Dependency graph
requires:
  - phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
    provides: TrustedPeerRegistry root trust domain, SecureCrdt/SecureCrdtRegistry policy machinery
provides:
  - sgns::peerregistry::PeerRegistry abstract interface (CurrentSignerSet/GetCurrentPeers/BaseKey) — the contract NetworkRegistry (15-03) and the gater membership source (15-05) implement
  - SecureCrdtRegistryEntry::peer_registry explicit per-key registry association (D-04)
  - peerregistry::MakeRegistrySignerSetSource adapter helper
  - TrustedPeerRegistry as the first PeerRegistry implementer (global root only, D-05)
  - peer_registry_test association suite (base/sig-child authority resolution, source adaptation, unregister)
affects: [15-03-networkregistry, 15-05-gater-membership, securecrdt, trustedpeer, burnconfig]

# Tech tracking
tech-stack:
  added: [] # header-only INTERFACE library; no new third-party dependencies
  patterns:
    - "PeerRegistry cached-only resolution contract: CurrentSignerSet must never re-enter the SecureCrdt quorum-read path (re-entrancy guard documented on the interface)"
    - "Adaptation helper placement: MakeRegistrySignerSetSource lives in peerregistry/PeerRegistry.hpp so SecureCrdtRegistry.hpp only forward-declares PeerRegistry (no include cycle)"
    - "Destruction-safe registry mutation: extract() node before destroying entries that may own the last PeerRegistry reference (destructor re-enters UnregisterIf)"

key-files:
  created:
    - src/peerregistry/PeerRegistry.hpp
    - src/peerregistry/CMakeLists.txt
    - test/src/peerregistry/peer_registry_test.cpp
    - test/src/peerregistry/CMakeLists.txt
  modified:
    - src/securecrdt/SecureCrdtRegistry.hpp
    - src/trustedpeer/TrustedPeerRegistry.hpp
    - src/trustedpeer/TrustedPeerRegistry.cpp
    - src/trustedpeer/CMakeLists.txt
    - src/CMakeLists.txt
    - test/src/CMakeLists.txt

key-decisions:
  - "peer_registry shared_ptr member declared LAST in SecureCrdtRegistryEntry: pre-existing positional aggregate initializers (securecrdt_quorum_fixture.hpp, securecrdt_quorum_gate_test.cpp) supply owner_token as the final element and must stay source-compatible"
  - "Shared (not weak) registry ownership per BurnConfig shared_ptr precedent; cycle risk is handled by destruction-safe extract() in the registry instead of weak_ptr"
  - "Register()/UnregisterIf() unlink entries via unordered_map::extract() and destroy them after releasing the mutex so a PeerRegistry destructor can safely re-enter Unregister()"

patterns-established:
  - "Destruction re-entrancy safety for policy registries owning their authority: extract-then-destroy-outside-lock"
  - "Registry self-association: RegisterSignerSetSource sets entry.peer_registry = shared_from_this() so Resolve() callers can see which authority owns a key"

requirements-completed: [D-04, D-05, PNET-REG]

# Metrics
duration: 140min
completed: 2026-09-01
---

# Phase 15 Plan 02: PeerRegistry Abstraction and SecureCRDT Association Summary

**Header-only sgns::peerregistry::PeerRegistry contract (D-04) with TrustedPeerRegistry as forwarding-only implementer (D-05) and an explicit per-key peer_registry association on SecureCrdtRegistryEntry, plus a destruction-re-entrancy fix in the policy registry**

## Performance

- **Duration:** ~140 min (includes fresh worktree build-tree configuration; first configure + first full compile of securecrdt/trustedpeer dependency cone dominated)
- **Started:** 2026-09-01T11:14:24Z
- **Completed:** 2026-09-01T13:34:06Z
- **Tasks:** 3/3
- **Files modified:** 10 (4 created, 6 modified)

## Accomplishments
- `sgns::peerregistry::PeerRegistry` pure-virtual interface resolving the authorized signer set + quorum from cached state only, with `MakeRegistrySignerSetSource` adapter; exported as INTERFACE library `peerregistry`
- `SecureCrdtRegistryEntry` now carries an explicit optional `peer_registry` authority (D-04); regex contract byte-identical, `UnregisterIf` token semantics unchanged
- `TrustedPeerRegistry` implements `PeerRegistry` via forwarding-only overrides and self-associates its policy entry — still the single global root (D-05), zero cache/quorum/lifecycle changes
- Fixed a real deadlock the association introduced: destroying an entry holding the last registry reference re-entered `UnregisterIf` under a held `std::shared_mutex`
- 4-case `peer_registry_test` suite proving base and sig-child keys resolve to the same authority, source adaptation equivalence, and unregister cleanup

## Task Commits

Each task was committed atomically:

1. **Task 1: Create the PeerRegistry interface module** - `924528d3` (feat)
2. **Task 2: Add explicit PeerRegistry association to SecureCrdtRegistryEntry** - `2a084d9e` (feat)
3. **Task 3: Adapt TrustedPeerRegistry onto PeerRegistry + association tests** - `b2182ad6` (feat)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified
- `src/peerregistry/PeerRegistry.hpp` - PeerRegistry interface + MakeRegistrySignerSetSource adapter (header-only)
- `src/peerregistry/CMakeLists.txt` - INTERFACE library peerregistry, exported via supergenius_install
- `src/CMakeLists.txt` - add_subdirectory(peerregistry)
- `src/securecrdt/SecureCrdtRegistry.hpp` - forward-declares PeerRegistry; entry gains peer_registry member; Register/UnregisterIf destruction-safe locking
- `src/trustedpeer/TrustedPeerRegistry.hpp` - public inheritance + CurrentSignerSet/BaseKey overrides, override on GetCurrentPeers
- `src/trustedpeer/TrustedPeerRegistry.cpp` - CurrentSignerSet forwards to ResolveSignerSet; RegisterSignerSetSource sets entry.peer_registry = shared_from_this()
- `src/trustedpeer/CMakeLists.txt` - PUBLIC link peerregistry
- `test/src/peerregistry/peer_registry_test.cpp` - 4 association tests over MakeSecureCrdtTestNode fixture
- `test/src/peerregistry/CMakeLists.txt` - peer_registry_test target (trustedpeer link set + peerregistry)
- `test/src/CMakeLists.txt` - add_subdirectory(peerregistry)

## Decisions Made
- `peer_registry` declared as the LAST member of `SecureCrdtRegistryEntry`: plan did not mandate a position, and pre-existing positional aggregate initializers in `test/src/securecrdt/securecrdt_quorum_fixture.hpp:85` and `securecrdt_quorum_gate_test.cpp:88` (owner_token as final element) must compile unchanged
- Shared_ptr ownership (not weak_ptr) per BurnConfig precedent; the cycle hazard is neutralized in the registry itself rather than at the type level
- `supergenius_install(peerregistry)` added so the target joins `supergeniusTargets` (install(EXPORT) rejects PUBLIC-linked targets outside the export set)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Destruction re-entrancy deadlock in SecureCrdtRegistry**
- **Found during:** Task 3 (first peer_registry_test run hung; `sample` showed TearDown -> UnregisterIf -> ~pair blocked on the registry mutex)
- **Issue:** Erasing/replacing an entry whose `peer_registry` holds the LAST TrustedPeerRegistry reference runs `~TrustedPeerRegistry` -> `Unregister()` -> `UnregisterIf()` while the registry `std::shared_mutex` is still held (non-recursive) — guaranteed deadlock. Latent for any future consumer (BurnConfig-style flows), triggered by the new association.
- **Fix:** `Register`/`UnregisterIf` now `extract()` the node, release the lock, and let the node handle destroy the entry outside the mutex. Token compare-and-remove semantics, regex line, and Resolve matching order unchanged.
- **Files modified:** src/securecrdt/SecureCrdtRegistry.hpp
- **Verification:** peer_registry_test passes; full securecrdt + trustedpeer + burnconfig suites green (10/10)
- **Committed in:** b2182ad6 (Task 3 commit)

**2. [Rule 3 - Blocking] CMake export-set failure for peerregistry**
- **Found during:** Task 3 reconfigure
- **Issue:** `install(EXPORT "supergeniusTargets") ... includes target "trustedpeer" which requires target "peerregistry" that is not in any export set` — trustedpeer PUBLIC-links the new INTERFACE library
- **Fix:** Added `supergenius_install(peerregistry)` (same convention as every src module)
- **Files modified:** src/peerregistry/CMakeLists.txt
- **Verification:** cmake configure exits 0; ninja builds all targets
- **Committed in:** b2182ad6 (Task 3 commit)

**3. [Rule 3 - Blocking] Plan's literal Resolve keys were pre-normalization**
- **Found during:** Task 3 (tests 1/2/4 failed: Resolve("trusted-peer-registry") returned nullopt)
- **Issue:** `HierarchicalKey` normalizes to a leading slash — TPR registers under "/trusted-peer-registry", and `SecureCrdt` always resolves with `base_key.GetKey()` (SecureCrdt.cpp:63,100,139). The plan's literal "trusted-peer-registry"/"trusted-peer-registry/sig/&lt;addr&gt;" strings can never match.
- **Fix:** Tests resolve with `kRegistryBaseKey = "/trusted-peer-registry"` (asserted equal to `registry_->BaseKey().GetKey()`), child key `/trusted-peer-registry/sig/&lt;addr&gt;`
- **Files modified:** test/src/peerregistry/peer_registry_test.cpp
- **Verification:** All 4 tests pass
- **Committed in:** b2182ad6 (Task 3 commit)

---

**Total deviations:** 3 auto-fixed (1 bug, 2 blocking)
**Impact on plan:** All fixes required for the association mechanism to work at all; no scope creep. Regex contract and token semantics verified untouched.

## Issues Encountered
- Worktree had no submodule checkouts (SGProcessingManager, evmrelay, ProofSystem, GeniusKDF) — CMake configure failed. Resolved build-only by APFS clone-copying the main repo's checkouts (no .git, untracked, never staged). gRPCForSuperGenius/docs not needed.
- No Ninja build tree existed in the worktree; configured fresh `build/OSX/Release` with `-DTHIRDPARTY_DIR=/Users/henriqueklein/gnus/3rdparty` (the dev_pnets install recommended by 15-RESEARCH). The main repo's build dir is owned by the parallel 15-01 executor and was not touched.
- First ctest invocation appeared hung — it was the Task 3 deadlock (documented above); `sample` pinpointed the blocked mutex.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- `PeerRegistry` contract is ready for NetworkRegistry (15-03) to implement and for the 15-05 gater membership source; per-key association is test-covered
- Trust boundary preserved: only the global TrustedPeerRegistry self-associates today; per-network authorities slot in via `MakeRegistrySignerSetSource` + `entry.peer_registry`
- All 10 related suites green: peer_registry, trustedpeer (3), securecrdt (5), burnconfig

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-01*
