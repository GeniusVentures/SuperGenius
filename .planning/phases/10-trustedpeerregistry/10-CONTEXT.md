# Phase 10: TrustedPeerRegistry - Context

**Gathered:** 2026-07-23
**Status:** Ready for planning

<domain>
## Phase Boundary

A new `TrustedPeerRegistry` component: a genesis-seeded, quorum-updatable set of trusted peers, built entirely on Phase 9's `SecureCrdt`/`SecureCrdtRegistry`/`ISignedCRDTData`. Genesis seeding uses a one-time ephemeral-key signing ceremony (not a bare hardcoded/unsigned config value). Adding/removing/replacing a member after genesis requires a configurable N-of-M quorum of signatures from the CURRENT trusted-peer set. This phase does NOT implement `BURN_BASIS_POINTS` (Phase 11) or migrate `ValidatorRegistry` (Phase 12) — but its `TrustedPeerRegistry` is the signer-set source those phases will depend on.

</domain>

<decisions>
## Implementation Decisions

### Genesis trust anchor
- **D-01:** The genesis trusted-peer list is NOT a bare, unsigned config value. It is written to CRDT as a real, signature-verified `TrustedPeerRegistry` record via the normal `SecureCrdt` flow: `ProposeValue(base_key, initial_list)` followed by exactly one `AddSignature(base_key, bootstrapper_address, signature)` call, using genesis quorum `threshold=1`. `ReadIfQuorum` reports the genesis value trusted once that single signature verifies — no special-casing in `SecureCrdt`/`ReadIfQuorum` itself, genesis is just an ordinary quorum-of-1 case.
- **D-02:** The signing key for genesis is an **ephemeral, one-time-use keypair**, not any node's long-term `GeniusAccount` key. Its private key signs the genesis list exactly once, then is destroyed immediately after (never persisted to disk, never reused). This means the bootstrapper identity can never sign anything again after genesis — it cannot unilaterally rewrite the registry later. All *subsequent* membership changes require a genuine N-of-M quorum from the real trusted-peer set (which does not include the bootstrapper's ephemeral key). This mirrors the "toxic waste destruction" pattern from zk-SNARK trusted-setup ceremonies — the goal here isn't hiding a cryptographic secret, but ensuring a one-time bootstrap authority can't retain standing power.

### Trust anchor location (bootstrapper public key)
- **D-03:** The ephemeral keypair's **public key** (i.e., its address — recall `GeniusAccount::GetAddress()` returns the raw hex-encoded public key directly, no hash/derivation) is distributed to every node via a new `bootstrapper_node` field in `sgns_config.json`, following the exact same shape as the existing `authorized_full_node` field (`GeniusNode.cpp:410-416`, used by `ValidatorRegistry`'s own genesis flow). This is a runtime-editable JSON config value, NOT hardcoded in source — consistent with this codebase's existing trust model for `authorized_full_node` (tampering with your own local config only affects your own node's view, not the network's consensus; not a new trust assumption).
- Every node includes `bootstrapper_node`'s address in the `signer_set` it uses (with `threshold=1`) specifically for verifying the genesis record — this is a fixed, one-signer, one-time-use signer set distinct from the normal N-of-M trusted-peer signer set used for all subsequent changes.

### Config location for the genesis list
- **D-04:** The genesis trusted-peer list is a new array field in `sgns_config.json` (e.g. `trusted_peers`), alongside the new `bootstrapper_node` field — not a new dedicated config file. Follows the exact `HasMember`+`IsArray`+push-loop pattern already used for `bootstrap_fullnodes` (`GeniusNode.cpp:399-408`).

### Signer-set-source pattern (bootstrapping self-reference)
- **D-05:** `TrustedPeerRegistry`'s `SignerSetSource` callback (required by `SecureCrdtRegistry::Register`, per Phase 9) reads from an **in-memory cache** of the currently-confirmed trusted-peer set (`last_known_good_` or similar), seeded at construction from the genesis config's `trusted_peers` list, and updated ONLY after a membership-change update is confirmed (`ReadIfQuorum` succeeds + `Verify`/`Apply` runs). This mirrors `ValidatorRegistry`'s proven `cached_registry_` pattern exactly (`ValidatorRegistry.cpp` cache-guarded-by-mutex, updated in place after acceptance). The callback must NOT call `ReadIfQuorum` recursively on itself — that would create re-entrancy against `SecureCrdt::AddSignature`'s per-signature quorum check.

