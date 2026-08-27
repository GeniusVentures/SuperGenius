#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <fstream>

#include <boost/dll/runtime_symbol_info.hpp>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TransactionManager.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/offline_chainlist.hpp"

using namespace sgns::test;
using namespace sgns;

static sgns::TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

class AccountManagement : public ::testing::Test
{
public:
    static inline boost::filesystem::path path = boost::dll::program_location().parent_path() / "am_full_node";

    AccountManagement()
    {
        try
        {
            test::removeAllWithRetry( path.string() );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }

        boost::filesystem::create_directories( path );
        sgns::GeniusNode::WriteNetworkConfig( path.generic_string() + '/', /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( path.generic_string() + '/',
                                           /*node_type=*/"Full",
                                           /*is_processor=*/true,
                                           /*rpc_catchup=*/false );

        // Inject in-memory secure storage to avoid OS keychain prompts during tests
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        node_ = sgns::GeniusNode::New(
            { "0xcafe", "0.35", "1.0", TOKEN_ID, path.generic_string() + '/' },
            sgns::FromPrivateKey{ "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" } );
        node_->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );
        sgns::Blockchain::SetAuthorizedFullNodeAddress( node_->GetAddress() );
        assert( node_ != nullptr );
        test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                      std::chrono::milliseconds( 4000000 ),
                                      "node not synced" );
        assert( node_->GetState() == GeniusNode::NodeState::READY );
    }

    std::shared_ptr<sgns::GeniusNode> node_;
};

TEST_F( AccountManagement, CantSelectAccountThatWasNotAdded )
{
    ASSERT_TRUE( node_->SelectAccount( "foobar" ).has_error() );
}

TEST_F( AccountManagement, CanSelectAccountThatWasAdded )
{
    auto old_account_address = node_->GetAddress();
    auto new_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path ).first->GetAddress();
    ASSERT_TRUE( node_->SelectAccount( new_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
    ASSERT_EQ( node_->GetAddress(), new_account_address );
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
    auto balance               = node_->GetBalance();
    auto other_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path ).first->GetAddress();
    ASSERT_TRUE( node_->TransferAccount( other_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
    ASSERT_EQ( node_->GetBalance(), balance );
}

TEST_F( AccountManagement, CanDeleteAccount )
{
    auto old_account_address = node_->GetAddress();
    auto new_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path ).first->GetAddress();
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
    sgns::GeniusNode::WriteSgnsConfig( path_receiver.generic_string() + '/',
                                       /*node_type=*/"Light",
                                       /*is_processor=*/false,
                                       /*rpc_catchup=*/false );
    boost::filesystem::create_directories( path_requester );
    sgns::GeniusNode::WriteNetworkConfig( path_requester.generic_string() + '/',
                                          /*port_seed=*/0,
                                          /*auto_dht=*/false );
    sgns::GeniusNode::WriteSgnsConfig( path_requester.generic_string() + '/',
                                       /*node_type=*/"Light",
                                       /*is_processor=*/false,
                                       /*rpc_catchup=*/false );

    auto node_receiver = sgns::GeniusNode::New(
        { "0xcafe", "0.35", "1.0", TOKEN_ID, path_receiver.generic_string() + '/' },
        sgns::FromPrivateKey{ "2071868aaf52ce5451a533dc5d9050c2024183e0dcb6bb55777c4ba617c6009f" } );
    auto node_requester = sgns::GeniusNode::New(
        { "0xcafe", "0.35", "1.0", TOKEN_ID, path_requester.generic_string() + '/' },
        sgns::FromPrivateKey{ "55189b416eb4267bbe16391adc33d9e30c297e6b7ee72be91b0bcc7b76c437c0" } );
    node_receiver->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );
    node_requester->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );

    node_->AddPeers(
        { node_receiver->GetPubSub()->GetInterfaceAddress(), node_requester->GetPubSub()->GetInterfaceAddress() } );
    node_receiver->AddPeers( { node_requester->GetPubSub()->GetInterfaceAddress() } );

    test::assertWaitForCondition( [&] { return node_receiver->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_receiver not synced" );
    ASSERT_EQ( node_receiver->GetState(), GeniusNode::NodeState::READY );

    test::assertWaitForCondition( [&] { return node_requester->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_requester not synced" );
    ASSERT_EQ( node_requester->GetState(), GeniusNode::NodeState::READY );

    ASSERT_TRUE( node_->SetPayoutAddress( node_receiver->GetAddress() ).has_value() );
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
	  "source_uri_param": "file://[basepath]data/ballet.data",
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
	  "source_uri_param": "file://[basepath]data/frisbee3.data",
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
        "source_uri_param": "file://[basepath]model.mnn",
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
        "source_uri_param": "file://[basepath]model.mnn",
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
    auto        cost      = node_requester->GetProcessCost( *procmgr.value() );
    // Assets live in the source tree. Deriving this from the binary location broke
    // whenever the build layout changed (multi-config or ABI subdirectory).
    std::string bin_path = std::string( SGNS_PROCESSING_ASSETS_DIR ) + "/";
    std::replace( bin_path.begin(), bin_path.end(), '\\', '/' );
    boost::replace_all( json_data, "[basepath]", bin_path );

    auto mint_result = node_requester->MintTokens( 50000000000,
                                                   sgns::test::NextMintSourceHash(),
                                                   "test",
                                                   TOKEN_ID,
                                                   "",
                                                   std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    auto balance_worker    = node_->GetBalance();
    auto balance_receiver  = node_receiver->GetBalance();
    auto balance_requester = node_requester->GetBalance();

    auto postjob = node_requester->ProcessImage( json_data );
    ASSERT_TRUE( postjob ) << "post job error: " << postjob.error().message();
    ASSERT_EQ( node_requester->WaitForEscrowRelease( postjob.value(), std::chrono::milliseconds( 300000 ) ),
               TransactionManager::TransactionStatus::CONFIRMED );

    assertWaitForCondition(
        [&]
        {
            auto result = node_requester->GetBalance();
            return result < balance_requester;
        },
        std::chrono::milliseconds( 20000 ),
        "Requester balance not updated in time" );
    ASSERT_TRUE( node_requester->GetBalance() < balance_requester );

    assertWaitForCondition(
        [&]
        {
            auto result = node_receiver->GetBalance();
            std::cout << "Rec: " << node_receiver->GetBalance() << " Req: " << node_requester->GetBalance() << '\n';
            return result > balance_receiver;
        },
        std::chrono::milliseconds( 40000 ),
        "Receiver balance not updated in time" );
}
