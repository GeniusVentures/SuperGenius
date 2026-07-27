# Phase 10 Pattern Mapping: Durable Vote Lock and Finalization State Machine

**Mapped:** 2026-07-27  
**Inputs:** `10-CONTEXT.md`, `10-RESEARCH.md`, and the current repository  
**Purpose:** Give the planner concrete file targets, data flow, and existing code patterns. This is a map, not an implementation prescription; names of new local-store files and protobuf messages remain discretionary.

## Architectural Through-line

Phase 10 should introduce one node-local, direct-RocksDB consensus state store and make `ConsensusManager` the only coordinator of its records. The replicated `/cert/v2/slot/<slot>` certificate remains finality. Local vote records, processing markers, and conflict evidence must never use `GlobalDB::Put()`, because that API creates CRDT state and broadcasts it.

The intended flow is:

```text
network_config.json
  -> GeniusNode resolves ConsensusConfig
  -> Blockchain::New(config)
  -> ConsensusManager::New(config)
       -> acquire GlobalDB::GetDataStore()
       -> construct/scan strict local state store
       -> validate vote/process/conflict records and authoritative certificates
       -> restore slot locks and SafetyViolation flags
       -> only then subscribe/register filters/start timer
       -> replay stored envelope bytes after transport is ready

proposal ingress
  -> validate subject/proposal
  -> first valid candidate fixes one steady_clock deadline
  -> comparator may replace best before deadline, never extend deadline
  -> deadline atomically reserves Signing generation
  -> CreateVote once
  -> serialize signed vote and outbound ConsensusMessage once
  -> durable direct-RocksDB vote record
  -> raw-publish the stored envelope bytes

local / pubsub / CRDT / recovery certificate ingress
  -> structural canonical normalization
  -> FinalizeSlot(certificate, source)
       -> empty slot: live first-observation time validation
       -> persist authoritative slot certificate + tx index through GlobalDB::Put(pair)
       -> create/repair local Pending processing marker bound to certificate digest + winner
       -> invoke one handler attempt
       -> durable Complete marker
       -> cleanup transient candidates/pending votes
       -> occupied different slot certificate: record conflict evidence + SafetyViolation
```

Two atomicity domains must remain explicit:

- **Replicated finality:** the existing slot certificate and tx index are one CRDT delta via `GlobalDB::Put(vector<DataPair>)`.
- **Node-local progress:** vote locks, processing markers, and conflict records are direct RocksDB operations. They cannot be atomically committed with the CRDT delta, so startup repairs certificate-without-marker crashes. The certificate is authoritative; the marker is never a second finality record.

## Likely File Surface

### New files

| Likely file | Role | Closest repository analog | Data flow / pattern to carry forward |
|---|---|---|---|
| `src/blockchain/ConsensusStateStore.hpp` | Typed API for local vote, processing, conflict, and slot-safety records; typed read/write/query failures; strict startup scan. | `src/crdt/globaldb/crdt_work_journal.hpp` for local namespacing/locking, but `UTXOManager` for strict errors and exact-record idempotency. | Construct from `db_->GetDataStore()`. Expose versioned records and typed results, not `void`, `bool`, or `optional` that conflate absence, parse failure, and I/O failure. Serialize store mutations under an internal mutex and use a RocksDB batch for multi-record transitions such as conflict evidence + SafetyViolation or durable retirement + next-generation eligibility. |
| `src/blockchain/ConsensusStateStore.cpp` | Direct RocksDB key construction, strict parse/key cross-checking, startup enumeration, atomic batches, digest-pair dedup updates. | `src/crdt/impl/crdt_work_journal.cpp`, `src/account/UTXOManager.cpp:941`, and `src/storage/rocksdb/rocksdb_batch.*`. | Use `/consensus/local/v2/...` prefixes; query by prefix; reject malformed keys, unknown versions/states, contradictory records, and query failures. Do not skip bad entries. Exact duplicate writes are idempotent; mismatched occupied identities return integrity/conflict errors. |
| `src/blockchain/impl/proto/ConsensusLocalState.proto` (or equivalent strict encoding colocated with the store) | Versioned local record schema. | `SGTransaction::BridgeApplicationRecord` constructed in `UTXOManager::ApplyMintEffectsAtomically()`. | Vote record contains slot, proposal, validator, exact signed vote bytes, exact outbound envelope bytes, signed proposal/validation context, registry CID/epoch, generation, state, timestamps, and acceptance horizon. Processing record binds slot + canonical certificate digest + proposal/winner and keeps `Pending/Processing/Complete`. Conflict record keeps only identities/digests/source/timestamps/count, never duplicate certificate bytes. |
| `test/src/blockchain/consensus_vote_journal_test.cpp` | Fast direct-RocksDB and manager-restart coverage for VOTE-01/02/03/07 and startup fail-closed ordering. | Fixture construction in `consensus_certificate_store_test.cpp`; friend access in `consensus_pending_lifecycle_test.cpp`; persistent database restart style in RocksDB tests. | Verify exact stored vote/envelope bytes, write-before-publish, one signature, corrupt-record startup refusal before any side effect, exact replay after restart, retirement boundaries, and same database path reopening. |
| `test/src/blockchain/consensus_finalization_test.cpp` | Unified finalization, processing recovery, source convergence, conflict, and timer/finalization race tests. | `consensus_certificate_store_test.cpp` CRDT fixture plus `UTXOManagerTestAccess` fault injection. | Feed local/pubsub/CRDT/recovery concurrently; count one handler attempt; block with barriers; inject crashes/failures at persistence boundaries; assert finality remains authoritative, marker remains pending, cleanup waits, conflicts dedup, metric increments, and publication does not occur. |

