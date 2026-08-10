# Phase 10: TrustedPeerRegistry - Pattern Map

**Mapped:** 2026-07-24
**Files analyzed:** 8
**Analogs found:** 8 / 8

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/trustedpeer/TrustedPeerRegistry.hpp` | model/service (header) | CRUD (quorum-gated) | `src/blockchain/ValidatorRegistry.hpp` (cache members) + `src/securecrdt/SecureCrdt.hpp` (API shape) | role-match (adapted, different trust model) |
| `src/trustedpeer/TrustedPeerRegistry.cpp` | service | CRUD (quorum-gated) | `src/blockchain/ValidatorRegistry.cpp` (cache update-in-place, lines 1300-1320) + `src/securecrdt/SecureCrdt.cpp` (ProposeValue/AddSignature/ReadIfQuorum delegation) | role-match (adapted) |
| `src/trustedpeer/CMakeLists.txt` | config (build) | — | `src/securecrdt/CMakeLists.txt` | exact |
| `src/CMakeLists.txt` (edit) | config (build) | — | existing `add_subdirectory(securecrdt)` line 18 | exact |
| `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp` | test | request-response (unit/e2e) | `test/src/securecrdt/securecrdt_propose_sign_quorum_test.cpp` + `securecrdt_registry_test.cpp` | exact |
| `test/src/trustedpeer/trustedpeerregistry_quorum_test.cpp` | test | request-response (unit) | `test/src/securecrdt/securecrdt_quorum_gate_test.cpp` | exact |
| `test/src/trustedpeer/CMakeLists.txt` | config (test build) | — | `test/src/securecrdt/CMakeLists.txt` | exact |
| `test/src/CMakeLists.txt` (edit) | config (test build) | — | existing `add_subdirectory(securecrdt)` line 7 | exact |
| `src/account/GeniusNode.cpp` (`LoadSgnsConfig`, edit) | config-loader | request-response (parse-only) | same file, `bootstrap_fullnodes`/`authorized_full_node` blocks, lines 399-416 | exact (self-analog) |

## Pattern Assignments

### `src/trustedpeer/TrustedPeerRegistry.hpp` / `.cpp` (service, CRUD quorum-gated)

**Analogs:** `src/blockchain/ValidatorRegistry.hpp`/`.cpp` (cache shape), `src/securecrdt/SecureCrdt.hpp` (delegate-to API), `src/securecrdt/ISignedCRDTData.hpp` (payload interface to implement), `src/securecrdt/SecureCrdtRegistry.hpp` (registration contract).

**Cache member declarations to mirror** (`src/blockchain/ValidatorRegistry.hpp:630-634`, current code):
```cpp
mutable std::shared_mutex       cache_mutex_;               ///< Guards cached registry/update state.
std::optional<Registry>         cached_registry_;           ///< Cached active registry snapshot.
std::string                     cached_registry_id_;        ///< Cached active registry CID.
bool                             cache_initialized_ = false; ///< Indicates whether cache has been initialized.
```
Adapt directly: rename `cached_registry_` -> `cached_peers_` (type `std::vector<std::string>`), drop `cached_registry_id_` if not needed, keep the `shared_mutex` + `cache_initialized_` guard shape.

**Update-after-confirmation pattern** (`src/blockchain/ValidatorRegistry.cpp:1319-1320`, current code — this is the ONLY place the cache is ever overwritten, after independent confirmation):
```cpp
cached_registry_    = decoded.value().registry();
cached_registry_id_ = cid;
```
For `TrustedPeerRegistry`: overwrite `cached_peers_` only immediately after `SecureCrdt::ReadIfQuorum` returns bytes AND `DeserializeFromBytes`+`Verify`+`Apply` all succeed on those bytes.

**`ISignedCRDTData` interface to implement** (`src/securecrdt/ISignedCRDTData.hpp`, full file, 65 lines — read in full, no truncation needed):
```cpp
class ISignedCRDTData
{
public:
    virtual ~ISignedCRDTData() = default;
    virtual std::vector<uint8_t> SerializeToBytes() const = 0;
    virtual bool DeserializeFromBytes( const std::vector<uint8_t> &bytes ) = 0; // false, never throw, on malformed input
    virtual bool Verify( const std::vector<uint8_t> &payload ) const = 0;       // structural validation ONLY (Pitfall 4) — no cache diffing
    virtual void Apply() = 0;                                                   // side effect only, no quorum re-check
};
```

**`SecureCrdt` API to delegate to, never reimplement** (`src/securecrdt/SecureCrdt.hpp:78-132`, full class read):
```cpp
outcome::result<void> ProposeValue( const sgns::crdt::HierarchicalKey &base_key,
                                    const std::vector<uint8_t>        &payload );
