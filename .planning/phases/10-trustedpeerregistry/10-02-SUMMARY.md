---
phase: 10-trustedpeerregistry
plan: 02
subsystem: crdt
tags: [securecrdt, quorum, trustedpeer, test, config]

requires:
  - phase: 10-trustedpeerregistry
    plan: 01
    provides: "TrustedPeerRegistry: genesis-seeded, quorum-updatable trusted-peer set"
provides:
  - "test/src/trustedpeer/ suite: automated TPR-01/TPR-02 proof via real SecureCrdt-backed single-node fixture, TPR-03 grep-inspection companion documented"
  - "GeniusNode trusted_peers_genesis_/bootstrapper_node_address_ config surface (parse-only, no live wiring)"
affects: [11-burnconfig-quorum-wiring]

tech-stack:
  added: []
  patterns:
    - "Ephemeral in-memory-only genesis-ceremony signer (EthereumKeyGenerator + GeniusAccount::Sign-replica routine), never persisted, confined to test binaries"
    - "Real GeniusAccount signers (MemorySecureStorage-backed) for post-genesis N-of-M membership-change quorum tests"

key-files:
  created:
    - test/src/trustedpeer/genesis_ceremony_helper.hpp
    - test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp
    - test/src/trustedpeer/trustedpeerregistry_quorum_test.cpp
    - test/src/trustedpeer/CMakeLists.txt
  modified:
    - test/src/CMakeLists.txt
    - src/trustedpeer/TrustedPeerRegistry.hpp
    - src/trustedpeer/TrustedPeerRegistry.cpp
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - example/node_test/sgns_config.json

key-decisions:
  - "Added TrustedPeerRegistry::Unregister() (thin wrapper over SecureCrdtRegistry::UnregisterIf) since Plan 01 did not expose one -- needed for clean per-test teardown"
  - "genesis_ceremony_helper.hpp replicates GeniusAccount::Sign's exact secp256k1/SHA256(SHA256) routine against a locally-created secp256k1_context (SECP256K1_CONTEXT_SIGN) rather than depending on GeniusAccount's non-exported file-local GetSecp256k1Context()"
  - "trusted_peers/bootstrapper_node config fields are parse-and-store only in GeniusNode -- verified via `grep -c TrustedPeerRegistry src/account/GeniusNode.cpp` returning 0, preserving the scope boundary for Phase 11"

requirements-completed: [TPR-01, TPR-02, TPR-03]

duration: 45min
completed: 2026-07-24
---

# Phase 10 Plan 02: TrustedPeerRegistry Test Coverage + Config Surface Summary

**Automated end-to-end proof of TrustedPeerRegistry's genesis-seeding (TPR-01) and N-of-M quorum-gated membership changes (TPR-02) via a real SecureCrdt-backed single-node fixture, plus the TPR-03 grep-inspection companion and a new parse-only `sgns_config.json` config surface for Phase 11.**

## Performance

- **Duration:** ~45 min
- **Tasks:** 3 completed
- **Files modified:** 9 (4 created, 5 modified)

## Accomplishments
- `genesis_ceremony_helper.hpp` generates a fresh, in-memory-only Ethereum keypair per call and signs a payload exactly the way `GeniusAccount::Sign` does (own locally-created/destroyed `secp256k1_context`, `SHA256(SHA256(payload))` digest, byte-reversed compact-signature encoding) so the result verifies unmodified via `multisig::VerifyPayloadSignature`
- `trustedpeerregistry_genesis_test.cpp` proves: (1) `GetCurrentPeers()` reflects the genesis list immediately at construction, before any `SeedGenesis`/`TryConfirm`; (2) a valid `SeedGenesis` + `TryConfirm` sequence confirms end-to-end through a real `SecureCrdt`; (3) a signature produced over a mismatched payload is rejected (`SeedGenesis` itself errors, since `SecureCrdt::AddSignature` verifies against the currently-proposed payload) and the cache stays at its constructor-seeded value
- `trustedpeerregistry_quorum_test.cpp` proves: (1) 1 valid signature at threshold 2 never mutates `GetCurrentPeers()`; (2) a 2nd valid signature from a distinct current signer meets quorum and replaces the *whole* peer list; (3) a cryptographically valid signature from a non-member address never contributes toward quorum, even combined with insufficient real member signatures
- `TrustedPeerRegistry::Unregister()` added (thin wrapper) so test fixtures can cleanly unregister their signer-set-source in `TearDown()`
- `GeniusNode` gains `trusted_peers_genesis_`/`bootstrapper_node_address_` members, populated by two new parse-only `LoadSgnsConfig()` blocks mirroring the existing `bootstrap_fullnodes`/`authorized_full_node` idiom -- zero `TrustedPeerRegistry`/`Blockchain`-level wiring introduced
- `example/node_test/sgns_config.json` extended with example `trusted_peers`/`bootstrapper_node` fields

