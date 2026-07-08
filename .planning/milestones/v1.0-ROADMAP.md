# Roadmap: SuperGenius — GeniusNode Construction Refactor

**Milestone:** v1.0 — GeniusNode Construction Refactor
**Mode:** standard (Horizontal Layers)
**Granularity:** coarse
**Created:** 2026-07-02

**Core Value:** Constructing a `GeniusNode` must be a single, self-documenting call driven by config files.

**Dependency note:** Phases are strictly sequential (1 → 2 → 3). Each later phase compiles only after the prior one lands. This matches the build order in `.planning/research/ARCHITECTURE.md`.

---

### Phase 1: Config-Driven Settings Foundation

**Goal:** Introduce config-file reads for network settings as an additive, behavior-preserving layer. After this phase, `auto_dht` and `port_seed` (renamed from `base_port`) are read from `network_config.json` (config wins if present; param is fallback), all missing keys fall back to today's exact defaults, and the `port_seed` ↔ `pubsub_port` priority is Doxygen-documented. The old factories still work unchanged — this phase adds read paths without removing anything. (node_type/NodeType enum deferred to Phase 2 per 01-CONTEXT.md D-09.)

**Success Criteria**:
1. `InitNetwork()` resolves `auto_dht` and `port_seed` from `network_config.json` with defaults `true` / `40001` (numeric `IsUint` for `port_seed`); config overrides the constructor param when present
2. Every new read defaults safely on missing key and WARN-logs unrecognized/ill-typed values
3. Port resolution priority (`pubsub_port` > `port_seed`-derived) is Doxygen-documented; `base_port` renamed to `port_seed` throughout touched code
4. A test proves config overrides the param; existing factory signatures and call sites are otherwise untouched; full build + CTest green with no behavior change

**Requirements:** CFG-01, CFG-04
**Dependencies:** none
**UI hint**: no

---

### Phase 2: Variant Factory + Constructor Reorder

**Goal:** Collapse the three overloaded factories into a single `New(dev_config, AccountSource)` and reorder the private constructor so the account is created after `LoadSgnsConfig()` resolves `node_type_` → `is_full_node_` (resolving the init-order hinge from `research/ARCHITECTURE.md`). Delete the old factories entirely. `is_full_node_` is now derived from `node_type_`, not passed as a param. Also introduces the `NodeType` enum + `node_type` config read (moved from Phase 1 per 01-CONTEXT.md D-09).

**Success Criteria**:
1. `New(dev_config, AccountSource)` is the sole public factory; `AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>` with `FromPublicKey` promoted to public
2. Private constructor signature is `(dev_config, AccountSource)`; account is created via `std::visit` AFTER `LoadSgnsConfig()` so `is_full_node_` is resolved first
3. `LoadSgnsConfig()` parses `node_type` ("Full"/"Light"/"Archive") into a new `NodeType` enum via `NodeTypeFromString()`; `is_full_node_` is derived from `node_type_` (Full/Archive → true, Light → false), set once during construction, immutable afterward
4. `New(dev_config, AccountSource)` is canonical; the old `New(autodht, port_seed, is_full_node)`, `NewFromPrivateKey`, `NewFromMnemonic` are **retained** (deleted in Phase 3 as the first migration step — see `02-CONTEXT.md` D-01) so the build stays green while the 18 call sites await migration
5. Downstream consumers (`UTXOManager`, `TransactionManager`, `MigrationManager`, `GeniusAccount`) receive the bool unchanged

**Requirements:** INTF-01, INTF-02, INTF-03, CFG-02, CFG-03  *(INTF-04 moved to Phase 3 per `02-CONTEXT.md` D-01)*
**Dependencies:** Phase 1 (needs network config read paths + port_seed rename in place)
**UI hint**: no

---

### Phase 3: Call-Site Migration + Verification

**Goal:** Delete the old factories (INTF-04, moved from Phase 2 per `02-CONTEXT.md` D-01), migrate all 18 existing call sites to the new `New(dev_config, AccountSource)` API with no compatibility shim, establish a shared test config-write helper, and verify the full build + test suite is green with no behavior regression and no stale references to the old factories.

**Success Criteria**:
1. All 18 `NewFromPrivateKey(...)` call sites (1 in `example/node_test/`, 17 in `test/src/`) rewritten to `New(dev_config, FromPrivateKey{...})`
2. A shared test helper writes minimal `network_config.json` (`base_port`, `autodht`) and `sgns_config.json` (`node_type`, `is_processor`), extending the existing pattern
3. Full build passes; existing CTest suite green; no behavior change for default or pre-existing config files
4. Verification grep confirms no `NewFromPrivateKey` / `NewFromMnemonic` / old `New(autodht, base_port, is_full_node)` references remain in `src/`, `example/`, `test/`

**Requirements:** INTF-04, MIG-01, MIG-02, MIG-03, MIG-04
**Dependencies:** Phase 2 (needs the new `New()` factory to exist)
**UI hint**: no

---

## Phase Coverage Summary

| Requirement | Phase | Description |
|-------------|-------|-------------|
| CFG-01 | 1 | auto_dht + port_seed in network_config.json |
| CFG-04 | 1 | Safe defaults + logging |
| INTF-01 | 2 | Single New() factory |
| INTF-02 | 2 | 4-source variant |
| INTF-03 | 2 | Ctor reorder |
| INTF-04 | 3 | Remove old factories (moved from Phase 2 per D-01) |
| CFG-02 | 2 | node_type in sgns_config.json + NodeType enum |
| CFG-03 | 2 | Derived is_full_node_ |
| MIG-01 | 3 | Migrate 18 call sites |
| MIG-02 | 3 | Test config-write helper |
| MIG-03 | 3 | Build + tests green |
| MIG-04 | 3 | Grep-clean of old signatures |

**Coverage:** 12/12 v1 requirements mapped ✓ | 0 unmapped

## Backlog

(Items deferred to future milestones — see `.planning/REQUIREMENTS.md` v2 section: PROP-01 NodeType propagation, PROP-02 Archive/Full behavior split, HARD-01 pubsub_port numeric, HARD-02 config schema versioning.)

---
*Roadmap created: 2026-07-02*
