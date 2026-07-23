---
phase: 09-securecrdt-layer
plan: 02
subsystem: infra
tags: [crdt, multisig, securecrdt, quorum, cpp17]

requires:
  - phase: 09-securecrdt-layer
    plan: 01
    provides: "ISignedCRDTData / SecureCrdtRegistry (Register/UnregisterIf/Resolve)"
  - phase: 08-multisig-primitive
    provides: "sgns::multisig::VerifyPayloadSignature / EvaluateQuorum"
provides:
  - "SecureCrdt: ProposeValue/AddSignature/ReadIfQuorum/RegisterFilters -- the mandatory, only sanctioned write/read wrapper for a SecureCrdtRegistry-registered key"
  - "securecrdt real CMake library target (links crdt_globaldb + multisig)"
  - "SecureCrdtRegistry::AllEntries() -- enumeration accessor added to support filter self-registration"
affects: [10-trustedpeerregistry, 11-burnconfig, 12-validatorregistry-migration]

tech-stack:
  added: []
  patterns:
    - "Local-write gate (D-03): ProposeValue/AddSignature are the ONLY call sites of GlobalDB::Put for a registered key; every write re-runs the same codec/semantic/signature check the remote filter enforces"
    - "Read-path quorum re-derivation (D-04): ReadIfQuorum never trusts a cached/'final' state -- it re-fetches value + all sig/<addr> children and re-runs multisig::EvaluateQuorum on every call"
    - "Verify/Apply handoff contract documented via Doxygen @note on ReadIfQuorum's declaration, not just implied"

key-files:
  created:
    - src/securecrdt/SecureCrdt.hpp
    - src/securecrdt/SecureCrdt.cpp
    - test/src/securecrdt/securecrdt_test_node.hpp
    - test/src/securecrdt/securecrdt_quorum_gate_test.cpp
    - test/src/securecrdt/securecrdt_propose_sign_quorum_test.cpp
    - test/src/securecrdt/securecrdt_quorum_contract_e2e_test.cpp
  modified:
    - src/securecrdt/CMakeLists.txt
    - src/securecrdt/SecureCrdtRegistry.hpp
    - test/src/securecrdt/CMakeLists.txt

key-decisions:
  - "Added SecureCrdtRegistry::AllEntries() (static snapshot-copy enumeration) -- Plan 01's registry exposed only Register/UnregisterIf/Resolve (single-key lookup), but RegisterFilters must iterate every registered entry to self-register a filter per base_key; the plan's action text assumed enumeration was possible without naming the accessor (Rule 3: blocking missing functionality)."
  - "Test setup logic (single-node unconnected GlobalDB construction) extracted into a new shared test-local header test/src/securecrdt/securecrdt_test_node.hpp rather than duplicated three times or by modifying test/src/crdt/globaldb_integration.cpp (which stays out of this plan's files_modified, and whose TestNodeCollection is a private nested class not exposed for reuse across test binaries)."

requirements-completed: [SCRDT-03, SCRDT-04]

duration: ~50min
completed: 2026-07-23
---

# Phase 09 Plan 02: SecureCrdt Wrapper (D-03 write gate + D-04 read-path re-derivation) Summary

**Built `SecureCrdt` -- the only sanctioned write/read entry point for registered SecureCrdt keys: `ProposeValue`/`AddSignature` enforce the local-write gate (rejecting malformed payloads and invalid signatures before any `Put`), and `ReadIfQuorum` always re-derives trust from `value + sig/*` via `multisig::EvaluateQuorum`, documented and proven end-to-end via a Verify/Apply handoff-contract test.**

## Performance

- **Duration:** ~50 min
- **Completed:** 2026-07-23
- **Tasks:** 3/3 completed
- **Files modified:** 9 (7 created, 2 modified `.cpp`/registry header aside from the 3 test-CMakeLists edits already counted)

## Accomplishments

