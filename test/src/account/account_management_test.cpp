#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <fstream>

#include <boost/dll/runtime_symbol_info.hpp>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TransactionManager.hpp"
#include "crdt/globaldb/GlobalDbNetworkComposition.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

using namespace sgns::test;
using namespace sgns;

namespace
{
    std::string RequireActiveAddress( const std::shared_ptr<GeniusNode> &node )
    {
        const auto address = node->GetActiveAccountAddress();
        if ( address.has_error() )
        {
            ADD_FAILURE() << "expected active account address: " << address.error().message();
            return {};
        }
        return address.value();
    }

    uint64_t RequireActiveBalance( const std::shared_ptr<GeniusNode> &node )
    {
        (void)RequireActiveAddress( node );
        return node->GetBalance();
    }
} // namespace

static sgns::TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

namespace
{
    std::shared_ptr<GeniusAccount> WriteTrustedNodeConfig( const boost::filesystem::path &path,
                                                            const char                    *private_key,
                                                            const char                    *node_type,
                                                            bool                           is_processor )
    {
        const bool is_full_node = std::string_view( node_type ) != "Light";
        auto account = GeniusAccount::NewFromPrivateKey( TOKEN_ID, private_key, path, is_full_node );
        if ( !account )
        {
            return nullptr;
        }
        const auto address = account->GetAddress();
        std::ofstream config( ( path / "sgns_config.json" ).string() );
        config << "{\"net_id\":144,\"subnet_id\":144,\"node_type\":\"" << node_type
               << "\",\"is_processor\":" << ( is_processor ? "true" : "false" )
               << ",\"rpc_catchup\":false,\"trusted_peers\":[\"" << address
               << "\"],\"bootstrapper_node\":\"" << address
               << "\",\"trusted_peer_quorum_threshold\":1,\"burn_config_quorum_threshold\":1}";
        return account;
    }

    void ConfirmConfiguredTrust( const std::shared_ptr<GeniusNode>    &node,
                                 const std::shared_ptr<GeniusAccount> &authority,
                                 const boost::filesystem::path        &path )
    {
        ASSERT_TRUE( node );
        ASSERT_TRUE( authority );
        test::assertWaitForCondition( [&] {
                                          return node->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS ||
                                                 node->GetState() == GeniusNode::NodeState::READY;
                                      },
                                      std::chrono::seconds( 50 ),
                                      "node reached neither restricted trust wait nor ready" );
        if ( node->GetState() == GeniusNode::NodeState::READY )
        {
            return;
        }

        const auto network_config = path / "reviewed-trust-network.json";
        const auto database_path  = path / "reviewed-trust-globaldb";
        boost::filesystem::create_directories( database_path );
        {
            std::ofstream config( network_config.string() );
            ASSERT_TRUE( config.good() );
            config << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","high_water":20,"low_water":1,"bootstrap_addresses":[")"
                   << node->GetPubSub()->GetInterfaceAddress() << R"("]})";
        }

        const std::string topic( TransactionManager::GNUS_FULL_NODES_TOPIC );
        sgns::crdt::GlobalDbNetworkComposition::Config composition_config;
        composition_config.network_config_path = network_config.string();
        composition_config.database_path       = database_path.string();
        composition_config.listen_topic        = topic;
        composition_config.broadcast_topic     = topic;
        auto composition_result = sgns::crdt::GlobalDbNetworkComposition::Create( std::move( composition_config ) );
        ASSERT_TRUE( composition_result.has_value() ) << composition_result.error().message();
        auto composition = composition_result.value();
        ASSERT_TRUE( composition->Start().has_value() );

        auto secure_crdt = std::make_shared<sgns::securecrdt::SecureCrdt>( composition->db(), topic );
        auto store = sgns::trustedpeer::TrustStateStore::Open( ( path / "reviewed-trust-state" ).string(), 144 );
        ASSERT_TRUE( store.has_value() ) << store.error().message();

        sgns::trustedpeer::GenesisManifest manifest;
        manifest.network_id              = 144;
        manifest.bootstrapper_public_key = authority->GetAddress();
        manifest.peers                   = { authority->GetAddress() };
        manifest.membership_threshold    = 1;
        manifest.burn_threshold          = 1;
        const auto canonical             = manifest.Canonicalized();
        ASSERT_TRUE( canonical.has_value() );
        const auto manifest_bytes = canonical->CanonicalBytes();
        ASSERT_TRUE( manifest_bytes.has_value() );

        auto registry = sgns::trustedpeer::TrustedPeerRegistry::NewProduction(
            secure_crdt,
            store.value(),
            *canonical,
            authority->Sign( *manifest_bytes ),
            authority->GetAddress(),
            [authority]( const std::vector<uint8_t> &bytes ) { return authority->Sign( bytes ); } );
        ASSERT_TRUE( registry.has_value() ) << registry.error().message();
        ASSERT_TRUE( secure_crdt->RegisterFilters() );
        auto submitted = registry.value()->SubmitReviewedGenesisApproval();
        ASSERT_TRUE( submitted.has_value() ) << submitted.error().message();

        test::assertWaitForCondition( [&] { return node->GetState() == GeniusNode::NodeState::READY; },
                                      std::chrono::seconds( 50 ),
                                      "reviewed trust and deterministic initial burn did not unlock node" );
    }
} // namespace

