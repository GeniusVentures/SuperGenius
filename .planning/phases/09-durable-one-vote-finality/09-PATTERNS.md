# Phase 9: Durable One-Vote Finality - Pattern Map

**Mapped:** 2026-08-20  
**Files analyzed:** 4 expected modifications (plus generated protobuf outputs)  
**Analogs found:** 4 / 4

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/blockchain/Consensus.hpp` | controller / model | event-driven, timed state machine | its existing `SlotState`, `PendingProposalEntry`, timer declarations | exact |
| `src/blockchain/Consensus.cpp` | controller / service | event-driven, local durable I/O, pub-sub | its timer, proposal continuation, certificate callback, and vote submission paths | exact |
| `src/blockchain/impl/proto/Consensus.proto` | model / serialization config | transform, file-I/O | existing consensus message schemas | exact |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | integration / lifecycle test | event-driven, file-I/O | its friend accessor and CRDT fixture | exact |

`test/src/blockchain/consensus_slot_key_test.cpp` is a regression-only companion: extend it only if Phase 9 changes a generic ranking/slot invariant. Do not alter `MintTransactionV2::GetSlotID()`. `test/src/blockchain/CMakeLists.txt` already defines the lifecycle target, so no CMake change is expected.

## Pattern Assignments

### `src/blockchain/Consensus.hpp` (controller/model, event-driven + timed state)

**Analog:** Existing private local-state declarations in `src/blockchain/Consensus.hpp`.

Add the active-vote record/config/helper declarations next to the existing `SlotState`, pending lifecycle, timer, and certificate-ingress declarations. Keep the durable-record authority private to `ConsensusManager`; tests obtain narrow access through the existing friend seam.

**State and friend-seam pattern** (`src/blockchain/Consensus.hpp:540-603`):

```cpp
friend class ConsensusPendingLifecycleTestAccess;

struct SlotState
{
    std::string                     best_proposal_id;
    std::string                     best_tx_hash;
    std::unordered_set<std::string> voted_proposal_ids;
};
```

Replace or constrain `voted_proposal_ids` as the authority with an explicit per-slot window/frozen/active-lock state. The slot needs a `steady_clock` two-second contention deadline, but the record needs a separate persisted Unix-millisecond deadline; never serialize the monotonic deadline.

**Timer/certificate helper placement** (`src/blockchain/Consensus.hpp:722-805`):

```cpp
void ClearProposalSlot( const Proposal &proposal );
void ContinueProposalAfterSubject( const Proposal &proposal );
void ProcessDuePendingRetries();
bool RegisterCertificateFilter();
void CertificateReceived( crdt::CRDTCallbackManager::NewDataPair new_data,
                          const std::string &cid );
void RecoverPendingCertificateWork();
```

Mirror this grouping for `ProcessDueVoteWork`, recovery/load/validate/persist-or-load-exact helpers, and a release helper invoked only by the durable certificate-acceptance path. Keep `ClearProposalSlot` as volatile cleanup rather than making it implicitly delete durable state.

**Existing local-state ownership** (`src/blockchain/Consensus.hpp:887-909`):

```cpp
std::shared_ptr<crdt::GlobalDB> db_;
std::unordered_map<std::string, ProposalState> proposals_;
std::unordered_map<std::string, SlotState>     slot_states_;
```

Use `db_->GetDataStore()` only inside the consensus durability helpers, and protect the get/compare/put decision and related `slot_states_` mutations with `proposals_mutex_`, matching the current state ownership.

---

### `src/blockchain/Consensus.cpp` (controller/service, event-driven + local durable I/O)

**Analog:** `ContinueProposalAfterSubject`, `StartRoundTimer`, `SubmitVote`, `CertificateReceived`, `HandleCertificate`, `ClearProposalSlot`, and `IsBetterProposal` in the same file.

This is the primary Phase 9 implementation file. Preserve the current generic slot-key and comparator implementations; move authorization to self-vote from immediate candidate admission to deadline freeze and persist-before-publish.

**Timer lifecycle and startup-recovery pattern** (`src/blockchain/Consensus.cpp:80-102`, `141-203`):

```cpp
instance->StartRoundTimer();
if ( !instance->RegisterCertificateFilter() )
{
    ConsensusManagerLogger()->error( "{}: Failed to register certificate filter", __func__ );
}
instance->RecoverPendingCertificateWork();

