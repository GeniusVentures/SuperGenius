# Phase 10: TrustedPeerRegistry - Research

**Researched:** 2026-07-23
**Domain:** C++ CRDT-backed quorum-signed registry, built atop Phase 9's SecureCrdt
**Confidence:** HIGH (all core claims verified directly against current source, no external libraries involved)

## Summary

`TrustedPeerRegistry` is architecturally a thin, type-specific consumer of Phase 9's already-shipped `SecureCrdt`/`SecureCrdtRegistry`/`ISignedCRDTData` machinery — it should NOT reimplement any signature/quorum logic. The best reference implementation to adapt is `ValidatorRegistry`'s cache pattern (`cached_registry_` + `cache_mutex_`, seeded-then-updated-in-place), but note `ValidatorRegistry`'s genesis path (`CreateGenesisRegistry`/`StoreGenesisRegistry`) is UNSIGNED at genesis — Phase 10 deliberately diverges (per CONTEXT.md D-01/D-02) and must route genesis through the real `SecureCrdt::ProposeValue` + one `AddSignature` call, not copy `StoreGenesisRegistry`'s direct-`Put` pattern.

All Phase 9 APIs referenced in CONTEXT.md (`ISignedCRDTData`, `SecureCrdt`, `SecureCrdtRegistry`) were read directly from source in this session and are confirmed stable/unchanged since Phase 9 shipped — no drift.

One material gap found during this research: **there is no existing ephemeral, in-memory-only (non-persisted) keypair generation utility** in this codebase. `GeniusAccount`'s factories (`New`, `NewFromPrivateKey`, `NewFromMnemonic`, `NewFromRandomMnemonic`) all persist keys to a `base_path` on disk — none of them are "generate and never touch disk." The lower-level `ethereum::EthereumKeyGenerator` default constructor (`ProofSystem/include/ProofSystem/EthereumKeyGenerator.hpp:31`) DOES generate a fresh random keypair purely in memory (no storage dependency) and exposes `GetEntirePubValue()` for the address — this is the correct building block for the one-time genesis ceremony. However, `GeniusAccount::Sign()` (the SHA256(SHA256(x))+secp256k1 signing routine) is a private-key-bound instance method on `GeniusAccount`, not a free function over an arbitrary keypair — the genesis-ceremony tool will need to either (a) replicate `GeniusAccount::Sign`'s ~15 lines of secp256k1 signing logic against a standalone `EthereumKeyGenerator` instance, or (b) construct a full `GeniusAccount` in a throwaway temp directory via `GeniusAccount::New` and delete the directory immediately after signing. Recommendation: (a) is more faithful to "never persisted to disk" (D-02) since (b) touches disk transiently even if cleaned up.

**Primary recommendation:** Implement `TrustedPeerRegistry` as a new class in `src/blockchain/` (or a new `src/trustedpeer/` module — see Architecture Patterns) mirroring `ValidatorRegistry`'s shape: a `cached_registry_`+`cache_mutex_` in-memory snapshot seeded from `sgns_config.json`'s new `trusted_peers` array at construction, a `SignerSetSource` lambda over that cache registered with `SecureCrdtRegistry::Register`, and `Propose`/`Sign`/`ReadIfQuorum`-wrapping methods that delegate to a `SecureCrdt` instance — no bespoke signature verification anywhere in this class. Build and test it as a standalone component (constructed/tested independently via `SecureCrdtTestNode`), matching Phase 8/9's precedent of not touching `GeniusNode`'s real startup wiring this phase — see Open Question 1 below for the reasoning and an explicit recommendation.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Genesis trusted-peer seeding | Blockchain/Node domain logic (C++ backend) | CRDT storage | Genesis trigger logic lives alongside `EnsureValidatorRegistry`-style domain code; actual persistence goes through CRDT via SecureCrdt |
| Signature verification / quorum evaluation | SecureCrdt (Phase 9) | MultiSig (Phase 8) | `SecureCrdt::AddSignature`/`ReadIfQuorum` already own this; `TrustedPeerRegistry` must not duplicate it |
| Trusted-peer set storage | CRDT / Database-Storage tier | — | `GlobalDB` via `SecureCrdt`'s `ProposeValue`/`AddSignature`/`ReadIfQuorum`, same as all other SecureCrdt-registered keys |
| Config-driven trust anchors (`bootstrapper_node`, `trusted_peers`) | Backend config loading (`GeniusNode::LoadSgnsConfig`) | — | Runtime-editable JSON, parsed once at startup, mirrors `authorized_full_node`/`bootstrap_fullnodes` exactly |
| Ephemeral genesis-signing ceremony | Standalone offline tooling (not in node runtime) | — | Per CONTEXT.md Claude's Discretion: not part of the main node binary's runtime path |

## Standard Stack

