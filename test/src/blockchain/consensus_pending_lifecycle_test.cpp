/**
 * @file       consensus_pending_lifecycle_test.cpp
 * @brief      Focused harness for consensus pending proposal lifecycle tests.
 * @details    Provides the Wave 0 target for PEND-01 through PEND-07 so
 *             later implementation plans can add D-01 through D-12 and D-16
 *             behavior cases without changing CTest wiring.
 * @date       2026-06-16
 */

#include <gtest/gtest.h>

#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

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
                   ( ( slot_it->second.lifecycle == ConsensusManager::SlotState::Lifecycle::Selecting &&
                       slot_it->second.best_proposal_id == proposal_id ) ||
                     ( slot_it->second.lifecycle == ConsensusManager::SlotState::Lifecycle::Voted &&
                       slot_it->second.durable_proposal_id == proposal_id ) );
        }

        static void HandleProposal( const std::shared_ptr<ConsensusManager> &manager,
                                    const ConsensusManager::Proposal        &proposal )
        {
            manager->HandleProposal( proposal );
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
            std::lock_guard lock( manager->proposals_mutex_ );
            return manager->pending_config_;
        }

        static void SetPendingConfig( const std::shared_ptr<ConsensusManager> &manager,
                                      ConsensusManager::PendingLifecycleConfig config )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            manager->pending_config_ = std::move( config );
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
            auto slot_result = ConsensusManager::GetSlotKey( proposal );
            ASSERT_TRUE( slot_result.has_value() );
            ConsensusManager::ProposalState state;
            state.proposal             = proposal;
            state.slot_key             = slot_result.value();
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
            auto slot_result = ConsensusManager::GetSlotKey( proposal );
            ASSERT_TRUE( slot_result.has_value() );
            manager->ContinueProposalAfterSubject( proposal, slot_result.value() );
            auto &slot = manager->slot_states_.at( slot_result.value() );
            manager->ProcessCandidateDeadlines( slot.deadline );
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

            auto store_result = registry->StoreGenesisRegistry( std::vector<std::string>{ kValidatorId }, DummySignature );
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

TEST_F( ConsensusPendingLifecycleTest, QuorumCertificateWorkRetainsStateWhenLocalNodeIsNotAggregator )
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

    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( observer_manager, proposal.proposal_id() ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::CertificatesPending( observer_manager ) );
    sgns::ConsensusPendingLifecycleTestAccess::Close( observer_manager );
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
    sgns::ConsensusPendingLifecycleTestAccess::SetPendingConfig( manager, config );

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
    sgns::ConsensusPendingLifecycleTestAccess::SetPendingConfig( manager, config );

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
    sgns::ConsensusPendingLifecycleTestAccess::SetPendingConfig( manager, config );

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

    sgns::ConsensusPendingLifecycleTestAccess::SetPendingConfig(
        manager,
        sgns::ConsensusManager::PendingLifecycleConfig{} );

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
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, retry_proposal_id ) );

    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, retry_proposal );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, retry_proposal_id ) );

    config                               = sgns::ConsensusManager::PendingLifecycleConfig{};
    config.pending_ttl                   = std::chrono::seconds( 10 );
    config.min_dependency_retry_interval = std::chrono::milliseconds( 0 );
    sgns::ConsensusPendingLifecycleTestAccess::SetPendingConfig( manager, config );

    std::unordered_map<std::string, int>                                                            handler_attempts;
    std::unordered_map<std::string, std::function<sgns::ConsensusManager::ValidationResult( int )>> handler_scripts;
    ASSERT_TRUE( manager->RegisterSubjectHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const sgns::ConsensusManager::Subject &subject )
            -> outcome::result<sgns::ConsensusManager::ValidationResult>
        {
            BOOST_OUTCOME_TRY( auto nonce_subject, sgns::ConsensusManager::DecodeNonceSubject( subject ) );
            const auto &tx_hash = nonce_subject.tx_hash();
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
    EXPECT_TRUE(
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
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, multi_proposal_id ) );

    config                               = sgns::ConsensusManager::PendingLifecycleConfig{};
    config.pending_ttl                   = std::chrono::seconds( 10 );
    config.min_dependency_retry_interval = std::chrono::seconds( 10 );
    config.scheduled_retry_delays        = { std::chrono::seconds( 1 ),
                                             std::chrono::seconds( 2 ),
                                             std::chrono::seconds( 5 ),
                                             std::chrono::seconds( 10 ) };
    sgns::ConsensusPendingLifecycleTestAccess::SetPendingConfig( manager, config );

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

    sgns::ConsensusPendingLifecycleTestAccess::Close( manager );
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
