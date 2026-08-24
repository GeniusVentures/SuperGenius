# Phase 11: Convergent Certificate Consumption & Mint Recovery - Pattern Map

**Mapped:** 2026-08-24
**Files analyzed:** 6 planned production/test files
**Analogs found:** 6 / 6

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/blockchain/Consensus.hpp` | service API | event-driven, recovery | Existing certificate handler/recovery declarations in the same header | exact |
| `src/blockchain/Consensus.cpp` | consensus service | event-driven, CRUD/recovery | Existing certificate work journal ingress and durable readback | exact |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | integration test | event-driven, recovery | Its existing callback → durable-readback lifecycle case | exact |
| `src/account/TransactionManager.hpp` | service API | request-response, recovery | Existing CRDT fetch, exact-binding, and private test seam declarations | exact |
| `src/account/TransactionManager.cpp` | transaction service | CRUD, request-response, recovery | Existing transaction-first certificate lookup, certificate handler, and state machine | exact |
| `test/src/account/transaction_manager_certificate_fallback_test.cpp` | integration test | request-response, CRUD/recovery | Its certificate fallback fixture and competing Mint slot case | exact |

`src/account/UTXOManager.cpp` is an important shared persistence analog, not an expected Phase 11 edit: its outpoint-keyed `PutUTXO` already makes replay safe. Do not add a bridge-specific UTXO journal or state record.

## Pattern Assignments

### `src/blockchain/Consensus.hpp` and `src/blockchain/Consensus.cpp` (service, event-driven recovery)

**Analog:** The existing `CertificateReceived` → `RecoverPendingCertificateWork` → `ProcessCommittedCertificate` pipeline in `src/blockchain/Consensus.cpp:2521-2531, 3534-3630`.

**API placement and registration pattern** (`src/blockchain/Consensus.hpp:236-239, 827-863`; `src/blockchain/Consensus.cpp:262-284`): keep the registration API public and the replay/dispatch helpers private. Registration currently mutates the handler map while holding `certificate_handlers_mutex_`; the Phase 11 recovery call must happen only after releasing that lock.

```cpp
bool ConsensusManager::RegisterCertificateHandler( std::string_view subject_type,
                                                   CertificateSubjectHandler handler )
{
    // validate handler and type hash first
    std::unique_lock lock( certificate_handlers_mutex_ );
    certificate_subject_handlers_[type_hash.value()] = std::move( handler );
    return true;
}
```

Adapt this narrowly: finish the map insertion, unlock, then invoke the existing `RecoverPendingCertificateWork()`. Do not create a second dispatch queue or a certificate-acceptance record.

**Ingress/recovery split** (`src/blockchain/Consensus.cpp:2521-2531, 3534-3589`): CRDT callbacks are deliberately pre-commit. They only retain work; durable `db_->Get` readback and validation are required before any release or handler invocation.

```cpp
// CertificateReceived: callback notification, not finality processing.
certificate_work_journal_->MarkSeen( key );
certificate_work_journal_->MarkStalled( key, std::chrono::milliseconds( 0 ) );
timer_cv_.notify_all();

// RecoverPendingCertificateWork: authoritative committed readback.
auto value = db_->Get( { entry.key } );
if ( value.has_error() ) {
    certificate_work_journal_->MarkStalled( entry.key, std::chrono::milliseconds( 0 ) );
    continue;
}
if ( !certificate.ParseFromArray( value.value().data(), value.value().size() ) ||
     !ValidateCertificateKey( certificate, entry.key ) ||
     ValidateCertificate( certificate ) != Check::Approve ) {
    certificate_work_journal_->MarkStalled( entry.key, std::chrono::milliseconds( 0 ) );
    continue;
}
```

Preserve the following ordering in recovery: read canonical `/cert/<slot>` → parse/key/quorum validation → `ReleaseActiveVoteForAcceptedSlot(slot)` → `ClearProposalSlot` → subject handler. Every local failure stays `Stalled` so the existing timer/startup recovery retries it.

**Dispatch result contract** (`src/blockchain/Consensus.cpp:3593-3630`): a handler error or `Check::Stalled` retains the work; only a non-stalled, successful handler result ends it.

```cpp
auto certificate_handler_result = handler( subject_hash.value(), certificate );
if ( certificate_handler_result.has_error() ||
     certificate_handler_result.value() == Check::Stalled ) {
    certificate_work_journal_->MarkStalled( key, std::chrono::milliseconds( 0 ) );
    return;
}
(void) certificate_work_journal_->MarkDone( key );
(void) WakePendingDependency( PendingDependencyKey::Certificate( subject_hash.value() ) );
```

Replace only the no-handler branch's `MarkDone(key)` behavior at `src/blockchain/Consensus.cpp:3607-3615`: no handler is a retryable availability condition, so it must use `MarkStalled(key, 0ms)` and return without waking completion dependencies. Keep parse/key/quorum failures fail-closed and retryable as the surrounding method does.

### `test/src/blockchain/consensus_pending_lifecycle_test.cpp` (integration test, event-driven recovery)

**Analog:** `CertificateCallbackStallsUntilPostCommitReadbackCanReleaseSameSlot` at `test/src/blockchain/consensus_pending_lifecycle_test.cpp:1558-1660`.

**Private-access seam** (`lines 34-135`): extend the existing friend accessor rather than adding production test hooks. Its current helpers directly call the callback and recovery and inspect the shared work journal.

```cpp
static void CertificateReceived( const std::shared_ptr<ConsensusManager> &manager,
                                 crdt::CRDTCallbackManager::NewDataPair new_data )
{
    manager->CertificateReceived( std::move( new_data ), std::string{} );
}