The protobuf filename is a recommendation, not a locked decision. Extending `Consensus.proto` would couple replicated wire messages to private local state; a separate local-state proto is the cleaner repository boundary.

### Existing files likely modified

| File | Role in Phase 10 | Closest existing analog / relevant area | Expected data-flow change |
|---|---|---|---|
| `src/blockchain/Consensus.hpp` | Define `ConsensusConfig`, explicit slot lifecycle, delivery/finalization result enums, store ownership, timer/deadline state, conflict counter, and private test seams. | Current `ProposalState`, `SlotState`, `CertificateNormalization`, `CertificateStoreError`, and friend-only reader/publish observer. | Replace `voted_proposal_ids` as the safety boundary with restored durable vote state. Add `FinalizeSlot`, raw-envelope publish, structural vs live validation, recovery, conflict recording, and deadline processing declarations. |
| `src/blockchain/Consensus.cpp` | Main integration point. | `ConsensusManager::New`, `ContinueProposalAfterSubject`, `SubmitVote`, `SubmitCertificate`, `FilterCertificateDelta`, `CertificateReceived`, `NormalizeCertificate`, `HandleVote`, `HandleCertificate`, `ProcessCertificates`, `RecoverPendingCertificateWork`, and `IsBetterProposal`. | Restore before side effects; freeze/select/sign/persist/publish; remove local-best gates from certificate acceptance and peer-vote tally; route every certificate path through one finalizer; persist finality before handler/cleanup; retry pending work; record CRDT conflicts inside the pre-merge filter. |
| `src/blockchain/impl/CMakeLists.txt` | Generate/link the local-state protobuf if used. | Existing `add_proto_library(ConsensusProto proto/Consensus.proto)` and target link stanza. | Add the local proto library and link it into `blockchain_genesis` (directly or through the store target). |
| `src/blockchain/Blockchain.hpp` | Carry `ConsensusConfig` into construction before the manager starts. | Existing `Blockchain::New(global_db, account, pubsub, callback)`. | Add a value parameter with safe defaults for direct test callers; avoid a post-`New()` setter. |
| `src/blockchain/impl/Blockchain.cpp` | Forward configuration to `ConsensusManager::New`; propagate factory failure. | Existing manager construction around line 183. | Pass the config in the factory call. If manager startup fails on local-state integrity, return `nullptr`/fail blockchain initialization without later transaction initialization. |
| `src/account/GeniusNode.hpp` | Own resolved consensus configuration for the node lifetime. | `BootstrapReconnectConfig` and `crdt_backup_config_` value objects. | Add a `ConsensusConfig` member (or a node-local mirror converted at the boundary), defaulting the selection window to 500 ms. |
| `src/account/GeniusNode.cpp` | Parse `consensus_vote_selection_window_ms` and forward it during `INITIALIZING_BLOCKCHAIN`. | Numeric `port_seed` parsing in `InitNetwork()` and the `Blockchain::New(...)` call in the state machine. | Accept only a positive integer in a bounded range; warn and retain the compiled default for missing/wrong/zero/negative/too-large values. Resolution must finish before blockchain construction. This file is currently user-modified, so implementation must preserve and merge with the existing dirty change. |
| `test/src/blockchain/CMakeLists.txt` | Register Wave 0 tests. | Active `consensus_pending_lifecycle_test` and `consensus_certificate_store_test` target wiring. | Add `consensus_vote_journal_test` and `consensus_finalization_test` with the same `blockchain_genesis`, `genius_node_test`, `rapidjson`, and `base_crdt_test` dependencies where required. Keep the old commented `consensus_certificate_test` disabled. |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | Extend deterministic candidate-window and cleanup lifecycle coverage. | Existing `ConsensusPendingLifecycleTestAccess` directly invokes private deadline/retry operations. | Replace expectations of immediate self-voting with fixed-window selection; expose `ProcessCandidateDeadlines(now)` through the friend accessor; verify deadline immutability, late-candidate non-revote, and cleanup only after durable complete. |
| `test/src/blockchain/consensus_certificate_store_test.cpp` | Preserve Phase 9 storage compatibility while extending conflict-filter behavior. | Current `ConsensusManagerTestAccess` certificate reader and publish observer. | Assert exact replay still succeeds, conflicts preserve the original pair, pre-merge CRDT conflict produces local evidence, and malformed inputs do not create safety evidence. Existing Phase 9 lookup/index tests remain regression coverage. |
| `test/src/account/network_config_precedence_test.cpp` | Parser/default/propagation coverage for the selection window. | Current `AutoDhtConfigDriven` and `PortSeedConfigDriven` scenes. | Add missing/valid/invalid key cases and inspect the resolved config via a narrow friend/accessor rather than starting timing-sensitive consensus behavior. |
| `test/src/account/transaction_manager_pending_lifecycle_test.cpp` | Verify handler registration wakes pending finalized work and exact winner retry remains idempotent. | Existing transaction pending lifecycle fixture and certificate handler registration. | Cover certificate finalized before `TransactionManager::New()`, then handler registration causing processing without marking missing-handler work complete. Confirm retry of mint-v2 observes `AlreadyApplied` as success. |