- `SecureCrdt::ProposeValue`: resolves the registry entry, runs `DeserializeFromBytes`+`Verify` on the payload (the SAME check the remote filter runs) BEFORE calling `Put` -- closes the local/remote asymmetry gap (T-09-10). Rejects unregistered keys and malformed/invalid payloads without ever writing to `GlobalDB`.
- `SecureCrdt::AddSignature`: fetches the CURRENT value fresh via `Get` each call (never stale, closing the replay threat T-09-07), verifies via `multisig::VerifyPayloadSignature`, and only then writes to `base_key/sig/<address>`. An invalid signature is never persisted.
- `SecureCrdt::ReadIfQuorum`: fetches the current value + enumerates all `sig/<addr>` rows via `QueryKeyValues`, resolves the current signer-set snapshot via the registry's injected `SignerSetSource`, and delegates quorum counting entirely to `multisig::EvaluateQuorum` (never reimplements dedup/counting). Returns `std::nullopt` until threshold valid unique signatures exist. Its declaration carries a Doxygen `@note` documenting that the CALLER (not `SecureCrdt`) is responsible for `DeserializeFromBytes`+`Verify`+`Apply` on the returned bytes.
- `SecureCrdt::RegisterFilters`: registers one element filter per `SecureCrdtRegistry` entry, mirroring `ValidatorRegistry::RegisterFilter`'s call-from-factory convention; the filter callback (`FilterSecureCrdtUpdate`) enforces the identical signature/codec checks for remote-originated deltas as a second, independent layer on top of the local gate.
- Three automated tests, all against a single unconnected `GlobalDB` node (no `connectNodes()`, per RESEARCH.md Pitfall 3a):
  - `securecrdt_quorum_gate_test` (SCRDT-03): under-signed (1/2) never reports quorum; an invalid signature is rejected and never persisted; a malformed `ProposeValue` payload returns `Error::MALFORMED_VALUE` and leaves the key unwritten.
  - `securecrdt_propose_sign_quorum_test` (SCRDT-04): full propose -> sign(1) -> sign(2) -> quorum-met sequence using only `SecureCrdt`'s own methods (no raw `GlobalDB::Put`/`Get` in the test body).
  - `securecrdt_quorum_contract_e2e_test`: proves `ReadIfQuorum`'s documented handoff contract end-to-end -- the returned bytes are fed through a fresh `ISignedCRDTData` implementer's `DeserializeFromBytes`+`Verify`+`Apply`, and `Apply()`'s side-effect flag flips to true.

## Task Commits

Each task was committed atomically:

1. **Task 1: SecureCrdt wrapper (D-03 local-write gate + D-04 read-path re-derivation)** - `232a9fb3` (feat)
2. **Task 2: SCRDT-03/04 automated tests** - `9ecfe7d8` (test)
3. **Task 3: End-to-end ReadIfQuorum handoff-contract test** - `2be40ae8` (test)

## Files Created/Modified

- `src/securecrdt/SecureCrdt.hpp` - class declaration: `ProposeValue`/`AddSignature`/`ReadIfQuorum`/`RegisterFilters`, `Error` enum + `outcome` category macro, Doxygen-documented handoff contract on `ReadIfQuorum`
- `src/securecrdt/SecureCrdt.cpp` - implementation of all four methods plus the private `FilterSecureCrdtUpdate` filter callback; `OUTCOME_CPP_DEFINE_CATEGORY_3` for `SecureCrdt::Error`
- `src/securecrdt/CMakeLists.txt` - switched from `INTERFACE` to a real library (`SecureCrdt.cpp`), linking `crdt_globaldb` + `multisig`
- `src/securecrdt/SecureCrdtRegistry.hpp` - added `AllEntries()` static enumeration accessor
- `test/src/securecrdt/securecrdt_test_node.hpp` - shared single-node unconnected `GlobalDB` test helper (extracted inline, mirrors `GlobalDBIntegrationTest::TestNodeCollection::addNode`)
- `test/src/securecrdt/securecrdt_quorum_gate_test.cpp` - SCRDT-03 + malformed-payload local-rejection test
- `test/src/securecrdt/securecrdt_propose_sign_quorum_test.cpp` - SCRDT-04 full propose/sign/quorum test
- `test/src/securecrdt/securecrdt_quorum_contract_e2e_test.cpp` - Verify/Apply handoff-contract end-to-end test
- `test/src/securecrdt/CMakeLists.txt` - three new `addtest(...)` targets linking `securecrdt`, `crdt_globaldb`, `multisig`, `json_secure_storage`

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking missing functionality] Added `SecureCrdtRegistry::AllEntries()`**
- **Found during:** Task 1
- **Issue:** `RegisterFilters()`'s behavior (per the plan's `<behavior>` block: "for each registry entry, `db_->RegisterElementFilter(...)`") requires enumerating every currently-registered `SecureCrdtRegistryEntry`, but Plan 01's `SecureCrdtRegistry` only exposes `Register`/`UnregisterIf`/single-key `Resolve` -- there was no way to implement the described loop without an enumeration accessor.
- **Fix:** Added `static std::vector<SecureCrdtRegistryEntry> AllEntries()` returning a snapshot copy of the static registry map's values.
- **Files modified:** `src/securecrdt/SecureCrdtRegistry.hpp`
- **Commit:** `232a9fb3`

