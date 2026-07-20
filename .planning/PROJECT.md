# SuperGenius

## What This Is

SuperGenius is a C++17 blockchain/crypto platform providing an account system (UTXO + DAG), consensus, a processing grid for distributed compute tasks, an EVM bridge, and a JSON-RPC + WebSocket API. It targets native node operators (full/light/archive) and ships cross-platform keystore support (Android NDK / iOS). The primary entry point and orchestration facade is `GeniusNode` in `src/account/`.

## Current Milestone: v1.1 Multi-Signature Secure CRDT Storage

**Goal:** Add a decoupled multi-signature component and a secure CRDT storage layer so specific CRDT-backed values can only be created/updated when signed by a quorum of authorized peers — applied first to a new `TrustedPeerRegistry` (genesis-seeded, quorum-updatable) and to make `BURN_BASIS_POINTS` a quorum-signed, live-updatable CRDT value instead of a hardcoded constant.

**Target features:**
- `ISignedCRDTData`-style interface: per-type classes implement `Verify()`/`Apply()`, reusing `ConsensusAuth`'s signing-bytes/SHA-256/`VerifySignature` primitives (not `ConsensusManager`'s proposal/vote/certificate machinery)
- Static topic/regex → policy registry (signer-set source + quorum rule + payload codec) declared in code
- Propose/sign/quorum flow transported entirely over CRDT itself (pending-value + signature entries, filter-callback pattern like `ValidatorRegistry`), no new networking/RPC
- New `TrustedPeerRegistry`: genesis-seeded initial set (hardcoded in genesis config), N-of-M configurable quorum from CURRENT members to add/remove/replace a member
- `BURN_BASIS_POINTS` becomes a `TrustedPeerRegistry`-quorum-signed CRDT value; `TransactionManager` caches it and refreshes via CRDT-change callback (no live CRDT read per `PayEscrow` call)
- `ValidatorRegistry` migrated onto the new `ISignedCRDTData` interface (reusing the abstraction, not just `BURN_BASIS_POINTS`)

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

<!-- v1.1 milestone scope. Hypotheses until shipped. -->

- [ ] `ISignedCRDTData`-style interface exists: per-type classes implement `Verify()`/`Apply()`, reusing `ConsensusAuth`'s signing-bytes/SHA-256/`VerifySignature` primitives
- [ ] Static topic/regex → policy registry (signer-set source + quorum rule + payload codec) declared in code
- [ ] Propose/sign/quorum flow transported entirely over CRDT (pending-value + signature entries, filter-callback pattern like `ValidatorRegistry`) — no new networking/RPC
- [ ] New `TrustedPeerRegistry`: genesis-seeded initial set (hardcoded in genesis config), N-of-M configurable quorum from CURRENT members to add/remove/replace a member
- [ ] `BURN_BASIS_POINTS` becomes a `TrustedPeerRegistry`-quorum-signed CRDT value; `TransactionManager` caches it and refreshes via CRDT-change callback (no live CRDT read per `PayEscrow` call)
- [ ] `ValidatorRegistry` migrated onto the new `ISignedCRDTData` interface

### Out of Scope

<!-- Explicit boundaries to prevent scope creep. -->

- `ConsensusManager` changes / pluggable voter sources — CRDT itself carries propose/sign/quorum messages, no consensus proposal/vote/certificate lifecycle involved
- Any new pubsub/RPC transport — reuse existing CRDT put/filter-callback machinery
- Unrelated consensus refactors
- Propagating the `NodeType` enum into `TransactionManager` (60+ `full_node_m` refs), `UTXOManager`, `MigrationManager` — deferred from v1.0; still out of scope
- Distinct runtime behavior between `Archive` and `Full` — deferred from v1.0; still out of scope
- New node roles beyond Full/Light/Archive (e.g. Validator/Bootstrap) — not introduced here
- Migration tooling for old on-disk config files — defaults cover it; no schema-version migration

## Context

**Current State (v1.0 — shipped 2026-07-03):** The GeniusNode construction-refactor milestone is complete. `New(dev_config, AccountSource)` is the sole public factory; `auto_dht`/`port_seed`/`node_type` are config-driven; `is_full_node_` is derived from `NodeType` in a reordered ctor (init-order hinge fixed); all ~25 call sites migrated; old factories deleted. Full build + CTest green; no behavior change for default/pre-existing configs. GSD subagent runtime was broken this milestone — all plan/execute/verify ran inline via the workflow's documented fallbacks.

**Note:** Between v1.0 and v1.1, a substantial body of bridge-relayer work (RPC endpoint wiring, burn detection, conflict/replay hardening, E2E integration, P2P burn-event gossip, deferred validation lifecycle — `.planning/phases/01` through `07`) was planned and executed directly without being tracked as a formal GSD milestone. `PROJECT.md`/`MILESTONES.md` were not reconciled against that work before starting v1.1; this is a known documentation gap, not a description of v1.1's own scope.

**v1.1 Goal:** Add a decoupled multi-signature component and secure CRDT storage layer so specific CRDT-backed values require quorum signatures to create/update — first applied to a new `TrustedPeerRegistry` and to `BURN_BASIS_POINTS` (currently a hardcoded constant in `TransactionManager.hpp:52`, with a comment already anticipating this: "Eventually settable via multisig CRDT config; hardcoded default until then"). Existing precedent to build from: `ValidatorRegistry` (`src/blockchain/ValidatorRegistry.hpp`) already does signature+quorum-gated CRDT updates; `ConsensusAuth.hpp` has the reusable signing-bytes/SHA-256/verify primitives. Key design decision from milestone questioning: do NOT route through `ConsensusManager` (voter/weight source is hardwired to a single `ValidatorRegistry` instance, not pluggable per proposal kind) — instead use CRDT's own put/filter-callback mechanism as the transport for proposals and signatures, same pattern `ValidatorRegistry` already uses.

