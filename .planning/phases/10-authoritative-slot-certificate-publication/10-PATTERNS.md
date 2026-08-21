# Phase 10: Authoritative Slot Certificate Publication - Pattern Map

**Mapped:** 2026-08-21  
**Files analyzed:** 9 expected production/test files, plus 4 conditional storage files  
**Analogs found:** 9 / 9 expected files

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/blockchain/Consensus.hpp` | protocol service / API | event-driven + CRUD | `src/blockchain/Consensus.hpp` certificate/recovery declarations | exact |
| `src/blockchain/Consensus.cpp` | protocol service | event-driven + CRUD | `src/blockchain/Consensus.cpp` `ProcessCertificates`, ingress, and recovery | exact |
| `src/blockchain/Blockchain.hpp` | facade API | request-response | `src/blockchain/Blockchain.hpp` certificate facade | exact |
| `src/blockchain/impl/Blockchain.cpp` | facade implementation | request-response | `src/blockchain/impl/Blockchain.cpp:1755-1784` | exact |
| `src/account/TransactionManager.cpp` | transaction service | request-response | `TransactionManager::FetchTransaction` at `src/account/TransactionManager.cpp:1654-1675` | exact |
| `src/account/GeniusInputValidator.cpp` | validation middleware | request-response | witness certificate validation at `src/account/GeniusInputValidator.cpp:454-468` | role-match |
| `src/blockchain/ValidatorRegistry.hpp` | registry service API | batch + transform | `ValidatorRegistry::LoadCertificateBySubjectHash` declaration at `src/blockchain/ValidatorRegistry.hpp:506-512` | exact |
| `src/blockchain/ValidatorRegistry.cpp` | registry service | batch + transform | batch certificate loading at `src/blockchain/ValidatorRegistry.cpp:752-769, 948-959, 1297-1318` | exact |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | component test | event-driven + CRUD | existing accessor, ingress, and durable-readback cases in the same file | exact |
| `src/crdt/globaldb/globaldb.hpp/.cpp` (conditional) | storage API | CRUD | current `Put`/`Get` facade at `src/crdt/globaldb/globaldb.hpp:91-115`, `.cpp:485-508` | role-match |
| `src/crdt/impl/crdt_datastore.cpp` and/or `src/crdt/impl/crdt_set.cpp` (conditional) | CRDT storage engine | event-driven + CRUD | `PutKey` and `SetValue` merge behavior at `src/crdt/impl/crdt_datastore.cpp:1303-1314`, `src/crdt/impl/crdt_set.cpp:609-651` | role-match |

`test/src/blockchain/CMakeLists.txt` already registers and links `consensus_pending_lifecycle_test` at lines 36-46; no CMake change is expected unless the planner introduces a separate test target. `test/src/blockchain/consensus_slot_key_test.cpp` is a useful slot-identity reference, but it is not the primary publication/recovery test home.

## Pattern Assignments

### `src/blockchain/Consensus.hpp` and `src/blockchain/Consensus.cpp` (protocol service, event-driven + CRUD)

**Analog:** Existing certificate publication, CRDT ingress, and committed recovery in `src/blockchain/Consensus.cpp`.

**Public/private API placement** (`src/blockchain/Consensus.hpp:481-521, 813-886`): keep public lookup declarations next to `SubmitCertificate` and put authoritative-key/collision helpers in the private recovery group. Replace the legacy-key declaration rather than retaining a second authority predicate.

```cpp
outcome::result<void> SubmitCertificate( const Certificate &certificate );
outcome::result<Certificate> GetCertificateBySubjectHash( const std::string &subject_hash ) const;
bool CheckCertificateForSubject( const std::string &subject_hash ) const;

outcome::result<bool> HasAcceptedCertificateForSlot( const std::string &slot_key ) const;
void RecoverPendingCertificateWork();
static std::string GetExpectedCertificateSlotKey( const Certificate &certificate );
```

**Deterministic authority gate** (`src/blockchain/Consensus.cpp:517-545, 2357-2414`): preserve the sorted validator, proposal-derived base index, and current-round rotation. `ProcessCertificates` is the only normal path to `SubmitCertificate`; an `ActiveButNotAggregator` returns without writing. Keep proposal evidence/state when a write cannot proceed. Do not add a receiver authority path.

```cpp
const auto round = GetCurrentRound( proposal.timestamp() );
const auto index = ( base_index + round ) % ordered.size();
return ordered[index] == account_address_ ? AggregatorRole::CurrentAggregator
                                          : AggregatorRole::ActiveButNotAggregator;

