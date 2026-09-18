# Phase 13: Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps - Pattern Map

**Mapped:** 2026-08-11
**Files analyzed:** 34 likely new/modified source, build, documentation, and test files (grouped below where one pattern applies to a header/source pair or test family)
**Analogs found:** 30 / 34

## Scope Derived From Context and Research

The file inventory below combines the explicit project structure in `13-RESEARCH.md:171-198`, its Wave 0 test list at `:603-615`, and the existing integration points locked by D-01..D-16. The planner may consolidate small model types, but it should preserve the responsibility boundaries: canonical bytes, candidate transport, durable state, policy activation, node-lifetime orchestration, and a thin one-shot command.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/trustedpeer/CanonicalTrustCodec.hpp/.cpp` | utility | transform | `src/base/buffer.cpp`; `src/crypto/sha/sha256.cpp` | primitives-match |
| `src/trustedpeer/GenesisManifest.hpp/.cpp` | model | transform | `src/trustedpeer/TrustedPeerRegistry.hpp/.cpp` (`TrustedPeerListPayload`) | role-match |
| `src/trustedpeer/QuorumPolicy.hpp/.cpp` | model/service | transform | `src/multisig/MultiSig.hpp/.cpp`; `src/securecrdt/QuorumThresholdValidation.hpp` | role-match |
| `src/trustedpeer/TrustStateStore.hpp/.cpp` | service | file-I/O / transactional CRUD | `src/storage/rocksdb/rocksdb.cpp`; `rocksdb_batch.cpp` | exact storage match |
| `src/securecrdt/SecureCrdtCandidate.hpp/.cpp` | model/service | event-driven / request-response | `src/securecrdt/SecureCrdt.hpp/.cpp` | role-and-flow match |
| `src/securecrdt/SecureCrdt.hpp/.cpp` | service | event-driven / CRUD | existing file itself | exact extension |
| `src/securecrdt/SecureCrdtRegistry.hpp` | provider/registry | request-response | existing owner-token registry | role-match; ownership must change |
| `src/securecrdt/QuorumThresholdValidation.hpp` | utility | transform | existing file itself; `src/multisig/MultiSig.cpp` | exact extension |
| `src/trustedpeer/TrustedPeerRegistry.hpp/.cpp` | service/provider | event-driven | existing file itself | exact extension |
| `src/account/BurnConfig.hpp/.cpp` | service/provider | event-driven | existing file itself; `TrustedPeerRegistry.cpp` | exact extension |
| `src/account/GeniusNode.hpp/.cpp` | controller/orchestrator | event-driven | existing startup state machine and teardown code | exact extension |
| `src/account/TransactionManager.hpp/.cpp` | service/consumer | request-response | current BurnConfig cache subscription | exact extension |
| `src/crdt/globaldb/globaldb.hpp/.cpp` and/or `src/crdt/crdt_callback_manager.hpp/.cpp` | provider | event-driven | current register/unregister callback API | exact if subscription API is needed |
| `src/trustedpeer/genesis_tool/GenesisCeremony.hpp/.cpp` | service | request-response / file-I/O | `TrustedPeerRegistry::SeedGenesis`; `GeniusSigner` | partial |
| `src/trustedpeer/genesis_tool/main.cpp` | controller/CLI | one-shot request-response | `example/crdt_globaldb/globaldb_app.cpp` | role-match |
| `src/trustedpeer/genesis_tool/CMakeLists.txt` plus parent CMake wiring | config | build graph | `example/crdt_globaldb/CMakeLists.txt`; subsystem library CMake files | exact |
| `src/securecrdt/CMakeLists.txt`, `src/trustedpeer/CMakeLists.txt`, `src/account/CMakeLists.txt` | config | build graph | their current `add_library`/PUBLIC/PRIVATE patterns | exact |
| `example/node_test/sgns_config.json` | config | startup input | existing file itself | exact modification |
| `docs/trusted-peer-genesis.md` (name at planner discretion) | documentation | manual workflow | no dedicated trust-ceremony runbook | none |
| `test/src/trustedpeer/genesis_manifest_test.cpp` | test | transform | `trustedpeerregistry_genesis_test.cpp` | role-match |
| `test/src/trustedpeer/quorum_policy_test.cpp` | test | transform | `trustedpeerregistry_threshold_floor_test.cpp`; `MultiSig` tests | exact behavior match |
| `test/src/trustedpeer/trust_state_store_test.cpp` | test | file-I/O / transactional CRUD | `test/src/storage/rocksdb/rocksdb_integration_test.cpp` | exact storage match |
| `test/src/securecrdt/securecrdt_candidate_test.cpp` | test | event-driven | `securecrdt_quorum_gate_test.cpp` | exact subsystem match |
| `test/src/securecrdt/securecrdt_candidate_race_test.cpp` | test | event-driven / concurrency | `securecrdt_quorum_gate_test.cpp`; single-node helper | role-match |
| `test/src/trustedpeer/trust_genesis_tool_test.cpp` | test | request-response / file-I/O | `trustedpeerregistry_genesis_test.cpp` | partial |
| `test/src/startup/trust_first_boot_e2e_test.cpp` | test | event-driven E2E | `test/src/startup/startup_wiring_test.cpp` | exact tier match |
| `test/src/startup/trust_restart_test.cpp` | test | file-I/O / startup | `startup_wiring_test.cpp`; RocksDB fixture | role-match |
| `test/src/startup/trust_tamper_e2e_test.cpp` | test | event-driven E2E | `startup_wiring_test.cpp` | role-match |
| `test/src/trustedpeer/operator_approval_test.cpp` | test | event-driven | `trustedpeerregistry_quorum_test.cpp` | exact behavior match |
| `test/src/account/burnconfig_policy_e2e_test.cpp` | test | event-driven / request-response | `test/src/account/burnconfig_test.cpp` | exact subsystem match |
| `test/src/multiaccount/policy_lifetime_multi_account_test.cpp` | test | event-driven E2E | `test/src/account/account_management_test.cpp` | exact workflow match |
| affected `test/src/*/CMakeLists.txt` | config | build graph | current subsystem `addtest` targets | exact |

## Pattern Assignments

### `CanonicalTrustCodec`, `GenesisManifest`, and `QuorumPolicy` (utility/models, transform)

**Primary analogs:** `src/base/buffer.cpp`, `src/crypto/sha/sha256.cpp`, `src/trustedpeer/TrustedPeerRegistry.cpp`

**Imports and byte-container pattern** (`src/base/buffer.hpp:4-10`, `src/crypto/sha/sha256.hpp:4-8`):

```cpp
#include <string_view>
#include <vector>
#include <gsl/span>
#include "base/blob.hpp"
#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
```

**Fixed-width big-endian writer pattern** (`src/base/buffer.cpp:11-30`):

```cpp
Buffer &Buffer::putUint32(uint32_t n) {
  data_.push_back(static_cast<unsigned char &&>((n >> 24) & 0xFF));
  data_.push_back(static_cast<unsigned char &&>((n >> 16) & 0xFF));
  data_.push_back(static_cast<unsigned char &&>((n >> 8) & 0xFF));
  data_.push_back(static_cast<unsigned char &&>((n)&0xFF));
  return *this;
}

Buffer &Buffer::putUint64(uint64_t n) {
  data_.push_back(static_cast<unsigned char &&>((n >> 56u) & 0xFF));
  // ...remaining bytes in descending significance...
  data_.push_back(static_cast<unsigned char &&>((n)&0xFF));
  return *this;
}
```

Copy `Buffer::putUint32`, `putUint64`, and `put(...)` as the encoding primitives. Add explicit length prefixes before every variable-length field; do not copy the current newline-delimited TPR encoding (`TrustedPeerRegistry.cpp:22-34`) into the canonical trust codec.

**Named SHA-256 helper** (`src/crypto/sha/sha256.cpp:7-24`):

```cpp
base::Hash256 sha256(gsl::span<const uint8_t> input) {
    base::Hash256 out;
    unsigned int digest_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, out.data(), &digest_len);
    EVP_MD_CTX_free(ctx);
    return out;
}
```

Use exactly one wrapper around `crypto::sha256(canonical_bytes)` for genesis, policy, burn, and candidate IDs. Never use `std::hash` or hash a display string.

**Structural peer validation pattern** (`src/trustedpeer/TrustedPeerRegistry.cpp:95-113`):

```cpp
if ( entries.empty() ) return false;
std::unordered_set<std::string> unique_entries;
for ( const auto &entry : entries ) {
    if ( !sgns::base::IsHexAddress( entry ) ) return false;
    if ( !unique_entries.insert( entry ).second ) return false;
}
```

Canonicalization must validate first, normalize consistently, sort once, and encode that sorted vector. Decoder rules should be bounded and reject trailing bytes, duplicate peers, non-canonical ordering, unknown encoding versions, and integer overflow.

---

### `SecureCrdtCandidate.hpp/.cpp` and `SecureCrdt.hpp/.cpp` (service, event-driven CRUD)

**Analog:** `src/securecrdt/SecureCrdt.cpp`

**Local validation-before-Put pattern** (`src/securecrdt/SecureCrdt.cpp:59-92`):

```cpp
const auto entry = SecureCrdtRegistry::Resolve( base_key.GetKey() );
if ( !entry ) return outcome::failure( Error::UNREGISTERED_KEY );

auto instance = entry->make_instance();
if ( !instance || !instance->DeserializeFromBytes( payload ) ||
     !instance->Verify( payload ) ) {
    return outcome::failure( Error::MALFORMED_VALUE );
}

auto put_result = db_->Put( base_key, sgns::base::Buffer( payload ), { topic_ } );
if ( put_result.has_error() ) return put_result.error();
return outcome::success();
```

Preserve this fail-before-`Put` shape, but make both local and remote candidate writes call one shared candidate validator: strict key parse, bounded record decode, content-hash/key match, network/kind/version/predecessor checks, current persisted policy lookup, signer-membership check, then signature verification.

**Existing signature/quorum primitives** (`src/securecrdt/SecureCrdt.cpp:114-123`, `:154-199`):

```cpp
if ( !multisig::VerifyPayloadSignature( signer_address, signature, payload ) )
    return outcome::failure( Error::INVALID_SIGNATURE );

const multisig::MultiSig quorum( snapshot.signer_set, snapshot.required_signatures );
if ( !quorum.IsValid() )
    return outcome::success( std::optional<sgns::base::Buffer>{} );
const auto quorum_result = quorum.EvaluateQuorum( collected_signatures, payload );
if ( !quorum_result.has_quorum )
    return outcome::success( std::optional<sgns::base::Buffer>{} );
```

Candidate approvals must verify/sign the canonical candidate bytes carried in the approval record. Use a key of the form:

```text
<domain>/candidate/v<version>/<64-hex-sha256>/approval/<128-hex-signer>
```

Do not copy the existing mutable `base_key` plus `sig/<address>` layout for production TPR/Burn changes: it overwrites candidates and makes acceptance arrival-order-dependent (`SecureCrdt.cpp:239-264`). Keep the old API only where compatibility requires it.

**Filter registration and weak-owner callback** (`src/securecrdt/SecureCrdt.cpp:202-227`):

```cpp
auto weak_self = weak_from_this();
const bool registered = db_->RegisterElementFilter(
    pattern,
    [weak_self, base_key, entry]( const crdt::pb::Element &element ) {
        if ( auto strong = weak_self.lock() )
            return strong->FilterSecureCrdtUpdate( base_key, entry, element );
        return std::optional<std::vector<crdt::pb::Element>>{};
    } );
all_registered = all_registered && registered;
```

Compile an exact segment-aware candidate regex, register once after all policies exist, check every return value, and add the listen topic once.

---

### `SecureCrdtRegistry.hpp` and callback ownership (provider, request-response/event-driven)

**Analogs:** `src/securecrdt/SecureCrdtRegistry.hpp`, `src/crdt/impl/crdt_callback_manager.cpp`

**Owner-token compare-and-remove pattern** (`SecureCrdtRegistry.hpp:78-103`):

```cpp
entry.owner_token = expected_owner;
registry()[key_pattern] = std::move( entry );

auto it = registry().find( key_pattern );
if ( it != registry().end() && it->second.owner_token == expected_token )
    registry().erase( it );
```

Retain the owner-token idea if registration objects remain removable, but make the registry an instance owned by one `SecureCrdt`/`GeniusNode`; the current function-static map at `SecureCrdtRegistry.hpp:143-152` is specifically not a production multi-node pattern.

**Duplicate pattern behavior and explicit removal** (`crdt_callback_manager.cpp:39-59`, `:99-118`):

```cpp
auto it = std::find_if(registry.begin(), registry.end(),
                       [&](const auto &registered) { return registered->pattern == pattern; });
if ( it == registry.end() ) {
    registry.push_back( std::move( entry ) );
    return true;
}
return false;

// Removal is pattern-based:
if ( it != registry.end() ) registry.erase( it );
```

This explains the account-switch bug: recreating an owner with the same pattern does not replace the expired callback. Preferred Phase 13 pattern is node-scoped services registered once. If the planner chooses removable subscriptions, return an opaque token and remove by token/owner, not a pattern that may be shared.

---

### `QuorumPolicy.hpp/.cpp` and threshold validation (model/service, transform)

**Analogs:** `src/multisig/MultiSig.cpp`, `src/securecrdt/QuorumThresholdValidation.hpp`

**N-of-M fail-closed and deduplicated quorum** (`src/multisig/MultiSig.cpp:20-69`):

```cpp
MultiSig::MultiSig( const std::vector<std::string> &signer_set,
                    uint64_t required_signatures )
  : signer_set_( signer_set.begin(), signer_set.end() ),
    required_signatures_( required_signatures ) {}

bool MultiSig::IsValid() const {
    return required_signatures_ > 0 && required_signatures_ <= signer_set_.size();
}

if ( valid_unique_signers.count( address ) != 0 ) continue;
if ( signer_set_.count( address ) == 0 ) continue;
if ( !VerifyPayloadSignature( address, signature, payload ) ) continue;
valid_unique_signers.insert( address );
```

Build quorum evaluation from the persisted current `QuorumPolicyState`, never the proposed successor. Validate the policy before constructing `MultiSig`.

**Replace, do not copy, the old generic floor** (`QuorumThresholdValidation.hpp:35-42` currently):

```cpp
const uint64_t minimum_safe_threshold =
    ( static_cast<uint64_t>( signer_set_size ) * 51 + 99 ) / 100;
```

Phase 13 requires policy-specific integer formulas after `1 <= threshold <= M` and non-empty/unique/valid signer checks:

```cpp
const uint64_t membership_floor = m / 2 + 1;
const uint64_t burn_floor = m - m / 3; // ceil(2m/3), avoids 2*m overflow
```

Every successor must additionally satisfy `version == current.version + 1`, `expected_previous_hash == current.hash`, and `authorizing_policy_hash == current.policy_hash`.

---

### `TrustStateStore.hpp/.cpp` (service, synchronous file-I/O / transactional CRUD)

**Analog:** `src/storage/rocksdb/rocksdb.cpp` and `rocksdb_batch.cpp`

**Synchronous open/write configuration** (`rocksdb.cpp:22-64`):

```cpp
auto store = std::make_shared<rocksdb>();
// ...DB::Open...
store->wo_.sync = true;
store->setWriteOptions( store->wo_ );
return store;
```

Use `storage::rocksdb::create(network_scoped_path)` and do not override its write options with `sync=false`.

**Atomic batch commit** (`rocksdb_batch.cpp:10-37`):

```cpp
auto batch = db->batch();
BOOST_OUTCOME_TRY( batch->put( snapshot_key, snapshot_bytes ) );
BOOST_OUTCOME_TRY( batch->put( policy_head_key, policy_hash ) );
BOOST_OUTCOME_TRY( batch->put( burn_head_key, burn_hash ) );
BOOST_OUTCOME_TRY( batch->commit() );
```

`Batch::commit()` calls `db_.db_->Write(db_.wo_, &batch_)`, so it inherits the synchronous option. Under one transition mutex: load and verify the current durable head, validate predecessor/version/quorum, commit all new records/head keys in one batch, then publish caches. Never publish a cache before `commit()` succeeds.

**Storage test pattern** (`test/src/storage/rocksdb/rocksdb_integration_test.cpp:54-79`):

```cpp
auto batch = db_->batch();
ASSERT_TRUE( batch );
EXPECT_OUTCOME_TRUE_1( batch->put( key, value ) );
EXPECT_FALSE( db_->contains( key ) );
EXPECT_OUTCOME_TRUE_1( batch->commit() );
EXPECT_TRUE( db_->contains( key ) );
```

Extend this with reopen/load verification, corrupt bytes, version decrease, wrong network, fork predecessor, and injected commit failure. A rejected candidate must leave the prior durable snapshot and cache intact.

---

### `TrustedPeerRegistry.hpp/.cpp` and `BurnConfig.hpp/.cpp` (node services, event-driven)

**Analogs:** their existing `New` factories, signer-source registrations, and callbacks.

**Outcome factory with register-after-construction** (`TrustedPeerRegistry.cpp:138-157`):

```cpp
auto validation_result = ValidateQuorumThreshold( quorum_threshold, genesis_peers.size() );
if ( validation_result.has_error() ) return validation_result.error();

auto instance = std::make_shared<TrustedPeerRegistry>( /* dependencies */ );
instance->RegisterSignerSetSource();
return instance;
```

Keep this factory/error style. Change its inputs to the confirmed-policy/store/provider abstractions and leave the active signer cache empty on fresh boot. The existing constructor assignment `cached_peers_(genesis_peers)` at `TrustedPeerRegistry.cpp:120-130` is an anti-pattern under D-13.

**Weak callback that re-derives authoritative state** (`BurnConfig.cpp:147-175`):

```cpp
auto weak_self = weak_from_this();
db_->RegisterNewElementCallback(
    pattern,
    [weak_self]( crdt::CRDTCallbackManager::NewDataPair, const std::string & ) {
        if ( auto self = weak_self.lock() ) self->OnCrdtElementChanged();
    } );

