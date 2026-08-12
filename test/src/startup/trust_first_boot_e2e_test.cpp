#include <gtest/gtest.h>

#include <sys/stat.h>

#include <boost/filesystem/operations.hpp>

#include <fstream>
#include <sstream>

#include "ProofSystem/EthereumKeyGenerator.hpp"
#include "account/BurnConfig.hpp"
#include "account/GeniusSigner.hpp"
#include "account/TrustStartupController.hpp"
#include "crdt/globaldb/GlobalDbNetworkComposition.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "testutil/wait_condition.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/genesis_tool/GenesisCeremony.hpp"

namespace
{
    using sgns::account::TrustStartupController;
    using sgns::securecrdt::CandidateApprovalRecord;
    using sgns::securecrdt::CandidateCore;
    using sgns::securecrdt::CandidateId;
    using sgns::trustedpeer::GenesisManifest;
    using sgns::trustedpeer::GenesisCeremony;
    using sgns::trustedpeer::TrustStateStore;

    constexpr char BOOTSTRAPPER_PRIVATE_KEY[] =
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    void WriteNetworkConfig( const boost::filesystem::path &path,
                             const std::optional<std::string> &bootstrap = std::nullopt )
    {
        std::ofstream output( path.string() );
        ASSERT_TRUE( output.good() );
        output << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","high_water":20,"low_water":1,"bootstrap_addresses":[)";
        if ( bootstrap )
        {
            output << '"' << *bootstrap << '"';
        }
        output << "]}";
    }