### Conditional modification, but mandatory verification

| File | Why it may need a small change | Existing pattern to preserve |
|---|---|---|
| `src/account/TransactionManager.cpp` / `.hpp` | The consensus finalizer will call the existing handler differently (possibly immediately on registration/recovery). If current code treats an already-applied exact winner as failure anywhere above `ApplyConfirmedMintV2`, adapt that narrow path. Avoid moving finality responsibility into TransactionManager. | `OnConsensusCertificate()` receives the certificate-selected tx hash, and mint-v2 ultimately calls `ApplyMintEffectsAtomically()`. The handler must stay idempotent because a crash may happen after UTXO effects and before the consensus Complete marker. |
| `src/account/UTXOManager.cpp` / `.hpp` | No redesign is indicated by the research. Modify only if tests reveal the winning mint-v2 retry does not propagate `AlreadyApplied` successfully or if a friend-only observer is needed. | Keep the exact application record, persistence mutex, candidate-copy construction, one RocksDB batch, and fail-closed mismatch. Do not add a second non-atomic UTXO application path. |
| `test/src/account/utxo_manager_test.cpp` | Extend only as needed to prove finalizer crash replay hits the exact-record idempotent path. | Existing `UTXOManagerTestAccess` and fault stages provide the right private-only test style. |

`src/crdt/impl/crdt_data_filter.cpp` is not expected to own Phase 10 semantics. The certificate conflict is rejected by `ConsensusManager::FilterCertificateDelta()` before normal callback delivery; therefore evidence must be recorded in that consensus-owned filter callback, not in the generic CRDT filter. Similarly, `src/crdt/globaldb/crdt_work_journal.*` should remain an analog, not be expanded into the consensus store unless its silent-error API is comprehensively replaced.

## Concrete Repository Patterns

### 1. Direct RocksDB local journal: copy the shape, harden the contract

`GlobalDB` already exposes the underlying local store:

```cpp
std::shared_ptr<RocksDB> GetDataStore();
```

The current CRDT work journal shows the local-prefix and lock shape:

```cpp
static constexpr std::string_view NAMESPACE_PREFIX = "/crdt/work/";

std::lock_guard<std::mutex> lock( mutex_ );
base::Buffer key_buf;
key_buf.put( BuildStorageKey( entry.key ) );
base::Buffer value_buf;
value_buf.put( SerializeEntry( entry ) );
return datastore_->put( key_buf, value_buf ).has_value();
```