auto read_result = secure_crdt_->ReadIfQuorum( base_key_ );
if ( read_result.has_error() || !read_result.value().has_value() ) return;
```

Continue to ignore the callback's positional value and re-read/validate candidate approvals. Add an explicit candidate inbox callback for presentation only; receiving a record must never call signing code.

**Copy callbacks before invocation** (`BurnConfig.cpp:177-195`):

```cpp
std::vector<RefreshCallback> callbacks_copy;
{
    std::lock_guard<std::mutex> lock( refresh_callbacks_mutex_ );
    callbacks_copy = refresh_callbacks_;
}
for ( const auto &cb : callbacks_copy ) cb( new_value );
```

Preserve the no-callback-under-lock rule, but publish only after `TrustStateStore` commits. Prefer a node-scoped atomic/shared cache directly consumed by each new `TransactionManager`; this avoids accumulating weak callbacks in `refresh_callbacks_` (`BurnConfig.cpp:244-247`).

---

### `GeniusNode.hpp/.cpp` and `TransactionManager.hpp/.cpp` (orchestrator/consumer, event-driven)

**Analog:** existing dependency construction and weak-consumer cache subscription.

**Dependency-order construction** (`GeniusNode.cpp:749-803`):

```cpp
secure_crdt_ = std::make_shared<securecrdt::SecureCrdt>( tx_globaldb_, quorum_topic );
auto tpr_result = trustedpeer::TrustedPeerRegistry::New( secure_crdt_, /*...*/ );
if ( tpr_result.has_error() ) return;
trusted_peer_registry_ = tpr_result.value();

