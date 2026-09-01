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
#include "account/MintTransactionV2.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

#include <atomic>
#include <array>
#include <boost/filesystem.hpp>
#include <chrono>
#include <condition_variable>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

        static ConsensusManager::Check ValidateCertificate( const std::shared_ptr<ConsensusManager> &manager,
                                                             const ConsensusManager::Certificate     &certificate )
        {
            return manager->ValidateCertificate( certificate );
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

        static void WriteLiveCertificate( const std::shared_ptr<ConsensusManager> &manager,
                                          const ConsensusManager::Certificate     &certificate )
        {
            const auto key = ConsensusManager::GetExpectedCertificateSlotKey( certificate );
            ASSERT_FALSE( key.empty() );
            std::string serialized;
            ASSERT_TRUE( certificate.SerializeToString( &serialized ) );
            crdt::GlobalDB::Buffer value;
            value.put( serialized );
            ASSERT_TRUE( manager->db_->PutConvergentImmutable( { key }, value, {} ).has_value() );
        }

        // Production-path slot write (mirrors WriteLiveCertificate / Consensus.cpp SubmitCertificate):
        // the convergent-immutable certificate slot key must be written through PutConvergentImmutable
        // so the stored value converges instead of relying on same-priority CRDT overwrite ordering.
        static void WriteConvergentCertificateAtKey( const std::shared_ptr<ConsensusManager> &manager,
                                                     const std::string                       &key,
                                                     const std::string                       &serialized )
        {
            crdt::GlobalDB::Buffer value;
            value.put( serialized );
            ASSERT_TRUE( manager->db_->PutConvergentImmutable( { key }, value, {} ).has_value() );
        }

        static void WriteCertificateAtKey( const std::shared_ptr<ConsensusManager> &manager,
                                           const std::string                       &key,
                                           const std::string                       &serialized )
        {
            crdt::GlobalDB::Buffer value;
            value.put( serialized );
            ASSERT_TRUE( manager->db_->Put( { key }, value, {} ).has_value() );
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

        static void SetTimerWorkHook( const std::shared_ptr<ConsensusManager> &manager, std::function<void()> hook )
        {
            std::lock_guard lock( manager->timer_mutex_ );
            manager->timer_work_hook_for_test_ = std::move( hook );
        }

        static void RequestTimerWork( const std::shared_ptr<ConsensusManager> &manager )
        {
            {
                std::lock_guard lock( manager->timer_mutex_ );
                manager->certificates_pending_.store( true );
            }
            manager->timer_cv_.notify_all();
        }

        static bool IsRoundTimerJoinable( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->close_mutex_ );
            return manager->round_timer_.joinable();
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

        static std::vector<std::string> ActiveVoteAnnouncements( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->active_vote_announcements_for_test_;
        }

        static void ClearActiveVoteAnnouncements( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
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

        static std::size_t PendingCertificateScanCandidateCount( const std::shared_ptr<ConsensusManager> &manager,
                                                                  const std::string                       &slot_key )
        {
            auto it = manager->slot_states_.find( slot_key );
            return it == manager->slot_states_.end() ? 0U : it->second.scan_pending_candidates.size();
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

    std::string SerializedCertificateHash( std::string_view serialized )
    {
        const auto hash = sgns::crypto::sha2_256( serialized.data(), serialized.size() );
        return sgns::base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
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

        struct MultiValidatorNode
        {
            std::string                                      path;
            std::shared_ptr<boost::asio::io_context>         io;
            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
            std::shared_ptr<sgns::crdt::GlobalDB>            db;
            std::shared_ptr<sgns::GeniusAccount>             account;
            std::shared_ptr<sgns::ValidatorRegistry>         registry;
            std::shared_ptr<sgns::ConsensusManager>          manager;
        };

        MultiValidatorNode MakeMultiValidatorNode( size_t index, const std::string &private_key )
        {
            const auto path = getPathString() + "/multi-validator-" + std::to_string( index );
            boost::filesystem::create_directories( path );

            MultiValidatorNode node;
            node.path    = path;
            node.io      = std::make_shared<boost::asio::io_context>();
            auto keypair = sgns::crdt::KeyPairFileStorage( path + "/keypair" ).GetKeyPair();
            if ( keypair.has_error() )
            {
                ADD_FAILURE() << "failed to create multi-validator keypair";
                return node;
            }
            node.pubsub = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keypair.value() );
            if ( !node.pubsub )
            {
                ADD_FAILURE() << "failed to create multi-validator pubsub";
                return node;
            }
            const auto start_result = node.pubsub->Start( static_cast<uint16_t>( 54001U + index ),
                                                          { node.pubsub->GetLocalAddress() } )
                                          .get();
            if ( start_result )
            {
                ADD_FAILURE() << "failed to start multi-validator pubsub: " << start_result.message();
                return node;
            }

            auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( node.io ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            auto graphsync = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( node.pubsub->GetHost(),
                                                                                            scheduler );
            auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
            auto db_result = sgns::crdt::GlobalDB::New( node.io,
                                                        path + "/rocksdb",
                                                        node.pubsub,
                                                        sgns::crdt::CrdtOptions::DefaultOptions(),
                                                        graphsync,
                                                        scheduler,
                                                        generator );
            if ( db_result.has_error() )
            {
                ADD_FAILURE() << "failed to create multi-validator GlobalDB: " << db_result.error().message();
                return node;
            }
            node.db = std::move( db_result.value() );
            node.db->Start();

            node.account = sgns::GeniusAccount::NewFromPrivateKey(
                sgns::TokenID::FromBytes( { 0x00 } ), private_key.c_str(), path + "/account", false );
            if ( !node.account )
            {
                ADD_FAILURE() << "failed to create multi-validator signing account";
            }
            return node;
        }

        static sgns::ValidatorRegistry::RegistryUpdate MakeThreeValidatorRegistryUpdate(
            const std::array<std::shared_ptr<sgns::GeniusAccount>, 3> &accounts )
        {
            sgns::ValidatorRegistry::RegistryUpdate update;
            update.mutable_registry()->set_epoch( 1 );
            for ( const auto &account : accounts )
            {
                auto *validator = update.mutable_registry()->add_validators();
                validator->set_validator_id( account->GetAddress() );
                validator->set_weight( 1 );
                validator->set_role( sgns::ValidatorRegistry::Role::REGULAR );
                validator->set_status( sgns::ValidatorRegistry::Status::ACTIVE );
            }

            sgns::validator::RegistrySigningPayload payload;
            *payload.mutable_registry() = update.registry();
            payload.set_prev_registry_hash( update.prev_registry_hash() );
            std::string serialized;
            EXPECT_TRUE( payload.SerializeToString( &serialized ) );
            auto signature = accounts.front()->Sign( std::vector<uint8_t>( serialized.begin(), serialized.end() ) );
            auto *entry    = update.add_signatures();
            entry->set_validator_id( accounts.front()->GetAddress() );
            entry->set_signature( signature.data(), signature.size() );
            return update;
        }

        static std::shared_ptr<sgns::ValidatorRegistry> MakeMultiValidatorRegistry(
            const MultiValidatorNode                                &node,
            const sgns::ValidatorRegistry::RegistryUpdate           &update,
            const std::string                                       &genesis_authority )
        {
            auto registry = sgns::ValidatorRegistry::New(
                node.db,
                2,
                3,
                sgns::ValidatorRegistry::WeightConfig{},
                genesis_authority,
                []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
                { cb( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );
            EXPECT_TRUE( registry->StoreRegistryUpdate( update ).has_value() );
            ASSERT_WAIT_FOR_CONDITION(
                [&registry]()
                {
                    auto current = registry->LoadCurrentRegistry();
                    return current.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "three-validator registry initialized",
                nullptr );
            return registry;
        }

        static std::shared_ptr<sgns::ConsensusManager> MakeMultiValidatorManager(
            const MultiValidatorNode                        &node,
            const std::shared_ptr<sgns::ValidatorRegistry> &registry )
        {
            auto manager = sgns::ConsensusManager::New(
                registry,
                node.db,
                node.pubsub,
                [account = node.account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
                node.account->GetAddress() );
            EXPECT_TRUE( manager );
            return manager;
        }

        static std::shared_ptr<sgns::MintTransactionV2> MakeMultiValidatorMint( uint64_t nonce )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "mint-v2" );
            dag.set_source_addr( "multi-validator-mint-source" );
            dag.set_nonce( nonce );
            dag.set_timestamp( static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                    .count() ) );
            const auto burn_hash = sgns::base::Hash256::fromReadableString( std::string( 64, 'b' ) );
            EXPECT_TRUE( burn_hash.has_value() );
            return std::make_shared<sgns::MintTransactionV2>( sgns::MintTransactionV2::New(
                42,
                "source-chain",
                sgns::TokenID::FromBytes( { 0x00 } ),
                std::move( dag ),
                { { burn_hash.value(), 0, {} } },
                "multi-validator-mint-destination" ) );
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

        sgns::ConsensusManager::Proposal MakeSigningProposal(
            const std::shared_ptr<sgns::ConsensusManager>  &manager,
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            const std::shared_ptr<sgns::GeniusAccount>     &account,
            uint64_t                                        nonce,
            const std::string                              &tx_hash )
        {
            auto subject_result = sgns::ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                              nonce,
                                                                              tx_hash,
                                                                              sgns::EmbeddedTransaction{},
                                                                              MakeTestCommitment(),
                                                                              MakeTestWitness() );
            EXPECT_TRUE( subject_result.has_value() );

            auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                            account->GetAddress(),
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

TEST_F( ConsensusPendingLifecycleTest, TimerWorkCanReleaseTheLastExternalManagerOwner )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );

    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    std::weak_ptr<sgns::ConsensusManager> weak_manager = manager;

    std::mutex              release_mutex;
    std::condition_variable release_cv;
    bool                    external_owner_released = false;
    sgns::ConsensusPendingLifecycleTestAccess::SetTimerWorkHook(
        manager,
        [&]()
        {
            manager.reset();
            {
                std::lock_guard lock( release_mutex );
                external_owner_released = true;
            }
            release_cv.notify_all();
        } );
    sgns::ConsensusPendingLifecycleTestAccess::RequestTimerWork( manager );

    {
        std::unique_lock lock( release_mutex );
        ASSERT_TRUE( release_cv.wait_for( lock,
                                          std::chrono::seconds( 5 ),
                                          [&] { return external_owner_released; } ) );
    }
    ASSERT_WAIT_FOR_CONDITION( [&] { return weak_manager.expired(); },
                               std::chrono::seconds( 2 ),
                               "timer thread released the last manager owner",
                               nullptr );
}

TEST_F( ConsensusPendingLifecycleTest, ConcurrentCloseCallsTransferTimerOwnershipOnlyOnce )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );

    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );

    constexpr std::size_t kCloseCallers = 8;
    std::mutex              start_mutex;
    std::condition_variable start_cv;
    bool                    start     = false;
    std::atomic<std::size_t> ready{ 0 };
    std::vector<std::thread> closers;
    closers.reserve( kCloseCallers );
    for ( std::size_t i = 0; i < kCloseCallers; ++i )
    {
        closers.emplace_back(
            [manager, &start_mutex, &start_cv, &start, &ready]()
            {
                ready.fetch_add( 1 );
                start_cv.notify_one();
                std::unique_lock lock( start_mutex );
                start_cv.wait( lock, [&] { return start; } );
                lock.unlock();
                manager->Close();
            } );
    }

    bool all_callers_ready = false;
    {
        std::unique_lock lock( start_mutex );
        all_callers_ready = start_cv.wait_for( lock,
                                               std::chrono::seconds( 2 ),
                                               [&] { return ready.load() == kCloseCallers; } );
        start = true;
    }
    start_cv.notify_all();
    for ( auto &closer : closers )
    {
        closer.join();
    }

    ASSERT_TRUE( all_callers_ready );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::IsRoundTimerJoinable( manager ) );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
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
    matching_element.set_key( sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( certificate ) );
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

    // Key-aware CRDT ingress accepts a certificate only when the supplied canonical slot matches.
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

TEST_F( ConsensusPendingLifecycleTest, FilterCertificateRejectsHigherHashOccupiedSlotBeforeCrdtApply )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto proposal = MakeSigningProposal( manager, registry, account, 90, "0xoccupied-slot-ordering" );
    auto vote = manager->CreateVote( proposal.proposal_id(),
                                     account->GetAddress(),
                                     true,
                                     [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
    ASSERT_TRUE( vote.has_value() );
    auto certificate = manager->CreateCertificate( proposal, { vote.value() } );
    ASSERT_TRUE( certificate.has_value() );

    auto existing = certificate.value();
    std::string existing_serialized;
    ASSERT_TRUE( existing.SerializeToString( &existing_serialized ) );
    sgns::ConsensusManager::Certificate candidate;
    std::string candidate_serialized;
    for ( uint64_t offset = 1; offset < 128; ++offset )
    {
        candidate = certificate.value();
        candidate.set_timestamp( certificate.value().timestamp() + offset );
        ASSERT_TRUE( candidate.SerializeToString( &candidate_serialized ) );
        if ( SerializedCertificateHash( existing_serialized ) < SerializedCertificateHash( candidate_serialized ) )
        {
            break;
        }
    }
    ASSERT_LT( SerializedCertificateHash( existing_serialized ), SerializedCertificateHash( candidate_serialized ) );

    sgns::ConsensusPendingLifecycleTestAccess::WriteLiveCertificate( manager, existing );
    sgns::crdt::pb::Element element;
    element.set_key( sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( existing ) );
    element.set_value( candidate_serialized );
    const auto filtered = sgns::ConsensusPendingLifecycleTestAccess::FilterCertificate( manager, element );
    ASSERT_TRUE( filtered.has_value() );
    EXPECT_TRUE( filtered->empty() );

    auto stored = manager->GetCertificateBySlot( sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( proposal ) );
    ASSERT_TRUE( stored.has_value() );
    EXPECT_EQ( stored.value().SerializeAsString(), existing_serialized );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, FilterCertificateTreatsSameMintAlternatesAsNormalAndDifferentMintQuorumsAsFaults )
{
    constexpr std::array<const char *, 3> private_keys = {
        "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        "cafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" };
    std::array<MultiValidatorNode, 3> nodes;
    for ( size_t index = 0; index < nodes.size(); ++index )
    {
        nodes[index] = MakeMultiValidatorNode( index + 10, private_keys[index] );
    }
    const std::array<std::shared_ptr<sgns::GeniusAccount>, 3> accounts = {
        nodes[0].account, nodes[1].account, nodes[2].account };
    const auto update = MakeThreeValidatorRegistryUpdate( accounts );
    for ( auto &node : nodes )
    {
        node.registry = MakeMultiValidatorRegistry( node, update, accounts.front()->GetAddress() );
        node.manager = MakeMultiValidatorManager( node, node.registry );
        ASSERT_TRUE( node.manager );
    }
    sgns::ConsensusManager::RegisterSlotKeyHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const sgns::ConsensusManager::Subject &subject )
        {
            const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject );
            if ( nonce.has_error() || nonce.value().transaction().transaction_case() != sgns::EmbeddedTransaction::kMintV2 )
            {
                return std::string{};
            }
            const auto bytes = nonce.value().transaction().mint_v2().SerializeAsString();
            const auto mint = sgns::MintTransactionV2::DeSerializeByteVector(
                std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            return mint ? mint->GetSlotID() : std::string{};
        } );

    const auto first_mint  = MakeMultiValidatorMint( 201 );
    const auto second_mint = MakeMultiValidatorMint( 202 );
    ASSERT_NE( first_mint->GetHash(), second_mint->GetHash() );
    ASSERT_EQ( first_mint->GetSlotID(), second_mint->GetSlotID() );
    const auto make_proposal = [&]( const std::shared_ptr<sgns::MintTransactionV2> &mint )
    {
        auto subject = sgns::ConsensusManager::CreateNonceSubject( accounts.front()->GetAddress(),
                                                                    mint->GetNonce(),
                                                                    mint->GetHash(),
                                                                    mint->SerializeToEmbeddedTransaction(),
                                                                    std::nullopt,
                                                                    std::nullopt );
        EXPECT_TRUE( subject.has_value() );
        return nodes.front().manager->CreateProposal(
            subject.value(), accounts.front()->GetAddress(), nodes.front().registry->GetRegistryCid(), 1 );
    };
    const auto first_proposal  = make_proposal( first_mint );
    const auto second_proposal = make_proposal( second_mint );
    ASSERT_TRUE( first_proposal.has_value() );
    ASSERT_TRUE( second_proposal.has_value() );
    const auto sign_vote = [&]( const sgns::ConsensusManager::Proposal &proposal,
                                const std::shared_ptr<sgns::GeniusAccount> &account )
    {
        return nodes.front().manager->CreateVote(
            proposal.proposal_id(), account->GetAddress(), true,
            [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
    };

    // This isolated consensus-fault fixture deliberately overlaps validator 0.
    // It is not used by the normal Phase 9 one-vote proof above.
    const auto first_vote_0  = sign_vote( first_proposal.value(), accounts[0] );
    const auto first_vote_1  = sign_vote( first_proposal.value(), accounts[1] );
    const auto second_vote_0 = sign_vote( second_proposal.value(), accounts[0] );
    const auto second_vote_2 = sign_vote( second_proposal.value(), accounts[2] );
    ASSERT_TRUE( first_vote_0.has_value() );
    ASSERT_TRUE( first_vote_1.has_value() );
    ASSERT_TRUE( second_vote_0.has_value() );
    ASSERT_TRUE( second_vote_2.has_value() );
    EXPECT_EQ( first_vote_0.value().voter_id(), second_vote_0.value().voter_id() );
    EXPECT_NE( first_vote_0.value().proposal_id(), second_vote_0.value().proposal_id() );
    const auto first_certificate = nodes.front().manager->CreateCertificate(
        first_proposal.value(), { first_vote_0.value(), first_vote_1.value() } );
    const auto second_certificate = nodes.front().manager->CreateCertificate(
        second_proposal.value(), { second_vote_0.value(), second_vote_2.value() } );
    ASSERT_TRUE( first_certificate.has_value() );
    ASSERT_TRUE( second_certificate.has_value() );
    EXPECT_EQ( first_certificate.value().registry_cid(), second_certificate.value().registry_cid() );
    EXPECT_EQ( first_certificate.value().registry_epoch(), second_certificate.value().registry_epoch() );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ValidateCertificate( nodes.front().manager,
                                                                                first_certificate.value() ),
               sgns::ConsensusManager::Check::Approve );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ValidateCertificate( nodes.front().manager,
                                                                                second_certificate.value() ),
               sgns::ConsensusManager::Check::Approve );

    std::string first_serialized;
    std::string second_serialized;
    ASSERT_TRUE( first_certificate.value().SerializeToString( &first_serialized ) );
    ASSERT_TRUE( second_certificate.value().SerializeToString( &second_serialized ) );
    const auto key = sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( first_certificate.value() );
    ASSERT_EQ( key, sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( second_certificate.value() ) );
    // Each direction targets one dedicated node db: the canonical slot key is written
    // exactly once per direction through the production convergent-immutable path, so
    // no direction's outcome can depend on same-priority CRDT overwrite ordering.
    const auto verify_order = [&]( const size_t node_index, const std::string &existing, const std::string &candidate )
    {
        const auto &manager = nodes[node_index].manager;
        sgns::ConsensusManager::Certificate parsed_existing;
        ASSERT_TRUE( parsed_existing.ParseFromString( existing ) );
        ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ValidateCertificate( manager, parsed_existing ),
                   sgns::ConsensusManager::Check::Approve );
        sgns::ConsensusPendingLifecycleTestAccess::WriteConvergentCertificateAtKey( manager, key, existing );
        const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( parsed_existing.proposal() );
        ASSERT_FALSE( slot.empty() );
        ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( parsed_existing ), key );
        auto stored = manager->GetCertificateBySlot( slot );
        ASSERT_TRUE( stored.has_value() );
        EXPECT_EQ( stored.value().SerializeAsString(), existing );
        sgns::crdt::pb::Element element;
        element.set_key( key );
        element.set_value( candidate );
        const auto filtered = sgns::ConsensusPendingLifecycleTestAccess::FilterCertificate( manager, element );
        if ( SerializedCertificateHash( existing ) < SerializedCertificateHash( candidate ) )
        {
            ASSERT_TRUE( filtered.has_value() );
            EXPECT_TRUE( filtered->empty() );
        }
        else
        {
            EXPECT_FALSE( filtered.has_value() );
        }
    };
    // Both directions execute the critical Mint-equivocation diagnostic while
    // retaining the normal lower-hash CRDT decision.
    verify_order( 0, first_serialized, second_serialized );
    verify_order( 1, second_serialized, first_serialized );

    const auto same_mint_alternate = nodes.front().manager->CreateCertificate(
        first_proposal.value(), { first_vote_1.value(), first_vote_0.value() } );
    ASSERT_TRUE( same_mint_alternate.has_value() );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ValidateCertificate( nodes.front().manager,
                                                                                same_mint_alternate.value() ),
               sgns::ConsensusManager::Check::Approve );
    std::string same_mint_serialized;
    ASSERT_TRUE( same_mint_alternate.value().SerializeToString( &same_mint_serialized ) );
    ASSERT_NE( same_mint_serialized, first_serialized );
    verify_order( 2, first_serialized, same_mint_serialized );

    for ( auto &node : nodes )
    {
        sgns::ConsensusPendingLifecycleTestAccess::Close( node.manager );
        node.pubsub->Stop();
    }
    sgns::ConsensusManager::UnregisterSlotKeyHandler( sgns::NONCE_SUBJECT_TYPE );
}