## Task Commits

1. **Task 1: Genesis ceremony test helper + TPR-01 genesis test** - `99b51085` (test)
2. **Task 2: TPR-02 quorum test + TPR-03 grep gate** - `3ac739db` (test)
3. **Task 3: sgns_config.json parsing for trusted_peers/bootstrapper_node** - `033fd43e` (feat)

(Task 3 was executed first in this run for straightforward ordering convenience; all three commits are independent and atomic.)

## Files Created/Modified
- `test/src/trustedpeer/genesis_ceremony_helper.hpp` - Ephemeral in-memory keypair generation + signing, mirrors `GeniusAccount::Sign`'s exact routine
- `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp` - TPR-01 coverage (3 tests)
- `test/src/trustedpeer/trustedpeerregistry_quorum_test.cpp` - TPR-02 coverage (2 tests) + TPR-03 header-comment documentation
- `test/src/trustedpeer/CMakeLists.txt` - New test target wiring
- `test/src/CMakeLists.txt` - `add_subdirectory(trustedpeer)`
- `src/trustedpeer/TrustedPeerRegistry.hpp`/`.cpp` - Added `Unregister()` method
- `src/account/GeniusNode.hpp`/`.cpp` - New parse-only config members + `LoadSgnsConfig()` blocks
- `example/node_test/sgns_config.json` - Example `trusted_peers`/`bootstrapper_node` fields

## Decisions Made
- Added a public `TrustedPeerRegistry::Unregister()` method (not present after Plan 01) since test fixtures need a way to unregister the signer-set-source in `TearDown()` without reaching into `SecureCrdtRegistry` internals directly.
- Implemented the ephemeral ceremony signer's crypto routine as a self-contained, locally-scoped `secp256k1_context` rather than depending on `GeniusAccount`'s non-exported file-local `GetSecp256k1Context()`, per the plan's explicit guidance.
- Kept `sgns_config.json`'s two new fields strictly parse-and-store, verified via `grep -c "TrustedPeerRegistry" src/account/GeniusNode.cpp` returning 0.

## Deviations from Plan

None - plan executed exactly as written, including the one explicitly-anticipated addition (`TrustedPeerRegistry::Unregister()`) called out in Task 1's `<action>` block ("add this one method to `TrustedPeerRegistry.hpp`/`.cpp` if not already present from Plan 01 -- check first").

## Issues Encountered