auto burn_result = account::BurnConfig::New( secure_crdt_, tx_globaldb_,
                                              trusted_peer_registry_, /*...*/ );
if ( burn_result.has_error() ) { ResetQuorumMembers(); return; }
burn_config_ = burn_result.value();

if ( !secure_crdt_->RegisterFilters() ) { ResetQuorumMembers(); return; }
```

Keep dependency order and checked construction, but perform it once when the retained GlobalDB/node policy layer starts. Insert `TrustStateStore` load/verification before exposing an active policy, then enter one of the explicit fresh/restart/mismatch/rollback states from Research Pattern 5.

**Current teardown boundary to split** (`GeniusNode.cpp:1702-1765`):

```cpp
if ( release_members ) {
    ResetProcessingMembers();
    transaction_manager_.reset();
    ResetQuorumMembers(); // Phase 13 must not run this during SelectAccount()
    // ...
}
```

`SelectAccount()` currently calls `ShutdownAccountBoundServices(true)` at `GeniusNode.cpp:2214`; that default destroys TPR/Burn/SecureCrdt. Refactor so account switching stops/recreates `TransactionManager` and other account consumers but retains `secure_crdt_`, the instance registry, `trust_state_store_`, `trusted_peer_registry_`, `burn_config_`, their callbacks, and confirmed caches. Call full policy teardown only from the GlobalDB/node shutdown path (`GeniusNode.cpp:1768-1787`).

**Weak consumer refresh pattern** (`TransactionManager.cpp:264-273`):

```cpp
burn_config->RegisterRefreshCallback(
    [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )]( uint64_t new_value ) {
        if ( auto strong = weak_ptr.lock() )
            strong->burn_basis_points_.store( new_value, std::memory_order_relaxed );
    } );