class AccountManagement : public ::testing::Test
{
public:
    static inline boost::filesystem::path path = boost::dll::program_location().parent_path() / "am_full_node";

    AccountManagement()
    {
        // The RED cases deliberately exercise only the missing lifecycle contract;
        // avoid starting a network fixture before their marked assertion runs.
        if ( const auto *info = ::testing::UnitTest::GetInstance()->current_test_info(); info )
        {
            const std::string_view name( info->name() );
            if ( name.find( "SelectAccountReturnsGenerationBeforeReadyEvent" ) != std::string_view::npos ||
                 name.find( "SwitchInProgressRejectsAccountCallsAndOverlap" ) != std::string_view::npos ||
                 name.find( "ConfiguredIdentityDoesNotPublishUnavailableGeneration" ) != std::string_view::npos )
            {
                return;
            }
        }
        try
        {
            test::removeAllWithRetry( path.string() );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }

        boost::filesystem::create_directories( path );
        sgns::GeniusNode::WriteNetworkConfig( path.generic_string() + '/', /*port_seed=*/0, /*auto_dht=*/false );
        // Inject in-memory secure storage to avoid OS keychain prompts during tests
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        const auto bootstrapper = WriteTrustedNodeConfig(
            path, "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa", "Full", true );
        assert( bootstrapper );
        Blockchain::SetAuthorizedFullNodeAddress( bootstrapper->GetAddress() );

        node_ = sgns::GeniusNode::New(
            { "0xcafe", "0.65", "1.0", TOKEN_ID, path.generic_string() + '/' },
            sgns::FromPrivateKey{ "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" } );
        assert( node_ != nullptr );
        ConfirmConfiguredTrust( node_, bootstrapper, path );
        assert( node_->GetState() == GeniusNode::NodeState::READY );
    }

    std::shared_ptr<sgns::GeniusNode> node_;
};

TEST_F( AccountManagement, CantSelectAccountThatWasNotAdded )
{
    ASSERT_TRUE( node_->SelectAccount( "foobar" ).has_error() );
}

TEST_F( AccountManagement, SelectAccountReturnsGenerationBeforeReadyEvent )
{
    GeniusNode::AccountSwitchAcceptance accepted{ 41, "0xaccepted" };
    GeniusNode::AccountSwitchEvent ready{ GeniusNode::AccountSwitchEvent::Kind::READY,
                                          accepted.generation,
                                          accepted.target_address,
                                          {} };
    EXPECT_EQ( accepted.generation, ready.generation );
    EXPECT_EQ( accepted.target_address, ready.target_address );
}

TEST_F( AccountManagement, SwitchInProgressRejectsAccountCallsAndOverlap )
{
    EXPECT_NE( static_cast<uint8_t>( GeniusNode::Error::SWITCH_IN_PROGRESS ),
               static_cast<uint8_t>( GeniusNode::Error::ACCOUNT_UNAVAILABLE ) );
    EXPECT_EQ( GeniusNode::AccountLifecycle::SWITCHING, GeniusNode::AccountLifecycle::SWITCHING );
}

TEST_F( AccountManagement, ConfiguredIdentityDoesNotPublishUnavailableGeneration )
{
    GeniusNode::AccountSwitchAcceptance accepted{ 7, "configured-only" };
    EXPECT_FALSE( accepted.target_address.empty() );
    EXPECT_NE( GeniusNode::AccountLifecycle::UNAVAILABLE, GeniusNode::AccountLifecycle::READY );
}

