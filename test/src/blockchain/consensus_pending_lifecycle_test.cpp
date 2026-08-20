/**
 * @file       consensus_pending_lifecycle_test.cpp
 * @brief      Focused harness for consensus pending proposal lifecycle tests.
 * @details    Provides the Wave 0 target for PEND-01 through PEND-07 so
 *             later implementation plans can add D-01 through D-12 and D-16
 *             behavior cases without changing CTest wiring.
 * @date       2026-06-16
 */

#include <gtest/gtest.h>

#include "account/GeniusAccount.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgns
{
    /**
     * @brief Test-only helper surface for future private pending lifecycle access.
     * @details Later implementation tasks can extend this accessor when
     *          ConsensusManager exposes local pending metadata, typed dependency
     *          indexes, retry scheduling, and expiry cleanup.
     */
    class ConsensusPendingLifecycleTestAccess
    {
    public:
        static constexpr const char *Scope()
        {
            return "consensus pending lifecycle";
        }

        static bool HasProposal( const std::shared_ptr<ConsensusManager> &manager, const std::string &proposal_id )
        {
            return manager && manager->proposals_.find( proposal_id ) != manager->proposals_.end();
        }

        static bool HasPendingProposal( const std::shared_ptr<ConsensusManager> &manager,
                                        const std::string                       &proposal_id )
        {
            return manager && manager->pending_entries_.find( proposal_id ) != manager->pending_entries_.end();
        }

        static bool LocalVoteCastForProposal( const std::shared_ptr<ConsensusManager> &manager,
                                              const std::string                       &proposal_id )
        {
            if ( !manager )
            {
                return false;
            }
            auto proposal_it = manager->proposals_.find( proposal_id );
            if ( proposal_it == manager->proposals_.end() )
            {
                return false;
            }
            auto slot_it = manager->slot_states_.find( proposal_it->second.slot_key );
            return slot_it != manager->slot_states_.end() &&
                   slot_it->second.voted_proposal_ids.find( proposal_id ) != slot_it->second.voted_proposal_ids.end();
        }

        static void HandleProposal( const std::shared_ptr<ConsensusManager> &manager,
                                    const ConsensusManager::Proposal        &proposal )
        {
            manager->HandleProposal( proposal );
        }

        static void HandleCertificate( const std::shared_ptr<ConsensusManager> &manager,
                                       const ConsensusManager::Certificate     &certificate )
        {
            manager->HandleCertificate( certificate );
        }

        static std::optional<std::vector<crdt::pb::Element>> FilterCertificate(
            const std::shared_ptr<ConsensusManager> &manager,
            const crdt::pb::Element                 &element )
        {
            return manager->FilterCertificate( element );
        }

        static void CertificateReceived( const std::shared_ptr<ConsensusManager> &manager,
                                         crdt::CRDTCallbackManager::NewDataPair   new_data )
        {
            manager->CertificateReceived( std::move( new_data ), std::string{} );
        }

        static void RecoverPendingCertificateWork( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->RecoverPendingCertificateWork();
        }

        static bool HasStalledCertificateWork( const std::shared_ptr<ConsensusManager> &manager,
                                               const std::string                       &key )
        {
            auto entry = manager->certificate_work_journal_->GetEntry( key );
            return entry.has_value() && entry->state == crdt::CRDTWorkJournal::State::Stalled;
        }

        static bool HasCertificateWork( const std::shared_ptr<ConsensusManager> &manager, const std::string &key )
        {
            return manager->certificate_work_journal_->GetEntry( key ).has_value();
        }

        static outcome::result<bool> HasAcceptedCertificateForSlot( const std::shared_ptr<ConsensusManager> &manager,
                                                                     const std::string                       &slot_key )
        {
            return manager->HasAcceptedCertificateForSlot( slot_key );
        }

        static void SetAcceptedCertificateScanFailure( const std::shared_ptr<ConsensusManager> &manager, bool fail )
        {
            manager->fail_accepted_certificate_scan_for_test_ = fail;
        }

        static void WriteLiveLegacyCertificate( const std::shared_ptr<ConsensusManager> &manager,
                                                const ConsensusManager::Certificate     &certificate )
        {
            auto subject_hash = ConsensusManager::GetSubjectHash( certificate.proposal().subject() );
            ASSERT_TRUE( subject_hash.has_value() );
            std::string serialized;
            ASSERT_TRUE( certificate.SerializeToString( &serialized ) );
            crdt::GlobalDB::Buffer value;
            value.put( serialized );
            ASSERT_TRUE( manager->db_->Put( { std::string( "/cert/" ) + subject_hash.value() }, value, {} ).has_value() );
        }

        static ConsensusManager::Proposal ResignWithLaterTimestamp(
            const std::shared_ptr<sgns::GeniusAccount> &account,
            ConsensusManager::Proposal                   proposal )
        {
            proposal.set_timestamp( proposal.timestamp() + 1 );
            proposal.clear_signature();
            proposal.set_proposal_id( ConsensusManager::CreateProposalId( proposal ) );
            auto bytes = ConsensusManager::ProposalSigningBytes( proposal );
            EXPECT_TRUE( bytes.has_value() );
            auto signature = account->Sign( bytes.value() );
            proposal.set_signature( signature.data(), signature.size() );
            return proposal;
        }

        static std::string GetSlotKey( const ConsensusManager::Proposal &proposal )
        {
            return ConsensusManager::GetSlotKey( proposal );
        }

        static std::string GetExpectedCertificateSlotKey( const ConsensusManager::Certificate &certificate )
        {
            return ConsensusManager::GetExpectedCertificateSlotKey( certificate );
        }

        static outcome::result<std::string> GetSubjectHash( const ConsensusManager::Subject &subject )
        {
            return ConsensusManager::GetSubjectHash( subject );
        }

        static bool AddPendingProposal( const std::shared_ptr<ConsensusManager>  &manager,
                                        const ConsensusManager::Proposal         &proposal,
                                        const std::string                        &subject_hash,
                                        const ConsensusManager::ValidationResult &validation_result )
        {
            return manager->AddPendingProposal( proposal, subject_hash, validation_result );
        }

        static bool RemovePendingProposal( const std::shared_ptr<ConsensusManager> &manager,
                                           const std::string                       &proposal_id,
                                           std::string_view                         reason )
        {
            return manager->RemovePendingProposal( proposal_id, reason );
        }

        static ConsensusManager::PendingLifecycleConfig PendingConfig(
            const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager->pending_config_;
        }

        static std::size_t PendingEntryCount( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager ? manager->pending_entries_.size() : 0U;
        }

        static std::size_t PendingDependencyWaiterCount( const std::shared_ptr<ConsensusManager>      &manager,
                                                         const ConsensusManager::PendingDependencyKey &dependency )
        {
            if ( !manager )
            {
                return 0;
            }
            auto it = manager->pending_by_dependency_.find( dependency );
            return it == manager->pending_by_dependency_.end() ? 0U : it->second.size();
        }

        static std::size_t PendingRetainedBytes( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager ? manager->pending_retained_bytes_ : 0U;
        }

        static std::size_t PendingCountForProposer( const std::shared_ptr<ConsensusManager> &manager,
                                                    const std::string                       &proposer_id )
        {
            if ( !manager )
            {
                return 0;
            }
            auto it = manager->pending_count_by_proposer_.find( proposer_id );
            return it == manager->pending_count_by_proposer_.end() ? 0U : it->second;
        }

        static std::chrono::milliseconds PendingTtlForProposal( const std::shared_ptr<ConsensusManager> &manager,
                                                                const std::string                       &proposal_id )
        {
            auto it = manager->pending_entries_.find( proposal_id );
            if ( it == manager->pending_entries_.end() )
            {
                return std::chrono::milliseconds( 0 );
            }
            return std::chrono::duration_cast<std::chrono::milliseconds>( it->second.expires_at -
                                                                          it->second.admitted_at );
        }

        static std::chrono::milliseconds PendingRetryDelayForProposal( const std::shared_ptr<ConsensusManager> &manager,
                                                                       const std::string &proposal_id )
        {
            auto it = manager->pending_entries_.find( proposal_id );
            if ( it == manager->pending_entries_.end() )
            {
                return std::chrono::milliseconds( 0 );
            }
            return std::chrono::duration_cast<std::chrono::milliseconds>( it->second.next_retry_at -
                                                                          it->second.admitted_at );
        }

        static std::size_t PendingScheduledRetryCount( const std::shared_ptr<ConsensusManager> &manager,
                                                       const std::string                       &proposal_id )
        {
            auto it = manager->pending_entries_.find( proposal_id );
            return it == manager->pending_entries_.end() ? 0U : it->second.scheduled_retry_count;
        }

        static void ForcePendingRetryDue( const std::shared_ptr<ConsensusManager> &manager,
                                          const std::string                       &proposal_id )
        {
            auto it = manager->pending_entries_.find( proposal_id );
            ASSERT_TRUE( it != manager->pending_entries_.end() );
            it->second.next_retry_at = std::chrono::steady_clock::now() - std::chrono::milliseconds( 1 );
        }

        static void ForcePendingExpired( const std::shared_ptr<ConsensusManager> &manager,
                                         const std::string                       &proposal_id )
        {
            auto it = manager->pending_entries_.find( proposal_id );
            ASSERT_TRUE( it != manager->pending_entries_.end() );
            it->second.expires_at = std::chrono::steady_clock::now() - std::chrono::milliseconds( 1 );
        }

        static void ProcessDuePendingRetries( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->ProcessDuePendingRetries();
        }

        static void ProcessCertificates( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->ProcessCertificates();
        }

        static void Close( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->Close();
        }

        static bool CertificatesPending( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager && manager->certificates_pending_.load();
        }

        static void AddQuorumReachedProposal( const std::shared_ptr<ConsensusManager> &manager,
                                              const ConsensusManager::Proposal        &proposal )
        {
            ConsensusManager::ProposalState state;
            state.proposal             = proposal;
            state.slot_key             = ConsensusManager::GetSlotKey( proposal );
            state.quorum_reached       = true;
            state.quorum_reached_ts_ms = 0;

            manager->proposals_[proposal.proposal_id()] = std::move( state );
            manager->certificates_pending_.store( true );
        }

        static void ExpirePendingProposals( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->ExpirePendingProposals();
        }

        static void ContinueProposalAfterSubject( const std::shared_ptr<ConsensusManager> &manager,
                                                  const ConsensusManager::Proposal        &proposal )
        {
            manager->ContinueProposalAfterSubject( proposal );
        }

        static void ForceCandidateWindowDue( const std::shared_ptr<ConsensusManager> &manager,
                                             const std::string                       &slot_key )
        {
            auto it = manager->slot_states_.find( slot_key );
            ASSERT_TRUE( it != manager->slot_states_.end() );
            it->second.candidate_deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds( 1 );
        }

        static void ProcessDueVoteWork( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->ProcessDueVoteWork();
        }

        static void SetActiveVotePersistenceFailure( const std::shared_ptr<ConsensusManager> &manager, bool fail )
        {
            manager->fail_active_vote_persistence_for_test_ = fail;
        }

        static void SetActiveVoteRemovalFailure( const std::shared_ptr<ConsensusManager> &manager, bool fail )
        {
            manager->fail_active_vote_removal_for_test_ = fail;
        }

        static const std::vector<std::string> &ActiveVoteAnnouncements( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager->active_vote_announcements_for_test_;
        }

        static void ClearActiveVoteAnnouncements( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->active_vote_announcements_for_test_.clear();
        }

        static bool HasActiveVoteLock( const std::shared_ptr<ConsensusManager> &manager, const std::string &slot_key )
        {
            auto it = manager->slot_states_.find( slot_key );
            return it != manager->slot_states_.end() && it->second.active_vote_locked;
        }

        static bool HasAcceptedCertificateScanPending( const std::shared_ptr<ConsensusManager> &manager,
                                                       const std::string                       &slot_key )
        {
            auto it = manager->slot_states_.find( slot_key );
            return it != manager->slot_states_.end() && it->second.certificate_scan_pending;
        }

        static std::string ActiveVoteStorageKey( const std::shared_ptr<ConsensusManager> &manager,
                                                 const std::string                       &slot_key )
        {
            return manager->ActiveVoteStorageKey( slot_key );
        }

        static std::optional<std::string> ReadActiveVoteRecord( const std::shared_ptr<ConsensusManager> &manager,
                                                                 const std::string                       &slot_key )
        {
            auto store = manager->db_ ? manager->db_->GetDataStore() : nullptr;
            if ( !store )
            {
                return std::nullopt;
            }
            sgns::crdt::GlobalDB::Buffer key;
            key.put( manager->ActiveVoteStorageKey( slot_key ) );
            auto value = store->get( key );
            return value.has_value() ? std::optional<std::string>( std::string( value.value().toString() ) )
                                     : std::nullopt;
        }

        static void WriteActiveVoteRecord( const std::shared_ptr<ConsensusManager> &manager,
                                           const std::string                       &slot_key,
                                           const std::string                       &bytes )
        {
            auto store = manager->db_ ? manager->db_->GetDataStore() : nullptr;
            ASSERT_TRUE( store );
            sgns::crdt::GlobalDB::Buffer key;
            key.put( manager->ActiveVoteStorageKey( slot_key ) );
            sgns::crdt::GlobalDB::Buffer value;
            value.put( bytes );
            ASSERT_TRUE( store->put( key, value ).has_value() );
        }

        static void ForceActiveVoteRetryDue( const std::shared_ptr<ConsensusManager> &manager,
                                             const std::string                       &slot_key )
        {
            auto it = manager->active_votes_.find( slot_key );
            ASSERT_TRUE( it != manager->active_votes_.end() );
            it->second.next_retry_at = std::chrono::steady_clock::now() - std::chrono::milliseconds( 1 );
        }

        static void ForceActiveVoteExpired( const std::shared_ptr<ConsensusManager> &manager,
                                            const std::string                       &slot_key )
        {
            auto it = manager->active_votes_.find( slot_key );
            ASSERT_TRUE( it != manager->active_votes_.end() );
            it->second.acceptance_deadline_ms = 1;
        }

        static std::vector<ConsensusManager::Proposal> TakePendingProposals(
            const std::shared_ptr<ConsensusManager> &manager,
            const std::string                       &subject_hash )
        {
            return manager->TakePendingProposals( subject_hash );
        }
    };
} // namespace sgns

