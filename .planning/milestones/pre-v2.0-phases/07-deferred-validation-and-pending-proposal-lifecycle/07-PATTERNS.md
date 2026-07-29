# Phase 07: Deferred Validation and Pending Proposal Lifecycle - Pattern Map

**Mapped:** 2026-06-16
**Files analyzed:** 10
**Analogs found:** 10 / 10

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/blockchain/Consensus.hpp` | model/service interface | event-driven, request-response | `src/blockchain/Consensus.hpp` | exact |
| `src/blockchain/Consensus.cpp` | service | event-driven, request-response, scheduled batch | `src/blockchain/Consensus.cpp` | exact |
| `src/blockchain/Blockchain.hpp` | facade/route | request-response | `src/blockchain/Blockchain.hpp` | exact |
| `src/blockchain/impl/Blockchain.cpp` | facade/route | request-response | `src/blockchain/impl/Blockchain.cpp` | exact |
| `src/account/TransactionManager.hpp` | model/service interface | CRUD, event-driven | `src/account/TransactionManager.hpp` | exact |
| `src/account/TransactionManager.cpp` | service | CRUD, event-driven, request-response | `src/account/TransactionManager.cpp` | exact |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | test | event-driven, scheduled batch | `test/src/blockchain/consensus_certificate_test.cpp` | exact |
| `test/src/account/transaction_manager_pending_lifecycle_test.cpp` | test | CRUD, event-driven | `test/src/account/transaction_manager_certificate_fallback_test.cpp` | role-match |
| `test/src/blockchain/CMakeLists.txt` | config | build registration | `test/src/blockchain/CMakeLists.txt` | exact |
| `test/src/account/CMakeLists.txt` | config | build registration | `test/src/account/CMakeLists.txt` | exact |

## Pattern Assignments

### `src/blockchain/Consensus.hpp` (model/service interface, event-driven request-response)

**Analog:** `src/blockchain/Consensus.hpp`

**Imports pattern** (lines 9-32):
```cpp
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <limits>

#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crdt/globaldb/crdt_work_journal.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/proto/delta.pb.h"
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "outcome/outcome.hpp"
```

**Validation result contract pattern** (lines 93-109):
```cpp
enum class Check
{
    Approve,
    Reject,
    Pending,
    Stalled
};

using SubjectHandler = std::function<outcome::result<Check>( const Subject &subject )>;
using CertificateSubjectHandler =
    std::function<outcome::result<Check>( const std::string &subject_hash, const Certificate &certificate )>;
using ProposalCleanupHandler = std::function<void( const std::string &tx_hash )>;
```

Copy this placement for `PendingDependencyKey`, `ValidationResult`, and any pending lifecycle config: keep them local C++ runtime API, not protobuf wire fields. Replace `SubjectHandler` with `outcome::result<ValidationResult>` or add a short adapter here so existing simple handlers can map `Check` to `ValidationResult`.

**Private state pattern** (lines 472-493, 720-726):
```cpp
struct ProposalState
{
    Proposal                        proposal;
    std::vector<Vote>               votes;
    std::string                     slot_key;
    uint64_t                        total_weight    = 0;
    uint64_t                        approved_weight = 0;
    std::unordered_set<std::string> seen_voters;
    bool                            quorum_reached       = false;
    uint64_t                        quorum_reached_ts_ms = 0;
    uint64_t                        last_attempt_round   = NO_ROUND;
};

struct SlotState
{
    std::string best_proposal_id;
    std::string best_tx_hash;
    bool        voted = false;
};

std::unordered_map<std::string, ProposalState> proposals_;
std::unordered_map<std::string, SlotState>     slot_states_;
std::unordered_map<std::string, Proposal> pending_proposals_;
std::unordered_map<std::string, std::vector<std::string>>
    pending_by_subject_hash_;
std::unordered_map<std::string, std::vector<Vote>> pending_votes_;
mutable std::mutex                                 proposals_mutex_;
```

Use this area for one canonical pending entry map plus secondary dependency index, per-proposer counters, byte accounting, retry timestamps, and TTL fields. Preserve `SlotState::voted` and `ProposalState::seen_voters`; retries must reuse them.

---

### `src/blockchain/Consensus.cpp` (service, event-driven request-response scheduled batch)

**Analog:** `src/blockchain/Consensus.cpp`

**Imports/logging pattern** (lines 7-31):
```cpp
#include "blockchain/Consensus.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <system_error>
#include <boost/format.hpp>

#include <gsl/span>

#include "base/hexutil.hpp"
#include "base/sgns_version.hpp"
#include "crypto/hasher/hasher_impl.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/ConsensusAuth.hpp"

base::Logger ConsensusManagerLogger()
{
    return base::createLogger( "ConsensusManager" );
}
```

**Handler registration pattern** (lines 222-238, 293-314):
```cpp
bool ConsensusManager::RegisterSubjectHandler( std::string_view subject_type, SubjectHandler handler )
{
    if ( !handler )
    {
        ConsensusManagerLogger()->error( "{}: ignored empty handler subject_type={}", __func__, subject_type );
        return false;
    }
    auto type_hash = ComputeSubjectTypeHash( subject_type );
    if ( type_hash.has_error() )
    {
        ConsensusManagerLogger()->error( "{}: ignored invalid handler subject_type={}", __func__, subject_type );
        return false;
    }
    ConsensusManagerLogger()->debug( "{}: Registering subject handler subject_type={}", __func__, subject_type );
    std::unique_lock lock( subject_handlers_mutex_ );
    subject_handlers_[type_hash.value()] = std::move( handler );
    return true;
}

