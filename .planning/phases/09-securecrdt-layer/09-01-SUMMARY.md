---
phase: 09-securecrdt-layer
plan: 01
subsystem: infra
tags: [crdt, multisig, registry, interface, cpp17]

requires:
  - phase: 08-multisig-primitive
    provides: "sgns::multisig::VerifyPayloadSignature / EvaluateQuorum standalone primitives (no CRDT/node dependency)"
provides:
  - "ISignedCRDTData: header-only, per-type virtual interface (SerializeToBytes/DeserializeFromBytes/Verify/Apply)"
  - "SecureCrdtRegistry: static Register/UnregisterIf/Resolve mapping a base_key regex pattern to {SignerSetSource, make_instance factory}"
  - "securecrdt INTERFACE CMake target (links multisig) wired into src/CMakeLists.txt"
  - "securecrdt_interface_test / securecrdt_registry_test wired into test/src/CMakeLists.txt"
affects: [09-02-securecrdt-wrapper, 10-trustedpeerregistry, 11-burnconfig, 12-validatorregistry-migration]

tech-stack:
  added: []
  patterns:
    - "Static function-local-static registry with compare-and-remove UnregisterIf (mirrors IInputValidator)"
    - "Regex pattern compiled once at Register() time as \"/?\" + key_pattern + \"(/sig/.*)?\" (mirrors CRDTDataFilter::RegisterElementFilter)"

key-files:
  created:
    - src/securecrdt/ISignedCRDTData.hpp
    - src/securecrdt/SecureCrdtRegistry.hpp
    - src/securecrdt/CMakeLists.txt
    - test/src/securecrdt/securecrdt_interface_test.cpp
    - test/src/securecrdt/securecrdt_registry_test.cpp
    - test/src/securecrdt/CMakeLists.txt
  modified:
    - src/CMakeLists.txt
    - test/src/CMakeLists.txt

key-decisions:
  - "SecureCrdtRegistryEntry carries an owner_token (const void*) field, set by the caller before Register(), so UnregisterIf's compare-and-remove has something concrete to compare against — the plan's action text described the compare-and-remove idiom but did not name a token field, so one was added to the struct to make it constructible (Rule 2: missing critical functionality for the described behavior)."

patterns-established:
  - "Header-only per-type ISignedCRDTData implementers, no template — same style as IInputValidator"

requirements-completed: [SCRDT-01, SCRDT-02]

duration: ~35min
completed: 2026-07-23
---

# Phase 09 Plan 01: SecureCRDT Interface + Registry Foundations Summary

**Established `ISignedCRDTData` (per-type payload codec + Verify/Apply) and static `SecureCrdtRegistry` (regex-keyed policy resolution with injectable SignerSetSource), both header-only, zero GlobalDB/CRDT/node dependency.**

## Performance

- **Duration:** ~35 min
- **Completed:** 2026-07-23
- **Tasks:** 2/2 completed
- **Files modified:** 8 (6 created, 2 modified)

## Accomplishments
- `ISignedCRDTData` interface with exactly the four required pure-virtual methods (`SerializeToBytes`, `DeserializeFromBytes`, `Verify`, `Apply`), no template — matches `IInputValidator`'s per-type style per milestone decision.
- `SecureCrdtRegistry` static registry: `Register`/`UnregisterIf`/`Resolve`, with `SignerSetSource` as an injectable `std::function` (not hard-wired to a not-yet-existing `TrustedPeerRegistry`), and regex resolution covering both the base key and any `sig/<addr>` child.
- `securecrdt` CMake INTERFACE target wired into the build (`src/CMakeLists.txt`), linking `multisig`.
- Two ctest targets (`securecrdt_interface_test`, `securecrdt_registry_test`) covering SCRDT-01 and SCRDT-02 respectively, wired into `test/src/CMakeLists.txt`.

## Task Commits

Each task was committed atomically:

1. **Task 1: Define ISignedCRDTData interface + SecureCrdtRegistry static registry** - `0dcd9b85` (feat)
2. **Task 2: Wave-0 tests for SCRDT-01/SCRDT-02 + test build wiring** - `cbff1b81` (test)

