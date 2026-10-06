# Phase 15: Private Network Identity and libp2p Gating - Pattern Map

**Mapped:** 2026-08-31
**Files analyzed:** 22 (new + modified)
**Analogs found:** 22 / 22 (12 exact, 10 role/self-analog; 0 with no analog)

All paths are relative to `/Users/henriqueklein/gnus/SGNUS` unless they start with `3rdparty/` (vendored repos at `/Users/henriqueklein/gnus/3rdparty`). Installed-header citations use `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release` as the authoritative API surface.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/peerregistry/PeerRegistry.hpp` (new) | interface (pure-virtual abstraction) | request-response (resolve signer set + quorum) | `src/securecrdt/SecureCrdtRegistry.hpp` (SignerSetSource/Snapshot types) + `src/securecrdt/ISignedCRDTData.hpp` (header-only interface style) | role-match |
| `src/peerregistry/CMakeLists.txt` (new) | build config | — | `src/trustedpeer/CMakeLists.txt` | exact |
| `src/networkregistry/NetworkRegistry.hpp` (new) | model/registry (SecureCRDT-backed) | CRUD over replicated store + event-driven cache refresh | `src/trustedpeer/TrustedPeerRegistry.hpp` | exact |
| `src/networkregistry/NetworkRegistry.cpp` (new) | model/registry impl | CRUD + event-driven | `src/trustedpeer/TrustedPeerRegistry.cpp` | exact |
| `src/networkregistry/CMakeLists.txt` (new) | build config | — | `src/trustedpeer/CMakeLists.txt` | exact |
| `src/securecrdt/SecureCrdtRegistry.hpp` (mod) | policy registry | request-response (key -> policy resolve) | self (`SecureCrdtRegistryEntry`) + `TrustedPeerRegistry::RegisterSignerSetSource` | exact (self) |
| `src/trustedpeer/TrustedPeerRegistry.{hpp,cpp}` (mod) | model/registry | CRUD + event-driven | self: implement `PeerRegistry` over existing `ResolveSignerSet`/`GetCurrentPeers` | exact (self) |
| `3rdparty/ipfs-pubsub/src/ipfs_pubsub/deny_list_connection_gater.hpp` (mod — injectable membership predicate, per 15-04) | middleware (connection gate) | request-response (per-upgrade-stage decision) | self: existing deny-list intercept ordering + mutex discipline | exact (self) |
| `3rdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.{hpp,cpp}` (mod) | service wrapper (host construction) | request-response | self: pnet ctor -> `Init(networkKey)` -> `MakeCustomHostInjector` thread-through | exact (self) |
| `src/account/GeniusNode.hpp` (mod) | config model | file-I/O | self: `NetworkSettings` struct (lines 1038-1047) | exact (self) |
| `src/account/GeniusNode.cpp` `LoadNetworkConfig` (mod) | config parsing | file-I/O | self: `read("network_key", ...)` at line 1277 | exact (self) |
| `src/account/GeniusNode.cpp` `WriteNetworkConfig` (mod) | config writer | file-I/O | self: `network_key` JSON-escape block (lines 187-209) | exact (self) |
| `src/processing/impl/processing_core_impl.{hpp,cpp}` (mod) | service (per-subtask libp2p host) | batch (host per subtask) | `3rdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:142-190` (`MakeCustomHostInjector`) | role-match |
| `src/processing/impl/TaskKeys.hpp` (mod) | utility (key builders) | transform | self: `ProcessingPrefix()` version threading + `src/crdt/hierarchical_key.hpp::ChildString` | exact (self) |
| `src/processing/impl/TaskQueueImpl.{hpp,cpp}` (mod — scope threading, 15-06) | service (CRDT task queue) | CRUD over replicated store | self: all 15 `TaskKeys::` key-construction sites (TaskQueueImpl.cpp:52-342) gain a scope argument; ctor default-arg precedent `TrustedPeerRegistry.hpp:128` | exact (self) |
| `src/processing/impl/processing_subtask_result_storage_impl.{hpp,cpp}` (mod — scoped results keys, 15-06) | storage service | CRUD over replicated store | self: literal `boost::format("results/%s")` keys (cpp:21,28,38) routed through `TaskKeys::SubTaskResultKey` | exact (self) |
| `src/account/EscrowTransaction.hpp` (mod — chain-id override, 15-06) | model (escrow tx) | — | self: inherited `GeniusTransaction::GetChainId()` virtual (GeniusTransaction.hpp:149), consumed per-tx at `TransactionManager.cpp:1333` | exact (self) |
| `src/account/GeniusTransaction.hpp` (mod) | model (chain-id constant) | — | self: `GENIUS_CHAIN_ID` (line 36) + `GetChainId()` (line 151) | exact (self) |
| `src/account/TransactionManager.{hpp,cpp}` (mod) | service (validator selection) | request-response | self: `SelectInputValidator` (`TransactionManager.cpp:1326-1355`) | exact (self) |
| `src/blockchain/ValidatorRegistry.{hpp,cpp}` (mod) | model (consensus registry) | CRUD | self: static `RegistryKey()/ValidatorTopic()` (hpp:386-399) -> instance-parameterized; default-arg precedent `TrustedPeerRegistry.hpp:128` | exact (self) |
| `src/blockchain/impl/Blockchain.cpp` (mod) | wiring/composition | — | self: single `ValidatorRegistry::New` creation site (line 122) | exact (self) |
| `example/node_test/network_config.json` (mod) | config data | file-I/O | self + `WriteNetworkConfig` output shape (GeniusNode.cpp:185-210) | exact (self) |
| `test/src/networkregistry/` (+CMakeLists, new) | test | event-driven | `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp` + `test/src/securecrdt/securecrdt_test_node.hpp` | exact |
| `test/src/pubsub_counts/pubsub_counts.cpp` (mod, D-07 layer) | integration test | event-driven (two-node network) | self: `PnetIsolationAndGaterBlocking` (lines 140-261) | exact (self) |
| `test/src/account/` config test (extend or new `network_config_test`) | test | file-I/O | `test/src/account/network_config_precedence_test.cpp` | exact |
| `test/src/peerregistry/` or securecrdt test ext (new) | test | request-response | `test/src/securecrdt/securecrdt_registry_test.cpp` | role-match |

## Pattern Assignments

### `src/networkregistry/NetworkRegistry.{hpp,cpp}` (model/registry, CRUD + event-driven)

**Analog:** `src/trustedpeer/TrustedPeerRegistry.{hpp,cpp}` — same role, same data flow; RESEARCH Pattern 3 names it "the direct template." Copy the class skeleton wholesale and swap the authority: bootstrap signer set becomes the **global TrustedPeerRegistry's current peers at TPR-majority** instead of a single ephemeral bootstrapper at threshold 1.

**Payload class co-located in the header** (TrustedPeerRegistry.hpp:38-75): `NetworkMembershipPayload : sgns::securecrdt::ISignedCRDTData` with `SerializeToBytes`/`FromBytes`/`Verify`/`Apply`. Follow the structural-only `Verify` convention (TrustedPeerRegistry.cpp:73-114): non-empty, no duplicates, valid peer encoding — never diff against cached state. Add only non-secret pnet metadata fields (version/fingerprint) per D-03; never the raw key.

**Constructor + factory with quorum floor** (TrustedPeerRegistry.cpp:120-158):
```cpp
// TrustedPeerRegistry::New — copy this shape for NetworkRegistry::New
auto validation_result = sgns::securecrdt::ValidateQuorumThreshold( quorum_threshold, genesis_peers.size() );
if ( validation_result.has_error() )
{
    return validation_result.error();   // QUORUM_THRESHOLD_BELOW_FLOOR -> construction fails
}
auto instance = std::make_shared<TrustedPeerRegistry>( ... );
instance->RegisterSignerSetSource();
return instance;
```
For D-06: the floor check applies twice — once against the TPR signer set for the bootstrap threshold, once against network membership for the self-governance threshold.

**Signer-set source registration with weak-self capture + owner token** (TrustedPeerRegistry.cpp:160-178):
```cpp
sgns::securecrdt::SecureCrdtRegistryEntry entry;
entry.signer_set_source = [weak_self = weak_from_this()](
                              const std::string & ) -> outcome::result<sgns::securecrdt::SignerSetSnapshot>
{
    auto self = weak_self.lock();
    if ( !self )
    {
        return sgns::securecrdt::SignerSetSnapshot{};
    }
    return self->ResolveSignerSet();
};
entry.make_instance = []() -> std::shared_ptr<sgns::securecrdt::ISignedCRDTData>
{ return std::make_shared<TrustedPeerListPayload>(); };
entry.owner_token = &registry_token_;
sgns::securecrdt::SecureCrdtRegistry::Register( base_key_.GetKey(), entry );
```

**Cached-only signer resolution — the re-entrancy guard (Pitfall 9)** (TrustedPeerRegistry.cpp:180-188):
```cpp
outcome::result<sgns::securecrdt::SignerSetSnapshot> TrustedPeerRegistry::ResolveSignerSet() const
{
    std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
    if ( !genesis_confirmed_ )
    {
        return sgns::securecrdt::SignerSetSnapshot{ { bootstrapper_address_ }, 1 };
    }
    return sgns::securecrdt::SignerSetSnapshot{ cached_peers_, quorum_threshold_ };
}
```
NetworkRegistry variant: pre-confirmation snapshot = TPR's `GetCurrentPeers()` snapshot held at construction with a TPR-majority `required_signatures`; post-confirmation = own `cached_network_peers_` + own `quorum_threshold_`. Never call `ReadIfQuorum` from inside this method.

**Quorum confirm -> cache overwrite** (TrustedPeerRegistry.cpp:230-265): `TryConfirm()` = `secure_crdt_->ReadIfQuorum(base_key_)` -> `FromBytes` -> `Verify` -> `Apply` -> take unique lock, overwrite `cached_peers_`, set `genesis_confirmed_`. Copy verbatim; refresh the cache from a CRDT change callback the way `BurnConfig` does (`RegisterNewElementCallback`, see BurnConfig.hpp doc block lines 100-127).

**Thread-safety members** (TrustedPeerRegistry.hpp:206-216): `mutable std::shared_mutex cache_mutex_`, cached vector + confirmed flag + `int registry_token_ = 0`, and `sgns::base::Logger logger_ = sgns::base::createLogger( "..." )`.

**Base-key default-arg convention** (TrustedPeerRegistry.hpp:123-128): `sgns::crdt::HierarchicalKey base_key = sgns::crdt::HierarchicalKey( "trusted-peer-registry" )` — NetworkRegistry should take the key non-defaulted (e.g. `network-registry/<privateNetworkId>`, per RESEARCH Open Question 3 recommendation) so per-network instances cannot collide.

---

### `src/peerregistry/PeerRegistry.hpp` (interface, request-response)

**Analog:** `src/securecrdt/SecureCrdtRegistry.hpp` for the value types; `src/securecrdt/ISignedCRDTData.hpp` for the header-only pure-virtual style (include guard `SGNS_<MODULE>_<NAME>_HPP`, doxygen `@file/@brief/@date`, namespace `sgns::<module>`).

**Types to reuse, not redeclare** (SecureCrdtRegistry.hpp:33-44):
```cpp
struct SignerSetSnapshot
{
    std::vector<std::string> signer_set;
    uint64_t                 required_signatures = 0;
};
using SignerSetSource =
    std::function<outcome::result<SignerSetSnapshot>( const std::string &base_key )>;