TEST_F( ConsensusPendingLifecycleTest, AuthoritativeSlotLookupReturnsOnlyAnApprovedBoundCertificate )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto proposal = MakeSigningProposal( manager, registry, account, 91, "0xslot-lookup-approved" );
    auto vote = manager->CreateVote(
        proposal.proposal_id(), account->GetAddress(), true,
        [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
    ASSERT_TRUE( vote.has_value() );
    auto certificate = manager->CreateCertificate( proposal, { vote.value() } );
    ASSERT_TRUE( certificate.has_value() );
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( proposal );

    sgns::ConsensusPendingLifecycleTestAccess::WriteLiveCertificate( manager, certificate.value() );
    auto loaded = manager->GetCertificateBySlot( slot );
    ASSERT_TRUE( loaded.has_value() );
    EXPECT_EQ( loaded.value().proposal_id(), proposal.proposal_id() );
    EXPECT_TRUE( manager->CheckCertificateForSlot( slot ) );
    EXPECT_FALSE( manager->CheckCertificateForSlot( "missing-authoritative-slot" ) );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, AuthoritativeSlotLookupRejectsLegacyMalformedAndMismatchedRecords )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto proposal = MakeSigningProposal( manager, registry, account, 92, "0xslot-lookup-negative" );
    auto vote = manager->CreateVote(
        proposal.proposal_id(), account->GetAddress(), true,
        [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
    ASSERT_TRUE( vote.has_value() );
    auto certificate = manager->CreateCertificate( proposal, { vote.value() } );
    ASSERT_TRUE( certificate.has_value() );
    std::string serialized;
    ASSERT_TRUE( certificate.value().SerializeToString( &serialized ) );

    auto subject_hash = sgns::ConsensusPendingLifecycleTestAccess::GetSubjectHash( proposal.subject() );
    ASSERT_TRUE( subject_hash.has_value() );
    const auto canonical_slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( proposal );
    const auto legacy_key = std::string( "/cert/" ) + subject_hash.value();
    ASSERT_NE( legacy_key, sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( certificate.value() ) );

    // A valid certificate under a legacy subject-hash suffix does not become authority.
    sgns::ConsensusPendingLifecycleTestAccess::WriteCertificateAtKey( manager, legacy_key, serialized );
    EXPECT_FALSE( manager->GetCertificateBySlot( canonical_slot ).has_value() );

    const std::string malformed_slot = "malformed-authoritative-slot";
    sgns::ConsensusPendingLifecycleTestAccess::WriteCertificateAtKey(
        manager, std::string( "/cert/" ) + malformed_slot, "not-a-certificate" );
    EXPECT_FALSE( manager->GetCertificateBySlot( malformed_slot ).has_value() );
    EXPECT_FALSE( manager->CheckCertificateForSlot( malformed_slot ) );

    const std::string mismatched_slot = "mismatched-authoritative-slot";
    sgns::ConsensusPendingLifecycleTestAccess::WriteCertificateAtKey(
        manager, std::string( "/cert/" ) + mismatched_slot, serialized );
    EXPECT_FALSE( manager->GetCertificateBySlot( mismatched_slot ).has_value() );
    EXPECT_FALSE( manager->CheckCertificateForSlot( mismatched_slot ) );
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
    element.set_key( sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( certificate_result.value() ) );
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

TEST_F( ConsensusPendingLifecycleTest, MultiValidatorSameSlotMintContentionPersistsOneHonestQuorumWinner )
{
    // With this shared three-validator 2-of-3 snapshot, two different 2-of-3
    // certificates must overlap. A second quorum for the losing Mint would therefore
    // require an overlapping validator to equivocate; this normal Phase 9 proof never
    // manufactures such a double-signed vote.
    constexpr std::array<const char *, 3> private_keys = {
        "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        "cafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" };

    std::array<MultiValidatorNode, 3> nodes;
    for ( size_t index = 0; index < nodes.size(); ++index )
    {
        nodes[index] = MakeMultiValidatorNode( index, private_keys[index] );
    }
    const std::array<std::shared_ptr<sgns::GeniusAccount>, 3> accounts = {
        nodes[0].account, nodes[1].account, nodes[2].account };
    const auto update = MakeThreeValidatorRegistryUpdate( accounts );

    std::string shared_registry_cid;
    for ( auto &node : nodes )
    {
        node.registry = MakeMultiValidatorRegistry( node, update, accounts.front()->GetAddress() );
        ASSERT_TRUE( node.registry );
        const auto current = node.registry->LoadCurrentRegistry();
        ASSERT_TRUE( current.has_value() );
        EXPECT_EQ( current.value().epoch(), 1U );
        EXPECT_EQ( sgns::ValidatorRegistry::TotalWeight( current.value() ), 3U );
        EXPECT_EQ( node.registry->QuorumThreshold( 3 ), 2U );
        EXPECT_FALSE( node.registry->IsQuorum( 1, 3 ) );
        EXPECT_TRUE( node.registry->IsQuorum( 2, 3 ) );
        if ( shared_registry_cid.empty() )
        {
            shared_registry_cid = node.registry->GetRegistryCid();
        }
        EXPECT_EQ( node.registry->GetRegistryCid(), shared_registry_cid );
        EXPECT_EQ( node.registry->GetRegistryEpoch(), 1U );
        node.manager = MakeMultiValidatorManager( node, node.registry );
        ASSERT_TRUE( node.manager );
    }

    const auto first_mint  = MakeMultiValidatorMint( 101 );
    const auto second_mint = MakeMultiValidatorMint( 102 );
    ASSERT_TRUE( first_mint );
    ASSERT_TRUE( second_mint );
    ASSERT_NE( first_mint->GetHash(), second_mint->GetHash() );
    ASSERT_EQ( first_mint->GetSlotID(), second_mint->GetSlotID() );
    sgns::ConsensusManager::RegisterSlotKeyHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const sgns::ConsensusManager::Subject &subject )
        {
            const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject );
            if ( nonce.has_error() || nonce.value().transaction().transaction_case() != sgns::EmbeddedTransaction::kMintV2 )
            {
                return std::string{};
            }
            const auto bytes = nonce.value().transaction().mint_v2().SerializeAsString();
            const auto mint = sgns::MintTransactionV2::DeSerializeByteVector(
                std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            return mint ? mint->GetSlotID() : std::string{};
        } );

    const auto first_subject = sgns::ConsensusManager::CreateNonceSubject( accounts.front()->GetAddress(),
                                                                            first_mint->GetNonce(),
                                                                            first_mint->GetHash(),
                                                                            first_mint->SerializeToEmbeddedTransaction(),
                                                                            std::nullopt,
                                                                            std::nullopt );
    const auto second_subject = sgns::ConsensusManager::CreateNonceSubject( accounts.front()->GetAddress(),
                                                                             second_mint->GetNonce(),
                                                                             second_mint->GetHash(),
                                                                             second_mint->SerializeToEmbeddedTransaction(),
                                                                             std::nullopt,
                                                                             std::nullopt );
    ASSERT_TRUE( first_subject.has_value() );
    ASSERT_TRUE( second_subject.has_value() );
    const auto first_proposal = nodes.front().manager->CreateProposal(
        first_subject.value(), accounts.front()->GetAddress(), shared_registry_cid, 1 );
    const auto second_proposal = nodes.front().manager->CreateProposal(
        second_subject.value(), accounts.front()->GetAddress(), shared_registry_cid, 1 );
    ASSERT_TRUE( first_proposal.has_value() );
    ASSERT_TRUE( second_proposal.has_value() );
    ASSERT_NE( first_proposal.value().proposal_id(), second_proposal.value().proposal_id() );
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( first_proposal.value() );
    ASSERT_EQ( slot, sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( second_proposal.value() ) );

    std::vector<sgns::ConsensusManager::Vote> normal_votes;
    std::vector<std::string>                  persisted_vote_bytes;
    std::string                               selected_proposal_id;
    for ( auto &node : nodes )
    {
        sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( node.manager, first_proposal.value() );
        sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( node.manager, second_proposal.value() );
        sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( node.manager, slot );
        sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( node.manager );

        ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( node.manager ).size(), 1U );
        const auto bytes = sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( node.manager ).front();
        auto record_bytes = sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( node.manager, slot );
        ASSERT_TRUE( record_bytes.has_value() );
        sgns::ActiveVoteRecord record;
        ASSERT_TRUE( record.ParseFromString( record_bytes.value() ) );
        EXPECT_EQ( record.canonical_slot(), slot );
        EXPECT_EQ( record.vote_bytes(), bytes );
        sgns::ConsensusManager::Vote vote;
        ASSERT_TRUE( vote.ParseFromString( bytes ) );
        if ( selected_proposal_id.empty() )
        {
            selected_proposal_id = vote.proposal_id();
        }
        EXPECT_EQ( vote.proposal_id(), selected_proposal_id );
        normal_votes.push_back( std::move( vote ) );
        persisted_vote_bytes.push_back( record.vote_bytes() );

        // Reconstruct this validator against its own prior GlobalDB immediately.
        // RecoverActiveVotes must retain the one exact durable vote and never create
        // a competing vote after restart.
        const auto node_index = static_cast<size_t>( &node - nodes.data() );
        sgns::ConsensusPendingLifecycleTestAccess::Close( node.manager );
        node.manager.reset();
        node.manager = MakeMultiValidatorManager( node, node.registry );
        ASSERT_TRUE( node.manager );
        sgns::ConsensusPendingLifecycleTestAccess::ClearActiveVoteAnnouncements( node.manager );
        sgns::ConsensusPendingLifecycleTestAccess::ForceActiveVoteRetryDue( node.manager, slot );
        sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( node.manager );
        ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( node.manager ).size(), 1U );
        EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( node.manager ).front(),
                   persisted_vote_bytes[node_index] );
    }

    const auto &winner = selected_proposal_id == first_proposal.value().proposal_id() ? first_proposal.value()
                                                                                       : second_proposal.value();
    const auto &loser  = selected_proposal_id == first_proposal.value().proposal_id() ? second_proposal.value()
                                                                                       : first_proposal.value();
    const auto winner_certificate = nodes.front().manager->CreateCertificate( winner, normal_votes );
    ASSERT_TRUE( winner_certificate.has_value() );
    EXPECT_EQ( winner_certificate.value().approved_weight(), 3U );
    const auto loser_tally = nodes.front().manager->TallyVotes( loser, normal_votes );
    ASSERT_TRUE( loser_tally.has_value() );
    EXPECT_FALSE( loser_tally.value().has_quorum );
    const auto loser_certificate = nodes.front().manager->CreateCertificate( loser, normal_votes );
    ASSERT_TRUE( loser_certificate.has_value() );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ValidateCertificate( nodes.front().manager,
                                                                                loser_certificate.value() ),
               sgns::ConsensusManager::Check::Reject );

    for ( auto &node : nodes )
    {
        sgns::ConsensusPendingLifecycleTestAccess::Close( node.manager );
        node.pubsub->Stop();
    }
    sgns::ConsensusManager::UnregisterSlotKeyHandler( sgns::NONCE_SUBJECT_TYPE );
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

