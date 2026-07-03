# SuperGenius

## What This Is

SuperGenius is a C++17 blockchain/crypto platform providing an account system (UTXO + DAG), consensus, a processing grid for distributed compute tasks, an EVM bridge, and a JSON-RPC + WebSocket API. It targets native node operators (full/light/archive) and ships cross-platform keystore support (Android NDK / iOS). The primary entry point and orchestration facade is `GeniusNode` in `src/account/`.

This milestone is an **interface refactor of `GeniusNode`** — not new product capability. It cleans up the node construction API and moves runtime knobs into configuration files where they belong.

## Core Value

**Constructing a `GeniusNode` must be a single, self-documenting call driven by config files** — no more overloaded factory methods carrying boolean network/role flags that are really config concerns. If this refactor lands clean and all 18 call sites compile and tests pass, the milestone succeeds.

## Requirements

### Validated

<!-- Already working in the existing codebase. Inferred from .planning/codebase/. -->

- ✓ Multi-account creation (new / from-private-key / from-mnemonic / from-public-key) via `GeniusAccount` factories — existing
- ✓ UTXO + DAG account system with address-based filtering (`UTXOManager`) — existing
- ✓ Blockchain genesis, consensus manager, validator registry — existing
- ✓ Processing grid (tasks/subtasks) over pubsub channels — existing
- ✓ EVM bridge via event watcher (`src/watcher/`) — existing
- ✓ JSON-RPC + WebSocket API transport (`src/api/`) — existing
- ✓ Runtime config via `network_config.json` (`InitNetwork`) and `sgns_config.json` (`LoadSgnsConfig`) — existing
- ✓ Cross-platform keystore (Android `KeyStoreHelper.java`, iOS) — existing
- ✓ CRDT datastore with migration managers — existing
- ✓ `auto_dht` + `port_seed` (renamed from `base_port`) read from `network_config.json` in `InitNetwork()` — config-wins precedence, safe defaults, port-resolution Doxygen (CFG-01, CFG-04) — **Validated in Phase 1**
- ✓ `node_type` read from `sgns_config.json` in `LoadSgnsConfig()` (case-insensitive `NodeTypeFromString`, default Light) → `NodeType` enum; `is_full_node_` derived (Full/Archive→true, Light→false) in the reordered ctor; canonical `New(dev_config, AccountSource)` variant factory with `FromPublicKey` public (CFG-02, CFG-03, INTF-01, INTF-02, INTF-03) — **Validated in Phase 2** *(old factories retained until Phase 3 deletion per D-01)*
- ✓ All ~25 `NewFromPrivateKey` call sites migrated to `New(dev_config, FromPrivateKey{...})`; old factories + old private ctor deleted (INTF-04); shared `WriteNetworkConfig`/`WriteSgnsConfig` helpers; full build + CTest green (MIG-01, MIG-02, MIG-03, MIG-04) — **Validated in Phase 3**

### Active

<!-- This milestone's scope. Hypotheses until shipped. — All v1 requirements VALIDATED (Phases 1-3). -->

- [x] `autodht` and `base_port` are read from `network_config.json` (in `InitNetwork`), not passed as constructor/factory params
- [x] `node_type` ("Full" / "Light" / "Archive") is read from `sgns_config.json` (in `LoadSgnsConfig`); a `NodeType` enum is introduced
- [x] `GeniusNode::is_full_node_` becomes a **derived** bool (Full/Archive → true, Light → false), sourced from `node_type`; downstream consumers (`UTXOManager`, `TransactionManager`, `MigrationManager`, `GeniusAccount`) keep the existing bool, unchanged
- [x] Three factories (`New`, `NewFromPrivateKey`, `NewFromMnemonic`) collapse into a single `New(dev_config, AccountSource)` where `AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>`
- [x] `FromPublicKey` (currently internal-only at `src/account/GeniusNode.cpp:1405`) is promoted to a public variant option (watch-only / read-only)
- [x] All 18 call sites of `NewFromPrivateKey` (1 in `example/node_test/`, 17 across `test/src/{account,node,blockchain,transaction_sync,processing_multi,processing_nodes,multiaccount}/`) migrate to the new `New()` API
- [x] Each migrated test writes its `autodht` / `base_port` / `node_type` into the appropriate config file (the `sgns_config.json` write pattern is already established in tests)
- [x] Existing config files without the new keys keep working via sensible defaults (`autodht=true`, `base_port=40001`, `node_type=Light`)
- [x] Full build passes and existing tests remain green after the refactor

### Out of Scope

<!-- Explicit boundaries to prevent scope creep. -->

- Propagating the `NodeType` enum into `TransactionManager` (60+ `full_node_m` refs), `UTXOManager`, `MigrationManager` — deferred; the derived bool stays this milestone
- Distinct runtime behavior between `Archive` and `Full` — both map to `is_full_node_=true` for now; the `Archive` value exists for forward compatibility only
- Any change to consensus, processing grid, EVM bridge, or API transport logic
- New node roles beyond Full/Light/Archive (e.g. Validator/Bootstrap) — not introduced here
- Rewriting `DevConfig_st` or the dev-config plumbing — only the `GeniusNode` construction surface changes
- Migration tooling for old on-disk config files — defaults cover it; no schema-version migration