```

If callbacks remain, retain the weak capture. Better: inject the node-scoped confirmed burn cache/provider into each new manager. Either approach must add an explicit readiness gate so BurnConfig-dependent economic operations fail closed until confirmed policy and initial burn state are ready.

---

### `GenesisCeremony` and one-shot `main.cpp` (service/CLI, request-response + file-I/O)

**Analogs:** `example/crdt_globaldb/globaldb_app.cpp`, `TrustedPeerRegistry::SeedGenesis`, `GeniusSigner`.

**Boost.Program_options parse/error pattern** (`globaldb_app.cpp:35-80`):

```cpp
namespace po = boost::program_options;
po::options_description desc( "Input arguments:" );
try {
    desc.add_options()
        ( "help,h", "print help" )
        ( "databasePath,db", po::value<std::string>(&database_path), "Path" );
    po::variables_map vm;
    po::store( po::parse_command_line( argc, argv, desc ), vm );
    po::notify( vm );
    if ( vm.count("help") ) { std::cout << desc << '\n'; return EXIT_SUCCESS; }
} catch ( const std::exception &e ) {
    std::cerr << "Error parsing arguments: " << e.what() << '\n';
    std::cout << desc << '\n';
    return EXIT_FAILURE;
}
```

Do not add the private key as an option. Accept only a protected file path or stdin selector; load secret bytes after ordinary options parse, never log them, and unlink only after confirmed local persistence.

**Minimal GlobalDB composition** (`globaldb_app.cpp:108-142`): construct `io_context`, GossipPubSub, scheduler, graphsync network/request generator, call `GlobalDB::New(...)`, then add listen/broadcast topics and `Start()`. Extract a reusable narrow composition helper if possible; do not construct a full `GeniusNode` with the ephemeral key.

**In-memory signer import** (`ProofSystem/src/EthereumKeyGenerator.cpp:26-40`, `src/account/GeniusSigner.hpp:18-46`):

```cpp
ethereum::EthereumKeyGenerator keypair( private_key_view );
sgns::GeniusSigner signer( std::move( keypair ) );
const auto address = signer.GetAddress();
const auto signature = signer.Sign( canonical_genesis_bytes );
```

`GeniusSigner` is explicitly persistence-free. Do not use `GeniusAccount::NewFromPrivateKey`, which enters account storage/indexing. Compare `signer.GetAddress()` to the manifest bootstrapper before operator confirmation and submission.

**Build target pattern** (`example/crdt_globaldb/CMakeLists.txt:1-5`):

```cmake
add_executable(sgns_trust_genesis main.cpp)
target_link_libraries(sgns_trust_genesis
    trustedpeer
    securecrdt
    crdt_globaldb
    sgns_genius_account
    Boost::program_options
)
```

Keep the executable thin; validation/canonicalize/fingerprint/submit/confirm behavior belongs in a reusable, unit-testable `GenesisCeremony` service.

---

### Test files and subsystem `CMakeLists.txt` (tests, transform/event-driven/file-I/O)

**Single-node real GlobalDB fixture** (`test/src/securecrdt/securecrdt_test_node.hpp:108-152`):

```cpp
const std::string basePath = binaryPath + "/" + dbName + "_" + testName;
boost::filesystem::remove_all( basePath );
boost::filesystem::create_directories( basePath );
// create GossipPubSub, scheduler, graphsync, then GlobalDB::New(...)
node->db->Start();
node->ioThread = std::thread( [io]() { io->run(); } );
```

Reuse this for CRDT candidate, operator approval, and small genesis integration tests. Pure codec/policy/store tests should avoid network construction. The fixture opens local listeners, so network-backed test execution may require the same local-listener permission noted by research.

**Fixture teardown and registry hygiene** (`securecrdt_quorum_gate_test.cpp:61-110`):

```cpp
void TearDown() override {
    SecureCrdtRegistry::UnregisterIf( pattern, &token_ );
    secure_crdt_.reset();
    node_.reset();
    GeniusAccount::SetSecureStorageFactory( nullptr );
    boost::filesystem::remove_all( path_ );
}
```

For the new instance-scoped registry, assertions should verify two nodes do not replace each other's registrations and that policy object addresses/registration counts remain stable across account selection.

**Quorum negative-path assertion style** (`securecrdt_quorum_gate_test.cpp:127-168`):

```cpp
ASSERT_FALSE( secure_crdt_->ProposeValue( key, payload ).has_error() );
auto read = secure_crdt_->ReadIfQuorum( key );
ASSERT_FALSE( read.has_error() );
EXPECT_FALSE( read.value().has_value() );

