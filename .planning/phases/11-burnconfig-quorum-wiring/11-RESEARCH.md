# Phase 11: BurnConfig Quorum Wiring - Research

**Researched:** 2026-07-24
**Domain:** C++ CRDT quorum-signed config wiring (GeniusNode startup integration)
**Confidence:** HIGH

## Summary

This phase is a pure wiring exercise: all the machinery it needs (`SecureCrdt`, `SecureCrdtRegistry`, `TrustedPeerRegistry`) already exists and is shipped (Phases 8-10 are complete in this codebase, contrary to `.planning/STATE.md`'s stale "Phase 10 executing" status — confirmed by reading the actual source: `src/trustedpeer/TrustedPeerRegistry.{hpp,cpp}` and `src/securecrdt/SecureCrdt.{hpp,cpp}` exist, are fully implemented, tested, and their doc comments explicitly name "Phase 11 BurnConfig" as the next consumer). The work is: (1) create a new `BurnConfigPayload : ISignedCRDTData` + thin `BurnConfig` wrapper class mirroring `TrustedPeerListPayload`/`TrustedPeerRegistry`'s existing shape, (2) construct a `SecureCrdt` instance, a `TrustedPeerRegistry` instance, and the new `BurnConfig` instance inside `GeniusNode::StateTransition`'s `INITIALIZING_TRANSACTIONS` case (`GeniusNode.cpp:696-708`), (3) auto-seed BurnConfig genesis (value=100) if this node is a trusted peer and genesis hasn't landed yet, (4) pass a cached-value + refresh-callback into `TransactionManager` so `PayEscrow` never does a live CRDT read.

**Primary recommendation:** Add a new `src/account/BurnConfig.{hpp,cpp}` (thin, alongside `TransactionManager`, not a new CMake library) containing `BurnConfigPayload` and `BurnConfig`. Construct `SecureCrdt`/`TrustedPeerRegistry`/`BurnConfig` in `GeniusNode::StateTransition`'s `INITIALIZING_TRANSACTIONS` case, immediately before the existing `TransactionManager::New(...)` call, then pass `BurnConfig`'s cached value (read once at construction) and a refresh callback into `TransactionManager::New` via two new trailing parameters. `TransactionManager` stores a `std::atomic<uint64_t> burn_basis_points_` (no mutex needed for a single scalar) instead of the current `static constexpr BURN_BASIS_POINTS`, replacing the one `PayEscrow` read site (`TransactionManager.cpp:800`).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| BurnConfig genesis/quorum signing & storage | Backend / CRDT layer (`src/securecrdt`, `src/trustedpeer`) | — | Existing SecureCrdt machinery already owns propose/sign/quorum for CRDT-backed config values |
| BurnConfig auto-seed-at-startup decision | Backend (`GeniusNode` state machine) | — | Requires node's own address + `TrustedPeerRegistry::GetCurrentPeers()`, both only available inside `GeniusNode` |
| Cached burn-rate read on `PayEscrow` | Backend (`TransactionManager`) | — | `PayEscrow` is a hot instance-method path; must never block on CRDT I/O |
| CRDT-change → cache refresh callback | Backend (`GlobalDB` filter/callback machinery) | `TransactionManager` (cache owner) | Reuses `GlobalDB::RegisterNewElementCallback`, same idiom as `ValidatorRegistry::RegistryUpdateReceived` |

## User Constraints (from CONTEXT.md)

### Locked Decisions
- D-01: BurnConfig genesis (=100) is proposed+signed by real trusted peers, no ephemeral key (unlike `TrustedPeerRegistry`'s own Phase 10 genesis, which DOES require an ephemeral bootstrapper ceremony — see Pitfall 1 below, this distinction matters).
- D-02: Auto-triggered at `GeniusNode` startup: if this node's own address is among `TrustedPeerRegistry::GetCurrentPeers()` AND BurnConfig has never been seeded (`SecureCrdt::ReadIfQuorum` on the BurnConfig key returns absent), the node proposes+signs `BURN_BASIS_POINTS=100` itself. Other trusted-peer nodes do the same independently, converging via CRDT.
- D-03: Auto-signing is STRICTLY limited to genesis (the known default=100). Any future change proposal must NEVER be auto-signed — that's explicit out-of-band operator action, out of scope.
- D-04: `TrustedPeerRegistry` + `BurnConfig` constructed inside `GeniusNode`'s `INITIALIZING_TRANSACTIONS` state, at/around where `TransactionManager::New` is called (`GeniusNode.cpp` ~line 692-708).
- D-05: BurnConfig has its own separately configurable N-of-M threshold, independent of `TrustedPeerRegistry`'s own membership-change threshold.

### Claude's Discretion
- Exact `BurnConfigPayload : ISignedCRDTData` class shape and serialization format (mirrors `TrustedPeerListPayload` — a small integer payload, `Verify()` checks it's `<= BASIS_POINTS_TOTAL`).
- Exact `BurnConfig`/wrapper class name and where it lives in the source tree (likely `src/account/` alongside `TransactionManager`, or a small new `src/burnconfig/`).
- Exact CRDT key name/`HierarchicalKey` for the BurnConfig value.

### Deferred Ideas (OUT OF SCOPE)
- A CLI/tooling for an operator to propose and sign a BurnConfig change post-genesis — explicitly out of this phase's scope (D-03). Future phase/milestone item.

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| BURN-01 | `BURN_BASIS_POINTS` becomes a `TrustedPeerRegistry`-quorum-signed CRDT value instead of a compile-time constant | `BurnConfigPayload`/`BurnConfig` design below, built directly on shipped `SecureCrdt`/`SecureCrdtRegistry`/`TrustedPeerRegistry` |
| BURN-02 | `TransactionManager` caches the current value and refreshes it via a CRDT-change callback (no CRDT read per `PayEscrow` call) | Cached-atomic + `GlobalDB::RegisterNewElementCallback` pattern (mirrors `ValidatorRegistry::RegistryUpdateReceived`) |
| BURN-03 | Existing behavior preserved by default — genesis seeds `BURN_BASIS_POINTS=100` until a quorum-signed update changes it | Auto-seed-at-startup logic in `GeniusNode::INITIALIZING_TRANSACTIONS`, falls back to hardcoded 100 pre-quorum |

## Project Constraints (from CLAUDE.md)

No project-local `./CLAUDE.md` exists in the SuperGNUS working directory (only the user's global `~/.claude/CLAUDE.md`, which contains only developer-preference directives, no codebase-specific rules). No additional constraints beyond the global directives (concise explanations, verify claims against actual source before asserting — already applied throughout this research).

## Standard Stack

No new external dependencies. This phase is 100% internal wiring of already-shipped in-repo components.

### Core (existing, reused)
| Component | Location | Purpose | Why Standard (in this codebase) |
|-----------|----------|---------|----------------------------------|
| `SecureCrdt` | `src/securecrdt/SecureCrdt.{hpp,cpp}` | `ProposeValue`/`AddSignature`/`ReadIfQuorum` over a registered CRDT key | Sole sanctioned write path (D-03 of Phase 9); already used by `TrustedPeerRegistry` |
| `SecureCrdtRegistry` | `src/securecrdt/SecureCrdtRegistry.hpp` | Static key-pattern → {signer-set source, quorum, payload type} registry | Header-only static registry BurnConfig must register into |
| `TrustedPeerRegistry` | `src/trustedpeer/TrustedPeerRegistry.{hpp,cpp}` | Genesis-seeded, cached, quorum-updatable trusted-peer set; provides `GetCurrentPeers()` | Explicitly built as BurnConfig's signer-set-source dependency (its own header doc names Phase 11 as consumer) |
| `ISignedCRDTData` | `src/securecrdt/ISignedCRDTData.hpp` | Interface: payload codec + `Verify()` + `Apply()` | `BurnConfigPayload` must implement this |

### New component this phase adds
| File | Purpose |
|------|---------|
| `src/account/BurnConfig.hpp` / `.cpp` | `BurnConfigPayload : ISignedCRDTData` + `BurnConfig` wrapper class (genesis auto-seed logic, cache, callback registration) |

**Installation:** N/A — no new external packages. `src/account/CMakeLists.txt`'s existing `sgns_genius_account` target already links `securecrdt`/`trustedpeer`? — **verify and add if missing** (see CMake Wiring section below; current `target_link_libraries` list must be checked for these two libs).

## Package Legitimacy Audit

Not applicable — no external packages are installed in this phase. All dependencies are pre-existing in-repo libraries (`securecrdt`, `trustedpeer`, `crdt_globaldb`, `multisig`).

## Architecture Patterns

### System Architecture Diagram

```
GeniusNode ctor
    │  account_ = <parsed keypair>              (GeniusNode.cpp:270, well before any state)
    │
    ▼
StateTransition(INITIALIZING_DATABASE)
    │  tx_globaldb_ = GlobalDB::New(...)         (GeniusNode.cpp ~1477, InitDatabase())
    │  account_->ConfigureDatabaseDependencies(tx_globaldb_)
    ▼
StateTransition(INITIALIZING_BLOCKCHAIN)
    │  blockchain_ = Blockchain::New(tx_globaldb_, account_, pubsub_, callback)
    ▼   (async callback fires once blockchain confirms started)
StateTransition(INITIALIZING_TRANSACTIONS)     <-- THIS PHASE'S WIRING POINT
    │
    │  1. secure_crdt_ = std::make_shared<SecureCrdt>(tx_globaldb_, <topic>)
    │     secure_crdt_->RegisterFilters()
    │
    │  2. trusted_peer_registry_ = TrustedPeerRegistry::New(
    │         secure_crdt_, trusted_peers_genesis_, bootstrapper_node_address_,
    │         <tpr_quorum_threshold>, <HierarchicalKey>)
    │     (registers its own signer-set-source with SecureCrdtRegistry
    │      as a SIDE EFFECT of New() -- must happen before BurnConfig
    │      registers, since BurnConfig's signer-set-source calls
    │      TrustedPeerRegistry::GetCurrentPeers())
    │
    │  3. burn_config_ = BurnConfig::New(
    │         secure_crdt_, trusted_peer_registry_, <burn_quorum_threshold>,
    │         account_->GetAddress())
    │     -- BurnConfig::New internally:
    │        a. registers BurnConfigPayload's signer-set-source (reads
    │           trusted_peer_registry_->GetCurrentPeers() + burn_quorum_threshold)
    │        b. registers a GlobalDB new-element callback on the BurnConfig
    │           key to refresh a cached std::atomic<uint64_t>
    │        c. ReadIfQuorum() once synchronously to seed the initial cache
    │           value (100 if absent -- BURN-03 default)
    │        d. IF absent AND account_->GetAddress() is among
    │           trusted_peer_registry_->GetCurrentPeers(): auto-propose +
    │           auto-sign BURN_BASIS_POINTS=100 (D-02, D-03 -- ONLY this
    │           exact genesis value, never any other)
    │
    │  4. transaction_manager_ = TransactionManager::New(
    │         tx_globaldb_, io_, account_, blockchain_, is_full_node_,
    │         subnet_id_, <existing timestamp/mutability args>,
    │         burn_config_->GetCachedBasisPoints(),      <- NEW param
    │         burn_config_)                              <- NEW param (or a
    │                                                        raw refresh-callback
    │                                                        registration, see
    │                                                        "Don't Hand-Roll")
    ▼
transaction_manager_->Start()
```

Data flow for a live quorum-signed update (post-genesis, out of this phase's scope to trigger, but must work correctly when it happens): an operator (future CLI, deferred) calls `BurnConfig::ProposeChange`/`SignChange` on some node → `SecureCrdt::ProposeValue`/`AddSignature` → CRDT delta propagates to all listening nodes → each node's `GlobalDB::RegisterNewElementCallback` fires → `BurnConfig`'s registered callback re-runs `ReadIfQuorum` (or verifies the incoming element directly, mirroring `ValidatorRegistry::RegistryUpdateReceived`) → updates the cached atomic → `TransactionManager`'s next `PayEscrow` call reads the new value with zero CRDT I/O.

### Recommended Project Structure
```
src/account/
├── TransactionManager.hpp/.cpp   # existing -- gains cached burn_basis_points_ + New() params
├── BurnConfig.hpp                # NEW -- BurnConfigPayload + BurnConfig class declarations
├── BurnConfig.cpp                # NEW -- implementation
└── GeniusNode.cpp                # gains ~15-25 lines in INITIALIZING_TRANSACTIONS case
```

### Pattern 1: `ISignedCRDTData` payload (mirror `TrustedPeerListPayload`)
**What:** A minimal payload class serializing a single `uint64_t` basis-points value.
**When to use:** For `BurnConfigPayload`.
**Example (adapting `TrustedPeerListPayload`'s shape, `src/trustedpeer/TrustedPeerRegistry.hpp:38-67`):**
```cpp
// src/account/BurnConfig.hpp -- adapted from TrustedPeerListPayload
class BurnConfigPayload : public sgns::securecrdt::ISignedCRDTData
{
public:
    BurnConfigPayload() = default;
    explicit BurnConfigPayload( uint64_t basis_points ) : basis_points_( basis_points ) {}

    std::vector<uint8_t> SerializeToBytes() const override;      // e.g. decimal ASCII or fixed 8-byte LE
    bool DeserializeFromBytes( const std::vector<uint8_t> &bytes ) override;
    bool Verify( const std::vector<uint8_t> &payload ) const override; // <= BASIS_POINTS_TOTAL (10000), structural only -- never diff against cached state (mirrors TrustedPeerListPayload's Pitfall-4 guidance)
    void Apply() override;   // typically a no-op marker; BurnConfig itself owns the cache update

    uint64_t GetBasisPoints() const { return basis_points_; }
private:
    uint64_t basis_points_ = 0;
    bool applied_ = false;
};
```

### Pattern 2: Cache-refresh-on-CRDT-callback (mirror `ValidatorRegistry::RegistryUpdateReceived`)
**What:** Register a `GlobalDB::RegisterNewElementCallback` that decodes the incoming element and updates an atomic cache — never a live re-read on the hot path.
**When to use:** `BurnConfig`'s constructor/`New()`.
**Example (adapted from `src/blockchain/ValidatorRegistry.cpp:1231-1256`, and `TransactionManager.cpp:226-235`'s existing `RegisterNewElementCallback` idiom in the SAME file this phase touches):**
```cpp
// Inside BurnConfig::New(...), after constructing the instance:
const std::string pattern = "^/?" + std::string( base_key.GetKey() );  // matches base_key.GetKey() escaping used elsewhere
instance->secure_crdt_->RegisterFilters(); // already called once per-process elsewhere; BurnConfig relies on SecureCrdt's own filter registration, does NOT need its own RegisterElementFilter -- SecureCrdt's FilterSecureCrdtUpdate already enforces quorum/signature validity for ALL registered keys (D-03 second layer). BurnConfig only needs the NEW-ELEMENT callback for cache refresh:
instance->db_->RegisterNewElementCallback(
    pattern,
    [weak_self = std::weak_ptr<BurnConfig>( instance )]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
    {
        if ( auto strong = weak_self.lock() )
        {
            strong->OnCrdtElementChanged( std::move( new_data ), cid );
        }
    } );
```
`OnCrdtElementChanged` should call `secure_crdt_->ReadIfQuorum(base_key)` (re-deriving quorum fresh, per `SecureCrdt::ReadIfQuorum`'s own doc: "always re-derives trust... never a 'final' marker") rather than trusting `new_data` directly, since a `sig/<addr>` child element arriving might be the one that pushes quorum over the threshold — the *value* element and the *signature that completes quorum* can arrive as separate CRDT elements. This mirrors why `ValidatorRegistry`'s `RegistryUpdateReceived` re-decodes rather than trusting positionally. **This is a load-bearing distinction from `ValidatorRegistry`'s pattern (see Pitfall 2 below).**

### Pattern 3: `TransactionManager` cached value + New() signature extension
**What:** Add a cached atomic + two new trailing parameters to `TransactionManager::New`.
**Example:**
```cpp
// TransactionManager.hpp (~line 99-107), REPLACE static constexpr BURN_BASIS_POINTS with instance state:
static std::shared_ptr<TransactionManager> New(
    std::shared_ptr<crdt::GlobalDB>          processing_db,
    std::shared_ptr<boost::asio::io_context> ctx,
    std::shared_ptr<GeniusAccount>           account,
    std::shared_ptr<Blockchain>              blockchain,
    bool                                     full_node           = false,
    uint16_t                                 subnet_id           = 0,
    std::chrono::milliseconds                timestamp_tolerance = std::chrono::milliseconds( 300000 ),
    std::chrono::milliseconds                mutability_window   = std::chrono::milliseconds( 0 ),
    uint64_t                                 initial_burn_basis_points = BURN_BASIS_POINTS_DEFAULT, // NEW
    std::shared_ptr<account::BurnConfig>     burn_config = nullptr );                                // NEW, optional

// member: std::atomic<uint64_t> burn_basis_points_{ initial_burn_basis_points };
// PayEscrow (TransactionManager.cpp:800), REPLACE:
//   const auto burn_amount = ( escrow_amount * BURN_BASIS_POINTS ) / BASIS_POINTS_TOTAL;
// WITH:
//   const auto burn_amount = ( escrow_amount * burn_basis_points_.load( std::memory_order_relaxed ) ) / BASIS_POINTS_TOTAL;
```
Register the refresh callback in `TransactionManager::New` (after construction, alongside the other `RegisterNewElementCallback` calls at `TransactionManager.cpp:226-235`) via a `burn_config->RegisterRefreshCallback([weak_ptr](uint64_t new_value){ if (auto strong = weak_ptr.lock()) strong->burn_basis_points_.store(new_value, std::memory_order_relaxed); })`-style API on `BurnConfig`, rather than having `TransactionManager` itself talk to `GlobalDB`/`SecureCrdt` directly. This keeps `TransactionManager` decoupled from CRDT/quorum internals (matches D-04's framing: `TransactionManager` should not need to know about `TrustedPeerRegistry` at all).

### Anti-Patterns to Avoid
- **Trusting `new_data` positionally in the callback:** Always re-run `ReadIfQuorum` inside the callback rather than assuming the just-arrived element IS the quorum-confirmed value (see Pitfall 2).
- **`TransactionManager` reaching into `SecureCrdt`/`TrustedPeerRegistry` directly:** Keep `BurnConfig` as the sole intermediary; `TransactionManager` should only see a plain `uint64_t` cache + a registration hook, not CRDT types (isolates `TransactionManager.hpp`'s include list from `securecrdt`/`trustedpeer` headers).
- **Auto-signing on `RegisterNewElementCallback` firing for ANY value:** The auto-sign-genesis logic (D-02/D-03) must run exactly ONCE, at `BurnConfig::New()` construction time, gated on `ReadIfQuorum` returning absent AND this node being a trusted peer — NEVER inside the ongoing refresh callback (which must only ever update the cache, never sign anything).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Signature/quorum verification for the BurnConfig value | Bespoke sign/verify/count logic | `SecureCrdt::ProposeValue`/`AddSignature`/`ReadIfQuorum` + `SecureCrdtRegistry` | This is exactly what Phase 9 built and TPR-03 mandates reuse of; `SecureCrdt`'s doc explicitly forbids bespoke logic for registered keys |
| Determining "is this node a trusted peer" | Re-deriving trust from CRDT elements manually | `TrustedPeerRegistry::GetCurrentPeers()` (cached, no CRDT read per D-04's own doc) | Already the exact API surface designed for this; `GetCurrentPeers()` is deliberately cache-only |
| Basis-points arithmetic bounds checking | New validation helper | `BurnConfigPayload::Verify()` checking `<= BASIS_POINTS_TOTAL` (10000), mirroring `TrustedPeerListPayload::Verify()`'s structural-only check | Consistent with existing `ISignedCRDTData` convention: `Verify()` is structural, never diffs against mutable state |

**Key insight:** Everything genuinely novel this phase needs (multisig, quorum, CRDT gating) was intentionally pre-built in Phases 8-10 specifically anticipating this phase — the `SecureCrdt.hpp` and `TrustedPeerRegistry.hpp` header doc-comments say so explicitly. Resist any temptation to add a new signing/quorum code path "for BurnConfig specifically."

## Common Pitfalls

### Pitfall 1: Confusing BurnConfig's genesis (no ephemeral key) with TrustedPeerRegistry's genesis (ephemeral key required)
**What goes wrong:** Assuming `GeniusNode` can auto-seed `TrustedPeerRegistry`'s OWN genesis the same way it auto-seeds BurnConfig's.
**Why it happens:** D-01 explicitly contrasts BurnConfig's genesis (no ephemeral key needed, real trusted peers already exist) against `TrustedPeerRegistry`'s Phase 10 genesis (which DOES require a one-time ephemeral bootstrapper keypair — confirmed by `test/src/trustedpeer/genesis_ceremony_helper.hpp`, a test-only helper explicitly never used in production code, implying `TrustedPeerRegistry`'s real genesis-seeding is an out-of-band operational ceremony, not something `GeniusNode` does automatically at every startup).
**How to avoid:** `GeniusNode` constructs `TrustedPeerRegistry::New(...)` with the parsed `trusted_peers_genesis_`/`bootstrapper_node_address_` config values as the CACHED genesis list (available via `GetCurrentPeers()` immediately per Phase 10's own test `GenesisPeersVisibleLocallyBeforeConfirmation`, confirmed at `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp:44-47` — peers are visible locally BEFORE `SeedGenesis`/`TryConfirm` are ever called). This phase does NOT need to call `TrustedPeerRegistry::SeedGenesis` at all — `GetCurrentPeers()` already returns the configured genesis list from construction, which is sufficient for BurnConfig's own auto-seed membership check (D-02). Whether/when `TrustedPeerRegistry`'s own genesis gets formally quorum-confirmed via `SeedGenesis`/`TryConfirm` is orthogonal and out of this phase's scope.
**Warning signs:** Any code path where `GeniusNode` invents/derives an ephemeral signature for `TrustedPeerRegistry`'s genesis — that would violate D-01/D-02's real-trusted-peers-only framing for a DIFFERENT registry (`TrustedPeerRegistry` itself, not BurnConfig).

### Pitfall 2: Trusting the just-arrived CRDT element as "the new value" in the refresh callback
**What goes wrong:** A signature-only delta (a `sig/<addr>` child key) can arrive as its own separate CRDT element AFTER the base value element — trusting `new_data` positionally in the callback risks caching a value before quorum is actually met, or missing the moment quorum IS met (which happens on the LAST signature's arrival, not the value's arrival).
**Why it happens:** `GlobalDB::RegisterNewElementCallback`'s pattern (`"^/?" + base_key + "(/sig/.*)?"`, per `SecureCrdtRegistry::Register`'s compiled-pattern convention) matches BOTH the base value key and every `sig/<addr>` child — the callback fires on each.
**How to avoid:** On EVERY callback firing (whether it's the base key or a `sig/<addr>` child), re-run `SecureCrdt::ReadIfQuorum(base_key)` fresh rather than trusting `new_data`'s bytes directly, exactly as `SecureCrdt::ReadIfQuorum`'s own doc mandates ("always re-derives trust from the current base_key value plus all sig/<addr> children... no 'final' marker key is ever written or read"). Only update the cache if `ReadIfQuorum` returns a non-nullopt value that differs from the current cache.
**Warning signs:** A cache update logged/observed before enough signatures have actually landed; a missed cache update when the LAST required signature arrives (since that signature's OWN key element, not the value key, is what completes quorum).

### Pitfall 3: Missing config field for BurnConfig's own quorum threshold
**What goes wrong:** D-05 mandates a threshold independent of `TrustedPeerRegistry`'s own membership-change threshold, but grep confirms `sgns_config.json` (see `example/node_test/sgns_config.json`) currently has NO threshold field at all for either registry — `trusted_peers`/`bootstrapper_node` are the only related fields, with no `trusted_peer_quorum_threshold` or `burn_config_quorum_threshold` key present anywhere in the codebase or example configs.
**Why it happens:** Phase 10 shipped `TrustedPeerRegistry::New`'s `quorum_threshold` parameter but never wired a config-driven value into `GeniusNode` (no caller of `TrustedPeerRegistry::New` exists outside tests yet — confirmed via grep, `TrustedPeerRegistry`/`SecureCrdt` are referenced nowhere in `GeniusNode.cpp`/`.hpp` today).
**How to avoid:** This phase must ADD at least one new config field (e.g. `"burn_config_quorum_threshold"`, integer, alongside the existing `trusted_peers`/`bootstrapper_node` parsing block at `GeniusNode.cpp:417-433`) with a sane default (e.g. matching genesis trusted-peer count, or a hardcoded safe default like `1` or `majority-of(trusted_peers_genesis_.size())` if unset) for BurnConfig's threshold. Whether `TrustedPeerRegistry`'s OWN membership-change threshold also needs a new config field this phase, or can default to a hardcoded value since Phase 10 didn't wire it, is a planning-time decision — flagged as an Open Question below.
**Warning signs:** Hardcoding BurnConfig's threshold as a magic number in `GeniusNode.cpp` instead of a configurable field, silently violating D-05's "separately configurable" requirement.

### Pitfall 4: Registration order between `TrustedPeerRegistry::New` and `BurnConfig::New`
**What goes wrong:** BurnConfig's `SignerSetSource` needs to call `TrustedPeerRegistry::GetCurrentPeers()` — if `BurnConfig`'s registration captures a `TrustedPeerRegistry` pointer that isn't yet constructed, or if `SecureCrdtRegistry::Register` for BurnConfig's key pattern happens before `TrustedPeerRegistry`'s is registered, this itself isn't strictly order-dependent (each key pattern is independently registered), but the CONSTRUCTION order matters: `BurnConfig::New` must receive an already-constructed `std::shared_ptr<TrustedPeerRegistry>` to close over.
**How to avoid:** Construct `TrustedPeerRegistry::New(...)` FIRST, then pass the resulting `shared_ptr` into `BurnConfig::New(...)`, exactly as shown in the Architecture Diagram above.
**Warning signs:** A `weak_ptr<TrustedPeerRegistry>` inside `BurnConfig`'s signer-set-source lambda that's `.lock()`-checked and silently returns an empty signer set on any transient nullptr — verify this doesn't mask a genuine construction-order bug in tests.

## Code Examples

### Existing `RegisterNewElementCallback` idiom (same file this phase edits)
```cpp
// Source: src/account/TransactionManager.cpp:226-235 (existing, in TransactionManager::New)
(void) instance->globaldb_m->RegisterNewElementCallback(
    "^/?" + blockchain_base + "tx/[^/]+",
    [weak_ptr( std::weak_ptr<TransactionManager>(
        instance ) )]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
    {
        if ( auto strong = weak_ptr.lock() )
        {
            strong->NewElementCallback( std::move( new_data ), cid );
        }
    } );
```

### `GlobalDB::RegisterNewElementCallback` signature
```cpp
// Source: src/crdt/globaldb/globaldb.hpp:73,174
using GlobalDBNewElementCallback = CrdtDatastore::CRDTNewElementCallback;
bool RegisterNewElementCallback( const std::string &pattern, GlobalDBNewElementCallback callback );
// impl (globaldb.cpp:582-585): delegates directly to m_crdtDatastore->RegisterNewElementCallback(...)
```

### `TrustedPeerRegistry::New` full signature (for constructing it in GeniusNode)
```cpp
// Source: src/trustedpeer/TrustedPeerRegistry.hpp:108-114
static std::shared_ptr<TrustedPeerRegistry> New(
    std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt,
    std::vector<std::string>                      genesis_peers,
    std::string                                    bootstrapper_address,
    uint64_t                                        quorum_threshold,
    sgns::crdt::HierarchicalKey                     base_key =
        sgns::crdt::HierarchicalKey( "trusted-peer-registry" ) );
```

### `SecureCrdt` constructor (only needs a `GlobalDB` shared_ptr + topic string)
```cpp
// Source: src/securecrdt/SecureCrdt.hpp:62
SecureCrdt( std::shared_ptr<sgns::crdt::GlobalDB> db, std::string topic );
// GeniusNode already has tx_globaldb_ fully initialized by INITIALIZING_TRANSACTIONS (set at GeniusNode.cpp:1477, started at :1479, well before INITIALIZING_TRANSACTIONS runs per the state machine sequence INITIALIZING_DATABASE -> INITIALIZING_BLOCKCHAIN -> INITIALIZING_TRANSACTIONS).
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| `static constexpr uint64_t BURN_BASIS_POINTS = 100` compile-time constant (`TransactionManager.hpp:52`) | Quorum-signed CRDT value, cached in an instance member, refreshed via callback | This phase (Phase 11) | `TransactionManager.hpp`'s existing comment already anticipated this: "Eventually settable via multisig CRDT config; hardcoded default until then." |

**Deprecated/outdated:** The `BURN_BASIS_POINTS` constant itself becomes dead after this phase (kept only as `BURN_BASIS_POINTS_DEFAULT` fallback constant for pre-quorum/genesis-absent behavior, per BURN-03).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | A new config field (e.g. `burn_config_quorum_threshold`) must be added to `sgns_config.json` parsing for D-05's "separately configurable threshold" | Pitfall 3 | If the planner instead hardcodes the threshold, D-05 is silently violated; low risk since this is directly inferable from grep evidence (no existing field), but the exact field NAME/default value is a planning-time judgment call, not verified against any spec |
| A2 | `TrustedPeerRegistry`'s own membership-change `quorum_threshold` (separate from BurnConfig's) also has no config field yet and needs one added, OR can default to a hardcoded safe value since Phase 10 never wired a live caller | Pitfall 3, Open Questions | If the planner assumes a config field already exists for this, the plan will fail at implementation; flagged explicitly as an Open Question for discuss-phase/planner to resolve |
| A3 | `BurnConfigPayload::SerializeToBytes()` should use a simple format (e.g. decimal ASCII or fixed-width binary uint64) rather than a more complex/versioned codec | Pattern 1 | Low risk — CONTEXT.md explicitly delegates this to Claude's Discretion; any reasonable round-trippable encoding satisfies BURN-01/02/03 |

## Open Questions

1. **(RESOLVED) Does `TrustedPeerRegistry`'s own membership-change quorum threshold get a new config field in this phase, or a hardcoded default?**
   - User confirmed: **add config fields for BOTH** thresholds (`TrustedPeerRegistry`'s own membership-change threshold AND BurnConfig's separate threshold per D-05) — this phase is already the first real wiring of config-consumption for the multi-sig framework into `GeniusNode`, so it's the natural point to do both properly rather than leaving one hardcoded.
   - **Critical follow-up security decision (new, not in original CONTEXT.md D-01..D-05 — call this D-06/D-07 in the planner's design):** Because both thresholds are read from **locally-editable JSON config**, and each node evaluates quorum independently against its OWN local `signer_set_source` (per Phase 9 D-04's "no final write, every reader re-derives trust" design), a compromised/malicious node operator could locally lower their own node's configured threshold (e.g. to 1) to make their own node wrongly "confirm" an under-signed value that honest nodes would correctly reject. This is a real, node-local integrity risk (not a network-wide forgery risk, but still dangerous if that node acts on the wrongly-confirmed value or is itself a trusted signer).
   - **Resolution:** Both `TrustedPeerRegistry::New` and `BurnConfig`'s constructor MUST validate their configured `quorum_threshold` against a computed **majority floor**: `minimum_safe_threshold = ceil(0.51 * signer_set.size())` (i.e., `(signer_set.size() * 51 + 99) / 100` in integer arithmetic, or equivalent). **If the configured threshold is below this floor, construction MUST FAIL** (return an `outcome::failure`/error, refuse to construct — NOT silently clamp upward, NOT just log a warning). This makes the 51% floor a hard, code-enforced invariant rather than a documented convention that a bad actor's edited config could silently violate. Apply this floor check to BOTH `TrustedPeerRegistry`'s own threshold and BurnConfig's separate threshold — no exceptions.
   - Planner action: add this floor-validation logic (likely a small shared helper, e.g. `ValidateQuorumThreshold(threshold, signer_set_size) -> outcome::result<void>`) called at the start of both `TrustedPeerRegistry::New` (if not already present from Phase 10 — check) and the new `BurnConfig::New`. Add a test case for each: constructing with a below-floor threshold must fail; constructing with an at-or-above-floor threshold must succeed.

2. **Exact CMake linkage: does `sgns_genius_account`/`genius_node` already link `securecrdt`/`trustedpeer`?**
   - What we know: `src/trustedpeer/CMakeLists.txt` and `src/securecrdt/CMakeLists.txt` both exist and build standalone libraries (`trustedpeer` links `PUBLIC securecrdt`; `securecrdt` links `PUBLIC crdt_globaldb multisig`).
   - What's unclear: Whether `src/account/CMakeLists.txt`'s `sgns_genius_account`/`genius_node`/`genius_node_test` targets already list `trustedpeer` in their link libraries (not confirmed by the grep performed — only source-file lists were checked, not full `target_link_libraries` contents for these specific libs).
   - Recommendation: Planner's first task should be a quick `grep -n "target_link_libraries" src/account/CMakeLists.txt` full read to confirm/add `trustedpeer` (which transitively pulls `securecrdt`) to the relevant target(s) before writing `BurnConfig.cpp`.

2. **Exact CMake linkage: does `sgns_genius_account`/`genius_node` already link `securecrdt`/`trustedpeer`?**
   - What we know: `src/trustedpeer/CMakeLists.txt` and `src/securecrdt/CMakeLists.txt` both exist and build standalone libraries (`trustedpeer` links `PUBLIC securecrdt`; `securecrdt` links `PUBLIC crdt_globaldb multisig`).
   - What's unclear: Whether `src/account/CMakeLists.txt`'s `sgns_genius_account`/`genius_node`/`genius_node_test` targets already list `trustedpeer` in their link libraries (not confirmed by the grep performed — only source-file lists were checked, not full `target_link_libraries` contents for these specific libs).
   - Recommendation: Planner's first task should be a quick `grep -n "target_link_libraries" src/account/CMakeLists.txt` full read to confirm/add `trustedpeer` (which transitively pulls `securecrdt`) to the relevant target(s) before writing `BurnConfig.cpp`.

## Environment Availability

Skipped — this phase has no external tool/service dependencies; it is pure C++ source/CMake wiring within the existing SuperGNUS build (same compiler/CMake toolchain already in use for Phases 8-10).

## Sources

### Primary (HIGH confidence — direct source read)
- `src/account/GeniusNode.cpp:270-292, 395-435, 596-729, 1477-1490` — account_ construction, config parsing, state machine `INITIALIZING_DATABASE`/`INITIALIZING_BLOCKCHAIN`/`INITIALIZING_TRANSACTIONS`
- `src/account/GeniusNode.hpp:805-816` — `trusted_peers_genesis_`/`bootstrapper_node_address_` member declarations
- `src/account/TransactionManager.hpp:46-53, 85-108` — current `BURN_BASIS_POINTS` constant, `New()` factory signature
- `src/account/TransactionManager.cpp:120-283, 800` — full `New()` factory body, constructor, `PayEscrow`'s burn-amount computation site
- `src/trustedpeer/TrustedPeerRegistry.hpp` (full file) — `TrustedPeerListPayload`, `TrustedPeerRegistry` full API
- `src/trustedpeer/TrustedPeerRegistry.cpp:146-160` — `New()` factory implementation
- `src/securecrdt/SecureCrdt.hpp` (full file) — constructor, `ProposeValue`/`AddSignature`/`ReadIfQuorum`/`RegisterFilters` full API + doc comments
- `src/securecrdt/SecureCrdtRegistry.hpp` (full file) — `SignerSetSnapshot`, `SecureCrdtRegistryEntry`, `Register`/`Resolve`/`AllEntries`
- `src/crdt/globaldb/globaldb.hpp:73,174`, `globaldb.cpp:582-585` — `RegisterNewElementCallback` signature + impl
- `src/blockchain/ValidatorRegistry.cpp:1230-1300` — `RegisterFilter`/`FilterRegistryUpdate`/`RegistryUpdateReceived` cache-refresh precedent
- `test/src/securecrdt/securecrdt_test_node.hpp` (full file) — single-node `GlobalDB` test fixture pattern
- `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp:1-70`, `test/src/trustedpeer/genesis_ceremony_helper.hpp` — confirms genesis peers are cache-visible pre-confirmation, confirms ephemeral-ceremony is test-only
- `src/trustedpeer/CMakeLists.txt`, `src/securecrdt/CMakeLists.txt`, `test/src/trustedpeer/CMakeLists.txt` — library/test linkage patterns
- `example/node_test/sgns_config.json` — confirms no existing threshold config field
- `.planning/REQUIREMENTS.md`, `.planning/phases/11-burnconfig-quorum-wiring/11-CONTEXT.md` — requirement IDs and locked decisions

### Secondary / Tertiary
None used — all findings verified directly against source in this session.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new external deps, all in-repo components read in full
- Architecture: HIGH — exact file:line wiring points confirmed by direct source read, not inference
- Pitfalls: HIGH — Pitfall 1/2 derived from direct comparison of Phase 10's test fixtures and `SecureCrdt.hpp`'s own doc comments; Pitfall 3 confirmed via grep showing absence of config fields

**Research date:** 2026-07-24
**Valid until:** Stable — this is internal-only wiring against already-shipped, stable in-repo APIs; re-verify only if Phase 10 code changes before Phase 11 executes.
