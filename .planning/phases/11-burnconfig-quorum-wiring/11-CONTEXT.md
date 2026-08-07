# Phase 11: BurnConfig Quorum Wiring - Context

**Gathered:** 2026-07-24
**Status:** Ready for planning

<domain>
## Phase Boundary

`BURN_BASIS_POINTS` (currently `static constexpr uint64_t BURN_BASIS_POINTS = 100` in `TransactionManager.hpp:52`) becomes a `TrustedPeerRegistry`-quorum-signed CRDT value instead of a hardcoded constant. `TransactionManager` caches the current value and refreshes it via a CRDT-change callback (no live CRDT read per `PayEscrow` call). This is the first phase in the milestone that wires the multi-sig CRDT framework (Phases 8-10) into a real running `GeniusNode` — Phases 8-10 deliberately built standalone/undwired components. This phase does NOT migrate `ValidatorRegistry` (Phase 12) and does NOT build an operator-facing CLI for proposing/signing changes (explicitly out of scope, see Deferred).

</domain>

<decisions>
## Implementation Decisions

### BurnConfig genesis seeding trigger
- **D-01:** No ephemeral bootstrap keypair is needed for BurnConfig's genesis value (unlike `TrustedPeerRegistry`'s Phase 10 genesis) — by the time this phase runs, real trusted peers already exist (seeded in Phase 10), so genesis is just an ordinary N-of-M-signed `ProposeValue`/`AddSignature` sequence using real trusted-peer signatures.
- **D-02:** Seeding is **auto-triggered at `GeniusNode` startup**: if this node's own address is among `TrustedPeerRegistry::GetCurrentPeers()` AND BurnConfig has never been seeded (`SecureCrdt::ReadIfQuorum` on the BurnConfig key returns absent/`nullopt`), the node proposes `BURN_BASIS_POINTS=100` and signs it itself. Other trusted-peer nodes independently do the same on their own startup — no coordination needed, converging via CRDT once enough have signed (the same self-healing pattern CRDT itself already provides). Non-trusted-peer nodes only ever read whatever value is present; a fresh, never-yet-quorum-confirmed CRDT state falls back to the hardcoded default (100) for local behavior until quorum is reached, per BURN-03's "existing behavior preserved by default" requirement.

