# Research Summary — GeniusNode Construction Refactor

**Date:** 2026-07-02
**Synthesizes:** STACK.md, FEATURES.md, ARCHITECTURE.md, PITFALLS.md

> Note: Research was produced inline (sequentially) because the subagent runtime returned a schema error (`no such column: replacement_seq`) on all 4 parallel researcher spawns. Content quality is unaffected — findings are grounded in actual codebase reads.

## One-Sentence Framing

Collapse three overloaded `GeniusNode::New*` factories into a single `std::variant`-driven `New(dev_config, AccountSource)`, and relocate the `autodht`/`base_port`/`node_type` knobs from constructor params into the existing JSON config files — with `NodeType` (Full/Light/Archive) introduced at the GeniusNode boundary and a derived `is_full_node_` bool passed unchanged to downstream consumers.

## Stack — Key Recommendation

- **`std::variant` + `std::visit` with `if constexpr` chain** for account-source dispatch. C++17 (codebase is `CMAKE_CXX_STANDARD 17`). Named aggregate structs as alternatives (`FromPrivateKey{key_hex}`).
- **Reuse existing RapidJSON pattern** (`HasMember && IsXxx && GetXxx`). `base_port` as numeric `IsUint()` (not the string style of `pubsub_port`).
- **`NodeType` enum** co-located with existing `NodeState`/`Error` enums; `NodeTypeFromString()` helper with WARN-on-unrecognized.
- **Avoid:** `std::any`, inheritance hierarchies, config frameworks, tagged structs.

## Table Stakes (must deliver)

1. Single `New(dev_config, AccountSource)` — replaces `New`, `NewFromPrivateKey`, `NewFromMnemonic` (`GeniusNode.hpp:82,98,115`).
2. Variant covers all 4 sources incl. `FromPublicKey` (promoted from `GeniusNode.cpp:1405`).
3. `autodht` + `base_port` from `network_config.json`; `node_type` from `sgns_config.json`.
4. Derived `is_full_node_` (Full/Archive → true, Light → false); downstream consumers unchanged.
5. Defaults identical to today: `autodht=true`, `base_port=40001`, `node_type=Light`.
6. All 18 call sites migrated; no shim. Full build + tests green.

## Architecture — The Pivotal Decision

**Init-order chicken-and-egg:** the account needs `is_full_node_`, but post-refactor that's derived from `node_type` loaded in `LoadSgnsConfig()` inside the constructor. **Resolution:** move `AccountSource` into the constructor and create the account AFTER `LoadSgnsConfig()`. `New()` becomes a thin wrapper; the private ctor signature changes to `(dev_config, AccountSource)`. `base_port`/`autodht` become local vars in `InitNetwork()` (no member storage — they're only used there).

**Data flow:** config file → loader → member/local → consumer. Single-directional.

**Scope respected:** enum stops at GeniusNode boundary; `UTXOManager`/`TransactionManager`/`MigrationManager`/`GeniusAccount` signatures unchanged.

## Build Order → Likely Phase Decomposition

(coarse granularity config → expect ~2-3 phases)

1. **Foundation + config reads** (additive): `NodeType` enum, `NodeTypeFromString`, `AccountSource.hpp` variant, parse `node_type` in `LoadSgnsConfig`, parse `base_port`/`autodht` in `InitNetwork`. Old ctor params still present.
2. **Constructor reorder + factory collapse:** private ctor `(dev_config, AccountSource)`; account created post-config-load; delete the 3 old factories; `New()` is the sole entry point.
3. **Call-site migration + verification:** rewrite 18 sites; tests write config files; full build + CTest green; grep-clean of old signatures.

## Watch Out For (top risks)

- **P1 — Init-order drift** (account created before `node_type` resolved). Mitigation: create account inside ctor after `LoadSgnsConfig`. *Highest risk.*
- **P2 — Silent default mismatch** for deployed configs missing new keys. Mitigation: byte-identical defaults + explicit missing-key test.
- **P3 — Missed call sites** (no shim to catch you). Mitigation: compiler enforces it; add a grep-verification step.
- **P5 — Non-exhaustive variant visitor.** Mitigation: `std::visit` + `if constexpr`; unit-test all 4 alternatives.
- **P6 — `node_type_`/`is_full_node_` desync.** Mitigation: derive once, treat as immutable post-construction.

## Anti-Features (explicitly out of scope)

No compat shim · no downstream enum propagation (60+ TransactionManager refs) · no Archive-vs-Full behavior split · no schema validation framework · no `DevConfig` changes · no `pubsub_port` string→numeric cleanup · no new node roles.

## Feeds Into

- **Requirements:** the table stakes map directly to v1 REQs; anti-features map to Out of Scope.
- **Roadmap:** build order above suggests ~2-3 phases (config/coarse). The init-order reorder is the dependency hinge.
- **Planning:** each phase's PLAN.md should reference the relevant Pitfall→Phase matrix entries as verification criteria.