Do **not** copy its error semantics. `ListUnfinished()` currently returns an empty vector on query failure, `GetEntryUnlocked()` returns `nullopt` for both not-found and read/parse errors, and malformed entries are skipped:

```cpp
auto result = datastore_->query( prefix_buf );
if ( result.has_error() )
{
    return out;
}
...
auto parsed = DeserializeEntry( raw_key.toString(), raw_value.toString() );
if ( parsed.has_value() )
{
    out.push_back( std::move( parsed.value() ) );
}
```

The Phase 10 store needs `outcome::result<T>` throughout. Only `DatabaseError::NOT_FOUND` means absence. Query failure, truncated payload, unknown version/state, malformed key, key/value mismatch, signature mismatch, validator mismatch, slot/proposal mismatch, or contradictory active records must fail the startup scan.

Recommended local namespaces, consistent with the research:

```text
/consensus/local/v2/vote/<validator-id-hash>/<slot-id>
/consensus/local/v2/process/<slot-id>
/consensus/local/v2/conflict/<slot-id>/<lower-digest>:<higher-digest>
/consensus/local/v2/safety/<slot-id>             # optional separate flag
```

Use a RocksDB `batch()` for transitions that must be indivisible locally. The repository batch interface is:

```cpp
auto batch = db->batch();
BOOST_OUTCOME_TRY( batch->put( key, value ) );
BOOST_OUTCOME_TRY( batch->remove( old_key ) );
BOOST_OUTCOME_TRY( batch->commit() );
```

Concrete batch boundaries:

- first vote record is one durable put before publication;
- retirement state (or retired record) must commit before the slot becomes eligible for a new generation;
- processing state/digest updates are one record replacement;
- repeated conflict observation updates evidence count/last-seen/sources and sets the durable safety flag in one batch;
- Complete marker must commit before transient slot cleanup.

### 2. Exact vote serialization and raw replay

The vote signature commits to every field except `signature`:

```cpp
inline outcome::result<std::vector<uint8_t>> VoteSigningBytes( const ConsensusVote &vote )
{
    ConsensusVote copy = vote;
    copy.clear_signature();
    std::string serialized;
    if ( !copy.SerializeToString( &serialized ) )
    {
        return outcome::failure( std::errc::invalid_argument );
    }
    return std::vector<uint8_t>( serialized.begin(), serialized.end() );
}
```

`CreateVote()` samples both time and endpoint hashes before signing:

```cpp
vote.set_timestamp( duration_cast<milliseconds>( system_clock::now().time_since_epoch() ).count() );
if ( slot_hash_populator_ )
{
    slot_hash_populator_( vote );
}
auto signing_bytes = VoteSigningBytes( vote );
BOOST_OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
vote.set_signature( signature.data(), signature.size() );
```

Therefore recovery must never call `CreateVote()`. Persist both:

1. exact serialized signed `ConsensusVote` bytes, for validation and inspection;
2. exact serialized `ConsensusMessage` envelope bytes, for publication and restart replay.

Current `SubmitVote()` serializes a fresh envelope and publishes before any persistence:

```cpp
ConsensusMessage message;
*message.mutable_vote() = vote;
auto result = Publish( message );
if ( self_handle )
{
    HandleVote( vote );
}
```

Phase 10 should introduce a raw helper whose input is the stored envelope bytes:

```text
CreateVote once -> build envelope once -> serialize once
  -> state_store.PutVote(record)
  -> PublishSerialized(record.envelope_bytes)
  -> HandleVote(parsed exact stored vote) as local tally input
```

On restart, validate the stored vote and envelope agree byte-for-byte and logically (envelope contains exactly that vote), then publish the stored bytes without protobuf reconstruction. Publication failure retains the same record for retry and never invokes the signer again.

### 3. Candidate selection and manager timer ownership

The comparator already implements the required deterministic ordering:

```cpp
if ( candidate_nonce.has_value() && current_nonce.has_value() )
{
    const auto &cand_hash = candidate_nonce.value().tx_hash();
    const auto &curr_hash = current_nonce.value().tx_hash();
    if ( cand_hash == curr_hash )
    {
        return candidate.proposal_id() < current.proposal_id();
    }
    return BestHash( curr_hash, cand_hash ) == cand_hash;
}
return candidate.proposal_id() < current.proposal_id();
```

