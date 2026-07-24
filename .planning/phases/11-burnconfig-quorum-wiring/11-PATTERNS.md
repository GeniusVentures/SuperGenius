# Phase 11: BurnConfig Quorum Wiring - Pattern Map

**Mapped:** 2026-07-24
**Files analyzed:** 8 (new + modified)
**Analogs found:** 8 / 8 (one flagged no-direct-analog: majority-floor validation helper)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|---------------|
| `src/account/BurnConfig.hpp` / `.cpp` (new) | model + service (CRDT-backed config wrapper) | event-driven (CRDT quorum propose/sign/read + change callback) | `src/trustedpeer/TrustedPeerRegistry.hpp`/`.cpp` | exact (same role: `ISignedCRDTData` payload + quorum-cached wrapper class, same constructor/`New()` shape) |
| `src/account/GeniusNode.cpp` / `.hpp` (modify, `INITIALIZING_TRANSACTIONS`) | controller / state-machine wiring | request-response (one-time construction sequence at startup) | Same file, `INITIALIZING_BLOCKCHAIN`/`INITIALIZING_DATABASE` cases + existing `TransactionManager::New(...)` call site | exact (same file, same idiom — construct dependency, wire into next state) |
| `src/account/TransactionManager.hpp` / `.cpp` (modify: replace `BURN_BASIS_POINTS` constant with cached member + callback) | service | event-driven (cache refresh on CRDT-change callback) + CRUD (read on `PayEscrow` hot path) | `src/blockchain/ValidatorRegistry.cpp` `RegisterFilter`/`RegistryUpdateReceived` (cache-refresh pattern) AND `TransactionManager.cpp:226-235`'s own existing `RegisterNewElementCallback` (same-file idiom) | exact (role/data-flow match on both counts) |
| Majority-floor validation helper (new, D-07) — e.g. `ValidateQuorumThreshold(threshold, signer_set_size)` | utility | transform (pure validation function) | **No direct analog** — closest partial precedent is `TrustedPeerListPayload::Verify()`'s structural-only validation style (validates shape, returns bool/outcome, called at construction time) | no-analog (see below) |
| New test files (e.g. `test/src/account/burnconfig_test.cpp` or `test/src/trustedpeer/...burnconfig...`) | test | request-response / CRUD | `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp`, `trustedpeerregistry_quorum_test.cpp`, `test/src/securecrdt/securecrdt_test_node.hpp` | exact (same fixture style: single-node `GlobalDB` + `SecureCrdt` test harness) |
| `src/account/CMakeLists.txt` (modify: add `trustedpeer`/`securecrdt` to `GENIUS_NODE_LIBS` and/or `sgns_genius_account` link libs) | config | batch (build config) | `test/src/trustedpeer/CMakeLists.txt`'s `TRUSTEDPEER_TEST_NODE_LIBS` list | role-match (shows exact lib names needed: `trustedpeer`, `securecrdt`, `crdt_globaldb`, `multisig`) |
| `example/node_test/sgns_config.json` (modify: add 2 new threshold fields) | config | batch | Same file's existing `trusted_peers`/`bootstrapper_node` fields | exact |

## Pattern Assignments

### `src/account/BurnConfig.hpp` / `.cpp` (new)

**Analog:** `src/trustedpeer/TrustedPeerRegistry.hpp` / `.cpp` (full files, both already read)