static bool HasStalledCertificateWork( const std::shared_ptr<ConsensusManager> &manager,
                                       const std::string &key )
{
    auto entry = manager->certificate_work_journal_->GetEntry( key );
    return entry.has_value() && entry->state == crdt::CRDTWorkJournal::State::Stalled;
}
```

**Durable lifecycle test shape** (`lines 1598-1656`): callback first, assert stalled; an immediate recovery before the CRDT write remains stalled; write the accepted certificate at its canonical slot; recover; then assert the journal and active-vote state only clear after success.

```cpp
CertificateReceived( manager, { key, std::move( callback_value ) } );
EXPECT_TRUE( HasStalledCertificateWork( manager, key ) );

RecoverPendingCertificateWork( manager ); // pre-commit / missing readback
EXPECT_TRUE( HasStalledCertificateWork( manager, key ) );

WriteLiveCertificate( manager, certificate );
RecoverPendingCertificateWork( manager );
```

Add the Phase 11 no-handler ordering case beside this test: persist a valid canonical certificate, leave it stalled when no nonce handler is registered, register a counting handler, and assert registration causes one durable-readback dispatch and clears work only after `Approve`. Replay recovery must not add another call after `MarkDone`. Retain the existing same-slot active-vote assertions and no-sleep/direct helper style.

### `src/account/TransactionManager.hpp` and `src/account/TransactionManager.cpp` (service, request-response/recovery)

**Analog:** Existing transaction-first finality selection in `src/account/TransactionManager.cpp:1782-1856`, exact certificate binding in `1517-1542`, and certificate handler registration in `108-151`.

**Handler registration/error propagation** (`src/account/TransactionManager.cpp:126-151`): `TransactionManager::New` registers the nonce subject handler as a thin lambda that returns `OnConsensusCertificate`'s `outcome::result<Check>`. Preserve this outcome boundary so Mint persistence failures reach `ConsensusManager::ProcessCommittedCertificate` and remain journal-retryable.

```cpp
instance->blockchain_->RegisterCertificateHandler(
    NONCE_SUBJECT_TYPE,
    [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
        const std::string &subject_hash,
        const ConsensusCertificate &certificate ) -> outcome::result<ConsensusManager::Check>
    {
        if ( auto strong = weak_ptr.lock() ) {
            return strong->OnConsensusCertificate( subject_hash, certificate );
        }
        return outcome::failure( std::errc::owner_dead );
    } );
```

**Exact certificate binding gate** (`src/account/TransactionManager.cpp:1517-1542`; declaration/comment at `src/account/TransactionManager.hpp:284-289`): keep the existing proof that binds the certificate subject's account, nonce, subject `tx_hash`, embedded transaction hash, and embedded transaction slot to the selected candidate.

```cpp
if ( subject.account_id() != transaction.GetSrcAddress() ||
     nonce_subject.value().nonce() != transaction.GetNonce() ||
     nonce_subject.value().tx_hash() != transaction.GetHash() ) {
    return false;
}
auto embedded_transaction = DeSerializeEmbeddedTransaction( nonce_subject.value().transaction() );
return embedded_transaction.has_value() && embedded_transaction.value() &&
       embedded_transaction.value()->GetHash() == transaction.GetHash() &&
       embedded_transaction.value()->GetSlotID() == transaction.GetSlotID();
