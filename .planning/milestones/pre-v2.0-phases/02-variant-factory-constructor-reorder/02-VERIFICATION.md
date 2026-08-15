---
status: passed
phase: 2-variant-factory-constructor-reorder
verified: 2026-07-02
verifier: inline (orchestrator) — gsd-verifier subagent unavailable in this environment
score: 7/7 must_haves satisfied (6 static + 1 user-confirmed build/test)
approved_by_user: 2026-07-02
---

> **Status updated to `passed` on user approval (2026-07-02).** Build + `ctest` run in the
> user's environment confirmed green (`node_type_derivation_test` passes; regression subset
> `account_management_test`/`node_initialization_progress`/`utxo_manager_test`/
> `network_config_precedence_test` green — old factories retained, no behavior change). Two
> compile fixes were applied during execution (premature `*/` in a Doxygen block; bare
> `NodeType::Full` → `GeniusNode::NodeType::Full` in the test). Static verification was inline;
> compile/test execution confirmed by the operator.

# Phase 2 Verification — Variant Factory + Constructor Reorder

**Phase goal:** Collapse the 3 factories into `New(dev_config, AccountSource)`, reorder the
ctor (account after `LoadSgnsConfig`), add `NodeType` + `node_type` read, derive `is_full_node_`.
Old factories retained (D-01); deletion + migration is Phase 3.

## Verification method

`gsd-verifier` could not be spawned (`no such column: replacement_seq` subagent infrastructure
error, same as Phases 1). Verification performed inline by the orchestrator against the
post-commit source and the phase `must_haves`. Static checks are conclusive for 6 of 7
must_have truths; the 7th (tests pass) requires a full project compile that is
environment-blocked.

## must_haves — goal-backward check

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `New(dev_config, AccountSource)` is canonical; `AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>` with owned `std::string` payloads | ✓ Verified | new 2-arg `New` overload + `using AccountSource = std::variant<...>` in GeniusNode.hpp; 4 structs with `std::string` payloads |
| 2 | `NodeType` enum co-located with NodeState/Error; `NodeTypeFromString` case-insensitive, default Light on unknown/missing | ✓ Verified | `enum class NodeType` after `Error`; `NodeTypeFromString` lowercases then maps, returns `std::nullopt` on unknown; LoadSgnsConfig defaults Light + WARN |
| 3 | `LoadSgnsConfig()` reads `node_type` into `node_type_`; does NOT assign `is_full_node_` | ✓ Verified | `HasMember("node_type")` + `IsString()` read present; `is_full_node_ =` count inside LoadSgnsConfig = 0 |
| 4 | New ctor creates account via `std::visit` AFTER `LoadSgnsConfig` and derives `is_full_node_ = (node_type_ != Light)` | ✓ Verified | new ctor body order: LoadSgnsConfig → `is_full_node_ = (node_type_ != NodeType::Light)` → `account_ = std::visit(...)` (4 branches) → InitNetwork |
| 5 | `New(dev_config, AccountSource)` returns `nullptr` on failure (ctor throws, factory catches) | ○ Pending build | new factory wraps `new GeniusNode(...)` in `try`/`catch(...)` → `nullptr`; ctor throws `"Account creation failed"` on null account. Scene B of the test asserts this — not yet compiled/run. |
| 6 | Retained old factories + old private ctor keep param-driven `is_full_node` behavior unchanged (D-01) | ✓ Verified | `git diff` across the 4 commits: only 1 deleted line (the `is_full_node_` member decl → `= false` version); old `New(autodht,port_seed,is_full_node)`, `NewFromPrivateKey`, `NewFromMnemonic`, and old private ctor present and unchanged |
| 7 | Downstream consumers receive the bool unchanged (criterion #5) | ✓ Verified (static) | no downstream signature changes this phase; `is_full_node_` remains a `bool` passed to UTXOManager/TransactionManager/MigrationManager/GeniusAccount exactly as before |

## Requirements coverage

- **INTF-01** (canonical `New`): ✓ new 2-arg factory added.
- **INTF-02** (4-source variant, `FromPublicKey` promoted): ✓ `AccountSource` variant with all 4; `FromPublicKey` now public.
- **INTF-03** (ctor reorder — account after `LoadSgnsConfig`): ✓ new private ctor body order.
- **CFG-02** (`node_type` read + `NodeType` enum): ✓ enum + `NodeTypeFromString` + LoadSgnsConfig read.
- **CFG-03** (derived `is_full_node_`, set once, immutable): ✓ derived in the new ctor after LoadSgnsConfig; no setter; old ctor keeps param (transient).

## ROADMAP success criteria

1. ✓ `New(dev_config, AccountSource)` is canonical; 4-source variant; `FromPublicKey` public. (Old factories retained per D-01 — criterion #4 re-scoped.)
2. ✓ Private ctor is `(dev_config, AccountSource)`; account via `std::visit` after `LoadSgnsConfig`.
3. ✓ `LoadSgnsConfig` parses `node_type` via `NodeTypeFromString()`; `is_full_node_` derived (Full/Archive→true, Light→false).
4. ✓ (Re-scoped per D-01) Old factories **retained**; deletion moves to Phase 3.
5. ✓ Downstream consumers receive the bool unchanged.
- ○ Full build + CTest green — **pending user environment**.

## human_verification

Run in the user's build environment to close verification:

1. **Compile:** build `node_type_derivation_test` (pulls in `genius_node`). Expected: zero compile errors. Watch for: any translation unit referencing the old `New(autodht,port_seed,is_full_node)` still resolving (they should — old factories retained).
2. **New-factory test:** `ctest -R node_type_derivation_test --output-on-failure`. Expected: both `NodeTypeDerivation.ConfigDrivenCaseInsensitive` and `NodeTypeDerivation.NullptrOnAccountRestoreFailure` PASS.
3. **Regression (no behavior change for the 18 retained call sites):** `ctest -R 'account_management_test|node_initialization_progress|utxo_manager_test|network_config_precedence_test'`. Expected: all green — these use the retained old factories whose param-driven `is_full_node` path is untouched.

Commands are in `02-01-SUMMARY.md`.

## Blockers

- **Build environment not primed for in-session verification.** `build/local` has a stale/partial cache and zero compiled artifacts; `_THIRDPARTY_BUILD_DIR` is referenced throughout `CommonBuildParameters.cmake` but provided externally (not in-repo). A from-scratch configure+compile of this project is beyond this session's scope. `../thirdparty/build/OSX` is populated, so the user's environment can build it.

Environment limitation, **not** a code defect. Static evidence (byte-for-byte RapidJSON/spdlog pattern reuse; standard C++17 `std::visit`+`if constexpr`; CMake block mirroring the adjacent Phase-1 test) indicates low compile risk.

## Recommendation

Mark the phase **complete** once the user runs the three steps in `human_verification` and confirms green. If any step fails, route to `/gsd-plan-phase 2 --gaps` with the failure output.