TEST_F( ConsensusPendingLifecycleTest, ScanFailureRetainsCandidateUntilTimerCanOpenOneWindow )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto proposal = MakeProposal( manager, registry, 86, "0xscan-retry-candidate" );
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( proposal );

    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, true );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, proposal );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasAcceptedCertificateScanPending( manager, slot ) );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCertificateScanCandidateCount( manager, slot ), 1U );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );

    // No proposal redelivery is needed: the next due-work pass retries the scan,
    // opens one fresh fixed window, and retains the original approved candidate.
    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, false );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasAcceptedCertificateScanPending( manager, slot ) );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCertificateScanCandidateCount( manager, slot ), 0U );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );

    // Once open, a scan failure at the already-closed deadline retains that
    // deadline rather than extending it or selecting a new contender set.
    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, true );
    sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasAcceptedCertificateScanPending( manager, slot ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );
    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, false );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).size(), 1U );
    ASSERT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );

    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).size(), 1U );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, ScanRecoveryMergesOnlyPreDeadlineContendersIntoTheOriginalWindow )
{
    sgns::ConsensusManager::RegisterSlotKeyHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const sgns::ConsensusManager::Subject &subject )
        {
            auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject );
            return subject.account_id() + ":" + std::to_string( nonce.has_value() ? nonce.value().nonce() : 0ULL );
        } );

    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto higher_ranked = MakeProposal( manager, registry, 87, "z-higher-hash" );
    auto lower_ranked  = MakeProposal( manager, registry, 87, "a-lower-hash" );
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( higher_ranked );
    ASSERT_EQ( slot, sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( lower_ranked ) );

    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, higher_ranked );
    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, true );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, lower_ranked );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCertificateScanCandidateCount( manager, slot ), 1U );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );

    // Recovery before the original deadline admits the stored lower hash without
    // requiring redelivery or moving that deadline.
    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, false );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasAcceptedCertificateScanPending( manager, slot ) );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCertificateScanCandidateCount( manager, slot ), 0U );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );

    // A lower hash arriving only after the fixed deadline cannot enter through
    // the recovery buffer or change the already-closed candidate set.
    sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( manager, slot );
    auto late = MakeProposal( manager, registry, 87, "0-late-lower-hash" );
    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, true );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, late );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCertificateScanCandidateCount( manager, slot ), 0U );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).empty() );

    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, false );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).size(), 1U );
    auto record_bytes = sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot );
    ASSERT_TRUE( record_bytes.has_value() );
    sgns::ActiveVoteRecord record;
    ASSERT_TRUE( record.ParseFromString( record_bytes.value() ) );
    sgns::ConsensusProposal persisted;
    ASSERT_TRUE( persisted.ParseFromString( record.proposal_bytes() ) );
    EXPECT_EQ( persisted.proposal_id(), lower_ranked.proposal_id() );

    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    EXPECT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).size(), 1U );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, ScanRecoveryAfterDeadlineCannotChangeTheFrozenWinner )
{
    sgns::ConsensusManager::RegisterSlotKeyHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const sgns::ConsensusManager::Subject &subject )
        {
            auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject );
            return subject.account_id() + ":" + std::to_string( nonce.has_value() ? nonce.value().nonce() : 0ULL );
        } );

    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto original_winner = MakeProposal( manager, registry, 88, "z-original-hash" );
    auto retained_later  = MakeProposal( manager, registry, 88, "a-retained-hash" );
    const auto slot = sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( original_winner );
    ASSERT_EQ( slot, sgns::ConsensusPendingLifecycleTestAccess::GetSlotKey( retained_later ) );

    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, original_winner );
    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, true );
    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, retained_later );
    ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::PendingCertificateScanCandidateCount( manager, slot ), 1U );

    sgns::ConsensusPendingLifecycleTestAccess::ForceCandidateWindowDue( manager, slot );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasAcceptedCertificateScanPending( manager, slot ) );
    sgns::ConsensusPendingLifecycleTestAccess::SetAcceptedCertificateScanFailure( manager, false );
    sgns::ConsensusPendingLifecycleTestAccess::ProcessDueVoteWork( manager );

    ASSERT_EQ( sgns::ConsensusPendingLifecycleTestAccess::ActiveVoteAnnouncements( manager ).size(), 1U );
    auto record_bytes = sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot );
    ASSERT_TRUE( record_bytes.has_value() );
    sgns::ActiveVoteRecord record;
    ASSERT_TRUE( record.ParseFromString( record_bytes.value() ) );
    sgns::ConsensusProposal persisted;
    ASSERT_TRUE( persisted.ParseFromString( record.proposal_bytes() ) );
    EXPECT_EQ( persisted.proposal_id(), original_winner.proposal_id() );
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
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const std::string &, const sgns::ConsensusManager::Certificate & )
        { return outcome::success( sgns::ConsensusManager::Check::Approve ); } ) );

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
    const auto legacy_key = sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( certificate.value() );
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
    const auto other_legacy_key = sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( other_certificate.value() );
    std::string other_serialized;
    ASSERT_TRUE( other_certificate.value().SerializeToString( &other_serialized ) );
    sgns::base::Buffer other_callback_value;
    other_callback_value.put( other_serialized );
    sgns::ConsensusPendingLifecycleTestAccess::CertificateReceived(
        manager, { other_legacy_key, std::move( other_callback_value ) } );
    sgns::ConsensusPendingLifecycleTestAccess::WriteLiveCertificate( manager, other_certificate.value() );
    sgns::ConsensusPendingLifecycleTestAccess::RecoverPendingCertificateWork( manager );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasStalledCertificateWork( manager, other_legacy_key ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasCertificateWork( manager, other_legacy_key ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );

    sgns::ConsensusPendingLifecycleTestAccess::WriteLiveCertificate( manager, certificate.value() );
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

    // A reconstructed manager only scans existing authoritative slot values; it does not create
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

TEST_F( ConsensusPendingLifecycleTest, DurableCertificateWaitsForHandlerRegistrationBeforeFinalizing )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto subject = sgns::ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                88,
                                                                "0xdurable-before-handler",
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
    const auto key = sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( certificate.value() );
    std::string serialized;
    ASSERT_TRUE( certificate.value().SerializeToString( &serialized ) );

    sgns::base::Buffer callback_value;
    callback_value.put( serialized );
    sgns::ConsensusPendingLifecycleTestAccess::CertificateReceived( manager, { key, std::move( callback_value ) } );
    sgns::ConsensusPendingLifecycleTestAccess::WriteLiveCertificate( manager, certificate.value() );

    // A durable certificate cannot consume a vote or finish work before its consumer exists.
    sgns::ConsensusPendingLifecycleTestAccess::RecoverPendingCertificateWork( manager );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasStalledCertificateWork( manager, key ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( manager, slot ) );

    std::atomic<int> handler_calls{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&handler_calls]( const std::string &, const sgns::ConsensusManager::Certificate & )
        {
            ++handler_calls;
            return outcome::success( sgns::ConsensusManager::Check::Approve );
        } ) );

    EXPECT_EQ( handler_calls.load(), 1 );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasCertificateWork( manager, key ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::ReadActiveVoteRecord( manager, slot ).has_value() );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasActiveVoteLock( manager, slot ) );

    sgns::ConsensusPendingLifecycleTestAccess::RecoverPendingCertificateWork( manager );
    EXPECT_EQ( handler_calls.load(), 1 );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}