bool ConsensusManager::RegisterProposalCleanupHandler( std::string_view subject_type,
                                                       ProposalCleanupHandler handler )
{
    if ( !handler )
    {
        ConsensusManagerLogger()->error( "{}: ignored empty cleanup handler subject_type={}",
                                         __func__,
                                         subject_type );
        return false;
    }
    auto type_hash = ComputeSubjectTypeHash( subject_type );
    if ( type_hash.has_error() )
    {
        ConsensusManagerLogger()->error( "{}: ignored invalid cleanup handler subject_type={}",
                                         __func__,
                                         subject_type );
        return false;
    }
    std::unique_lock lock( cleanup_handlers_mutex_ );
    proposal_cleanup_handlers_[type_hash.value()].push_back( std::move( handler ) );
    return true;
}
```

Copy this for any new test/config registration API: validate callable/input, hash canonical subject type, log, then mutate under the correct lock.

**Current pending admission/index pattern to replace** (lines 675-716):
```cpp
void ConsensusManager::AddPendingProposal( const Proposal &proposal, const std::string &subject_hash )
{
    std::lock_guard lock( proposals_mutex_ );
    if ( pending_proposals_.find( proposal.proposal_id() ) != pending_proposals_.end() )
    {
        ConsensusManagerLogger()->error(
            "{}: Failed adding pending proposal for {}: already have a proposal with id {}",
            __func__,
            subject_hash.substr( 0, 8 ),
            proposal.proposal_id().substr( 0, 8 ) );
        return;
    }
    pending_proposals_.emplace( proposal.proposal_id(), proposal );
    pending_by_subject_hash_[subject_hash].push_back( proposal.proposal_id() );
}

std::vector<ConsensusManager::Proposal> ConsensusManager::TakePendingProposals( const std::string &subject_hash )
{
    std::vector<Proposal> result;
    std::lock_guard       lock( proposals_mutex_ );
    auto                  it = pending_by_subject_hash_.find( subject_hash );
    if ( it == pending_by_subject_hash_.end() )
    {
        return result;
    }
    for ( const auto &proposal_id : it->second )
    {
        auto prop_it = pending_proposals_.find( proposal_id );
        if ( prop_it != pending_proposals_.end() )
        {
            result.push_back( prop_it->second );
            pending_proposals_.erase( prop_it );
        }
    }
    pending_by_subject_hash_.erase( it );
    return result;
}
```

Keep the same canonical-map plus secondary-index shape, but change the secondary index from `subject_hash` to typed `PendingDependencyKey`. Add admission checks before insertion; capacity failure logs and returns local failure/pending refusal without emitting votes.

**Proposal validation and local pending pattern** (lines 1232-1386):
```cpp
void ConsensusManager::HandleProposal( const Proposal &proposal )
{
    if ( !CheckProposal( proposal ) )
    {
        return;
    }
    if ( !IsTimestampSane( proposal.timestamp() ) )
    {
        return;
    }
    auto subject_hash = GetSubjectHash( proposal.subject() );
    if ( subject_hash.has_error() )
    {
        return;
    }
    auto proposal_registry_result = registry_->LoadRegistry( proposal.registry_cid() );
    if ( proposal_registry_result.has_error() )
    {
        std::lock_guard lock( proposals_mutex_ );
        if ( proposals_.find( proposal.proposal_id() ) == proposals_.end() )
        {
            ProposalState state;
            state.proposal = proposal;
            state.slot_key = GetSlotKey( proposal );
            proposals_.emplace( proposal.proposal_id(), std::move( state ) );
        }
        AddPendingProposal( proposal, subject_hash.value() );
        return;
    }

    SubjectHandler subject_handler;
    {
        std::shared_lock lock( subject_handlers_mutex_ );
        auto handler_it = subject_handlers_.find( proposal.subject().subject_type_hash().hash() );
        if ( handler_it == subject_handlers_.end() )
        {
            return;
        }
        subject_handler = handler_it->second;
    }

    auto subject_result = subject_handler( proposal.subject() );
    if ( subject_result.has_error() || subject_result.value() == Check::Reject )
    {
        return;
    }
    if ( subject_result.value() == Check::Pending )
    {
        AddPendingProposal( proposal, subject_hash.value() );
        return;
    }
    ContinueProposalAfterSubject( proposal );
}
```

Refactor this by replacing enum checks with `ValidationResult`. `Pending` must stay before `ContinueProposalAfterSubject()` because that helper can self-vote.

**Retry pattern through same validation path** (lines 1389-1460):
```cpp
outcome::result<void> ConsensusManager::ResumeProposalHandling( const std::string &subject_hash )
{
    if ( subject_hash.empty() )
    {
        return outcome::failure( std::errc::invalid_argument );
    }

    auto to_process = TakePendingProposals( subject_hash );
    for ( const auto &proposal : to_process )
    {
        SubjectHandler subject_handler;
        {
            std::shared_lock lock( subject_handlers_mutex_ );
            auto handler_it = subject_handlers_.find( proposal.subject().subject_type_hash().hash() );
            if ( handler_it == subject_handlers_.end() )
            {
                continue;
            }
            subject_handler = handler_it->second;
        }

        auto subject_result = subject_handler( proposal.subject() );
        if ( subject_result.has_error() || subject_result.value() == Check::Reject )
        {
            continue;
        }
        if ( subject_result.value() == Check::Pending )
        {
            auto subject_hash_result = GetSubjectHash( proposal.subject() );
            if ( subject_hash_result.has_value() )
            {
                AddPendingProposal( proposal, subject_hash_result.value() );
            }
            continue;
        }

        ContinueProposalAfterSubject( proposal );
    }
    return outcome::success();
}
```

Copy this control flow for dependency-triggered and scheduled retries: take due proposal ids, re-run handler, update pending entry if still pending, terminally remove on reject/stalled expiry, and call `ContinueProposalAfterSubject()` only on approve.

**Timer/scheduled batch pattern** (lines 1462-1580, 2729-2763):
```cpp
void ConsensusManager::ProcessCertificates()
{
    std::vector<ProposalState> to_process;
    {
        std::lock_guard lock( proposals_mutex_ );
        for ( auto &kv : proposals_ )
        {
            auto &state = kv.second;
            if ( !state.quorum_reached )
            {
                continue;
            }
            to_process.push_back( state );
        }
    }

    for ( auto &state : to_process )
    {
        auto subject_hash = GetSubjectHash( state.proposal.subject() );
        if ( subject_hash.has_value() && CheckCertificateForSubject( subject_hash.value() ) )
        {
            FireProposalCleanupCallbacks( state.proposal );
            ClearProposalSlot( state.proposal );
            continue;
        }
        ...
        (void)SubmitCertificate( certificate_result.value() );
        FireProposalCleanupCallbacks( state.proposal );
        ClearProposalSlot( state.proposal );
    }
}