Keep this as the sole ranking rule in `Selecting`. The first admitted valid proposal sets `selection_deadline = steady_clock::now() + config.vote_selection_window`; later candidates may update `best_proposal_id` but never the deadline.

Current timer ownership is safe and reusable: it promotes a `weak_ptr`, waits on a manager-owned condition variable, checks `stop_timer_`, and `Close()` joins it. Extend its wake calculation to the nearest selection deadline. Do not retain the current hard 500 ms polling floor for deadline processing:

```cpp
auto interval = self->round_duration_ / 2;
if ( interval < min_interval )
{
    interval = min_interval; // currently 500 ms; too coarse for a 500 ms vote window
}
self->timer_cv_.wait_for( lock, interval, ... );
```

The timer should wait until the earliest of round work, pending retry, candidate deadline, or shutdown. Tests should invoke deadline processing with an explicit `steady_clock::time_point`, not sleep.

Use a state/generation reservation around signing:

```text
under proposals/state mutex:
  Selecting -> Signing
  freeze chosen proposal and generation
outside mutex:
  sign and serialize
under mutex again:
  confirm same slot/generation still Signing and no finality/safety violation
  persist exact vote
after durable success:
  Voted, then raw publish
```

If finalization wins before the recheck, discard the newly computed signature without publication. If the durable write fails, preserve the same `Signing` generation/candidate for retry; never select/sign a competitor.

### 4. Startup fail-closed ordering

Current startup performs compatibility validation, then immediately creates side effects:

```cpp
instance->certificate_work_journal_ = instance->db_->GetWorkJournal();
if ( !instance->HasCompatibleCertificateState() )
{
    return nullptr;
}
instance->consensus_subs_future_ = instance->pubsub_->Subscribe( ... );
instance->StartRoundTimer();
instance->RegisterCertificateFilter();
instance->RecoverPendingCertificateWork();
```

Required ordering:

```text
validate constructor dependencies
acquire direct RocksDB handle and construct local store
scan and strictly decode every local consensus record
enumerate authoritative slot certificates
cross-check vote/process/conflict/safety records against keys, validator identity, signatures, and certificates
restore in-memory slot states
repair only safe recoverable gaps (authoritative cert with missing Pending marker)
FAIL HERE on unreadable/inconsistent state
subscribe to consensus pubsub
register certificate delta/element/callback hooks
start owned timer
raw-republish live stored vote envelopes
process authoritative pending winners when handlers exist
```

The factory should return `nullptr` on any pre-side-effect failure. Friend-only startup counters should prove zero subscribe, timer, filter/listener, and publish calls. A missing handler is not corruption and must leave processing pending; `RegisterCertificateHandler()` should notify recovery after it installs the handler.

### 5. Unified finalization and certificate storage

The Phase 9 authoritative persistence pattern is already correct:

```cpp
crdt::HierarchicalKey  cert_key( slot_key_string );
crdt::GlobalDB::Buffer cert_value;
cert_value.put( serialized );
crdt::HierarchicalKey  index_key( index_key_string );
crdt::GlobalDB::Buffer index_value;
index_value.put( slot_id );

auto cert_put = db_->Put(
    { { std::move( cert_key ), std::move( cert_value ) },
      { std::move( index_key ), std::move( index_value ) } },
    { consensus_datastore_topic_ } );
```

Also preserve Phase 9 replay behavior: exact occupied slot bytes require a matching tx index and return success; different bytes return `CertificateStoreError::Conflict` without overwrite. Move this logic behind `FinalizeSlot(certificate, DeliverySource)` rather than duplicating it among adapters.

Current paths diverge and must become thin adapters:

- `SubmitCertificate()` stores/publishes but does not directly apply;
- `HandleCertificate()` validates local best and clears immediately;
- `CertificateReceived()` invokes the handler and currently marks missing-handler work done;
- `ProcessCertificates()` calls `SubmitCertificate()` and then unconditionally clears.

Replace these rules with:

```text
SubmitCertificate      -> FinalizeSlot(Local) -> publish only accepted canonical winner
HandleCertificate      -> FinalizeSlot(PubSub), no cleanup
CertificateReceived    -> FinalizeSlot(CRDT), retire legacy journal only after local Complete
recovery               -> FinalizeSlot(Recovery) for authoritative stored winner
ProcessCertificates    -> FinalizeSlot(Local), no caller-owned cleanup
```