### Auto-signing scope (genesis only, not all future changes)
- **D-03:** Auto-signing is **strictly limited to the known genesis default (100)** at startup. Any *proposed change* to `BURN_BASIS_POINTS` after genesis is an economic policy decision and must NEVER be auto-signed by a node just because a new value appears in CRDT — signing a change requires deliberate, explicit, out-of-band operator action (a future CLI or manual procedure, out of this phase's scope). The distinction: genesis auto-signing reproduces a value every trusted peer already knows and agrees is correct (preserving existing behavior); auto-signing arbitrary future changes would let a compromised/misconfigured node rubber-stamp a bad change.

### TransactionManager wiring point
- **D-04:** `TrustedPeerRegistry` + a `SecureCrdt`-backed `BurnConfig` component are constructed inside `GeniusNode`'s `INITIALIZING_TRANSACTIONS` state, at/around the same point `TransactionManager::New(...)` is called (`GeniusNode.cpp` ~line 692-708) — CRDT/`GlobalDB` (`tx_globaldb_`) is already fully initialized by this point (set up in the earlier `INITIALIZING_DATABASE`/`INITIALIZING_BLOCKCHAIN` states), and `GeniusNode`'s own account/address (`account_`) is already known (set in the `GeniusNode` constructor itself, well before this state). No new node-startup state is introduced.

### Quorum threshold for BurnConfig changes
- **D-05:** BurnConfig registers its **own separately configurable threshold**, independent of `TrustedPeerRegistry`'s membership-change threshold. Changing the burn rate (economic parameter) and changing network trusted-peer membership are different-stakes decisions and should not be forced to share one N-of-M number, even though both draw their *signer set* from the same `TrustedPeerRegistry::GetCurrentPeers()`.
- **D-06 (config fields for both thresholds):** This phase adds NEW config fields for BOTH `TrustedPeerRegistry`'s own membership-change threshold AND BurnConfig's separate threshold (per D-05) — this is the first phase that wires real config-consumption for the multi-sig framework into `GeniusNode`, so it's the natural point to do both properly rather than leaving one hardcoded.
- **D-07 (majority-floor enforcement, security-critical):** Because both thresholds come from locally-editable JSON config, and each node evaluates quorum independently against its own local signer-set-source (Phase 9 D-04: no final write, every reader re-derives trust), a malicious/compromised node operator could locally lower their own node's threshold (e.g. to 1) to make their own node wrongly "confirm" an under-signed value. To prevent this: both `TrustedPeerRegistry::New` and `BurnConfig`'s constructor MUST validate the configured `quorum_threshold` against a computed majority floor — `minimum_safe_threshold = ceil(0.51 * signer_set.size())` — and MUST FAIL TO CONSTRUCT (return an error, refuse to start) if the configured threshold is below this floor. No silent clamping, no warning-only behavior — this is a hard, code-enforced invariant. Applies to BOTH thresholds, no exceptions.

### Claude's Discretion
- Exact `BurnConfigPayload : ISignedCRDTData` class shape and serialization format (mirrors `TrustedPeerListPayload` from Phase 10 — a small integer payload, `Verify()` checks it's `<= BASIS_POINTS_TOTAL`) — not discussed in depth.
- Exact `BurnConfig`/wrapper class name and where it lives in the source tree (likely `src/account/` alongside `TransactionManager`, or a small new `src/burnconfig/` — planner's call, given this is a thin wiring layer rather than a new standalone library like Phases 8-10).
- Exact CRDT key name/`HierarchicalKey` for the BurnConfig value.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Current BURN_BASIS_POINTS
- `src/account/TransactionManager.hpp:51-53` — current hardcoded constant, with a comment already anticipating this phase: "Eventually settable via multisig CRDT config; hardcoded default until then."
- `src/account/TransactionManager.cpp:800` — usage inside `PayEscrow`: `const auto burn_amount = ( escrow_amount * BURN_BASIS_POINTS ) / BASIS_POINTS_TOTAL;` — an instance-method context, trivially convertible to a cached member read.
- `src/account/TransactionManager.cpp:262-283` — the private constructor (invoked only via the static factory `New`, `TransactionManager.cpp:123-260`, declared `TransactionManager.hpp:100-108`) — natural place to initialize a cached `burn_basis_points_` member and register the CRDT-change callback.
- `src/account/TransactionManager.cpp:220-229` — existing `RegisterNewElementCallback` usage for tx elements inside `TransactionManager::New`, the exact idiom to replicate for a BurnConfig key's callback in the same file.

### GeniusNode wiring point
- `src/account/GeniusNode.cpp:692` onward — `NodeState::INITIALIZING_TRANSACTIONS`, where `TransactionManager::New` is called (`GeniusNode.cpp:703-708`).
- `src/account/GeniusNode.cpp:610-619` (`INITIALIZING_DATABASE`) and `:624-681` (`INITIALIZING_BLOCKCHAIN`) — confirm `tx_globaldb_`/`Blockchain` are already initialized before `INITIALIZING_TRANSACTIONS` runs.
- `src/account/GeniusNode.cpp:270-292` — `account_` (node's own `GeniusAccount`/address) constructed in the `GeniusNode` constructor itself, well before `INITIALIZING_TRANSACTIONS`.
- Confirmed via grep: `TrustedPeerRegistry`/`SecureCrdt` are referenced nowhere in `GeniusNode.cpp`/`.hpp` today — this phase is the first to wire them in.

### Phase 10 TrustedPeerRegistry API (already shipped, full file read required)
- `src/trustedpeer/TrustedPeerRegistry.hpp` — `New(secure_crdt, genesis_peers, bootstrapper_address, quorum_threshold, base_key)`, `SeedGenesis`, `ProposeMembershipChange`, `SignMembershipChange`, `TryConfirm`, `GetCurrentPeers` (cached-only, no live CRDT read), `IsGenesisConfirmed`, `Unregister`.
- `src/trustedpeer/TrustedPeerRegistry.hpp:34-64` — `TrustedPeerListPayload : ISignedCRDTData` — the direct structural precedent for a new `BurnConfigPayload`.

### Phase 9 SecureCrdt/SecureCrdtRegistry API (already shipped)
- `src/securecrdt/SecureCrdtRegistry.hpp` — `Register`/`UnregisterIf`/`Resolve`/`AllEntries`, `SecureCrdtRegistryEntry{key_pattern, signer_set_source, make_instance, ...}`, `SignerSetSource = std::function<outcome::result<SignerSetSnapshot>(const std::string&)>`. BurnConfig's `SignerSetSource` calls `TrustedPeerRegistry::GetCurrentPeers()` + its own separately-configured threshold (D-05).
- `src/securecrdt/SecureCrdt.hpp` — `ProposeValue`, `AddSignature`, `ReadIfQuorum` (raw bytes only, caller deserializes/`Verify()`/`Apply()`s), `RegisterFilters`. **The header's own doc comment explicitly names "Phase 11 BurnConfig" as a forthcoming `ISignedCRDTData` implementer** — this design was pre-planned.

### CRDT-change callback mechanism
- `src/crdt/globaldb/globaldb.hpp:174` — `GlobalDB::RegisterNewElementCallback(pattern, callback)`; type alias `GlobalDBNewElementCallback` at `:73`; impl `globaldb.cpp:582-585`.
- `src/blockchain/ValidatorRegistry.cpp:1231-1256` (`RegisterFilter`) and `:1286+` (`RegistryUpdateReceived`) — the exact "refresh cached value on CRDT-change callback, no polling" pattern to replicate for `TransactionManager`'s cached `burn_basis_points_` member.

### Project-level context
- `.planning/PROJECT.md` — v1.1 milestone Key Decisions.
- `.planning/REQUIREMENTS.md` §"Milestone v1.1" — BURN-01, BURN-02, BURN-03 (this phase's requirements).
- `.planning/phases/10-trustedpeerregistry/10-CONTEXT.md` — Phase 10's locked decisions (D-01..D-06), which this phase's `TrustedPeerRegistry` consumption must remain consistent with.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/trustedpeer/TrustedPeerRegistry.hpp`/`.cpp` — already shipped, this phase's signer-set-source dependency.
- `src/securecrdt/SecureCrdt.hpp`/`SecureCrdtRegistry.hpp` — already shipped, BurnConfig's registration/write/read machinery.
- `ValidatorRegistry`'s `RegisterNewElementCallback`/`RegistryUpdateReceived` pattern — direct precedent for the cache-refresh-on-change mechanism.

### Established Patterns
- `TransactionManager::New`'s existing `RegisterNewElementCallback` usage (same file, same idiom already in place for other CRDT keys).
- `TrustedPeerListPayload`'s `ISignedCRDTData` shape — direct structural template for `BurnConfigPayload`.

### Integration Points
- Depends on Phase 9 (`SecureCrdt`/`SecureCrdtRegistry`) and Phase 10 (`TrustedPeerRegistry`), both already shipped.
- First real `GeniusNode` integration point for the entire multi-sig CRDT framework — construct `TrustedPeerRegistry` + `BurnConfig` at `INITIALIZING_TRANSACTIONS`, wire the cached value into `TransactionManager`.

</code_context>

<specifics>
## Specific Ideas

The user's framing during discussion: genesis auto-seeding is safe because it reproduces a value every trusted peer already knows and agrees is correct (preserving existing behavior, no judgment call involved) — but that same automation must NOT extend to future changes, which are real economic policy decisions requiring deliberate human approval. This mirrors the trust-boundary reasoning already established in Phase 10 (the ephemeral genesis key can never sign again after use) — automation is fine for reproducing known-good state, never for approving new state.

</specifics>

<deferred>
## Deferred Ideas

- A CLI/tooling for an operator to propose and sign a BurnConfig change post-genesis — explicitly out of this phase's scope (D-03). Future phase/milestone item if the project needs to actually exercise a live burn-rate change.

</deferred>

---

*Phase: 11-BurnConfig Quorum Wiring*
*Context gathered: 2026-07-24*