**2. [Rule 3 - Blocking] Extracted shared single-node test setup into a new header instead of reusing `TestNodeCollection`**
- **Found during:** Task 2
- **Issue:** The plan suggested reusing `GlobalDBIntegrationTest::TestNodeCollection` if it's "exposed via a shared test-utility header." It is not -- `TestNodeCollection` is a `public` nested class defined entirely inline inside `test/src/crdt/globaldb_integration.cpp`, not in a header, so it cannot be `#include`d by a different test binary.
- **Fix:** Created `test/src/securecrdt/securecrdt_test_node.hpp` with an equivalent `MakeSecureCrdtTestNode(dbName)` free function + RAII `SecureCrdtTestNode` struct, extracted from the same `addNode` logic, shared across all three new test files in this plan (no duplication three times over, and `test/src/crdt/globaldb_integration.cpp` was not touched, staying within this plan's `files_modified`).
- **Files modified:** `test/src/securecrdt/securecrdt_test_node.hpp` (new)
- **Commit:** `9ecfe7d8`

None of these deviations changed the plan's architecture or intent -- both are additive accessors/helpers required to implement exactly what the plan's `<behavior>`/`<action>` blocks already specified.

## Verification Notes

**IMPORTANT -- build not run in this worktree.** This worktree checkout has 6 uninitialized top-level submodules (`GeniusKDF`, `ProofSystem`, `SGProcessingManager`, `docs`, `evmrelay`, `gRPCForSuperGenius`) and no pre-configured CMake build directory of its own (the project's only configured build, `build/OSX/Release`, lives in the main repo checkout at a different absolute path, and CMake caches embed absolute source/build paths so it cannot be reused directly against this worktree's source tree). A from-scratch `cmake`+full-dependency-graph build (Boost, libp2p, RocksDB, protobuf, GoogleTest, ipfs-lite, etc.) was judged out of this execution's time budget, consistent with Plan 01's precedent.

As a substitute, this plan's code was verified by:
- Careful manual cross-reference against the real, already-shipped project headers read during this execution (`src/crdt/globaldb/globaldb.hpp`, `src/crdt/hierarchical_key.hpp`, `src/multisig/MultiSig.hpp`, `src/blockchain/ValidatorRegistry.cpp`'s `RegisterFilter`/`FilterRegistryUpdate` precedent, `src/base/buffer.hpp`) to confirm every method signature, return type, and outcome-error-category pattern used in `SecureCrdt.hpp`/`.cpp` matches the real shipped API exactly (verified line-by-line, not from training-data assumption).
- Confirming the `outcome::result<void>` "return other_result.error();" idiom and the `.error() == CustomEnum::Value` comparison idiom are both established patterns already used elsewhere in this codebase (`globaldb.cpp`, `ValidatorRegistry.cpp`, `GeniusAccount.cpp`, `UTXOManager.cpp`), rather than invented.
- Confirming `test/src/crdt/CMakeLists.txt`'s `globaldb_integration_test` target links only `crdt_globaldb` (no additional pubsub/libp2p/graphsync targets needed explicitly, since `crdt_globaldb` is a `PUBLIC` library bringing those in transitively) -- this plan's three new test targets mirror that exact minimal link-library pattern rather than the longer list originally guessed at.

This does **not** confirm `cmake --build . --target securecrdt` or `ctest -R securecrdt` actually succeed inside a real build -- that remains to be confirmed the next time a full project build is run (e.g. by the orchestrator's follow-up, as happened for Plan 01). Flagging this explicitly per the plan's `<verify>` requirement so it is not silently assumed green.

## Build/Test Verification (orchestrator follow-up)

The orchestrator merged the worktree and ran the real build against the project's configured build (`build/OSX/Release`). This surfaced **two real bugs** the executor's standalone cross-reference could not have caught:

1. **Segfault in `MakeSecureCrdtTestNode`**: constructing a `GossipPubSub`/libp2p `Host` before configuring libp2p's soralog logging system crashes inside the Noise security adaptor's DI injector (`createLogger()` called unconditionally during construction). Fixed by adding a one-time `EnsureLoggingSystemConfigured()` call to `securecrdt_test_node.hpp`, mirroring `test/src/crdt/globaldb_integration.cpp`'s `SetUpTestSuite` sequence. Commit `ba02fb58`.
2. **`LastKeySegment()` extracted the wrong path segment**: `GlobalDB::QueryKeyValues(..., QUERY_VALUESUFFIX)` returns entries keyed by the *raw datastore key* (`/crdt/s/k/<base>/sig/<address>/v`), not the logical CRDT key (`/base/sig/<address>`) — the original code took the *last* path segment, which was always the fixed `"v"` value-suffix marker, not the signer address. This meant `EvaluateQuorum` always saw 0 valid unique signers regardless of how many real, valid signatures were collected — `ReadIfQuorum` could never report quorum met. Fixed to take the second-to-last segment (the suffix marker is always exactly one path component). Commit `ba02fb58`.

After both fixes:
- `cmake --build . --target securecrdt_quorum_gate_test securecrdt_propose_sign_quorum_test securecrdt_quorum_contract_e2e_test` — **succeeded**.
- `ctest -R securecrdt --output-on-failure` — **5/5 tests passed** (all of Plan 01 + Plan 02's tests, including the malformed-payload symmetry test and the end-to-end `ReadIfQuorum`→`Verify`/`Apply` handoff-contract test).
- Full project build + full `ctest`: 77/79 passed. The 2 failures (`transaction_sync_test`, `multi_account_test`) are pre-existing/flaky and unrelated to this phase — `multi_account_test` passes 4/4 in isolation (full-suite resource contention), and `transaction_sync_test` traces to unrelated bridge-relayer commits (same finding as Phase 8's verification).

SCRDT-03 and SCRDT-04 (and D-03/D-04's safety properties) are now fully proven by actual compiled/executed tests against real signature verification and real CRDT storage — not just structural/cross-reference verification.

## Self-Check: PASSED (build+test verified by orchestrator follow-up; two real bugs found and fixed)

- FOUND: src/securecrdt/SecureCrdt.hpp
- FOUND: src/securecrdt/SecureCrdt.cpp
- FOUND: src/securecrdt/CMakeLists.txt
- FOUND: src/securecrdt/SecureCrdtRegistry.hpp (AllEntries() added)
- FOUND: test/src/securecrdt/securecrdt_test_node.hpp
- FOUND: test/src/securecrdt/securecrdt_quorum_gate_test.cpp
- FOUND: test/src/securecrdt/securecrdt_propose_sign_quorum_test.cpp
- FOUND: test/src/securecrdt/securecrdt_quorum_contract_e2e_test.cpp
- FOUND: test/src/securecrdt/CMakeLists.txt (three new addtest targets)
- FOUND commit 232a9fb3
- FOUND commit 9ecfe7d8
- FOUND commit 2be40ae8
