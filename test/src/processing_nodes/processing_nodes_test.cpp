#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <thread>
#include <cstdio>

#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/genius_node_test_access.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns::test;

class ProcessingNodesTest : public ::testing::Test
{
protected:
    static std::shared_ptr<sgns::GeniusNode> node_main;
    static std::shared_ptr<sgns::GeniusNode> node_proc1;
    static std::shared_ptr<sgns::GeniusNode> node_proc2;

    static GeniusNodeConfig DEV_CONFIG;
    static GeniusNodeConfig DEV_CONFIG2;
    static GeniusNodeConfig DEV_CONFIG3;

    static std::string binary_path;

    static void SetUpTestSuite()
    {
        sgns::GeniusAccount::SetSecureStorageFactory(
            []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
            { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

        std::string binary_path = boost::dll::program_location().parent_path().string();

        DEV_CONFIG.BaseWritePath  = ( binary_path + "/pnt_node1/" );
        DEV_CONFIG2.BaseWritePath = ( binary_path + "/pnt_node2/" );
        DEV_CONFIG3.BaseWritePath = ( binary_path + "/pnt_node3/" );

        auto prepare_node_dir = []( const std::string &path )
        {
            removeAllWithRetry( path );
            std::filesystem::create_directories( path );
            std::ofstream bridge_config_file( path + "bridge_chains_config.json" );
            bridge_config_file << "{}";
        };

        prepare_node_dir( DEV_CONFIG.BaseWritePath );
        prepare_node_dir( DEV_CONFIG2.BaseWritePath );
        prepare_node_dir( DEV_CONFIG3.BaseWritePath );

        // node_main: non-processor, light node. Config-driven construction (Phase 3).
        sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/false, /*rpc_catchup=*/false );

        sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG2.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG2.BaseWritePath, /*node_type=*/"Full", /*is_processor=*/true, /*rpc_catchup=*/false );
        node_proc1 = sgns::GeniusNode::New(
            DEV_CONFIG2,
            sgns::FromPrivateKey{ "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
        sgns::GeniusNodeTestAccess::CacheGnusPrice( node_proc1, 1.0 );
        sgns::Blockchain::SetAuthorizedFullNodeAddress( node_proc1->GetAddress() );

        node_main = sgns::GeniusNode::New(
            DEV_CONFIG,
            sgns::FromPrivateKey{ "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
        sgns::GeniusNodeTestAccess::CacheGnusPrice( node_main, 1.0 );

        sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG3.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG3.BaseWritePath, /*node_type=*/"Full", /*is_processor=*/true, /*rpc_catchup=*/false );
        node_proc2 = sgns::GeniusNode::New(
            DEV_CONFIG3,
            sgns::FromPrivateKey{ "fecabeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
        sgns::GeniusNodeTestAccess::CacheGnusPrice( node_proc2, 1.0 );

        //Connect to each other
        std::vector bootstrappers = { node_proc1->GetPubSub()->GetInterfaceAddress(),
                                      node_proc2->GetPubSub()->GetInterfaceAddress() };
        node_main->AddPeers( bootstrappers );

        bootstrappers = { node_proc2->GetPubSub()->GetInterfaceAddress() };
        node_proc1->AddPeers( bootstrappers );

        sgns::test::assertWaitForCondition( [&]
                                            { return node_proc1->GetState() == sgns::GeniusNode::NodeState::READY; },
                                            std::chrono::milliseconds( 50000 ),
                                            "node_proc1 not ready" );
        sgns::test::assertWaitForCondition( [&] { return node_main->GetState() == sgns::GeniusNode::NodeState::READY; },
                                            std::chrono::milliseconds( 50000 ),
                                            "node_main not ready" );
        sgns::test::assertWaitForCondition( [&]
                                            { return node_proc2->GetState() == sgns::GeniusNode::NodeState::READY; },
                                            std::chrono::milliseconds( 50000 ),
                                            "node_proc2 not ready" );
    }

    static void TearDownTestSuite()
    {
        std::cout << "Tear down main" << std::endl;
        node_main.reset();

        std::cout << "Tear down 2" << std::endl;
        node_proc1.reset();

        std::cout << "Tear down 3" << std::endl;
        node_proc2.reset();
    }
};

// Static member initialization
std::shared_ptr<sgns::GeniusNode> ProcessingNodesTest::node_main  = nullptr;
std::shared_ptr<sgns::GeniusNode> ProcessingNodesTest::node_proc1 = nullptr;
std::shared_ptr<sgns::GeniusNode> ProcessingNodesTest::node_proc2 = nullptr;

GeniusNodeConfig ProcessingNodesTest::DEV_CONFIG  = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node1" };
GeniusNodeConfig ProcessingNodesTest::DEV_CONFIG2 = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node2" };
GeniusNodeConfig ProcessingNodesTest::DEV_CONFIG3 = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node3" };

std::string ProcessingNodesTest::binary_path = "";

TEST_F( ProcessingNodesTest, DISABLED_ProcessNodesAddress )
{
    std::string address_main  = node_main->GetAddress();
    std::string address_proc1 = node_proc1->GetAddress();
    std::string address_proc2 = node_proc2->GetAddress();
    std::cout << "Addresses " << std::endl;
    std::cout << "Main Node: " << address_main << std::endl;
    std::cout << "Proc Node 1: " << address_proc1 << std::endl;
    std::cout << "Proc Node 2: " << address_proc2 << std::endl;

    EXPECT_NE( address_main, address_proc1 ) << "node_main and node_proc1 have the same address!";
    EXPECT_NE( address_main, address_proc2 ) << "node_main and node_proc2 have the same address!";
    EXPECT_NE( address_proc1, address_proc2 ) << "node_proc1 and node_proc2 have the same address!";
}

TEST_F( ProcessingNodesTest, DISABLED_ProcessNodesPubsubs )
{
    std::string address_main  = node_main->GetPubSub()->GetInterfaceAddress();
    std::string address_proc1 = node_proc1->GetPubSub()->GetInterfaceAddress();
    std::string address_proc2 = node_proc2->GetPubSub()->GetInterfaceAddress();
    EXPECT_NE( address_main, address_proc1 ) << "node_main and node_proc1 have the same address!";
    EXPECT_NE( address_main, address_proc2 ) << "node_main and node_proc2 have the same address!";
    EXPECT_NE( address_proc1, address_proc2 ) << "node_proc1 and node_proc2 have the same address!";
}

TEST_F( ProcessingNodesTest, DISABLED_ProcessNodesTransactionsCount )
{
    sgns::test::assertWaitForCondition( [&] { return node_main->GetState() == sgns::GeniusNode::NodeState::READY; },
                                        std::chrono::milliseconds( 50000 ),
                                        "Main node not synced" );
    sgns::test::assertWaitForCondition( [&] { return node_proc1->GetState() == sgns::GeniusNode::NodeState::READY; },
                                        std::chrono::milliseconds( 50000 ),
                                        "Node proc 1 not synced" );
    sgns::test::assertWaitForCondition( [&] { return node_proc2->GetState() == sgns::GeniusNode::NodeState::READY; },
                                        std::chrono::milliseconds( 50000 ),
                                        "Node proc 2 not synced" );
    node_main->MintTokens( 50000000000,
                           sgns::test::NextMintSourceHash(),
                           "test",
                           sgns::TokenID::FromBytes( { 0x00 } ),
                           "",
                           std::chrono::milliseconds( sgns::GeniusNode::TIMEOUT_MINT ) );
    node_main->MintTokens( 50000000000,
                           sgns::test::NextMintSourceHash(),
                           "test",
                           sgns::TokenID::FromBytes( { 0x00 } ),
                           "",
                           std::chrono::milliseconds( sgns::GeniusNode::TIMEOUT_MINT ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 10000 ) );
    int transcount_main  = node_main->CountTransactions( sgns::TransactionManager::TransactionStatus::CONFIRMED );
    int transcount_node1 = node_proc1->CountTransactions( sgns::TransactionManager::TransactionStatus::CONFIRMED );
    int transcount_node2 = node_proc2->CountTransactions( sgns::TransactionManager::TransactionStatus::CONFIRMED );
    std::cout << "Count 1" << transcount_main << std::endl;
    //std::cout << "Count 2" << transcount_node1 << std::endl;
    std::cout << "Count 3" << transcount_node2 << std::endl;

    //ASSERT_EQ( transcount_main, 2 );
    // ASSERT_EQ( transcount_node1, transcount_node2 );
}

TEST_F( ProcessingNodesTest, DISABLED_CalculateProcessingCost )
{
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
	  "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/data/ballet.data",
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
	  "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/data/frisbee3.data",
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
        "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/model.mnn",
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
        "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/model.mnn",
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
    auto        cost      = node_main->GetProcessCost( *procmgr.value() );
    ASSERT_EQ( 18, cost );
}

TEST_F( ProcessingNodesTest, DISABLED_CalculateProcessingCostFail )
{
    std::string json_data = R"(
                garbage
               )";
    auto        procmgr   = sgns::sgprocessing::ProcessingManager::Create( json_data );
    auto        cost      = node_main->GetProcessCost( *procmgr.value() );
    ASSERT_EQ( 0, cost );
}

TEST_F( ProcessingNodesTest, PostProcessing )
{
    std::string bin_path = boost::dll::program_location().parent_path().string() + "/";
#if defined( _WIN32 ) || defined( __linux__ )
    bin_path += "../";
#endif
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
    auto        cost      = node_main->GetProcessCost( *procmgr.value() );

    auto mint_result = node_main->MintTokens( 50000000000,
                                              sgns::test::NextMintSourceHash(),
                                              "test",
                                              sgns::TokenID::FromBytes( { 0x00 } ),
                                              "",
                                              std::chrono::milliseconds( sgns::GeniusNode::TIMEOUT_MINT ) );

    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    std::replace( bin_path.begin(), bin_path.end(), '\\', '/' );
    boost::replace_all( json_data, "[basepath]", bin_path );
    std::cout << "Json Data: " << json_data << std::endl;
    auto balance_main  = node_main->GetBalance();
    auto balance_node1 = node_proc1->GetBalance();
    auto balance_node2 = node_proc2->GetBalance();
    auto postjob       = node_main->ProcessImage( json_data );

    EXPECT_TRUE( postjob ) << "post job error: " << postjob.error().message();

    EXPECT_EQ( node_main->WaitForEscrowRelease( postjob.value(), std::chrono::milliseconds( 300000 ) ),
               sgns::TransactionManager::TransactionStatus::CONFIRMED );

    std::cout << "Balance main (Before):  " << balance_main << std::endl;
    std::cout << "Balance node1 (Before): " << balance_node1 << std::endl;
    std::cout << "Balance node2 (Before): " << balance_node2 << std::endl;
    std::cout << "Cost:                   " << cost << std::endl;

    assertWaitForCondition(
        [&]
        {
            auto result = node_main->GetBalance();
            return result == balance_main - cost;
        },
        std::chrono::milliseconds( 20000 ),
        "Main Balance not updated in time" );
    ASSERT_EQ( balance_main - cost, node_main->GetBalance() );
    auto burn_amount = ( cost * sgns::GeniusNode::GetBurnBasisPoints() ) / sgns::GeniusNode::GetBasisPointsTotal();
    auto available   = cost - burn_amount;
    assertWaitForCondition(
        [&]
        {
            auto result             = node_proc1->GetBalance() + node_proc2->GetBalance();
            auto expected_peer_gain = ( ( available * 65 ) / 100 ) / 2;
            return result == balance_node1 + balance_node2 + 2 * expected_peer_gain;
        },
        std::chrono::milliseconds( 40000 ),
        "Balances not updated in time" );
    std::cout << "Balance main (After):   " << node_main->GetBalance() << std::endl;
    std::cout << "Balance node1 (After):  " << node_proc1->GetBalance() << std::endl;
    std::cout << "Balance node2 (After):  " << node_proc2->GetBalance() << std::endl;
    //TODO: convert DEV_CONFIG.Cut from string to fixed and use below
    auto expected_peer_gain = ( ( available * 65 ) / 100 ) / 2;
    ASSERT_EQ( balance_node1 + balance_node2 + 2 * expected_peer_gain,
               node_proc1->GetBalance() + node_proc2->GetBalance() );

    auto gameDeveloperPayment = available - 2 * expected_peer_gain;
    ASSERT_EQ( balance_main + balance_node1 + balance_node2,
               node_main->GetBalance() + node_proc1->GetBalance() + node_proc2->GetBalance() + gameDeveloperPayment +
                   burn_amount );
}
