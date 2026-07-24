# SuperGenius

## What This Is

SuperGenius is a C++17 blockchain/crypto platform providing an account system (UTXO + DAG), consensus, a processing grid for distributed compute tasks, an EVM bridge, and a JSON-RPC + WebSocket API. It targets native node operators (full/light/archive) and ships cross-platform keystore support (Android NDK / iOS).

The current milestone hardens consensus finality so competing transactions resolve through one canonical slot. Certificates continue to prove the exact winning proposal, while slot-scoped storage and validator vote locks ensure that the same account nonce or bridge burn cannot produce multiple valid certificates.

## Core Value

**At most one valid certificate may finalize a canonical consensus slot.** Transaction execution, CRDT delivery order, validator restarts, and competing proposers must not allow a second certificate for the same account nonce or bridge burn.

## Current Milestone: v2.0 Slot-Scoped Consensus Finality

**Goal:** Guarantee that only one certificate can finalize a canonical consensus slot while preserving transaction-hash retrieval and trusting a valid quorum certificate over local state.

**Target features:**
- Authoritative certificate storage and lookup by canonical slot ID
- Transaction-hash-to-slot secondary index for existing certificate consumers
- Restart-safe one-signature-per-validator-per-slot journal
- Atomic slot finalization when a valid certificate is first observed
- Best-proposal collection before an irreversible validator vote
- Slot-owned bridge burn reservations that survive losing proposals and become consumed at finality
- Regression coverage for the observed two-certificate race and its critical interleavings

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
- ✓ Certificates remain cryptographically bound to exact proposals while authoritative storage and finality identity use the canonical slot — **Validated in Phase 9**
- ✓ Existing transaction-hash consumers retrieve the winning slot certificate through a verified secondary index — **Validated in Phase 9**
- ✓ Normal transactions retain address-plus-nonce slot identity and certificate-chain validation — **Validated in Phase 9**

### Active

<!-- This milestone's scope. Hypotheses until shipped. -->

- [ ] A validator signs at most one proposal per canonical slot while its signature can still contribute to a valid certificate
- [ ] Validator vote locks survive restart and transition atomically to finalized slot state when a valid certificate is observed
- [ ] Competing proposals may replace the current best before voting; a validator never revotes after publishing its slot signature
- [ ] Bridge burn reservations are owned by the canonical burn slot, survive losing proposals, and become consumed at certificate finality
- [ ] Automated tests reproduce the observed double-certificate race and prove exactly one certificate and one confirmed mint

### Out of Scope

<!-- Explicit boundaries to prevent scope creep. -->

- Propagating the `NodeType` enum into `TransactionManager` (60+ `full_node_m` refs), `UTXOManager`, `MigrationManager` — deferred; the derived bool stays this milestone
- Distinct runtime behavior between `Archive` and `Full` — both map to `is_full_node_=true` for now; the `Archive` value exists for forward compatibility only
- Any change to consensus, processing grid, EVM bridge, or API transport logic
- New node roles beyond Full/Light/Archive (e.g. Validator/Bootstrap) — not introduced here
- Rewriting `DevConfig` or the dev-config plumbing — only the `GeniusNode` construction surface changes
- Migration tooling for old on-disk config files — defaults cover it; no schema-version migration
- Broader Phase 8 fault injection for node kill, RPC disagreement, and pubsub partition — preserve as archived planning, but exclude from v2.0
- Bridge parser/configuration fuzzing — preserve as archived planning, but exclude from v2.0
- Changing certificate signatures to sign only a slot ID — certificates must continue binding the complete winning proposal
- Treating transaction execution or CRDT callback order as the consensus safety boundary — finality must be established when the certificate is first validated
- Allowing validators to retract or replace a published vote — published signatures are irreversible
- Redesigning validator quorum weights or bridge RPC verification policy — retain current quorum and external-burn verification rules

## Context

