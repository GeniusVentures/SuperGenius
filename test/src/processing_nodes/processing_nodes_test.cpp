#include <gtest/gtest.h>

#include <memory>
#include <iostream>
#include <thread>
#include <cstdio>

#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "account/GeniusNode.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "testutil/mint_source_hash.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns::test;

class ProcessingNodesTest : public ::testing::Test
{
protected:
    static std::shared_ptr<GeniusNode> node_main;
    static std::shared_ptr<GeniusNode> node_proc1;
    static std::shared_ptr<GeniusNode> node_proc2;

    static DevConfig_st DEV_CONFIG;
    static DevConfig_st DEV_CONFIG2;
    static DevConfig_st DEV_CONFIG3;

    static std::string binary_path;

    static void SetUpTestSuite()
    {
        std::string full_node_pub_address =
            "d4985fbd36d29a48744cd92ee288c18ea0507d83bd993f12cedd32c3e80b2cee105cf696d85a2117156d37f3f69c5eda82e3adb1185c39f8836cce58c63af64d";
        std::string binary_path = boost::dll::program_location().parent_path().string();
        Blockchain::SetAuthorizedFullNodeAddress( full_node_pub_address );

        DEV_CONFIG.BaseWritePath  = ( binary_path + "/node1/" );
        DEV_CONFIG2.BaseWritePath = ( binary_path + "/node2/" );
        DEV_CONFIG3.BaseWritePath = ( binary_path + "/node3/" );

        node_proc1 = sgns::GeniusNode::New( DEV_CONFIG2,
                                            "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                            false,
                                            true,
                                            40054,
                                            true );

        test::assertWaitForCondition( [&] { return node_proc1->GetState() == GeniusNode::NodeState::READY; },
                                      std::chrono::milliseconds( 30000 ),
                                      "node_proc1 not ready" );

        node_main = sgns::GeniusNode::New( DEV_CONFIG,
                                           "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           false );

        node_proc2 = sgns::GeniusNode::New( DEV_CONFIG3,
                                            "fecabeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                            false,
                                            true,
                                            40060,
                                            true );

        //Connect to each other
        std::vector bootstrappers = { node_proc1->GetPubSub()->GetInterfaceAddress(),
                                      node_proc2->GetPubSub()->GetInterfaceAddress() };
        node_main->GetPubSub()->AddPeers( bootstrappers );

        bootstrappers = { node_proc2->GetPubSub()->GetInterfaceAddress() };
        node_proc1->GetPubSub()->AddPeers( bootstrappers );
        test::assertWaitForCondition( [&] { return node_main->GetState() == GeniusNode::NodeState::READY; },
                                      std::chrono::milliseconds( 30000 ),
                                      "node_main not ready" );
        test::assertWaitForCondition( [&] { return node_proc2->GetState() == GeniusNode::NodeState::READY; },
                                      std::chrono::milliseconds( 30000 ),
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

DevConfig_st ProcessingNodesTest::DEV_CONFIG  = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node1" };
DevConfig_st ProcessingNodesTest::DEV_CONFIG2 = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node2" };
DevConfig_st ProcessingNodesTest::DEV_CONFIG3 = { "0xcafe",
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
    test::assertWaitForCondition( [&] { return node_main->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 20000 ),
                                  "Main node not synced" );
    test::assertWaitForCondition( [&] { return node_proc1->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 20000 ),
                                  "Node proc 1 not synced" );
    test::assertWaitForCondition( [&] { return node_proc2->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 20000 ),
                                  "Node proc 2 not synced" );
    node_main->MintTokens( 50000000000,
                           sgns::test::NextMintSourceHash(),
                           "",
                           sgns::TokenID::FromBytes( { 0x00 } ),
                           "",
                           std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    node_main->MintTokens( 50000000000,
                           sgns::test::NextMintSourceHash(),
                           "",
                           sgns::TokenID::FromBytes( { 0x00 } ),
                           "",
                           std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 10000 ) );
    int transcount_main  = node_main->GetTransactions(TransactionManager::TransactionStatus::CONFIRMED).size();
    int transcount_node1 = node_proc1->GetTransactions(TransactionManager::TransactionStatus::CONFIRMED).size();
    int transcount_node2 = node_proc2->GetTransactions(TransactionManager::TransactionStatus::CONFIRMED).size();
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
    auto        cost      = node_main->GetProcessCost( procmgr.value() );
    ASSERT_EQ( 18, cost );
}

TEST_F( ProcessingNodesTest, DISABLED_CalculateProcessingCostFail )
{
    std::string json_data = R"(
                garbage
               )";
    auto        procmgr   = sgns::sgprocessing::ProcessingManager::Create( json_data );
    auto        cost      = node_main->GetProcessCost( procmgr.value() );
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
    auto        cost      = node_main->GetProcessCost( procmgr.value() );

    auto mint_result =
        node_main->MintTokens( 50000000000,
                                              sgns::test::NextMintSourceHash(),
                                              "",
                                              sgns::TokenID::FromBytes( { 0x00 } ),
                                              "",
                                              std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );

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
               TransactionManager::TransactionStatus::CONFIRMED );

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
    assertWaitForCondition(
        [&]
        {
            auto result             = node_proc1->GetBalance() + node_proc2->GetBalance();
            auto expected_peer_gain = ( ( cost * 65 ) / 100 ) / 2;
            return result == balance_node1 + balance_node2 + 2 * expected_peer_gain;
        },
        std::chrono::milliseconds( 40000 ),
        "Balances not updated in time" );
    std::cout << "Balance main (After):   " << node_main->GetBalance() << std::endl;
    std::cout << "Balance node1 (After):  " << node_proc1->GetBalance() << std::endl;
    std::cout << "Balance node2 (After):  " << node_proc2->GetBalance() << std::endl;
    //TODO: convert DEV_CONFIG.Cut from string to fixed and use below
    auto expected_peer_gain = ( ( cost * 65 ) / 100 ) / 2;
    ASSERT_EQ( balance_node1 + balance_node2 + 2 * expected_peer_gain,
               node_proc1->GetBalance() + node_proc2->GetBalance() );

    auto gameDeveloperPayment = cost - 2 * expected_peer_gain;
    ASSERT_EQ( balance_main + balance_node1 + balance_node2,
               node_main->GetBalance() + node_proc1->GetBalance() + node_proc2->GetBalance() + gameDeveloperPayment );
}