```

Call this guard before every certificate-first `ChangeTransactionState(CONFIRMED)` call. Do not accept same-slot equality alone, query `/cert/<tx-hash>`, or use a different confirmation route for the embedded fallback.

**CRDT fetch and normal transaction-first lifecycle** (`src/account/TransactionManager.cpp:1698-1705, 1782-1856`): reuse `FetchTransaction(globaldb_m, transaction_key)` and the monitored-network `GetTransactionPath(network_id, tx_hash)` convention. A new private helper belongs with `FetchTransaction`/`GetMonitoredNetworkIDs` declarations (`TransactionManager.hpp:198-204, 376-380`), returning a transaction only if its decoded `GetHash()` is exactly `tx_hash`.

```cpp
auto next_tx_state      = TransactionStatus::VERIFYING;
auto certificate_result = blockchain_->GetCertificateBySlot( transaction->GetSlotID() );
if ( certificate_result.has_value() &&
     CertificateMatchesTransaction( certificate_result.value(), *transaction ) ) {
    next_tx_state = TransactionStatus::CONFIRMED;
}
BOOST_OUTCOME_TRY( ChangeTransactionState( transaction, next_tx_state ) );
```

For `OnConsensusCertificate` (`src/account/TransactionManager.cpp:3698-3809`), use the selected candidate order: (1) current `GetTransactionByHash(tx_hash)` tracking entry; (2) exact-hash CRDT fetch across monitored networks; (3) only then `DeSerializeEmbeddedTransaction` from the accepted certificate. Preserve the existing empty/non-nonce certificate compatibility returns. On all paths, require `CertificateMatchesTransaction(certificate, *tx)` before changing state. The fallback must retain its current `CheckHash()`/subject-hash integrity gate.

**Mint parser outcome style** (`src/account/TransactionManager.cpp:1860-1989`): transfer and legacy mint parsing already use `BOOST_OUTCOME_TRY` for UTXO persistence. Apply the same propagation to only the Mint V2 output/bridge-input operations; do not swallow their return values.

```cpp
for ( std::uint32_t i = 0; i < outputs.size(); ++i ) {
    GeniusUTXO new_utxo( hash, i, outputs[i].encrypted_amount, outputs[i].token_id );
    BOOST_OUTCOME_TRY( account_m->GetUTXOManager().PutUTXO( new_utxo, outputs[i].dest_address ) );
}
if ( !inputs.empty() ) {
    BOOST_OUTCOME_TRY( account_m->GetUTXOManager().ConsumeUTXOs(
        inputs, mint_tx_v2->GetSrcAddress(), UTXOManager::UTXOType::UTXO_BRIDGE ) );
}
```

**Terminal state ordering** (`src/account/TransactionManager.cpp:5168-5425`; marker constants at `TransactionManager.hpp:569-570`): the current `CONFIRMED` branch prematurely writes `tx_processed_m` as `CONFIRMED`, writes `/bridge/executed/…`, logs its error only, then calls `ParseTransaction`. Refactor the Mint V2 branch so the existing lifecycle remains the sole completion path but its order becomes: keep/reuse retryable `VERIFYING` entry → `ParseTransaction` (idempotent durable UTXOs) → durable executed-marker write with returned error → update tracking entry to `CONFIRMED`, metrics/nonces/missing set. A pre-existing terminal `CONFIRMED` entry remains the duplicate no-op only after that complete sequence.

```cpp
auto put_result = datastore->put( key_buffer, value_buffer );
if ( put_result.has_error() ) {
    TransactionManagerLogger()->error( "... Failed to persist executed bridge mint ..." );
    return outcome::failure( put_result.error() );
}
```

Do not roll back partial outputs on a failure. The returned error is the contract that causes certificate work to remain `Stalled`; retry replays idempotent outpoints and repairs the missing marker. Limit this changed ordering to certified `mint-v2` completion unless code inspection finds another type sharing the same marker.

### `test/src/account/transaction_manager_certificate_fallback_test.cpp` (integration test, request-response/CRUD recovery)

**Analog:** The existing `CertificateFallbackTest` CRDT fixture at `test/src/account/transaction_manager_certificate_fallback_test.cpp:199-382`, its friend accessor at `31-70`, and `SharedMintSlotConfirmsOnlyTheCertifiedTransaction` at `658-681`.

**Fixture/persistence pattern:** the fixture loads the account UTXO store from `db_->GetDataStore()`, creates a real `Blockchain` and `TransactionManager`, and persists test values using `GlobalDB::Buffer` plus `db_->Put`. Extend this fixture; do not introduce a parallel mock consensus store.

```cpp
auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
assert( load_result.has_value() );

