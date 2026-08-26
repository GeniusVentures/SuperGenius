# Milestones

## v1.1 Multi-Signature Secure CRDT Storage (**ACTIVE — re-opened 2026-08-26**)

> **v1.1 extended, not closed.** Phases 10–12 (TrustedPeerRegistry, BurnConfig Quorum Wiring, ValidatorRegistry Migration — the items listed as "deferred" below) completed 2026-07-24/27. On **2026-08-26** the milestone was extended with the ELM Bridging phases as a **product-v1.0 pre-ship requirement** (owner directive; see `.planning/notes/ELM-bridging-gaps.md` and `INGEST-CONFLICTS.md`): Phase 13 — ELM Job Bridging (SuperGenius, issue #369), Phase 14 — ELM Runtime in SGProcessingManager (cross-repo, GeniusVentures/SGProcessingManager#17), Phase 15 — ELM Events & Streaming (deferred). The milestone now gates product v1.0. A final shipped entry replaces this section when Phases 13–14 close.

## v1.1 Multi-Signature Secure CRDT Storage (partial ship: 2026-07-29)

**Phases completed:** 2 phases (08-multisig-primitive, 09-securecrdt-layer), 4 plans

**Key accomplishments:**

- `MultiSig` library: canonical signing-bytes + N-of-M quorum evaluation (dedup + verify loop) reusing `ConsensusAuth`'s SHA-256/`VerifySignature` primitives, usable independently of CRDT (Phase 08-multisig-primitive — MSIG-01/02/03)
- `ISignedCRDTData` interface + `SecureCrdtRegistry`: static topic/key-pattern → {signer-set source, quorum rule, type} registry declared in code (Phase 09-securecrdt-layer — SCRDT-01/02)
- `SecureCrdt` wrapper: local-write gate rejecting unsigned/under-signed writes before apply, plus read-path quorum re-derivation; propose/sign/quorum flow transported entirely over CRDT put + filter-callback, no new networking/RPC (Phase 09-securecrdt-layer — SCRDT-03/04)
- Full build + CTest green, including an end-to-end propose/sign/quorum handoff-contract test

**Note:** This milestone's branch (`gsd/phase-09-securecrdt-layer`) numbered its own phases 8-9 independently of the `v2.0` roadmap's phases 8-9 (unrelated topics — burn/mint datapath robustness vs. multisig/SecureCRDT). Rebased onto `develop` on 2026-07-29 after `v2.0` phase 8 had already merged; `v2.0` was kept as the current milestone in `PROJECT.md`/`STATE.md` throughout.

**Deferred to a future milestone:** `TrustedPeerRegistry` (TPR-01/02/03), `BURN_BASIS_POINTS` as a quorum-signed CRDT value (BURN-01/02/03), and `ValidatorRegistry` migration onto `ISignedCRDTData` (MIG-05/06) — these were in the original v1.1 requirements but not implemented in phases 08-09.

---

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