    TEST( TrustFirstBootE2ETest, FreshStateIsRestrictedUntilBothDurableGenesisStages )
    {
        sgns::test::securecrdt::EnsureLoggingSystemConfigured();
        const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( path );
        auto cleanup = [&] { boost::filesystem::remove_all( path ); };

        sgns::GeniusSigner bootstrapper{ ethereum::EthereumKeyGenerator( BOOTSTRAPPER_PRIVATE_KEY ) };
        auto peer_a       = sgns::GeniusSigner::Generate();
        auto peer_b       = sgns::GeniusSigner::Generate();
        const std::string topic = "trust-first-boot-topic";

        const auto node_config_path = path / "node-network.json";
        const auto tool_config_path = path / "tool-network.json";
        boost::filesystem::create_directories( path / "node-globaldb" );
        boost::filesystem::create_directories( path / "tool-globaldb" );
        WriteNetworkConfig( node_config_path );

        sgns::crdt::GlobalDbNetworkComposition::Config node_config;
        node_config.network_config_path = node_config_path.string();
        node_config.database_path       = ( path / "node-globaldb" ).string();
        node_config.listen_topic        = topic;
        node_config.broadcast_topic     = topic;
        auto node_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create( std::move( node_config ) );
        ASSERT_TRUE( node_composition_result.has_value() ) << node_composition_result.error().message();
        auto node_composition = node_composition_result.value();
        ASSERT_TRUE( node_composition->Start().has_value() );
        ASSERT_NE( node_composition->db(), nullptr );
        ASSERT_FALSE( node_composition->interface_address().empty() );

        WriteNetworkConfig( tool_config_path, node_composition->interface_address() );
        auto secure = std::make_shared<sgns::securecrdt::SecureCrdt>( node_composition->db(), topic );
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

        sgns::crdt::GlobalDbNetworkComposition::Config tool_config;
        tool_config.network_config_path = tool_config_path.string();
        tool_config.database_path       = ( path / "tool-globaldb" ).string();
        tool_config.listen_topic        = topic;
        tool_config.broadcast_topic     = topic;
        auto tool_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create( std::move( tool_config ) );
        ASSERT_TRUE( tool_composition_result.has_value() ) << tool_composition_result.error().message();
        auto tool_composition = tool_composition_result.value();

        std::shared_ptr<sgns::securecrdt::SecureCrdt>           tool_secure;
        std::shared_ptr<TrustStateStore>                        tool_store;
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> tool_registry;
        std::shared_ptr<sgns::account::BurnConfig>              tool_burn;
        GenesisCeremony::Network ceremony_network;
        ceremony_network.start = [&]
        {
            return tool_composition->Start();
        };
        ceremony_network.submit = [&]( const GenesisManifest &reviewed,
                                       const std::vector<uint8_t> &signature,
                                       const std::string &address,
                                       sgns::trustedpeer::TrustedPeerRegistry::SignCallback sign )
            -> outcome::result<CandidateId>
        {
            tool_secure = std::make_shared<sgns::securecrdt::SecureCrdt>( tool_composition->db(), topic );
            BOOST_OUTCOME_TRY( tool_store,
                               TrustStateStore::Open( ( path / "tool-globaldb/trust-state" ).string(), 42 ) );
            BOOST_OUTCOME_TRY( tool_registry,
                               sgns::trustedpeer::TrustedPeerRegistry::NewProduction(
                                   tool_secure, tool_store, reviewed, signature, address, sign ) );
            BOOST_OUTCOME_TRY( tool_burn,
                               sgns::account::BurnConfig::NewProduction(
                                   tool_secure, tool_registry, tool_store, address, std::move( sign ) ) );
            if ( !tool_secure->RegisterFilters() )
            {
                return outcome::failure( std::errc::operation_not_permitted );
            }
            return tool_registry->SubmitReviewedGenesisApproval();
        };
        ceremony_network.confirmed = [&]() -> outcome::result<std::optional<sgns::trustedpeer::ConfirmedTrustSnapshot>>
        {
            if ( !tool_store )
            {
                return std::optional<sgns::trustedpeer::ConfirmedTrustSnapshot>{};
            }
            auto loaded = tool_store->LoadAndVerify();
            if ( loaded.has_error() )
            {
                return loaded.error();
            }
            return std::optional<sgns::trustedpeer::ConfirmedTrustSnapshot>( loaded.value() );
        };

        const auto key_path = path / "bootstrap.key";
        {
            std::ofstream key_file( key_path.string() );
            ASSERT_TRUE( key_file.good() );
            key_file << BOOTSTRAPPER_PRIVATE_KEY << '\n';
        }
        ASSERT_EQ( ::chmod( key_path.c_str(), 0600 ), 0 );
        const auto fingerprint = manifest.Fingerprint().value();
        std::istringstream ceremony_input( fingerprint + "\n" );
        std::ostringstream ceremony_output;
        std::ostringstream ceremony_errors;
        GenesisCeremony::Request ceremony_request;
        ceremony_request.manifest             = manifest.Canonicalized().value();
        ceremony_request.key_file             = key_path.string();
        ceremony_request.confirmation_timeout = std::chrono::seconds( 5 );
        ceremony_request.poll_interval        = std::chrono::milliseconds( 25 );
        GenesisCeremony ceremony;
        auto ceremony_result = ceremony.Run(
            ceremony_request, ceremony_network, ceremony_input, ceremony_output, ceremony_errors );
        ASSERT_TRUE( ceremony_result.has_value() ) << ceremony_errors.str();
        EXPECT_FALSE( boost::filesystem::exists( key_path ) );

        sgns::test::assertWaitForCondition(
            [&]
            {
                return controller->Refresh().has_value() &&
                       controller->GetState() == TrustStartupController::State::WaitingForInitialBurn;
            },
            std::chrono::seconds( 5 ),
            "reviewed genesis candidate did not become durable" );

        EXPECT_EQ( controller->GetState(), TrustStartupController::State::WaitingForInitialBurn );
        EXPECT_NE( node_composition->db(), nullptr );
        EXPECT_NE( tool_composition->db(), nullptr );
        EXPECT_EQ( controller->GetCurrentPeers(), manifest.Canonicalized()->peers );
        EXPECT_TRUE( controller->CanApproveSuccessors() );
        EXPECT_FALSE( controller->IsEconomicallyReady() );
        EXPECT_EQ( store_result.value()->LoadAndVerify().value().genesis_fingerprint, fingerprint );
        EXPECT_EQ( tool_store->LoadAndVerify().value().genesis_fingerprint, fingerprint );

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
        EXPECT_NE( node_composition->db(), nullptr );
        EXPECT_TRUE( controller->IsEconomicallyReady() );
        EXPECT_EQ( controller->burn_config()->GetCachedBasisPoints(), 100U );

        controller.reset();
        secure.reset();
        store_result.value().reset();
        tool_burn.reset();
        tool_registry.reset();
        tool_secure.reset();
        tool_store.reset();
        tool_composition->Stop();
        node_composition->Stop();
        cleanup();
    }
} // namespace