```
Minimal recommended surface (RESEARCH sketch is a recommendation; exact shape is discretion):
```cpp
class PeerRegistry
{
public:
    virtual ~PeerRegistry() = default;
    virtual outcome::result<securecrdt::SignerSetSnapshot> CurrentSignerSet() const = 0; // cached-only
    virtual std::vector<std::string> GetCurrentPeers() const = 0;   // for gater allow-list polling
    virtual crdt::HierarchicalKey BaseKey() const = 0;
};
```
Keep it header-only like QuorumThresholdValidation.hpp. Place in new lowercase module dir `src/peerregistry/` (convention: `trustedpeer/`, `securecrdt/`).

**Adapting TrustedPeerRegistry to implement it:** the methods above already exist with compatible semantics (`GetCurrentPeers` hpp:175, `ResolveSignerSet` cpp:180) — add `: public sgns::peerregistry::PeerRegistry` and override; no logic changes.

---

### `src/securecrdt/SecureCrdtRegistry.hpp` (mod — explicit PeerRegistry association)

**Analog:** self + the ownership precedent in `src/account/BurnConfig.hpp:94-99`:
```cpp
BurnConfig( std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
            std::shared_ptr<sgns::crdt::GlobalDB>                   db,
            std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry,  // <-- shared_ptr registry
            uint64_t                                                quorum_threshold, ... );
