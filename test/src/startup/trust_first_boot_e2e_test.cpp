#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusSigner.hpp"
#include "account/TrustStartupController.hpp"
#include "account/BurnConfig.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "testutil/wait_condition.hpp"
#include "trustedpeer/TrustStateStore.hpp"

namespace
{
    using sgns::account::TrustStartupController;
    using sgns::securecrdt::CandidateApprovalRecord;
    using sgns::securecrdt::CandidateCore;
    using sgns::securecrdt::CandidateId;
    using sgns::trustedpeer::GenesisManifest;
    using sgns::trustedpeer::TrustStateStore;

    TEST( TrustFirstBootE2ETest, FreshStateIsRestrictedUntilBothDurableGenesisStages )
    {
        const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( path );
        auto cleanup = [&] { boost::filesystem::remove_all( path ); };

        auto bootstrapper = sgns::GeniusSigner::Generate();
        auto peer_a       = sgns::GeniusSigner::Generate();
        auto peer_b       = sgns::GeniusSigner::Generate();
        auto node         = sgns::test::securecrdt::MakeSecureCrdtTestNode( "trust_first_boot" );
        ASSERT_NE( node, nullptr );
        auto secure       = std::make_shared<sgns::securecrdt::SecureCrdt>( node->db, "trust-first-boot-topic" );
        auto store_result = TrustStateStore::Open( ( path / "trust-state" ).string(), 42 );
        ASSERT_TRUE( store_result.has_value() );

        GenesisManifest manifest;
        manifest.network_id              = 42;
        manifest.bootstrapper_public_key = bootstrapper.GetAddress();
        manifest.peers                   = { peer_a.GetAddress(), peer_b.GetAddress() };
        manifest.membership_threshold    = 2;
        manifest.burn_threshold          = 2;

        auto controller_result = TrustStartupController::New( secure,
                                                              store_result.value(),
                                                              manifest,
                                                              peer_a.GetAddress(),
                                                              [&]( const std::vector<uint8_t> &bytes )
                                                              { return peer_a.Sign( bytes ); } );
        ASSERT_TRUE( controller_result.has_value() ) << controller_result.error().message();
        auto controller = controller_result.value();

        EXPECT_EQ( controller->GetState(), TrustStartupController::State::FreshWaitingForGenesis );
        EXPECT_TRUE( controller->GetCurrentPeers().empty() );
        EXPECT_FALSE( controller->CanApproveSuccessors() );
        EXPECT_FALSE( controller->IsEconomicallyReady() );

        const auto    fingerprint = manifest.Fingerprint().value();
        CandidateCore genesis_core{ CandidateCore::ENCODING_VERSION,
                                    "trusted-peer-genesis",
                                    manifest.network_id,
                                    sgns::securecrdt::CandidateKind::TrustedPeerGenesis,
                                    manifest.policy_version,
                                    fingerprint,
                                    fingerprint,
                                    manifest.CanonicalBytes().value() };
        const auto    genesis_bytes = genesis_core.CanonicalBytes().value();
        ASSERT_TRUE( secure
                         ->SubmitCandidateApproval( { CandidateApprovalRecord::ENCODING_VERSION,
                                                      genesis_core,
                                                      bootstrapper.GetAddress(),
                                                      bootstrapper.Sign( genesis_bytes ) } )
                         .has_value() );
        sgns::test::assertWaitForCondition(
            [&]
            {
                return controller->Refresh().has_value() &&
                       controller->GetState() == TrustStartupController::State::WaitingForInitialBurn;
            },
            std::chrono::seconds( 5 ),
            "reviewed genesis candidate did not become durable" );

        EXPECT_EQ( controller->GetState(), TrustStartupController::State::WaitingForInitialBurn );
        EXPECT_EQ( controller->GetCurrentPeers(), manifest.Canonicalized()->peers );
        EXPECT_TRUE( controller->CanApproveSuccessors() );
        EXPECT_FALSE( controller->IsEconomicallyReady() );
        EXPECT_EQ( store_result.value()->LoadAndVerify().value().genesis_fingerprint, fingerprint );

        auto burn_candidate = controller->burn_config()->OnTrustedPeerGenesisConfirmed();
        ASSERT_TRUE( burn_candidate.has_value() ) << burn_candidate.error().message();
        auto approvals = secure->ReadCandidateApprovals( burn_candidate.value() ).value();
        ASSERT_EQ( approvals.size(), 1U );
        const auto burn_core_bytes = approvals.front().core.CanonicalBytes().value();
        ASSERT_TRUE( secure
                         ->SubmitCandidateApproval( { CandidateApprovalRecord::ENCODING_VERSION,
                                                      approvals.front().core,
                                                      peer_b.GetAddress(),
                                                      peer_b.Sign( burn_core_bytes ) } )
                         .has_value() );
        sgns::test::assertWaitForCondition(
            [&]
            {
                return controller->Refresh().has_value() &&
                       controller->GetState() == TrustStartupController::State::ConfirmedReady;
            },
            std::chrono::seconds( 5 ),
            "burn genesis candidate did not reach durable quorum" );

        EXPECT_EQ( controller->GetState(), TrustStartupController::State::ConfirmedReady );
        EXPECT_TRUE( controller->IsEconomicallyReady() );
        EXPECT_EQ( controller->burn_config()->GetCachedBasisPoints(), 100U );

        controller.reset();
        secure.reset();
        node.reset();
        store_result.value().reset();
        cleanup();
    }
} // namespace