outcome::result<void> AddSignature( const sgns::crdt::HierarchicalKey &base_key,
                                    const std::string                 &signer_address,
                                    std::string_view                   signature );
outcome::result<std::optional<sgns::base::Buffer>> ReadIfQuorum(
    const sgns::crdt::HierarchicalKey &base_key );
bool RegisterFilters(); // call once after construction, mirrors ValidatorRegistry::RegisterFilter convention
```
`TrustedPeerRegistry`'s `Propose`/`Sign`/`TryConfirm`-style methods are thin wrappers over these three; `TrustedPeerRegistry` must never call `db_->Put` directly (Pitfall 1 warning sign) and its `SignerSetSource` lambda must never call `ReadIfQuorum` on itself (Pitfall 2 — reentrancy against `AddSignature`'s internal quorum check).

**`SecureCrdtRegistry` registration contract** (`src/securecrdt/SecureCrdtRegistry.hpp`, full file, 145 lines):
```cpp
struct SignerSetSnapshot { std::vector<std::string> signer_set; uint64_t threshold = 0; };
using SignerSetSource = std::function<outcome::result<SignerSetSnapshot>( const std::string &base_key )>;
struct SecureCrdtRegistryEntry
{
    std::string                                       key_pattern;
    SignerSetSource                                   signer_set_source;
    std::function<std::shared_ptr<ISignedCRDTData>()> make_instance;
    std::regex                                        compiled_pattern; // filled in by Register()
    const void                                        *owner_token = nullptr;
};
static void Register( const std::string &key_pattern, SecureCrdtRegistryEntry entry );
```
Only existing usage example is test-only (`test/src/securecrdt/securecrdt_registry_test.cpp:47-70`, full excerpt below) — `TrustedPeerRegistry` is the first REAL consumer:
```cpp
// Source: test/src/securecrdt/securecrdt_registry_test.cpp:49-57
SecureCrdtRegistryEntry entry;
entry.signer_set_source = []( const std::string & ) -> outcome::result<SignerSetSnapshot>
{
    return SignerSetSnapshot{ { "addr1", "addr2" }, 2 };
};
entry.make_instance = [] { return std::make_shared<TestSignedData>(); };
entry.owner_token    = &correct_token_storage;
SecureCrdtRegistry::Register( kTestKeyPattern, entry );
```
For `TrustedPeerRegistry`, `signer_set_source` reads only `cached_peers_` (never `SecureCrdt::ReadIfQuorum`), and `make_instance` returns a fresh `TrustedPeerListPayload` (or similarly named `ISignedCRDTData` implementer) instance.

**Genesis trigger shape to adapt (NOT the write path)** (`src/blockchain/impl/Blockchain.cpp:701-720`, full function):
```cpp
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
**Divergence (critical, per RESEARCH.md Pattern 2):** copy the trigger-condition SHAPE (only proceed if this node is the designated authority) but NOT `StoreGenesisRegistry`'s direct `Put` after a single ad-hoc `sign` callback. `TrustedPeerRegistry`'s genesis method must instead call `SecureCrdt::ProposeValue(base_key, genesis_list_bytes)` then exactly one `SecureCrdt::AddSignature(base_key, bootstrapper_address, ephemeral_signature)`, with the `SignerSetSource` returning `{signer_set: {bootstrapper_address}, threshold: 1}` for the pre-membership state. Also, unlike `EnsureValidatorRegistry`, this genesis method CANNOT call `account_->Sign(...)` live — the ephemeral signature must be supplied as a precomputed value (already produced offline by the ceremony helper), since the ephemeral private key is destroyed after the one-time ceremony (D-02).