namespace
{
    constexpr std::array<const char *, 12> kConsensusPendingBehaviors = {
        "D-01 structured deferred validation result",
        "D-02 local-only pending dependencies",
        "D-03 typed Certificate dependency key",
        "D-04 retry on any dependency arrival",
        "D-05 scheduled transient retry backoff",
        "D-06 dependency-triggered retry throttle",
        "D-07 idempotent retry approval",
        "D-08 global pending count limit",
        "D-09 per-proposer pending count limit",
        "D-10 retained-byte admission limit",
        "D-11 local capacity refusal without reject vote",
        "D-12 pending TTL expiry" };
    const std::string kValidatorId = "validator-pending-lifecycle";
    constexpr const char *kBindingPrivateKey = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    std::vector<uint8_t> DummySignature( std::vector<uint8_t> )
    {
        return std::vector<uint8_t>{ 0x07, 0x02 };
    }

    class ConsensusPendingLifecycleTest : public test::CRDTFixture
    {
    public:
        ConsensusPendingLifecycleTest() : CRDTFixture( "consensus_pending_lifecycle_test" )
        {
        }

    protected:
        void SetUp() override
        {
            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
                { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
        }

        static std::vector<std::string> PendingBehaviorNames()
        {
            return std::vector<std::string>( kConsensusPendingBehaviors.begin(), kConsensusPendingBehaviors.end() );
        }

        std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry()
        {
            auto registry = sgns::ValidatorRegistry::New(
                db_,
                1,
                1,
                sgns::ValidatorRegistry::WeightConfig{},
                kValidatorId,
                []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
                { cb( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );

            auto store_result = registry->StoreGenesisRegistry( kValidatorId, DummySignature );
            EXPECT_FALSE( store_result.has_error() );

            ASSERT_WAIT_FOR_CONDITION(
                [&registry]()
                {
                    auto load = registry->LoadCurrentRegistry();
                    return load.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "registry initialized",
                nullptr );

            return registry;
        }

        std::shared_ptr<sgns::ConsensusManager> MakeManager( const std::shared_ptr<sgns::ValidatorRegistry> &registry,
                                                             const std::string &local_id = kValidatorId )
        {
            auto manager = sgns::ConsensusManager::New(
                registry,
                db_,
                pubs_,
                []( std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
                { return DummySignature( std::move( payload ) ); },
                local_id );
            EXPECT_TRUE( manager );
            return manager;
        }

        std::shared_ptr<sgns::GeniusAccount> MakeSigningAccount()
        {
            auto account = sgns::GeniusAccount::NewFromPrivateKey(
                sgns::TokenID::FromBytes( { 0x00 } ), kBindingPrivateKey, getPathString(), false );
            EXPECT_TRUE( account );
            return account;
        }

        std::shared_ptr<sgns::ValidatorRegistry> MakeSigningRegistry(
            const std::shared_ptr<sgns::GeniusAccount> &account )
        {
            auto registry = sgns::ValidatorRegistry::New(
                db_,
                1,
                1,
                sgns::ValidatorRegistry::WeightConfig{},
                account->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
                { cb( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );

            auto store_result = registry->StoreGenesisRegistry(
                account->GetAddress(),
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
            EXPECT_FALSE( store_result.has_error() );

            ASSERT_WAIT_FOR_CONDITION(
                [&registry]()
                {
                    auto load = registry->LoadCurrentRegistry();
                    return load.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "signing registry initialized",
                nullptr );
            return registry;
        }

        std::shared_ptr<sgns::ConsensusManager> MakeSigningManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            const std::shared_ptr<sgns::GeniusAccount>     &account )
        {
            auto manager = sgns::ConsensusManager::New(
                registry,
                db_,
                pubs_,
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
                account->GetAddress() );
            EXPECT_TRUE( manager );
            return manager;
        }

        static sgns::UTXOTransitionCommitment MakeTestCommitment()
        {
            sgns::UTXOTransitionCommitment commitment;
            auto                          *consumed = commitment.add_consumed_outpoints();
            consumed->set_tx_id_hash( std::string( 32, '\x01' ) );
            consumed->set_output_index( 0 );
            auto *produced = commitment.add_produced_outputs();
            produced->set_tx_id_hash( std::string( 32, '\x02' ) );
            produced->set_output_index( 0 );
            produced->set_owner_address( "owner" );
            produced->set_token_id( std::string( 32, '\x03' ) );
            produced->set_amount( 1 );
            commitment.set_consumed_outpoints_root( std::string( 32, '\x05' ) );
            commitment.set_produced_outputs_root( std::string( 32, '\x04' ) );
            return commitment;
        }

        static sgns::UTXOWitness MakeTestWitness()
        {
            return sgns::UTXOWitness{};
        }

        sgns::ConsensusManager::Proposal MakeProposal( const std::shared_ptr<sgns::ConsensusManager>  &manager,
                                                       const std::shared_ptr<sgns::ValidatorRegistry> &registry,
                                                       uint64_t                                        nonce,
                                                       const std::string                              &account )
        {
            auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kValidatorId,
                                                                              nonce,
                                                                              account,
                                                                              sgns::EmbeddedTransaction{},
                                                                              MakeTestCommitment(),
                                                                              MakeTestWitness() );
            EXPECT_TRUE( subject_result.has_value() );

            auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                            kValidatorId,
                                                            registry->GetRegistryCid(),
                                                            registry->GetRegistryEpoch() );
            EXPECT_TRUE( proposal_result.has_value() );
            return proposal_result.value();
        }
    };
} // namespace

TEST( ConsensusPendingLifecycleContractTest, HarnessIsDiscoverable )
{
    /**
     * Given a dedicated consensus pending lifecycle CTest target,
     * When the current Wave 0 harness is discovered,
     * Then it exposes the future behavior slots for PEND-01 through PEND-07
     * and avoids bridge or EVM RPC concerns.
     */
    const auto behaviors = std::vector<std::string>( kConsensusPendingBehaviors.begin(),
                                                     kConsensusPendingBehaviors.end() );

    ASSERT_EQ( behaviors.size(), kConsensusPendingBehaviors.size() );
    EXPECT_STREQ( sgns::ConsensusPendingLifecycleTestAccess::Scope(), "consensus pending lifecycle" );
    EXPECT_FALSE( sgns::NONCE_SUBJECT_TYPE.empty() );
}

TEST( ConsensusPendingLifecycleContractTest, ValidationResultPreservesTerminalChecks )
{
    /**
     * Given the local structured validation result contract,
     * When terminal outcomes are constructed,
     * Then each result preserves the legacy consensus check value without
     * adding local pending dependency metadata.
     */
    const auto approve = sgns::ConsensusManager::ValidationResult::Approve();
    const auto reject  = sgns::ConsensusManager::ValidationResult::Reject();
    const auto stalled = sgns::ConsensusManager::ValidationResult::Stalled();

    EXPECT_EQ( approve.check, sgns::ConsensusManager::Check::Approve );
    EXPECT_TRUE( approve.dependencies.empty() );
    EXPECT_FALSE( approve.retry_after.has_value() );

    EXPECT_EQ( reject.check, sgns::ConsensusManager::Check::Reject );
    EXPECT_TRUE( reject.dependencies.empty() );

    EXPECT_EQ( stalled.check, sgns::ConsensusManager::Check::Stalled );
    EXPECT_TRUE( stalled.dependencies.empty() );
}

TEST( ConsensusPendingLifecycleContractTest, PendingResultCarriesTypedCertificateDependencyAndRetryMetadata )
{
    /**
     * Given a proposal waiting for a predecessor certificate,
     * When Pending is constructed with a Certificate dependency key,
     * Then the typed dependency and optional retry metadata stay local to the
     * structured result.
     */
    using PendingDependencyKey = sgns::ConsensusManager::PendingDependencyKey;

    const auto dependency = PendingDependencyKey::Certificate( "tx-previous-cert" );
    const auto pending = sgns::ConsensusManager::ValidationResult::Pending( { dependency }, std::chrono::seconds( 2 ) );

    ASSERT_EQ( pending.check, sgns::ConsensusManager::Check::Pending );
    ASSERT_EQ( pending.dependencies.size(), 1U );
    EXPECT_EQ( pending.dependencies.front().type, PendingDependencyKey::Type::Certificate );
    EXPECT_EQ( pending.dependencies.front().value, "tx-previous-cert" );
    ASSERT_TRUE( pending.retry_after.has_value() );
    EXPECT_EQ( pending.retry_after.value(), std::chrono::seconds( 2 ) );
}

TEST( ConsensusPendingLifecycleContractTest, PendingDependencyKeySupportsHashIdentity )
{
    /**
     * Given typed dependency keys are used as local pending indexes,
     * When identical and different Certificate keys are inserted into an
     * unordered map,
     * Then identical keys address the same entry and different values stay
     * isolated.
     */
    using PendingDependencyKey     = sgns::ConsensusManager::PendingDependencyKey;
    using PendingDependencyKeyHash = sgns::ConsensusManager::PendingDependencyKeyHash;

    std::unordered_map<PendingDependencyKey, int, PendingDependencyKeyHash> index;
    index.emplace( PendingDependencyKey::Certificate( "tx-a" ), 1 );
    index[PendingDependencyKey::Certificate( "tx-a" )] += 1;
    index.emplace( PendingDependencyKey::Certificate( "tx-b" ), 7 );

    EXPECT_EQ( index.size(), 2U );
    EXPECT_EQ( index.at( PendingDependencyKey::Certificate( "tx-a" ) ), 2 );
    EXPECT_EQ( index.at( PendingDependencyKey::Certificate( "tx-b" ) ), 7 );
}

TEST( ConsensusPendingLifecycleContractTest, PendingLifecycleConfigDefaultsToThreeMinuteBoundedPool )
{
    const sgns::ConsensusManager::PendingLifecycleConfig config;

    EXPECT_EQ( config.max_pending_proposals, 1024U );
    EXPECT_EQ( config.max_pending_per_proposer, 64U );
    EXPECT_EQ( config.max_retained_pending_bytes, 64ULL * 1024ULL * 1024ULL );
    EXPECT_EQ( config.pending_ttl, std::chrono::minutes( 3 ) );
}

TEST_F( ConsensusPendingLifecycleTest, QuorumCertificateWorkClearsWhenLocalNodeNotInProposalRegistry )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );

    auto observer_manager = MakeManager( registry, "observer-not-validator" );
    ASSERT_TRUE( observer_manager );

    auto proposal = MakeProposal( observer_manager, registry, 9, "0xobserver-quorum" );

    sgns::ConsensusPendingLifecycleTestAccess::AddQuorumReachedProposal( observer_manager, proposal );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( observer_manager, proposal.proposal_id() ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::CertificatesPending( observer_manager ) );

    sgns::ConsensusPendingLifecycleTestAccess::ProcessCertificates( observer_manager );

    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( observer_manager, proposal.proposal_id() ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::CertificatesPending( observer_manager ) );
    sgns::ConsensusPendingLifecycleTestAccess::Close( observer_manager );
}

TEST_F( ConsensusPendingLifecycleTest, CertificateIngressRejectsMismatchedLegacyKeyBeforeEffects )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    const std::string tx_hash = "0xcertificate-binding";
    auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        account->GetAddress(), 71, tx_hash, sgns::EmbeddedTransaction{}, MakeTestCommitment(), MakeTestWitness() );
    ASSERT_TRUE( subject_result.has_value() );
    auto proposal_result = manager->CreateProposal(
        subject_result.value(), account->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( proposal_result.has_value() );
    auto vote_result = manager->CreateVote(
        proposal_result.value().proposal_id(),
        account->GetAddress(),
        true,
        [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
    ASSERT_TRUE( vote_result.has_value() );
    auto certificate_result = manager->CreateCertificate( proposal_result.value(), { vote_result.value() } );
    ASSERT_TRUE( certificate_result.has_value() );

    const auto &certificate = certificate_result.value();
    const auto canonical_slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( certificate.proposal() );
    EXPECT_EQ( std::string( "/cert/" ) + canonical_slot,
               sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( certificate ) );

    auto subject_hash = sgns::ConsensusPendingLifecycleTestAccess::GetSubjectHash( certificate.proposal().subject() );
    ASSERT_TRUE( subject_hash.has_value() );

    std::string serialized;
    ASSERT_TRUE( certificate.SerializeToString( &serialized ) );
    sgns::crdt::pb::Element matching_element;
    matching_element.set_key( std::string( "/cert/" ) + subject_hash.value() );
    matching_element.set_value( serialized );
    sgns::crdt::pb::Element mismatched_element;
    mismatched_element.set_key( "/cert/not-the-subject-hash" );
    mismatched_element.set_value( serialized );

    std::atomic<int> handler_calls{ 0 };
    ASSERT_TRUE( manager->RegisterSubjectHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const sgns::ConsensusManager::Subject & )
        { return sgns::ConsensusManager::ValidationResult::Approve(); } ) );
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&handler_calls]( const std::string &, const sgns::ConsensusManager::Certificate & )
        {
            ++handler_calls;
            return outcome::success( sgns::ConsensusManager::Check::Approve );
        } ) );
    sgns::ConsensusPendingLifecycleTestAccess::HandleProposal( manager, certificate.proposal() );
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( manager, certificate.proposal_id() ) );

    // Key-aware CRDT ingress accepts a certificate only when the supplied legacy key matches its subject.
    auto matching_filter = sgns::ConsensusPendingLifecycleTestAccess::FilterCertificate( manager, matching_element );
    EXPECT_FALSE( matching_filter.has_value() );
    sgns::base::Buffer matching_buffer;
    matching_buffer.put( serialized );
    sgns::ConsensusPendingLifecycleTestAccess::CertificateReceived(
        manager,
        sgns::crdt::CRDTCallbackManager::NewDataPair{ matching_element.key(), std::move( matching_buffer ) } );
    EXPECT_EQ( handler_calls.load(), 0 );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( manager, certificate.proposal_id() ) );

    auto filter_result = sgns::ConsensusPendingLifecycleTestAccess::FilterCertificate( manager, mismatched_element );
    ASSERT_TRUE( filter_result.has_value() );
    EXPECT_TRUE( filter_result->empty() );
    sgns::base::Buffer serialized_buffer;
    serialized_buffer.put( serialized );
    sgns::ConsensusPendingLifecycleTestAccess::CertificateReceived(
        manager,
        sgns::crdt::CRDTCallbackManager::NewDataPair{ mismatched_element.key(), std::move( serialized_buffer ) } );
    EXPECT_EQ( handler_calls.load(), 0 );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( manager, certificate.proposal_id() ) );

    // Keyless pubsub ingress accepts the same exactly-bound certificate without inventing a storage key.
    sgns::ConsensusPendingLifecycleTestAccess::HandleCertificate( manager, certificate );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( manager, certificate.proposal_id() ) );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, UnavailableRegistryDoesNotAllowMalformedCertificateCleanup )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        account->GetAddress(), 72, "0xunavailable-registry", sgns::EmbeddedTransaction{}, MakeTestCommitment(), MakeTestWitness() );
    ASSERT_TRUE( subject_result.has_value() );
    auto proposal_result = manager->CreateProposal(
        subject_result.value(), account->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( proposal_result.has_value() );
    auto vote_result = manager->CreateVote(
        proposal_result.value().proposal_id(),
        account->GetAddress(),
        true,
        [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
    ASSERT_TRUE( vote_result.has_value() );
    auto certificate_result = manager->CreateCertificate( proposal_result.value(), { vote_result.value() } );
    ASSERT_TRUE( certificate_result.has_value() );

    auto malformed = certificate_result.value();
    malformed.set_registry_cid( "unavailable-registry-cid" );
    malformed.mutable_proposal()->set_registry_cid( "unavailable-registry-cid" );

    auto subject_hash = sgns::ConsensusPendingLifecycleTestAccess::GetSubjectHash( malformed.proposal().subject() );
    ASSERT_TRUE( subject_hash.has_value() );
    std::string serialized;
    ASSERT_TRUE( malformed.SerializeToString( &serialized ) );
    sgns::crdt::pb::Element element;
    element.set_key( std::string( "/cert/" ) + subject_hash.value() );
    element.set_value( serialized );

    auto filter_result = sgns::ConsensusPendingLifecycleTestAccess::FilterCertificate( manager, element );
    ASSERT_TRUE( filter_result.has_value() );
    EXPECT_TRUE( filter_result->empty() );

    std::atomic<int> handler_calls{ 0 };
    ASSERT_TRUE( manager->RegisterSubjectHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const sgns::ConsensusManager::Subject & )
        { return sgns::ConsensusManager::ValidationResult::Approve(); } ) );
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&handler_calls]( const std::string &, const sgns::ConsensusManager::Certificate & )
        {
            ++handler_calls;
            return outcome::success( sgns::ConsensusManager::Check::Approve );
        } ) );
    sgns::ConsensusPendingLifecycleTestAccess::HandleProposal( manager, certificate_result.value().proposal() );
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( manager, certificate_result.value().proposal_id() ) );

    sgns::ConsensusPendingLifecycleTestAccess::HandleCertificate( manager, malformed );
    EXPECT_EQ( handler_calls.load(), 0 );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( manager, certificate_result.value().proposal_id() ) );

    sgns::base::Buffer serialized_buffer;
    serialized_buffer.put( serialized );
    sgns::ConsensusPendingLifecycleTestAccess::CertificateReceived(
        manager,
        sgns::crdt::CRDTCallbackManager::NewDataPair{ element.key(), std::move( serialized_buffer ) } );
    EXPECT_EQ( handler_calls.load(), 0 );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( manager, certificate_result.value().proposal_id() ) );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, BoundedPendingPoolIndexesDependenciesAndCleansAccounting )
{
    using PendingDependencyKey = sgns::ConsensusManager::PendingDependencyKey;

    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );

    sgns::ConsensusManager::PendingLifecycleConfig config;
    config.pending_ttl = std::chrono::seconds( 10 );
    manager->SetPendingLifecycleConfig( config );

    auto       proposal    = MakeProposal( manager, registry, 11, "0xpending-index" );
    auto       proposal_id = proposal.proposal_id();
    const auto dep_a       = PendingDependencyKey::Certificate( "cert-a" );
    const auto dep_b       = PendingDependencyKey::Certificate( "cert-b" );

    const bool admitted = sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal(
        manager,
        proposal,
        "subject-hash",
        sgns::ConsensusManager::ValidationResult::Pending( { dep_a, dep_b }, std::chrono::seconds( 2 ) ) );

    EXPECT_TRUE( admitted );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingEntryCount( manager ), 1U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingDependencyWaiterCount( manager, dep_a ), 1U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingDependencyWaiterCount( manager, dep_b ), 1U );
    EXPECT_GT( sgns::ConsensusPendingLifecycleTestAccess::PendingRetainedBytes( manager ), 0U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCountForProposer( manager, kValidatorId ), 1U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingTtlForProposal( manager, proposal_id ),
               std::chrono::seconds( 10 ) );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingRetryDelayForProposal( manager, proposal_id ),
               std::chrono::seconds( 2 ) );

    EXPECT_TRUE(
        sgns::ConsensusPendingLifecycleTestAccess::RemovePendingProposal( manager, proposal_id, "test-reset" ) );

    config.max_pending_proposals    = 1;
    config.max_pending_per_proposer = 1;
    config.pending_ttl              = std::chrono::seconds( 10 );
    manager->SetPendingLifecycleConfig( config );

    auto first  = MakeProposal( manager, registry, 21, "0xcapacity-first" );
    auto second = MakeProposal( manager, registry, 22, "0xcapacity-second" );

    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal(
        manager,
        first,
        "subject-first",
        sgns::ConsensusManager::ValidationResult::Pending() ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal(
        manager,
        second,
        "subject-second",
        sgns::ConsensusManager::ValidationResult::Pending() ) );

    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingEntryCount( manager ), 1U );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, first.proposal_id() ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, second.proposal_id() ) );
    EXPECT_FALSE(
        sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, second.proposal_id() ) );

    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::RemovePendingProposal( manager,
                                                                                   first.proposal_id(),
                                                                                   "test-reset" ) );

    config.max_pending_proposals    = 2;
    config.max_pending_per_proposer = 1;
    config.pending_ttl              = std::chrono::seconds( 10 );
    manager->SetPendingLifecycleConfig( config );

    auto remove_first  = MakeProposal( manager, registry, 31, "0xremove-first" );
    auto remove_second = MakeProposal( manager, registry, 32, "0xremove-second" );
    auto remove_third  = MakeProposal( manager, registry, 33, "0xremove-third" );
    remove_first.set_proposer_id( "validator-a" );
    remove_second.set_proposer_id( "validator-b" );
    remove_third.set_proposer_id( "validator-a" );

    const auto dependency = PendingDependencyKey::Certificate( "shared-cert" );
    const auto pending    = sgns::ConsensusManager::ValidationResult::Pending( { dependency } );

    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal( manager,
                                                                                remove_first,
                                                                                "subject-first",
                                                                                pending ) );
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal( manager,
                                                                                remove_second,
                                                                                "subject-second",
                                                                                pending ) );

    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingDependencyWaiterCount( manager, dependency ), 2U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCountForProposer( manager, "validator-a" ), 1U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCountForProposer( manager, "validator-b" ), 1U );

    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::RemovePendingProposal( manager,
                                                                                   remove_first.proposal_id(),
                                                                                   "test-remove" ) );

    EXPECT_FALSE(
        sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, remove_first.proposal_id() ) );
    EXPECT_TRUE(
        sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, remove_second.proposal_id() ) );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingDependencyWaiterCount( manager, dependency ), 1U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCountForProposer( manager, "validator-a" ), 0U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCountForProposer( manager, "validator-b" ), 1U );

    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal( manager,
                                                                                remove_third,
                                                                                "subject-third",
                                                                                pending ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, remove_third.proposal_id() ) );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingEntryCount( manager ), 2U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingDependencyWaiterCount( manager, dependency ), 2U );

    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::RemovePendingProposal( manager,
                                                                                   remove_second.proposal_id(),
                                                                                   "test-reset" ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::RemovePendingProposal( manager,
                                                                                   remove_third.proposal_id(),
                                                                                   "test-reset" ) );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingEntryCount( manager ), 0U );

    manager->SetPendingLifecycleConfig( sgns::ConsensusManager::PendingLifecycleConfig{} );

    auto retry_proposal    = MakeProposal( manager, registry, 41, "0xpending-retry" );
    auto retry_proposal_id = retry_proposal.proposal_id();
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal(
        manager,
        retry_proposal,
        "0xpending-retry",
        sgns::ConsensusManager::ValidationResult::Pending(
            { sgns::ConsensusManager::PendingDependencyKey::Certificate( "0xprevious" ) } ) ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, retry_proposal_id ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, retry_proposal_id ) );

    const auto retry_pending = sgns::ConsensusPendingLifecycleTestAccess::TakePendingProposals( manager, "0xprevious" );
    ASSERT_EQ( retry_pending.size(), 1U );
    EXPECT_EQ( retry_pending.front().proposal_id(), retry_proposal_id );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, retry_proposal_id ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, retry_proposal_id ) );

    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, retry_pending.front() );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( manager, retry_proposal_id ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, retry_proposal_id ) );

    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, retry_proposal );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, retry_proposal_id ) );

    config                               = sgns::ConsensusManager::PendingLifecycleConfig{};
    config.pending_ttl                   = std::chrono::seconds( 10 );
    config.min_dependency_retry_interval = std::chrono::milliseconds( 0 );
    manager->SetPendingLifecycleConfig( config );

    std::unordered_map<std::string, int>                                                            handler_attempts;
    std::unordered_map<std::string, std::function<sgns::ConsensusManager::ValidationResult( int )>> handler_scripts;
    ASSERT_TRUE( manager->RegisterSubjectHandler( sgns::NONCE_SUBJECT_TYPE,
                                                  [&]( const sgns::ConsensusManager::Subject &subject )
                                                      -> outcome::result<sgns::ConsensusManager::ValidationResult>
                                                  {
                                                      auto nonce_subject = sgns::ConsensusManager::DecodeNonceSubject(
                                                          subject );
                                                      if ( nonce_subject.has_error() )
                                                      {
                                                          return outcome::failure( nonce_subject.error() );
                                                      }
                                                      const auto &tx_hash = nonce_subject.value().tx_hash();
                                                      auto       &attempt = handler_attempts[tx_hash];
                                                      ++attempt;
                                                      auto script_it = handler_scripts.find( tx_hash );
                                                      if ( script_it == handler_scripts.end() )
                                                      {
                                                          return sgns::ConsensusManager::ValidationResult::Approve();
                                                      }
                                                      return script_it->second( attempt );
                                                  } ) );

    auto       dependency_proposal    = MakeProposal( manager, registry, 51, "0xdependency-tx2" );
    auto       dependency_proposal_id = dependency_proposal.proposal_id();
    const auto predecessor_key        = PendingDependencyKey::Certificate( "0xdependency-tx1" );
    const auto wrong_key              = PendingDependencyKey::Certificate( "0xdependency-tx2" );
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal(
        manager,
        dependency_proposal,
        "0xdependency-tx2",
        sgns::ConsensusManager::ValidationResult::Pending( { predecessor_key } ) ) );

    ASSERT_TRUE( manager->WakePendingDependency( wrong_key ).has_value() );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, dependency_proposal_id ) );
    EXPECT_EQ( handler_attempts["0xdependency-tx2"], 0 );
    EXPECT_FALSE(
        sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, dependency_proposal_id ) );

    ASSERT_TRUE( manager->WakePendingDependency( predecessor_key ).has_value() );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, dependency_proposal_id ) );
    EXPECT_EQ( handler_attempts["0xdependency-tx2"], 1 );
    EXPECT_FALSE(
        sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, dependency_proposal_id ) );

    auto       multi_proposal      = MakeProposal( manager, registry, 52, "0xmulti-dep" );
    auto       multi_proposal_id   = multi_proposal.proposal_id();
    const auto multi_a             = PendingDependencyKey::Certificate( "0xmulti-a" );
    const auto multi_b             = PendingDependencyKey::Certificate( "0xmulti-b" );
    handler_scripts["0xmulti-dep"] = [&]( int attempt )
    {
        if ( attempt == 1 )
        {
            return sgns::ConsensusManager::ValidationResult::Pending( { multi_b } );
        }
        return sgns::ConsensusManager::ValidationResult::Approve();
    };
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal(
        manager,
        multi_proposal,
        "0xmulti-dep",
        sgns::ConsensusManager::ValidationResult::Pending( { multi_a, multi_b } ) ) );

    ASSERT_TRUE( manager->WakePendingDependency( multi_a ).has_value() );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, multi_proposal_id ) );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingDependencyWaiterCount( manager, multi_a ), 0U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingDependencyWaiterCount( manager, multi_b ), 1U );

    ASSERT_TRUE( manager->WakePendingDependency( multi_b ).has_value() );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, multi_proposal_id ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, multi_proposal_id ) );

    config                               = sgns::ConsensusManager::PendingLifecycleConfig{};
    config.pending_ttl                   = std::chrono::seconds( 10 );
    config.min_dependency_retry_interval = std::chrono::seconds( 10 );
    config.scheduled_retry_delays        = { std::chrono::seconds( 1 ),
                                             std::chrono::seconds( 2 ),
                                             std::chrono::seconds( 5 ),
                                             std::chrono::seconds( 10 ) };
    manager->SetPendingLifecycleConfig( config );

    auto       scheduled_proposal    = MakeProposal( manager, registry, 53, "0xscheduled" );
    auto       scheduled_proposal_id = scheduled_proposal.proposal_id();
    const auto scheduled_dep         = PendingDependencyKey::Certificate( "0xscheduled-dep" );
    handler_scripts["0xscheduled"]   = [&]( int )
    { return sgns::ConsensusManager::ValidationResult::Pending( { scheduled_dep } ); };
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal(
        manager,
        scheduled_proposal,
        "0xscheduled",
        sgns::ConsensusManager::ValidationResult::Pending( { scheduled_dep } ) ) );
    EXPECT_EQ(
        sgns::ConsensusPendingLifecycleTestAccess::PendingRetryDelayForProposal( manager, scheduled_proposal_id ),
        std::chrono::seconds( 1 ) );

    const std::array<std::chrono::seconds, 4> expected_delays = { std::chrono::seconds( 2 ),
                                                                  std::chrono::seconds( 5 ),
                                                                  std::chrono::seconds( 10 ),
                                                                  std::chrono::seconds( 10 ) };
    for ( std::size_t i = 0; i < expected_delays.size(); ++i )
    {
        sgns::ConsensusPendingLifecycleTestAccess::ForcePendingRetryDue( manager, scheduled_proposal_id );
        sgns::ConsensusPendingLifecycleTestAccess::ProcessDuePendingRetries( manager );
        EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, scheduled_proposal_id ) );
        EXPECT_EQ(
            sgns::ConsensusPendingLifecycleTestAccess::PendingScheduledRetryCount( manager, scheduled_proposal_id ),
            i + 1 );
        EXPECT_EQ(
            sgns::ConsensusPendingLifecycleTestAccess::PendingRetryDelayForProposal( manager, scheduled_proposal_id ),
            expected_delays[i] );
    }

    const auto scheduled_attempts_after_dependency = handler_attempts["0xscheduled"];
    ASSERT_TRUE( manager->WakePendingDependency( scheduled_dep ).has_value() );
    EXPECT_EQ( handler_attempts["0xscheduled"], scheduled_attempts_after_dependency );
    ASSERT_TRUE( manager->WakePendingDependency( scheduled_dep ).has_value() );
    EXPECT_EQ( handler_attempts["0xscheduled"], scheduled_attempts_after_dependency );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, scheduled_proposal_id ) );

    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::RemovePendingProposal( manager,
                                                                                   scheduled_proposal_id,
                                                                                   "test-reset" ) );

    int cleanup_count = 0;
    ASSERT_TRUE( manager->RegisterProposalCleanupHandler( sgns::NONCE_SUBJECT_TYPE,
                                                          [&]( const std::string &tx_hash )
                                                          {
                                                              if ( tx_hash == "0xttl" )
                                                              {
                                                                  ++cleanup_count;
                                                              }
                                                          } ) );

    auto ttl_proposal    = MakeProposal( manager, registry, 54, "0xttl" );
    auto ttl_proposal_id = ttl_proposal.proposal_id();
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal(
        manager,
        ttl_proposal,
        "0xttl",
        sgns::ConsensusManager::ValidationResult::Pending( { PendingDependencyKey::Certificate( "0xttl-dep" ) } ) ) );
    sgns::ConsensusPendingLifecycleTestAccess::ForcePendingExpired( manager, ttl_proposal_id );
    sgns::ConsensusPendingLifecycleTestAccess::ExpirePendingProposals( manager );
    sgns::ConsensusPendingLifecycleTestAccess::ExpirePendingProposals( manager );

    EXPECT_EQ( cleanup_count, 1 );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, ttl_proposal_id ) );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingEntryCount( manager ), 0U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingRetainedBytes( manager ), 0U );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, ActiveVoteFreezesEligibleCandidatesBeforePublishingOneWinner )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto winner = MakeProposal( manager, registry, 81, "0xfrozen-slot" );
    winner.set_proposal_id( "a-frozen-winner" );
    auto contender = winner;
    contender.set_proposal_id( "z-frozen-contender" );
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( winner );

    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, contender );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, winner );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );

    sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );

    ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).size(), 1U );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( manager, slot ) );
    auto record_bytes = sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot );
    ASSERT_TRUE( record_bytes.has_value() );
    sgns::ActiveVoteRecord record;
    ASSERT_TRUE( record.ParseFromString( record_bytes.value() ) );
    EXPECT_EQ( record.canonical_slot(), slot );
    EXPECT_EQ( record.vote_bytes(), sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).front() );
    EXPECT_GT( record.acceptance_deadline_ms(), 0U );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, winner.proposal_id() ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, contender.proposal_id() ) );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, ActiveVoteWriteFailureCannotPublishOrCreateALock )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto proposal = MakeProposal( manager, registry, 82, "0xwrite-failure" );
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( proposal );
    sgns::ConsensusPendingLifecycleTestAccess::SetActiveVotePersistenceFailure( manager, true );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, proposal );
    sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );

    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( manager, slot ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, proposal.proposal_id() ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, ActiveVoteRetriesAndRestartsWithOnlyItsStoredSignedBytes )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto proposal = MakeProposal( manager, registry, 83, "0xretry-exact" );
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( proposal );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, proposal );
    sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    auto bytes = sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot );
    ASSERT_TRUE( bytes.has_value() );
    sgns::ActiveVoteRecord record;
    ASSERT_TRUE( record.ParseFromString( bytes.value() ) );

    sgns::ConsensusPendingLifecycleTestAccess::ClearActiveVoteAnnouncements( manager );
    sgns::ConsensusPendingLifecycleTestAccess::ForceActiveVoteRetryDue( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).size(), 1U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).front(), record.vote_bytes() );

    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
    auto restarted = MakeSigningManager( registry, account );
    ASSERT_TRUE( restarted );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( restarted, slot ) );
    sgns::ConsensusPendingLifecycleTestAccess::ClearActiveVoteAnnouncements( restarted );
    sgns::ConsensusPendingLifecycleTestAccess::ForceActiveVoteRetryDue( restarted, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( restarted );
    ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( restarted ).size(), 1U );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( restarted ).front(), record.vote_bytes() );
    sgns::ConsensusPendingLifecycleTestAccess::Close( restarted );
}