### Core
No new external libraries. This phase is 100% internal C++ composition over already-shipped in-repo components.

| Component | Location | Purpose | Why Standard |
|-----------|----------|---------|--------------|
| `sgns::securecrdt::SecureCrdt` | `src/securecrdt/SecureCrdt.hpp/.cpp` | Quorum-gated CRDT read/write wrapper | Phase 9-shipped, the ONLY sanctioned write path for registered keys |
| `sgns::securecrdt::SecureCrdtRegistry` | `src/securecrdt/SecureCrdtRegistry.hpp` | Static key-pattern -> {signer_set_source, make_instance} policy map | Phase 9-shipped, header-only static registry |
| `sgns::securecrdt::ISignedCRDTData` | `src/securecrdt/ISignedCRDTData.hpp` | Per-type payload codec + Verify + Apply interface | Phase 9-shipped interface every registered type implements |
| `sgns::multisig::VerifyPayloadSignature` | `src/multisig/MultiSig.hpp` | Signature verification primitive (used internally by SecureCrdt, not called directly by TrustedPeerRegistry) | Phase 8-shipped |
| `ethereum::EthereumKeyGenerator` | `ProofSystem/include/ProofSystem/EthereumKeyGenerator.hpp` | In-memory-only ECDSA keypair generation (no disk persistence) | Only existing utility that generates a keypair without writing to disk — needed for the ephemeral genesis ceremony |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Replicating `GeniusAccount::Sign`'s secp256k1 logic for the ephemeral ceremony | Constructing a throwaway `GeniusAccount` via `GeniusAccount::New` in a temp dir, then deleting the dir | Simpler code reuse, but the private key transiently touches disk (violates the spirit, if not the letter, of D-02's "never persisted") |
| A new `src/trustedpeer/` module | Adding `TrustedPeerRegistry` inside `src/blockchain/` alongside `ValidatorRegistry` | `src/blockchain/` risks conceptual conflation with consensus validators (CONTEXT.md explicitly says TrustedPeerRegistry is a *different* concern from ValidatorRegistry) — a sibling module under `src/` (e.g. `src/trustedpeer/`) more cleanly signals separation, at the cost of one more top-level directory |

**Installation:** None — no new packages. No `## Package Legitimacy Audit` section is needed; this phase installs zero external packages.

## Architecture Patterns

### System Architecture Diagram

```
[sgns_config.json]
      |  (trusted_peers[], bootstrapper_node)
      v
GeniusNode::LoadSgnsConfig()  ------------------------------> Blockchain::SetAuthorizedFullNodeAddress()  (existing, unrelated)
      |
      | trusted_peers[] --------------------> TrustedPeerRegistry ctor
      |                                              |
      | bootstrapper_node addr -----------------> genesis signer_set (threshold=1, one-time use)
      v
(node startup state machine — Phase 10 need NOT wire in here, see Open Question 1)

TrustedPeerRegistry
  |
  |-- cached_registry_ (in-memory, mutex-guarded)  <-- seeded at construction from trusted_peers[]
  |                                                  <-- updated ONLY after ReadIfQuorum confirms a change
  |
  |-- SignerSetSource lambda ------> reads cached_registry_ ------> registered via SecureCrdtRegistry::Register(key_pattern, entry)
  |
  |-- ProposeMembershipChange(new_list) --> SecureCrdt::ProposeValue(base_key, serialized_new_list)
  |-- SignMembershipChange(signer_addr, sig) --> SecureCrdt::AddSignature(base_key, signer_addr, sig)
  |-- TryConfirm() --> SecureCrdt::ReadIfQuorum(base_key) --> if Some(bytes): DeserializeFromBytes + Verify + Apply (updates cached_registry_)

Genesis ceremony (offline, one-time, standalone tool — NOT in node runtime path):
  ephemeral EthereumKeyGenerator() [in-memory only]
      |
      |-- sign(genesis_list_bytes) using replicated GeniusAccount::Sign-style secp256k1 routine
      |-- operator distributes: (a) genesis trusted_peers[] into sgns_config.json of every node,
      |                          (b) ephemeral pubkey into bootstrapper_node field of every node
      |-- ONE node calls TrustedPeerRegistry::SeedGenesis(list, signature) which does
      |   ProposeValue + one AddSignature(bootstrapper_addr, signature) against a
      |   genesis-only signer_set = {bootstrapper_addr}, threshold=1
      v
  [private key discarded, never persisted]
```

### Recommended Project Structure
Follow existing convention (`ValidatorRegistry` lives in `src/blockchain/`, is registered/consumed by `Blockchain`). Given CONTEXT.md's explicit note that `TrustedPeerRegistry` is a *different concern* from `ValidatorRegistry` (economic/config signers vs. consensus validators), and to keep it independently testable per Phase 8/9 precedent:

