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
#include "storage/rocksdb/rocksdb.hpp"
#include "storage/rocksdb/rocksdb_batch.hpp"
#include "testutil/wait_condition.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/genesis_tool/GenesisCeremony.hpp"
#include "trustedpeer/genesis_tool/LocalTrustAdmin.hpp"

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

    outcome::result<void> CommitBatch( sgns::storage::rocksdb                         &database,
                                       const std::vector<TrustStateStore::Write> &writes )
    {
        auto batch = database.batch();
        if ( !batch )
        {
            return outcome::failure( std::errc::io_error );
        }
        for ( const auto &[key, value] : writes )
        {
            auto put = batch->put( key, value );
            if ( put.has_error() )
            {
                return put.error();
            }
        }
        return batch->commit();
    }

    TEST( TrustFirstBootE2ETest, ProductionControllerInitiatesBurnAndRestartRecoversPersistedApprovals )
    {
        sgns::test::securecrdt::EnsureLoggingSystemConfigured();
        const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( path );
        auto cleanup = [&] { boost::filesystem::remove_all( path ); };

        sgns::GeniusSigner bootstrapper{ ethereum::EthereumKeyGenerator( BOOTSTRAPPER_PRIVATE_KEY ) };
        auto peer_a       = sgns::GeniusSigner::Generate();
        auto peer_b       = sgns::GeniusSigner::Generate();
        auto non_member   = sgns::GeniusSigner::Generate();
        const std::string topic = "trust-first-boot-topic";

        const auto node_config_path = path / "peer-a-network.json";
        const auto peer_b_config_path = path / "peer-b-network.json";
        const auto non_member_config_path = path / "non-member-network.json";
        const auto tool_config_path = path / "tool-network.json";
        boost::filesystem::create_directories( path / "peer-a-globaldb" );
        boost::filesystem::create_directories( path / "peer-b-globaldb" );
        boost::filesystem::create_directories( path / "non-member-globaldb" );
        boost::filesystem::create_directories( path / "tool-globaldb" );
        WriteNetworkConfig( node_config_path );

        sgns::crdt::GlobalDbNetworkComposition::Config node_config;
        node_config.network_config_path = node_config_path.string();
        node_config.database_path       = ( path / "peer-a-globaldb" ).string();
        node_config.listen_topic        = topic;
        node_config.broadcast_topic     = topic;
        auto node_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create( std::move( node_config ) );
        ASSERT_TRUE( node_composition_result.has_value() ) << node_composition_result.error().message();
        auto node_composition = node_composition_result.value();
        ASSERT_TRUE( node_composition->Start().has_value() );
        ASSERT_NE( node_composition->db(), nullptr );
        ASSERT_FALSE( node_composition->interface_address().empty() );

        WriteNetworkConfig( peer_b_config_path, node_composition->interface_address() );
        WriteNetworkConfig( non_member_config_path, node_composition->interface_address() );
        WriteNetworkConfig( tool_config_path, node_composition->interface_address() );

        auto make_composition = [&]( const boost::filesystem::path &config_path,
                                     const boost::filesystem::path &database_path )
        {
            sgns::crdt::GlobalDbNetworkComposition::Config config;
            config.network_config_path = config_path.string();
            config.database_path       = database_path.string();
            config.listen_topic        = topic;
            config.broadcast_topic     = topic;
            return sgns::crdt::GlobalDbNetworkComposition::Create( std::move( config ) );
        };
        auto peer_b_composition_result = make_composition( peer_b_config_path, path / "peer-b-globaldb" );
        auto non_member_composition_result =
            make_composition( non_member_config_path, path / "non-member-globaldb" );
        ASSERT_TRUE( peer_b_composition_result.has_value() );
        ASSERT_TRUE( non_member_composition_result.has_value() );
        auto peer_b_composition = peer_b_composition_result.value();
        auto non_member_composition = non_member_composition_result.value();
        ASSERT_TRUE( peer_b_composition->Start().has_value() );
        ASSERT_TRUE( non_member_composition->Start().has_value() );

        auto peer_a_secure = std::make_shared<sgns::securecrdt::SecureCrdt>( node_composition->db(), topic );
        auto peer_b_secure = std::make_shared<sgns::securecrdt::SecureCrdt>( peer_b_composition->db(), topic );
        auto non_member_secure =
            std::make_shared<sgns::securecrdt::SecureCrdt>( non_member_composition->db(), topic );

        GenesisManifest manifest;
        manifest.network_id              = 42;
        manifest.bootstrapper_public_key = bootstrapper.GetAddress();
        manifest.peers                   = { peer_a.GetAddress(), peer_b.GetAddress() };
        manifest.membership_threshold    = 2;
        manifest.burn_threshold          = 2;
        manifest = manifest.Canonicalized().value();

        std::atomic_uint32_t peer_a_commit_count{ 0 };
        auto peer_a_store = TrustStateStore::Open(
            ( path / "peer-a-trust" ).string(),
            manifest.network_id,
            [&]( sgns::storage::rocksdb &database,
                 const std::vector<TrustStateStore::Write> &writes ) -> outcome::result<void>
            {
                if ( peer_a_commit_count.fetch_add( 1 ) >= 1 )
                {
                    return outcome::failure( std::errc::io_error );
                }
                return CommitBatch( database, writes );
            } ).value();
        auto peer_b_store =
            TrustStateStore::Open( ( path / "peer-b-trust" ).string(), manifest.network_id ).value();
        auto non_member_store =
            TrustStateStore::Open( ( path / "non-member-trust" ).string(), manifest.network_id ).value();

        std::atomic_uint32_t peer_a_activation_failures{ 0 };
        std::atomic_uint32_t non_member_activation_failures{ 0 };
        auto peer_a_controller_result = TrustStartupController::New(
            peer_a_secure,
            peer_a_store,
            manifest,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); },
            [&]( const auto &event )
            {
                if ( event.code == TrustStartupController::EventCode::TRUST_ACTIVATION_FAILED )
                    ++peer_a_activation_failures;
            } );
        auto peer_b_controller_result = TrustStartupController::New(
            peer_b_secure,
            peer_b_store,
            manifest,
            peer_b.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_b.Sign( bytes ); } );
        auto non_member_controller_result = TrustStartupController::New(
            non_member_secure,
            non_member_store,
            manifest,
            non_member.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return non_member.Sign( bytes ); },
            [&]( const auto &event )
            {
                if ( event.code == TrustStartupController::EventCode::TRUST_ACTIVATION_FAILED )
                    ++non_member_activation_failures;
            } );
        ASSERT_TRUE( peer_a_controller_result.has_value() );
        ASSERT_TRUE( peer_b_controller_result.has_value() );
        ASSERT_TRUE( non_member_controller_result.has_value() );
        auto peer_a_controller = peer_a_controller_result.value();
        auto peer_b_controller = peer_b_controller_result.value();
        auto non_member_controller = non_member_controller_result.value();
        peer_a_controller_result.value().reset();
        peer_b_controller_result.value().reset();
        non_member_controller_result.value().reset();

        EXPECT_EQ( peer_a_controller->GetState(), TrustStartupController::State::FreshWaitingForGenesis );
        EXPECT_EQ( peer_b_controller->GetState(), TrustStartupController::State::FreshWaitingForGenesis );
        EXPECT_EQ( non_member_controller->GetState(), TrustStartupController::State::FreshWaitingForGenesis );

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
                return peer_a_store->LoadAndVerify().has_value() && peer_b_store->LoadAndVerify().has_value() &&
                       non_member_store->LoadAndVerify().has_value();
            },
            std::chrono::seconds( 5 ),
            "reviewed genesis did not become durable on every production controller" );

        const auto burn_core = sgns::account::BurnConfig::BurnCandidateCore( peer_a_store->LoadAndVerify().value().burn );
        ASSERT_TRUE( burn_core.has_value() );
        const auto burn_candidate = CandidateId::FromCore( *burn_core );
        ASSERT_TRUE( burn_candidate.has_value() );
        sgns::test::assertWaitForCondition(
            [&]
            {
                (void) peer_a_controller->Refresh();
                (void) peer_b_controller->Refresh();
                (void) non_member_controller->Refresh();
                auto approvals = peer_a_secure->ReadCandidateApprovals( *burn_candidate );
                return approvals.has_value() && approvals.value().size() == 2U &&
                       peer_b_controller->GetState() == TrustStartupController::State::ConfirmedReady;
            },
            std::chrono::seconds( 5 ),
            "production controllers did not publish exactly two initial-burn approvals" );

        const auto approvals_before_restart = peer_a_secure->ReadCandidateApprovals( *burn_candidate ).value();
        EXPECT_EQ( std::count_if( approvals_before_restart.begin(),
                                  approvals_before_restart.end(),
                                  [&]( const auto &approval ) { return approval.signer == peer_a.GetAddress(); } ),
                   1 );
        EXPECT_EQ( std::count_if( approvals_before_restart.begin(),
                                  approvals_before_restart.end(),
                                  [&]( const auto &approval ) { return approval.signer == peer_b.GetAddress(); } ),
                   1 );
        EXPECT_EQ( std::count_if( approvals_before_restart.begin(),
                                  approvals_before_restart.end(),
                                  [&]( const auto &approval ) { return approval.signer == non_member.GetAddress(); } ),
                   0 );
        EXPECT_FALSE( peer_a_controller->IsEconomicallyReady() );
        EXPECT_EQ( peer_a_activation_failures.load(), 1U );
        EXPECT_EQ( non_member_activation_failures.load(), 0U );

        peer_a_controller.reset();
        peer_a_store.reset();
        outcome::result<std::shared_ptr<TrustStateStore>> reopened_peer_a_store_result =
            outcome::failure( std::errc::resource_unavailable_try_again );
        sgns::test::assertWaitForCondition(
            [&]
            {
                reopened_peer_a_store_result =
                    TrustStateStore::Open( ( path / "peer-a-trust" ).string(), manifest.network_id );
                return reopened_peer_a_store_result.has_value();
            },
            std::chrono::seconds( 5 ),
            "failed controller retained the trust-store lock after destruction" );
        ASSERT_TRUE( reopened_peer_a_store_result.has_value() );
        auto reopened_peer_a_store = reopened_peer_a_store_result.value();
        auto restarted = TrustStartupController::New(
            peer_a_secure,
            reopened_peer_a_store,
            manifest,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); } );
        ASSERT_TRUE( restarted.has_value() ) << restarted.error().message();
        peer_a_controller = restarted.value();
        ASSERT_TRUE( peer_a_controller->Refresh().has_value() );
        EXPECT_EQ( peer_a_controller->GetState(), TrustStartupController::State::ConfirmedReady );
        EXPECT_TRUE( peer_a_controller->IsEconomicallyReady() );
        EXPECT_EQ( peer_a_controller->burn_config()->GetCachedBasisPoints(), 100U );
        const auto approvals_after_restart = peer_a_secure->ReadCandidateApprovals( *burn_candidate ).value();
        EXPECT_EQ( approvals_after_restart.size(), approvals_before_restart.size() );
        EXPECT_EQ( CandidateId::FromCore( approvals_after_restart.front().core ), burn_candidate );

        peer_a_controller.reset();
        peer_b_controller.reset();
        non_member_controller.reset();
        tool_burn.reset();
        tool_registry.reset();
        tool_secure.reset();
        tool_store.reset();
        tool_composition->Stop();
        non_member_composition->Stop();
        peer_b_composition->Stop();
        node_composition->Stop();
        cleanup();
    }

    TEST( TrustFirstBootE2ETest, PreloadedRefreshActivationFailuresAreReturnedAndEmitted )
    {
        sgns::test::securecrdt::EnsureLoggingSystemConfigured();
        const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( path );
        auto cleanup = [&] { boost::filesystem::remove_all( path ); };
        auto bootstrapper = sgns::GeniusSigner::Generate();
        auto peer_a = sgns::GeniusSigner::Generate();
        auto peer_b = sgns::GeniusSigner::Generate();
        auto non_member = sgns::GeniusSigner::Generate();
        GenesisManifest manifest;
        manifest.network_id = 42;
        manifest.bootstrapper_public_key = bootstrapper.GetAddress();
        manifest.peers = { peer_a.GetAddress(), peer_b.GetAddress() };
        manifest.membership_threshold = 2;
        manifest.burn_threshold = 2;
        manifest = manifest.Canonicalized().value();
        const auto manifest_bytes = manifest.CanonicalBytes().value();
        const auto bootstrap_signature = bootstrapper.Sign( manifest_bytes );
        auto fail_commits = []( sgns::storage::rocksdb &, const std::vector<TrustStateStore::Write> & )
            -> outcome::result<void> { return outcome::failure( std::errc::io_error ); };

        auto genesis_node = sgns::test::securecrdt::MakeSecureCrdtTestNode( "preloaded_genesis_failure" );
        ASSERT_NE( genesis_node, nullptr );
        auto genesis_secure =
            std::make_shared<sgns::securecrdt::SecureCrdt>( genesis_node->db, "preloaded-genesis-topic" );
        auto genesis_preload_store =
            TrustStateStore::Open( ( path / "genesis-preload" ).string(), manifest.network_id ).value();
        auto genesis_preloader = sgns::trustedpeer::TrustedPeerRegistry::NewProduction(
            genesis_secure,
            genesis_preload_store,
            manifest,
            bootstrap_signature,
            bootstrapper.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return bootstrapper.Sign( bytes ); } ).value();
        const auto genesis_id = genesis_preloader->SubmitReviewedGenesisApproval();
        ASSERT_TRUE( genesis_id.has_value() );
        genesis_preloader.reset();
        genesis_preload_store.reset();

        auto failing_genesis_store = TrustStateStore::Open(
            ( path / "genesis-target" ).string(), manifest.network_id, fail_commits ).value();
        std::vector<TrustStartupController::Event> genesis_events;
        auto failed_genesis = TrustStartupController::New(
            genesis_secure,
            failing_genesis_store,
            manifest,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); },
            [&]( const auto &event ) { genesis_events.push_back( event ); } );
        ASSERT_TRUE( failed_genesis.has_error() );
        EXPECT_EQ( failed_genesis.error(), TrustStateStore::Error::COMMIT_FAILED );
        ASSERT_EQ( genesis_events.size(), 1U );
        EXPECT_EQ( genesis_events.front().code, TrustStartupController::EventCode::TRUST_ACTIVATION_FAILED );
        EXPECT_EQ( genesis_events.front().fields.at( 0 ), genesis_id.value().domain );
        EXPECT_EQ( genesis_events.front().fields.at( 1 ), std::to_string( genesis_id.value().version ) );
        EXPECT_EQ( genesis_events.front().fields.at( 2 ), genesis_id.value().content_hash );
        EXPECT_EQ( genesis_events.front().fields.at( 3 ), failed_genesis.error().message() );
        failing_genesis_store.reset();

        auto recovered_genesis_store =
            TrustStateStore::Open( ( path / "genesis-target" ).string(), manifest.network_id ).value();
        auto recovered_genesis = TrustStartupController::New(
            genesis_secure,
            recovered_genesis_store,
            manifest,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); } );
        ASSERT_TRUE( recovered_genesis.has_value() );
        EXPECT_EQ( recovered_genesis.value()->GetState(), TrustStartupController::State::WaitingForInitialBurn );
        recovered_genesis.value().reset();
        recovered_genesis_store.reset();

        auto burn_node = sgns::test::securecrdt::MakeSecureCrdtTestNode( "preloaded_burn_failure" );
        ASSERT_NE( burn_node, nullptr );
        auto burn_secure = std::make_shared<sgns::securecrdt::SecureCrdt>( burn_node->db, "preloaded-burn-topic" );
        auto burn_store = TrustStateStore::Open( ( path / "burn-target" ).string(), manifest.network_id ).value();
        ASSERT_TRUE( burn_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );
        auto burn_registry = sgns::trustedpeer::TrustedPeerRegistry::NewProduction(
            burn_secure,
            burn_store,
            manifest,
            {},
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); } ).value();
        auto burn_config = sgns::account::BurnConfig::NewProduction(
            burn_secure,
            burn_registry,
            burn_store,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); } ).value();
        const auto burn_core = sgns::account::BurnConfig::BurnCandidateCore( burn_store->LoadAndVerify().value().burn );
        ASSERT_TRUE( burn_core.has_value() );
        const auto burn_id = CandidateId::FromCore( *burn_core );
        ASSERT_TRUE( burn_id.has_value() );
        const auto burn_bytes = burn_core->CanonicalBytes().value();
        ASSERT_TRUE( burn_secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION, *burn_core, peer_a.GetAddress(), peer_a.Sign( burn_bytes ) } )
                         .has_value() );
        ASSERT_TRUE( burn_secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION, *burn_core, peer_b.GetAddress(), peer_b.Sign( burn_bytes ) } )
                         .has_value() );
        burn_config.reset();
        burn_registry.reset();
        burn_store.reset();

        auto failing_burn_store =
            TrustStateStore::Open( ( path / "burn-target" ).string(), manifest.network_id, fail_commits ).value();
        std::vector<TrustStartupController::Event> burn_events;
        auto failed_burn = TrustStartupController::New(
            burn_secure,
            failing_burn_store,
            manifest,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); },
            [&]( const auto &event ) { burn_events.push_back( event ); } );
        ASSERT_TRUE( failed_burn.has_error() );
        EXPECT_EQ( failed_burn.error(), TrustStateStore::Error::COMMIT_FAILED );
        ASSERT_EQ( burn_events.size(), 1U );
        EXPECT_EQ( burn_events.front().code, TrustStartupController::EventCode::TRUST_ACTIVATION_FAILED );
        EXPECT_EQ( burn_events.front().fields.at( 0 ), burn_id->domain );
        EXPECT_EQ( burn_events.front().fields.at( 1 ), std::to_string( burn_id->version ) );
        EXPECT_EQ( burn_events.front().fields.at( 2 ), burn_id->content_hash );
        EXPECT_EQ( burn_events.front().fields.at( 3 ), failed_burn.error().message() );
        failing_burn_store.reset();

        auto recovered_burn_store =
            TrustStateStore::Open( ( path / "burn-target" ).string(), manifest.network_id ).value();
        auto recovered_burn = TrustStartupController::New(
            burn_secure,
            recovered_burn_store,
            manifest,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); } );
        ASSERT_TRUE( recovered_burn.has_value() );
        EXPECT_EQ( recovered_burn.value()->GetState(), TrustStartupController::State::ConfirmedReady );
        EXPECT_TRUE( recovered_burn.value()->IsEconomicallyReady() );
        recovered_burn.value().reset();
        recovered_burn_store.reset();

        auto pending_node = sgns::test::securecrdt::MakeSecureCrdtTestNode( "preloaded_pending_controls" );
        ASSERT_NE( pending_node, nullptr );
        auto pending_secure =
            std::make_shared<sgns::securecrdt::SecureCrdt>( pending_node->db, "preloaded-pending-topic" );
        auto pending_store =
            TrustStateStore::Open( ( path / "pending-target" ).string(), manifest.network_id ).value();
        ASSERT_TRUE( pending_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );
        auto pending_registry = sgns::trustedpeer::TrustedPeerRegistry::NewProduction(
            pending_secure,
            pending_store,
            manifest,
            {},
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); } ).value();
        auto pending_burn = sgns::account::BurnConfig::NewProduction(
            pending_secure,
            pending_registry,
            pending_store,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); } ).value();
        const auto pending_core =
            sgns::account::BurnConfig::BurnCandidateCore( pending_store->LoadAndVerify().value().burn ).value();
        const auto pending_id = CandidateId::FromCore( pending_core ).value();
        const auto pending_bytes = pending_core.CanonicalBytes().value();
        ASSERT_TRUE( pending_secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              pending_core,
              peer_a.GetAddress(),
              peer_a.Sign( pending_bytes ) } ).has_value() );
        pending_burn.reset();
        pending_registry.reset();
        pending_store.reset();

        auto below_quorum_store =
            TrustStateStore::Open( ( path / "pending-target" ).string(), manifest.network_id ).value();
        std::vector<TrustStartupController::Event> pending_events;
        auto below_quorum = TrustStartupController::New(
            pending_secure,
            below_quorum_store,
            manifest,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); },
            [&]( const auto &event ) { pending_events.push_back( event ); } );
        ASSERT_TRUE( below_quorum.has_value() );
        EXPECT_EQ( below_quorum.value()->GetState(), TrustStartupController::State::WaitingForInitialBurn );
        EXPECT_TRUE( pending_events.empty() );
        EXPECT_EQ( pending_secure->ReadCandidateApprovals( pending_id ).value().size(), 1U );
        below_quorum.value().reset();
        below_quorum_store.reset();

        auto non_member_store =
            TrustStateStore::Open( ( path / "pending-target" ).string(), manifest.network_id ).value();
        std::vector<TrustStartupController::Event> non_member_events;
        auto non_member_controller = TrustStartupController::New(
            pending_secure,
            non_member_store,
            manifest,
            non_member.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return non_member.Sign( bytes ); },
            [&]( const auto &event ) { non_member_events.push_back( event ); } );
        ASSERT_TRUE( non_member_controller.has_value() );
        EXPECT_EQ( non_member_controller.value()->GetState(), TrustStartupController::State::WaitingForInitialBurn );
        EXPECT_TRUE( non_member_events.empty() );
        const auto pending_approvals = pending_secure->ReadCandidateApprovals( pending_id ).value();
        EXPECT_EQ( pending_approvals.size(), 1U );
        EXPECT_TRUE( std::none_of( pending_approvals.begin(), pending_approvals.end(), [&]( const auto &approval ) {
            return approval.signer == non_member.GetAddress();
        } ) );

        non_member_controller.value().reset();
        non_member_store.reset();

        std::atomic_bool pending_fail_commits{ false };
        std::atomic_uint32_t pending_commit_attempts{ 0 };
        auto callback_failure_store = TrustStateStore::Open(
            ( path / "pending-target" ).string(),
            manifest.network_id,
            [&]( sgns::storage::rocksdb &database,
                 const std::vector<TrustStateStore::Write> &writes ) -> outcome::result<void>
            {
                if ( pending_fail_commits.load() )
                {
                    ++pending_commit_attempts;
                    return outcome::failure( std::errc::io_error );
                }
                return CommitBatch( database, writes );
            } ).value();
        std::vector<TrustStartupController::Event> callback_events;
        std::atomic_uint32_t callback_event_count{ 0 };
        auto callback_failure = TrustStartupController::New(
            pending_secure,
            callback_failure_store,
            manifest,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); },
            [&]( const auto &event )
            {
                callback_events.push_back( event );
                callback_event_count.store( callback_events.size(), std::memory_order_release );
            } );
        ASSERT_TRUE( callback_failure.has_value() );
        pending_fail_commits.store( true );
        ASSERT_TRUE( pending_secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              pending_core,
              peer_b.GetAddress(),
              peer_b.Sign( pending_bytes ) } ).has_value() );
        sgns::test::assertWaitForCondition(
            [&]
            {
                return callback_event_count.load( std::memory_order_acquire ) == 1U &&
                       pending_commit_attempts.load() == 1U;
            },
            std::chrono::seconds( 5 ),
            "quorate callback failure was not emitted" );
        ASSERT_EQ( callback_events.size(), 1U );
        EXPECT_EQ( callback_events.front().code, TrustStartupController::EventCode::TRUST_ACTIVATION_FAILED );
        const auto attempts_after_callback = pending_commit_attempts.load();
        auto repeated_refresh = callback_failure.value()->Refresh();
        EXPECT_TRUE( repeated_refresh.has_value() );
        EXPECT_EQ( pending_commit_attempts.load(), attempts_after_callback );
        callback_failure.value().reset();
        callback_failure_store.reset();

        auto recovered_pending_store =
            TrustStateStore::Open( ( path / "pending-target" ).string(), manifest.network_id ).value();
        auto recovered_pending = TrustStartupController::New(
            pending_secure,
            recovered_pending_store,
            manifest,
            peer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return peer_a.Sign( bytes ); } );
        ASSERT_TRUE( recovered_pending.has_value() );
        EXPECT_EQ( recovered_pending.value()->GetState(), TrustStartupController::State::ConfirmedReady );
        recovered_pending.value().reset();
        recovered_pending_store.reset();

        pending_secure.reset();
        pending_node.reset();
        burn_secure.reset();
        burn_node.reset();
        genesis_secure.reset();
        genesis_node.reset();
        cleanup();
    }
} // namespace
