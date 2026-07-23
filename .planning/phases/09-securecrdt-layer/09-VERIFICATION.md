---
phase: 09-securecrdt-layer
verified: 2026-07-23T00:00:00Z
status: passed
score: 4/4 must-haves verified
overrides_applied: 0
---

# Phase 9: SecureCRDT Layer Verification Report

**Phase Goal:** Registered CRDT keys can only be created/updated when accompanied by quorum-verified signatures, using CRDT's own put/filter-callback mechanism as the sole transport — no unsigned or under-signed write is ever applied.
**Verified:** 2026-07-23
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth (from ROADMAP success criteria) | Status | Evidence |
|---|------|--------|----------|
| 1 | `ISignedCRDTData` interface exists; a concrete implementer can supply payload codec + `Verify()`/`Apply()`, compiles/links independent of any specific data type | ✓ VERIFIED | `src/securecrdt/ISignedCRDTData.hpp` — pure abstract, 4 methods (`SerializeToBytes`, `DeserializeFromBytes`, `Verify`, `Apply`), zero CRDT/GlobalDB includes. `securecrdt_interface_test` (ctest) passes. |
| 2 | Static, code-declared registry maps topic/key patterns to {signer-set source, quorum rule, `ISignedCRDTData` type}, resolvable at startup for a given key | ✓ VERIFIED | `src/securecrdt/SecureCrdtRegistry.hpp` — `Register`/`UnregisterIf`/`Resolve`/`AllEntries`, regex `"/?" + key_pattern + "(/sig/.*)?"` resolves both base key and `sig/<addr>` children. `securecrdt_registry_test` passes. |
| 3 | Propose+sign+quorum sequence driven entirely by CRDT puts + filter callbacks results in value applied only after quorum, no new networking/RPC | ✓ VERIFIED | `SecureCrdt::ProposeValue`/`AddSignature`/`ReadIfQuorum` use only `GlobalDB::Put`/`Get`/`QueryKeyValues`/`RegisterElementFilter`. `securecrdt_propose_sign_quorum_test` proves 1-of-2 sigs never reports quorum, 2-of-2 does; test body contains zero raw `db_->Put`/`Get` calls (grep confirms only via `SecureCrdt`'s own API). |
| 4 | Unsigned/under-signed write attempt to a registered key rejected locally (never applied) before quorum reached, verified by automated test | ✓ VERIFIED | `securecrdt_quorum_gate_test`: 1 valid signature (< threshold 2) → `ReadIfQuorum` returns `nullopt`; invalid/tampered signature → `AddSignature` returns `Error::INVALID_SIGNATURE` and is never persisted; malformed `ProposeValue` payload → `Error::MALFORMED_VALUE`, `db_->Get` confirms nothing was written. |

**Score:** 4/4 truths verified (matches SCRDT-01..04)

### Independent Re-run Evidence (not trusting SUMMARY claims)

Rebuilt against the project's real configured build and ran the tests myself:

```
cd build/OSX/Release
cmake --build . --target securecrdt_interface_test securecrdt_registry_test \
  securecrdt_quorum_gate_test securecrdt_propose_sign_quorum_test securecrdt_quorum_contract_e2e_test -j8
ctest -R securecrdt --output-on-failure
```

Result:
```
1/5 securecrdt_interface_test .............   Passed    0.03 sec
2/5 securecrdt_registry_test ..............   Passed    0.04 sec
3/5 securecrdt_quorum_gate_test ...........   Passed    3.21 sec
4/5 securecrdt_propose_sign_quorum_test ...   Passed    1.62 sec
5/5 securecrdt_quorum_contract_e2e_test ...   Passed    1.62 sec
100% tests passed, 0 tests failed out of 5
```

This independently confirms the SUMMARY's "5/5 pass" claim — not merely trusted from the document.

### LastKeySegment() Fix Verification (Requested Item #1)

Read `src/securecrdt/SecureCrdt.cpp`'s current implementation:

```cpp
std::string LastKeySegment( const std::string &key )
{
    sgns::crdt::HierarchicalKey hk( key );
    auto list = hk.GetList();
    if ( list.size() < 2 ) return {};
    return list[list.size() - 2];
}
```

Traced `HierarchicalKey::GetList()` (`src/crdt/impl/hierarchical_key.cpp`): splits the key on `/` and drops the first (empty, from the leading `/`) element. For a real raw datastore key such as `/crdt/s/k/<base>/sig/<address>/v` (the shape `QueryKeyValues(..., QUERY_VALUESUFFIX)` actually returns, per the git commit message and `crdt_data_filter`/`crdt_datastore` conventions), `GetList()` yields `["crdt","s","k","<base>","sig","<address>","v"]`. `list[size-2]` is `"<address>"` — correct. The fix is position-relative (second-to-last), so it is robust regardless of how many path segments precede `sig/<address>`, as long as the trailing `/v` value-suffix marker is always exactly one segment — which the code comment and commit message assert is the case for this datastore. **Confirmed correct** for realistic key shapes, and confirmed correct empirically: `securecrdt_quorum_gate_test`/`securecrdt_propose_sign_quorum_test`/`securecrdt_quorum_contract_e2e_test` all depend on `ReadIfQuorum` correctly detecting 2-of-2 real signer addresses via this exact code path, and all pass.

### D-03 Sole Write-Path Verification (Requested Item #2)

```
grep -n "db_->Put" src/securecrdt/SecureCrdt.cpp
```
Only two matches: line 81 inside `ProposeValue` and line 121 inside `AddSignature`. `ReadIfQuorum` (lines 134-190) and `RegisterFilters`/`FilterSecureCrdtUpdate` (lines 192-259) contain **zero** `Put` calls. A repo-wide search confirms no other file (outside `src/securecrdt/`) references `SecureCrdtRegistry`/`securecrdt::` at all — Phases 10-12 (the only future consumers) haven't been built yet, so there is currently no other code path in the repo that could bypass the wrapper. **Confirmed**: `ProposeValue`/`AddSignature` are the sole call sites, and this holds after the recent bug fixes (the fixes touched `LastKeySegment` and the test-node logging init only, not the write paths).

### D-04 No-Final-Write / Reader Re-derivation Verification (Requested Item #3)

`ReadIfQuorum` (SecureCrdt.cpp:134-190): on every call it re-fetches the current value via `db_->Get(base_key)` (line 145), re-queries all `sig/*` children via `db_->QueryKeyValues` (line 153), re-resolves the signer set via `entry->signer_set_source(...)` (line 170), and re-runs `multisig::EvaluateQuorum` (line 178) — no caching, no "final" key is ever written or read anywhere in the file. Confirmed no method other than `ProposeValue`/`AddSignature` calls `Put`, so no final/terminal marker write exists. This matches D-04 exactly and is exercised by `securecrdt_propose_sign_quorum_test` (quorum state changes from not-met to met purely by adding a second signature, re-derived fresh each `ReadIfQuorum` call, no separate "commit" step).

### Requirements Coverage (SCRDT-01..04)

| Requirement | Description | Status | Evidence |
|---|---|---|---|
| SCRDT-01 | `ISignedCRDTData` interface | ✓ SATISFIED | `ISignedCRDTData.hpp`, `securecrdt_interface_test` |
| SCRDT-02 | Static registry {pattern → signer-source, quorum, type} | ✓ SATISFIED | `SecureCrdtRegistry.hpp`, `securecrdt_registry_test` |
| SCRDT-03 | Writes to registered keys require quorum; under-signed rejected locally | ✓ SATISFIED | `SecureCrdt.cpp` gate logic, `securecrdt_quorum_gate_test` (real signatures, real rejection, real non-persistence) |
| SCRDT-04 | Propose/sign/quorum entirely via CRDT puts + filter callbacks, no new networking | ✓ SATISFIED | `securecrdt_propose_sign_quorum_test` + `securecrdt_quorum_contract_e2e_test`, only `GlobalDB` primitives used |

### Anti-Patterns / Suspicious-Pattern Scan

- No `TODO`/`FIXME`/`XXX`/`HACK`/placeholder markers found in `src/securecrdt/*.hpp`/`.cpp`.
- `grep -rn "QueryKeyValues" src --include=*.cpp` (outside securecrdt): **no other production call sites** exist yet. This means the exact bug class that caused the quorum-detection failure (raw-datastore-key-shape assumption) has no other current consumers to silently share the same mistake — the risk is contained to this one function, which is now fixed and covered by 3 tests that exercise it with real signer addresses end-to-end. This is a reasonable mitigation, though Phases 10-12 will introduce new `SecureCrdt`/`SecureCrdtRegistry` consumers and should re-run these same tests against their own registered key shapes as a regression check (recommended, not required for this phase's pass/fail).
- `ReadIfQuorum`'s silent-`nullopt`-on-`db_->Get`-error path (line 145-150) does not currently distinguish "key truly absent" from "other GlobalDB error" — both are treated as "not yet proposed." This is consistent with the phase's stated behavior (`std::nullopt` for no-value-yet) and is not a gap against SCRDT-01..04, but is worth flagging as a minor robustness note for future phases (an unexpected DB error would silently look identical to "not proposed yet" rather than surfacing as a distinct error).

## Human Verification Required

None. All four success criteria are backed by automated, independently-re-run tests against the real compiled build (not SUMMARY claims).

## Gaps Summary

None. Both real bugs found during the orchestrator's build-verification pass (test-fixture logging segfault, `LastKeySegment` address-extraction bug) are fixed, committed (`ba02fb58`), and now covered by regression-proof passing tests that specifically exercise real multi-signer quorum detection. Independent re-run in this verification confirms 5/5 securecrdt tests pass against the actual project build. Phase 9's four Success Criteria and SCRDT-01..04 are genuinely satisfied by the current shipped code.

---

_Verified: 2026-07-23_
_Verifier: Claude (gsd-verifier)_
