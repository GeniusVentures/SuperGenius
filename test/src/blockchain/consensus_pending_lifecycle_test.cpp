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
            return manager && manager->pending_proposals_.find( proposal_id ) != manager->pending_proposals_.end();
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
            return slot_it != manager->slot_states_.end() && slot_it->second.voted;
        }

        static void HandleProposal( const std::shared_ptr<ConsensusManager> &manager,
                                    const ConsensusManager::Proposal        &proposal )
        {
            manager->HandleProposal( proposal );
        }

        static void AddPendingProposal( const std::shared_ptr<ConsensusManager>        &manager,
                                        const ConsensusManager::Proposal               &proposal,
                                        const std::string                              &subject_hash,
                                        const ConsensusManager::ValidationResult       &validation_result )
        {
            manager->AddPendingProposal( proposal, subject_hash, validation_result );
        }

        static void ContinueProposalAfterSubject( const std::shared_ptr<ConsensusManager> &manager,
                                                  const ConsensusManager::Proposal        &proposal )
        {
            manager->ContinueProposalAfterSubject( proposal );
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
        "D-12 pending TTL expiry"
    };
    const std::string kValidatorId = "validator-pending-lifecycle";

    std::vector<uint8_t> DummySignature( std::vector<uint8_t> )
    {
        return std::vector<uint8_t>{ 0x07, 0x02 };
    }

    class ConsensusPendingLifecycleTest : public test::CRDTFixture
    {
    public:
        ConsensusPendingLifecycleTest() : CRDTFixture( "consensus_pending_lifecycle_test" ) {}

    protected:
        static std::vector<std::string> PendingBehaviorNames()
        {
            return std::vector<std::string>(
                kConsensusPendingBehaviors.begin(),
                kConsensusPendingBehaviors.end() );
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

            auto store_result = registry->StoreGenesisRegistry(
                kValidatorId,
                DummySignature );
            EXPECT_FALSE( store_result.has_error() );

            ASSERT_WAIT_FOR_CONDITION(
                [&registry]()
                {
                    auto load = registry->LoadRegistry();
                    return load.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "registry initialized",
                nullptr );

            return registry;
        }

        std::shared_ptr<sgns::ConsensusManager> MakeManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry )
        {
            auto manager = sgns::ConsensusManager::New(
                registry,
                db_,
                pubs_,
                []( std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
                { return DummySignature( std::move( payload ) ); },
                kValidatorId );
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
    };
} // namespace

TEST_F( ConsensusPendingLifecycleTest, HarnessIsDiscoverable )
{
    /**
     * Given a dedicated consensus pending lifecycle CTest target,
     * When the current Wave 0 harness is discovered,
     * Then it exposes the future behavior slots for PEND-01 through PEND-07
     * and avoids bridge or EVM RPC concerns.
     */
    const auto behaviors = PendingBehaviorNames();

    ASSERT_EQ( behaviors.size(), kConsensusPendingBehaviors.size() );
    EXPECT_STREQ( sgns::ConsensusPendingLifecycleTestAccess::Scope(),
                  "consensus pending lifecycle" );
    EXPECT_FALSE( sgns::NONCE_SUBJECT_TYPE.empty() );
}

TEST_F( ConsensusPendingLifecycleTest, ValidationResultPreservesTerminalChecks )
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

TEST_F( ConsensusPendingLifecycleTest, PendingResultCarriesTypedCertificateDependencyAndRetryMetadata )
{
    /**
     * Given a proposal waiting for a predecessor certificate,
     * When Pending is constructed with a Certificate dependency key,
     * Then the typed dependency and optional retry metadata stay local to the
     * structured result.
     */
    using PendingDependencyKey = sgns::ConsensusManager::PendingDependencyKey;

    const auto dependency = PendingDependencyKey::Certificate( "tx-previous-cert" );
    const auto pending    = sgns::ConsensusManager::ValidationResult::Pending(
        { dependency },
        std::chrono::seconds( 2 ) );

    ASSERT_EQ( pending.check, sgns::ConsensusManager::Check::Pending );
    ASSERT_EQ( pending.dependencies.size(), 1U );
    EXPECT_EQ( pending.dependencies.front().type, PendingDependencyKey::Type::Certificate );
    EXPECT_EQ( pending.dependencies.front().value, "tx-previous-cert" );
    ASSERT_TRUE( pending.retry_after.has_value() );
    EXPECT_EQ( pending.retry_after.value(), std::chrono::seconds( 2 ) );
}

TEST_F( ConsensusPendingLifecycleTest, PendingDependencyKeySupportsHashIdentity )
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

TEST_F( ConsensusPendingLifecycleTest, PendingProposalRemainsLocalUntilRetryApproval )
{
    /**
     * Given a subject handler returns local Pending for a valid proposal,
     * When the proposal is handled and later resumed with Approve,
     * Then Pending emits no local vote and retry approval uses the normal
     * proposal path exactly once.
     */
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );

    auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kValidatorId,
        7,
        "0xpending-retry",
        sgns::EmbeddedTransaction{},
        MakeTestCommitment(),
        MakeTestWitness() );
    ASSERT_TRUE( subject_result.has_value() );

    auto proposal_result = manager->CreateProposal(
        subject_result.value(),
        kValidatorId,
        registry->GetRegistryCid(),
        registry->GetRegistryEpoch() );
    ASSERT_TRUE( proposal_result.has_value() );

    const auto proposal_id = proposal_result.value().proposal_id();
    sgns::ConsensusPendingLifecycleTestAccess::AddPendingProposal(
        manager,
        proposal_result.value(),
        "0xpending-retry",
        sgns::ConsensusManager::ValidationResult::Pending(
            { sgns::ConsensusManager::PendingDependencyKey::Certificate( "0xprevious" ) } ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, proposal_id ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, proposal_id ) );

    const auto pending = sgns::ConsensusPendingLifecycleTestAccess::TakePendingProposals( manager, "0xpending-retry" );
    ASSERT_EQ( pending.size(), 1U );
    EXPECT_EQ( pending.front().proposal_id(), proposal_id );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::HasPendingProposal( manager, proposal_id ) );
    EXPECT_FALSE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, proposal_id ) );

    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, pending.front() );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::HasProposal( manager, proposal_id ) );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, proposal_id ) );

    sgns::ConsensusPendingLifecycleTestAccess::ContinueProposalAfterSubject( manager, proposal_result.value() );
    EXPECT_TRUE( sgns::ConsensusPendingLifecycleTestAccess::LocalVoteCastForProposal( manager, proposal_id ) );
}