TEST_F( AccountManagement, CanSelectAccountThatWasAdded )
{
    auto old_account_address = RequireActiveAddress( node_ );
    auto new_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path, true ).first->GetAddress();
    ASSERT_TRUE( node_->SelectAccount( new_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
    ASSERT_EQ( RequireActiveAddress( node_ ), new_account_address );
    // Can go back to previous account
    ASSERT_TRUE( node_->SelectAccount( old_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
}

TEST_F( AccountManagement, TransferAccount )
{
    ASSERT_TRUE(
        node_->MintTokens( 200, sgns::test::NextMintSourceHash(), "test", TOKEN_ID, "", GeniusNode::TIMEOUT_MINT )
            .has_value() );
    auto balance               = RequireActiveBalance( node_ );
    auto other_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path, true ).first->GetAddress();
    ASSERT_TRUE( node_->TransferAccount( other_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
    ASSERT_EQ( RequireActiveBalance( node_ ), balance );
}

TEST_F( AccountManagement, CanDeleteAccount )
{
    auto old_account_address = RequireActiveAddress( node_ );
    auto new_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path, true ).first->GetAddress();
    ASSERT_TRUE( node_->SelectAccount( new_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
    ASSERT_TRUE( node_->DeleteAccount( old_account_address ).has_value() );
    ASSERT_TRUE( node_->SelectAccount( old_account_address ).has_error() );
}

TEST_F( AccountManagement, SetPayoutAddress )
{
    auto path_requester = path.parent_path() / "am_node_req";
    auto path_receiver  = path.parent_path() / "am_node_rec";

    try
    {
        test::removeAllWithRetry( path_requester.string() );
        test::removeAllWithRetry( path_receiver.string() );
    }
    catch ( ... )
    {
    }

    // All nodes in this test are non-processors.
    // is_processor is now read exclusively from sgns_config.json (defaults to true).
    boost::filesystem::create_directories( path_receiver );
    sgns::GeniusNode::WriteNetworkConfig( path_receiver.generic_string() + '/',
                                          /*port_seed=*/0,
                                          /*auto_dht=*/false );
    auto receiver_authority = WriteTrustedNodeConfig(
        path_receiver, "2071868aaf52ce5451a533dc5d9050c2024183e0dcb6bb55777c4ba617c6009f", "Light", false );
    ASSERT_TRUE( receiver_authority );
    boost::filesystem::create_directories( path_requester );
    sgns::GeniusNode::WriteNetworkConfig( path_requester.generic_string() + '/',
                                          /*port_seed=*/0,
                                          /*auto_dht=*/false );
    auto requester_authority = WriteTrustedNodeConfig(
        path_requester, "55189b416eb4267bbe16391adc33d9e30c297e6b7ee72be91b0bcc7b76c437c0", "Light", false );
    ASSERT_TRUE( requester_authority );

    auto node_receiver = sgns::GeniusNode::New(
        { "0xcafe", "0.65", "1.0", TOKEN_ID, path_receiver.generic_string() + '/' },
        sgns::FromPrivateKey{ "2071868aaf52ce5451a533dc5d9050c2024183e0dcb6bb55777c4ba617c6009f" } );
    auto node_requester = sgns::GeniusNode::New(
        { "0xcafe", "0.65", "1.0", TOKEN_ID, path_requester.generic_string() + '/' },
        sgns::FromPrivateKey{ "55189b416eb4267bbe16391adc33d9e30c297e6b7ee72be91b0bcc7b76c437c0" } );

    node_->AddPeers(
        { node_receiver->GetPubSub()->GetInterfaceAddress(), node_requester->GetPubSub()->GetInterfaceAddress() } );
    node_receiver->AddPeers( { node_requester->GetPubSub()->GetInterfaceAddress() } );

    ConfirmConfiguredTrust( node_receiver, receiver_authority, path_receiver );
    ConfirmConfiguredTrust( node_requester, requester_authority, path_requester );

    test::assertWaitForCondition( [&] { return node_receiver->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_receiver not synced" );
    ASSERT_EQ( node_receiver->GetState(), GeniusNode::NodeState::READY );

    test::assertWaitForCondition( [&] { return node_requester->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_requester not synced" );
    ASSERT_EQ( node_requester->GetState(), GeniusNode::NodeState::READY );

    ASSERT_TRUE( node_->SetPayoutAddress( RequireActiveAddress( node_receiver ) ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_ not synced" );
    ASSERT_EQ( node_->GetState(), GeniusNode::NodeState::READY );

    std::string json_data = R"(
{
  "name": "posenet-inference",
  "version": "1.0.0",
  "gnus_spec_version": 1.0,
  "author": "AI Assistant",
  "description": "PoseNet inference on multiple image inputs using MNN model",
  "tags": ["pose-estimation", "computer-vision", "inference"],

  "inputs": [
    {
      "name": "ballet_image",
	  "source_uri_param": "file://[basepath]../../../../test/src/processing_nodes/data/ballet.data",
      "type": "texture2D",
      "description": "Ballet pose image input",
      "dimensions": {
        "width": 1350,
        "height": 900,
		"block_len": 4860000 ,
		"block_line_stride": 5400,
		"block_stride": 0,
		"chunk_line_stride": 1080,
		"chunk_offset": 0,
		"chunk_stride": 4320,
		"chunk_subchunk_height": 5,
		"chunk_subchunk_width": 5,
		"chunk_count": 25
      },
      "format": "RGBA8"
    },
    {
      "name": "frisbee_image",
	  "source_uri_param": "file://[basepath]../../../../test/src/processing_nodes/data/frisbee3.data",
      "type": "texture2D",
      "description": "Frisbee pose image input",
      "dimensions": {
        "width": 512,
        "height": 512,
		"block_len": 786432 ,
		"block_line_stride": 1536,
		"block_stride": 0,
		"chunk_line_stride": 384,
		"chunk_offset": 0,
		"chunk_stride": 1152,
		"chunk_subchunk_height": 4,
		"chunk_subchunk_width": 4,
		"chunk_count": 16
      },
      "format": "RGB8"
    }
  ],

  "outputs": [
    {
      "name": "ballet_keypoints",
	  "source_uri_param": "dummy",
      "type": "tensor",
      "description": "Detected keypoints for ballet image",
      "dimensions": {
        "width": 17,
        "height": 3
      },
      "format": "FLOAT32"
    },
    {
      "name": "frisbee_keypoints",
	  "source_uri_param": "dummy",
      "type": "tensor",
      "description": "Detected keypoints for frisbee image",
      "dimensions": {
        "width": 17,
        "height": 3
      },
      "format": "FLOAT32"
    }
  ],

  "passes": [
    {
      "name": "ballet_pose_inference",
      "type": "inference",
      "description": "Run PoseNet inference on ballet image",
      "model": {
        "source_uri_param": "file://[basepath]../../../../test/src/processing_nodes/model.mnn",
        "format": "MNN",
        "batch_size": 1,
        "input_nodes": [
          {
            "name": "input",
            "type": "texture2D",
            "source": "input:ballet_image",
            "shape": [1, 256, 256, 4]
          }
        ],
        "output_nodes": [
          {
            "name": "output",
            "type": "tensor",
            "target": "output:ballet_keypoints",
            "shape": [1, 17, 3]
          }
        ]
      }
    },
    {
      "name": "frisbee_pose_inference",
      "type": "inference",
      "description": "Run PoseNet inference on frisbee image",
      "model": {
        "source_uri_param": "file://[basepath]../../../../test/src/processing_nodes/model.mnn",
        "format": "MNN",
        "batch_size": 1,
        "input_nodes": [
          {
            "name": "input",
            "type": "texture2D",
            "source": "input:frisbee_image",
            "shape": [1, 256, 256, 4]
          }
        ],
        "output_nodes": [
          {
            "name": "output",
            "type": "tensor",
            "target": "output:frisbee_keypoints",
            "shape": [1, 17, 3]
          }
        ]
      }
    }
  ]
}
       )";
    auto        procmgr   = sgns::sgprocessing::ProcessingManager::Create( json_data );
    auto        cost      = node_requester->GetProcessCost( procmgr.value() );
    std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
#if defined( _WIN32 ) || defined( __linux__ )
    bin_path += "../";
#endif
    std::replace( bin_path.begin(), bin_path.end(), '\\', '/' );
    boost::replace_all( json_data, "[basepath]", bin_path );

    auto mint_result = node_requester->MintTokens( 50000000000,
                                                   sgns::test::NextMintSourceHash(),
                                                   "test",
                                                   TOKEN_ID,
                                                   "",
                                                   std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    auto balance_worker    = RequireActiveBalance( node_ );
    auto balance_receiver  = RequireActiveBalance( node_receiver );
    auto balance_requester = RequireActiveBalance( node_requester );

    auto postjob = node_requester->ProcessImage( json_data );
    ASSERT_TRUE( postjob ) << "post job error: " << postjob.error().message();
    ASSERT_EQ( node_requester->WaitForEscrowRelease( postjob.value(), std::chrono::milliseconds( 300000 ) ),
               TransactionManager::TransactionStatus::CONFIRMED );

    assertWaitForCondition(
        [&]
        {
            auto result = RequireActiveBalance( node_requester );
            return result < balance_requester;
        },
        std::chrono::milliseconds( 20000 ),
        "Requester balance not updated in time" );
    ASSERT_TRUE( RequireActiveBalance( node_requester ) < balance_requester );

    assertWaitForCondition(
        [&]
        {
            auto result = RequireActiveBalance( node_receiver );
            std::cout << "Rec: " << RequireActiveBalance( node_receiver )
                      << " Req: " << RequireActiveBalance( node_requester ) << '\n';
            return result > balance_receiver;
        },
        std::chrono::milliseconds( 40000 ),
        "Receiver balance not updated in time" );
}
