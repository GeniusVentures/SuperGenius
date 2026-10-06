#include <gtest/gtest.h>

#include <atomic>
#include <utility>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusSigner.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "testutil/trust_candidate_core.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::trustedpeer;

    class OperatorApprovalTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
            boost::filesystem::create_directories( path_ );
            for ( size_t i = 0; i < 4; ++i )
            {
                signers_.push_back( GeniusSigner::Generate() );
            }
            node_ = test::securecrdt::MakeSecureCrdtTestNode( "operator_approval" );
            ASSERT_NE( node_, nullptr );
            secure_crdt_ = std::make_shared<securecrdt::SecureCrdt>( node_->db, "operator-approval-topic" );
            store_       = TrustStateStore::Open( ( path_ / "trust" ).string(), 42 ).value();
        }

        void TearDown() override
        {
            registry_.reset();
            secure_crdt_.reset();
            node_.reset();
            store_.reset();
            boost::filesystem::remove_all( path_ );
        }

        GenesisManifest Manifest() const
        {
            GenesisManifest manifest;
            manifest.network_id              = 42;
            manifest.bootstrapper_public_key = signers_[0].GetAddress();
            manifest.peers = { signers_[0].GetAddress(), signers_[1].GetAddress(), signers_[2].GetAddress() };
            manifest.membership_threshold = 2;
            manifest.burn_threshold       = 2;
            return manifest;
        }

        void Construct( size_t local_signer = 0 )
        {
            const auto manifest = Manifest();
            const auto bytes    = manifest.CanonicalBytes().value();
            auto       created  = TrustedPeerRegistry::NewProduction(
                secure_crdt_,
                store_,
                manifest,
                signers_[0].Sign( bytes ),
                signers_[local_signer].GetAddress(),
                [this, local_signer]( const std::vector<uint8_t> &payload )
                {
                    ++sign_invocations_;
                    return signers_[local_signer].Sign( payload );
                } );
            ASSERT_TRUE( created.has_value() ) << created.error().message();
            registry_ = created.value();
            ASSERT_TRUE( secure_crdt_->RegisterFilters() );
        }

        void ConfirmGenesis()
        {
            Construct();
            auto submitted = registry_->SubmitReviewedGenesisApproval();
            ASSERT_TRUE( submitted.has_value() ) << submitted.error().message();
            ASSERT_TRUE( registry_->IsGenesisConfirmed() );
        }

        void ConfirmInitialBurn()
        {
            auto                          snapshot      = store_->LoadAndVerify().value();
            const auto                    core          = sgns::testutil::BurnCandidateCore( snapshot.burn ).value();
            const auto                    authorization = core.CanonicalBytes().value();
            multisig::CollectedSignatures proof{
                { signers_[0].GetAddress(), signers_[0].Sign( authorization ) },
                { signers_[1].GetAddress(), signers_[1].Sign( authorization ) },
            };
            auto confirmed = store_->CommitBurnSuccessor( snapshot.burn, proof, authorization );
            ASSERT_TRUE( confirmed.has_value() ) << confirmed.error().message();
            ASSERT_EQ( confirmed.value().burn_authorization, BurnAuthorizationKind::PeerQuorum );
        }

        QuorumPolicyState Successor() const
        {
            auto       current               = registry_->GetConfirmedSnapshot().value().policy;
            const auto hash                  = current.Hash().value();
            current.version                 += 1;
            current.expected_previous_hash   = hash;
            current.authorizing_policy_hash  = hash;
            return current;
        }

        // Given for the above-quorum tests: a successor policy proposed by the
        // local signer, seconded by signers_[1], and durably activated exactly
        // at the membership threshold (the CurrentPolicyQuorum... shape).
        std::pair<sgns::securecrdt::CandidateId, sgns::securecrdt::CandidateCore> ActivateSuccessorAtThreshold()
        {
            ConfirmInitialBurn();
            auto candidate                 = Successor();
            candidate.peers                = { signers_[0].GetAddress(), signers_[3].GetAddress() };
            candidate.membership_threshold = 2;
            candidate.burn_threshold       = 2;
            const auto core                = TrustedPeerRegistry::PolicyCandidateCore( candidate ).value();
            const auto proposed            = registry_->ProposePolicyCandidate( candidate ).value();
            EXPECT_TRUE( secure_crdt_
                             ->SubmitCandidateApproval( { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                                                          core,
                                                          signers_[1].GetAddress(),
                                                          signers_[1].Sign( core.CanonicalBytes().value() ) } )
                             .has_value() );
            EXPECT_TRUE( registry_->TryActivatePolicyCandidate( proposed ).value() );
            return { proposed, core };
        }

        boost::filesystem::path                               path_;
        std::vector<GeniusSigner>                             signers_;
        std::unique_ptr<test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<securecrdt::SecureCrdt>               secure_crdt_;
        std::shared_ptr<TrustStateStore>                      store_;
        std::shared_ptr<TrustedPeerRegistry>                  registry_;
        std::atomic_uint32_t                                  sign_invocations_{ 0 };
    };
}