**Signer address precedent** (`src/blockchain/impl/Blockchain.cpp:505,515`):
```cpp
void Blockchain::SetAuthorizedFullNodeAddress( const std::string &pub_address );
const std::string &Blockchain::GetAuthorizedFullNodeAddress();
```
Mirror this exact getter/setter shape if `bootstrapper_node` needs an analogous static accessor (Discretion: only if `TrustedPeerRegistry`'s owner needs cross-module access to it; otherwise a plain constructor parameter suffices per RESEARCH.md's "no `Blockchain`-level static storage needed" note).

**Ephemeral keypair generation for the offline genesis ceremony** (`ProofSystem/include/ProofSystem/EthereumKeyGenerator.hpp:24-108`, full class):
```cpp
ethereum::EthereumKeyGenerator ephemeral;              // default ctor: in-memory only, no disk I/O
std::string bootstrapper_address = ephemeral.GetEntirePubValue();  // -> bootstrapper_node config value
```
Signing must replicate `GeniusAccount::Sign`'s routine (`src/account/GeniusAccount.cpp:845`, ~15-line secp256k1/SHA256(SHA256(x)) routine) against `ephemeral`'s raw private key — do NOT construct a `GeniusAccount` (its factories always persist keys under `base_path`, violating D-02). This ceremony helper belongs in test/tooling code only, not the node runtime (per resolved Open Question 1/2 in RESEARCH.md).

---

### `src/trustedpeer/CMakeLists.txt` (config, build)

**Analog:** `src/securecrdt/CMakeLists.txt` (full file, current code):
```cmake
add_library(securecrdt
    SecureCrdt.cpp
)
target_link_libraries(securecrdt
    PUBLIC
    crdt_globaldb
    multisig
)
supergenius_install(securecrdt)
```
For `trustedpeer`: `add_library(trustedpeer TrustedPeerRegistry.cpp)`, `target_link_libraries(trustedpeer PUBLIC securecrdt)` (securecrdt already publicly links crdt_globaldb/multisig, so transitively available), plus `supergenius_install(trustedpeer)`.

**Edit to `src/CMakeLists.txt`:** add `add_subdirectory(trustedpeer)` immediately alongside the existing `add_subdirectory(securecrdt)` at line 18.

---

### `test/src/trustedpeer/CMakeLists.txt` (config, test build)

**Analog:** `test/src/securecrdt/CMakeLists.txt` (full file, current code):
```cmake
addtest(securecrdt_registry_test
    securecrdt_registry_test.cpp
)
target_link_libraries(securecrdt_registry_test
    securecrdt
)

set(SECURECRDT_TEST_NODE_LIBS
    securecrdt
    crdt_globaldb
    multisig
    json_secure_storage
)

addtest(securecrdt_propose_sign_quorum_test
    securecrdt_propose_sign_quorum_test.cpp
)
target_link_libraries(securecrdt_propose_sign_quorum_test
    ${SECURECRDT_TEST_NODE_LIBS}
)
```
For `trustedpeer`: define `TRUSTEDPEER_TEST_NODE_LIBS` = `trustedpeer securecrdt crdt_globaldb multisig json_secure_storage`, then `addtest(trustedpeerregistry_genesis_test ...)` and `addtest(trustedpeerregistry_quorum_test ...)` each linking that list — exact structural clone.

**Edit to `test/src/CMakeLists.txt`:** add `add_subdirectory(trustedpeer)` alongside the existing `add_subdirectory(securecrdt)` at line 7.

---

### `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp` / `trustedpeerregistry_quorum_test.cpp` (test)

