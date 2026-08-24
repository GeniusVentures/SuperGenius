# SuperGenius

## What This Is

SuperGenius is a C++17 blockchain/crypto platform providing an account system (UTXO + DAG), consensus, a processing grid for distributed compute tasks, an EVM bridge, and a JSON-RPC + WebSocket API. It targets native node operators (full/light/archive) and ships cross-platform keystore support (Android NDK / iOS). The primary entry point and orchestration facade is `GeniusNode` in `src/account/`.

This milestone rebuilds bridge-mint finality from the `develop` baseline. Competing proposals for one external burn must converge on a single canonical finality slot without treating CRDT callback timing or a local message-delivery flag as protocol authority.

## Core Value

**One external burn must produce at most one authoritative certificate and one mint effect, even when proposals, certificates, and CRDT data arrive in different orders or nodes restart.**

## Current Milestone: v3.0 Canonical Burn Finality Rebuild

**Goal:** Implement a minimal, generic canonical-slot certificate path on `develop`, with a durable one-vote-per-slot lock, slot-keyed certificate authority, and safe publication recovery.

**Target features:**
- Canonical external-burn slot identity shared by every competing mint proposal
- Deterministic winner/finality rules that preserve certificate-to-proposal binding
- Persisted local vote locks that prohibit a second usable vote for a slot until matching finality or vote expiry
- Generic slot-keyed certificate storage, persistence-before-advertisement, and safe publication recovery
- Multi-node regression coverage for contention, delayed CRDT delivery, publisher loss, restart, and exactly-once minting

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
- ✓ Canonical bridge-burn identity is shared by competing Mint proposals and derived from verified burn facts, never proposer address or nonce — **Validated in Phase 8**
- ✓ Certificate publication has deterministic verifiable authority, persistence-before-advertisement, and safe failover — **Validated in Phase 10**
- ✓ Local completion, PubSub receipt, CRDT synchronization, and restart recovery converge through one durable canonical-certificate path; honest validators produce one same-slot winner — **Validated in Phases 9–11**

### Active

<!-- This milestone's scope. Hypotheses until shipped. -->

- [ ] A certified burn is minted exactly once across multi-node contention, delayed propagation, publisher loss, and restart.

### Out of Scope

<!-- Explicit boundaries to prevent scope creep. -->

- Propagating the `NodeType` enum into `TransactionManager` (60+ `full_node_m` refs), `UTXOManager`, `MigrationManager` — deferred; the derived bool stays this milestone
- Distinct runtime behavior between `Archive` and `Full` — both map to `is_full_node_=true` for now; the `Archive` value exists for forward compatibility only
- Any change to consensus, processing grid, EVM bridge, or API transport logic
- New node roles beyond Full/Light/Archive (e.g. Validator/Bootstrap) — not introduced here
- Rewriting `DevConfig_st` or the dev-config plumbing — only the `GeniusNode` construction surface changes
- Migration tooling for old on-disk config files — defaults cover it; no schema-version migration
- Porting, rebasing, or repairing the rejected Phase 9–12 implementation — its design and dependencies are reference material only, not a source of production code
- A local `DeliverySource` flag as proof of certificate authorship or CRDT write authority — local call provenance is neither network-verifiable nor durable
- Broad TransactionManager, CRDT, registry, or persistence refactors that are not required by the canonical-finality contract

## Context

**Current State:** v3.0 rebuilt canonical slot identity, durable one-vote finality, slot-keyed publication, and convergent certificate/Mint recovery from the `develop` baseline. Phase 12 remains to prove those guarantees across production-path multi-node faults; the old exploratory worktree remains forensic reference only.

**Observed failure:** Different mint proposals for the same external burn used different source/nonce identities and could independently reach certificate quorum. The exploratory fix made certificates slot-keyed, but allowed every PubSub recipient to write the same CRDT key. Its follow-up avoided writes from non-local ingress by treating `DeliverySource::Local` as the author, which stranded receivers waiting for an unverified presumed author.

**Required design boundary:** Canonical-slot competition, certificate authority, publication/failover, durable vote locking, and application idempotency must be specified as one protocol contract. The certificate store is generic and keyed by canonical slot, not a bridge-only finality side channel. The finality path cannot use a local callback source as authorization, and receiver behavior must remain live if the initial publisher fails.

**Brownfield.** A full codebase map exists at `.planning/codebase/` (STACK, ARCHITECTURE, STRUCTURE, CONVENTIONS, TESTING, INTEGRATIONS, CONCERNS — 2,039 lines). Key facts informing this refactor:

- `GeniusNode` is a god-class facade (`src/account/GeniusNode.cpp` is 2,831 lines) — see `.planning/codebase/CONCERNS.md`.
- `network_config.json` is already parsed in `InitNetwork()` (`GeniusNode.cpp:768`): holds `pubsub_port`, `pubsub_bind_address`, `bootstrap_addresses`, `upnp_enabled`, `high_water`/`low_water`, reconnect config. Adding `autodht` + `base_port` is an incremental extension.
- `sgns_config.json` is already parsed in `LoadSgnsConfig()` (`GeniusNode.cpp:251`): holds `is_processor`, `net_id`, `subnet_id`, `bootstrap_fullnodes`, `authorized_full_node`. Adding `node_type` is an incremental extension; tests already write this file.
- `is_full_node` is overloaded: it gates connection watermarks (`400/200` vs `300/150`), UTXO address-filtering, the `GNUS_FULL_NODES_TOPIC` subscription, and several migration/account paths.
- Coding conventions: C++17, `snake_case_` for private members, `std::shared_ptr` factory pattern, RapidJSON for config parsing, Doxygen `@param` docs on public API. See `.planning/codebase/CONVENTIONS.md`.
- Tests use CTest; see `.planning/codebase/TESTING.md`.

**External consumers:** none known beyond this repo's `example/` and `test/`. The factory is treated as an internal API; a breaking change with full call-site migration is acceptable.

## Constraints

- **Tech stack**: C++17, CMake, existing RocksDB/CRDT/libp2p facilities; no new dependency unless research establishes a concrete need.
- **Protocol safety**: a certificate remains cryptographically bound to its exact winning proposal while the canonical slot establishes the shared finality domain.
- **Publication safety**: writer authority and failover must be deterministic, protocol-verifiable, and covered by failure tests; no local-only provenance shortcut.
- **Durability**: write ordering and restart recovery must prevent a second certificate or second mint effect.
- **Verification**: multi-node tests must exercise production ingress and propagation paths, not direct local-author helper calls.

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
| Restart canonical-finality work from `develop` | The unmerged Phase 9–12 branch has a large blast radius and a publication-authority design flaw; retain its observations, not its implementation | — Pending |
| Treat certificate publication authority as a protocol rule | A local ingress enum cannot prove authorship across peers or survive restart; publication and failover must be validated from durable certificate/proposal facts | Phase 10 ✓ |
| Store authoritative certificates by canonical slot | Same-slot contenders must meet one generic certificate authority, while the certificate itself retains exact-proposal binding; no bridge-only finality record is introduced | Phase 10 ✓ |
| Persist one local active vote per slot before publication | Volatile slot arbitration is insufficient after restart or cleanup; a published vote remains locked until matching durable finality or cryptographic expiry | Phase 9 ✓ |

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
*Last updated: 2026-08-24 after completing Phase 11 of milestone v3.0*