TEST_F( ConsensusPendingLifecycleTest, CorruptOrExpiredActiveVoteCannotAuthorizeAReplacement )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto corrupt = MakeProposal( manager, registry, 84, "0xcorrupt-record" );
    const auto corrupt_slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( corrupt );
    sgns::ConsensusPendingLifecycleTestAccess::WriteActiveVoteRecord( manager, corrupt_slot, "not-a-protobuf-record" );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, corrupt );
    sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( manager, corrupt_slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( manager, corrupt_slot ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );

    auto proposal = MakeProposal( manager, registry, 85, "0xexpired-record" );
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( proposal );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, proposal );
    sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( manager, slot ) );
    sgns::ConsensusPendingLifecycleTestAccess::ClearActiveVoteAnnouncements( manager );
    sgns::ConsensusPendingLifecycleTestAccess::ForceActiveVoteExpired( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ForceActiveVoteRetryDue( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( manager, slot ) );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, CertificateCallbackStallsUntilPostCommitReadbackCanReleaseSameSlot )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto subject = sgns::ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                86,
                                                                "0xdurable-certificate-release",
                                                                sgns::EmbeddedTransaction{},
                                                                MakeTestCommitment(),
                                                                MakeTestWitness() );
    ASSERT_TRUE( subject.has_value() );
    auto voted_proposal_result = manager->CreateProposal(
        subject.value(), account->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( voted_proposal_result.has_value() );
    auto voted_proposal = voted_proposal_result.value();
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( voted_proposal );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, voted_proposal );
    sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );

    auto certified_proposal = sgns::ConsensusPendingLifecycleTestAccess::ResignWithLaterTimestamp( account, voted_proposal );
    ASSERT_NE( certified_proposal.proposal_id(), voted_proposal.proposal_id() );
    auto certified_vote = manager->CreateVote(
        certified_proposal.proposal_id(), account->GetAddress(), true,
        [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
    ASSERT_TRUE( certified_vote.has_value() );
    auto certificate = manager->CreateCertificate( certified_proposal, { certified_vote.value() } );
    ASSERT_TRUE( certificate.has_value() );
    auto subject_hash = sgns::ConsensusPendingLifecycleTestAccess::GetSubjectHash( certified_proposal.subject() );
    ASSERT_TRUE( subject_hash.has_value() );
    const auto legacy_key = std::string( "/cert/" ) + subject_hash.value();
    std::string serialized;
    ASSERT_TRUE( certificate.value().SerializeToString( &serialized ) );

    // The CRDT callback occurs before its batch commits, so it can only retain stalled work.
    sgns::base::Buffer callback_value;
    callback_value.put( serialized );
    sgns::ConsensusPendingLifecycleTestAccess::CertificateReceived( manager, { legacy_key, std::move( callback_value ) } );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasStalledCertificateWork( manager, legacy_key ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( manager, slot ) );

    // A pre-commit/missing readback must remain retryable and cannot unlock the slot.
    sgns::ConsensusPendingLifecycleTestAccess::RecoverPendingCertificateWork( manager );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasStalledCertificateWork( manager, legacy_key ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );

    // A durable, accepted certificate for another canonical slot cannot release this lock.
    auto other_subject = sgns::ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                      87,
                                                                      "0xdurable-certificate-other-slot",
                                                                      sgns::EmbeddedTransaction{},
                                                                      MakeTestCommitment(),
                                                                      MakeTestWitness() );
    ASSERT_TRUE( other_subject.has_value() );
    auto other_proposal = manager->CreateProposal(
        other_subject.value(), account->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( other_proposal.has_value() );
    auto other_vote = manager->CreateVote(
        other_proposal.value().proposal_id(), account->GetAddress(), true,
        [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
    ASSERT_TRUE( other_vote.has_value() );
    auto other_certificate = manager->CreateCertificate( other_proposal.value(), { other_vote.value() } );
    ASSERT_TRUE( other_certificate.has_value() );
    auto other_subject_hash = sgns::ConsensusPendingLifecycleTestAccess::GetSubjectHash( other_proposal.value().subject() );
    ASSERT_TRUE( other_subject_hash.has_value() );
    const auto other_legacy_key = std::string( "/cert/" ) + other_subject_hash.value();
    std::string other_serialized;
    ASSERT_TRUE( other_certificate.value().SerializeToString( &other_serialized ) );
    sgns::base::Buffer other_callback_value;
    other_callback_value.put( other_serialized );
    sgns::ConsensusPendingLifecycleTestAccess::CertificateReceived(
        manager, { other_legacy_key, std::move( other_callback_value ) } );
    sgns::ConsensusPendingLifecycleTestAccess::WriteLiveLegacyCertificate( manager, other_certificate.value() );
    sgns::ConsensusPendingLifecycleTestAccess::RecoverPendingCertificateWork( manager );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasStalledCertificateWork( manager, other_legacy_key ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasCertificateWork( manager, other_legacy_key ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );

    sgns::ConsensusPendingLifecycleTestAccess::WriteLiveLegacyCertificate( manager, certificate.value() );
    // A keyless pubsub delivery is not a durable release boundary.
    sgns::ConsensusPendingLifecycleTestAccess::HandleCertificate( manager, certificate.value() );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );

    // Even after successful readback, a local deletion failure retains the exact lock/work for retry.
    sgns::ConsensusPendingLifecycleTestAccess::SetActiveVoteRemovalFailure( manager, true );
    sgns::ConsensusPendingLifecycleTestAccess::RecoverPendingCertificateWork( manager );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasStalledCertificateWork( manager, legacy_key ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );
    sgns::ConsensusPendingLifecycleTestAccess::SetActiveVoteRemovalFailure( manager, false );
    sgns::ConsensusPendingLifecycleTestAccess::RecoverPendingCertificateWork( manager );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( manager, slot ) );
    auto accepted_certificate = sgns::ConsensusPendingLifecycleTestAccess::HasAcceptedCertificateForSlot( manager, slot );
    ASSERT_TRUE( accepted_certificate.has_value() );
    EXPECT_TRUE( accepted_certificate.value() );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );

    // A transient scan failure after durable same-slot finality is indeterminate,
    // never proof that the finalized slot can receive a replacement local vote.
    auto scan_failed = MakeSigningManager( registry, account );
    ASSERT_TRUE( scan_failed );
    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( scan_failed, true );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( scan_failed, voted_proposal );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( scan_failed );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasAcceptedCertificateScanPending( scan_failed, slot ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( scan_failed ).empty() );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( scan_failed, slot ).has_value() );
    sgns::ConsensusPendingLifecycleTestAccess::Close( scan_failed );

    // A reconstructed manager only scans existing legacy values; it does not create
    // a certificate key or cast another vote for a finalized canonical slot.
    auto restarted = MakeSigningManager( registry, account );
    ASSERT_TRUE( restarted );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( restarted, voted_proposal );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( restarted );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( restarted, slot ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( restarted ).empty() );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( restarted, slot ).has_value() );
    sgns::ConsensusPendingLifecycleTestAccess::Close( restarted );
}