**Brownfield.** A full codebase map exists at `.planning/codebase/` (STACK, ARCHITECTURE, STRUCTURE, CONVENTIONS, TESTING, INTEGRATIONS, CONCERNS — 2,039 lines). Key facts informing this refactor:

- `GeniusNode` is a god-class facade (`src/account/GeniusNode.cpp` is 2,831 lines) — see `.planning/codebase/CONCERNS.md`.
- `network_config.json` is already parsed in `InitNetwork()` (`GeniusNode.cpp:768`): holds `pubsub_port`, `pubsub_bind_address`, `bootstrap_addresses`, `upnp_enabled`, `high_water`/`low_water`, reconnect config. Adding `autodht` + `base_port` is an incremental extension.
- `sgns_config.json` is already parsed in `LoadSgnsConfig()` (`GeniusNode.cpp:251`): holds `is_processor`, `net_id`, `subnet_id`, `bootstrap_fullnodes`, `authorized_full_node`. Adding `node_type` is an incremental extension; tests already write this file.
- `is_full_node` is overloaded: it gates connection watermarks (`400/200` vs `300/150`), UTXO address-filtering, the `GNUS_FULL_NODES_TOPIC` subscription, and several migration/account paths.
- Coding conventions: C++17, `snake_case_` for private members, `std::shared_ptr` factory pattern, RapidJSON for config parsing, Doxygen `@param` docs on public API. See `.planning/codebase/CONVENTIONS.md`.
- Tests use CTest; see `.planning/codebase/TESTING.md`.

**External consumers:** none known beyond this repo's `example/` and `test/`. The factory is treated as an internal API; a breaking change with full call-site migration is acceptable.

## Constraints

- **Tech stack**: C++17, CMake, RapidJSON, Boost, libp2p, git submodules — no new dependencies this milestone.
- **Compatibility**: deployed nodes have `network_config.json` / `sgns_config.json` **without** the new keys — they must keep working via defaults; no hard-fail on missing keys.
- **Non-functional**: no behavior change for existing configurations from earlier milestones.
- **Scope boundary (v1.1)**: no `ConsensusManager` changes; no new pubsub/RPC transport — CRDT put/filter-callback is the only transport for proposals/signatures.
- **Scope boundary (v1.0, still holds)**: the `NodeType` enum stops at the `GeniusNode` boundary (derived bool passed downstream).

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| v1.1: Reuse `ConsensusAuth` primitives directly (signing-bytes/SHA-256/`VerifySignature`), not `ConsensusManager`'s proposal/vote/certificate lifecycle | `ConsensusManager`'s voter/weight source is hardwired to a single `ValidatorRegistry` instance per manager, not pluggable per proposal kind — extending it is bigger scope than needed | — Pending |
| v1.1: Propose/sign/quorum flow transported over CRDT itself (pending-value + signature entries via filter callbacks), no new networking | `ValidatorRegistry` already proves this pattern works for signature+quorum-gated CRDT updates; avoids building new RPC/gossip machinery | — Pending |
| v1.1: `ISignedCRDTData` interface-based per-type classes (not a generic `SignedCRDTValue<T>` template) | Matches `ValidatorRegistry`'s existing per-type `Verify()`/`Apply()` style; less abstraction risk for the first two instances (`TrustedPeerRegistry`, `BURN_BASIS_POINTS`) | — Pending |
| v1.1: `TrustedPeerRegistry` is separate from `ValidatorRegistry`'s consensus voter set | Validator consensus roles and "who can sign economic-parameter changes" are different concerns; genesis-seeded, quorum-updatable from its own current membership | — Pending |
| v1.1: `BURN_BASIS_POINTS` cached in `TransactionManager`, refreshed via CRDT-change callback | Avoids a CRDT read on every `PayEscrow` call while still picking up quorum-signed updates promptly | — Pending |
| v1.0: `node_type` lives in `sgns_config.json`, not as a constructor param | Node role is a deployment-time concern, not a per-call concern; `sgns_config.json` already drives `is_processor` and other role-ish fields | Phase 2 ✓ (read via `NodeTypeFromString`, case-insensitive, default Light) |
| v1.0: `autodht` + `base_port` live in `network_config.json` | They are network-layer settings; `network_config.json` already holds the adjacent knobs (`pubsub_port`, watermarks, reconnect) | Phase 1 ✓ (reads added; `base_port` renamed to `port_seed`) |
| v1.0: Keep `is_full_node_` as a derived bool, do not propagate enum downstream | `TransactionManager` has 60+ `full_node_m` refs; propagation is a separate, larger refactor | Phase 2 ✓ (derived in the reordered ctor; downstream keeps the bool) |
| v1.0: Single `New(dev_config, AccountSource)` with `std::variant` | One entry point, self-documenting, forward-compatible for new account sources; eliminates 3 near-duplicate factories | Phase 2 ✓ (canonical factory + variant added; old factories deleted Phase 3) |
| v1.0: `Archive` and `Full` both map to `is_full_node_=true` for now | Distinguishing them is a future behavior change; introduce the vocabulary now, wire behavior later | — Pending |
| v1.0: Defaults: `autodht=true`, `base_port=40001`, `node_type=Light` | Match today's factory default args so deployed configs behave identically when keys are absent | Phase 2 ✓ |

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
*Last updated: 2026-07-20 — milestone v1.1 (Multi-Signature Secure CRDT Storage) started*