crdt::GlobalDB::Buffer serialized;
serialized.put( transaction->SerializeByteVector() );
ASSERT_TRUE( db_->Put( { TransactionManager::GetTransactionPath( *transaction ) }, serialized, {} ).has_value() );
```

**Certificate-first tests:** add private-access helpers only where necessary (for example, CRDT transaction lookup state or marker inspection). Seed an exact winning serialized transaction at its normal CRDT transaction path without adding it to `tx_processed_m`, invoke `OnConsensusCertificate`, and assert it confirms that CRDT value. A separate CRDT-miss case must prove the exact embedded winner follows the same state transition. Use the existing malformed/hash mismatch cases at `442-511` as the error expectation: invalid embedded data is not applied.

**Competing Mint binding test:** preserve the exact test construction and assertions at `658-681`:

```cpp
ASSERT_NE( winner->GetHash(), loser->GetHash() );
ASSERT_EQ( winner->GetSlotID(), loser->GetSlotID() );
EXPECT_TRUE( TransactionManager::CertificateMatchesTransaction( loaded.value(), *winner ) );
EXPECT_FALSE( TransactionManager::CertificateMatchesTransaction( loaded.value(), *loser ) );
```

Extend it through the certificate-handler path, not just `FetchAndProcess`: an accepted winner certificate can yield one winner output/marker, while the same-slot loser cannot become confirmed or create an output.

**Replay/recovery assertions:** add deterministic tests in this fixture for duplicate certificate processing and "UTXOs durable, marker absent" replay. Inspect the existing marker key (`/bridge/executed/<chain>:<source>`) and stored UTXO outpoint/balance through current public manager APIs or a narrow friend accessor. Assert only one output/outpoint/balance effect, marker restoration on replay, and terminal `CONFIRMED` only after restoration. For an injected or `ReleaseStorage` persistence failure, assert `OnConsensusCertificate` returns an error/`Stalled` path and tracked state stays nonterminal; restore persistence and replay to success.

## Shared Patterns

### Existing certificate work journal is the only retry/finality work boundary

**Sources:** `src/blockchain/Consensus.cpp:2521-2531, 3534-3630`.

All certificate sources—local publication, PubSub notification, CRDT receipt, timer, and startup—must converge through callback-stalled work plus committed canonical-record recovery. PubSub must remain notification/volatile cleanup only. Do not add `/bridge/certified`, a certificate acceptance table, or another finality journal.

### Fail closed on exact certificate/transaction binding

**Sources:** `src/account/TransactionManager.cpp:1517-1542, 1782-1856, 3698-3809`.

The transaction derives the canonical slot for lookup; `CertificateMatchesTransaction` proves exact winner identity. A missing/malformed/mismatched CRDT transaction or embedded fallback must never upgrade a same-slot loser. Return `outcome` errors for retryable local persistence faults; do not convert valid-cert local failure into `FAILED`/`Approve` completion.

### UTXO replay idempotence plus durable error propagation

**Sources:** `src/account/UTXOManager.cpp:150-179, 211-265, 767-774`; `src/account/TransactionManager.cpp:1897-1942`.

`PutUTXO` keys entries by `(transaction hash, output index)` and returns `false` when the outpoint already exists, then persists only new output state through `StoreUTXOs`. Treat an existing Mint output as successful replay, but propagate an actual `outcome` storage error. Partial effects are retained and replayed; do not compensate/rollback them.

```cpp
const OutPoint outpoint{ new_utxo.GetTxID(), new_utxo.GetOutputIdx() };
if ( utxo_outpoints_.find( outpoint ) != utxo_outpoints_.end() ) {
    return false;
}
// insert, then BOOST_OUTCOME_TRY( StoreUTXOs( address ) )
```

### Terminal confirmation means all Mint effects are durable

**Sources:** `src/account/TransactionManager.cpp:5168-5425`; `TransactionManager.hpp:569-570`.

For Mint V2 only, output effects must be persisted before the existing executed marker, and the marker must be durable before the `CONFIRMED` transition. Marker failure is an error, never a log-only success. This ordering uses the existing transaction map plus UTXOs/marker as recovery truth and creates no Mint-specific state machine.

## No Analog Found

None. The phase extends established consensus journal, transaction lifecycle, UTXO persistence, and focused fixture patterns.

## Metadata

**Analog search scope:** `src/blockchain`, `src/account`, `test/src/blockchain`, `test/src/account`, Phase 10 planning map
**Files scanned:** 10 production/test files plus phase context, research, and validation inputs
**Pattern extraction date:** 2026-08-24