if ( aggregator_role == AggregatorRole::ActiveButNotAggregator ) {
    continue;
}
// selected aggregator creates and submits the certificate
if ( SubmitCertificate( certificate_result.value() ).has_error() ) {
    continue;
}
```

**Publication sequencing and error style** (`src/blockchain/Consensus.cpp:1967-2025`): retain validation, protobuf serialization, `crdt::HierarchicalKey`, `GlobalDB::Buffer`, structured logging, and `outcome::failure`. Change the order and key: validate → derive `/cert/<slot>` → collision-safe occupied-record decision → durable `db_->Put` → construct/publish the full `ConsensusMessage`. On read, parse, validation, or write uncertainty return an error so normal rounds retain/retry evidence. Do not clear the slot locally after the write.

```cpp
if ( ValidateCertificate( certificate ) != Check::Approve ) {
    return outcome::failure( std::errc::invalid_argument );
}
std::string serialized;
if ( !certificate.SerializeToString( &serialized ) ) {
    return outcome::failure( std::errc::invalid_argument );
}
crdt::HierarchicalKey cert_key( key );
crdt::GlobalDB::Buffer cert_value;
cert_value.put( serialized );
auto cert_put = db_->Put( cert_key, cert_value, { consensus_datastore_topic_ } );
if ( cert_put.has_error() ) {
    return outcome::failure( cert_put.error() );
}
```

**Authoritative ingress predicate** (`src/blockchain/Consensus.cpp:2474-2505, 2615-2637`): copy the parse → required ID → key binding → `ValidateCertificate` rejection ladder, but bind `element.key()` to `GetExpectedCertificateSlotKey(certificate)`, not `GetSubjectHash`. This same predicate must serve filter, durable recovery, accepted-slot detection, and direct lookup.

```cpp
if ( !certificate.ParseFromString( element.value() ) ) {
    return std::vector<crdt::pb::Element>{};
}
if ( certificate.proposal_id().empty() ) {
    return std::vector<crdt::pb::Element>{};
}
if ( !ValidateAuthoritativeCertificateKey( certificate, element.key() ) ) {
    return std::vector<crdt::pb::Element>{};
}
if ( ValidateCertificate( certificate ) == Check::Reject ) {
    return std::vector<crdt::pb::Element>{};
}
return std::nullopt;
```

`GetExpectedCertificateSlotKey` already has the desired key shape (`src/blockchain/Consensus.cpp:2630-2637`):

```cpp
if ( !ValidateCertificateBinding( certificate ) ) {
    return {};
}
return std::string{ CERTIFICATE_BASE_PATH_KEY } + GetSlotKey( certificate.proposal() );
```

**Pre-commit callback / recovery split** (`src/blockchain/Consensus.cpp:2507-2518, 3521-3577`): callbacks only mark the journal seen/stalled. Recovery subsequently calls `db_->Get`, parses, checks the authoritative key, validates, releases the matching active vote, and processes the committed certificate. Preserve this for the publisher too.

```cpp
// Callback: no authority, persistence, or lock release.
certificate_work_journal_->MarkSeen( key );
certificate_work_journal_->MarkStalled( key, std::chrono::milliseconds( 0 ) );

// Recovery: committed readback is required before completion.
auto value = db_->Get( { entry.key } );
if ( value.has_error() ) {
    certificate_work_journal_->MarkStalled( entry.key, std::chrono::milliseconds( 0 ) );
    continue;
}
```

**PubSub receiver boundary** (`src/blockchain/Consensus.cpp:2795-2831, 3368-3372`): retain `HandleCertificate` as validation and volatile proposal-state handling only. It must never derive a key, call `SubmitCertificate`, or call `db_->Put` merely because a PubSub envelope arrived.

### `src/blockchain/Blockchain.hpp` and `src/blockchain/impl/Blockchain.cpp` (facade API, request-response)

**Analog:** Current thin forwarding wrappers at `src/blockchain/impl/Blockchain.cpp:1755-1784`.

```cpp
bool Blockchain::CheckCertificate( const std::string &subject_hash ) const {
    return consensus_manager_->CheckCertificateForSubject( subject_hash );
}

outcome::result<ConsensusManager::Certificate> Blockchain::GetCertificateBySubjectHash(
    const std::string &subject_hash ) const {
    return consensus_manager_->GetCertificateBySubjectHash( subject_hash );
}
```

Expose a slot-aware/transaction-aware facade alongside or in place of the hash-only names, then preserve this direct forwarding style. The façade must not synthesize `/cert/<hash>`; it should delegate slot derivation/validation to the appropriate consensus or transaction owner and return `outcome` errors to callers.

### `src/account/TransactionManager.cpp` (transaction service, request-response)

**Analog:** Transaction address → CRDT retrieval → deserialize at `src/account/TransactionManager.cpp:1320-1333, 1654-1675`.

```cpp
std::string TransactionManager::GetTransactionPath( const std::string &tx_hash ) {
    return GetBlockChainBase() + GeniusTransaction::GetTransactionFullPath( tx_hash );
}

