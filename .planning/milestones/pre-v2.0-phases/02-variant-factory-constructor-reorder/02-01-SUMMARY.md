---
plan: 01
phase: 2-variant-factory-constructor-reorder
status: code-complete-build-pending
committed: 2026-07-02
requirements: [INTF-01, INTF-02, INTF-03, CFG-02, CFG-03]
---

# Plan 01 Summary — Variant Factory + Constructor Reorder

## What was built

The canonical `New(dev_config, AccountSource)` factory + `AccountSource` variant, a reordered
private constructor that creates the account via `std::visit` **after** `LoadSgnsConfig()`
resolves `node_type_` → `is_full_node_` (the init-order hinge fix), the `NodeType` enum +
case-insensitive `NodeTypeFromString()`, and the `node_type` config read. `New()` preserves
`nullptr`-on-failure. **Old factories + old private ctor retained unchanged (D-01)** so the 18
unmigrated call sites keep their behavior and the build stays green; deletion + migration is
Phase 3.

## Key files

**Modified:**
- `src/account/GeniusNode.hpp` — `NodeType` enum (Full/Light/Archive, co-located with NodeState/Error); `AccountSource` variant + 4 owned-`std::string` source structs (`NewAccount`/`FromPrivateKey`/`FromMnemonic`/`FromPublicKey`); new public `New(dev_config, AccountSource)` overload; new private ctor `(dev_config, AccountSource)` declaration; `node_type_` member (default Light); `IsFullNode()`/`GetNodeType()` getters; `is_full_node_` gained a defensive `= false` in-class initializer; `#include <variant>`.
- `src/account/GeniusNode.cpp` — `NodeTypeFromString()` (anon namespace, case-insensitive → `std::optional<NodeType>`); `node_type` read in `LoadSgnsConfig()` (sets `node_type_` only, default Light + WARN); reordered private ctor (LoadSgnsConfig → derive `is_full_node_` → `std::visit` account creation with null-guard throw → InitNetwork); new public `New(dev_config, AccountSource)` (try/catch → nullptr); `IsFullNode()`/`GetNodeType()` definitions; `#include <cctype>`.

**Created:**
- `test/src/account/node_type_derivation_test.cpp` — Scene A (`New(dev_config, FromPrivateKey{key})` + `sgns_config {"node_type":"full"}` → `IsFullNode()==true`, `GetNodeType()==Full`; proves case-insensitive derivation) and Scene B (`FromPrivateKey{"invalid"}` → `nullptr`; proves D-04).
- `test/src/account/CMakeLists.txt` — registered `node_type_derivation_test` (addtest + `genius_node` + Apple `-force_load` whole-archive block, mirroring the Phase-1 registration).

## Commits

- `7a949350` refactor(02-01): NodeType enum, AccountSource variant, new factory+ctor declarations
- `d6ad9292` feat(02-01): NodeTypeFromString + node_type read in LoadSgnsConfig + getters
- `9df31d46` feat(02-01): reordered AccountSource constructor + canonical New() factory
- `159f171c` test(02-01): node_type_derivation_test + CMake registration

## Deviations

- **`is_full_node_` gained an in-class `= false` initializer** (not explicitly in the plan). The new ctor assigns `is_full_node_` in the body (after `LoadSgnsConfig`); the in-class default guarantees no uninitialized window and silences static analyzers. The old ctor's init-list (`is_full_node_(is_full_node)`) still overrides it, so old-factory behavior is unchanged.
- No other deviations. `FromPublicKey.public_address` is passed as the `public_key` arg to `GeniusAccount::NewFromPublicKey` (vocabulary mismatch noted in CONTEXT D-03; value flows through unchanged).

## Self-Check

Static verification (all PASS — grep/code-review; no compile required):
- ✓ `NodeType` enum present (co-located with NodeState/Error); `AccountSource` variant with 4 owned-`std::string` structs
- ✓ `NodeTypeFromString` case-insensitive (lowercase-normalize), returns `std::optional<NodeType>`, `nullopt` on unknown
- ✓ `LoadSgnsConfig` reads `node_type` → `node_type_` (default Light on missing/unknown + WARN); does **not** assign `is_full_node_` (verified: 0 matches in LoadSgnsConfig body)
- ✓ New ctor derives `is_full_node_ = (node_type_ != NodeType::Light)` exactly once, then `std::visit` creates the account (4 `if constexpr` branches), then throws on null account
- ✓ New `New(dev_config, AccountSource)` wraps `new GeniusNode(...)` in `try`/`catch(...)` → `nullptr`
- ✓ Old factories (`New(autodht,port_seed,is_full_node)`, `NewFromPrivateKey`, `NewFromMnemonic`) + old private ctor retained unchanged — `git diff` across the 4 commits shows only 1 deleted line (the `is_full_node_` member decl, replaced with the `= false` version). The 18 existing call sites are unaffected.
- ✓ Test modeled on the Phase-1 harness; CMake registration mirrors the adjacent `network_config_precedence_test` block

**NOT verified (environment-blocked):**
- ✗ Full project compile (`genius_node` + new test) — `build/local` has no compiled artifacts and `_THIRDPARTY_BUILD_DIR` is externally provided (not in-repo). Same constraint as Phase 1.
- ✗ `ctest -R node_type_derivation_test` and the regression subset (`account_management_test`, `node_initialization_progress`, `utxo_manager_test`) — pending the compile above.

The C++ follows the existing RapidJSON/Doxygen/spdlog conventions and the `std::visit`+`if constexpr` idiom is standard C++17, so compile risk is low — but **the build/test must run in the user's environment to close verification.**

## Build/test commands (for the user's environment)

```bash
# Use your normal configure invocation + _THIRDPARTY_BUILD_DIR; build the new target:
cmake --build <your-build-dir> --target node_type_derivation_test -j
ctest --test-dir <your-build-dir> -R node_type_derivation_test --output-on-failure
# Regression (old factories retained — these must stay green, proving no behavior change):
ctest --test-dir <your-build-dir> -R 'account_management_test|node_initialization_progress|utxo_manager_test|network_config_precedence_test' --output-on-failure
```