void ConsensusManager::RecoverPendingCertificateWork()
{
    auto unfinished = certificate_work_journal_->ListUnfinished( CERT_KEY_PATTERN );
    for ( const auto &entry : unfinished )
    {
        ...
        CertificateReceived( { entry.key, value.value() }, std::string{} );
    }
}
```

Use the same collect-under-lock, process-outside-lock style for `ProcessDuePendingRetries()` and `ExpirePendingProposals()`.

**Certificate arrival event pattern** (lines 1666-1736):
```cpp
void ConsensusManager::CertificateReceived( crdt::CRDTCallbackManager::NewDataPair new_data,
                                            const std::string                     &cid )
{
    auto [key, value] = new_data;
    (void)cid;
    Certificate certificate;
    if ( !certificate.ParseFromArray( value.data(), value.size() ) )
    {
        ConsensusManagerLogger()->error( "{}: invalid certificate payload key={}", __func__, key );
        return;
    }

    auto subject_hash = GetSubjectHash( certificate.proposal().subject() );
    if ( subject_hash.has_error() )
    {
        return;
    }

    auto certificate_check = ValidateCertificate( certificate );
    if ( certificate_check == Check::Stalled )
    {
        certificate_work_journal_->MarkStalled( key );
        return;
    }

    registry_->OnFinalizedCertificate( certificate );
    ...
    auto certificate_handler_result = handler( subject_hash.value(), certificate );
    ...
    (void)certificate_work_journal_->MarkDone( key );
}
```

After successful certificate validation/finalization, wake `PendingDependencyKey::Certificate(subject_hash.value())`. Keep certificate work journal semantics separate from local pending proposal state.

**Cleanup pattern** (lines 2086-2135):
```cpp
void ConsensusManager::ClearProposalSlot( const Proposal &proposal )
{
    std::lock_guard lock( proposals_mutex_ );

    std::string slot_key;
    auto        it = proposals_.find( proposal.proposal_id() );
    if ( it != proposals_.end() )
    {
        slot_key = it->second.slot_key;
    }
    else
    {
        slot_key = GetSlotKey( proposal );
    }

    std::unordered_set<std::string> ids_to_remove;
    ids_to_remove.insert( proposal.proposal_id() );
    for ( const auto &kv : proposals_ )
    {
        if ( kv.second.slot_key == slot_key )
        {
            ids_to_remove.insert( kv.first );
        }
    }

    for ( const auto &proposal_id : ids_to_remove )
    {
        proposals_.erase( proposal_id );
        pending_proposals_.erase( proposal_id );
        pending_votes_.erase( proposal_id );
    }

    for ( auto it_hash = pending_by_subject_hash_.begin(); it_hash != pending_by_subject_hash_.end(); )
    {
        auto &vec = it_hash->second;
        vec.erase( std::remove_if( vec.begin(), vec.end(), [&]( const std::string &proposal_id )
                   { return ids_to_remove.find( proposal_id ) != ids_to_remove.end(); } ),
                   vec.end() );
        if ( vec.empty() )
        {
            it_hash = pending_by_subject_hash_.erase( it_hash );
        }
        else
        {
            ++it_hash;
        }
    }
```

Extend this cleanup to remove dependency indexes, retry schedule metadata, TTL state, byte counters, per-proposer counters, and any temporary transaction tracking. Prefer one helper (`RemovePendingProposal` or equivalent) used by approval, rejection, expiry, and certificate cleanup.

---

### `src/blockchain/Blockchain.hpp` (facade/route, request-response)

**Analog:** `src/blockchain/Blockchain.hpp`

**Imports and facade API pattern** (lines 19-24, 147-178, 233-257):
```cpp
#include "outcome/outcome.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/proto/delta.pb.h"
#include "account/GeniusAccount.hpp"
#include "blockchain/impl/proto/SGBlockchain.pb.h"
#include "blockchain/Consensus.hpp"

bool RegisterSubjectHandler( std::string_view subject_type, ConsensusManager::SubjectHandler handler );
void UnregisterSubjectHandler( std::string_view subject_type );
bool RegisterCertificateHandler( std::string_view subject_type,
                                 ConsensusManager::CertificateSubjectHandler handler );
void UnregisterCertificateHandler( std::string_view subject_type );
bool RegisterProposalCleanupHandler( std::string_view subject_type,
                                     ConsensusManager::ProposalCleanupHandler handler );

outcome::result<void> TryResumeProposal( const std::string &hash );
bool CheckCertificate( const std::string &subject_hash ) const;
outcome::result<ConsensusManager::Certificate> GetCertificateBySubjectHash(
    const std::string &subject_hash ) const;
```

Only expose pending lifecycle through `Blockchain` when callers actually need it. Keep implementation thin; the consensus manager owns dependency indexes, TTL, retry, and capacity.

---

### `src/blockchain/impl/Blockchain.cpp` (facade/route, request-response)

**Analog:** `src/blockchain/impl/Blockchain.cpp`

**Constructor wiring pattern** (lines 167-180):
```cpp
instance->consensus_manager_ = ConsensusManager::New(
    instance->validator_registry_,
    instance->db_,
    std::move( pubsub ),
    [weak_ptr( std::weak_ptr<Blockchain>( instance ) )](
        std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
    {
        if ( auto strong = weak_ptr.lock() )
        {
            return strong->account_->Sign( std::move( payload ) );
        }
        return outcome::failure( std::errc::owner_dead );
    },
    instance->account_->GetAddress() );
```

**Thin facade pattern** (lines 1669-1767):
```cpp
bool Blockchain::RegisterSubjectHandler( std::string_view subject_type, ConsensusManager::SubjectHandler handler )
{
    return consensus_manager_->RegisterSubjectHandler( subject_type, std::move( handler ) );
}

bool Blockchain::RegisterProposalCleanupHandler( std::string_view subject_type,
                                                 ConsensusManager::ProposalCleanupHandler handler )
{
    return consensus_manager_->RegisterProposalCleanupHandler( subject_type, std::move( handler ) );
}

outcome::result<void> Blockchain::TryResumeProposal( const std::string &hash )
{
    if ( consensus_manager_->CheckCertificateForSubject( hash ) )
    {
        return outcome::success();
    }
    return consensus_manager_->ResumeProposalHandling( hash );
}

outcome::result<ConsensusManager::Certificate> Blockchain::GetCertificateBySubjectHash(
    const std::string &subject_hash ) const
{
    return consensus_manager_->GetCertificateBySubjectHash( subject_hash );
}
```

If adding typed dependency resume to the facade, mirror this style exactly: no local storage, no extra policy, just delegate to `ConsensusManager`.

---

### `src/account/TransactionManager.hpp` (model/service interface, CRUD event-driven)

**Analog:** `src/account/TransactionManager.hpp`

**Imports pattern** (lines 10-34):
```cpp
#include <memory>
#include <deque>
#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <optional>

#include <boost/format.hpp>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/atomic_transaction.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "account/GeniusTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusInputValidator.hpp"
#include "account/InputValidators.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "base/logger.hpp"
#include "base/buffer.hpp"
#include "crypto/hasher.hpp"

#include "blockchain/Blockchain.hpp"
#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"
```

**Transaction status pattern** (lines 71-79):
```cpp
enum class TransactionStatus : uint8_t
{
    CREATED,
    SENDING,
    CONFIRMED,
    VERIFYING,
    FAILED,
    INVALID
};
```

Add `UNCONFIRMED` here. Use it only for inconclusive pending TTL of local outgoing transactions. Keep proven-invalid validation rejects on `FAILED`.

**Consensus/cleanup API pattern** (lines 465-473, 666-684):
```cpp
outcome::result<ConsensusManager::Check> OnConsensusCertificate( const std::string          &tx_hash,
                                                                 const ConsensusCertificate &certificate );
void OnProposalTimeoutCleanup( const std::string &tx_hash );

outcome::result<ConsensusManager::Check> HandleNonceConsensusSubject(
    const ConsensusManager::Subject &subject );
bool CheckTransactionReplayProtection( const GeniusTransaction &tx ) const;
outcome::result<void> ChangeTransactionState( const std::shared_ptr<GeniusTransaction> &tx,
                                              TransactionStatus                         new_status );
```

When `SubjectHandler` changes to `ValidationResult`, update both declarations together and keep `outcome::result` wrapping for infrastructure errors.

---

### `src/account/TransactionManager.cpp` (service, CRUD event-driven request-response)

**Analog:** `src/account/TransactionManager.cpp`

**Imports/logging pattern** (lines 7-33, 83-88):
```cpp
#include "account/TransactionManager.hpp"

#include <utility>
#include <thread>
#include <system_error>

#include <boost/asio/post.hpp>
#include <openssl/err.h>

#include <ProofSystem/EthereumKeyPairParams.hpp>
#include "TransferTransaction.hpp"
#include "MintTransaction.hpp"
#include "MintTransactionV2.hpp"
#include "MigrationTransaction.hpp"
#include "MigrationInputValidator.hpp"
#include "MigrationAllowList.hpp"
#include "EscrowTransaction.hpp"
#include "ProcessingTransaction.hpp"
#include "UTXOMerkle.hpp"
#include "account/TokenAmount.hpp"
#include "account/AccountMessenger.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crdt/proto/delta.pb.h"
#include "base/sgns_version.hpp"

base::Logger TransactionManagerLogger()
{
    return base::createLogger( "TransactionManager" );
}
```

**Consensus handler registration pattern** (lines 125-166):
```cpp
instance->blockchain_->RegisterCertificateHandler(
    NONCE_SUBJECT_TYPE,
    [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
        const std::string          &subject_hash,
        const ConsensusCertificate &certificate ) -> outcome::result<ConsensusManager::Check>
    {
        if ( auto strong = weak_ptr.lock() )
        {
            auto process_result = strong->OnConsensusCertificate( subject_hash, certificate );
            if ( process_result.has_error() )
            {
                TransactionManagerLogger()->error(
                    "[{} - full: {}] Failed to process certificate proposal_id={} error={}",
                    strong->account_m->GetAddress().substr( 0, 8 ),
                    strong->full_node_m,
                    certificate.proposal_id(),
                    process_result.error().message() );
            }
            return process_result;
        }
        return outcome::failure( std::errc::owner_dead );
    } );
instance->blockchain_->RegisterSubjectHandler(
    NONCE_SUBJECT_TYPE,
    [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
        const ConsensusManager::Subject &subject ) -> outcome::result<ConsensusManager::Check>
    {
        if ( auto strong = weak_ptr.lock() )
        {
            return strong->HandleNonceConsensusSubject( subject );
        }
        return outcome::failure( std::errc::owner_dead );
    } );
instance->blockchain_->RegisterProposalCleanupHandler(
    NONCE_SUBJECT_TYPE,
    [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )]( const std::string &tx_hash )
    {
        if ( auto strong = weak_ptr.lock() )
        {
            strong->OnProposalTimeoutCleanup( tx_hash );
        }
    } );
```

Update lambda return types when `ValidationResult` replaces `Check`. Preserve weak pointer ownership and `owner_dead` errors.

**Current timeout cleanup behavior to change** (lines 3615-3640):
```cpp
void TransactionManager::OnProposalTimeoutCleanup( const std::string &tx_hash )
{
    auto tx = GetTransactionByHash( tx_hash );
    if ( !tx )
    {
        return;
    }

    std::shared_lock tx_lock( tx_mutex_m );
    const auto       key = GetTransactionPath( *tx );
    auto             it  = tx_processed_m.find( key );
    if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::VERIFYING )
    {
        tx_lock.unlock();
        TransactionManagerLogger()->info(
            "[{} - full: {}] {}: Proposal timeout - transitioning temp entry to FAILED tx={}",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            __func__,
            tx_hash );
        (void) ChangeTransactionState( tx, TransactionStatus::FAILED );
    }
}
```

Replace the terminal state choice: local outgoing pending TTL -> `UNCONFIRMED`; remote embedded temporary `VERIFYING` -> remove temporary tracking entry without failure side effects.

**Nonce subject validation pattern** (lines 3889-4095):
```cpp
outcome::result<ConsensusManager::Check> TransactionManager::HandleNonceConsensusSubject(
    const ConsensusManager::Subject &subject )
{
    auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
    if ( nonce_subject.has_error() )
    {
        return outcome::failure( std::errc::invalid_argument );
    }

    const std::string tx_hash = nonce_subject.value().tx_hash();
    const auto        key     = GetTransactionPath( tx_hash );

    if ( nonce_subject.value().transaction().transaction_case() == EmbeddedTransaction::TRANSACTION_NOT_SET )
    {
        return ConsensusManager::Check::Reject;
    }

    auto tx_result = DeSerializeEmbeddedTransaction( nonce_subject.value().transaction() );
    if ( tx_result.has_error() )
    {
        return ConsensusManager::Check::Reject;
    }
    auto tx = tx_result.value();

    if ( tx->GetHash() != tx_hash )
    {
        return ConsensusManager::Check::Reject;
    }

    uint64_t          tracked_nonce  = tx->GetNonce();
    TransactionStatus tracked_status = TransactionStatus::VERIFYING;
    ...
    auto reject_and_maybe_fail_local = [&]( const char *reason ) -> ConsensusManager::Check
    {
        metrics_validation_reject_.fetch_add( 1, std::memory_order_relaxed );
        ...
        return ConsensusManager::Check::Reject;
    };
```

Keep this gate order: decode subject, deserialize embedded transaction, verify hash binding, create/inspect tracking entry, then run validation. For Phase 07, return `ValidationResult::Pending(Certificate(previous_hash))` where replay protection discovers a missing predecessor certificate.

**Replay-protection dependency source** (lines 4410-4438):
```cpp
bool TransactionManager::CheckTransactionReplayProtection( const GeniusTransaction &tx ) const
{
    if ( tx.GetNonce() > 0 )
    {
        const auto previous_hash = tx.GetPreviousHash();
        if ( previous_hash.empty() )
        {
            return false;
        }
        auto previous_cert_result = blockchain_->GetCertificateBySubjectHash( previous_hash );
        if ( previous_cert_result.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Missing previous certificate for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               previous_hash );
            return false;
        }
```

This is the exact spot to surface `PendingDependencyKey{Certificate, previous_hash}` instead of terminal `false`.

**State transition pattern** (lines 5085-5203, 5212-5273, 5281-5355):
```cpp
outcome::result<void> TransactionManager::ChangeTransactionState( const std::shared_ptr<GeniusTransaction> &tx,
                                                                  TransactionStatus new_status )
{
    switch ( new_status )
    {
        case TransactionStatus::CREATED:
        {
            std::unique_lock tx_lock( tx_mutex_m );
            const auto       key = GetTransactionPath( *tx );
            auto             it  = tx_processed_m.find( key );
            if ( it != tx_processed_m.end() )
            {
                return outcome::failure( std::errc::file_exists );
            }
            tx_processed_m.emplace( key, TrackedTx{ tx, TransactionStatus::CREATED, tx->GetNonce() } );
            metrics_tracking_insert_.fetch_add( 1, std::memory_order_relaxed );
        }
        break;
        case TransactionStatus::VERIFYING:
        {
            std::unique_lock tx_lock( tx_mutex_m );
            const auto       key = GetTransactionPath( *tx );
            tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::VERIFYING, tx->GetNonce() };
            tx_lock.unlock();
            BOOST_OUTCOME_TRY( blockchain_->TryResumeProposal( tx->GetHash() ) );
        }
        break;
        case TransactionStatus::CONFIRMED:
        {
            std::unique_lock tx_lock( tx_mutex_m );
            const auto       key = GetTransactionPath( *tx );
            tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::CONFIRMED, tx->GetNonce() };
            metrics_tracking_confirm_.fetch_add( 1, std::memory_order_relaxed );
            BOOST_OUTCOME_TRY( ParseTransaction( tx ) );
            account_m->SetPeerConfirmedNonce( tx->GetNonce(), tx->GetSrcAddress(), tx->GetHash() );
        }
        break;
        case TransactionStatus::INVALID:
        case TransactionStatus::FAILED:
        {
            std::unique_lock tx_lock( tx_mutex_m );
            const auto       key = GetTransactionPath( *tx );
            ...
            tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::FAILED, tx->GetNonce() };
            metrics_tracking_fail_.fetch_add( 1, std::memory_order_relaxed );
            account_m->ReleaseNonce( tx->GetNonce() );
        }
```

Add an `UNCONFIRMED` case that preserves retry possibility and does not run proven-failure rollback side effects blindly. Keep all map writes under `tx_mutex_m`.

---

### `test/src/blockchain/consensus_pending_lifecycle_test.cpp` (test, event-driven scheduled batch)

**Analogs:** `test/src/blockchain/consensus_certificate_test.cpp`, `test/src/blockchain/consensus_slot_key_test.cpp`, `test/src/blockchain/consensus_subject_test.cpp`

**Friend accessor pattern** (`consensus_certificate_test.cpp` lines 10-69):
```cpp
namespace sgns
{
    class ConsensusManagerTestAccess
    {
    public:
        static bool HasProposal( const std::shared_ptr<ConsensusManager> &manager, const std::string &proposal_id )
        {
            return manager && manager->proposals_.find( proposal_id ) != manager->proposals_.end();
        }

        static bool HasPendingProposal( const std::shared_ptr<ConsensusManager> &manager,
                                        const std::string                       &proposal_id )
        {
            return manager && manager->pending_proposals_.find( proposal_id ) != manager->pending_proposals_.end();
        }

        static void HandleProposal( const std::shared_ptr<ConsensusManager> &manager,
                                    const ConsensusManager::Proposal        &proposal )
        {
            manager->HandleProposal( proposal );
        }
    };
}
```

Extend this test accessor for pending dependency index counts, retained bytes, due retry processing, TTL expiry, and local self-vote counts. Add corresponding `friend class` entries in `Consensus.hpp` if needed, matching existing test access style.

**Manager fixture/setup pattern** (`consensus_certificate_test.cpp` lines 99-120, 492-518):
```cpp
std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry( const std::shared_ptr<sgns::crdt::GlobalDB> &db,
                                                       const std::shared_ptr<sgns::GeniusAccount>  &account )
{
    using sgns::ValidatorRegistry;
    auto registry = ValidatorRegistry::New(
        db,
        1,
        1,
        ValidatorRegistry::WeightConfig{},
        account->GetAddress(),
        []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
        { cb( outcome::failure( std::errc::not_supported ) ); } );
    EXPECT_TRUE( registry );
    auto store_result = registry->StoreGenesisRegistry( account->GetAddress(),
                                                        [account]( std::vector<uint8_t> payload )
                                                        { return account->Sign( std::move( payload ) ); } );
    ...
}

manager->RegisterSubjectHandler( NONCE_SUBJECT_TYPE,
                                 []( const ConsensusManager::Subject & )
                                 { return ConsensusManager::Check::Approve; } );
```

**Existing pending/resume test pattern** (`consensus_certificate_test.cpp` lines 542-581):
```cpp
TEST_F( ConsensusCertificateTest, ResumeProposalHandlingFromPending )
{
    auto account  = MakeAccount( getPathString() );
    auto registry = MakeRegistry( db_, account );
    auto manager  = MakeManager( registry, db_, pubs_, account );

    auto subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(), 5, "0x333435",
                                                                std::string{}, std::vector<uint8_t>{},
                                                                MakeTestCommitment(), MakeTestWitness() );
    auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                    account->GetAddress(),
                                                    registry->GetRegistryCid(),
                                                    registry->GetRegistryEpoch() );

    manager->RegisterSubjectHandler( NONCE_SUBJECT_TYPE,
                                     []( const ConsensusManager::Subject & )
                                     { return ConsensusManager::Check::Pending; } );
    ConsensusManagerTestAccess::HandleProposal( manager, proposal_result.value() );
    EXPECT_TRUE( ConsensusManagerTestAccess::HasPendingProposal( manager, proposal_result.value().proposal_id() ) );

    manager->RegisterSubjectHandler( NONCE_SUBJECT_TYPE,
                                     []( const ConsensusManager::Subject & )
                                     { return ConsensusManager::Check::Approve; } );

    auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject_result.value() );
    auto resume = manager->ResumeProposalHandling( nonce_subject.value().tx_hash() );
    EXPECT_FALSE( resume.has_error() );
    EXPECT_FALSE( ConsensusManagerTestAccess::HasPendingProposal( manager, proposal_result.value().proposal_id() ) );
    EXPECT_TRUE( ConsensusManagerTestAccess::HasProposal( manager, proposal_result.value().proposal_id() ) );
}
```

Use this as the base for typed dependency tests. Change the resume trigger from subject hash to `PendingDependencyKey::Certificate(previous_hash)` once implemented.

**Cleanup callback test style** (`consensus_subject_test.cpp` lines 968-991, 1001-1024):
```cpp
TEST( ConsensusSubjectTest, CleanupCallback_VerifyingEntryTransitionsToFailed )
{
    TestTrackingMap tracking;
    const std::string tx_hash = "tx-cleanup-01";
    tracking[tx_hash] = TestTrackingEntry{ TestTrackingEntry::Status::VERIFYING, 42 };

    sgns::ConsensusManager::ProposalCleanupHandler handler =
        [&tracking]( const std::string &hash )
    {
        auto it = tracking.find( hash );
        if ( it != tracking.end() && it->second.status == TestTrackingEntry::Status::VERIFYING )
        {
            it->second.status = TestTrackingEntry::Status::FAILED;
        }
    };

    handler( tx_hash );
    ASSERT_TRUE( tracking.find( tx_hash ) != tracking.end() );
    EXPECT_EQ( tracking[tx_hash].status, TestTrackingEntry::Status::FAILED );
}
```

Rewrite the new lifecycle tests around the new semantics: local outgoing TTL -> `UNCONFIRMED`, remote temporary -> erased, confirmed entries unaffected, and cleanup indexes/counters zeroed.

---

### `test/src/account/transaction_manager_pending_lifecycle_test.cpp` (test, CRUD event-driven)

**Analog:** `test/src/account/transaction_manager_certificate_fallback_test.cpp`

**Imports pattern** (lines 11-27):
```cpp
#include <gtest/gtest.h>
#include <chrono>
#include <string>