TEST_F( OperatorApprovalTest, FreshRegistryIsEmptyAndPolicyOperationsRequireConfirmedGenesis )
{
    Construct();
    EXPECT_TRUE( registry_->GetCurrentPeers().empty() );
    EXPECT_FALSE( registry_->IsGenesisConfirmed() );
    EXPECT_TRUE( registry_->ListPendingPolicyCandidates().has_error() );
    EXPECT_TRUE( registry_->ProposePolicyCandidate( QuorumPolicyState{} ).has_error() );
}

TEST_F( OperatorApprovalTest, ReviewedGenesisCommitsBeforePeersBecomeVisible )
{
    Construct();
    EXPECT_TRUE( registry_->GetCurrentPeers().empty() );
    ASSERT_TRUE( registry_->SubmitReviewedGenesisApproval().has_value() );
    EXPECT_EQ( registry_->GetCurrentPeers(), Manifest().Canonicalized()->peers );
    EXPECT_TRUE( store_->LoadAndVerify().has_value() );
}

TEST_F( OperatorApprovalTest, ReceiptNeverSignsAndExplicitApprovalIsDeduplicated )
{
    ConfirmGenesis();
    sign_invocations_.store( 0 );
    const auto                          candidate = Successor();
    const auto                          core      = TrustedPeerRegistry::PolicyCandidateCore( candidate ).value();
    const auto                          bytes     = core.CanonicalBytes().value();
    securecrdt::CandidateApprovalRecord remote{ securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                                                core,
                                                signers_[1].GetAddress(),
                                                signers_[1].Sign( bytes ) };
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( remote ).has_value() );
    EXPECT_EQ( sign_invocations_.load(), 0U );

    auto proposed = registry_->ProposePolicyCandidate( candidate );
    ASSERT_TRUE( proposed.has_value() ) << proposed.error().message();
    EXPECT_EQ( sign_invocations_.load(), 1U );
    ASSERT_TRUE( registry_->ApprovePolicyCandidate( proposed.value() ).has_value() );
    EXPECT_EQ( sign_invocations_.load(), 1U );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( proposed.value() ).value().size(), 2U );
}

TEST_F( OperatorApprovalTest, ProposedPeersCannotSelfAuthorizeAndWrongLinksNeverReachStore )
{
    ConfirmGenesis();
    auto       candidate                      = Successor();
    const auto durable                        = store_->LoadAndVerify().value();
    candidate.peers                           = { signers_[3].GetAddress() };
    candidate.membership_threshold            = 1;
    candidate.burn_threshold                  = 1;
    const auto                          core  = TrustedPeerRegistry::PolicyCandidateCore( candidate ).value();
    const auto                          bytes = core.CanonicalBytes().value();
    securecrdt::CandidateApprovalRecord self_authorized{ securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                                                         core,
                                                         signers_[3].GetAddress(),
                                                         signers_[3].Sign( bytes ) };
    EXPECT_TRUE( secure_crdt_->SubmitCandidateApproval( self_authorized ).has_error() );

    candidate.expected_previous_hash = std::string( 64, '0' );
    EXPECT_TRUE( registry_->ProposePolicyCandidate( candidate ).has_error() );
    EXPECT_EQ( store_->LoadAndVerify().value(), durable );
}