**Build/test verification could not be executed as a compiled/run build.** As in Plan 01, this worktree checkout has no configured `CMakeCache.txt` anywhere in the tree and git submodules (`ProofSystem`, `GeniusKDF`, `SGProcessingManager`, etc.) are not checked out -- `cmake --build`/`ctest` cannot run in this sandbox (environment limitation, not a code issue, consistent with Plan 01's summary).

Source-level acceptance gates that do not require a full build were run and pass:
- `grep -rn "secp256k1\|VerifySignature\|VerifyPayloadSignature" src/trustedpeer/` -> zero matches (TPR-03 gate)
- `grep -c "trusted_peers_genesis_" src/account/GeniusNode.hpp` -> 1
- `grep -c "bootstrapper_node_address_" src/account/GeniusNode.hpp` -> 1
- `grep -c "trusted_peers_genesis_.push_back" src/account/GeniusNode.cpp` -> 1
- `grep -c "bootstrapper_node_address_ =" src/account/GeniusNode.cpp` -> 1
- `grep -c "TrustedPeerRegistry" src/account/GeniusNode.cpp` -> 0 (scope boundary confirmed)
- `grep -c "trusted_peers\|bootstrapper_node" example/node_test/sgns_config.json` -> 2 (both new fields present)
- `python3 -m json.tool example/node_test/sgns_config.json` -> valid JSON

Implementation was verified via careful manual review:
- Test files' API usage checked line-by-line against `src/trustedpeer/TrustedPeerRegistry.hpp`'s actual signatures and `src/securecrdt/SecureCrdt.cpp`'s `AddSignature`/`ReadIfQuorum` implementation (confirming e.g. that a signature over a mismatched payload is rejected immediately by `AddSignature`, not silently accepted and only failing at `TryConfirm`).
- `genesis_ceremony_helper.hpp`'s signing routine checked field-by-field against `GeniusAccount::Sign` (`src/account/GeniusAccount.cpp:845-873`) for exact type/byte-order parity (`ethereum::scalar_field_type`, `field_element_to_bytes`, byte-reversal, `SHA256(SHA256(...))`, compact-signature serialization + byte-reversal).
- Include ordering in the new test helper mirrors `GeniusAccount.cpp`'s documented "keep these include files here to prevent errors within crypto3's headers" comment exactly.
- Cross-directory test includes (`"securecrdt/securecrdt_test_node.hpp"`, `"trustedpeer/..."`) verified against `test/CMakeLists.txt`'s `include_directories(${CMAKE_CURRENT_SOURCE_DIR} ...)` (i.e. `test/`), matching the pattern already used elsewhere in the test tree.

## Build/Test Verification (orchestrator follow-up)

The orchestrator merged the worktree and ran the real build against the project's configured build (`build/OSX/Release`). This surfaced **three real build-wiring bugs** the executor's grep/manual-review verification could not have caught:

1. **Missing `libsecp256k1::secp256k1` link**: `test/src/trustedpeer/CMakeLists.txt` didn't link secp256k1 even though `genesis_ceremony_helper.hpp` directly includes `<secp256k1.h>` — `#include <secp256k1.h>` failed with "file not found" (the link library also carries the include path via CMake's transitive `INTERFACE_INCLUDE_DIRECTORIES`). Fixed by adding `libsecp256k1::secp256k1` to `TRUSTEDPEER_TEST_NODE_LIBS`.
2. **Missing include path for the shared test fixture**: `"securecrdt/securecrdt_test_node.hpp"` couldn't resolve — `test/src/securecrdt/` isn't under the `src/` include root that makes `"securecrdt/SecureCrdt.hpp"` resolve. Fixed with `target_include_directories(... PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)` on both new test targets, so quoted includes resolve relative to `test/src/`.
3. **Header-order macro collision** (the SUMMARY's claim that include ordering "mirrors GeniusAccount.cpp's... pattern" was inaccurate for `trustedpeerregistry_quorum_test.cpp`): it included `<gtest/gtest.h>` and `<boost/filesystem/operations.hpp>` *before* `genesis_ceremony_helper.hpp`. Since boost/gtest transitively include `<sys/termios.h>` (which `#define`s `B0`/`B1` as baud-rate constants), and `genesis_ceremony_helper.hpp` pulls in `nil::crypto3` algebra headers that use `B0`/`B1` as local variable names, the macros corrupted the crypto3 header's own source — 3 compile errors. Fixed by reordering the ceremony helper's include to be first in the file, actually matching `GeniusAccount.cpp`'s documented pattern this time.

After all three fixes:
- `cmake --build . --target trustedpeerregistry_genesis_test trustedpeerregistry_quorum_test` — **succeeded**.
- `ctest -R trustedpeer --output-on-failure` — **2/2 tests passed**.
- TPR-03 grep gate re-confirmed: `grep -rn "secp256k1\|VerifySignature\|VerifyPayloadSignature" src/trustedpeer/` — zero matches.
- Full project build + full `ctest`: 80/81 passed. The 1 failure (`transaction_sync_test`) is the same pre-existing, unrelated issue already confirmed out of scope in Phase 8/9's verification.

TPR-01, TPR-02, TPR-03 are now fully proven by actual compiled/executed tests, not just grep-based structural checks.

## Next Phase Readiness
- Phase 10's success criteria (TPR-01, TPR-02, TPR-03) now have both implementation (Plan 01) and automated test coverage (this plan), verified against a real compiled build.
- `GeniusNode` exposes a clean, parse-only `trusted_peers_genesis_`/`bootstrapper_node_address_` config surface for Phase 11 (BURN_BASIS_POINTS/quorum wiring) to consume, with zero premature `Blockchain`/startup coupling.

---
*Phase: 10-trustedpeerregistry*
*Completed: 2026-07-24*

## Self-Check: PASSED (build+test verified by orchestrator follow-up; three real build-wiring bugs found and fixed)

All created files verified present on disk; all task commits (`033fd43e`, `99b51085`, `3ac739db`) verified present in git log.