**Payload shape** (`TrustedPeerRegistry.hpp:38-67`, `TrustedPeerRegistry.cpp:128-131` for `Apply()`):
```cpp
class TrustedPeerListPayload : public sgns::securecrdt::ISignedCRDTData
{
public:
    TrustedPeerListPayload() = default;
    explicit TrustedPeerListPayload( std::vector<std::string> peers );

    std::vector<uint8_t> SerializeToBytes() const override;
    bool                 DeserializeFromBytes( const std::vector<uint8_t> &bytes ) override;
    bool                 Verify( const std::vector<uint8_t> &payload ) const override;
    void                 Apply() override;

    const std::vector<std::string> &GetPeers() const { return peers_; }
private:
    std::vector<std::string> peers_;
    bool                      applied_ = false;
};
```
Copy this shape verbatim for `BurnConfigPayload`, swapping `peers_` (`std::vector<std::string>`) for a single `uint64_t basis_points_`. `Verify()` must be structural-only (`<= BASIS_POINTS_TOTAL`, i.e. 10000) — never diff against cached state, exactly as `TrustedPeerListPayload::Verify()` does (see `TrustedPeerRegistry.cpp:107-126` for its structural-check style: non-empty, no duplicates, format check, no comparison to any live/mutable field).

**Wrapper class constructor + `New()` factory** (`TrustedPeerRegistry.hpp:75-114`, impl `TrustedPeerRegistry.cpp:133-160`):
```cpp
TrustedPeerRegistry::TrustedPeerRegistry( std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt,
                                           std::vector<std::string>                      genesis_peers,
                                           std::string                                   bootstrapper_address,
                                           uint64_t                                      quorum_threshold,
                                           sgns::crdt::HierarchicalKey                   base_key ) :
    secure_crdt_( std::move( secure_crdt ) ),
    base_key_( std::move( base_key ) ),
    bootstrapper_address_( std::move( bootstrapper_address ) ),
    quorum_threshold_( quorum_threshold ),
    cached_peers_( std::move( genesis_peers ) )
{
}

std::shared_ptr<TrustedPeerRegistry> TrustedPeerRegistry::New( ... )
{
    auto instance = std::make_shared<TrustedPeerRegistry>( ... );
    instance->RegisterSignerSetSource();
    return instance;
}
```
`BurnConfig::New(secure_crdt, trusted_peer_registry, quorum_threshold, node_address, base_key = HierarchicalKey("burn-config"))` should follow the identical shape: private constructor + static `New()` that does post-construction setup (register signer-set-source, register the CRDT-change callback, do the one-time `ReadIfQuorum` cache seed, run the D-07 majority-floor check, and run the D-02/D-03 genesis auto-seed check) — **use `std::make_shared` + `enable_shared_from_this` for the `weak_from_this()` capture in the callback lambda**, exactly as `TrustedPeerRegistry` does (`TrustedPeerRegistry.hpp:75`, `class TrustedPeerRegistry : public std::enable_shared_from_this<TrustedPeerRegistry>`).

**Signer-set-source registration** (`TrustedPeerRegistry.cpp:162-192`):
```cpp
void TrustedPeerRegistry::RegisterSignerSetSource()
{
    sgns::securecrdt::SecureCrdtRegistryEntry entry;
    entry.signer_set_source =
        [weak_self = weak_from_this()]( const std::string & ) -> outcome::result<sgns::securecrdt::SignerSetSnapshot>
    {
        auto self = weak_self.lock();
        if ( !self ) { return sgns::securecrdt::SignerSetSnapshot{}; }
        return self->ResolveSignerSet();
    };
    entry.make_instance = []() -> std::shared_ptr<sgns::securecrdt::ISignedCRDTData>
    {
        return std::make_shared<TrustedPeerListPayload>();
    };
    entry.owner_token = &registry_token_;
    sgns::securecrdt::SecureCrdtRegistry::Register( base_key_.GetKey(), entry );
}

outcome::result<sgns::securecrdt::SignerSetSnapshot> TrustedPeerRegistry::ResolveSignerSet() const
{
    std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
    if ( !genesis_confirmed_ ) { return sgns::securecrdt::SignerSetSnapshot{ { bootstrapper_address_ }, 1 }; }
    return sgns::securecrdt::SignerSetSnapshot{ cached_peers_, quorum_threshold_ };
}
```
`BurnConfig`'s `SignerSetSource` is simpler (no genesis/bootstrapper split needed per D-01/D-02): it always returns `SignerSetSnapshot{ trusted_peer_registry_->GetCurrentPeers(), burn_quorum_threshold_ }`. Register `BurnConfigPayload::make_instance` the same way.