```
D-04 evolution: add an optional `std::shared_ptr<PeerRegistry>` (or `std::weak_ptr` — discretion; shared matches BurnConfig) to `SecureCrdtRegistryEntry` (struct at lines 50-59). When present, `Resolve()`-returned entries' `signer_set_source` adapts the registry (`[registry]( const std::string & ){ return registry->CurrentSignerSet(); }`). Do NOT change the Register/Resolve regex contract (lines 78-84) — `"/?" + key_pattern + "(/sig/[^/]+)?"` must stay byte-compatible for `trusted-peer-registry` and `burn-config`.

---

### `3rdparty/ipfs-pubsub` membership gater (new) + `gossip_pubsub.{hpp,cpp}` (mod)

**Analog (exact):** installed `3rdparty/build/OSX/Release/ipfs-pubsub/include/ipfs_pubsub/deny_list_connection_gater.hpp` (source of truth is `3rdparty/ipfs-pubsub/src/ipfs_pubsub/`). Copy the class shape and invert semantics: deny-list becomes membership allow-list backed by `std::function<bool(const libp2p::peer::PeerId&)>`.

**Class skeleton + mutex discipline** (deny_list_connection_gater.hpp:26-37):
```cpp
class DenyListConnectionGater final : public libp2p::network::ConnectionGater
{
public:
    void BlockPeer( const libp2p::peer::PeerId &peer )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_denied_peers.insert( peer );
    }
    // ...