## Context

**Current State (v1.0 — shipped 2026-07-03):** The GeniusNode construction-refactor milestone is complete. `New(dev_config, AccountSource)` is the sole public factory; `auto_dht`/`port_seed`/`node_type` are config-driven; `is_full_node_` is derived from `NodeType` in a reordered ctor (init-order hinge fixed); all ~25 call sites migrated; old factories deleted. Full build + CTest green; no behavior change for default/pre-existing configs. GSD subagent runtime was broken this milestone — all plan/execute/verify ran inline via the workflow's documented fallbacks.

**Next Milestone Goals:** TBD — run `/gsd-new-milestone`. Candidate work (deferred to v2, see `.planning/milestones/v1.0-REQUIREMENTS.md`): `NodeType` downstream propagation (PROP-01), distinct Archive-vs-Full runtime behavior (PROP-02), `pubsub_port` numeric cleanup (HARD-01), config schema versioning (HARD-02). Also consider restoring the GSD subagent runtime before the next milestone.

**Brownfield.** A full codebase map exists at `.planning/codebase/` (STACK, ARCHITECTURE, STRUCTURE, CONVENTIONS, TESTING, INTEGRATIONS, CONCERNS — 2,039 lines). Key facts informing this refactor:

- `GeniusNode` is a god-class facade (`src/account/GeniusNode.cpp` is 2,831 lines) — see `.planning/codebase/CONCERNS.md`.
- `network_config.json` is already parsed in `InitNetwork()` (`GeniusNode.cpp:768`): holds `pubsub_port`, `pubsub_bind_address`, `bootstrap_addresses`, `upnp_enabled`, `high_water`/`low_water`, reconnect config. Adding `autodht` + `base_port` is an incremental extension.
- `sgns_config.json` is already parsed in `LoadSgnsConfig()` (`GeniusNode.cpp:251`): holds `is_processor`, `net_id`, `subnet_id`, `bootstrap_fullnodes`, `authorized_full_node`. Adding `node_type` is an incremental extension; tests already write this file.
- `is_full_node` is overloaded: it gates connection watermarks (`400/200` vs `300/150`), UTXO address-filtering, the `GNUS_FULL_NODES_TOPIC` subscription, and several migration/account paths.
- Coding conventions: C++17, `snake_case_` for private members, `std::shared_ptr` factory pattern, RapidJSON for config parsing, Doxygen `@param` docs on public API. See `.planning/codebase/CONVENTIONS.md`.
- Tests use CTest; see `.planning/codebase/TESTING.md`.

**External consumers:** none known beyond this repo's `example/` and `test/`. The factory is treated as an internal API; a breaking change with full call-site migration is acceptable.

## Constraints

- **Tech stack**: C++17, CMake, RapidJSON, Boost, libp2p, git submodules — no new dependencies this milestone (use existing `std::variant` + RapidJSON).
- **Compatibility**: deployed nodes have `network_config.json` / `sgns_config.json` **without** the new keys — they must keep working via defaults; no hard-fail on missing keys.
- **Non-functional**: no behavior change for existing configurations — pure interface/config-location refactor. Tests stay green.
- **Scope boundary**: the `NodeType` enum stops at the `GeniusNode` boundary this milestone (derived bool passed downstream).

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| `node_type` lives in `sgns_config.json`, not as a constructor param | Node role is a deployment-time concern, not a per-call concern; `sgns_config.json` already drives `is_processor` and other role-ish fields | Phase 2 ✓ (read via `NodeTypeFromString`, case-insensitive, default Light) |
| `autodht` + `base_port` live in `network_config.json` | They are network-layer settings; `network_config.json` already holds the adjacent knobs (`pubsub_port`, watermarks, reconnect) | Phase 1 ✓ (reads added; `base_port` renamed to `port_seed`). Factory params still exist (additive) — collapse deferred to Phase 2/3 |
| Keep `is_full_node_` as a derived bool, do not propagate enum downstream | `TransactionManager` has 60+ `full_node_m` refs; propagation is a separate, larger refactor. Enum introduced at the boundary now, deep migration later | Phase 2 ✓ (derived in the reordered ctor; downstream keeps the bool) |
| Single `New(dev_config, AccountSource)` with `std::variant` | One entry point, self-documenting, forward-compatible for new account sources; eliminates 3 near-duplicate factories | Phase 2 ✓ (canonical factory + variant added; old factories retained until Phase 3 deletion per D-01) |
| No compatibility shim — migrate all 18 call sites directly | Only this repo consumes the factory; a shim would just delay the cleanup. Tests already write `sgns_config.json`, so the config-write pattern is established | Phase 3 ✓ (all ~25 call sites migrated + old factories deleted) |
| `Archive` and `Full` both map to `is_full_node_=true` for now | Distinguishing them is a future behavior change; introduce the vocabulary now, wire behavior later | — Pending |
| Defaults: `autodht=true`, `base_port=40001`, `node_type=Light` | Match today's factory default args so deployed configs behave identically when keys are absent | Phase 2 ✓ (all three defaults verified: `auto_dht=true`/`port_seed=40001` Phase 1, `node_type=Light` Phase 2) |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-07-03 after Phase 3 completion (milestone v1 complete — all 3 phases shipped green)*