auto invalid = secure_crdt_->AddSignature( key, signer, invalid_signature );
ASSERT_TRUE( invalid.has_error() );
EXPECT_EQ( invalid.error(), SecureCrdt::Error::INVALID_SIGNATURE );
EXPECT_TRUE( node_->db->Get( rejected_key ).has_error() );
```

Candidate tests should assert rejection before retention/callback, exact candidate-byte binding, outsider rejection, deduplication, coexistence, and one durable winner under a barrier-driven race.

**Account-switch workflow** (`test/src/account/account_management_test.cpp:68-82`):

```cpp
auto old_address = node_->GetAddress();
auto new_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path, true ).first->GetAddress();
ASSERT_TRUE( node_->SelectAccount( new_address ).has_value() );
test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; }, timeout, "node not synced" );
ASSERT_EQ( node_->GetAddress(), new_address );
ASSERT_TRUE( node_->SelectAccount( old_address ).has_value() );
```

Extend this pattern with captured node-policy object identities, callback/registration counts, and a post-switch BurnConfig quorum update whose value is observed by the replacement `TransactionManager` and actual `PayEscrow` output.

**`addtest` target conventions** (`test/src/securecrdt/CMakeLists.txt:15-42`, `test/src/trustedpeer/CMakeLists.txt:1-33`):

```cmake
set(TRUSTEDPEER_TEST_NODE_LIBS
    trustedpeer securecrdt crdt_globaldb multisig
    json_secure_storage libsecp256k1::secp256k1 sgns_genius_account
)