```

**Intercept-stage ordering — copy exactly** (deny_list_connection_gater.hpp:100-132):
```cpp
libp2p::outcome::result<void> interceptAccept( const libp2p::multi::Multiaddress &local,
                                               const libp2p::multi::Multiaddress &remote ) override
{
    // Remote peer identity is not established yet at this stage.
    // The connection gets checked in interceptSecured once the
    // security handshake reveals the remote peer id.
    return libp2p::outcome::success();
}
libp2p::outcome::result<void> interceptSecured( bool is_initiator,
                                                const libp2p::peer::PeerId &remote_peer,
                                                const libp2p::multi::Multiaddress &remote_addr ) override
{
    if ( IsPeerBlocked( remote_peer ) )
    {
        return libp2p::network::ConnectionGaterError::GATER_REJECTED_SECURED;
    }
    return libp2p::outcome::success();
}
```
Allow-list variant: check membership at `interceptPeerDial`, `interceptSecured`, `interceptUpgraded`; pass through `interceptAccept` (peer unknown at raw stage). Decide fail-open/fail-closed for unconfirmed registry explicitly (RESEARCH Open Question 4 recommends fail-closed in private mode) and test both. Compose deny-list AND membership in one gater (deny wins) or chain them — but `MakeCustomHostInjector` binds exactly one `network::ConnectionGater`.

**Interface verified at** `3rdparty/build/OSX/Release/libp2p/include/libp2p/network/connection_gater.hpp:33-70` (five pure-virtual `intercept*` returning `outcome::result<void>`).

**Surfacing through GossipPubSub** — follow the existing `BlockPeer` forwarding pattern (gossip_pubsub.cpp:227-253) and the pnet-constructor threading (gossip_pubsub.cpp:219-225 ctor; 255-277 `Init`):
```cpp
// gossip_pubsub.cpp:255-270 (installed + vendored identical)
void GossipPubSub::Init( std::optional<libp2p::crypto::KeyPair> keyPair, std::optional<std::string> networkKey )
{
    m_connection_gater = std::make_shared<DenyListConnectionGater>();
    if ( networkKey )
    {
        auto injector = MakeCustomHostInjector(
            std::move( keyPair ), m_connection_gater, libp2p::injector::usePrivateNetwork( *networkKey ) );
        InitHostFromInjector( std::move( injector ) );
    }
    // ...
```
The extension adds a membership source to the gater (constructor arg or setter) and, if a gater-accepting construction path is chosen, a new `GossipPubSub` overload mirroring the three-arg pnet ctor (installed gossip_pubsub.hpp:109-111). Keep the eager-throw contract documented on the pnet ctor.

---

### `src/processing/impl/processing_core_impl.{hpp,cpp}` (mod — D-11 processing-host gating)

**Analog (role-match):** `MakeCustomHostInjector` in `3rdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:142-190` — the proven Noise-only + gater + pnet binding set. Current SGNUS code at `processing_core_impl.cpp:92-98` is the anti-pattern to replace:
```cpp
// CURRENT (Plaintext+Noise defaults; no gater; no pnet) — processing_core_impl.cpp:96
auto injector = libp2p::injector::makeHostInjector(
    libp2p::injector::makeKademliaInjector( libp2p::injector::useKademliaConfig( kademlia_config ) ) );
```
Target shape (bindings from gossip_pubsub.cpp:173-187):
```cpp
auto injector = libp2p::injector::makeHostInjector<di::extension::shared_config>(
    libp2p::injector::makeKademliaInjector<di::extension::shared_config>(
        libp2p::injector::useKademliaConfig( kademlia_config ) ),
    libp2p::injector::useSecurityAdaptors<libp2p::security::Noise>(),   // D-11: no Plaintext
    di::bind<network::ConnectionGater>().TEMPLATE_TO( gater )[di::override],
    libp2p::injector::usePrivateNetwork( network_key_text ) );          // pnet mode only
```
Verified injector API (installed `network_injector.hpp`): `useSecurityAdaptors` line 208, `useConnectionGater` line 254, `useAllowLoopbackDial` line 275, `usePrivateNetwork(std::string_view)` line 308 (throws `PskValidationError` eagerly), `usePrivateNetwork(Psk)` line 341 (exception-free).

**Construction threading** — extend `ProcessingCoreImpl::New`/ctor the way the existing signature reads (processing_core_impl.cpp:33-53: takes task queue, count, TokenID; add the node's private-network context). Call site to update: `GeniusNode.cpp:1641`:
```cpp
processing_core_ = processing::ProcessingCoreImpl::New( task_queue_, 1, dev_config_.TokenID );
```
Wrap any pnet-mode injector build in try/catch -> init failure, per the `StartPubSub` pattern below. The host is built **per `ProcessSubTask`** (Pitfall 8) — bindings go inside that loop site, not node start.

---

### `src/account/GeniusNode.{hpp,cpp}` (mod — config identity, D-01/D-02)

**Analog (exact self):** the optional-key reader in `LoadNetworkConfig` (GeniusNode.cpp:1244-1277):
```cpp
auto read = [&]( const char *key, auto &out )
{
    using T           = std::decay_t<decltype( out )>;
    const auto member = config_json.FindMember( key );
    if ( member == config_json.MemberEnd() )
    {
        return;
    }
    // ... type-checked assignment
};
read( "network_key", settings.network_key );   // GeniusNode.cpp:1277 — add sibling:
// read( "private_network_id", settings.private_network_id );
```
Struct member with doc comment (GeniusNode.hpp:1046):
```cpp
std::string network_key;              ///< "network_key" pnet PSK; empty = public network.
```
Add `private_network_id` next to it plus a member `private_network_id_` beside `network_key_` (GeniusNode.hpp:955). InitNetwork retention pattern at GeniusNode.cpp:1508-1513 (log "private-network mode enabled" — log the **public id only, never the key**).

**Validation beyond `read`:** follow the warn-on-ill-typed divergence precedent at GeniusNode.cpp:1304-1319 (`port_seed` numeric read with explicit warn). For the id: 0x-hex of exactly 32 bytes, reject all-zero, absent = public (RESEARCH recommendation, assumption A3).

**Writer** — `WriteNetworkConfig` (GeniusNode.cpp:167-212): add the escaped-field emit for `private_network_id` mirroring lines 187-209. Extend `example/node_test/network_config.json` with placeholder-only values (Runtime State Inventory rule).

**pnet-mode construction + teardown discipline** (GeniusNode.cpp:1413-1452) — reuse verbatim for any new gated construction:
```cpp
if ( settings.network_key.empty() )
{
    pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>( std::move( keypair ), config );
}
else
{
    try
    {
        pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>( std::move( keypair ), config, settings.network_key );
    }
    catch ( const std::exception &e )
    {
        node_logger_->error( "Private-network (pnet) initialization failed: {}", e.what() );
        return false;
    }
}
// A half-started PubSub must not be left reachable, so every failure tears it down.
auto fail = [this]( const std::string &message ) { node_logger_->error( "{}", message ); pubsub_->Stop(); pubsub_.reset(); return false; };
```

---

### `src/processing/impl/TaskKeys.hpp` (mod — scoped job keys, D-08)

**Analog (exact self):** `ProcessingPrefix()` (TaskKeys.hpp:25-33) shows the established way to thread an environment component into every derived key:
```cpp
static std::string ProcessingPrefix()
{
    return std::string( PROCESSING_PREFIX_BASE ) + std::to_string( sgns::version::ProcessingVersion() );
}
```
Scope variant (discretion on encoding): overloads taking a scope (default `0` = today's byte-identical strings) that prepend `/chain/<privateNetworkId>` via `sgns::crdt::HierarchicalKey::ChildString` (hierarchical_key.hpp:44). Public paths MUST remain byte-stable (Runtime State Inventory). Do NOT use `version::SetNetworkId` (process-global, Pitfall 6).

---

### `src/account/GeniusTransaction.hpp` + `src/account/TransactionManager.{hpp,cpp}` (mod — scope-aware chain IDs)

**Analog (exact self):** the existing per-tx chain-id override already plumbed — `TransactionManager::SelectInputValidator` (TransactionManager.cpp:1326-1355):
```cpp
std::string chain_id( GENIUS_CHAIN_ID );
if ( tx )
{
    if ( auto tx_chain_id = tx->GetChainId(); !tx_chain_id.empty() )
    {
        chain_id = std::move( tx_chain_id );   // <-- per-job override point; derive private ids at job creation
    }
    // ...
}
```
Two distinct constants, both must stay byte-stable publicly (Pitfall 5): `GeniusTransaction.hpp:36` = `"supergenius_chain"`; `TransactionManager.hpp:783` = `"supergenius"`. `GetChainId()` override path is `GeniusTransaction.hpp:151`.

---

### `src/blockchain/ValidatorRegistry.{hpp,cpp}` + `src/blockchain/impl/Blockchain.cpp` (mod — D-09 per-scope registries)

**Analog (exact self):** the statics to parameterize (ValidatorRegistry.hpp:386-407):
```cpp
static constexpr std::string_view RegistryKey()    { return "gnus-validator-registry"; }
static constexpr std::string_view ValidatorTopic() { return "gnus-validator-registry"; }
static constexpr std::string_view RegistryCidKey() { return "gnus-validator-registry-cid"; }
```
Convert to instance accessors parameterized by scope, with the public instance keeping the exact current strings (Pitfall 7 — a second instance with the default key would merge the networks' consensus). Constructor-default precedent: `TrustedPeerRegistry.hpp:123-128` (defaulted `base_key` argument).

**Single creation site to make scope-aware** (Blockchain.cpp:122):
```cpp
instance->validator_registry_ = ValidatorRegistry::New(
    instance->db_, 2, 3, ValidatorRegistry::WeightConfig{}, GetAuthorizedFullNodeAddress(), /*...*/ );
```

---

### Test files

**`test/src/networkregistry/` (new)** — analog `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp:20-103` + helper `test/src/securecrdt/securecrdt_test_node.hpp`:
```cpp
void SetUp() override
{
    node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "networkregistry" );
    ASSERT_NE( node_, nullptr );
    secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "networkregistry-topic" );
    secure_crdt_->RegisterFilters();
}
void TearDown() override
{
    if ( registry_ ) { registry_->Unregister(); registry_.reset(); }
    secure_crdt_.reset();
    node_.reset();
}
```
Bootstrap tests: TPR-majority required (under-signed bootstrap never confirms — mirror `SignatureOverMismatchedPayloadNeverConfirms`), post-confirm self-governance, no unilateral admission, no raw key bytes in serialized records (assert payload contains no key material). CMakeLists: copy `test/src/trustedpeer/CMakeLists.txt` (`addtest(name file.cpp)` + `target_link_libraries` + `target_include_directories(... ${CMAKE_CURRENT_SOURCE_DIR}/..)`; new dirs need registration in the parent `test/src/CMakeLists.txt`).

**Gating tests (D-07 layer)** — extend `test/src/pubsub_counts/pubsub_counts.cpp` `PnetIsolationAndGaterBlocking` (lines 140-261). Reuse its fixtures verbatim:
```cpp
constexpr std::string_view SWARM_KEY_PNET = "/key/swarm/psk/1.0.0/\n/base16/"
                                            "000102...1e1f\n";   // lines 38-45; OUTSIDE differs