TEST_F( OperatorApprovalTest, CurrentPolicyQuorumCommitsBeforePublishingSuccessor )
{
    ConfirmGenesis();
    ConfirmInitialBurn();
    auto candidate                 = Successor();
    candidate.peers                = { signers_[0].GetAddress(), signers_[3].GetAddress() };
    candidate.membership_threshold = 2;
    candidate.burn_threshold       = 2;
    const auto old_peers           = registry_->GetCurrentPeers();
    auto       proposed            = registry_->ProposePolicyCandidate( candidate );
    ASSERT_TRUE( proposed.has_value() ) << proposed.error().message();
    EXPECT_EQ( registry_->GetCurrentPeers(), old_peers );
    const auto core       = TrustedPeerRegistry::PolicyCandidateCore( candidate ).value();
    const auto core_bytes = core.CanonicalBytes().value();
    ASSERT_TRUE( secure_crdt_
                     ->SubmitCandidateApproval( { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                                                  core,
                                                  signers_[1].GetAddress(),
                                                  signers_[1].Sign( core_bytes ) } )
                     .has_value() );
    auto activated = registry_->TryActivatePolicyCandidate( proposed.value() );
    ASSERT_TRUE( activated.has_value() ) << activated.error().message();
    EXPECT_TRUE( activated.value() );
    EXPECT_EQ( registry_->GetCurrentPeers(), candidate.Canonicalized()->peers );
    EXPECT_EQ( store_->LoadAndVerify().value().policy, candidate.Canonicalized().value() );
}

TEST_F( OperatorApprovalTest, LateMembershipApprovalAfterActivationIsAcceptedInertAndAuditable )
{
    ConfirmGenesis();
    const auto [proposed_id, core]   = ActivateSuccessorAtThreshold();
    const auto durable_at_activation = store_->LoadAndVerify().value();
    ASSERT_EQ( secure_crdt_->ReadCandidateApprovals( proposed_id ).value().size(), 2U );

    // Raw CRDT seam: activation advanced the authorization context, so a late
    // distinct-signer record is rejected loudly — the same seam-split as the
    // raw duplicate-approval rule (workflow inert, raw loud) — never silently
    // stored or dropped.
    const auto late = secure_crdt_->SubmitCandidateApproval( { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                                                               core,
                                                               signers_[3].GetAddress(),
                                                               signers_[3].Sign( core.CanonicalBytes().value() ) } );
    EXPECT_TRUE( late.has_error() );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( proposed_id ).value().size(), 2U );

    // Workflow seam: the same late actor approving explicitly is an inert
    // success — a slow honest operator gets no error (D-13).
    const auto accepted = registry_->ApprovePolicyCandidate( proposed_id );
    ASSERT_TRUE( accepted.has_value() ) << accepted.error().message();
    EXPECT_EQ( store_->LoadAndVerify().value(), durable_at_activation );

    // Re-activating the already-durable candidate is idempotent, and the two
    // approvals activation consumed stay readable as the audit trace (D-15).
    const auto reactivated = registry_->TryActivatePolicyCandidate( proposed_id );
    ASSERT_TRUE( reactivated.has_value() ) << reactivated.error().message();
    EXPECT_FALSE( reactivated.value() );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( proposed_id ).value().size(), 2U );
    EXPECT_EQ( store_->LoadAndVerify().value(), durable_at_activation );
}

TEST_F( OperatorApprovalTest, DuplicateSelfApprovalAfterActivationIsInertWithoutNewRecord )
{
    ConfirmGenesis();
    const auto activated             = ActivateSuccessorAtThreshold();
    const auto durable_at_activation = store_->LoadAndVerify().value();
    sign_invocations_.store( 0 );

    auto again = registry_->ApprovePolicyCandidate( activated.first );
    ASSERT_TRUE( again.has_value() ) << again.error().message();
    EXPECT_EQ( sign_invocations_.load(), 0U );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( activated.first ).value().size(), 2U );
    EXPECT_EQ( store_->LoadAndVerify().value(), durable_at_activation );
}