**Genesis auto-seed (D-02/D-03) — SeedGenesis as the shape to follow, but simplified** (`TrustedPeerRegistry.cpp:194-215`):
```cpp
outcome::result<void> TrustedPeerRegistry::SeedGenesis( const std::vector<std::string> &genesis_peers,
                                                         std::string_view                ephemeral_signature )
{
    TrustedPeerListPayload payload( genesis_peers );
    auto propose_result = secure_crdt_->ProposeValue( base_key_, payload.SerializeToBytes() );
    if ( propose_result.has_error() ) { return propose_result.error(); }
    auto sign_result = secure_crdt_->AddSignature( base_key_, bootstrapper_address_, ephemeral_signature );
    if ( sign_result.has_error() ) { return sign_result.error(); }
    return outcome::success();
}
```
`BurnConfig`'s genesis path differs per D-01 (NO ephemeral key — signs with the *node's own* real trusted-peer signature): `secure_crdt_->ProposeValue(base_key_, BurnConfigPayload(100).SerializeToBytes())` then `secure_crdt_->AddSignature(base_key_, node_address_, <this node's own real signature over the payload>)`. This self-signing logic runs exactly once inside `BurnConfig::New()`, gated on `ReadIfQuorum(base_key_)` returning absent AND `node_address_` being present in `trusted_peer_registry_->GetCurrentPeers()` (per RESEARCH.md Anti-Patterns: never inside the ongoing refresh callback).

**Cache-refresh callback registration — copy exactly this idiom, adapted** (`ValidatorRegistry.cpp:1231-1261`, cache-refresh precedent for the callback registration shape; `TransactionManager.cpp:226-235` for the same lambda-capture idiom in the file `TransactionManager` itself will call into):
```cpp
bool ValidatorRegistry::RegisterFilter()
{
    const std::string pattern   = "/?" + std::string( RegistryKey() );
    auto              weak_self = weak_from_this();
    const bool callback_registered = db_->RegisterNewElementCallback(
        pattern,
        [weak_self]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
        {
            if ( auto strong = weak_self.lock() )
            {
                strong->RegistryUpdateReceived( std::move( new_data ), cid );
            }
        } );
    db_->AddListenTopic( std::string( ValidatorTopic() ) );
    ...
}
```
**Critical divergence (RESEARCH.md Pitfall 2):** unlike `ValidatorRegistry::RegistryUpdateReceived` (which decodes `new_data` directly), `BurnConfig`'s callback must call `secure_crdt_->ReadIfQuorum(base_key_)` fresh on every firing (base key OR any `sig/<addr>` child) rather than trusting `new_data` positionally, since a signature-only delta can be the element that completes quorum. Only update the cached atomic if the re-derived value differs from the current cache.

---

### `src/account/GeniusNode.cpp` (modify, `INITIALIZING_TRANSACTIONS` case)

**Analog:** same file, existing `INITIALIZING_TRANSACTIONS` case body (`GeniusNode.cpp:696-719`, already read)

**Current code to extend:**
```cpp
case NodeState::INITIALIZING_TRANSACTIONS:
{
    if ( !blockchain_ )
    {
        node_logger_->error( "Blockchain not initialized, cannot initialize transactions" );
        return;
    }
    transaction_manager_ = TransactionManager::New( tx_globaldb_,
                                                    io_,
                                                    account_,
                                                    blockchain_,
                                                    is_full_node_,
                                                    subnet_id_ );

    transaction_manager_->RegisterStateChangeCallback( ... );
    transaction_manager_->Start();
```
Insert construction of `secure_crdt_`, `trusted_peer_registry_`, `burn_config_` immediately BEFORE the existing `TransactionManager::New(...)` call, in that exact order (RESEARCH.md Pitfall 4: `TrustedPeerRegistry` must be fully constructed before `BurnConfig::New` closes over it). Pass `burn_config_->GetCachedBasisPoints()` and `burn_config_` as two new trailing args to `TransactionManager::New(...)`. If either `TrustedPeerRegistry::New` or `BurnConfig::New` returns a construction failure (D-07 majority-floor violation), log an error via `node_logger_->error(...)` and `return;` exactly like the existing `!blockchain_` guard above.

