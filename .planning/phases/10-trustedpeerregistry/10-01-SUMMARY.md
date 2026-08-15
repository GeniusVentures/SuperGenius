---
phase: 10-trustedpeerregistry
plan: 01
subsystem: crdt
tags: [securecrdt, quorum, trustedpeer, cmake]

requires:
  - phase: 09-securecrdt-layer
    provides: "SecureCrdt/SecureCrdtRegistry/ISignedCRDTData quorum-write machinery"
provides:
  - "TrustedPeerListPayload: ISignedCRDTData payload type for the trusted-peer list"
  - "TrustedPeerRegistry: genesis-seeded, quorum-updatable trusted-peer set, first real consumer of Phase 9's SecureCrdt layer"
  - "trustedpeer CMake library target"
affects: [10-02, 11-burnconfig-quorum-wiring]

tech-stack:
  added: []
  patterns:
    - "ISignedCRDTData payload implementer with structural-only Verify (no cache diffing)"
    - "signer-set-source lambda reading only cached state, never re-entering SecureCrdt::ReadIfQuorum"

key-files:
  created:
    - src/trustedpeer/TrustedPeerRegistry.hpp
    - src/trustedpeer/TrustedPeerRegistry.cpp
    - src/trustedpeer/CMakeLists.txt
  modified:
    - src/CMakeLists.txt

key-decisions:
  - "Public constructor + static New() factory (mirrors SecureCrdt's own public-constructor convention) rather than private-ctor+friend"
  - "128-lowercase-hex address format constant duplicated locally in TrustedPeerRegistry.cpp rather than importing GeniusAccount, per plan's dependency-avoidance guidance"

patterns-established:
  - "TrustedPeerRegistry-style consumer of SecureCrdt: cache mutated only inside TryConfirm, strictly after ReadIfQuorum confirms quorum, never speculatively"

requirements-completed: [TPR-01, TPR-02, TPR-03]

duration: 25min
completed: 2026-07-24
---

# Phase 10 Plan 01: TrustedPeerRegistry Component Summary

**Genesis-seeded, quorum-updatable TrustedPeerRegistry built as the first real consumer of Phase 9's SecureCrdt/SecureCrdtRegistry/ISignedCRDTData machinery, wired into a standalone `trustedpeer` CMake library.**

## Performance

- **Duration:** ~25 min
- **Tasks:** 2 completed
- **Files modified:** 4 (3 created, 1 modified)

## Accomplishments
- `TrustedPeerListPayload` implements `ISignedCRDTData` with newline-joined codec and structural-only `Verify` (non-empty, no duplicates, 128-lowercase-hex entries), never diffing against mutable state
- `TrustedPeerRegistry` provides `SeedGenesis`/`ProposeMembershipChange`/`SignMembershipChange`/`TryConfirm`/`GetCurrentPeers`/`IsGenesisConfirmed`, delegating all propose/sign/quorum logic to `SecureCrdt` — zero bespoke signature/quorum code in `src/trustedpeer/`
- `ResolveSignerSet` (the registered signer-set-source) reads only cached state under `cache_mutex_`, never re-entering `SecureCrdt::ReadIfQuorum` (Pitfall 2 avoided)
- `trustedpeer` CMake target created, linking `securecrdt`, wired into `src/CMakeLists.txt`

## Task Commits

1. **Task 1: Define TrustedPeerListPayload contract + TrustedPeerRegistry class declaration** - `95978301` (feat)
2. **Task 2: Implement TrustedPeerRegistry.cpp + CMake wiring** - `77472fb5` (feat)

## Files Created/Modified
- `src/trustedpeer/TrustedPeerRegistry.hpp` - `TrustedPeerListPayload` + `TrustedPeerRegistry` class declarations
- `src/trustedpeer/TrustedPeerRegistry.cpp` - Implementation: codec, structural verification, cache, signer-set resolution, propose/sign/confirm delegation to `SecureCrdt`
- `src/trustedpeer/CMakeLists.txt` - New `trustedpeer` library target linking `securecrdt`
- `src/CMakeLists.txt` - Added `add_subdirectory(trustedpeer)` after `securecrdt`

## Decisions Made
- Used a public constructor + `New()` static factory (matches `SecureCrdt`'s own convention) instead of a private constructor + friend workaround for `make_shared`.
- Duplicated the 128-lowercase-hex address-length constant locally (`kTrustedPeerAddressHexLength`) rather than importing `GeniusAccount`, per the plan's explicit dependency-avoidance guidance.
- Malformed/invalid confirmed payloads in `TryConfirm` surface as `std::errc::bad_message`, matching the existing codebase convention (`src/proof/TransferProof.cpp`, `src/local_secure_storage/impl/Apple.cpp`) rather than declaring a bespoke error enum for this first task.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

**Build verification could not be executed as a compiled build.** The worktree checkout has no top-level `CMakeLists.txt` reachable in this sandbox, no configured `CMakeCache.txt` anywhere in the tree, and git submodules (`gRPCForSuperGenius`, `GeniusKDF`, `ProofSystem`, `SGProcessingManager`, etc.) are not checked out — `cmake --build build --target trustedpeer` fails immediately with "could not load cache" rather than compiling. This is an environment limitation, not a code issue: the two source-level acceptance gates that don't require a full build were run and both pass:
- `grep -rn "secp256k1\|VerifySignature\|VerifyPayloadSignature" src/trustedpeer/` → zero matches (TPR-03 gate)
- `grep -c "db_->Put\|->Put(" src/trustedpeer/TrustedPeerRegistry.cpp` → 0

Implementation was verified by careful manual review against `SecureCrdt.hpp`'s documented contracts (types, signatures, `outcome::result` usage) and by mirroring the same `outcome::failure(std::errc::...)` idiom already used elsewhere in the codebase (`src/proof/TransferProof.cpp`, `src/local_secure_storage/impl/Apple.cpp`).

## Build Verification (orchestrator follow-up)

The orchestrator merged the worktree and ran the real build against the project's configured build (`build/OSX/Release`, sibling `thirdparty` deps already built):

- `cmake .` (reconfigure) picked up the new `add_subdirectory(trustedpeer)` cleanly.
- `cmake --build . --target trustedpeer` — **succeeded**, no compile errors or warnings.

No tests exist yet for this plan (Plan 02 adds them) — real test-level verification of TPR-01/TPR-02's behavior happens in Wave 2's build+test pass.

## Next Phase Readiness
- `TrustedPeerRegistry::New`, `SeedGenesis`, `ProposeMembershipChange`, `SignMembershipChange`, `TryConfirm`, `GetCurrentPeers` are all available for Plan 02's test scaffold (ceremony helper, quorum tests) and for Phase 11's real wiring.
- The library compiles cleanly against the real project build — no blocker remains for Plan 02.

---
*Phase: 10-trustedpeerregistry*
*Completed: 2026-07-24*
