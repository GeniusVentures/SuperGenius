# Features Research — GeniusNode Construction Refactor

**Date:** 2026-07-02
**Scope:** Capabilities the refactored interface must/should/must-not deliver.

## Table Stakes (MUST have — without these the refactor is worse than before)

| Feature | Complexity | Notes |
|---------|-----------|-------|
| Single `New(dev_config, AccountSource)` entry point | Low | Replaces 3 factories (`GeniusNode.hpp:82,98,115`). All existing call sites go through it. |
| `AccountSource` variant covers NewAccount / FromPrivateKey / FromMnemonic / FromPublicKey | Low | `FromPublicKey` promoted from internal-only (`GeniusNode.cpp:1405`) to public variant option. |
| `autodht` read from `network_config.json` in `InitNetwork` | Low | Default `true` when absent (matches today's default arg). |
| `base_port` read from `network_config.json` in `InitNetwork` | Low | Default `40001`. Use numeric `IsUint()` field, not the string style of `pubsub_port` (`GeniusNode.cpp:791`). |
| `node_type` ("Full"/"Light"/"Archive") read from `sgns_config.json` in `LoadSgnsConfig` | Medium | New `NodeType` enum; sets derived `is_full_node_` (Full/Archive → true, Light → false). |
| Default-on-missing-key for all new config reads | Low | Deployed configs lack the new keys; must behave identically to today. |
| INFO-log every resolved config value | Low | Match existing `"sgns_config.json: is_processor={}"` style. |
| All 18 call sites migrated (1 example/ + 17 test/) | Medium | No compat shim. Each test writes its own config files. |
| Full build + existing tests green | Medium | No behavior change for existing configs. |
| Doxygen docs updated on the new public API | Low | Match existing `@param`/`@brief` convention. |

## Differentiators (NICE to have — make the interface notably better)

| Feature | Complexity | Notes |
|---------|-----------|-------|
| Named-struct variant alternatives (`FromPrivateKey{key_hex}`) | Low | Self-documenting call sites; recommended in STACK.md. |
| `NodeTypeFromString()` helper with logged fallback | Low | Robust to typos; logs unrecognized values. |
| Visitor-based dispatch via `std::visit` + `if constexpr` | Low | Exhaustive handling; future-proof. |
| WARN-log on unrecognized `node_type` string (not silent fallback) | Low | Operator visibility; better than silent default. |
| Consolidate the account-creation ordering so config loads once | Medium | See ARCHITECTURE.md — resolve chicken-and-egg cleanly. |
| Deprecation comments removed cleanly (no dead code left) | Low | The 3 old factories are deleted, not stubbed. |

## Anti-Features (Deliberately NOT build this milestone)

| Anti-Feature | Why Excluded |
|--------------|--------------|
| Compatibility shim / deprecated wrappers forwarding old factories | Project decision: only this repo consumes the factory; a shim just delays cleanup. Defeats the milestone's purpose. |
| Propagating `NodeType` enum into `TransactionManager`/`UTXOManager`/`MigrationManager`/`GeniusAccount` | 60+ refs in `TransactionManager` alone; separate larger milestone. `is_full_node_` stays a derived bool at the GeniusNode boundary this round. |
| Distinct `Archive` vs `Full` runtime behavior | Both → `is_full_node_=true` today. Vocabulary introduced now, behavior later. |
| Schema validation framework / JSON schema for config files | Out of scope; `HasMember + IsXxx` guards suffice. |
| Changing `DevConfig` plumbing | Only the `GeniusNode` construction surface changes. |
| Migrating `pubsub_port` from string to numeric | Out of scope; only adding new keys. |
| Additional node roles (Validator/Bootstrap/Watcher) | Not introduced; Full/Light/Archive only. |
| On-disk config schema versioning / migration tooling | Defaults cover backward compat; no version field. |
| Refactoring the `GeniusNode` god-class itself (2831-line file) | Separate concern; this milestone only touches the construction API. |

## Feature Dependencies

```
NodeType enum + NodeTypeFromString
        │
        ▼
node_type read in LoadSgnsConfig  ──►  derived is_full_node_
        │                                       │
        ▼                                       ▼
(resolve init-order; see ARCHITECTURE.md)   account creation needs it
        │
autodht/base_port read in InitNetwork  (independent of node_type)
        │
AccountSource variant + New(dev_config, source)
        │
        ▼
Migrate 18 call sites  (depends on new New() existing)
        │
        ▼
Build + tests green
```

Key dependency: **account creation needs `is_full_node_`, which now comes from config loaded inside the constructor.** This forces a reorder — see ARCHITECTURE.md. This is the single most important design decision of the milestone and the likely first phase.