**Analogs:** `test/src/securecrdt/securecrdt_registry_test.cpp` (registry Register/Resolve pattern, full 70-line excerpt read), `test/src/securecrdt/securecrdt_propose_sign_quorum_test.cpp` and `securecrdt_quorum_gate_test.cpp` (propose/sign/confirm sequences), `test/src/securecrdt/securecrdt_test_node.hpp` (fixture — DO NOT reinvent).

**Test fixture to reuse verbatim** (`test/src/securecrdt/securecrdt_test_node.hpp`, full file, 156 lines):
```cpp
#include "test/src/securecrdt/securecrdt_test_node.hpp"

auto node = sgns::test::securecrdt::MakeSecureCrdtTestNode( "trustedpeer_test" );
ASSERT_NE( node, nullptr );
auto secure_crdt = std::make_shared<sgns::securecrdt::SecureCrdt>( node->db, "trustedpeer-topic" );
secure_crdt->RegisterFilters();
```
This fixture already solves the libp2p/soralog `EnsureLoggingSystemConfigured` segfault-avoidance and single-node (unconnected, no `connectNodes()`) `GlobalDB` setup that Phase 9's own tests rely on — new trustedpeer tests must call `MakeSecureCrdtTestNode` directly rather than building a second copy of this scaffolding.

**`ISignedCRDTData` minimal test-double shape** (`test/src/securecrdt/securecrdt_registry_test.cpp:10-38`, full excerpt — adapt to a real `TrustedPeerListPayload` type rather than a bare byte-echo double):
```cpp
class TestSignedData : public ISignedCRDTData
{
public:
    std::vector<uint8_t> SerializeToBytes() const override { return value_; }
    bool DeserializeFromBytes( const std::vector<uint8_t> &bytes ) override
    {
        if ( bytes.empty() ) { return false; }
        value_ = bytes;
        return true;
    }
    bool Verify( const std::vector<uint8_t> &payload ) const override { return payload == value_; }
    void Apply() override {}
    std::vector<uint8_t> value_;
};
```

---

### `src/account/GeniusNode.cpp` `LoadSgnsConfig()` (config-loader, edit only — parsing, no live wiring)

**Analog:** same function, existing `bootstrap_fullnodes` array-field block and `authorized_full_node` string-field block (`src/account/GeniusNode.cpp:399-416`, current code, confirmed by direct read):
```cpp
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
// Read authorized_full_node and immediately set it
if ( config_json.HasMember( "authorized_full_node" ) && config_json["authorized_full_node"].IsString() )
{
    const std::string addr = config_json["authorized_full_node"].GetString();
    node_logger_->info( "sgns_config.json: setting authorized_full_node" );
    Blockchain::SetAuthorizedFullNodeAddress( addr );
}
```

**Exact new blocks to insert** (immediately after line 416, before the `ipfs_cache_dir` block at line 417 — per RESEARCH.md recommendation):
```cpp
if ( config_json.HasMember( "trusted_peers" ) && config_json["trusted_peers"].IsArray() )
{
    for ( auto &v : config_json["trusted_peers"].GetArray() )
    {
        if ( v.IsString() )
        {
            trusted_peers_genesis_.push_back( v.GetString() );  // new member, std::vector<std::string>
        }
    }
    node_logger_->info( "sgns_config.json: loaded {} trusted_peers", trusted_peers_genesis_.size() );
}
if ( config_json.HasMember( "bootstrapper_node" ) && config_json["bootstrapper_node"].IsString() )
{
    bootstrapper_node_address_ = config_json["bootstrapper_node"].GetString();  // new member, std::string
    node_logger_->info( "sgns_config.json: bootstrapper_node set" );
}
```
**Member declaration site:** add `std::vector<std::string> trusted_peers_genesis_;` and `std::string bootstrapper_node_address_;` in `src/account/GeniusNode.hpp` alongside the existing `std::vector<std::string> bootstrap_fullnodes_;` member (confirmed at `src/account/GeniusNode.hpp:811`).

