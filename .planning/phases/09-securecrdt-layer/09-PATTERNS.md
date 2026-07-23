# Phase 9: SecureCRDT Layer - Pattern Map

**Mapped:** 2026-07-23
**Files analyzed:** 10 (new library + tests + 2 build-file edits)
**Analogs found:** 10 / 10

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|----------------|
| `src/securecrdt/ISignedCRDTData.hpp` | model/interface | transform (codec+verify) | `src/account/InputValidators.hpp` (`IInputValidator` interface shape) | role-match |
| `src/securecrdt/SecureCrdtRegistry.hpp` (+ `.cpp` if needed) | service (static registry) | request-response (lookup) | `src/account/InputValidators.hpp` `IInputValidator::Register/Get/UnregisterIf` | exact |
| `src/securecrdt/SecureCrdt.hpp` / `SecureCrdt.cpp` | service (wrapper) | event-driven + request-response (propose/sign/read via CRDT puts + filter callback) | `src/blockchain/ValidatorRegistry.hpp`/`.cpp` (`RegisterFilter`/`FilterRegistryUpdate`/`RegistryUpdateReceived`) | exact |
| `src/securecrdt/proto/securecrdt.proto` (optional signing-payload framing) | model (protobuf schema) | transform | `src/blockchain/proto/validator_registry.proto`-style pattern (see `ValidatorRegistry`'s `RegistrySigningPayload`, `.cpp` L1332-1345) | role-match |
| `src/securecrdt/CMakeLists.txt` | config | batch (build) | `src/multisig/CMakeLists.txt` | exact |
| `src/CMakeLists.txt` (edit: add `add_subdirectory(securecrdt)`) | config | batch (build) | existing line 17 `add_subdirectory(multisig)` | exact |
| `test/src/securecrdt/CMakeLists.txt` | test config | batch (build) | `test/src/multisig/CMakeLists.txt` | exact |
| `test/src/CMakeLists.txt` (edit: add `add_subdirectory(securecrdt)`) | test config | batch (build) | existing line 6 `add_subdirectory(multisig)` | exact |
| `test/src/securecrdt/securecrdt_registry_test.cpp` | test | request-response (logic-only) | `test/src/multisig/multisig_quorum_test.cpp` (logic-only, no GlobalDB) | role-match |
| `test/src/securecrdt/securecrdt_quorum_gate_test.cpp` / `securecrdt_propose_sign_quorum_test.cpp` | test | CRUD + event-driven (single-node GlobalDB) | `test/src/crdt/globaldb_integration.cpp` (`TestNodeCollection::addNode`, single node, skip `connectNodes()`) | role-match |

## Pattern Assignments

### `src/securecrdt/ISignedCRDTData.hpp` (interface)

**Analog:** `src/account/InputValidators.hpp` (`IInputValidator`, lines 46-117)

**Interface shape to mirror** (lines 46-52, 61-82):
```cpp
class IInputValidator
{
public:
    virtual ~IInputValidator() = default;
    virtual bool ValidateUTXOParameters( const UTXOTxParameters &params,
                                         const std::string      &address,
                                         const UTXOManager      &utxo_manager ) const = 0;
    virtual bool ValidateWitness( ... ) const = 0;
    virtual bool RequiresConsensusUTXOData() const = 0;
};
```
Generalize to `ISignedCRDTData` with pure-virtual `Verify()` / `Apply()` and a payload codec, per SCRDT-01. Each concrete implementer owns its own protobuf message the way `ValidatorRegistry` owns `validator::Registry` — do not template this interface (milestone decision, CONTEXT.md).

**Serialization precedent** (`src/blockchain/ValidatorRegistry.cpp` lines 538, 566, 758, 1175, 1338 — all the same shape):
```cpp
std::string serialized;
if ( !registry.SerializeToString( &serialized ) )
{
    // handle error
}
```
Wrap `serialized` into a `base::Buffer` before `GlobalDB::Put`, exactly as `ValidatorRegistry` does at cpp L637-639 / L782-783.

---

### `src/securecrdt/SecureCrdtRegistry.hpp` (static registry, SCRDT-02)

**Analog:** `src/account/InputValidators.hpp` lines 84-117 (full file above; this is the entire pattern to generalize — it's a tiny, self-contained static map).

**Core pattern to copy verbatim (structure), lines 84-117:**
```cpp
using ValidatorPtr = const IInputValidator *;

static void Register( const std::string &chain_id, ValidatorPtr validator )
{
    registry()[chain_id] = validator;
}

static void UnregisterIf( const std::string &chain_id, ValidatorPtr expected )
{
    auto it = registry().find( chain_id );
    if ( it != registry().end() && it->second == expected )
    {
        registry().erase( it );
    }
}

static ValidatorPtr Get( const std::string &chain_id )
{
    auto it = registry().find( chain_id );
    return it != registry().end() ? it->second : nullptr;
}

private:
static std::unordered_map<std::string, ValidatorPtr> &registry()
{
    static std::unordered_map<std::string, ValidatorPtr> map;
    return map;
}
```
Copy this exact compare-and-remove `UnregisterIf` idiom (prevents a stale entry from clobbering a newer registration) into `SecureCrdtRegistry::UnregisterIf`. Extend `Register`/`Get` to resolve by longest-prefix/regex match against a key (per RESEARCH.md's `Resolve(key)` design) instead of exact `chain_id` map lookup, since SecureCRDT keys are hierarchical (`base_key`, `base_key/sig/<addr>`), unlike `IInputValidator`'s flat `chain_id` map.

**Header-only style note:** `InputValidators.hpp` has no corresponding `.cpp` — the whole class lives in the header with an inline static local (function-local `static` map, C++11 magic-statics, thread-safe init). Follow the same header-only style for `SecureCrdtRegistry` unless the regex-based `Resolve()` logic is large enough to warrant a `.cpp` (RESEARCH.md lists an optional `.cpp`).

---

### `src/securecrdt/SecureCrdt.hpp` / `SecureCrdt.cpp` (wrapper, SCRDT-03/04)

**Analog:** `src/blockchain/ValidatorRegistry.hpp`/`.cpp` — `RegisterFilter`/`FilterRegistryUpdate`/`RegistryUpdateReceived` (cpp lines 1231-1327, verified directly).

**Filter + callback registration pattern (copy this shape), `ValidatorRegistry.cpp` lines 1231-1261:**
```cpp
bool ValidatorRegistry::RegisterFilter()
{
    logger_->trace( "{}: entry", __func__ );
    const std::string pattern           = "/?" + std::string( RegistryKey() );
    auto              weak_self         = weak_from_this();
    const bool        filter_registered = db_->RegisterElementFilter(
        pattern,
        [weak_self]( const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
        {
            if ( auto strong = weak_self.lock() )
            {
                return strong->FilterRegistryUpdate( element );
            }
            return std::nullopt;
        } );
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

    const bool result = filter_registered && callback_registered;
    logger_->info( "{}: result={}", __func__, result );
    return result;
}
```
For `SecureCrdt`, per RESEARCH.md's resolved Open Question 1, use ONE combined regex per registered `base_key`: `"/?" + base_key + "(/sig/.*)?"` — `RegisterElementFilter` compiles `pattern` as `std::regex` and matches with `std::regex_match` (confirmed in `src/crdt/impl/crdt_data_filter.cpp` L20-25, L108), so this single registration covers both the value key and all `sig/<addr>` children.

**Veto/accept semantics (copy exactly), `ValidatorRegistry.cpp` lines 1263-1284:**
```cpp
std::optional<std::vector<crdt::pb::Element>> ValidatorRegistry::FilterRegistryUpdate(
    const crdt::pb::Element &element )
{
    logger_->trace( "{}: entry key={}", __func__, element.key() );
    std::vector<uint8_t> bytes( element.value().begin(), element.value().end() );
    auto                 decoded_update = DeserializeRegistryUpdate( bytes );
    if ( decoded_update.has_error() )
    {
        logger_->error( "{}: parse failed, rejecting: {}", __func__, element.key() );
        return std::vector<crdt::pb::Element>{};
    }

    RegistryUpdate update = decoded_update.value();
    if ( !VerifyUpdate( update, false ) )
    {
        logger_->error( "{}: verification failed, rejecting: {}", __func__, element.key() );
        return std::vector<crdt::pb::Element>{};
    }

    logger_->debug( "{}: update accepted", __func__ );
    return std::nullopt;
}
```
`std::nullopt` = accept/keep element; returning any vector (even empty) = reject/delete from delta before merge (confirmed `src/crdt/impl/crdt_data_filter.cpp` L90-160). For `SecureCrdt`'s equivalent filter, replace `DeserializeRegistryUpdate`+`VerifyUpdate` with the registered `ISignedCRDTData::Verify()` plus `multisig::VerifyPayloadSignature`/`EvaluateQuorum` calls, keeping the identical parse-failure-rejects, verify-failure-rejects, success-returns-nullopt structure.

**Critical divergence from this analog (D-03):** `ValidatorRegistry`'s filter ONLY runs for remote-originated deltas (`crdt_datastore.cpp GetDeltaFromNode`, `if (!created_by_self)`). `SecureCrdt` MUST implement a second, structurally identical check inside its own `ProposeValue()`/`AddSignature()` methods that runs before calling `GlobalDB::Put` locally — this has no direct analog in `ValidatorRegistry` (which never gates its own local `Put` calls) and is new code per D-03/D-04.

**Read-path pattern (new code, no direct analog — build from GlobalDB API + MultiSig):**
```cpp
// Sketch, confirmed API surface: src/crdt/globaldb/globaldb.hpp
outcome::result<Buffer> Get( const HierarchicalKey &key );                         // L111-115
outcome::result<QueryResult> QueryKeyValues( std::string_view keyPrefix );           // L124-128
```
Combine with `multisig::EvaluateQuorum` (below) — see RESEARCH.md "Pattern 3" for the full `ReadIfQuorum` sketch. Key point: pass the CURRENT value's bytes as the `payload` argument every time (never a cached/stale value), so a changed value invalidates old signatures automatically.

**HierarchicalKey derivation (copy exactly), `src/crdt/impl/hierarchical_key.cpp` lines 24-33:**
```cpp
HierarchicalKey HierarchicalKey::ChildString(std::string_view s) const
{
    std::string result = this->key_;
    if (!s.empty() && s[0] != '/')
    {
        result += '/';
    }
    result += s;
    return {std::move(result)};
}
```
Usage: `base_key.ChildString("sig").ChildString(signer_address)` yields `/base_key/sig/<addr>`.

**MultiSig call surface (already shipped, use as-is), `src/multisig/MultiSig.hpp` lines 33-69:**
```cpp
bool sgns::multisig::VerifyPayloadSignature( const std::string &address,
                                              std::string_view signature,
                                              const std::vector<uint8_t> &payload );

struct QuorumResult { bool has_quorum = false; uint64_t valid_unique_count = 0; };

QuorumResult sgns::multisig::EvaluateQuorum( const std::vector<std::string> &signer_set,
                                              uint64_t threshold,
                                              const std::vector<std::pair<std::string, std::string>> &collected_signatures,
                                              const std::vector<uint8_t> &payload );
```
Dedup-before-verify semantics are already implemented — `SecureCrdt` must delegate, never re-implement counting/dedup itself.

---

### `src/securecrdt/CMakeLists.txt` (build config)

**Analog:** `src/multisig/CMakeLists.txt` (full file, 7 lines):
```cmake
add_library(multisig
    MultiSig.cpp
)
target_link_libraries(multisig
    PUBLIC
    sgns_genius_account
)
supergenius_install(multisig)
```
For `securecrdt`, link `PUBLIC crdt_globaldb multisig` instead of `sgns_genius_account` (linking `crdt_globaldb` transitively pulls `crdt_datastore`/`crdt_data_filter`/`hierarchical_key`/etc. — confirmed in `src/crdt/globaldb/CMakeLists.txt`, which itself links `crdt_datastore` PUBLIC). If a `.proto` is added, mirror `add_proto_library(crdt_globaldb_proto proto/broadcast.proto)` from `src/crdt/globaldb/CMakeLists.txt` line 1.

---

### `test/src/securecrdt/CMakeLists.txt` (test build config)

**Analog:** `test/src/multisig/CMakeLists.txt` (full file):
```cmake
addtest(multisig_verify_test
    multisig_verify_test.cpp
)
target_link_libraries(multisig_verify_test
    multisig
    json_secure_storage
)

addtest(multisig_quorum_test
    multisig_quorum_test.cpp
)
target_link_libraries(multisig_quorum_test
    multisig
    json_secure_storage
)
```
Follow this `addtest(name) + target_link_libraries(name securecrdt ...)` shape for `securecrdt_registry_test` and `securecrdt_interface_test` (logic-only, no GlobalDB). For `securecrdt_quorum_gate_test`/`securecrdt_propose_sign_quorum_test`, additionally link against whatever `test/src/crdt/CMakeLists.txt` links for `globaldb_integration_gtest` (pubsub/libp2p/graphsync deps) since these tests instantiate a real single-node `GlobalDB`.

---

### `test/src/securecrdt/securecrdt_quorum_gate_test.cpp` / `securecrdt_propose_sign_quorum_test.cpp`

**Analog:** `test/src/crdt/globaldb_integration.cpp` — `GlobalDBIntegrationTest::TestNodeCollection::addNode` (lines 1-100+, single-node construction).

**Node construction pattern to reuse (lines 85-100, and continues further building `io`/`db`)**:
```cpp
void addNode( const std::string &dbName )
{
    const std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    const std::string binaryPath = boost::dll::program_location().parent_path().string();
    const std::string basePath   = binaryPath + "/" + dbName + "_" + testName;
    boost::filesystem::remove_all( basePath );
    boost::filesystem::create_directories( basePath );

    sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
    auto                           keyPair  = keyStore.GetKeyPair().value();
    auto                           pubsub   = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
    const std::string              listenIp = "0.0.0.0";
    const auto startError = pubsub->Start( 0, {}, listenIp, {} ).get();
    ASSERT_FALSE( startError ) << "Could not start GlobalDB test node: " << startError.message();

    auto io = std::make_shared<boost::asio::io_context>();
    // ... (continues constructing GlobalDB; read full file during implementation for GlobalDB ctor args)
}
```
Per RESEARCH.md Pitfall 3, reuse `addNode` but SKIP `connectNodes()` — SecureCRDT's local-write gate (SCRDT-03) and propose/sign/quorum flow (SCRDT-04) can be exercised entirely against one unconnected node via local `Put`/`Get`/`QueryKeyValues`, avoiding real peer networking in the test.

---

### `test/src/securecrdt/securecrdt_registry_test.cpp` / `securecrdt_interface_test.cpp`

**Analog:** `test/src/multisig/multisig_quorum_test.cpp` — logic-only GoogleTest file with zero CRDT/GlobalDB dependency (mirrors the `addtest(multisig_quorum_test ...)` target above). Use this as the structural template for registry-resolution and interface-conformance tests that don't need a live datastore.

## Shared Patterns

### Static registry pattern
**Source:** `src/account/InputValidators.hpp` lines 84-117
**Apply to:** `SecureCrdtRegistry` (Register/UnregisterIf/Get, function-local `static` map for magic-statics thread safety)

### Filter-registration / veto pattern
**Source:** `src/blockchain/ValidatorRegistry.cpp` lines 1231-1284
**Apply to:** `SecureCrdt`'s constructor/factory (self-registers filters at construction time, not via C++ static init order — confirmed call site `ValidatorRegistry.cpp` line 165 `if (!instance->RegisterFilter())` inside its `New(...)`-style factory)

### Protobuf serialize-to-Buffer pattern
**Source:** `src/blockchain/ValidatorRegistry.cpp` lines 538/566/758/1175/1338 (`.SerializeToString()`), combined with `Put` calls at lines 637-639, 782-783
**Apply to:** Every `ISignedCRDTData` implementer's payload codec and every `SecureCrdt::ProposeValue`/`AddSignature` call

### MultiSig quorum evaluation
**Source:** `src/multisig/MultiSig.hpp` lines 33-69 (already shipped, Phase 8)
**Apply to:** `SecureCrdt`'s local-write gate (`ProposeValue`/`AddSignature`) and read-path (`ReadIfQuorum`) — never re-implement dedup/verify logic locally

### HierarchicalKey sub-key derivation
**Source:** `src/crdt/impl/hierarchical_key.cpp` lines 24-33 (`ChildString`)
**Apply to:** All `sig/<address>` key construction: `base_key.ChildString("sig").ChildString(signer_address)`

### Build wiring (add_subdirectory)
**Source:** `src/CMakeLists.txt` line 17 / `test/src/CMakeLists.txt` line 6 (existing `multisig` entries)
**Apply to:** Add `add_subdirectory(securecrdt)` immediately alongside/after the `multisig` line in both files

## No Analog Found

None — every file in this phase's scope has at least a role-match analog in the existing codebase (see table above). The only genuinely new logic (per RESEARCH.md) is the local-write enforcement gate inside `SecureCrdt::ProposeValue`/`AddSignature` (D-03) and the reader-side quorum re-derivation in `ReadIfQuorum` (D-04) — both are new code assembled from existing primitives (`GlobalDB`, `MultiSig`, `HierarchicalKey`) rather than adaptations of a single existing analog file.

## Metadata

**Analog search scope:** `src/account/`, `src/blockchain/`, `src/multisig/`, `src/crdt/` (incl. `globaldb/`, `impl/`), `test/src/multisig/`, `test/src/crdt/`, plus root `src/CMakeLists.txt` / `test/src/CMakeLists.txt`
**Files scanned:** `InputValidators.hpp`, `ValidatorRegistry.hpp`/`.cpp`, `MultiSig.hpp`, `hierarchical_key.hpp`/`.cpp`, `globaldb.hpp`, `crdt_data_filter.cpp` (via RESEARCH.md verification), `globaldb_integration.cpp`, `multisig/CMakeLists.txt`, `test/src/multisig/CMakeLists.txt`, `crdt/globaldb/CMakeLists.txt`, `src/CMakeLists.txt`, `test/src/CMakeLists.txt`
**Pattern extraction date:** 2026-07-23
</content>