### Membership change representation
- **D-06:** `ProposeValue` for a membership change carries the WHOLE new peer list each time (not a diff/delta operation). Simpler `Verify()`/`Apply()` logic (validate the full new list is well-formed and differs sanely from the current cached set) and simpler `ReadIfQuorum` semantics. Trade-off accepted: all N-of-M current signers must re-sign the complete list on every single membership change, not just the delta — acceptable since membership changes are expected to be rare (peer set churn, not routine).

### Claude's Discretion
- Exact `TrustedPeerRegistry` public API shape (class name, method signatures for proposing/signing/reading membership) — not discussed in depth, follow the `ValidatorRegistry` precedent's general shape adapted to the `SecureCrdt` API from Phase 9.
- Exact `ISignedCRDTData` payload format for the trusted-peer list (protobuf message vs simpler serialization) — left to planner/implementer, per each `ISignedCRDTData` implementer owning its own codec (Phase 8 D-01).
- How the ephemeral keypair generation + one-time signing ceremony is actually invoked (a CLI tool, a test-only helper, a documented manual procedure) — not discussed; this is tooling/operational, not core `TrustedPeerRegistry` logic. Flag as an open question for the planner to scope appropriately (likely a small standalone utility/script, not part of the main node binary's runtime path).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Genesis precedent (adapt, do not copy blindly — different trust model)
- `src/blockchain/ValidatorRegistry.cpp:514-531` — `CreateGenesisRegistry`: pure in-memory builder for genesis validator entries (NO signature check in that function itself — the milestone's design deliberately diverges here by requiring a real signed CRDT record for TrustedPeerRegistry's genesis, not an unsigned bootstrap).
- `src/blockchain/ValidatorRegistry.cpp:589+` (`StoreGenesisRegistry`) — idempotency guard pattern (skip if already seeded), builds update, signs via caller-supplied callback, serializes, `Put`s into GlobalDB. Direct precedent for TrustedPeerRegistry's genesis `ProposeValue`+`AddSignature` flow shape.
- `src/blockchain/impl/Blockchain.cpp:701-720` (`EnsureValidatorRegistry`) — trigger logic: only the node whose account address matches the configured authority address performs genesis seeding. `TrustedPeerRegistry`'s trigger will be analogous but keyed off `bootstrapper_node`, not `authorized_full_node`.
- `src/blockchain/impl/Blockchain.cpp:505,515` — `GetAuthorizedFullNodeAddress`/`SetAuthorizedFullNodeAddress` — the exact existing pattern `bootstrapper_node` should mirror.

### Config parsing precedent
- `src/account/GeniusNode.cpp:322-435` (`LoadSgnsConfig`) — existing `sgns_config.json` field parsing (`net_id`, `node_type`, `subnet_id`, `bootstrap_fullnodes`, `authorized_full_node`, etc.), all fields optional with logged defaults.
- `src/account/GeniusNode.cpp:399-408` — exact `HasMember`+`IsArray`+push-loop pattern for `bootstrap_fullnodes`; `trusted_peers` should follow this exactly.
- `example/node_test/sgns_config.json` — existing example config file to extend with `trusted_peers` (array) and `bootstrapper_node` (string) fields.

### Signer identity
- `src/account/GeniusAccount.cpp:780` — `GetAddress()` returns the raw hex-encoded public key directly (`eth_keypair_->GetEntirePubValue()`) — confirms "I know a peer's public key" is directly usable as its `GeniusAccount` address for `multisig::VerifyPayloadSignature`, no derivation/registration needed.
- `src/multisig/MultiSig.hpp:24-25` — doc comment confirming `address` parameter is "Public address (hex-encoded public key)".

### Self-referential cache pattern
- `ValidatorRegistry`'s `cached_registry_` member (guarded by `cache_mutex_`), seeded at cache-init (`ValidatorRegistry.cpp:2205-2216`) and updated in place after a confirmed registry update (`ValidatorRegistry.cpp:1300-1320`) — the exact pattern `TrustedPeerRegistry`'s in-memory signer-set cache (D-05) should follow.
- `ValidatorRegistry::FindValidator` (`ValidatorRegistry.cpp:1217-1222`) — precedent for checking whether a signer belongs to the current cached set.

### Phase 9 API (SecureCrdt/SecureCrdtRegistry/ISignedCRDTData — already shipped)
- `src/securecrdt/ISignedCRDTData.hpp` (65 lines, full file) — interface `TrustedPeerRegistry`'s payload type must implement: `SerializeToBytes`, `DeserializeFromBytes` (must return false, not throw, on malformed input), `Verify`, `Apply` (side effect only, does NOT itself check quorum).
- `src/securecrdt/SecureCrdtRegistry.hpp` (144 lines, full file) — `SecureCrdtRegistryEntry` shape (`signer_set_source`, `make_instance`, `owner_token`), `SignerSetSnapshot{signer_set, threshold}`, `Register`/`UnregisterIf`/`Resolve`/`AllEntries`.
- `src/securecrdt/SecureCrdt.hpp` (159 lines, full file) — `ProposeValue`, `AddSignature`, `ReadIfQuorum` (explicitly documented, per its own Doxygen `@note`, as expecting the caller — i.e. `TrustedPeerRegistry` — to call `DeserializeFromBytes`+`Verify`+`Apply` on the returned bytes itself), `RegisterFilters` (must be called once after construction).
- `test/src/securecrdt/securecrdt_registry_test.cpp:44-56` — only existing usage example (test-only, fixed signer list lambda) — `TrustedPeerRegistry` is the first real (non-test) consumer.

### Project-level context
- `.planning/PROJECT.md` — v1.1 milestone Key Decisions, esp. TrustedPeerRegistry genesis-seeding and N-of-M quorum requirements.
- `.planning/REQUIREMENTS.md` §"Milestone v1.1" — TPR-01, TPR-02, TPR-03 (this phase's requirements).
- `.planning/phases/09-securecrdt-layer/09-CONTEXT.md` — Phase 9's locked decisions (D-01..D-05), especially D-04 (no "final" write, reader always re-derives trust) which this phase's genesis design must remain consistent with — genesis is NOT an exception to D-04, it's simply the quorum-of-1 case.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/securecrdt/SecureCrdt.hpp`/`SecureCrdtRegistry.hpp`/`ISignedCRDTData.hpp` — the complete Phase 9 machinery this phase is the first real consumer of.
- `ValidatorRegistry`'s cache/genesis/trigger patterns — adapt, don't copy (different trust model per D-01/D-02).

### Established Patterns
- `sgns_config.json` field-parsing convention (optional fields, logged defaults, `HasMember`+`IsArray`).
- Address == raw public key (no separate identity/derivation layer).

### Integration Points
- Depends on Phase 9's `SecureCrdt`/`SecureCrdtRegistry`/`ISignedCRDTData` (already shipped).
- Will be the signer-set-source dependency for Phase 11 (`BURN_BASIS_POINTS`) — its public "get current trusted peer set" surface must be usable by that phase's `SecureCrdtRegistryEntry.signer_set_source`.

</code_context>

<specifics>
## Specific Ideas

The user's own framing, refined during discussion: this genesis-signing pattern is directly inspired by zk-SNARK trusted-setup ceremonies (the "toxic waste" destruction pattern) — a one-time secret authorizes genesis, then is destroyed so it can never be used to forge later state. The design correctly separates two concerns that could otherwise be conflated: (1) where the trust ANCHOR (bootstrapper's public key) lives — settled as ordinary runtime JSON config, same precedent as `authorized_full_node`; and (2) whether the genesis VALUE itself is a real signed CRDT record — settled as yes, via the normal `ProposeValue`+`AddSignature` flow at threshold=1, not a special-cased unsigned bootstrap.

</specifics>

<deferred>
## Deferred Ideas

None new this phase. Raw-public-key signer identity concern from Phase 8/9 was resolved during this phase's research (D-03: address IS the raw public key, no separate identity type needed).

</deferred>

---

*Phase: 10-TrustedPeerRegistry*
*Context gathered: 2026-07-23*