```
src/
├── trustedpeer/                      # new module, sibling to blockchain/ and securecrdt/
│   ├── TrustedPeerRegistry.hpp
│   ├── TrustedPeerRegistry.cpp
│   └── CMakeLists.txt
test/
├── src/
│   └── trustedpeer/
│       ├── trustedpeerregistry_test.cpp     # genesis + quorum-change unit/e2e tests
│       └── CMakeLists.txt                   # reuses test/src/securecrdt/securecrdt_test_node.hpp fixture
```

Discretion note (per CONTEXT.md "Claude's Discretion"): if the planner prefers to keep it in `src/blockchain/` next to `ValidatorRegistry` for build-system/dependency-graph simplicity, that is acceptable — this is not a locked decision, just a recommendation based on the codebase's existing module boundaries.

### Pattern 1: Cached signer-set source seeded from config, updated post-confirmation (D-05)
**What:** `TrustedPeerRegistry` keeps an in-memory `cached_peers_` (e.g. `std::vector<std::string>`) guarded by a `std::shared_mutex cache_mutex_`, seeded at construction time from the parsed `trusted_peers` config array, and ONLY overwritten after a membership-change value has been independently confirmed via `SecureCrdt::ReadIfQuorum` + `Verify()` + `Apply()`.
**When to use:** Exactly this phase's `SignerSetSource` implementation — this callback is invoked by `SecureCrdt::AddSignature`/`ReadIfQuorum` internals every time a signature is checked, so it must be fast and non-reentrant.
**Example (adapting `ValidatorRegistry`'s proven shape):**
```cpp
// Source: src/blockchain/ValidatorRegistry.hpp:630-634 (member declaration, adapt directly)
mutable std::shared_mutex       cache_mutex_;
std::optional<Registry>         cached_registry_;   // -> TrustedPeerRegistry: std::vector<std::string> cached_peers_
bool                             cache_initialized_ = false;

// Source: src/blockchain/ValidatorRegistry.cpp:1319-1320 (update-after-confirmation, adapt directly)
// -- inside the callback/handler that runs after ReadIfQuorum + Verify + Apply succeed:
cached_registry_    = decoded.value().registry();
cached_registry_id_ = cid;
```

**Do NOT copy this shape from `ValidatorRegistry::StoreGenesisRegistry`** (`src/blockchain/ValidatorRegistry.cpp:589-631`): that function signs an UNVERIFIED-by-quorum genesis registry with a single `sign` callback and directly serializes+`Put`s it — it never routes through anything like `SecureCrdt::ProposeValue`/`AddSignature`. Per CONTEXT.md D-01, `TrustedPeerRegistry`'s genesis must go through the real `SecureCrdt::ProposeValue`+`AddSignature(threshold=1)` flow instead, so genesis is not a special case in `SecureCrdt` (consistent with Phase 9 D-04: no "final" write, reader always re-derives trust).

### Pattern 2: Genesis trigger — analogous to `EnsureValidatorRegistry`, but decoupled
**What:** `Blockchain::EnsureValidatorRegistry` (`src/blockchain/impl/Blockchain.cpp:701-720`) is the exact trigger-logic precedent:
```cpp
// Source: src/blockchain/impl/Blockchain.cpp:701-720 (full function, current code)
outcome::result<void> Blockchain::EnsureValidatorRegistry() const
{
    if ( account_->GetAddress() != GetAuthorizedFullNodeAddress() )
    {
        return outcome::success();
    }

    std::vector<std::string> genesis_ids{ GetAuthorizedFullNodeAddress() };
    const auto              &additional = GetAdditionalGenesisValidatorAddresses();
    genesis_ids.insert( genesis_ids.end(), additional.begin(), additional.end() );
    auto registry_result = validator_registry_->StoreGenesisRegistry( genesis_ids,
                                                                      [this]( const std::vector<uint8_t> &data )
                                                                      { return account_->Sign( data ); } );
    if ( registry_result.has_error() )
    {
        logger_->error( "[{}] Failed to ensure validator registry", account_->GetAddress().substr( 0, 8 ) );
        return outcome::failure( Error::VALIDATOR_REGISTRY_CREATION_FAILED );
    }
    return outcome::success();
}
```
It is called once inside `Blockchain::New(...)`'s static factory body at `src/blockchain/impl/Blockchain.cpp:291` (`auto ensure_registry_result = instance->EnsureValidatorRegistry();`), which itself runs inside `GeniusNode`'s `INITIALIZING_BLOCKCHAIN` state (`src/account/GeniusNode.cpp:605-616`, the `blockchain_ = Blockchain::New(...)` call).

**Crucial difference for `TrustedPeerRegistry`:** `EnsureValidatorRegistry`'s trigger condition is "am I the one authorized full node?" — this maps directly to `bootstrapper_node` for the ephemeral one-time key. BUT unlike `ValidatorRegistry`'s genesis (self-signed by the node's own long-term `account_`), `TrustedPeerRegistry`'s genesis signature must come from the ephemeral, already-discarded keypair — it is physically impossible for any running node's `account_->Sign(...)` to produce it after the ceremony. So a `TrustedPeerRegistry::EnsureGenesis()`-style method **cannot** self-sign like `EnsureValidatorRegistry` does; it can only detect "genesis not yet seeded" and (if a pre-computed signature was supplied externally, e.g. baked into the genesis ceremony's output artifact) submit `ProposeValue`+`AddSignature` using that already-produced signature — never generate a new one live. This is a structural divergence from the `EnsureValidatorRegistry` pattern that the planner must account for.

**When to use:** As the shape for `TrustedPeerRegistry`'s own genesis-trigger method, adapted per the above divergence — NOT wired into `Blockchain`/`GeniusNode`'s actual startup sequence this phase (see Open Question 1).

### Pattern 3: `sgns_config.json` field parsing — exact `bootstrap_fullnodes` precedent
**What:** `GeniusNode::LoadSgnsConfig()` (`src/account/GeniusNode.cpp:322-434`, full function read in this session) parses each optional field with `HasMember`+type-check, logging a default if absent. The exact array-parsing block to clone for `trusted_peers`:
```cpp
// Source: src/account/GeniusNode.cpp:399-408 (current code, exact block to clone)
if ( config_json.HasMember( "bootstrap_fullnodes" ) && config_json["bootstrap_fullnodes"].IsArray() )
{
    for ( auto &v : config_json["bootstrap_fullnodes"].GetArray() )
    {
        if ( v.IsString() )
        {
            bootstrap_fullnodes_.push_back( v.GetString() );
        }
    }
    node_logger_->info( "sgns_config.json: loaded {} bootstrap fullnodes", bootstrap_fullnodes_.size() );
}
```
And the exact `authorized_full_node` string-field precedent `bootstrapper_node` should mirror:
```cpp
// Source: src/account/GeniusNode.cpp:410-416 (current code)
// Read authorized_full_node and immediately set it
if ( config_json.HasMember( "authorized_full_node" ) && config_json["authorized_full_node"].IsString() )
{
    const std::string addr = config_json["authorized_full_node"].GetString();
    node_logger_->info( "sgns_config.json: setting authorized_full_node" );
    Blockchain::SetAuthorizedFullNodeAddress( addr );
}
```
**Recommended new blocks to add** (insert directly after the `authorized_full_node` block, before `ipfs_cache_dir`, at `src/account/GeniusNode.cpp:417`):
```cpp
if ( config_json.HasMember( "trusted_peers" ) && config_json["trusted_peers"].IsArray() )
{
    for ( auto &v : config_json["trusted_peers"].GetArray() )
    {
        if ( v.IsString() )
        {
            trusted_peers_genesis_.push_back( v.GetString() );  // new member, e.g. std::vector<std::string>
        }
    }
    node_logger_->info( "sgns_config.json: loaded {} trusted_peers", trusted_peers_genesis_.size() );
}
if ( config_json.HasMember( "bootstrapper_node" ) && config_json["bootstrapper_node"].IsString() )
{
    bootstrapper_node_address_ = config_json["bootstrapper_node"].GetString();  // new member
    node_logger_->info( "sgns_config.json: bootstrapper_node set" );
}
```
Note: unlike `authorized_full_node`, `bootstrapper_node` should NOT call a `Set...` static setter immediately (there is no analogous `Blockchain`-level static storage needed for it) — it's a plain member on whatever owns `TrustedPeerRegistry`'s construction, passed to its constructor. `GeniusNode.cpp:399-408`'s `HasMember`+`IsArray`+push-loop shape is the load-bearing pattern to copy; the destination member/class differs.

**Signer identity confirmed:** `GeniusAccount::GetAddress()` (`src/account/GeniusAccount.cpp:778`) returns `eth_keypair_->GetEntirePubValue()` directly — the raw hex-encoded public key, no hashing/derivation. `src/multisig/MultiSig.hpp:24-25`'s doc comment confirms `VerifyPayloadSignature`'s `address` parameter is exactly this same "public address (hex-encoded public key)." This means the ephemeral keypair's `EthereumKeyGenerator::GetEntirePubValue()` is directly usable as `bootstrapper_node`'s value with zero conversion — verified in this session, matches CONTEXT.md D-03 exactly.

### Anti-Patterns to Avoid
- **Special-casing genesis inside `SecureCrdt` or `TrustedPeerRegistry`'s `Verify()`:** Per D-01/Phase 9 D-04, genesis is an ordinary threshold=1 quorum case. Do not add an `is_genesis` branch anywhere in the quorum-check path.
- **Reusing `ValidatorRegistry::CreateGenesisRegistry`/`StoreGenesisRegistry` verbatim:** those are UNSIGNED-by-`SecureCrdt`-standards (single ad-hoc `sign` callback, direct `Put`, no quorum re-derivation) — copy the SHAPE (cache pattern, trigger condition) but never the actual write path.
- **Calling `ReadIfQuorum` inside the `SignerSetSource` callback:** explicitly warned against in CONTEXT.md D-05 and confirmed as a real reentrancy risk by reading `SecureCrdt.hpp`'s doc comments — `AddSignature`/`ReadIfQuorum` invoke the registered `SignerSetSource` internally to resolve the current authorized set; calling back into `ReadIfQuorum` from inside that callback risks reentrant locking/logic loops.
- **Persisting the ephemeral genesis keypair to disk in any form** (including a "temporary" `GeniusAccount::New` directory that gets deleted) — favor replicating the signing routine over a raw `EthereumKeyGenerator` instance instead (see Summary).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Signature verification over a payload | Custom secp256k1/SHA256 verification code | `SecureCrdt::AddSignature`/`ReadIfQuorum` (internally uses `multisig::VerifyPayloadSignature`) | Already implements the exact SHA256(SHA256(x)) + secp256k1 protocol this codebase standardizes on; duplicating it is exactly what TPR-03 forbids |
| N-of-M quorum evaluation | Custom counting/threshold logic | `SecureCrdt::ReadIfQuorum` (delegates to MultiSig's quorum evaluator internally) | Phase 8/9 already solved this generically |
| CRDT key registration / filter wiring | Direct `GlobalDB::RegisterElementFilter` calls | `SecureCrdtRegistry::Register` + `SecureCrdt::RegisterFilters()` | This is precisely what Phase 9 built to avoid every consumer wiring its own filter logic |
| Ephemeral one-time signing key | A "special genesis mode" flag inside SecureCrdt | A standalone offline tool using `ethereum::EthereumKeyGenerator`'s in-memory ctor | Keeps the one-time ceremony entirely outside the node's runtime trust boundary, matching D-02 |

**Key insight:** Every piece of cryptographic/quorum logic this phase needs already exists in Phase 8 (`MultiSig`) and Phase 9 (`SecureCrdt`). `TrustedPeerRegistry`'s entire job is: (1) own the payload codec/semantics (`ISignedCRDTData`), (2) own the in-memory signer-set cache (`SignerSetSource`), (3) provide a thin convenience API over `SecureCrdt`'s three methods. Any temptation to add a parallel signature check anywhere is a TPR-03 violation and should be treated as a implementation bug, not a design choice.

## Common Pitfalls

### Pitfall 1: Treating genesis as unsigned (copying `ValidatorRegistry`'s genesis path)
**What goes wrong:** Implementer copies `StoreGenesisRegistry`'s direct-`Put`-after-single-signature pattern, bypassing `SecureCrdt::ProposeValue`/`AddSignature` entirely.
**Why it happens:** `ValidatorRegistry` is the most visible/nearest precedent in the codebase, and its genesis path looks superficially similar (propose + one signature).
**How to avoid:** Genesis MUST go through `SecureCrdt::ProposeValue(base_key, genesis_list_bytes)` then exactly one `SecureCrdt::AddSignature(base_key, bootstrapper_address, ephemeral_signature)`, with the `SignerSetSource` returning `{signer_set: {bootstrapper_address}, threshold: 1}` for that specific key/state (before any confirmed membership exists).
**Warning signs:** Any direct `db_->Put(...)` call inside `TrustedPeerRegistry` — there should be none; `SecureCrdt` is the only sanctioned `Put` caller.

### Pitfall 2: Reentrant `SignerSetSource` calling `ReadIfQuorum`
**What goes wrong:** The callback registered via `SecureCrdtRegistry::Register` calls `SecureCrdt::ReadIfQuorum` on the same key to "double check" the current state, causing reentrant invocation since `ReadIfQuorum` itself invokes the `SignerSetSource` to check quorum.
**Why it happens:** Feels natural to "just read the current confirmed value" rather than trusting a separately-maintained cache.
**How to avoid:** The `SignerSetSource` lambda must ONLY read `cached_peers_` (protected by its own mutex) — never call back into `SecureCrdt`.
**Warning signs:** Deadlocks or infinite recursion during signature verification; any `SecureCrdt` method call inside the lambda passed to `SecureCrdtRegistryEntry::signer_set_source`.

### Pitfall 3: Ephemeral key accidentally persisted
**What goes wrong:** Genesis ceremony tool constructs a `GeniusAccount` (which always writes keys under `base_path`) instead of a bare `EthereumKeyGenerator`, leaving private key material on disk even transiently.
**Why it happens:** `GeniusAccount` is the "obvious" way to get a signable identity in this codebase; its lower-level `EthereumKeyGenerator` dependency is less discoverable.
**How to avoid:** Use `ethereum::EthereumKeyGenerator()`'s default constructor directly (confirmed in-memory only, no storage dependency in its header) and replicate `GeniusAccount::Sign`'s ~15-line secp256k1 signing routine (`src/account/GeniusAccount.cpp:845-870`) against it, rather than instantiating a full `GeniusAccount`.
**Warning signs:** Any file path / `ISecureStorage` / `KeyPairFileStorage` reference anywhere near the genesis-ceremony tool's code.

### Pitfall 4: Whole-list vs delta confusion in `Verify()`
**What goes wrong:** `ISignedCRDTData::Verify()` implementation tries to diff the new proposed list against `cached_peers_` and reject "too large" changes, conflating semantic validation with the "propose whole list" representation decision (D-06).
**Why it happens:** Natural to want additional safety logic (e.g., "don't allow removing >50% of peers at once") but this isn't specified anywhere in CONTEXT.md/REQUIREMENTS.md.
**How to avoid:** `Verify()` should validate only structural well-formedness (non-empty list, no duplicate addresses, valid address format via `GeniusAccount::IsValidPublicKey`-style check) — do not invent additional quorum-adjacent business rules unless a locked decision calls for them. Flag any such idea as an open question rather than silently implementing it.
**Warning signs:** `Verify()` implementation comparing against `cached_peers_`/mutable state rather than doing pure structural checks on the payload alone.

## Code Examples

### `ValidatorRegistry`'s full genesis trigger (adapt shape, not write-path)
```cpp
// Source: src/blockchain/impl/Blockchain.cpp:701-720 (current code, read in full this session)
outcome::result<void> Blockchain::EnsureValidatorRegistry() const
{
    if ( account_->GetAddress() != GetAuthorizedFullNodeAddress() )
    {
        return outcome::success();
    }
    std::vector<std::string> genesis_ids{ GetAuthorizedFullNodeAddress() };
    const auto              &additional = GetAdditionalGenesisValidatorAddresses();
    genesis_ids.insert( genesis_ids.end(), additional.begin(), additional.end() );
    auto registry_result = validator_registry_->StoreGenesisRegistry( genesis_ids,
                                                                      [this]( const std::vector<uint8_t> &data )
                                                                      { return account_->Sign( data ); } );
    if ( registry_result.has_error() )
    {
        logger_->error( "[{}] Failed to ensure validator registry", account_->GetAddress().substr( 0, 8 ) );
        return outcome::failure( Error::VALIDATOR_REGISTRY_CREATION_FAILED );
    }
    return outcome::success();
}
```

### `ValidatorRegistry`'s cache member declarations (exact type/guard to mirror)
```cpp
// Source: src/blockchain/ValidatorRegistry.hpp:630-634 (current code)
mutable std::shared_mutex       cache_mutex_;               ///< Guards cached registry/update state.
std::optional<Registry>         cached_registry_;           ///< Cached active registry snapshot.
bool                             cache_initialized_ = false; ///< Indicates whether cache has been initialized.
```

### Test fixture to reuse (do not reinvent)
```cpp
// Source: test/src/securecrdt/securecrdt_test_node.hpp (full file read this session, 156 lines)
#include "test/src/securecrdt/securecrdt_test_node.hpp"

auto node = sgns::test::securecrdt::MakeSecureCrdtTestNode( "trustedpeer_test" );
ASSERT_NE( node, nullptr );
auto secure_crdt = std::make_shared<sgns::securecrdt::SecureCrdt>( node->db, "trustedpeer-topic" );
secure_crdt->RegisterFilters();
```
This fixture already solves the libp2p/soralog logging-initialization segfault (`EnsureLoggingSystemConfigured`) that a naive single-node `GlobalDB` test setup would hit. Phase 10's tests should call `MakeSecureCrdtTestNode` directly, exactly as Phase 9's own tests do (`securecrdt_registry_test.cpp`, `securecrdt_propose_sign_quorum_test.cpp`, etc.) — do not duplicate this scaffolding.

### `EthereumKeyGenerator`'s in-memory-only ctor (genesis ceremony building block)
```cpp
// Source: ProofSystem/include/ProofSystem/EthereumKeyGenerator.hpp:24-108 (full class, current code)
ethereum::EthereumKeyGenerator ephemeral;              // default ctor: generates a fresh random keypair, no disk I/O
std::string bootstrapper_address = ephemeral.GetEntirePubValue();  // -> goes into bootstrapper_node config field
// Signing: replicate GeniusAccount::Sign's secp256k1 routine (src/account/GeniusAccount.cpp:845-870)
// against ephemeral.get_private_key(), since Sign() itself is a GeniusAccount instance method, not
// a free function over an arbitrary EthereumKeyGenerator.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| `ValidatorRegistry`'s unsigned/ad-hoc-signed genesis (`StoreGenesisRegistry`) | Real `SecureCrdt::ProposeValue`+`AddSignature(threshold=1)` genesis flow | This phase (Phase 10) | `TrustedPeerRegistry` sets the new pattern; Phase 12 will retrofit `ValidatorRegistry` onto the same pattern (MIG-05) |
| Per-type bespoke signature/quorum logic (none existed before Phase 8/9) | Shared `MultiSig`/`SecureCrdt` primitives | Phases 8-9 (2026-07-21/23, just shipped) | `TrustedPeerRegistry` is literally the FIRST real (non-test) consumer of `SecureCrdt`/`SecureCrdtRegistry` — expect to surface any remaining rough edges in that API during integration |

**Deprecated/outdated:** None — Phase 9's API is brand new (dated 2026-07-23 in file headers) and this is its first real usage.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `EthereumKeyGenerator`'s default constructor never touches disk/storage (verified only by reading its header, not by tracing every line of its `.cpp`) | Summary, Pitfall 3, Code Examples | If it internally logs/persists somewhere (e.g. via a static PRNG seed file), D-02's "never persisted to disk" guarantee could be silently violated |
| A2 | No existing repo utility already replicates `GeniusAccount::Sign`'s routine as a free function over a raw keypair (search was header/grep-based within this session's time budget, not exhaustive) | Summary, Architecture Pattern 3 | If such a helper already exists, the planner should reuse it instead of asking implementer to hand-write a secp256k1 signing routine a second time |
| A3 | `src/trustedpeer/` as a new sibling module (vs. placing `TrustedPeerRegistry` inside `src/blockchain/`) is purely a recommendation, not validated against the project's CMake/build conventions beyond visual inspection of existing module structure | Architecture Patterns > Recommended Project Structure | If the project's CMake conventions require new components to live in blockchain/ specifically (e.g. shared build target linkage), a new top-level module could require extra CMakeLists wiring not anticipated here |

## Open Questions

1. **(RESOLVED) Should `TrustedPeerRegistry` be wired into `GeniusNode`'s real startup sequence this phase, or built/tested standalone?**
   - User confirmed: **standalone only** this phase. `TrustedPeerRegistry` is built and tested as an independently constructible/testable component (constructor + genesis-seeding + propose/sign/confirm methods, tested via `SecureCrdtTestNode` exactly like Phase 9's own tests) — no changes to `GeniusNode.cpp`'s real startup state machine or `Blockchain::New` this phase.
   - Rationale (per user decision): matches Phase 8/9 precedent and TPR-01..03's literal wording; real node wiring naturally belongs with Phase 11 (`BURN_BASIS_POINTS`), which is the first phase that actually needs `TrustedPeerRegistry` to gate something live in a running node.
   - Planner action: satisfy ROADMAP.md success criterion #1 ("a freshly-initialized genesis node's TrustedPeerRegistry contains...") via a standalone test that constructs a `TrustedPeerRegistry`, feeds it genesis config + a valid pre-computed genesis signature (produced via the test-only ceremony helper from Open Question 2), and asserts the resulting cached set matches exactly.

2. **Exact signing-ceremony tooling shape (CLI vs test-only helper vs documented manual procedure) is unspecified.**
   - What we know: CONTEXT.md explicitly leaves this to planner discretion, flags it as "likely a small standalone utility/script, not part of the main node binary's runtime path."
   - What's unclear: Whether this phase should ship an actual runnable tool (e.g. a small `tools/genesis_ceremony` C++ binary or Python/shell script invoking existing primitives) vs. just a documented manual procedure + test-only helper function used to generate fixtures for `TrustedPeerRegistry`'s own tests.
   - Recommendation: Ship a minimal test-only/tooling helper function (e.g. `GenerateGenesisCeremonyArtifact(peer_list) -> {signature, bootstrapper_address}`) usable both by unit tests and, if desired, wrapped by a thin CLI later — this satisfies "test/verify TrustedPeerRegistry's genesis path end-to-end" without committing to a production ceremony-tool binary this phase, which isn't required by TPR-01..03.

## Environment Availability

Skipped — this phase has no external tool/service/runtime dependencies beyond the existing in-repo C++ build (CMake/GCC-Clang toolchain, already provisioned for Phases 8-9). No new package managers, databases, or external services are introduced.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest (gtest), already used throughout `test/src/securecrdt/` and `test/src/blockchain/` |
| Config file | `test/src/securecrdt/CMakeLists.txt` (existing pattern to mirror for a new `test/src/trustedpeer/CMakeLists.txt`) |
| Quick run command | `ctest -R trustedpeer --output-on-failure` (once test target named accordingly) |
| Full suite command | `ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TPR-01 | Genesis node seeds initial trusted-peer set from hardcoded genesis config entry, no manual bootstrap | unit/e2e (single-node `SecureCrdtTestNode`) | `ctest -R trustedpeer_genesis` | Wave 0 (new file: `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp`) |
| TPR-02 | N-of-M quorum required for add/remove/replace; sub-quorum rejected, set unchanged | unit (propose/sign/confirm sequences, positive and negative) | `ctest -R trustedpeer_quorum` | Wave 0 (new file: `test/src/trustedpeer/trustedpeerregistry_quorum_test.cpp`) |
| TPR-03 | Implementation is a pure `ISignedCRDTData`/`SecureCrdt` consumer, no parallel signature logic | code-inspection (manual/grep-based, not automatable as a unit test) | `grep -rn "secp256k1\|VerifySignature\|VerifyPayloadSignature" src/trustedpeer/` (should show zero direct crypto calls outside delegation to SecureCrdt) | N/A — inspection gate, not a test file |

### Sampling Rate
- **Per task commit:** targeted `ctest -R trustedpeer_<area>`
- **Per wave merge:** `ctest -R trustedpeer` (full trustedpeer suite)
- **Phase gate:** full `ctest --output-on-failure` green, plus a manual TPR-03 grep-inspection pass, before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `test/src/trustedpeer/CMakeLists.txt` — new test target, modeled on `test/src/securecrdt/CMakeLists.txt`
- [ ] `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp` — covers TPR-01
- [ ] `test/src/trustedpeer/trustedpeerregistry_quorum_test.cpp` — covers TPR-02
- [ ] A genesis-ceremony test helper (see Open Question 2) producing a valid `{signature, bootstrapper_address}` pair for fixtures, reusable across both new test files

## Project Constraints (from CLAUDE.md)

No project-local `./CLAUDE.md` was found in the working directory at research time — only the user's global `~/.claude/CLAUDE.md` (developer-profile preferences: concise explanations, single strong recommendations, conservative/well-established tooling, "never guess codebase details" — all directly followed in producing this research). No project-specific coding conventions/security requirements were found to enumerate here beyond what's already captured in `.planning/codebase/CONVENTIONS.md` (not read in this session — planner should cross-check it if present, since it wasn't in this research's required-reads list).

## Sources

### Primary (HIGH confidence — read directly from repository source in this session)
- `src/blockchain/ValidatorRegistry.cpp` (lines 500-630, 1200-1320, 2200-2220) — genesis, cache, and update-after-confirmation patterns
- `src/blockchain/ValidatorRegistry.hpp` (lines 625-635) — cache member declarations
- `src/blockchain/impl/Blockchain.cpp` (lines 260-300, 495-530, 690-720) — `EnsureValidatorRegistry`, `GetAuthorizedFullNodeAddress`/`SetAuthorizedFullNodeAddress`, genesis trigger call site
- `src/account/GeniusNode.cpp` (lines 260-435, 590-650) — `LoadSgnsConfig` (full function), blockchain construction site in the node state machine
- `src/securecrdt/ISignedCRDTData.hpp` (full file, 65 lines)
- `src/securecrdt/SecureCrdt.hpp` (full file, 159 lines)
- `src/securecrdt/SecureCrdtRegistry.hpp` (full file, 145 lines)
- `test/src/securecrdt/securecrdt_test_node.hpp` (full file, 156 lines)
- `src/account/GeniusAccount.hpp`/`.cpp` (factory methods, `Sign`, `VerifySignature`, `GetAddress`)
- `ProofSystem/include/ProofSystem/EthereumKeyGenerator.hpp` (full file, 143 lines)
- `src/multisig/MultiSig.hpp` (top of file, `VerifyPayloadSignature` declaration + doc comment)
- `.planning/phases/10-trustedpeerregistry/10-CONTEXT.md`, `.planning/REQUIREMENTS.md`, `.planning/STATE.md`

### Secondary / Tertiary
None used — all findings this phase were verifiable directly against repository source; no web research was needed for this internal-composition phase.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no external libraries, all internal APIs read directly from source this session
- Architecture: HIGH — `ValidatorRegistry`/`Blockchain`/`GeniusNode` reference code read in full for all cited line ranges; Phase 9 API confirmed unchanged
- Pitfalls: MEDIUM-HIGH — derived from direct code reading + CONTEXT.md's own explicit warnings (D-05 reentrancy note); Pitfall 3/4 are reasoned extrapolations, not directly observed bugs
- Ephemeral key generation gap (Assumptions A1/A2): MEDIUM — confirmed by header inspection only, not full `.cpp`/build-time verification

**Research date:** 2026-07-23
**Valid until:** 30 days (stable, purely internal codebase composition; no external dependency drift risk)