**Important scope boundary (per resolved Open Question 1 in RESEARCH.md):** unlike `authorized_full_node`, do NOT call any `TrustedPeerRegistry`-level setter/constructor from inside this block this phase — these two new members are parsed-and-stored only; no `TrustedPeerRegistry` instance is constructed or wired into `GeniusNode`'s real startup state machine in this phase. Also update `example/node_test/sgns_config.json` to add example `trusted_peers` (array) and `bootstrapper_node` (string) fields, mirroring the existing `bootstrap_fullnodes`/`authorized_full_node` example entries.

## Shared Patterns

### Quorum/signature verification — never reimplement
**Source:** `src/securecrdt/SecureCrdt.hpp` (`ProposeValue`, `AddSignature`, `ReadIfQuorum`), internally delegating to `src/multisig/MultiSig.hpp`'s `VerifyPayloadSignature`.
**Apply to:** `TrustedPeerRegistry.cpp` exclusively — TPR-03's grep-inspection gate (`grep -rn "secp256k1\|VerifySignature\|VerifyPayloadSignature" src/trustedpeer/` should return zero matches) depends on this being followed with no exceptions.

### Cache-guarded-by-mutex, updated only post-confirmation
**Source:** `src/blockchain/ValidatorRegistry.hpp:630-634` (declarations) + `src/blockchain/ValidatorRegistry.cpp:1319-1320` (update site).
**Apply to:** `TrustedPeerRegistry`'s `cached_peers_`/`cache_mutex_` and its `SignerSetSource` lambda (D-05) — the lambda reads ONLY the cache, never calls back into `SecureCrdt`.

### `sgns_config.json` optional-field parsing
**Source:** `src/account/GeniusNode.cpp:399-416`.
**Apply to:** the two new `LoadSgnsConfig()` blocks for `trusted_peers`/`bootstrapper_node` — same `HasMember`+type-check+logged-default idiom, no exceptions/asserts on missing fields.

### GoogleTest fixture reuse for SecureCrdt-backed types
**Source:** `test/src/securecrdt/securecrdt_test_node.hpp` (`MakeSecureCrdtTestNode`).
**Apply to:** both new `test/src/trustedpeer/*.cpp` files — single unconnected node, no `connectNodes()`, matches Phase 9's own test precedent exactly.

## No Analog Found

| File | Role | Data Flow | Reason |
|---|---|---|---|
| Genesis-ceremony helper (e.g. `GenerateGenesisCeremonyArtifact`, test-only per RESEARCH.md Open Question 2) | utility | batch (one-shot, offline) | No existing in-repo utility generates a keypair without persisting to disk AND signs with it without going through `GeniusAccount`; must be hand-written by replicating `GeniusAccount::Sign`'s secp256k1 routine (`src/account/GeniusAccount.cpp:845`) against a raw `ethereum::EthereumKeyGenerator` instance. Not a locked file — planner should scope it as a small test-support header/helper under `test/src/trustedpeer/`, not a production tool, per CONTEXT.md's discretion note. |

## Metadata

**Analog search scope:** `src/securecrdt/`, `src/blockchain/`, `src/blockchain/impl/`, `src/account/`, `src/multisig/`, `ProofSystem/include/ProofSystem/`, `test/src/securecrdt/`
**Files scanned:** `SecureCrdt.hpp`, `SecureCrdtRegistry.hpp`, `ISignedCRDTData.hpp`, `ValidatorRegistry.hpp`/`.cpp`, `Blockchain.cpp`, `GeniusNode.cpp`/`.hpp`, `GeniusAccount.cpp`, `EthereumKeyGenerator.hpp`, `MultiSig.hpp`, `securecrdt_test_node.hpp`, `securecrdt_registry_test.cpp`, `CMakeLists.txt` files for `src/securecrdt/`, `test/src/securecrdt/`, `src/CMakeLists.txt`, `test/src/CMakeLists.txt`
**Pattern extraction date:** 2026-07-24