## Files Created/Modified
- `src/securecrdt/ISignedCRDTData.hpp` - abstract interface: payload codec + Verify()/Apply(), no quorum check inside Apply()
- `src/securecrdt/SecureCrdtRegistry.hpp` - static Register/UnregisterIf/Resolve over a regex-keyed map; `SignerSetSnapshot`/`SignerSetSource` types
- `src/securecrdt/CMakeLists.txt` - `securecrdt` INTERFACE library linking `multisig`
- `src/CMakeLists.txt` - added `add_subdirectory(securecrdt)` after `multisig`
- `test/src/securecrdt/securecrdt_interface_test.cpp` - `TestSignedData` concrete subclass; asserts Verify true/false + malformed-input rejection
- `test/src/securecrdt/securecrdt_registry_test.cpp` - Register/Resolve/UnregisterIf incl. `sig/<addr>` child resolution and token compare-and-remove
- `test/src/securecrdt/CMakeLists.txt` - `addtest(...)` for both new targets, linking `securecrdt`
- `test/src/CMakeLists.txt` - added `add_subdirectory(securecrdt)` after `multisig`

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical functionality] Added `owner_token` field to `SecureCrdtRegistryEntry`**
- **Found during:** Task 1
- **Issue:** The plan's action text specifies `UnregisterIf(key_pattern, expected_token)` using a "compare-and-remove idiom... using a caller-supplied opaque token stored alongside the entry" but the `SecureCrdtRegistryEntry` struct definition in the same action text did not list a token field, making the described comparison impossible to implement as literally specified.
- **Fix:** Added `const void *owner_token = nullptr;` to `SecureCrdtRegistryEntry`; callers set it in the entry passed to `Register()`, and `UnregisterIf` compares against it.
- **Files modified:** `src/securecrdt/SecureCrdtRegistry.hpp`
- **Commit:** `0dcd9b85`

## Verification Notes

The full CMake/ctest build of this repository (Boost, libp2p, RocksDB, protobuf, etc. as submodule dependencies) was not feasible to configure from a clean worktree checkout within this execution's time budget — no pre-configured build directory existed for this worktree, and a from-scratch configure/build of the full dependency graph is a multi-hour operation unrelated to this plan's two-header change.

As a substitute verification, both new headers and both new test files were compiled and run standalone:
- `ISignedCRDTData.hpp` + `SecureCrdtRegistry.hpp` verified with `c++ -std=c++17 -fsyntax-only` against a byte-for-byte copy of their real logic (with a minimal `outcome::result<T>` stand-in substituted only for the actual `libp2p`/Boost-backed `outcome::result`, since that dependency requires the full submodule build).
- `securecrdt_interface_test.cpp` and `securecrdt_registry_test.cpp` were compiled and executed against the actual project header sources (`src/securecrdt/*.hpp`, unmodified) using a minimal GoogleTest-macro-compatible shim (`TEST`, `EXPECT_*`, `ASSERT_*`) since GoogleTest itself is only available via the project's vendored submodule build. All assertions passed (`ALL_TESTS_RAN`, no failure lines printed) for both files, individually compiled and run.

This confirms the header logic and test assertions are correct against the shipped interface, but does **not** confirm the `ctest -R securecrdt` invocation itself succeeds inside this repository's actual CMake/GoogleTest build — that remains to be confirmed the next time a full project build is run (e.g., by CI or a subsequent plan's build step). Flagging this explicitly per the plan's `<verify>` requirement (`cmake --build . --target securecrdt`, `ctest -R securecrdt`) so it is not silently assumed green.

## Build/Test Verification (orchestrator follow-up)

The orchestrator merged the worktree and ran the real build against the project's configured build (`build/OSX/Release`):

- `cmake .` (reconfigure) picked up the new `add_subdirectory(securecrdt)` in both `src/CMakeLists.txt` and `test/src/CMakeLists.txt` cleanly.
- `cmake --build . --target securecrdt_interface_test securecrdt_registry_test` — **succeeded**, both targets compile and link against the real project headers/GoogleTest/CMake build (no stand-ins).
- `ctest -R securecrdt --output-on-failure` — **2/2 tests passed** (`securecrdt_interface_test`, `securecrdt_registry_test`).

SCRDT-01 and SCRDT-02 are now fully proven by actual compiled/executed tests, not just the executor's standalone-shim verification.

## Self-Check: PASSED (build+test verified by orchestrator follow-up)

- FOUND: src/securecrdt/ISignedCRDTData.hpp
- FOUND: src/securecrdt/SecureCrdtRegistry.hpp
- FOUND: src/securecrdt/CMakeLists.txt
- FOUND: test/src/securecrdt/securecrdt_interface_test.cpp
- FOUND: test/src/securecrdt/securecrdt_registry_test.cpp
- FOUND: test/src/securecrdt/CMakeLists.txt
- FOUND commit 0dcd9b85
- FOUND commit cbff1b81