#include <boost/filesystem/operations.hpp>

#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "account/proto/SGTransaction.pb.h"
#include "crypto/hasher/hasher_impl.hpp"
#include <gsl/span>
#include "testutil/storage/base_crdt_test.hpp"
```

**TransactionManager friend accessor pattern** (lines 30-68):
```cpp
namespace sgns
{
    class CertificateFallbackTestAccess
    {
    public:
        static outcome::result<ConsensusManager::Check> OnConsensusCertificate(
            TransactionManager        &tm,
            const std::string         &tx_hash,
            const ConsensusCertificate &cert )
        {
            return tm.OnConsensusCertificate( tx_hash, cert );
        }

        static std::optional<TransactionManager::TrackedTx> GetTrackedTxByHash(
            TransactionManager &tm,
            const std::string  &hash )
        {
            return tm.GetTrackedTxByHash( hash );
        }
    };
}
```

Add methods for `HandleNonceConsensusSubject`, `OnProposalTimeoutCleanup`, and any new temp-entry removal helper.

**Fixture pattern** (lines 187-228):
```cpp
class CertificateFallbackTest : public test::CRDTFixture
{
public:
    CertificateFallbackTest() : CRDTFixture( "cert_fallback_test" )
    {
        account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
        assert( account_ != nullptr );

        auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
        assert( load_result.has_value() );

        blockchain_ = Blockchain::New(
            db_, account_, pubs_, []( outcome::result<void> ) {} );
        assert( blockchain_ != nullptr );

        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

        tm_ = TransactionManager::New(
            db_, io_, account_,
            std::make_shared<crypto::HasherImpl>(),
            blockchain_,
            false,
            kTimestampTolerance,
            kMutabilityWindow );
        assert( tm_ != nullptr );
    }