auto pnetA = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
    GenerateKeyPair(), MakeGossipConfig(), std::string( SWARM_KEY_PNET ) );   // lines 146-153
// Start(0,{}) -> future.wait() -> AddPeers({ other->GetInterfaceAddress() })  // interface addr, NOT loopback (Pitfall 1)
```
New negative case: same PSK but NOT in NetworkRegistry => never meshes. Keep the grace-window negative-assertion loop and `connectedness` check (lines 235-252).

**Config tests (PNET-CFG)** — analog `test/src/account/network_config_precedence_test.cpp` (helpers at lines 24-59: `MakeDevConfig`, `MakeTempDir`, `UseMemorySecureStorage`, `WaitForReady` via `test::assertWaitForCondition`; scene pattern at 65-80: `WriteNetworkConfig` -> `WriteSgnsConfig` -> `GeniusNode::New` -> assert resolved value).

**Wait-condition rule:** use `ASSERT_WAIT_FOR_CONDITION` / `test::assertWaitForCondition` from `test/testutil/wait_condition.hpp` — no `sleep_for` (issue #367 acceptance criteria).

## Shared Patterns

### SecureCRDT registry lifecycle (bootstrap authority -> cached self-governance)
**Source:** `src/trustedpeer/TrustedPeerRegistry.cpp:160-188, 230-265`
**Apply to:** `NetworkRegistry`, any future SecureCRDT-backed registry. Signer-set resolution reads ONLY `cached_peers_`/`genesis_confirmed_` under `shared_mutex`; cache refresh happens in `TryConfirm`/change-callback paths. Any `ResolveSignerSet` calling `ReadIfQuorum` is a bug (Pitfall 9).

### Quorum-threshold safety floor
**Source:** `src/securecrdt/QuorumThresholdValidation.hpp:35-43`
**Apply to:** `NetworkRegistry::New` (both bootstrap and self-governance thresholds), any quorum-configured construction:
```cpp
inline outcome::result<void> ValidateQuorumThreshold( uint64_t threshold, size_t signer_set_size )
{
    const uint64_t minimum_safe_threshold = ( static_cast<uint64_t>( signer_set_size ) * 51 + 99 ) / 100;
    if ( threshold < minimum_safe_threshold )
    {
        return outcome::failure( SecureCrdt::Error::QUORUM_THRESHOLD_BELOW_FLOOR );
    }
    return outcome::success();
}
```

### Registry ownership: shared_ptr registry as dependency
**Source:** `src/account/BurnConfig.hpp:94-99, 121-127`
**Apply to:** `SecureCrdtRegistryEntry` PeerRegistry association (D-04), `GeniusNode` holding NetworkRegistry instances, gater holding the membership source. Default to `shared_ptr` (BurnConfig precedent); `weak_ptr` only with demonstrated cycle risk.

### Eager-throw pnet construction -> init failure (never a half-configured host)
**Source:** `src/account/GeniusNode.cpp:1413-1434`
**Apply to:** every new pnet-mode construction path — especially the processing host (Pitfall 4). Wrap in try/catch, log `e.what()`, return false / tear down via the `fail` lambda pattern (1436-1443).

### Injector binding set for gated hosts
**Source:** `3rdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:173-189`
**Apply to:** processing host, any future SGNUS-constructed libp2p host in private mode: `useSecurityAdaptors<Noise>()` + `di::bind<network::ConnectionGater>().TEMPLATE_TO(gater)[di::override]` + `usePrivateNetwork(key)` (pnet mode only). Authoritative signatures: installed `network_injector.hpp:208, 254, 275, 308, 341`.

### Gater intercept ordering
**Source:** installed `ipfs-pubsub/include/ipfs_pubsub/deny_list_connection_gater.hpp:81-132`
**Apply to:** the new membership gater. `interceptAccept` passes through (peer unknown at raw stage); peer-aware checks at `interceptPeerDial`/`interceptSecured`/`interceptUpgraded`; error codes from `libp2p::network::ConnectionGaterError`.

### Optional-key config reader + warn-on-ill-typed divergence
**Source:** `src/account/GeniusNode.cpp:1244-1277` (reader), `1304-1319` (warn divergence)
**Apply to:** `private_network_id` parsing and any new `network_config.json` keys. Absent key = current default; secrets never logged.

### Secret hygiene
**Source:** `WriteNetworkConfig` escaping (GeniusNode.cpp:187-209), `Psk` move-only self-zeroing design (installed `security/pnet/psk.hpp`)
**Apply to:** all files touching `network_key` / `private_network_id`. Raw PSK never enters CRDT records, log lines, or error messages (D-03); registry stores at most version/fingerprint metadata.

### Test scaffolding
**Sources:** `test/src/trustedpeer/CMakeLists.txt` (addtest + link set), `test/src/pubsub_counts/CMakeLists.txt` (gating test link set: `ipfs-pubsub`, `p2p::p2p_ed25519_provider`, ...), `test/testutil/wait_condition.hpp`
**Apply to:** all six Wave-0 test gaps. Fixture/teardown with explicit `Unregister()`; wait-condition macros only.

## No Analog Found

None. Every file has an exact or role-match analog in the codebase or the vendored trees. Two files rely on cross-repo analogs (processing host injector <- ipfs-pubsub `MakeCustomHostInjector`; membership gater <- vendored `DenyListConnectionGater`) because no SGNUS-side gater or Noise-only injector exists yet.

## Metadata

**Analog search scope:** `src/{securecrdt,trustedpeer,account,processing/impl,blockchain,crdt}`, `test/src/{trustedpeer,securecrdt,pubsub_counts,account}`, `test/testutil/`, `example/node_test/`, vendored `3rdparty/ipfs-pubsub` (source + installed headers), installed `3rdparty/build/OSX/Release/{libp2p,ipfs-pubsub}/include`
**Files scanned:** ~25 (16 read in full or targeted ranges)
**Pattern extraction date:** 2026-08-31
**Caveat for planner:** line numbers are from the working tree at branch `gsd/phase-15-...` (commit 0515def3 lineage) and the dev_pnets vendored checkouts; re-verify after D-10 prerequisite branches land.