// Timer loop work after releasing timer_mutex_.
self->ExpirePendingProposals();
self->ProcessDuePendingRetries();
self->RecoverPendingCertificateWork();
```

Recover active-vote records during `New` before normal proposal processing, and call one due-work helper from this existing timer thread. Do not create a second thread. The timer has a 500 ms minimum interval (`156-174`), so eligibility must also check the recorded monotonic deadline during admission; the timer alone cannot define the 2-second boundary.

**Current arbitration seam to replace** (`src/blockchain/Consensus.cpp:577-689`):

```cpp
std::lock_guard lock( proposals_mutex_ );
auto &slot_state = slot_states_[slot_key];
// Current code updates best proposal immediately via IsBetterProposal(...).
if ( slot_state.best_proposal_id == proposal.proposal_id() &&
     slot_state.voted_proposal_ids.find( proposal.proposal_id() ) == slot_state.voted_proposal_ids.end() )
{
    slot_state.voted_proposal_ids.insert( proposal.proposal_id() );
    should_vote = true;
}

if ( should_vote )
{
    auto vote_result = CreateVote( proposal.proposal_id(), account_address_, true, signer_ );
    if ( vote_result.has_value() )
    {
        (void) SubmitVote( vote_result.value() );
    }
}
```

Copy the mutex/state shape, but change it to: admit only already-approved candidates before the fixed deadline; freeze at deadline; choose exactly one with the unchanged comparator; create the vote locally; serialize and synchronously persist/load the exact record; only then install a usable local lock, self-handle/publish, and schedule exact replay. On serialization/get/put/validation failure, leave no usable memory vote state and do not call `SubmitVote`.

**Winner-comparison invariant** (`src/blockchain/Consensus.cpp:2613-2633`):

```cpp
if ( cand_hash == curr_hash )
{
    return candidate.proposal_id() < current.proposal_id();
}
return BestHash( curr_hash, cand_hash ) == cand_hash;
```

Do not add a Mint-specific branch or modify this ordering. Its generic fallback is already proposal-ID order (`2632`).

**Outbound boundary** (`src/blockchain/Consensus.cpp:1508-1531`):

```cpp
ConsensusMessage message;
*message.mutable_vote() = vote;
auto result = Publish( message );
if ( result.has_error() )
{
    return result;
}
if ( self_handle )
{
    HandleVote( vote );
}
```

All first sends and retries must pass the stored parsed vote through this boundary. `Publish` only serializes then calls the void pubsub API and returns success (`206-221`); no acknowledgement exists. Retry exact stored bytes/vote on a bounded cadence while the absolute deadline is in the future, rather than inferring delivery.

**Direct local RocksDB error propagation** (`src/account/TransactionManager.cpp:3386-3409`):

```cpp
auto datastore = globaldb_m ? globaldb_m->GetDataStore() : nullptr;
if ( !datastore )
{
    return outcome::failure( std::errc::bad_file_descriptor );
}

crdt::GlobalDB::Buffer key_buffer;
key_buffer.put( key );
crdt::GlobalDB::Buffer value_buffer;
value_buffer.put( cid );
auto put_result = datastore->put( key_buffer, value_buffer );
if ( put_result.has_error() )
{
    return outcome::failure( put_result.error() );
}
```

Follow this direct datastore style using the generic `"/consensus/vote/" + slot` prefix; do not call `GlobalDB::Put` or `Remove`, which are CRDT operations. `rocksdb::create` sets synchronous write options (`src/storage/rocksdb/rocksdb.cpp:22-64`), and `put`/`remove` return `outcome` failures (`223-248`). Use `get` to distinguish an existing idempotent exact record from a different record; `get` returns an error for absent keys (`112-128`). Use a prefix query for restart recovery (`131-154`).

**Protobuf parse + fail-closed pattern** (`src/blockchain/Consensus.cpp:2073-2117`):

```cpp
Certificate certificate;
if ( !certificate.ParseFromArray( value.data(), value.size() ) )
{
    ConsensusManagerLogger()->error( "{}: invalid certificate payload key={}", __func__, key );
    return;
}