TEST_F( ConsensusPendingLifecycleTest, CertificateRecoverySerializesHandlerRegistrationAndTimerDispatch )
{
    auto account = MakeSigningAccount();
    ASSERT_TRUE( account );
    auto registry = MakeSigningRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeSigningManager( registry, account );
    ASSERT_TRUE( manager );

    auto subject = sgns::ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                89,
                                                                "0xconcurrent-certificate-recovery",
                                                                sgns::EmbeddedTransaction{},
                                                                MakeTestCommitment(),
                                                                MakeTestWitness() );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = manager->CreateProposal(
        subject.value(), account->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    auto vote = manager->CreateVote(
        proposal.value().proposal_id(), account->GetAddress(), true,
        [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
    ASSERT_TRUE( vote.has_value() );
    auto certificate = manager->CreateCertificate( proposal.value(), { vote.value() } );
    ASSERT_TRUE( certificate.has_value() );
    const auto key = sgns::ConsensusPendingLifecycleTestAccess::GetExpectedCertificateSlotKey( certificate.value() );

    std::string serialized;
    ASSERT_TRUE( certificate.value().SerializeToString( &serialized ) );
    sgns::base::Buffer callback_value;
    callback_value.put( serialized );
    sgns::ConsensusPendingLifecycleTestAccess::CertificateReceived( manager, { key, std::move( callback_value ) } );
    sgns::ConsensusPendingLifecycleTestAccess::WriteLiveCertificate( manager, certificate.value() );

    std::mutex              handler_mutex;
    std::condition_variable handler_cv;
    bool                    handler_started = false;
    bool                    allow_handler   = false;
    std::atomic<int>        handler_calls{ 0 };
    std::atomic<bool>       registration_succeeded{ false };

    std::thread registration_thread(
        [&]
        {
            registration_succeeded.store( manager->RegisterCertificateHandler(
                sgns::NONCE_SUBJECT_TYPE,
                [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
                {
                    ++handler_calls;
                    std::unique_lock lock( handler_mutex );
                    handler_started = true;
                    handler_cv.notify_all();
                    handler_cv.wait( lock, [&] { return allow_handler; } );
                    return outcome::success( sgns::ConsensusManager::Check::Approve );
                } ) );
        } );

    std::unique_lock handler_lock( handler_mutex );
    const bool started = handler_cv.wait_for( handler_lock, std::chrono::seconds( 5 ), [&] { return handler_started; } );
    if ( !started )
    {
        allow_handler = true;
        handler_lock.unlock();
        handler_cv.notify_all();
        registration_thread.join();
        FAIL() << "handler registration did not enter certificate recovery";
        sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
        return;
    }

    // This represents the timer retry arriving while registration-triggered
    // recovery owns the dispatch. It must observe completion, not invoke again.
    std::thread timer_recovery_thread(
        [&] { sgns::ConsensusPendingLifecycleTestAccess::RecoverPendingCertificateWork( manager ); } );
    allow_handler = true;
    handler_lock.unlock();
    handler_cv.notify_all();
    registration_thread.join();
    timer_recovery_thread.join();

    EXPECT_TRUE( registration_succeeded.load() );
    EXPECT_EQ( handler_calls.load(), 1 );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasCertificateWork( manager, key ) );
    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
}