outcome::result<std::shared_ptr<GeniusTransaction>> TransactionManager::FetchTransaction(
    const std::shared_ptr<crdt::GlobalDB> &db, std::string_view transaction_key ) {
    BOOST_OUTCOME_TRY( auto transaction_data, db->Get( { std::string( transaction_key ) } ) );
    return DeSerializeTransaction( transaction_data );
}
```

For prior-transaction finality (`src/account/TransactionManager.cpp:4439-4461`), replace direct `GetCertificateBySubjectHash(previous_hash)` with the existing lookup pattern: `FetchTransaction(globaldb_m, GetTransactionPath(previous_hash))`, then `previous_tx->GetSlotID()`, then authoritative slot lookup. Keep the current error-to-`ValidationResult::Pending(PendingDependencyKey::Certificate(previous_hash))` behavior when the transaction/certificate is unavailable; never retry by querying `/cert/<previous_hash>`.

### `src/account/GeniusInputValidator.cpp` (validation middleware, request-response)

**Analog:** Existing producer-certificate guard at `src/account/GeniusInputValidator.cpp:454-468`.

```cpp
auto producer_cert_result = blockchain->GetCertificateBySubjectHash(
    input.txid_hash_.toReadableString() );
if ( producer_cert_result.has_error() ) {
    logger->error( "ValidateWitness(Genius) missing producer certificate for input tx={}",
                   PreviewValue( input.txid_hash_.toReadableString() ) );
    return false;
}
```

Keep the immediate `false`/error-log guard and subsequent certificate subject validation. Only migrate how the certificate is resolved: load the hash-addressed transaction, call its `GetSlotID()` (base implementation at `src/account/GeniusTransaction.hpp:274-277`), then use the authoritative slot lookup. Missing transaction remains fail-closed; no legacy key fallback.

### `src/blockchain/ValidatorRegistry.hpp` and `src/blockchain/ValidatorRegistry.cpp` (registry service, batch + transform)

**Analog:** Existing batch lookup helper and explicit failure propagation at `src/blockchain/ValidatorRegistry.cpp:752-769, 948-959, 1297-1318`.

```cpp
auto cert_result = LoadCertificateBySubjectHash( tx_subject_hash );
if ( cert_result.has_error() ) {
    std::lock_guard<std::mutex> lock( batch_mutex_ );
    applying_batch_subject_ids_.erase( subject_hash );
    return BatchCertificateDecision::Reject;
}
certificates.push_back( cert_result.value() );
```

The header declaration (`src/blockchain/ValidatorRegistry.hpp:506-512`) and its definition currently concatenate `"/cert/" + subject_hash`. Replace that helper/API with a generic-slot-derived certificate lookup; it must validate that the retrieved record derives to the requested generic slot. Preserve `subject_hashes` as batch identity metadata and preserve the existing batch cleanup/reject behavior. Do not apply the transaction-only `GetSlotID()` approach here or redesign batch identity.

### `test/src/blockchain/consensus_pending_lifecycle_test.cpp` (component test, event-driven + CRUD)

**Analog:** The in-file friend accessor and deterministic lifecycle tests.

The accessor exposes private behavior without production test hooks (`lines 78-159, 271-274`):

```cpp
static std::optional<std::vector<crdt::pb::Element>> FilterCertificate(
    const std::shared_ptr<ConsensusManager> &manager, const crdt::pb::Element &element ) {
    return manager->FilterCertificate( element );
}
static void CertificateReceived( const std::shared_ptr<ConsensusManager> &manager,
                                 crdt::CRDTCallbackManager::NewDataPair new_data ) {
    manager->CertificateReceived( std::move( new_data ), std::string{} );
}
static void RecoverPendingCertificateWork( const std::shared_ptr<ConsensusManager> &manager ) {
    manager->RecoverPendingCertificateWork();
}
```

Copy the existing ingress test layout at `lines 717-799`: build an actual signed certificate, serialize it, construct key/value `crdt::pb::Element`s, register a handler counter, assert filter acceptance/rejection, then assert the keyless receiver creates no storage effect. Change the positive key to `GetExpectedCertificateSlotKey(certificate)` and make the legacy subject key a negative case.

Copy the durable recovery progression at `lines 1496-1558`: callback → stalled journal and locked vote → missing readback stays stalled → durable write → recovery releases only the matching slot. Rename the legacy test helper at `lines 125-135` to write the canonical slot key and add deterministic assertions for:

- non-selected aggregator: no write and no announcement;
- selected aggregator: write occurs before one best-effort announcement;
- write failure or indeterminate read: no announcement and slot state remains retryable;
- empty slot, byte-identical replay, different valid payload, malformed payload, and unreadable slot record;
- successor normal round with the same exact certificate and no competing overwrite;
- publisher completion only after callback/recovery, just like a receiving node.

Use no sleeps: existing test access already provides direct `ProcessCertificates` and state/round forcing seams. Extend that accessor with observable write/advertisement ordering and canonical seed/read helpers rather than adding general production hooks.

`test/src/blockchain/consensus_slot_key_test.cpp:129-153` remains the analog for asserting slot identity registration and `GetSlotID()` dispatch; retain it as a focused unit companion, but keep publication/collision/recovery scenarios in the lifecycle fixture.

## Shared Patterns

### Certificate validation and fail-closed errors

**Source:** `src/blockchain/Consensus.cpp:2520-2613` and `3521-3577`  
**Apply to:** all certificate key checks, collision reads, public lookup helpers, and recovery.

Use `Check::Approve` as the only accepted state. Treat `Pending`, `Stalled`, parse failure, mismatched key, unavailable registry, and datastore read failure as unavailable/failed—not as proof that the slot is empty. Log with `ConsensusManagerLogger()` and return an `outcome::failure`/keep the journal stalled.

### Authority and PubSub boundary

**Source:** `src/blockchain/Consensus.cpp:2357-2414, 2795-2831, 3368-3372`  
**Apply to:** certificate production and all PubSub receive paths.

Only normal-round `CurrentAggregator` can enter the write path. `HandleCertificate` is deliberately keyless and must not become a writer. A local publisher must also await normal CRDT callback/readback recovery.

### Canonical slot key

**Source:** `src/blockchain/Consensus.cpp:2615-2637`  
**Apply to:** persistence, element filtering, recovery, direct lookup, and registry batch lookup.

```cpp
return std::string{ CERTIFICATE_BASE_PATH_KEY } + GetSlotKey( certificate.proposal() );
```

For transaction consumers, the source of the slot is the stored `GeniusTransaction::GetSlotID()`, not a certificate/transaction hash suffix.

### CRDT collision constraint (planning checkpoint)

**Source:** `src/crdt/globaldb/globaldb.cpp:485-508`, `src/crdt/impl/crdt_datastore.cpp:1303-1314`, and `src/crdt/impl/crdt_set.cpp:609-651`.

`GlobalDB::Put` delegates directly to `PutKey`; `PutKey` creates an add delta; `CrdtSet::SetValue` writes a different payload after equal-priority comparison unless bytes are identical. Therefore a local `Get` followed by `Put` is not proof of distributed first-writer immutability. Before implementation sign-off, resolve whether an existing lower-layer conditional/immutable primitive can be exposed or whether the protocol guarantees serialize the candidate writers. Do not claim D-04 is satisfied solely by a pre-read.

```cpp
if ( aPriority == priorityResult.value() ) {
    auto valueResult = this->GetValueFromDatastore( valueK );
    if ( valueResult.value() == std::string( aValue.toString() ) ) {
        return outcome::success();
    }
}
// Different equal-priority value falls through to storage.
BOOST_OUTCOME_TRY( aDataStore->put( valueKeyBuffer, aValue ) );
```

### Test registration

**Source:** `test/src/blockchain/CMakeLists.txt:36-46`  
**Apply to:** Phase 10 tests.

Extend the existing registered `consensus_pending_lifecycle_test`; it already links `blockchain_genesis`, `genius_node_test`, `rapidjson`, and `base_crdt_test`. No target wiring change is needed for the planned tests.

## No Analog Found

| File / seam | Role | Data Flow | Reason / planner action |
|---|---|---|---|
| Distributed conditional immutable certificate write | storage API | CRUD | No exposed `GlobalDB` compare-and-set/put-if-absent API exists. Inspect/resolve this before asserting collision safety; conditional modifications may be required in GlobalDB/CrdtDatastore/CrdtSet. |
| Observable PubSub transport failure | transport test seam | event-driven | Current `ConsensusManager::Publish` invokes a void transport call, so the checked-in seam cannot expose a send failure. Decide whether a narrow injectable/returning wrapper is available/in scope; never add retries. |
| Dedicated transaction-to-slot consumer test target | integration/component test | request-response | No existing focused account test is identified for COMP-01. Prefer adding coverage to the lifecycle fixture only if it can build real stored transactions; otherwise add the smallest existing account test target rather than inventing a framework. |

## Metadata

**Analog search scope:** `src/blockchain`, `src/account`, `src/crdt`, `test/src/blockchain`  
**Files scanned:** 16 source/test/config files plus Phase 10 context, research, and validation artifacts  
**Pattern extraction date:** 2026-08-21