auto certificate_check = ValidateCertificate( certificate );
if ( certificate_check == Check::Reject )
{
    return;
}
if ( certificate_check == Check::Stalled )
{
    certificate_work_journal_->MarkStalled( key );
    return;
}
```

Use the same style to parse and validate stored `ActiveVoteRecord`: recompute `GetSlotKey(proposal)`, ensure it equals the record slot, ensure vote/proposal IDs and local voter/approval fields cohere, and verify the stored vote signature using `VoteSigningBytes`/`GeniusAccount::VerifySignature`. Corrupt/mismatched records are not usable for reannouncement.

**Durable certificate ingress boundary** (`src/blockchain/Consensus.cpp:2119-2157`):

```cpp
registry_->OnFinalizedCertificate( certificate );
// ... subject handler result is checked ...
(void) certificate_work_journal_->MarkDone( key );
(void) WakePendingDependency( PendingDependencyKey::Certificate( subject_hash.value() ) );
```

First confirm exact callback ordering against `CrdtDatastore` before implementation. The release helper belongs after the proven durable/accepted boundary and must compare `GetSlotKey(certificate.proposal())`, not proposal ID or the legacy certificate key. Do not delete in PubSub `HandleCertificate`, which merely validates then calls volatile cleanup (`2434-2469`), nor inside `ClearProposalSlot`, which erases process memory (`2540-2587`). A same-slot certificate with another winning proposal must release; rejected, stalled, malformed, or other-slot certificates must not.

---

### `src/blockchain/impl/proto/Consensus.proto` (model/serialization config, transform)

**Analog:** Existing `ConsensusProposal`, `ConsensusVote`, and `ConsensusCertificate` schema definitions (`src/blockchain/impl/proto/Consensus.proto:76-111`).

Add a standalone local `ActiveVoteRecord` message in the `sgns` package, rather than modifying network `ConsensusMessage`:

```proto
message ConsensusProposal {
  string proposal_id = 1;
  string proposer_id = 2;
  uint64 timestamp = 3;
  string registry_cid = 4;
  uint64 registry_epoch = 5;
  ConsensusSubject subject = 6;
  bytes signature = 7;
}

message ConsensusVote {
  string proposal_id = 1;
  string voter_id = 2;
  bool approve = 3;
  uint64 timestamp = 4;
  bytes signature = 5;
}
```

The record must carry `canonical_slot`, complete serialized proposal bytes, exact serialized signed vote bytes, and `acceptance_deadline_ms`. This preserves the exact signed wire vote across restart and keeps the record local-only. Do not add it to the `ConsensusMessage` oneof (`107-113`) and do not hand-roll binary framing.

**Serialization error pattern** (`src/blockchain/ConsensusAuth.hpp:54-64`):

```cpp
ConsensusVote copy = vote;
copy.clear_signature();
std::string serialized;
if ( !copy.SerializeToString( &serialized ) )
{
    return outcome::failure( std::errc::invalid_argument );
}
```

For the record, retain the full signed vote bytes, not signing bytes or a reconstructed `CreateVote` output. Parse with `ParseFromArray` and preserve byte equality for idempotence comparison.

---

### `test/src/blockchain/consensus_pending_lifecycle_test.cpp` (integration/lifecycle test, event-driven + local file I/O)

**Analog:** Existing friend access and real CRDT/RocksDB fixture in the same test file.

Extend this one focused target with a minimal test-access surface for deterministic contention deadlines, active-vote records, due work/recovery, and outbound announcement observations. Do not sleep for the two-second window and do not introduce a separate RocksDB fixture unless direct wrapper coverage is needed.

**Private-access pattern** (`test/src/blockchain/consensus_pending_lifecycle_test.cpp:36-262`):

```cpp
static void ProcessDuePendingRetries( const std::shared_ptr<ConsensusManager> &manager )
{
    manager->ProcessDuePendingRetries();
}