    std::shared_ptr<GeniusAccount>      account_;
    std::shared_ptr<Blockchain>         blockchain_;
    std::shared_ptr<TransactionManager> tm_;
};
```

Use this fixture for predecessor-certificate pending and status expiry tests. Prefer deterministic direct calls over sleeps.

**Idempotency test pattern** (lines 425-456):
```cpp
TEST_F( CertificateFallbackTest, MultipleCerts_SameTx_Idempotent )
{
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    const auto subject_a = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto cert_a    = BuildCertificate( subject_a, "proposal-multi-a" );

    const auto result_a =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert_a );
    ASSERT_TRUE( result_a.has_value() );
    EXPECT_EQ( result_a.value(), ConsensusManager::Check::Approve );

    const auto subject_b = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto cert_b    = BuildCertificate( subject_b, "proposal-multi-b" );

    const auto result_b =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert_b );
    ASSERT_TRUE( result_b.has_value() );
    EXPECT_EQ( result_b.value(), ConsensusManager::Check::Approve );

    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );
}
```

Copy this shape for "retry does not corrupt or duplicate transaction state" checks.

---

### `test/src/blockchain/CMakeLists.txt` (config, build registration)

**Analog:** `test/src/blockchain/CMakeLists.txt`

**Test registration pattern** (lines 6-12, 27-34):
```cmake
addtest(consensus_subject_test
    consensus_subject_test.cpp
)
target_link_libraries(consensus_subject_test
    blockchain_genesis
    rapidjson
)

