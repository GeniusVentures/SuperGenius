# Architecture Research — GeniusNode Construction Refactor

**Date:** 2026-07-02
**Scope:** Component boundaries, data flow, init ordering, and build order for the refactored construction API.

> Existing system architecture is in `.planning/codebase/ARCHITECTURE.md`. This doc covers ONLY the construction-flow changes.

## The Critical Finding: Init-Order Chicken-and-Egg

Today's flow (in the factory, BEFORE the constructor body runs):

```
New(dev_config, autodht, base_port, is_full_node)
  └─ GeniusAccount::New(..., is_full_node)      ← account created with is_full_node KNOWN
  └─ new GeniusNode(dev_config, account, autodht, base_port, is_full_node)
       └─ ctor body:
            line 233: LoadSgnsConfig()           ← reads sgns_config.json
            line 235: InitNetwork(base_port, is_full_node_)
```

After the refactor, `is_full_node` is **no longer a constructor param** — it's derived from `node_type` read inside `LoadSgnsConfig()`. But the account is still created with `is_full_node`. **Problem:** the account is created before `LoadSgnsConfig` resolves `node_type`.

### Recommended resolution: defer account creation into the constructor

Move the `AccountSource` variant into the constructor and create the account AFTER config is loaded:

```
New(dev_config, AccountSource source)           ← thin wrapper
  └─ new GeniusNode(dev_config, source)          ← pass variant, not account
       └─ ctor body:
            [setup loggers etc.]
            LoadSgnsConfig()                     ← resolves node_type_ → is_full_node_
            account_ = std::visit(..., source)   ← create account NOW, is_full_node_ known
            InitNetwork()                        ← reads base_port/autodht from network_config.json itself
```

**Why this is correct:**
- `node_type_` and the derived `is_full_node_` are resolved before the account is created.
- `base_port` and `autodht` are local to `InitNetwork()` (they're only used there — see `GeniusNode.cpp:768-918`), so reading them directly from `network_config.json` inside `InitNetwork` needs no member storage and no ordering concern.
- The `New()` factory stays a thin `shared_ptr` wrapper (preserves the existing factory idiom and `BeginDBInitialization()` call).

**Trade-off:** the constructor now owns account creation (previously the factory did). This is acceptable — it centralizes config resolution. The private constructor signature changes from `(dev_config, account, autodht, base_port, is_full_node)` to `(dev_config, AccountSource)`.

## Component Boundaries

```
┌─────────────────────────────────────────────────────────────┐
│  New(dev_config, AccountSource source)   [public factory]   │
│  - thin: constructs GeniusNode, calls BeginDBInitialization │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  GeniusNode(dev_config, AccountSource)   [private ctor]     │
│                                                             │
│   1. logger/openssl setup                                   │
│   2. LoadSgnsConfig()  ──► node_type_ ──► is_full_node_     │
│   3. std::visit(...)  ──► account_   (needs is_full_node_)  │
│   4. InitNetwork()    ──► reads base_port + autodht locally │
│                         from network_config.json            │
│   5. LoadCrdtConfig()                                       │
└─────────────────────────────────────────────────────────────┘
```

### Where things live

| Concern | Location | Notes |
|---------|----------|-------|
| `AccountSource` variant + alternative structs | new small header `src/account/AccountSource.hpp` | Keeps `GeniusNode.hpp` from growing; easily includable by call sites. |
| `NodeType` enum + `NodeTypeFromString` | `src/account/GeniusNode.hpp` (alongside `NodeState`/`Error` at lines 129/143) | Matches existing enum co-location. |
| `node_type_` member | `GeniusNode.hpp` member block (near `is_full_node_` at line 662) | New private member. |
| `is_full_node_` member | unchanged (line 662) | Now derived, not ctor-initialized. |
| `base_port`/`autodht` | NO member — local vars in `InitNetwork` | They're only used in `InitNetwork` today. |
| Config reads | `LoadSgnsConfig` (node_type) + `InitNetwork` (base_port, autodht) | Both already parse their respective JSON files. |

## Data Flow

```
network_config.json                sgns_config.json
   │ base_port (uint, default 40001)   │ node_type (string, default "Light")
   │ autodht   (bool, default true)    │
   ▼                                   ▼
InitNetwork() local vars           LoadSgnsConfig()
   │                                   │ node_type_  = NodeTypeFromString(...)
   │                                   │ is_full_node_ = (node_type_ != Light)
   │                                   ▼
   │                              std::visit(account source) ──► account_
   ▼
[pubsub/graphsync/dht setup with base_port, autodht, is_full_node_]
```

**Direction:** config file → loader method → private member (or local var) → consumer. Single-directional; no back-flow.

## Suggested Build Order (informs roadmap phases)

Dependency-driven. Each later step assumes the earlier ones compile.

1. **Foundation (no behavior change):** Introduce `NodeType` enum + `NodeTypeFromString` + `AccountSource` variant header. Pure additive — compiles, nothing uses it yet.
2. **Config reads:** Add `node_type` parsing to `LoadSgnsConfig` (sets `node_type_` + derived `is_full_node_`); add `base_port`/`autodht` parsing to `InitNetwork` (local vars, still also accept the params temporarily). Old ctor params still present but now redundant.
3. **Constructor reorder + new ctor signature:** Switch private ctor to `(dev_config, AccountSource)`; move account creation into ctor body after `LoadSgnsConfig`. `New()` factory takes `(dev_config, AccountSource)`.
4. **Delete old factories:** Remove `New(autodht, base_port, is_full_node)`, `NewFromPrivateKey`, `NewFromMnemonic`. `New()` is the only entry point.
5. **Migrate call sites:** Rewrite 18 `NewFromPrivateKey(...)` calls to `New(dev_config, FromPrivateKey{...})`; each test writes `autodht`/`base_port`/`node_type` into the right config file.
6. **Verify:** Full build + CTest green; no behavior change for default configs.

**Granularity note:** With `coarse` granularity (config setting), steps 1–4 naturally collapse into one "interface refactor" phase and step 5 into a "call-site migration" phase. Steps 1+2 could also be one phase. The dependency above is what the planner must respect.

## Respects Scope Boundary

This architecture **does not** change `UTXOManager`, `TransactionManager`, `MigrationManager`, or `GeniusAccount` signatures. They continue to receive the `bool is_full_node` exactly as today — the only difference is that the bool is now derived inside `GeniusNode` from `node_type_` rather than threaded from a constructor param. No downstream propagation this milestone.