addtest(quorum_policy_test quorum_policy_test.cpp)
target_link_libraries(quorum_policy_test ${TRUSTEDPEER_TEST_NODE_LIBS})
target_include_directories(quorum_policy_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
```

Use `addtest(name source)` with an explicit target link list. Startup/multiaccount tests that link `genius_node_test` must copy the existing platform-specific whole-archive/`-force_load` block (`test/src/startup/CMakeLists.txt:27-37`, `test/src/account/CMakeLists.txt:49-59`) so registration-based code is not stripped.

## Shared Patterns

### Authentication and Quorum

**Source:** `src/multisig/MultiSig.cpp:13-18`, `:40-70`

**Apply to:** candidate local gate, remote filter, TPR/policy activation, BurnConfig activation, genesis confirmation.

Use `multisig::VerifyPayloadSignature(address, signature, canonical_bytes)` and `MultiSig` over a previously loaded persisted policy snapshot. Authenticate the proposer before CRDT retention; re-derive quorum again immediately before durable activation.

### Outcome Errors and Logging

**Source:** `src/securecrdt/SecureCrdt.cpp:12-29`, `:62-92`

**Apply to:** all new service factories, decoders, storage operations, and CLI service calls.

Declare typed errors with `OUTCOME_HPP_DECLARE_ERROR_2`, define human-readable categories with `OUTCOME_CPP_DEFINE_CATEGORY_3`, return the first lower-layer error, and log domain/kind/version/hash/signer/outcome. Never log private key bytes or full secret-bearing input.

### Persist Before Publish

**Source:** synchronous adapter `src/storage/rocksdb/rocksdb.cpp:22-64`; batch `rocksdb_batch.cpp:29-37`.

**Apply to:** genesis confirmation, policy successor activation, and BurnConfig activation.

One node-scoped transition mutex must cover durable-head reload, stale/fork checks, quorum evaluation, and atomic batch commit. Cache stores and consumer callbacks happen only after commit succeeds.

### Callback Lifetime

**Source:** `BurnConfig.cpp:147-159`; `crdt_callback_manager.cpp:25-59`, `:99-118`.

**Apply to:** candidate inbox, TPR activation, BurnConfig activation, and TransactionManager consumption.

Capture service owners weakly, register policy callbacks exactly once for the GlobalDB lifetime, check duplicate-registration failures, and either retain services across account switching or introduce owner-scoped subscription tokens.

### Build Targets

**Source:** `src/securecrdt/CMakeLists.txt:1-9`, `src/trustedpeer/CMakeLists.txt:1-10`, `src/account/CMakeLists.txt:58-69`.

Use one focused library per subsystem, declare reusable dependencies `PUBLIC`, implementation-only helpers `PRIVATE`, and call `supergenius_install(...)` for production libraries. No new external package is required.

## Configuration and Documentation Assignment

### `example/node_test/sgns_config.json`

Current trust fields are at lines 10-16. Keep any bootstrap copies clearly marked as fresh-bootstrap diagnostics/input only and label all current addresses as non-production placeholders. Post-confirmation authority comes from `TrustStateStore`, not this JSON.

### Operator runbook

No close code analog exists. The runbook must concretely cover trusted-channel public-key collection, validation/canonical sorting, displayed fingerprint comparison, explicit typed confirmation, protected key-file/stdin use, deletion only after verified confirmation, restart/config-conflict behavior, candidate approval procedure, alerts for rollback/fork, and the limitation that local disk rollback/physical secure deletion cannot be fully prevented by this software alone.

## No Analog Found

| File / Responsibility | Role | Data Flow | Reason / Planner Guidance |
|---|---|---|---|
| `CanonicalTrustCodec` complete decoder/schema | utility | transform | `base::Buffer` supplies write primitives but no bounded canonical reader or domain-separated trust schema. Follow `13-RESEARCH.md` Pattern 1 and add golden vectors. |
| `TrustStateStore` verifiable policy/burn chain schema | service | file-I/O | RocksDB mechanics exist, but no local signed trust-chain/high-water-mark store exists. Follow persist-before-publish and atomic batch patterns. |
| `GenesisCeremony` secret lifecycle | service | file-I/O / request-response | In-memory signer import and CLI parsing exist separately; no existing command combines protected secret input, operator review, confirmation wait, cleanse, and success-only unlink. |
| Local operator candidate inbox/approval surface | controller/service | event-driven | No authenticated local administration surface exists. Keep it local-only and explicit; do not add remote RPC/pubsub. |

## Planner Guardrails

- Treat D-01..D-16 as locked; research recommendations are design guidance where the context grants discretion.
- Do not let genesis config peers become an active signer set before durable genesis confirmation.
- Do not use a proposed policy to authorize itself.
- Do not retain an unauthenticated candidate base value.
- Do not mix approval signatures across candidate content hashes or policy epochs.
- Do not update caches before synchronous storage commit.
- Do not destroy/re-register node policy services in `SelectAccount()`.
- Do not pass the ephemeral private key through argv/environment, `GeniusNode::FromPrivateKey`, or `GeniusAccount::NewFromPrivateKey`.
- Do not trust replicated “final” markers; readers re-derive quorum and validate ancestry.

## Metadata

**Analog search scope:** `src/base`, `src/crypto`, `src/multisig`, `src/securecrdt`, `src/trustedpeer`, `src/account`, `src/crdt`, `src/storage/rocksdb`, `ProofSystem`, `example/crdt_globaldb`, `example/node_test`, and affected `test/src` subsystems.

**Strong analog set:** `base::Buffer`/SHA-256; `SecureCrdt`/`SecureCrdtRegistry`; `MultiSig`; RocksDB batch adapter; `TrustedPeerRegistry`/`BurnConfig`/`GeniusNode`; Program_options GlobalDB example; GoogleTest `addtest` targets.

**Pattern extraction date:** 2026-08-11
