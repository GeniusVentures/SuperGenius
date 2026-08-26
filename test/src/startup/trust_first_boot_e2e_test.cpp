#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <boost/filesystem/operations.hpp>

#include <fstream>
#include <algorithm>
#include <deque>
#include <mutex>
#include <sstream>

#include "account/BurnConfig.hpp"
#include "account/GeniusSigner.hpp"
#include "base/hexutil.hpp"
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

    sgns::GeniusSigner::PrivateKey BootstrapperPrivateKey()
    {
        auto key_bytes = sgns::base::unhex( BOOTSTRAPPER_PRIVATE_KEY );
        EXPECT_FALSE( key_bytes.has_error() );
        sgns::GeniusSigner::PrivateKey secret_key{};
        if ( !key_bytes.has_error() && key_bytes.value().size() == secret_key.size() )
        {
            std::copy( key_bytes.value().begin(), key_bytes.value().end(), secret_key.begin() );
        }
        return secret_key;
    }

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

    struct RefreshObservations
    {
        std::mutex                                      mutex;
        std::vector<uint32_t>                           attempts;
        std::vector<uint64_t>                           delays;
        std::deque<std::function<void()>>               timers;
        std::vector<TrustStartupController::Event>      events;
        std::function<void()>                           request_refresh;
        uint32_t                                        coalesced_requests = 0;
        uint32_t                                        idle_notifications = 0;
        bool                                            idle = false;

        std::vector<uint32_t> Attempts()
        {
            std::lock_guard<std::mutex> lock( mutex );
            return attempts;
        }

        std::vector<uint64_t> Delays()
        {
            std::lock_guard<std::mutex> lock( mutex );
            return delays;
        }

        std::function<void()> PopTimer()
        {
            std::lock_guard<std::mutex> lock( mutex );
            if ( timers.empty() ) return {};
            auto timer = std::move( timers.front() );
            timers.pop_front();
            return timer;
        }
    };

    std::shared_ptr<TrustStartupController::RefreshTestHooks> MakeRefreshHooks(
        const std::shared_ptr<RefreshObservations> &observations )
    {
        auto hooks = std::make_shared<TrustStartupController::RefreshTestHooks>();
        hooks->observe_attempt = [observations]( uint32_t attempt )
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            observations->attempts.push_back( attempt );
            observations->idle = false;
        };
        hooks->schedule_retry = [observations]( std::chrono::milliseconds delay, std::function<void()> retry )
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            observations->delays.push_back( static_cast<uint64_t>( delay.count() ) );
            observations->timers.push_back( std::move( retry ) );
        };
        hooks->observe_dispatch_idle = [observations]
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            observations->idle = true;
            ++observations->idle_notifications;
        };
        hooks->observe_coalesced_request = [observations]
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            ++observations->coalesced_requests;
        };
        hooks->bind_request_refresh = [observations]( std::function<void()> request )
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            observations->request_refresh = std::move( request );
        };
        return hooks;
    }

    struct RefreshHarness
    {
        boost::filesystem::path path;
        std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode> node;
        std::shared_ptr<sgns::securecrdt::SecureCrdt> secure;
        std::shared_ptr<TrustStateStore> store;
        std::shared_ptr<TrustStartupController> controller;
        std::vector<sgns::GeniusSigner> signers;
        GenesisManifest manifest;

        ~RefreshHarness()
        {
            controller.reset();
            store.reset();
            secure.reset();
            node.reset();
            boost::filesystem::remove_all( path );
        }
    };

    std::unique_ptr<RefreshHarness> MakeReadyRefreshHarness(
        const std::string &name,
        const std::shared_ptr<TrustStartupController::RefreshTestHooks> &hooks,
        TrustStartupController::EventCallback event_callback = {} )
    {
        auto harness = std::make_unique<RefreshHarness>();
        harness->path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( harness->path );
        harness->node = sgns::test::securecrdt::MakeSecureCrdtTestNode( name );
        if ( !harness->node ) return nullptr;
        harness->secure = std::make_shared<sgns::securecrdt::SecureCrdt>( harness->node->db, name + "-topic" );
        harness->signers = { sgns::GeniusSigner::Generate(),
                             sgns::GeniusSigner::Generate(),
                             sgns::GeniusSigner::Generate() };
        harness->manifest.network_id = 42;
        harness->manifest.bootstrapper_public_key = harness->signers[0].GetAddress();
        harness->manifest.peers = { harness->signers[0].GetAddress(),
                                    harness->signers[1].GetAddress(),
                                    harness->signers[2].GetAddress() };
        harness->manifest.membership_threshold = 2;
        harness->manifest.burn_threshold = 2;
        harness->manifest = harness->manifest.Canonicalized().value();
        auto opened = TrustStateStore::Open( ( harness->path / "trust" ).string(), harness->manifest.network_id );
        if ( opened.has_error() ) return nullptr;
        harness->store = opened.value();
        auto initial = harness->store->CommitGenesis(
            harness->manifest,
            harness->signers[0].Sign( harness->manifest.CanonicalBytes().value() ) );
        if ( initial.has_error() ) return nullptr;
        const auto burn_core = sgns::account::BurnConfig::BurnCandidateCore( initial.value().burn );
        if ( !burn_core ) return nullptr;
        const auto burn_bytes = burn_core->CanonicalBytes();
        if ( !burn_bytes ) return nullptr;
        auto ready = harness->store->CommitBurnSuccessor(
            initial.value().burn,
            { { harness->signers[0].GetAddress(), harness->signers[0].Sign( *burn_bytes ) },
              { harness->signers[1].GetAddress(), harness->signers[1].Sign( *burn_bytes ) } },
            *burn_bytes );
        if ( ready.has_error() ) return nullptr;
        auto created = TrustStartupController::New(
            harness->secure,
            harness->store,
            harness->manifest,
            harness->signers[2].GetAddress(),
            [signer = harness->signers[2]]( const std::vector<uint8_t> &bytes ) mutable
            { return signer.Sign( bytes ); },
            std::move( event_callback ),
            {},
            hooks );
        if ( created.has_error() ) return nullptr;
        harness->controller = created.value();
        return harness;
    }

    TEST( TrustFirstBootE2ETest, PolicyV2BeforeInitialBurnCannotStrandStartup )
    {
        sgns::test::securecrdt::EnsureLoggingSystemConfigured();
        const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( path );
        auto cleanup = [&] { boost::filesystem::remove_all( path ); };

        auto bootstrapper = sgns::GeniusSigner::Generate();
        auto operator_a = sgns::GeniusSigner::Generate();
        auto operator_b = sgns::GeniusSigner::Generate();
        const std::string topic = "initial-burn-policy-ordering-topic";

        const auto operator_a_config_path = path / "operator-a-network.json";
        const auto operator_b_config_path = path / "operator-b-network.json";
        boost::filesystem::create_directories( path / "operator-a-globaldb" );
        boost::filesystem::create_directories( path / "operator-b-globaldb" );
        WriteNetworkConfig( operator_a_config_path );

        auto make_config = [&]( const boost::filesystem::path &config_path,
                                const boost::filesystem::path &database_path )
        {
            sgns::crdt::GlobalDbNetworkComposition::Config config;
            config.network_config_path = config_path.string();
            config.database_path = database_path.string();
            config.listen_topic = topic;
            config.broadcast_topic = topic;
            return config;
        };
        auto operator_a_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create(
            make_config( operator_a_config_path, path / "operator-a-globaldb" ) );
        ASSERT_TRUE( operator_a_composition_result.has_value() );
        auto operator_a_composition = operator_a_composition_result.value();
        ASSERT_TRUE( operator_a_composition->Start().has_value() );
        ASSERT_FALSE( operator_a_composition->interface_address().empty() );

        WriteNetworkConfig( operator_b_config_path, operator_a_composition->interface_address() );
        auto operator_b_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create(
            make_config( operator_b_config_path, path / "operator-b-globaldb" ) );
        ASSERT_TRUE( operator_b_composition_result.has_value() );
        auto operator_b_composition = operator_b_composition_result.value();
        ASSERT_TRUE( operator_b_composition->Start().has_value() );

        auto operator_a_secure =
            std::make_shared<sgns::securecrdt::SecureCrdt>( operator_a_composition->db(), topic );
        auto operator_b_secure =
            std::make_shared<sgns::securecrdt::SecureCrdt>( operator_b_composition->db(), topic );

        GenesisManifest manifest;
        manifest.network_id = 42;
        manifest.bootstrapper_public_key = bootstrapper.GetAddress();
        manifest.peers = { operator_a.GetAddress(), operator_b.GetAddress() };
        manifest.membership_threshold = 2;
        manifest.burn_threshold = 2;
        manifest = manifest.Canonicalized().value();
        const auto bootstrap_signature = bootstrapper.Sign( manifest.CanonicalBytes().value() );

        auto operator_a_store =
            TrustStateStore::Open( ( path / "operator-a-trust" ).string(), manifest.network_id ).value();
        auto operator_b_store =
            TrustStateStore::Open( ( path / "operator-b-trust" ).string(), manifest.network_id ).value();
        ASSERT_TRUE( operator_a_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );
        ASSERT_TRUE( operator_b_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );

        std::atomic_uint32_t operator_a_signatures{ 0 };
        std::atomic_uint32_t operator_b_signatures{ 0 };
        auto operator_a_controller = TrustStartupController::New(
            operator_a_secure,
            operator_a_store,
            manifest,
            operator_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes )
            {
                ++operator_a_signatures;
                return operator_a.Sign( bytes );
            } ).value();

        const auto initial_burn_core =
            sgns::account::BurnConfig::BurnCandidateCore( operator_a_store->LoadAndVerify().value().burn );
        ASSERT_TRUE( initial_burn_core.has_value() );
        const auto initial_burn_candidate = CandidateId::FromCore( *initial_burn_core );
        ASSERT_TRUE( initial_burn_candidate.has_value() );
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto approvals = operator_a_secure->ReadCandidateApprovals( *initial_burn_candidate );
                return approvals.has_value() && approvals.value().size() == 1U;
            },
            std::chrono::seconds( 5 ),
            "first production controller did not publish its deterministic initial-burn approval" );

        ASSERT_EQ( operator_a_controller->GetState(), TrustStartupController::State::WaitingForInitialBurn );
        ASSERT_FALSE( operator_a_controller->IsEconomicallyReady() );
        ASSERT_FALSE( operator_a_controller->CanApproveSuccessors() );
        ASSERT_EQ( operator_a_signatures.load(), 1U );

        sgns::trustedpeer::LocalTrustAdmin operator_a_admin(
            operator_a_controller->registry(), operator_a_controller->burn_config() );
        const auto before_policy_attempt = operator_a_store->LoadAndVerify().value();
        ASSERT_EQ( before_policy_attempt.burn_authorization,
                   sgns::trustedpeer::BurnAuthorizationKind::BootstrapOnly );
        const auto policy_v1_hash = before_policy_attempt.policy.Hash().value();
        auto policy_v2 = before_policy_attempt.policy;
        policy_v2.version += 1;
        policy_v2.expected_previous_hash = policy_v1_hash;
        policy_v2.authorizing_policy_hash = policy_v1_hash;
        policy_v2 = policy_v2.Canonicalized().value();

        auto rejected_policy = operator_a_admin.ProposePolicy( policy_v2 );
        ASSERT_TRUE( rejected_policy.has_error() );
        EXPECT_EQ( rejected_policy.error(), std::make_error_code( std::errc::operation_not_permitted ) );
        EXPECT_EQ( operator_a_store->LoadAndVerify().value(), before_policy_attempt );
        EXPECT_TRUE( operator_a_controller->registry()->ListPendingPolicyCandidates().value().empty() );
        EXPECT_EQ( operator_a_signatures.load(), 1U );
        EXPECT_EQ( operator_a_controller->GetState(), TrustStartupController::State::WaitingForInitialBurn );

        auto operator_b_controller = TrustStartupController::New(
            operator_b_secure,
            operator_b_store,
            manifest,
            operator_b.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes )
            {
                ++operator_b_signatures;
                return operator_b.Sign( bytes );
            } ).value();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto durable_a = operator_a_store->LoadAndVerify();
                auto durable_b = operator_b_store->LoadAndVerify();
                return durable_a.has_value() && durable_b.has_value() &&
                       durable_a.value().burn_authorization ==
                           sgns::trustedpeer::BurnAuthorizationKind::PeerQuorum &&
                       durable_b.value().burn_authorization ==
                           sgns::trustedpeer::BurnAuthorizationKind::PeerQuorum &&
                       operator_a_controller->GetState() == TrustStartupController::State::ConfirmedReady &&
                       operator_b_controller->GetState() == TrustStartupController::State::ConfirmedReady;
            },
            std::chrono::seconds( 5 ),
            "production callbacks did not complete deterministic initial burn v1" );
        EXPECT_EQ( operator_a_signatures.load(), 1U );
        EXPECT_EQ( operator_b_signatures.load(), 1U );
        EXPECT_TRUE( operator_a_controller->IsEconomicallyReady() );
        EXPECT_TRUE( operator_a_controller->CanApproveSuccessors() );
        EXPECT_EQ( operator_a_controller->burn_config()->GetCachedBasisPoints(), 100U );

        sgns::trustedpeer::LocalTrustAdmin operator_b_admin(
            operator_b_controller->registry(), operator_b_controller->burn_config() );
        auto proposed_policy = operator_a_admin.ProposePolicy( policy_v2 );
        ASSERT_TRUE( proposed_policy.has_value() ) << proposed_policy.error().message();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto approvals = operator_b_secure->ReadCandidateApprovals( proposed_policy.value() );
                return approvals.has_value() && approvals.value().size() == 1U;
            },
            std::chrono::seconds( 5 ),
            "policy v2 proposal did not replicate before approval" );
        EXPECT_EQ( operator_a_store->LoadAndVerify().value().policy.Hash(),
                   std::optional<std::string>( policy_v1_hash ) );

        auto approved_policy = operator_b_admin.Approve( proposed_policy.value() );
        ASSERT_TRUE( approved_policy.has_value() ) << approved_policy.error().message();
        const auto policy_v2_hash = policy_v2.Hash().value();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto durable_a = operator_a_store->LoadAndVerify();
                auto durable_b = operator_b_store->LoadAndVerify();
                return durable_a.has_value() && durable_b.has_value() &&
                       durable_a.value().policy.Hash() == std::optional<std::string>( policy_v2_hash ) &&
                       durable_b.value().policy.Hash() == std::optional<std::string>( policy_v2_hash ) &&
                       operator_a_controller->GetState() == TrustStartupController::State::ConfirmedReady &&
                       operator_b_controller->GetState() == TrustStartupController::State::ConfirmedReady;
            },
            std::chrono::seconds( 5 ),
            "policy v2 did not activate after initial-burn readiness and one peer approval" );

        const auto policy_approvals = operator_a_secure->ReadCandidateApprovals( proposed_policy.value() ).value();
        ASSERT_EQ( policy_approvals.size(), 2U );
        EXPECT_EQ( std::count_if( policy_approvals.begin(), policy_approvals.end(), [&]( const auto &approval ) {
                       return approval.signer == operator_a.GetAddress();
                   } ),
                   1 );
        EXPECT_EQ( std::count_if( policy_approvals.begin(), policy_approvals.end(), [&]( const auto &approval ) {
                       return approval.signer == operator_b.GetAddress();
                   } ),
                   1 );
        EXPECT_EQ( operator_a_signatures.load(), 2U );
        EXPECT_EQ( operator_b_signatures.load(), 2U );
        EXPECT_TRUE( operator_a_controller->IsEconomicallyReady() );
        EXPECT_TRUE( operator_b_controller->IsEconomicallyReady() );

        operator_b_controller.reset();
        operator_a_controller.reset();
        operator_b_store.reset();
        operator_a_store.reset();
        operator_b_composition->Stop();
        operator_a_composition->Stop();
        cleanup();
    }

    TEST( TrustFirstBootE2ETest, ProductionControllerInitiatesBurnAndRestartRecoversPersistedApprovals )
    {
        sgns::test::securecrdt::EnsureLoggingSystemConfigured();
        const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( path );
        auto cleanup = [&] { boost::filesystem::remove_all( path ); };

        sgns::GeniusSigner bootstrapper{ BootstrapperPrivateKey() };
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

    TEST( TrustFirstBootE2ETest, PassiveNodeDurablyActivatesPolicyWithoutExtraAdminCall )
    {
        sgns::test::securecrdt::EnsureLoggingSystemConfigured();
        const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( path );
        auto cleanup = [&] { boost::filesystem::remove_all( path ); };

        auto bootstrapper = sgns::GeniusSigner::Generate();
        auto operator_a = sgns::GeniusSigner::Generate();
        auto operator_b = sgns::GeniusSigner::Generate();
        auto passive = sgns::GeniusSigner::Generate();
        auto replacement = sgns::GeniusSigner::Generate();
        const std::string topic = "passive-policy-activation-topic";

        const auto operator_a_config_path = path / "operator-a-network.json";
        const auto operator_b_config_path = path / "operator-b-network.json";
        const auto passive_config_path = path / "passive-network.json";
        boost::filesystem::create_directories( path / "operator-a-globaldb" );
        boost::filesystem::create_directories( path / "operator-b-globaldb" );
        boost::filesystem::create_directories( path / "passive-globaldb" );
        WriteNetworkConfig( operator_a_config_path );

        auto make_config = [&]( const boost::filesystem::path &config_path,
                                const boost::filesystem::path &database_path )
        {
            sgns::crdt::GlobalDbNetworkComposition::Config config;
            config.network_config_path = config_path.string();
            config.database_path = database_path.string();
            config.listen_topic = topic;
            config.broadcast_topic = topic;
            return config;
        };
        auto operator_a_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create(
            make_config( operator_a_config_path, path / "operator-a-globaldb" ) );
        ASSERT_TRUE( operator_a_composition_result.has_value() );
        auto operator_a_composition = operator_a_composition_result.value();
        ASSERT_TRUE( operator_a_composition->Start().has_value() );
        ASSERT_FALSE( operator_a_composition->interface_address().empty() );

        WriteNetworkConfig( operator_b_config_path, operator_a_composition->interface_address() );
        WriteNetworkConfig( passive_config_path, operator_a_composition->interface_address() );
        auto operator_b_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create(
            make_config( operator_b_config_path, path / "operator-b-globaldb" ) );
        auto passive_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create(
            make_config( passive_config_path, path / "passive-globaldb" ) );
        ASSERT_TRUE( operator_b_composition_result.has_value() );
        ASSERT_TRUE( passive_composition_result.has_value() );
        auto operator_b_composition = operator_b_composition_result.value();
        auto passive_composition = passive_composition_result.value();
        ASSERT_TRUE( operator_b_composition->Start().has_value() );
        ASSERT_TRUE( passive_composition->Start().has_value() );

        auto operator_a_secure =
            std::make_shared<sgns::securecrdt::SecureCrdt>( operator_a_composition->db(), topic );
        auto operator_b_secure =
            std::make_shared<sgns::securecrdt::SecureCrdt>( operator_b_composition->db(), topic );
        auto passive_secure = std::make_shared<sgns::securecrdt::SecureCrdt>( passive_composition->db(), topic );

        GenesisManifest manifest;
        manifest.network_id = 42;
        manifest.bootstrapper_public_key = bootstrapper.GetAddress();
        manifest.peers = { operator_a.GetAddress(), operator_b.GetAddress() };
        manifest.membership_threshold = 2;
        manifest.burn_threshold = 2;
        manifest = manifest.Canonicalized().value();
        const auto bootstrap_signature = bootstrapper.Sign( manifest.CanonicalBytes().value() );

        std::atomic_bool fail_operator_a_commits{ false };
        std::atomic_bool fail_operator_b_commits{ false };
        std::atomic_bool fail_passive_commits{ false };
        std::atomic_uint32_t operator_a_failed_commit_attempts{ 0 };
        std::atomic_uint32_t passive_failed_commit_attempts{ 0 };
        auto open_store = [&]( const boost::filesystem::path &store_path,
                               std::atomic_bool &fail_commits,
                               std::atomic_uint32_t *attempts = nullptr )
        {
            auto *fail_commits_state = &fail_commits;
            return TrustStateStore::Open(
                store_path.string(),
                manifest.network_id,
                [fail_commits_state,
                 attempts]( sgns::storage::rocksdb &database,
                            const std::vector<TrustStateStore::Write> &writes ) -> outcome::result<void>
                {
                    if ( fail_commits_state->load() )
                    {
                        if ( attempts ) ++( *attempts );
                        return outcome::failure( std::errc::io_error );
                    }
                    return CommitBatch( database, writes );
                } ).value();
        };
        auto operator_a_store =
            open_store( path / "operator-a-trust", fail_operator_a_commits, &operator_a_failed_commit_attempts );
        auto operator_b_store = open_store( path / "operator-b-trust", fail_operator_b_commits );
        auto passive_store =
            open_store( path / "passive-trust", fail_passive_commits, &passive_failed_commit_attempts );
        ASSERT_TRUE( operator_a_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );
        ASSERT_TRUE( operator_b_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );
        ASSERT_TRUE( passive_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );

        std::atomic_uint32_t operator_a_signatures{ 0 };
        std::atomic_uint32_t operator_b_signatures{ 0 };
        std::atomic_uint32_t passive_signatures{ 0 };
        std::atomic_uint32_t passive_activation_failures{ 0 };
        std::atomic_uint32_t passive_state_changes{ 0 };
        std::mutex event_mutex;
        std::vector<std::string> passive_failure_fields;
        auto operator_a_controller = TrustStartupController::New(
            operator_a_secure,
            operator_a_store,
            manifest,
            operator_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes )
            {
                ++operator_a_signatures;
                return operator_a.Sign( bytes );
            } ).value();
        auto operator_b_controller = TrustStartupController::New(
            operator_b_secure,
            operator_b_store,
            manifest,
            operator_b.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes )
            {
                ++operator_b_signatures;
                return operator_b.Sign( bytes );
            } ).value();
        auto passive_controller = TrustStartupController::New(
            passive_secure,
            passive_store,
            manifest,
            passive.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes )
            {
                ++passive_signatures;
                return passive.Sign( bytes );
            },
            [&]( const auto &event )
            {
                if ( event.code == TrustStartupController::EventCode::TRUST_ACTIVATION_FAILED )
                {
                    {
                        std::lock_guard<std::mutex> lock( event_mutex );
                        passive_failure_fields = event.fields;
                    }
                    ++passive_activation_failures;
                }
            },
            [&]( const auto & ) { ++passive_state_changes; } ).value();

        sgns::test::assertWaitForCondition(
            [&]
            {
                return operator_a_controller->GetState() == TrustStartupController::State::ConfirmedReady &&
                       operator_b_controller->GetState() == TrustStartupController::State::ConfirmedReady &&
                       passive_controller->GetState() == TrustStartupController::State::ConfirmedReady;
            },
            std::chrono::seconds( 5 ),
            "production controllers did not reach initial-burn readiness" );
        EXPECT_EQ( operator_a_signatures.load(), 1U );
        EXPECT_EQ( operator_b_signatures.load(), 1U );
        EXPECT_EQ( passive_signatures.load(), 0U );

        const auto durable_v1 = passive_store->LoadAndVerify().value();
        const auto durable_v1_hash = durable_v1.policy.Hash().value();
        auto policy_v2 = durable_v1.policy;
        policy_v2.version += 1;
        policy_v2.expected_previous_hash = durable_v1_hash;
        policy_v2.authorizing_policy_hash = durable_v1_hash;
        policy_v2.peers = { operator_a.GetAddress(), operator_b.GetAddress(), passive.GetAddress() };
        policy_v2 = policy_v2.Canonicalized().value();

        sgns::trustedpeer::LocalTrustAdmin operator_a_admin(
            operator_a_controller->registry(), operator_a_controller->burn_config() );
        sgns::trustedpeer::LocalTrustAdmin operator_b_admin(
            operator_b_controller->registry(), operator_b_controller->burn_config() );
        const auto state_changes_before_pending = passive_state_changes.load();
        auto proposed = operator_a_admin.ProposePolicy( policy_v2 );
        ASSERT_TRUE( proposed.has_value() ) << proposed.error().message();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto approvals = passive_secure->ReadCandidateApprovals( proposed.value() );
                return approvals.has_value() && approvals.value().size() == 1U;
            },
            std::chrono::seconds( 5 ),
            "passive node did not retain the authenticated pending policy" );
        EXPECT_EQ( passive_store->LoadAndVerify().value().policy.Hash(), std::optional<std::string>( durable_v1_hash ) );
        EXPECT_EQ( passive_activation_failures.load(), 0U );
        EXPECT_EQ( passive_state_changes.load(), state_changes_before_pending );

        // The passive-side retention check above does not imply operator_b's store
        // has the proposal yet — approving against an empty approval list fails with
        // INVALID_CANDIDATE.
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto approvals = operator_b_secure->ReadCandidateApprovals( proposed.value() );
                return approvals.has_value() && approvals.value().size() == 1U;
            },
            std::chrono::seconds( 5 ),
            "operator_b did not retain the authenticated pending policy" );

        auto approved = operator_b_admin.Approve( proposed.value() );
        ASSERT_TRUE( approved.has_value() ) << approved.error().message();
        const auto policy_v2_hash = policy_v2.Hash().value();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto a = operator_a_store->LoadAndVerify();
                auto b = operator_b_store->LoadAndVerify();
                auto p = passive_store->LoadAndVerify();
                return a.has_value() && b.has_value() && p.has_value() &&
                       a.value().policy.Hash() == std::optional<std::string>( policy_v2_hash ) &&
                       b.value().policy.Hash() == std::optional<std::string>( policy_v2_hash ) &&
                       p.value().policy.Hash() == std::optional<std::string>( policy_v2_hash ) &&
                       passive_controller->GetState() == TrustStartupController::State::ConfirmedReady;
            },
            std::chrono::seconds( 5 ),
            "passive node did not durably activate policy v2" );
        const auto policy_v2_approvals = passive_secure->ReadCandidateApprovals( proposed.value() ).value();
        ASSERT_EQ( policy_v2_approvals.size(), 2U );
        EXPECT_EQ( std::count_if( policy_v2_approvals.begin(), policy_v2_approvals.end(), [&]( const auto &item ) {
                       return item.signer == operator_a.GetAddress();
                   } ),
                   1 );
        EXPECT_EQ( std::count_if( policy_v2_approvals.begin(), policy_v2_approvals.end(), [&]( const auto &item ) {
                       return item.signer == operator_b.GetAddress();
                   } ),
                   1 );
        EXPECT_TRUE( std::none_of( policy_v2_approvals.begin(), policy_v2_approvals.end(), [&]( const auto &item ) {
            return item.signer == passive.GetAddress();
        } ) );
        EXPECT_EQ( passive_signatures.load(), 0U );

        auto failed_policy_v3 = policy_v2;
        failed_policy_v3.version += 1;
        failed_policy_v3.expected_previous_hash = policy_v2_hash;
        failed_policy_v3.authorizing_policy_hash = policy_v2_hash;
        failed_policy_v3.peers = { operator_a.GetAddress(), operator_b.GetAddress(), replacement.GetAddress() };
        failed_policy_v3 = failed_policy_v3.Canonicalized().value();
        const auto failed_core =
            sgns::trustedpeer::TrustedPeerRegistry::PolicyCandidateCore( failed_policy_v3 ).value();
        const auto failed_id = CandidateId::FromCore( failed_core ).value();
        const auto failed_bytes = failed_core.CanonicalBytes().value();
        fail_operator_a_commits.store( true );
        fail_operator_b_commits.store( true );
        fail_passive_commits.store( true );
        ASSERT_TRUE( operator_a_secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              failed_core,
              operator_a.GetAddress(),
              operator_a.Sign( failed_bytes ) } ).has_value() );
        ASSERT_TRUE( operator_a_secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              failed_core,
              operator_b.GetAddress(),
              operator_b.Sign( failed_bytes ) } ).has_value() );
        sgns::test::assertWaitForCondition(
            [&]
            {
                return operator_a_failed_commit_attempts.load() == 1U &&
                       passive_activation_failures.load() == 1U && passive_failed_commit_attempts.load() == 1U;
            },
            std::chrono::seconds( 5 ),
            "passive policy commit failure was not emitted" );
        {
            std::lock_guard<std::mutex> lock( event_mutex );
            ASSERT_EQ( passive_failure_fields.size(), 4U );
            EXPECT_EQ( passive_failure_fields.at( 0 ), failed_id.domain );
            EXPECT_EQ( passive_failure_fields.at( 1 ), std::to_string( failed_id.version ) );
            EXPECT_EQ( passive_failure_fields.at( 2 ), failed_id.content_hash );
            EXPECT_EQ( passive_failure_fields.at( 3 ), "synchronous trust-state batch commit failed" );
        }
        EXPECT_EQ( passive_store->LoadAndVerify().value().policy.Hash(), std::optional<std::string>( policy_v2_hash ) );
        EXPECT_EQ( passive_signatures.load(), 0U );

        passive_controller.reset();
        fail_operator_a_commits.store( false );
        fail_operator_b_commits.store( false );
        fail_passive_commits.store( false );
        auto winning_policy_v3 = policy_v2;
        winning_policy_v3.version += 1;
        winning_policy_v3.expected_previous_hash = policy_v2_hash;
        winning_policy_v3.authorizing_policy_hash = policy_v2_hash;
        winning_policy_v3.peers = { operator_a.GetAddress(), operator_b.GetAddress() };
        winning_policy_v3 = winning_policy_v3.Canonicalized().value();
        const auto winning_core =
            sgns::trustedpeer::TrustedPeerRegistry::PolicyCandidateCore( winning_policy_v3 ).value();
        const auto winning_id = CandidateId::FromCore( winning_core ).value();
        const auto winning_bytes = winning_core.CanonicalBytes().value();
        ASSERT_FALSE( winning_id == failed_id );
        ASSERT_TRUE( operator_a_secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              winning_core,
              operator_a.GetAddress(),
              operator_a.Sign( winning_bytes ) } ).has_value() );
        ASSERT_TRUE( operator_a_secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              winning_core,
              operator_b.GetAddress(),
              operator_b.Sign( winning_bytes ) } ).has_value() );
        const auto winning_policy_v3_hash = winning_policy_v3.Hash().value();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto a = operator_a_store->LoadAndVerify();
                auto b = operator_b_store->LoadAndVerify();
                return a.has_value() && b.has_value() &&
                       a.value().policy.Hash() == std::optional<std::string>( winning_policy_v3_hash ) &&
                       b.value().policy.Hash() == std::optional<std::string>( policy_v2_hash );
            },
            std::chrono::seconds( 5 ),
            "remaining production callback did not process quorum after passive teardown" );
        EXPECT_EQ( passive_store->LoadAndVerify().value().policy.Hash(), std::optional<std::string>( policy_v2_hash ) );
        EXPECT_EQ( passive_failed_commit_attempts.load(), 1U );

        operator_a_controller.reset();
        operator_b_controller.reset();
        passive_store.reset();
        operator_b_store.reset();
        operator_a_store.reset();
        passive_composition->Stop();
        operator_b_composition->Stop();
        operator_a_composition->Stop();
        cleanup();
    }

    TEST( TrustFirstBootE2ETest, RetainedPolicyQuorumRetriesAfterControllerReconstructionWithoutNewWrite )
    {
        sgns::test::securecrdt::EnsureLoggingSystemConfigured();
        const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( path );
        auto cleanup = [&] { boost::filesystem::remove_all( path ); };

        auto bootstrapper = sgns::GeniusSigner::Generate();
        auto operator_a = sgns::GeniusSigner::Generate();
        auto operator_b = sgns::GeniusSigner::Generate();
        auto passive = sgns::GeniusSigner::Generate();
        const std::string topic = "retained-policy-reconstruction-topic";

        const auto operator_a_config_path = path / "operator-a-network.json";
        const auto operator_b_config_path = path / "operator-b-network.json";
        const auto passive_config_path = path / "passive-network.json";
        boost::filesystem::create_directories( path / "operator-a-globaldb" );
        boost::filesystem::create_directories( path / "operator-b-globaldb" );
        boost::filesystem::create_directories( path / "passive-globaldb" );
        WriteNetworkConfig( operator_a_config_path );

        auto make_config = [&]( const boost::filesystem::path &config_path,
                                const boost::filesystem::path &database_path )
        {
            sgns::crdt::GlobalDbNetworkComposition::Config config;
            config.network_config_path = config_path.string();
            config.database_path = database_path.string();
            config.listen_topic = topic;
            config.broadcast_topic = topic;
            return config;
        };
        auto operator_a_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create(
            make_config( operator_a_config_path, path / "operator-a-globaldb" ) );
        ASSERT_TRUE( operator_a_composition_result.has_value() );
        auto operator_a_composition = operator_a_composition_result.value();
        ASSERT_TRUE( operator_a_composition->Start().has_value() );
        ASSERT_FALSE( operator_a_composition->interface_address().empty() );

        WriteNetworkConfig( operator_b_config_path, operator_a_composition->interface_address() );
        WriteNetworkConfig( passive_config_path, operator_a_composition->interface_address() );
        auto operator_b_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create(
            make_config( operator_b_config_path, path / "operator-b-globaldb" ) );
        auto passive_composition_result = sgns::crdt::GlobalDbNetworkComposition::Create(
            make_config( passive_config_path, path / "passive-globaldb" ) );
        ASSERT_TRUE( operator_b_composition_result.has_value() );
        ASSERT_TRUE( passive_composition_result.has_value() );
        auto operator_b_composition = operator_b_composition_result.value();
        auto passive_composition = passive_composition_result.value();
        ASSERT_TRUE( operator_b_composition->Start().has_value() );
        ASSERT_TRUE( passive_composition->Start().has_value() );

        auto operator_a_secure =
            std::make_shared<sgns::securecrdt::SecureCrdt>( operator_a_composition->db(), topic );
        auto operator_b_secure =
            std::make_shared<sgns::securecrdt::SecureCrdt>( operator_b_composition->db(), topic );
        auto passive_secure = std::make_shared<sgns::securecrdt::SecureCrdt>( passive_composition->db(), topic );

        GenesisManifest manifest;
        manifest.network_id = 42;
        manifest.bootstrapper_public_key = bootstrapper.GetAddress();
        manifest.peers = { operator_a.GetAddress(), operator_b.GetAddress() };
        manifest.membership_threshold = 2;
        manifest.burn_threshold = 2;
        manifest = manifest.Canonicalized().value();
        const auto bootstrap_signature = bootstrapper.Sign( manifest.CanonicalBytes().value() );

        std::atomic_bool fail_passive_commits{ false };
        std::atomic_uint32_t passive_failed_commit_attempts{ 0 };
        auto operator_a_store =
            TrustStateStore::Open( ( path / "operator-a-trust" ).string(), manifest.network_id ).value();
        auto operator_b_store =
            TrustStateStore::Open( ( path / "operator-b-trust" ).string(), manifest.network_id ).value();
        auto passive_store = TrustStateStore::Open(
            ( path / "passive-trust" ).string(),
            manifest.network_id,
            [&]( sgns::storage::rocksdb &database,
                 const std::vector<TrustStateStore::Write> &writes ) -> outcome::result<void>
            {
                if ( fail_passive_commits.load() )
                {
                    ++passive_failed_commit_attempts;
                    return outcome::failure( std::errc::io_error );
                }
                return CommitBatch( database, writes );
            } ).value();
        ASSERT_TRUE( operator_a_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );
        ASSERT_TRUE( operator_b_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );
        ASSERT_TRUE( passive_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );

        std::atomic_uint32_t passive_signatures{ 0 };
        std::atomic_uint32_t passive_activation_failures{ 0 };
        std::mutex failure_mutex;
        std::vector<std::string> failure_fields;
        auto operator_a_controller = TrustStartupController::New(
            operator_a_secure,
            operator_a_store,
            manifest,
            operator_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return operator_a.Sign( bytes ); } ).value();
        auto operator_b_controller = TrustStartupController::New(
            operator_b_secure,
            operator_b_store,
            manifest,
            operator_b.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return operator_b.Sign( bytes ); } ).value();
        auto passive_controller = TrustStartupController::New(
            passive_secure,
            passive_store,
            manifest,
            passive.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes )
            {
                ++passive_signatures;
                return passive.Sign( bytes );
            },
            [&]( const auto &event )
            {
                if ( event.code == TrustStartupController::EventCode::TRUST_ACTIVATION_FAILED )
                {
                    std::lock_guard<std::mutex> lock( failure_mutex );
                    failure_fields = event.fields;
                    ++passive_activation_failures;
                }
            } ).value();

        sgns::test::assertWaitForCondition(
            [&]
            {
                return operator_a_controller->GetState() == TrustStartupController::State::ConfirmedReady &&
                       operator_b_controller->GetState() == TrustStartupController::State::ConfirmedReady &&
                       passive_controller->GetState() == TrustStartupController::State::ConfirmedReady;
            },
            std::chrono::seconds( 5 ),
            "production controllers did not reach initial-burn readiness" );
        EXPECT_EQ( passive_signatures.load(), 0U );

        const auto durable_v1 = passive_store->LoadAndVerify().value();
        const auto durable_v1_hash = durable_v1.policy.Hash().value();
        auto policy_v2 = durable_v1.policy;
        policy_v2.version += 1;
        policy_v2.expected_previous_hash = durable_v1_hash;
        policy_v2.authorizing_policy_hash = durable_v1_hash;
        policy_v2.peers = { operator_a.GetAddress(), operator_b.GetAddress(), passive.GetAddress() };
        policy_v2 = policy_v2.Canonicalized().value();
        const auto policy_v2_hash = policy_v2.Hash().value();

        sgns::trustedpeer::LocalTrustAdmin operator_a_admin(
            operator_a_controller->registry(), operator_a_controller->burn_config() );
        sgns::trustedpeer::LocalTrustAdmin operator_b_admin(
            operator_b_controller->registry(), operator_b_controller->burn_config() );
        auto proposed = operator_a_admin.ProposePolicy( policy_v2 );
        ASSERT_TRUE( proposed.has_value() ) << proposed.error().message();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto approvals = passive_secure->ReadCandidateApprovals( proposed.value() );
                return approvals.has_value() && approvals.value().size() == 1U;
            },
            std::chrono::seconds( 5 ),
            "passive node did not retain the first policy approval" );
        // The passive-side wait above does not imply operator_b's store has the
        // proposal yet — approving against an empty approval list fails with
        // INVALID_CANDIDATE.
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto approvals = operator_b_secure->ReadCandidateApprovals( proposed.value() );
                return approvals.has_value() && approvals.value().size() == 1U;
            },
            std::chrono::seconds( 5 ),
            "operator_b did not retain the first policy approval" );

        fail_passive_commits.store( true );
        auto approved = operator_b_admin.Approve( proposed.value() );
        ASSERT_TRUE( approved.has_value() ) << approved.error().message();
        sgns::test::assertWaitForCondition(
            [&]
            {
                return passive_activation_failures.load() == 1U && passive_failed_commit_attempts.load() == 1U;
            },
            std::chrono::seconds( 5 ),
            "passive controller did not report the injected policy commit failure" );
        {
            std::lock_guard<std::mutex> lock( failure_mutex );
            ASSERT_EQ( failure_fields.size(), 4U );
            EXPECT_EQ( failure_fields.at( 0 ), proposed.value().domain );
            EXPECT_EQ( failure_fields.at( 1 ), std::to_string( proposed.value().version ) );
            EXPECT_EQ( failure_fields.at( 2 ), proposed.value().content_hash );
            EXPECT_EQ( failure_fields.at( 3 ), "synchronous trust-state batch commit failed" );
        }
        EXPECT_EQ( passive_store->LoadAndVerify().value().policy.Hash(),
                   std::optional<std::string>( durable_v1_hash ) );

        const auto approvals_before_reconstruction =
            passive_secure->ReadCandidateApprovals( proposed.value() ).value();
        ASSERT_EQ( approvals_before_reconstruction.size(), 2U );
        passive_controller.reset();
        passive_store.reset();
        fail_passive_commits.store( false );

        outcome::result<std::shared_ptr<TrustStateStore>> reopened_passive_store_result =
            outcome::failure( std::errc::resource_unavailable_try_again );
        sgns::test::assertWaitForCondition(
            [&]
            {
                reopened_passive_store_result =
                    TrustStateStore::Open( ( path / "passive-trust" ).string(), manifest.network_id );
                return reopened_passive_store_result.has_value();
            },
            std::chrono::seconds( 5 ),
            "failed passive controller retained the trust-store lock after destruction" );
        ASSERT_TRUE( reopened_passive_store_result.has_value() );
        auto reopened_passive_store = reopened_passive_store_result.value();

        // RECONSTRUCTION_NO_WRITE_WINDOW_BEGIN
        auto reconstructed = TrustStartupController::New(
            passive_secure,
            reopened_passive_store,
            manifest,
            passive.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes )
            {
                ++passive_signatures;
                return passive.Sign( bytes );
            } );
        ASSERT_TRUE( reconstructed.has_value() ) << reconstructed.error().message();
        passive_controller = reconstructed.value();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto durable = reopened_passive_store->LoadAndVerify();
                return durable.has_value() &&
                       durable.value().policy.Hash() == std::optional<std::string>( policy_v2_hash ) &&
                       passive_controller->GetState() == TrustStartupController::State::ConfirmedReady;
            },
            std::chrono::seconds( 5 ),
            "reconstructed controller did not activate retained policy quorum" );
        // RECONSTRUCTION_NO_WRITE_WINDOW_END

        const auto approvals_after_reconstruction =
            passive_secure->ReadCandidateApprovals( proposed.value() ).value();
        EXPECT_EQ( approvals_after_reconstruction.size(), approvals_before_reconstruction.size() );
        EXPECT_TRUE( std::equal(
            approvals_after_reconstruction.begin(),
            approvals_after_reconstruction.end(),
            approvals_before_reconstruction.begin(),
            []( const auto &left, const auto &right )
            {
                return left.core == right.core && left.signer == right.signer && left.signature == right.signature;
            } ) );
        EXPECT_EQ( reopened_passive_store->LoadAndVerify().value().policy.Hash(),
                   std::optional<std::string>( policy_v2_hash ) );
        EXPECT_TRUE( passive_controller->IsEconomicallyReady() );
        EXPECT_EQ( passive_signatures.load(), 0U );
        EXPECT_EQ( passive_activation_failures.load(), 1U );
        EXPECT_EQ( passive_failed_commit_attempts.load(), 1U );

        passive_controller.reset();
        operator_b_controller.reset();
        operator_a_controller.reset();
        reopened_passive_store.reset();
        operator_b_store.reset();
        operator_a_store.reset();
        passive_composition->Stop();
        operator_b_composition->Stop();
        operator_a_composition->Stop();
        cleanup();
    }

    TEST( TrustFirstBootE2ETest, TransientPolicyListingRetriesWithoutNewWrite )
    {
        auto observations = std::make_shared<RefreshObservations>();
        auto hooks = MakeRefreshHooks( observations );
        std::atomic_uint32_t policy_list_calls{ 0 };
        std::atomic_uint32_t burn_list_calls{ 0 };
        std::atomic_uint32_t failures_remaining{ 0 };
        std::shared_ptr<std::optional<CandidateId>> target =
            std::make_shared<std::optional<CandidateId>>();
        hooks->list_policy_candidates = [&, target]( auto &registry )
        {
            ++policy_list_calls;
            auto listed = registry.ListPendingPolicyCandidates();
            if ( listed.has_value() && *target &&
                 std::find( listed.value().begin(), listed.value().end(), **target ) != listed.value().end() &&
                 failures_remaining.load() > 0 )
            {
                --failures_remaining;
                return TrustStartupController::RefreshTestHooks::CandidateList(
                    outcome::failure( std::errc::resource_unavailable_try_again ) );
            }
            return listed;
        };
        hooks->list_burn_candidates = [&]( auto &burn )
        {
            ++burn_list_calls;
            return burn.ListPendingBurnCandidates();
        };
        auto harness = MakeReadyRefreshHarness(
            "transient_policy_retry",
            hooks,
            [observations]( const auto &event )
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                observations->events.push_back( event );
            } );
        ASSERT_NE( harness, nullptr );

        auto current = harness->store->LoadAndVerify().value();
        const auto current_hash = current.policy.Hash().value();
        auto successor = current.policy;
        successor.version += 1;
        successor.expected_previous_hash = current_hash;
        successor.authorizing_policy_hash = current_hash;
        successor = successor.Canonicalized().value();
        const auto expected_successor_hash = successor.Hash().value();
        const auto core = sgns::trustedpeer::TrustedPeerRegistry::PolicyCandidateCore( successor ).value();
        const auto bytes = core.CanonicalBytes().value();
        *target = CandidateId::FromCore( core ).value();

        const auto burn_calls_before_first = burn_list_calls.load();
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            observations->idle = false;
        }
        ASSERT_TRUE( harness->secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              core,
              harness->signers[0].GetAddress(),
              harness->signers[0].Sign( bytes ) } ).has_value() );
        // Wait for the first refresh dispatch to FINISH (idle), not just for a burn
        // listing to happen — an in-flight attempt would otherwise bleed across the
        // observation reset below and shift the expected attempt sequence.
        sgns::test::assertWaitForCondition(
            [&]
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                return burn_list_calls.load() > burn_calls_before_first && observations->idle;
            },
            std::chrono::seconds( 5 ),
            "first below-quorum policy refresh did not complete" );
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            observations->attempts.clear();
            observations->delays.clear();
            observations->timers.clear();
            observations->events.clear();
            observations->idle = false;
        }
        failures_remaining.store( 1 );
        ASSERT_TRUE( harness->secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              core,
              harness->signers[1].GetAddress(),
              harness->signers[1].Sign( bytes ) } ).has_value() );

        // TRANSIENT_POLICY_NO_WRITE_BEGIN
        sgns::test::assertWaitForCondition(
            [&]
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                return !observations->timers.empty() || observations->attempts.size() >= 2;
            },
            std::chrono::seconds( 5 ),
            "CR-13 policy transient retry missing" );
        if ( auto retry = observations->PopTimer() ) retry();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto durable = harness->store->LoadAndVerify();
                return durable.has_value() &&
                       durable.value().policy.Hash() == std::optional<std::string>( expected_successor_hash );
            },
            std::chrono::seconds( 5 ),
            "CR-13 policy transient retry missing" );
        // TRANSIENT_POLICY_NO_WRITE_END

        const std::vector<uint32_t> expected_attempts{ 1, 2 };
        const std::vector<uint64_t> expected_delays{ 100 };
        const auto observed_attempts = observations->Attempts();
        const auto observed_delays = observations->Delays();
        std::vector<TrustStartupController::Event> retry_events;
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            std::copy_if( observations->events.begin(),
                          observations->events.end(),
                          std::back_inserter( retry_events ),
                          []( const auto &event )
                          {
                              return event.code ==
                                     TrustStartupController::EventCode::TRUST_REFRESH_RETRY_SCHEDULED;
                          } );
        }
        EXPECT_EQ( observed_attempts, expected_attempts ) << "CR-13 policy transient retry missing";
        EXPECT_EQ( observed_delays, expected_delays ) << "CR-13 policy transient retry missing";
        ASSERT_EQ( retry_events.size(), 1U ) << "CR-13 policy transient retry missing";
        ASSERT_EQ( retry_events.front().fields.size(), 6U );
        EXPECT_EQ( retry_events.front().fields.at( 0 ), "policy-discovery" );
        EXPECT_EQ( retry_events.front().fields.at( 3 ), "attempt=2" );
        EXPECT_EQ( retry_events.front().fields.at( 4 ), "retry=1" );
        EXPECT_EQ( retry_events.front().fields.at( 5 ), "delay_ms=100" );
        EXPECT_EQ( harness->store->LoadAndVerify().value().policy.Hash(),
                   std::optional<std::string>( expected_successor_hash ) );
    }

    TEST( TrustFirstBootE2ETest, TransientBurnListingRetriesWithoutNewWrite )
    {
        auto observations = std::make_shared<RefreshObservations>();
        auto hooks = MakeRefreshHooks( observations );
        std::atomic_uint32_t policy_list_calls{ 0 };
        std::atomic_uint32_t burn_list_calls{ 0 };
        std::atomic_uint32_t failures_remaining{ 0 };
        std::shared_ptr<std::optional<CandidateId>> target =
            std::make_shared<std::optional<CandidateId>>();
        hooks->list_policy_candidates = [&]( auto &registry )
        {
            ++policy_list_calls;
            return registry.ListPendingPolicyCandidates();
        };
        hooks->list_burn_candidates = [&, target]( auto &burn )
        {
            ++burn_list_calls;
            auto listed = burn.ListPendingBurnCandidates();
            if ( listed.has_value() && *target &&
                 std::find( listed.value().begin(), listed.value().end(), **target ) != listed.value().end() &&
                 failures_remaining.load() > 0 )
            {
                --failures_remaining;
                return TrustStartupController::RefreshTestHooks::CandidateList(
                    outcome::failure( std::errc::resource_unavailable_try_again ) );
            }
            return listed;
        };
        auto harness = MakeReadyRefreshHarness(
            "transient_burn_retry",
            hooks,
            [observations]( const auto &event )
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                observations->events.push_back( event );
            } );
        ASSERT_NE( harness, nullptr );

        auto current = harness->store->LoadAndVerify().value();
        sgns::trustedpeer::ConfirmedBurnState successor;
        successor.network_id = current.burn.network_id;
        successor.version = current.burn.version + 1;
        successor.expected_previous_hash = current.burn.Hash().value();
        successor.authorizing_policy_hash = current.policy.Hash().value();
        successor.basis_points = current.burn.basis_points + 1;
        const auto expected_successor_hash = successor.Hash().value();
        const auto core = sgns::account::BurnConfig::BurnCandidateCore( successor ).value();
        const auto bytes = core.CanonicalBytes().value();
        *target = CandidateId::FromCore( core ).value();

        const auto burn_calls_before_first = burn_list_calls.load();
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            observations->idle = false;
        }
        ASSERT_TRUE( harness->secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              core,
              harness->signers[0].GetAddress(),
              harness->signers[0].Sign( bytes ) } ).has_value() );
        // Wait for the first refresh dispatch to FINISH (idle), not just for a burn
        // listing to happen — an in-flight attempt would otherwise bleed across the
        // observation reset below and shift the expected attempt sequence.
        sgns::test::assertWaitForCondition(
            [&]
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                return burn_list_calls.load() > burn_calls_before_first && observations->idle;
            },
            std::chrono::seconds( 5 ),
            "first below-quorum burn refresh did not complete" );
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            observations->attempts.clear();
            observations->delays.clear();
            observations->timers.clear();
            observations->events.clear();
            observations->idle = false;
        }
        failures_remaining.store( 1 );
        ASSERT_TRUE( harness->secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              core,
              harness->signers[1].GetAddress(),
              harness->signers[1].Sign( bytes ) } ).has_value() );

        // TRANSIENT_BURN_NO_WRITE_BEGIN
        sgns::test::assertWaitForCondition(
            [&]
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                return !observations->timers.empty() || observations->attempts.size() >= 2;
            },
            std::chrono::seconds( 5 ),
            "CR-13 burn transient retry missing" );
        if ( auto retry = observations->PopTimer() ) retry();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto durable = harness->store->LoadAndVerify();
                return durable.has_value() &&
                       durable.value().burn.Hash() == std::optional<std::string>( expected_successor_hash ) &&
                       harness->controller->burn_config()->GetConfirmedValueProvider()->IsReady();
            },
            std::chrono::seconds( 5 ),
            "CR-13 burn transient retry missing" );
        // TRANSIENT_BURN_NO_WRITE_END

        const std::vector<uint32_t> expected_attempts{ 1, 2 };
        const std::vector<uint64_t> expected_delays{ 100 };
        const auto observed_attempts = observations->Attempts();
        const auto observed_delays = observations->Delays();
        std::vector<TrustStartupController::Event> retry_events;
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            std::copy_if( observations->events.begin(),
                          observations->events.end(),
                          std::back_inserter( retry_events ),
                          []( const auto &event )
                          {
                              return event.code ==
                                     TrustStartupController::EventCode::TRUST_REFRESH_RETRY_SCHEDULED;
                          } );
        }
        EXPECT_EQ( observed_attempts, expected_attempts ) << "CR-13 burn transient retry missing";
        EXPECT_EQ( observed_delays, expected_delays ) << "CR-13 burn transient retry missing";
        ASSERT_EQ( retry_events.size(), 1U ) << "CR-13 burn transient retry missing";
        ASSERT_EQ( retry_events.front().fields.size(), 6U );
        EXPECT_EQ( retry_events.front().fields.at( 0 ), "burn-discovery" );
        EXPECT_EQ( retry_events.front().fields.at( 3 ), "attempt=2" );
        EXPECT_EQ( retry_events.front().fields.at( 4 ), "retry=1" );
        EXPECT_EQ( retry_events.front().fields.at( 5 ), "delay_ms=100" );
        EXPECT_EQ( harness->store->LoadAndVerify().value().burn.Hash(),
                   std::optional<std::string>( expected_successor_hash ) );
    }

    TEST( TrustFirstBootE2ETest, TransientRefreshRetryExhaustionIsCappedCoalescedAndIdle )
    {
        auto observations = std::make_shared<RefreshObservations>();
        auto hooks = MakeRefreshHooks( observations );
        std::atomic_bool fail_policy_listing{ false };
        std::atomic_uint32_t burn_list_calls{ 0 };
        hooks->list_policy_candidates = [&]( auto &registry )
        {
            if ( fail_policy_listing.load() )
            {
                return TrustStartupController::RefreshTestHooks::CandidateList(
                    outcome::failure( std::errc::resource_unavailable_try_again ) );
            }
            return registry.ListPendingPolicyCandidates();
        };
        hooks->list_burn_candidates = [&]( auto &burn )
        {
            ++burn_list_calls;
            return burn.ListPendingBurnCandidates();
        };
        auto harness = MakeReadyRefreshHarness(
            "transient_retry_exhaustion",
            hooks,
            [observations]( const auto &event )
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                observations->events.push_back( event );
            } );
        ASSERT_NE( harness, nullptr );

        auto current = harness->store->LoadAndVerify().value();
        const auto current_hash = current.policy.Hash().value();
        auto successor = current.policy;
        successor.version += 1;
        successor.expected_previous_hash = current_hash;
        successor.authorizing_policy_hash = current_hash;
        successor = successor.Canonicalized().value();
        const auto core = sgns::trustedpeer::TrustedPeerRegistry::PolicyCandidateCore( successor ).value();
        const auto id = CandidateId::FromCore( core ).value();
        const auto bytes = core.CanonicalBytes().value();

        const auto burn_calls_before_first = burn_list_calls.load();
        ASSERT_TRUE( harness->secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              core,
              harness->signers[0].GetAddress(),
              harness->signers[0].Sign( bytes ) } ).has_value() );
        sgns::test::assertWaitForCondition(
            [&] { return burn_list_calls.load() > burn_calls_before_first; },
            std::chrono::seconds( 5 ),
            "first below-quorum exhaustion refresh did not complete" );
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            observations->attempts.clear();
            observations->delays.clear();
            observations->timers.clear();
            observations->events.clear();
            observations->idle = false;
            observations->coalesced_requests = 0;
        }
        fail_policy_listing.store( true );
        ASSERT_TRUE( harness->secure->SubmitCandidateApproval(
            { CandidateApprovalRecord::ENCODING_VERSION,
              core,
              harness->signers[1].GetAddress(),
              harness->signers[1].Sign( bytes ) } ).has_value() );

        // TRANSIENT_EXHAUSTION_NO_WRITE_BEGIN
        sgns::test::assertWaitForCondition(
            [&]
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                return !observations->timers.empty();
            },
            std::chrono::seconds( 5 ),
            "CR-13 retry exhaustion cap missing" );
        std::function<void()> request_refresh;
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            request_refresh = observations->request_refresh;
        }
        ASSERT_TRUE( static_cast<bool>( request_refresh ) ) << "CR-13 retry exhaustion cap missing";
        request_refresh();
        request_refresh();
        for ( size_t retry = 0; retry < 6; ++retry )
        {
            sgns::test::assertWaitForCondition(
                [&]
                {
                    std::lock_guard<std::mutex> lock( observations->mutex );
                    return !observations->timers.empty();
                },
                std::chrono::seconds( 5 ),
                "CR-13 retry exhaustion cap missing" );
            auto timer = observations->PopTimer();
            ASSERT_TRUE( static_cast<bool>( timer ) ) << "CR-13 retry exhaustion cap missing";
            timer();
        }
        sgns::test::assertWaitForCondition(
            [&]
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                return observations->idle && observations->attempts.size() == 7U;
            },
            std::chrono::seconds( 5 ),
            "CR-13 retry exhaustion cap missing" );
        // TRANSIENT_EXHAUSTION_NO_WRITE_END

        const std::vector<uint32_t> expected_attempts{ 1, 2, 3, 4, 5, 6, 7 };
        const std::vector<uint64_t> expected_delays{ 100, 200, 400, 800, 1600, 3200 };
        const auto observed_attempts = observations->Attempts();
        const auto observed_delays = observations->Delays();
        uint32_t retry_scheduled_events = 0;
        uint32_t retry_exhausted_events = 0;
        uint32_t coalesced_request_count = 0;
        bool dispatcher_idle = false;
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            retry_scheduled_events = static_cast<uint32_t>( std::count_if(
                observations->events.begin(), observations->events.end(), []( const auto &event )
                { return event.code == TrustStartupController::EventCode::TRUST_REFRESH_RETRY_SCHEDULED; } ) );
            retry_exhausted_events = static_cast<uint32_t>( std::count_if(
                observations->events.begin(), observations->events.end(), []( const auto &event )
                { return event.code == TrustStartupController::EventCode::TRUST_REFRESH_RETRY_EXHAUSTED; } ) );
            coalesced_request_count = observations->coalesced_requests;
            dispatcher_idle = observations->idle;
        }
        const auto attempt_count_after_idle = observations->Attempts().size();
        const auto write_count_after_exhaustion =
            harness->secure->ReadCandidateApprovals( id ).value().size() - 2U;
        EXPECT_EQ( observed_attempts, expected_attempts ) << "CR-13 retry exhaustion cap missing";
        EXPECT_EQ( observed_delays, expected_delays ) << "CR-13 retry exhaustion cap missing";
        EXPECT_EQ( retry_scheduled_events, 6U ) << "CR-13 retry exhaustion cap missing";
        EXPECT_EQ( retry_exhausted_events, 1U ) << "CR-13 retry exhaustion cap missing";
        EXPECT_EQ( coalesced_request_count, 2U ) << "CR-13 retry exhaustion cap missing";
        EXPECT_TRUE( dispatcher_idle ) << "CR-13 retry exhaustion cap missing";
        EXPECT_EQ( attempt_count_after_idle, 7U ) << "CR-13 retry exhaustion cap missing";
        EXPECT_EQ( write_count_after_exhaustion, 0U ) << "CR-13 retry exhaustion cap missing";
        EXPECT_EQ( harness->store->LoadAndVerify().value().policy.Hash(),
                   std::optional<std::string>( current_hash ) );
    }

    void RunWorkerCallbackLastOwnerChild()
    {
        auto observations = std::make_shared<RefreshObservations>();
        auto hooks = MakeRefreshHooks( observations );
        std::atomic_bool fail_policy_listing{ false };
        hooks->list_policy_candidates = [&]( auto &registry )
        {
            if ( fail_policy_listing.exchange( false ) )
            {
                return TrustStartupController::RefreshTestHooks::CandidateList(
                    outcome::failure( std::errc::resource_unavailable_try_again ) );
            }
            return registry.ListPendingPolicyCandidates();
        };
        auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories( path );
        auto node = sgns::test::securecrdt::MakeSecureCrdtTestNode( "callback_last_owner" );
        if ( !node ) _exit( 10 );
        auto secure = std::make_shared<sgns::securecrdt::SecureCrdt>( node->db, "callback-last-owner-topic" );
        auto signer_a = sgns::GeniusSigner::Generate();
        auto signer_b = sgns::GeniusSigner::Generate();
        GenesisManifest manifest;
        manifest.network_id = 42;
        manifest.bootstrapper_public_key = signer_a.GetAddress();
        manifest.peers = { signer_a.GetAddress(), signer_b.GetAddress() };
        manifest.membership_threshold = 2;
        manifest.burn_threshold = 2;
        manifest = manifest.Canonicalized().value();
        auto store_result = TrustStateStore::Open( ( path / "trust" ).string(), manifest.network_id );
        if ( store_result.has_error() ) _exit( 11 );
        auto store = store_result.value();
        auto initial = store->CommitGenesis( manifest, signer_a.Sign( manifest.CanonicalBytes().value() ) );
        if ( initial.has_error() ) _exit( 12 );

        std::shared_ptr<TrustStartupController> owner;
        std::weak_ptr<TrustStartupController> weak;
        std::atomic_bool callback_released_owner{ false };
        auto created = TrustStartupController::New(
            secure,
            store,
            manifest,
            signer_a.GetAddress(),
            [&]( const std::vector<uint8_t> &bytes ) { return signer_a.Sign( bytes ); },
            [&]( const TrustStartupController::Event &event )
            {
                if ( event.code == TrustStartupController::EventCode::TRUST_REFRESH_RETRY_SCHEDULED && owner )
                {
                    // DISPATCHED_LAST_OWNER_CALLBACK_BEGIN
                    owner.reset();
                    callback_released_owner.store( true );
                    // DISPATCHED_LAST_OWNER_CALLBACK_END
                }
            },
            {},
            hooks );
        if ( created.has_error() ) _exit( 13 );
        owner = created.value();
        created.value().reset();
        weak = owner;
        std::function<void()> request_refresh;
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            request_refresh = observations->request_refresh;
        }
        if ( !request_refresh ) _exit( 14 );
        fail_policy_listing.store( true );
        request_refresh();
        sgns::test::assertWaitForCondition(
            [&]
            {
                std::lock_guard<std::mutex> lock( observations->mutex );
                return callback_released_owner.load() && weak.expired() && observations->idle &&
                       observations->idle_notifications >= 2U && observations->timers.empty();
            },
            std::chrono::seconds( 5 ),
            "callback owner did not expire and drain" );
        const bool dispatch_drained = [&]
        {
            std::lock_guard<std::mutex> lock( observations->mutex );
            return observations->idle && observations->idle_notifications >= 2U && observations->timers.empty();
        }();
        if ( !callback_released_owner.load() ) _exit( 17 );
        if ( !weak.expired() ) _exit( 18 );
        if ( !dispatch_drained ) _exit( 19 );
        _exit( 0 );
    }

    TEST( TrustFirstBootE2ETest, WorkerCallbackCanReleaseLastControllerOwnerSafely )
    {
        ::testing::FLAGS_gtest_death_test_style = "threadsafe";
        EXPECT_EXIT( RunWorkerCallbackLastOwnerChild(), ::testing::ExitedWithCode( 0 ), "" )
            << "CR-14 callback owner unsafe";
    }
} // namespace