**Current state:** Phase 9 completed on 2026-07-24. Canonical slot identities, authoritative slot-keyed certificates, verified transaction-hash lookup, atomic mint application, safe CRDT teardown, and immutable RPC decision snapshots are implemented and verified. Durable validator vote locking remains Phase 10.

**Observed safety failure:** In `log_bridge_race.txt`, transaction `7541b3e2...` and transaction `9a378fd9...` reference the same burn `771780cf...` and resolve to the same mint-v2 slot. Nine validators sign both proposals, allowing both to reach quorum and become confirmed.

**Root cause:** Certificates are signed for exact proposals and stored by nonce-subject transaction hash, while conflict exclusivity is transiently tracked by slot. `HandleCertificate()` clears the slot before CRDT certificate delivery confirms the transaction and consumes its bridge UTXO. Validators then see no slot state, no confirmed-input conflict, and a still-valid external burn, so they sign the second proposal.

**Existing identities:**
- Normal transaction slot: source address + nonce (`GeniusTransaction::GetSlotID()`)
- Mint-v2 slot: chain, token, amount, destination, and burn hash (`MintTransactionV2::GetSlotID()`)
- Current certificate key: nonce-subject transaction hash
- Required bridge finality resource: canonical burn identity, independent of proposer address and nonce

**Compatibility dependencies:** `TransactionManager` follows previous certificates by transaction hash, and `GeniusInputValidator` retrieves producer certificates by transaction hash. Slot-keyed authoritative storage therefore requires a verified transaction-hash-to-slot index rather than removal of transaction-hash lookup.

**Brownfield:** A codebase map exists at `.planning/codebase/`. Primary integration points are `src/blockchain/Consensus.{hpp,cpp}`, `src/account/TransactionManager.cpp`, `src/account/GeniusTransaction.hpp`, `src/account/MintTransactionV2.cpp`, `src/account/UTXOManager.{hpp,cpp}`, and bridge race tests under `test/src/bridge_race/`.

## Constraints

- **Protocol safety**: No validator may publish two signatures that can simultaneously contribute to certificates for the same canonical slot.
- **Durability**: A validator vote lock must survive restart until a certificate finalizes the slot or the previous signature can no longer form a valid certificate.
- **Atomicity**: Finalized-slot state must be established before proposal state or vote locks are cleared.
- **Compatibility**: Transaction-hash certificate lookup remains available for nonce chaining and producer-UTXO verification.
- **Certificate semantics**: A valid quorum certificate overrides a validator's local proposal preference, but a slot may have only one authoritative certificate.
- **Bridge identity**: Burn uniqueness cannot depend on proposer address, nonce, amount, or destination supplied by a candidate when a canonical external burn identifier is available.
- **Tech stack**: C++17 and existing persistence/CRDT facilities; no new dependency is required.
- **Verification**: The observed interleaving must be covered deterministically, including restart and late-certificate cases.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Store the authoritative certificate by canonical slot, with transaction hash as a secondary lookup index | Certificate finality must match the resource over which proposals compete while existing transaction-chain consumers still need hash retrieval | Phase 9 ✓ |
| Enforce one published validator signature per slot | A 75% quorum can only remain safe if honest validators do not sign competing proposals in the same finality domain | — Pending |
| Keep vote locks until certificate finality or cryptographic expiry, and persist them before publishing | In-memory or proposal-lifetime locks allow restart and timeout equivocation while old signatures remain usable | — Pending |
| Finalize the slot in `HandleCertificate()` before clearing proposal state | This is the earliest valid-certificate observation and closes the gap before CRDT transaction application | — Pending |
| Allow best-proposal replacement only before the validator's one irreversible vote | Published signatures cannot be retracted; a bounded collection window preserves deterministic candidate selection without double-signing | — Pending |
| Make bridge reservations slot-owned rather than proposal-owned | Competing candidates share one burn resource; losing proposals must not unlock the eventual winner | — Pending |
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
*Last updated: 2026-07-24 after completing Phase 9 Canonical Slot and Certificate Storage*