static void ContinueProposalAfterSubject( const std::shared_ptr<ConsensusManager> &manager,
                                          const ConsensusManager::Proposal &proposal )
{
    manager->ContinueProposalAfterSubject( proposal );
}
```

Add similarly narrow accessors: force window/deadline due, invoke active-vote due/recovery work, read/write a record through the fixture datastore for corrupt/collision cases, and inspect local lock/announcement state. Keep tests outside production code paths.

**Fixture and in-memory signing-storage pattern** (`test/src/blockchain/consensus_pending_lifecycle_test.cpp:288-400`):

```cpp
class ConsensusPendingLifecycleTest : public test::CRDTFixture
{
protected:
    void SetUp() override
    {
        sgns::GeniusAccount::SetSecureStorageFactory(
            []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
            { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    }

    std::shared_ptr<sgns::ConsensusManager> MakeSigningManager(
        const std::shared_ptr<sgns::ValidatorRegistry> &registry,
        const std::shared_ptr<sgns::GeniusAccount> &account )
    {
        return sgns::ConsensusManager::New(
            registry, db_, pubs_,
            [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
            account->GetAddress() );
    }
};
```

Keep `MemorySecureStorage`; it avoids OS keychain state while generating verifiable votes. Build valid signing accounts for record-recovery/signature tests, and call the existing `Close` accessor in each test that constructs a manager.

**Certificate-ingress test shape** (`test/src/blockchain/consensus_pending_lifecycle_test.cpp:560-711`):

```cpp
auto filter_result = ConsensusPendingLifecycleTestAccess::FilterCertificate( manager, element );
ASSERT_TRUE( filter_result.has_value() );
EXPECT_TRUE( filter_result->empty() );

ConsensusPendingLifecycleTestAccess::CertificateReceived(
    manager, { element.key(), std::move( serialized_buffer ) } );
```

Use this direct filter/callback separation to prove that malformed/rejected/stalled/other-slot events retain an active record, and a durably accepted same-slot certificate deletes it—even if the certificate proposal differs from the local voted proposal.

## Shared Patterns

### Local storage, not CRDT replication

**Sources:** `src/crdt/globaldb/globaldb.hpp:91-119,158`; `src/storage/rocksdb/rocksdb.cpp:22-64,112-154,223-248`.

`GlobalDB::GetDataStore()` exposes `storage::rocksdb`; direct `get`/`put`/`remove` are the local durability API. `GlobalDB::Put`/`Remove` are explicitly CRDT writes and must not hold a validator's private one-vote lock. Writes from the path factory are synchronous.

### Error handling

**Sources:** `src/account/TransactionManager.cpp:3386-3409`; `src/blockchain/Consensus.cpp:2044-2067`.

Return/propagate `outcome` errors, log locally with the manager logger, and fail closed. A missing or corrupt record must never authorize reconstructed vote state; a write error means no broadcast, no self-handle, and no active in-memory lock.

### Serialization and signature preservation

**Sources:** `src/blockchain/ConsensusAuth.hpp:32-85`; `src/blockchain/Consensus.cpp:1508-1531`.

Use protobuf `SerializeToString`/`ParseFromArray`. Save and replay full stored `ConsensusVote` bytes, with its original timestamp and signature. Never call `CreateVote` during recovery because it signs a replacement vote.

### Timer and locking

**Sources:** `src/blockchain/Consensus.cpp:141-203,577-660`; `src/blockchain/Consensus.hpp:904-905`.

Use the existing timer and `proposals_mutex_`. Admission and freeze must enforce the monotonic deadline while holding the state mutex; every durable record mutation and in-memory lock transition must have a clearly ordered persist-before-publish flow.

### Certificate-release boundary

**Sources:** `src/blockchain/Consensus.cpp:2010-2037,2073-2157,2434-2469,2540-2587`.

Certificate PubSub parsing and volatile cleanup are not sufficient. Record deletion is permitted only after the designated CRDT callback has passed certificate validation and the planner has confirmed the exact durability/acceptance ordering. Release by canonical slot, not legacy `/cert/<subject-hash>` identity.

## No Analog Found

| File / concern | Role | Data Flow | Reason |
|---|---|---|---|
| `ActiveVoteRecord` local envelope fields | protobuf model | local file-I/O | No existing record stores an exact signed vote plus a canonical slot/deadline; use the adjacent consensus schemas. |
| RocksDB fault injection / publish observation | test seam | event-driven | Concrete RocksDB and PubSub APIs lack an existing controllable failure/ack hook; add the narrowest `ConsensusManager` test-access seam, retaining production `GetDataStore()` and `SubmitVote` paths. |
| Proven CRDT write-to-callback ordering | integration boundary | event-driven | `ConsensusManager` registers the callback but does not prove datastore callback ordering; inspect `CrdtDatastore` before choosing the exact release line. |

## Metadata

**Analog search scope:** `src/blockchain`, `src/crdt/globaldb`, `src/storage/rocksdb`, `src/account`, `src/local_secure_storage`, `test/src/blockchain`, `test/src/storage/rocksdb`  
**Files scanned:** 13  
**Pattern extraction date:** 2026-08-20