`FinalizeSlot` should use one finalization/process gate and typed outcomes such as `Applied`, `PendingApplication`, `AlreadyFinalized`, `Conflict`, `Invalid`, and `StorageFailure`. Its occupied-slot check uses timeless structural normalization, while first acceptance into an empty slot additionally uses live timestamp validity. An exact stored certificate remains readable/applicable after its live horizon.

Remove certificate dependence on local preference. The following current check contradicts D-13/VOTE-06 and should disappear from certificate acceptance:

```cpp
if ( slot_it != slot_states_.end() &&
     slot_it->second.best_proposal_id != certificate.proposal_id() )
{
    return false;
}
```

Likewise remove the same local-best gate from `HandleVote()` so an aggregator can collect valid peer votes for a proposal it did not rank locally.

### 6. Processing marker and UTXO atomic application

`UTXOManager::ApplyMintEffectsAtomically()` is the strongest local persistence analog. It:

1. builds a versioned exact application record bound to the winning transaction;
2. acquires `persistence_mutex_` then `utxos_mutex_` in documented order;
3. reads an existing record and treats only exact bytes as `AlreadyApplied`;
4. rejects any mismatch with `state_not_recoverable`;
5. builds candidate in-memory maps;
6. writes all UTXO mutations plus the application record in one RocksDB batch;
7. swaps in-memory state only after commit.

The critical existing excerpt is:

```cpp
auto existing_application = bridge_application_reader_( db, application_key );
if ( existing_application.has_value() )
{
    if ( existing_application.value().toString() != application_bytes )
    {
        return outcome::failure( std::errc::state_not_recoverable );
    }
    // Cross-check every persisted/in-memory effect...
    return AtomicMintEffectResult::AlreadyApplied;
}
if ( existing_application.error() != storage::DatabaseError::NOT_FOUND )
{
    return outcome::failure( existing_application.error() );
}
...
BOOST_OUTCOME_TRY( batch->put( application_key, application_value ) );
BOOST_OUTCOME_TRY( batch->commit() );
utxo_outpoints_.swap( candidate_outpoints );
```

The consensus processing marker should mirror this identity binding: slot ID, canonical certificate digest, proposal ID, and winning subject/transaction hash. `Complete` is valid only when those identities match the authoritative slot certificate. A marker mismatch or Complete-without-authoritative-certificate is startup corruption.

Do not claim the consensus marker makes arbitrary handlers atomic with the marker—it cannot. The recovery contract is at-least-once handler invocation with an idempotent exact winner. For mint-v2, `TransactionManager::ApplyConfirmedMintV2()` already derives `winning_transaction_hash` from the certificate-selected transaction and calls `ApplyMintEffectsAtomically()`. Crash after effects/before Complete must retry and observe `AlreadyApplied`, then persist Complete.

Cleanup ordering is strict:

```text
certificate pair durable
  -> Pending/Processing marker durable
  -> exact winning handler succeeds (Applied or AlreadyApplied)
  -> Complete marker durable
  -> ClearProposalSlot / pending votes / temporary candidate cleanup
```

Handler absence, error, `Stalled`, or owner destruction leaves Pending/Stalled state and retains candidates/votes. Current `CertificateReceived()` does the opposite for a missing handler:

```cpp
if ( it == certificate_subject_handlers_.end() )
{
    (void) certificate_work_journal_->MarkDone( key );
    return;
}
```

That `MarkDone` behavior must not be carried into the new marker.

### 7. Conflict filter, evidence, and metric patterns

The CRDT pre-merge filter is the only place that can observe a replicated conflict before it is rejected. Today it checks occupied state before full `NormalizeCertificate()` and only logs:

```cpp
if ( existing_slot &&
     ( existing_slot->toString() != slot_element->value() ||
       existing_index->toString() != slot_result.value() ) )
{
    ConsensusManagerLogger()->critical(
        "{}: replicated certificate conflict slot={} incoming_proposal_id={} winner={}", ... );
    return crdt::DeltaFilterResult::Reject();
}
```

Reorder the filter so a conflict is classified only after structural/canonical/cryptographic validation. Then call the same local conflict recorder used by local and pubsub finalization before returning `Reject`. Invalid payloads remain ordinary rejects with no safety evidence.

Evidence identity and update pattern:

```text
original_digest = SHA-256(canonical deterministic original certificate bytes)
incoming_digest = SHA-256(canonical deterministic incoming certificate bytes)
pair = sort(original_digest, incoming_digest)
key = /consensus/local/v2/conflict/<slot>/<pair[0]>:<pair[1]>

first observation:
  original/incoming proposal IDs and digests
  first source + sources-seen bitmask
  first_seen_ms = last_seen_ms = now
  observation_count = 1
repeat/reversed observation:
  same key
  OR source bit
  last_seen_ms = now
  observation_count += 1
```

The record must not contain either full certificate. Commit the evidence update and durable slot `SafetyViolation` together. Restore that state before participation at startup. It blocks proposal admission, local signing, aggregation/certificate creation, and normal certificate publication for the slot, but does not block retrying the original authoritative winner's pending application.

Use an atomic counter following the existing TransactionManager metric style:

```cpp
std::atomic<uint64_t> certificate_conflict_count_{ 0 };
...
certificate_conflict_count_.fetch_add( 1, std::memory_order_relaxed );
```

Emit a `critical` log carrying slot, evidence key, both proposal IDs, both digests, source, and observation count. Whether the metric counts every observation or unique evidence pair should be documented and tested; unique safety events is usually more actionable, while the record separately tracks observation count.

Never call normal certificate publication for a conflict. The publish observer/counter test seam should remain zero for both direct and CRDT-filter conflicts.

### 8. Configuration propagation

The repository currently resolves network JSON inside `GeniusNode::InitNetwork()`. Its numeric parsing style is:

```cpp
if ( config_json.HasMember( "port_seed" ) )
{
    if ( config_json["port_seed"].IsUint() )
    {
        port_seed = static_cast<uint16_t>( config_json["port_seed"].GetUint() );
    }
    else
    {
        node_logger_->warn( "... using default/param {}", port_seed );
    }
}
```

Apply the same config-wins/default-fallback shape to `consensus_vote_selection_window_ms`, with additional bounds validation. Suggested value object:

```cpp
struct ConsensusConfig
{
    std::chrono::milliseconds vote_selection_window{ 500 };
};
```

Propagation should be value-based and pre-start:

```text
GeniusNode::InitNetwork resolves member
  -> StateTransition(INITIALIZING_BLOCKCHAIN)
  -> Blockchain::New(..., consensus_config, callback)
  -> ConsensusManager::New(..., consensus_config, topic)
  -> constructor stores immutable/defaulted config
  -> startup scan/subscription/timer
```

Keep defaults on the factory parameters if necessary for the many direct test call sites. Do not use `Configure...()` after `ConsensusManager::New()` because `New()` already subscribes and starts the timer.

### 9. GoogleTest friend-only seams

The established pattern is a forward friend in production and the accessor class defined only in the test translation unit:

```cpp
// Consensus.hpp
friend class ConsensusManagerTestAccess;
friend class ConsensusPendingLifecycleTestAccess;

// consensus_certificate_store_test.cpp
class ConsensusManagerTestAccess
{
public:
    static void SetCertificatePublishObserver(
        const std::shared_ptr<ConsensusManager> &manager,
        std::function<void()> observer )
    {
        manager->certificate_publish_observer_ = std::move( observer );
    }
};
```

`UTXOManager` uses the same pattern for private fault stages and read seams. Phase 10 should add purpose-specific friends such as `ConsensusVoteJournalTestAccess` and `ConsensusFinalizationTestAccess`, not public production failure APIs.

Recommended private-only seam types:

- state-store read/write/query callbacks that return typed RocksDB errors;
- signature/persistence/publication observers: after sign, after durable vote commit, before raw publish;
- certificate/process observers: after certificate pair commit, after Pending commit, after handler effect, before Complete;
- explicit system and steady `now` providers or method parameters;
- `ProcessCandidateDeadlines(now)` direct invocation;
- handler blocker/counter using `std::barrier`, `std::latch`, promises, or condition variables;
- conflict evidence/metric readers;
- startup side-effect observer/counters for subscribe, filter/listener, timer, and publish.

Do not expose mutable state-store internals publicly. Avoid long sleeps; use barriers and explicit clocks. Restart tests must `Close()` the first manager and reopen a manager over the same database path.

## State and Locking Pattern

The current `SlotState` is only:

```cpp
struct SlotState
{
    std::string best_proposal_id;
    std::string best_tx_hash;
    std::unordered_set<std::string> voted_proposal_ids;
};
```

Replace its safety semantics with an explicit lifecycle and generation. A practical mapping is:

| State | Required durable/in-memory facts | Permitted transition |
|---|---|---|
| `Empty` | No candidate/vote/finality | `Selecting`, `FinalizedPendingApplication`, `SafetyViolation` |
| `Selecting` | Immutable steady deadline, mutable ranked best | `Signing`, finalization, violation |
| `Signing` | Frozen proposal + generation; signer may run outside lock | `Voted`, same-generation persistence retry, finalization, violation |
| `Voted` | Exact durable vote/envelope + acceptance horizon | finalization, durable `Retired`, violation |
| `Retired` | Durable proof/state that old signature is outside live acceptance horizon | later-generation `Selecting`, finalization, violation |
| `FinalizedPendingApplication` | Authoritative certificate + matching Pending/Processing marker | `Applied`, violation |
| `Applied` | Matching durable Complete marker | violation on a later valid conflict |
| `SafetyViolation` | Durable conflict flag/evidence; original winner immutable | no participation; original application retry may continue |

Use one synchronization boundary for slot selection/signing/finalization transitions. Do not hold that broad mutex while calling the signer, CRDT persistence, or arbitrary transaction handler. Reserve a generation under lock, perform external work, then recheck under lock. Document any separate store/finalization/process mutex lock order as explicitly as UTXOManager documents `persistence_mutex_` before `utxos_mutex_`.

## Validation Split and Vote Retirement

Current `NormalizeCertificate()` is timeless: it verifies proposal/votes/registry/quorum/canonical bytes but does not call `IsTimestampSane()` for certificate contents. Split it conceptually into:

```text
NormalizeCertificateStructural(certificate)
  -> unknown-field rejection
  -> proposal/certificate/registry identity checks
  -> signatures, unique voters, quorum, ordering
  -> canonical deterministic bytes

ValidateCertificateForFirstObservation(normalized, now)
  -> signed proposal timestamp accepted now
  -> every signed vote timestamp accepted now
```

Stored authoritative reads and identical occupied-slot replay use structural normalization only. Empty-slot first acceptance uses both. The local vote's durable acceptance horizon is the minimum upper bound derived from its signed proposal and signed vote under that exact live rule. Retirement must use the same function/boundary and commit durably before a later generation can sign. Pending-proposal TTL, selection deadline, round advancement, certificate delay, and local transaction timeout are not retirement evidence.

Boundary tests must cover immediately before, exactly at, and immediately after the horizon, with the inequality matching `IsTimestampSane()` exactly.

## Planner Guardrails

- Do not use `GlobalDB::Put()` for local votes, process markers, or conflict evidence.
- Do not reconstruct or re-sign a stored vote; raw-replay the exact stored envelope.
- Do not make proposal cleanup erase the only vote lock.
- Do not let later proposals extend the first selection deadline.
- Do not accept/reject certificates based on local candidate or local vote preference.
- Do not filter peer votes based on the local best proposal.
- Do not clear after certificate creation/delivery; clear only after durable Complete.
- Do not mark processing complete when no handler exists.
- Do not treat local preflight/mutex as a distributed CAS.
- Do not classify malformed/invalid payloads as safety conflicts.
- Do not wait for a CRDT callback to record a pre-merge conflict; it will never arrive.
- Do not overwrite/apply the second certificate or rebroadcast it.
- Do not add detached per-slot timer threads.
- Do not expose public fault injection; use friend-only accessors.
- Preserve user changes already present in `src/account/GeniusNode.cpp` when implementation begins.

## Suggested Implementation Order From the Patterns

1. Add local-state schema/store and strict standalone RocksDB tests.
2. Add `ConsensusConfig` propagation before changing timer behavior.
3. Restore state before startup side effects and add fail-closed/replay tests.
4. Replace immediate voting with fixed-window state/generation handling and exact durable publication.
5. Split structural/live certificate validation and implement durable retirement.
6. Move Phase 9 pair persistence behind `FinalizeSlot`; route all source adapters through it.
7. Add processing marker/recovery/handler-registration wake-up and finality-before-cleanup.
8. Add conflict evidence at local, pubsub, and CRDT pre-merge rejection points, then restore SafetyViolation on startup.
9. Run existing Phase 9 certificate/index, pending lifecycle, transaction pending lifecycle, and UTXO idempotency suites as compatibility gates.

This order keeps the state-store contract and deterministic test seams available before concurrency-sensitive consensus behavior is moved onto them.
