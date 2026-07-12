# Milestones

## v1.0 GeniusNode Construction Refactor (Shipped: 2026-07-03)

**Phases completed:** 3 phases, 5 plans, 0 tasks

**Stats:** 39 files changed, +2956 / −341 lines · 12/12 v1 requirements validated · git range `2585e6a9 → HEAD`

**Key accomplishments:**

- Config-driven network settings: `auto_dht` + `port_seed` (renamed from `base_port`) read from `network_config.json` in `InitNetwork()` with config-wins precedence, safe defaults, and `pubsub_port` > `port_seed` priority Doxygen (Phase 1 — CFG-01, CFG-04)
- `NodeType` enum (Full/Light/Archive) + case-insensitive `NodeTypeFromString()` + `node_type` read in `LoadSgnsConfig()`; `is_full_node_` derived in a reordered private ctor that creates the account via `std::visit` **after** `LoadSgnsConfig` resolves the role (the init-order hinge fix) (Phase 2 — CFG-02, CFG-03)
- Canonical `New(dev_config, AccountSource)` variant factory (`AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>`, `FromPublicKey` promoted public); `nullptr`-on-failure preserved; old factories retained through Phase 2 then deleted (Phase 2 — INTF-01/02/03; Phase 3 — INTF-04)
- All ~25 `NewFromPrivateKey`/old-`New` call sites across 14 files migrated to `New(dev_config, FromPrivateKey{...})`; shared `WriteNetworkConfig`/`WriteSgnsConfig` helpers (truncate, create-dir, validate `node_type`); old factories + old private ctor deleted — `New(dev_config, AccountSource)` is the sole entry point (Phase 3 — MIG-01/02/03/04)
- Full build + CTest green; no behavior change for default or pre-existing config files (deployed configs without the new keys keep working via defaults)

**Known deferred items at close:** 0 (open-artifact audit clear). Future-milestone items noted in REQUIREMENTS v2: `NodeType` downstream propagation (PROP-01), distinct Archive-vs-Full behavior (PROP-02), `pubsub_port` numeric cleanup (HARD-01), config schema versioning (HARD-02).

---