addtest(consensus_slot_key_test
    consensus_slot_key_test.cpp
)
target_link_libraries(consensus_slot_key_test
    blockchain_genesis
    genius_node_test
    rapidjson
)
```

Add `consensus_pending_lifecycle_test` with the same `addtest` style. Link at least `blockchain_genesis`, and add `genius_node_test`, `rapidjson`, or `base_crdt_test` if the fixture needs them.

---

### `test/src/account/CMakeLists.txt` (config, build registration)

**Analog:** `test/src/account/CMakeLists.txt`

**Test registration and platform link pattern** (lines 13-33, 73-91):
```cmake
addtest(utxo_manager_test
    utxo_manager_test.cpp
)

target_link_libraries(utxo_manager_test
    genius_node_test
    json_secure_storage
    base_crdt_test
)

if(MSVC)
    target_link_options(chain_rpc_endpoint_provider_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(chain_rpc_endpoint_provider_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(chain_rpc_endpoint_provider_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
```

Add `transaction_manager_pending_lifecycle_test` beside existing account tests. If it constructs `TransactionManager` with `CRDTFixture`, link `genius_node_test`, `base_crdt_test`, and any storage target needed by the fixture.

## Shared Patterns

### Outcome-Based Errors
**Source:** `src/blockchain/Consensus.hpp` lines 102-105; `src/account/TransactionManager.hpp` lines 665-684  
**Apply to:** Consensus validation result API, transaction handler changes, facade methods.

```cpp
using SubjectHandler = std::function<outcome::result<Check>( const Subject &subject )>;
outcome::result<ConsensusManager::Check> HandleNonceConsensusSubject(
    const ConsensusManager::Subject &subject );
outcome::result<void> ChangeTransactionState( const std::shared_ptr<GeniusTransaction> &tx,
                                              TransactionStatus                         new_status );
```

Do not throw exceptions for lifecycle outcomes. Use `outcome::result` for infrastructure failure and `ValidationResult`/status values for domain outcomes.

### Locking and Copy-Out
**Source:** `src/blockchain/Consensus.cpp` lines 1462-1481; `src/blockchain/Consensus.cpp` lines 361-385  
**Apply to:** Pending retry scan, TTL scan, dependency wakeups, cleanup callback dispatch.

```cpp
std::vector<ProposalState> to_process;
{
    std::lock_guard lock( proposals_mutex_ );
    for ( auto &kv : proposals_ )
    {
        auto &state = kv.second;
        if ( !state.quorum_reached )
        {
            continue;
        }
        to_process.push_back( state );
    }
}

std::vector<ProposalCleanupHandler> handlers_copy;
{
    std::shared_lock lock( cleanup_handlers_mutex_ );
    auto it = proposal_cleanup_handlers_.find( proposal.subject().subject_type_hash().hash() );
    if ( it != proposal_cleanup_handlers_.end() )
    {
        handlers_copy = it->second;
    }
}
```

Collect under lock and process outside where handler calls or expensive work can re-enter manager state.

### Idempotent Voting
**Source:** `src/blockchain/Consensus.hpp` lines 472-493; `src/blockchain/Consensus.cpp` lines 1389-1460  
**Apply to:** All pending retry paths.

```cpp
std::unordered_set<std::string> seen_voters;
bool voted = false;
...
if ( subject_result.value() == Check::Pending )
{
    AddPendingProposal( proposal, subject_hash_result.value() );
    continue;
}

ContinueProposalAfterSubject( proposal );
```

Retry must route through the same helper that handles first approval. Do not emit votes directly from dependency wakeups or timer scans.

### Certificate Dependency Wakeup
**Source:** `src/blockchain/Consensus.cpp` lines 1666-1736; `src/account/TransactionManager.cpp` lines 4410-4438  
**Apply to:** `Certificate(tx_hash)` dependency indexing and retry.

```cpp
auto subject_hash = GetSubjectHash( certificate.proposal().subject() );
...
registry_->OnFinalizedCertificate( certificate );
...
auto previous_cert_result = blockchain_->GetCertificateBySubjectHash( previous_hash );
if ( previous_cert_result.has_error() )
{
    return false;
}
```

Missing predecessor certificate should become `Pending(Certificate(previous_hash))`. Certificate arrival should wake all proposals indexed by `Certificate(subject_hash)`.

### Transaction State Semantics
**Source:** `src/account/TransactionManager.cpp` lines 5085-5355  
**Apply to:** `UNCONFIRMED`, timeout cleanup, remote temp removal.

```cpp
case TransactionStatus::VERIFYING:
{
    tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::VERIFYING, tx->GetNonce() };
    tx_lock.unlock();
    BOOST_OUTCOME_TRY( blockchain_->TryResumeProposal( tx->GetHash() ) );
}
break;
case TransactionStatus::FAILED:
{
    tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::FAILED, tx->GetNonce() };
    metrics_tracking_fail_.fetch_add( 1, std::memory_order_relaxed );
    account_m->ReleaseNonce( tx->GetNonce() );
}
```

Keep `FAILED` for proven invalidity. `UNCONFIRMED` should avoid failure-only rollback/release behavior unless implementation explicitly proves that side effect is correct for local inconclusive expiry.

### No Wire Schema Change
**Source:** `src/blockchain/impl/proto/Consensus.proto` from research; `src/blockchain/Consensus.hpp` lines 93-109  
**Apply to:** Pending dependencies, retry metadata, capacity state.

Pending dependency keys are local-only runtime state. Do not add pending fields to proposal, vote, or certificate protobuf messages for this phase.

## No Analog Found

None. Every planned file has an exact or role-match analog in the current codebase.

## Metadata

**Analog search scope:** `src/blockchain`, `src/blockchain/impl`, `src/account`, `test/src/blockchain`, `test/src/account`, relevant `CMakeLists.txt` files.  
**Files scanned:** 19 focused files plus phase artifacts.  
**Pattern extraction date:** 2026-06-16