**Config parsing precedent** (`GeniusNode.cpp:417-434`, already read):
```cpp
if ( config_json.HasMember( "trusted_peers" ) && config_json["trusted_peers"].IsArray() )
{
    for ( auto &v : config_json["trusted_peers"].GetArray() )
    {
        if ( v.IsString() ) { trusted_peers_genesis_.push_back( v.GetString() ); }
    }
    node_logger_->info( "sgns_config.json: loaded {} trusted peers", trusted_peers_genesis_.size() );
}
if ( config_json.HasMember( "bootstrapper_node" ) && config_json["bootstrapper_node"].IsString() )
{
    bootstrapper_node_address_ = config_json["bootstrapper_node"].GetString();
    node_logger_->info( "sgns_config.json: loaded bootstrapper_node" );
}
```
Copy this exact `HasMember`/type-check/log idiom for the two new integer fields (`trusted_peer_quorum_threshold`, `burn_config_quorum_threshold`), using `.IsUint64()`/`.GetUint64()` in place of `.IsString()`/`.GetString()`, with sane defaults matching genesis trusted-peer count if unset (per RESEARCH.md Pitfall 3).

---

### `src/account/TransactionManager.hpp` / `.cpp` (modify)

**Analog:** same file's own `New()` factory + `RegisterNewElementCallback` idiom (`TransactionManager.cpp:123-260`, esp. `:226-235`, already read), plus `ValidatorRegistry`'s cache-refresh pattern above.

**Constant removal** (`TransactionManager.hpp:50-53`):
```cpp
/// Fraction of an escrow payout burned to the zero address during PayEscrow, in basis points.
/// Eventually settable via multisig CRDT config; hardcoded default until then.
static constexpr uint64_t BURN_BASIS_POINTS  = 100; // 1%
static constexpr uint64_t BASIS_POINTS_TOTAL = 10000;
```
Replace `BURN_BASIS_POINTS` with `static constexpr uint64_t BURN_BASIS_POINTS_DEFAULT = 100;` (kept only as the pre-quorum/genesis-absent fallback per BURN-03) plus a new instance member `std::atomic<uint64_t> burn_basis_points_{ BURN_BASIS_POINTS_DEFAULT };`. Keep `BASIS_POINTS_TOTAL` as-is (still used by `BurnConfigPayload::Verify()` too).

**`New()` factory signature extension** (`TransactionManager.hpp:99-107`, `TransactionManager.cpp:123-139`):
```cpp
static std::shared_ptr<TransactionManager> New(
    std::shared_ptr<crdt::GlobalDB>          processing_db,
    std::shared_ptr<boost::asio::io_context> ctx,
    std::shared_ptr<GeniusAccount>           account,
    std::shared_ptr<Blockchain>              blockchain,
    bool                                     full_node           = false,
    uint16_t                                 subnet_id           = 0,
    std::chrono::milliseconds                timestamp_tolerance = std::chrono::milliseconds( 300000 ),
    std::chrono::milliseconds                mutability_window   = std::chrono::milliseconds( 0 ) );
```
Add two new trailing params: `uint64_t initial_burn_basis_points = BURN_BASIS_POINTS_DEFAULT` and `std::shared_ptr<account::BurnConfig> burn_config = nullptr`. Thread both through to the private constructor exactly as every other param is threaded (`TransactionManager.cpp:132-139` shows the `std::move(...)` forwarding pattern into `new TransactionManager(...)`).

**Callback registration site — copy this exact idiom** (`TransactionManager.cpp:226-235`, in `TransactionManager::New`, same file):
```cpp
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
Do NOT replicate this GlobalDB-level registration for burn config directly in `TransactionManager` — per RESEARCH.md's Anti-Patterns, `TransactionManager` must stay decoupled from CRDT/quorum internals. Instead, inside `TransactionManager::New`, call `burn_config->RegisterRefreshCallback([weak_ptr(std::weak_ptr<TransactionManager>(instance))](uint64_t new_value){ if (auto strong = weak_ptr.lock()) { strong->burn_basis_points_.store(new_value, std::memory_order_relaxed); } });` — same weak-capture idiom, but through `BurnConfig`'s own registration API rather than `globaldb_m` directly.

**`PayEscrow` read site** (`TransactionManager.cpp:800`):
```cpp
const auto burn_amount = ( escrow_amount * BURN_BASIS_POINTS ) / BASIS_POINTS_TOTAL;
```
Replace with:
```cpp
const auto burn_amount = ( escrow_amount * burn_basis_points_.load( std::memory_order_relaxed ) ) / BASIS_POINTS_TOTAL;
```

---

### Majority-floor validation helper (D-07, security-critical) — NO DIRECT ANALOG

**Closest partial precedent:** `TrustedPeerListPayload::Verify()`'s style (`TrustedPeerRegistry.cpp:80-126`, already read) — a small, pure, structural-check function called at a specific gating point, returning a bool/outcome and refusing to proceed on failure. This is a STYLE precedent only (validation-at-construction convention), not a structural template — the actual computation (`ceil(0.51 * signer_set.size())` majority floor) has no existing counterpart anywhere in the codebase.

**Required shape** (per CONTEXT.md D-07 / RESEARCH.md Open Question 1 resolution):
```cpp
// New, e.g. src/securecrdt/QuorumThresholdValidation.hpp or a static helper in BurnConfig.hpp
// reused by both TrustedPeerRegistry::New and BurnConfig::New:
outcome::result<void> ValidateQuorumThreshold( uint64_t threshold, size_t signer_set_size )
{
    const uint64_t minimum_safe_threshold = ( static_cast<uint64_t>( signer_set_size ) * 51 + 99 ) / 100;
    if ( threshold < minimum_safe_threshold )
    {
        return outcome::failure( /* appropriate std::errc or project error code */ );
    }
    return outcome::success();
}
```
**Must be called at the START of both `TrustedPeerRegistry::New` (retrofit — check whether Phase 10 already has this; current read of `TrustedPeerRegistry.cpp:146-160` shows NO such check exists yet, so this is a required addition, not just new code for `BurnConfig`) and the new `BurnConfig::New`.** No silent clamping, no warning-only path — construction must fail (return `outcome::failure`, do not construct/return the `shared_ptr`) if the configured threshold is below the floor. Both `New()` factories must be updated to return `outcome::result<std::shared_ptr<T>>` instead of a bare `std::shared_ptr<T>` if they don't already support failure (confirmed: `TrustedPeerRegistry::New` currently returns a bare `std::shared_ptr<TrustedPeerRegistry>` with no failure path — this is itself a signature change needed for D-07, propagating to its caller in `GeniusNode.cpp`).

---

### New test files

**Analog:** `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp`, `test/src/trustedpeer/trustedpeerregistry_quorum_test.cpp`, `test/src/securecrdt/securecrdt_test_node.hpp` (single-node `GlobalDB` fixture, full file already read for context)

Use the same single-node `GlobalDB`/`SecureCrdt` test fixture pattern for BurnConfig tests: construct a `SecureCrdt` over an in-memory/test `GlobalDB`, construct a `TrustedPeerRegistry` with a small fixed genesis peer list, then construct `BurnConfig::New(...)` against it. Required test cases per RESEARCH.md Open Question 1: (a) constructing `BurnConfig`/`TrustedPeerRegistry` with a below-majority-floor threshold must fail; (b) at-or-above-floor threshold must succeed; (c) genesis auto-seed fires exactly once when node address is in `GetCurrentPeers()` and `ReadIfQuorum` is absent; (d) refresh callback re-derives via `ReadIfQuorum` rather than trusting positional `new_data` (Pitfall 2 regression test).

---

## Shared Patterns

### `ISignedCRDTData` payload pattern
**Source:** `src/trustedpeer/TrustedPeerRegistry.hpp:38-67`, `.cpp:80-131`
**Apply to:** `BurnConfigPayload`
Structural-only `Verify()`, `SerializeToBytes`/`DeserializeFromBytes` round-trip, `Apply()` as an internal marker (actual cache update owned by the wrapper class, not the payload).

### Quorum-cached wrapper class + static `New()` factory
**Source:** `src/trustedpeer/TrustedPeerRegistry.hpp:75-203`, `.cpp:133-283` (private ctor + static `New()` + `RegisterSignerSetSource` + `ResolveSignerSet`)
**Apply to:** `BurnConfig` class shape end-to-end

### CRDT new-element callback → cache refresh (weak_ptr-captured lambda)
**Source:** `src/blockchain/ValidatorRegistry.cpp:1231-1261` (registration) + `:1286+` (`RegistryUpdateReceived` refresh body) and `src/account/TransactionManager.cpp:226-235` (identical lambda-capture idiom already in the file this phase edits)
**Apply to:** `BurnConfig`'s CRDT-change callback registration, and `TransactionManager`'s consumption of `BurnConfig::RegisterRefreshCallback`
**Critical divergence:** re-derive via `SecureCrdt::ReadIfQuorum` on every firing rather than trusting `new_data` positionally (Pitfall 2) — this is stricter than `ValidatorRegistry`'s own pattern, which decodes `new_data` directly; do not copy that shortcut for BurnConfig.

### Config field parsing (`HasMember`/type-check/log)
**Source:** `src/account/GeniusNode.cpp:417-434`
**Apply to:** the two new `sgns_config.json` threshold fields

### CMake library linkage
**Source:** `test/src/trustedpeer/CMakeLists.txt`'s `TRUSTEDPEER_TEST_NODE_LIBS = { trustedpeer, securecrdt, crdt_globaldb, multisig, json_secure_storage, libsecp256k1::secp256k1 }`
**Apply to:** `src/account/CMakeLists.txt` — add `trustedpeer` (transitively pulls `securecrdt`) to `GENIUS_NODE_LIBS` (currently missing — confirmed by direct read of `src/account/CMakeLists.txt:16-39,75-125`, no `trustedpeer`/`securecrdt` entry present in either `sgns_genius_account`'s or `genius_node`'s link lists)

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| Majority-floor validation helper (D-07) | utility | transform | No existing quorum-floor-vs-signer-set-size check anywhere in the codebase; closest precedent (`TrustedPeerListPayload::Verify()`) is a style match only, not a structural template — this is genuinely new security logic per CONTEXT.md D-07 |

## Metadata

**Analog search scope:** `src/trustedpeer/`, `src/securecrdt/`, `src/account/`, `src/blockchain/ValidatorRegistry.cpp`, `test/src/trustedpeer/`, `test/src/securecrdt/`, `example/node_test/sgns_config.json`, `src/account/CMakeLists.txt`
**Files scanned:** `TrustedPeerRegistry.hpp/.cpp` (full), `TransactionManager.hpp` (full), `TransactionManager.cpp` (targeted: New() factory lines 110-290, PayEscrow lines 790-810), `ValidatorRegistry.cpp` (targeted: lines 1225-1300), `SecureCrdtRegistry.hpp` (full), `GeniusNode.cpp` (targeted: lines 395-440, 680-720), `src/account/CMakeLists.txt` (targeted: lines 1-160), `example/node_test/sgns_config.json` (full), `test/src/trustedpeer/CMakeLists.txt` (full)
**Pattern extraction date:** 2026-07-24
