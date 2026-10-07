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
#include "testutil/local_trust_setup.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/offline_chainlist.hpp"
#include "testutil/genius_node_test_access.hpp"
#include "testutil/wait_condition.hpp"
#include "HttpStubServer.hpp"
#include "testutil/scoped_env.hpp"

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

    // Hermetic price source (Phase 4 cutover): serves the genius-ai price the
    // old CacheGnusPrice friend-accessor used to inject into the node's
    // deleted price cache. Both tiers redirect to the same loopback stub.
    static sgns::testutil::HttpStubServer                    price_stub_;
    static std::unique_ptr<sgns::testutil::ScopedEnvVar>     env_coin_gecko_;
    static std::unique_ptr<sgns::testutil::ScopedEnvVar>     env_fallback_;

    static void SetUpTestSuite()
    {
        price_stub_.OnPath( "/api/v3/simple/price",
                            { 200, "application/json", R"({"genius-ai":{"usd":1.0}})" } );
        price_stub_.Start();
        const auto base = "http://127.0.0.1:" + std::to_string( price_stub_.Port() );
        env_coin_gecko_ = std::make_unique<sgns::testutil::ScopedEnvVar>( "SGNS_COINGECKO_URL", base );
        env_fallback_   = std::make_unique<sgns::testutil::ScopedEnvVar>( "SGNS_PRICE_FALLBACK_URL", base );

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
        sgns::test::WriteLocalTrustSgnsConfig( DEV_CONFIG.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/false, /*rpc_catchup=*/false, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );

        sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG2.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::test::WriteLocalTrustSgnsConfig( DEV_CONFIG2.BaseWritePath, /*node_type=*/"Full", /*is_processor=*/true, /*rpc_catchup=*/false, "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
        node_proc1 = sgns::GeniusNode::New(
            DEV_CONFIG2,
            sgns::FromPrivateKey{ "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
        node_proc1->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );
        sgns::Blockchain::SetAuthorizedFullNodeAddress( node_proc1->GetAddress() );

        node_main = sgns::GeniusNode::New(
            DEV_CONFIG,
            sgns::FromPrivateKey{ "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
        node_main->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );

        sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG3.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::test::WriteLocalTrustSgnsConfig( DEV_CONFIG3.BaseWritePath, /*node_type=*/"Full", /*is_processor=*/true, /*rpc_catchup=*/false, "fecabeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
        node_proc2 = sgns::GeniusNode::New(
            DEV_CONFIG3,
            sgns::FromPrivateKey{ "fecabeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
        node_proc2->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );

        //Connect to each other
        std::vector bootstrappers = { node_proc1->GetPubSub()->GetInterfaceAddress(),
                                      node_proc2->GetPubSub()->GetInterfaceAddress() };
        node_main->AddPeers( bootstrappers );

        bootstrappers = { node_proc2->GetPubSub()->GetInterfaceAddress() };
        node_proc1->AddPeers( bootstrappers );

        sgns::test::MakeNodeReadyWithLocalTrust( node_proc1 );
        sgns::test::MakeNodeReadyWithLocalTrust( node_main );
        sgns::test::MakeNodeReadyWithLocalTrust( node_proc2 );
    }

    static void TearDownTestSuite()
    {
        // Ordered pre-destruction shutdown (see GeniusNodeTestAccess::StopNode):
        // stop each node's services, round timer and pubsub listener BEFORE the
        // shared_ptr release, so default destruction can never leave a zombie
        // node overlapping a later binary phase. Same landmine child_tokens_test
        // hit on CI (port_seed=0 reuses the same ports across suites).
        std::cout << "Tear down main" << std::endl;
        sgns::GeniusNodeTestAccess::StopNode( node_main );
        node_main.reset();

        std::cout << "Tear down 2" << std::endl;
        sgns::GeniusNodeTestAccess::StopNode( node_proc1 );
        node_proc1.reset();

        std::cout << "Tear down 3" << std::endl;
        sgns::GeniusNodeTestAccess::StopNode( node_proc2 );
        node_proc2.reset();

        // Restore env BEFORE the stub goes away (guards reference the stub
        // only via the base-URL string, but restore-then-shutdown keeps the
        // redirect window strictly inside the suite).
        env_coin_gecko_.reset();
        env_fallback_.reset();
        price_stub_.Shutdown();
    }
};

// Static member initialization
std::shared_ptr<sgns::GeniusNode> ProcessingNodesTest::node_main  = nullptr;
std::shared_ptr<sgns::GeniusNode> ProcessingNodesTest::node_proc1 = nullptr;
std::shared_ptr<sgns::GeniusNode> ProcessingNodesTest::node_proc2 = nullptr;

GeniusNodeConfig ProcessingNodesTest::DEV_CONFIG  = { "0xcafe",
                                                  "0.35",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node1" };
GeniusNodeConfig ProcessingNodesTest::DEV_CONFIG2 = { "0xcafe",
                                                  "0.35",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node2" };
GeniusNodeConfig ProcessingNodesTest::DEV_CONFIG3 = { "0xcafe",
                                                  "0.35",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node3" };

std::string ProcessingNodesTest::binary_path = "";

sgns::testutil::HttpStubServer                                                ProcessingNodesTest::price_stub_;
std::unique_ptr<sgns::testutil::ScopedEnvVar> ProcessingNodesTest::env_coin_gecko_ = nullptr;
std::unique_ptr<sgns::testutil::ScopedEnvVar> ProcessingNodesTest::env_fallback_   = nullptr;

/// Scale of SubTaskResult::developer_cut, mirroring SGProcessing.proto.
static constexpr uint64_t DEVELOPER_CUT_SCALE = 1000000;
/// Developer fraction every node in this fixture is configured with (0.35 above).
static constexpr uint64_t DEVELOPER_CUT = 350000;

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
    auto        cost      = node_main->GetProcessCost( *procmgr.value() ).minions;
    ASSERT_EQ( 18, cost );
}

TEST_F( ProcessingNodesTest, DISABLED_CalculateProcessingCostFail )
{
    std::string json_data = R"(
                garbage
               )";
    auto        procmgr   = sgns::sgprocessing::ProcessingManager::Create( json_data );
    auto        cost      = node_main->GetProcessCost( *procmgr.value() ).minions;
    ASSERT_EQ( 0, cost );
}

TEST_F( ProcessingNodesTest, PostProcessing )
{
    // Assets live in the source tree. Deriving this from the binary location broke
    // whenever the build layout changed (multi-config or ABI subdirectory).
    std::string bin_path = std::string( SGNS_PROCESSING_ASSETS_DIR ) + "/";
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
    auto        cost      = node_main->GetProcessCost( *procmgr.value() ).minions;

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
    // Both processors report a 0.35 developer cut. The two subtask results share an even split of
    // the available amount, and each result's cut is floored with the residue staying on its peer,
    // so the peers' combined gain is an exact number no matter which processor ran which subtask.
    const uint64_t per_result        = available / 2;
    const uint64_t peers_entitlement = 2 * ( per_result - ( per_result * DEVELOPER_CUT ) / DEVELOPER_CUT_SCALE );
    assertWaitForCondition(
        [&]
        {
            auto gain = ( node_proc1->GetBalance() + node_proc2->GetBalance() ) - ( balance_node1 + balance_node2 );
            return gain == peers_entitlement;
        },
        std::chrono::milliseconds( 40000 ),
        "Balances not updated in time" );
    std::cout << "Balance main (After):   " << node_main->GetBalance() << std::endl;
    std::cout << "Balance node1 (After):  " << node_proc1->GetBalance() << std::endl;
    std::cout << "Balance node2 (After):  " << node_proc2->GetBalance() << std::endl;

    const auto peers_gain = ( node_proc1->GetBalance() + node_proc2->GetBalance() ) -
                            ( balance_node1 + balance_node2 );
    ASSERT_EQ( peers_gain, peers_entitlement );

    // Whatever the peers did not take went to the developer: the outputs sum to the escrow exactly.
    const auto gameDeveloperPayment = available - peers_gain;
    ASSERT_EQ( balance_main + balance_node1 + balance_node2,
               node_main->GetBalance() + node_proc1->GetBalance() + node_proc2->GetBalance() + gameDeveloperPayment +
                   burn_amount );
}

namespace
{
    /// @brief RAII guard for D-08-12 stub flips: installs the given price
    /// payload on construction and re-installs the fixture-default 1.0
    /// payload on destruction, so an early ASSERT_* exit can never leak a
    /// flipped price into a later suite case.
    class ScopedStubPrice
    {
    public:
        ScopedStubPrice( sgns::testutil::HttpStubServer &stub, const char *body )
            : stub_( stub )
        {
            stub_.OnPath( "/api/v3/simple/price", { 200, "application/json", body } );
        }

        ~ScopedStubPrice()
        {
            stub_.OnPath( "/api/v3/simple/price", { 200, "application/json", R"({"genius-ai":{"usd":1.0}})" } );
        }

        ScopedStubPrice( const ScopedStubPrice & )            = delete;
        ScopedStubPrice &operator=( const ScopedStubPrice & ) = delete;

    private:
        sgns::testutil::HttpStubServer &stub_;
    };
} // namespace

TEST_F( ProcessingNodesTest, GamedPriceJobRejectedAndRefunded )
{
    // TEST-02 (D-08-11..13): a gamed-price job posted through the real
    // ProcessImage wire path is BelowBand-rejected by every honest
    // validator, never processed by either processor, and fully refunded
    // to the poster with no burn (regime 1: reservation rollback via the
    // rejection certificate - asserted in-test, never assumed).
    //
    // Gamed construction (D-08-12). LocalPriceManager::GetQuotes serves
    // FRESH L1 cache entries (kFreshMaxAge = 60s, fresh-closed boundary)
    // with zero network, so the eras are sequenced around that freshness
    // window (verified against HandleRequestOnStrand - the flagged TEST-02
    // cache-hit assumption):
    //   - t0:   warm all three nodes' caches at the honest 1.0 era;
    //   - t61:  every L1 entry has aged past fresh, so the next forced
    //           fetches are guaranteed NETWORK fetches, never cache hits;
    //   - t61:  stub serves 50.0 -> both validators fetch; 50.0-era
    //           observations land in their QueryHistory windows;
    //   - t61.5: stub serves 1.0 again -> the poster refetches a FRESH 1.0
    //           quote (the claim era) into its cache;
    //   - t62:  ProcessImage prices the job from the poster's fresh cached
    //           1.0 quote (L1 hit, zero network) while both validators'
    //           only in-window evidence is 50.0-era. With the validation
    //           window shrunk to [T-5s, T+2s] (TTL=3, skew=2), the 1.0-era
    //           observations sit outside the window and the claim sits far
    //           below the [45, 55] band -> BelowBand Reject on both. The
    //           50x flip dwarfs the 10% tolerance, so the verdict cannot
    //           land on a boundary (TEST-02 boundary item).

    // (1) All three nodes READY (fixture idiom).
    assertWaitForCondition( [&] { return node_main->GetState() == sgns::GeniusNode::NodeState::READY; },
                            std::chrono::milliseconds( 50000 ),
                            "Main node not synced" );
    assertWaitForCondition( [&] { return node_proc1->GetState() == sgns::GeniusNode::NodeState::READY; },
                            std::chrono::milliseconds( 50000 ),
                            "Node proc 1 not synced" );
    assertWaitForCondition( [&] { return node_proc2->GetState() == sgns::GeniusNode::NodeState::READY; },
                            std::chrono::milliseconds( 50000 ),
                            "Node proc 2 not synced" );

    // Mint funds first (its finalization latency must not eat the poster's
    // 60s quote-freshness window): wait for the exact credit before any
    // era sequencing begins.
    const uint64_t mint_amount       = 50000000000;
    const uint64_t balance_pre_mint  = node_main->GetBalance();
    const auto     mint_result       = node_main->MintTokens( mint_amount,
                                                              sgns::test::NextMintSourceHash(),
                                                              "test",
                                                              sgns::TokenID::FromBytes( { 0x00 } ),
                                                              "",
                                                              std::chrono::milliseconds( sgns::GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";
    assertWaitForCondition( [&] { return node_main->GetBalance() == balance_pre_mint + mint_amount; },
                            std::chrono::milliseconds( 60000 ),
                            "Gamed-case mint credit not observed" );

    // (2) Warm every price cache at the honest 1.0 era (the stub still
    // serves the fixture-default 1.0): populates the poster cache AND both
    // validators' QueryHistory with 1.0-era observations.
    const auto warm_main  = node_main->GetGNUSPrice();
    const auto warm_proc1 = node_proc1->GetGNUSPrice();
    const auto warm_proc2 = node_proc2->GetGNUSPrice();
    ASSERT_TRUE( warm_main.has_value() );
    ASSERT_TRUE( warm_proc1.has_value() );
    ASSERT_TRUE( warm_proc2.has_value() );
    ASSERT_NEAR( warm_main.value(), 1.0, 0.05 );
    ASSERT_NEAR( warm_proc1.value(), 1.0, 0.05 );
    ASSERT_NEAR( warm_proc2.value(), 1.0, 0.05 );

    // Age every L1 entry past kFreshMaxAge (60s; an age of exactly 60s is
    // still Fresh, so 61s guarantees Stale) so the next forced fetches are
    // guaranteed network fetches, never cache hits.
    std::this_thread::sleep_for( std::chrono::seconds( 61 ) );

    // (3)+(4) Flip the stub to the 50.0 era and force fresh evidence on
    // both validators only: their stale caches miss, the loopback stub
    // serves 50.0, and the 50.0-era observations land in their windows.
    {
        const ScopedStubPrice era_50( price_stub_, R"({"genius-ai":{"usd":50.0}})" );
        const auto            forced_proc1 = node_proc1->GetGNUSPrice();
        const auto            forced_proc2 = node_proc2->GetGNUSPrice();
        ASSERT_TRUE( forced_proc1.has_value() );
        ASSERT_TRUE( forced_proc2.has_value() );
        ASSERT_NEAR( forced_proc1.value(), 50.0, 0.5 );
        ASSERT_NEAR( forced_proc2.value(), 50.0, 0.5 );
    }

    // Re-warm the poster's cache with a FRESH 1.0-era quote while the stub
    // briefly serves 1.0 again: this is the quote ProcessImage will serve
    // from L1 (zero network) when it prices the gamed job seconds later.
    {
        const ScopedStubPrice era_10( price_stub_, R"({"genius-ai":{"usd":1.0}})" );
        const auto            forced_main = node_main->GetGNUSPrice();
        ASSERT_TRUE( forced_main.has_value() );
        ASSERT_NEAR( forced_main.value(), 1.0, 0.05 );
    }

    // (5) Shrink the validation window for deterministic aging (the env is
    // read per call by ResolvePriceValidatorConfig - never cached): the
    // window around the upcoming escrow's DAG timestamp T is [T-5s, T+2s],
    // which contains only the 50.0-era observations.
    const sgns::testutil::ScopedEnvVar window_ttl( "SGNS_PRICEVAL_WINDOW_TTL_S", "3" );
    const sgns::testutil::ScopedEnvVar clock_skew( "SGNS_PRICEVAL_CLOCK_SKEW_S", "2" );

    // (7) Baselines for the exact-refund and no-processing assertions.
    const uint64_t balance_before_gamed = node_main->GetBalance();
    const uint64_t balance_proc1_before = node_proc1->GetBalance();
    const uint64_t balance_proc2_before = node_proc2->GetBalance();
    std::cout << "Balance main (gamed before): " << balance_before_gamed << std::endl;

    // Same asset-substituted json as PostProcessing, distinct name so logs
    // distinguish the gamed post from the honest job.
    std::string bin_path  = std::string( SGNS_PROCESSING_ASSETS_DIR ) + "/";
    std::string json_data = R"(
{
  "name": "posenet-inference-gamed",
  "version": "1.0.0",
  "gnus_spec_version": 1.0,
  "author": "AI Assistant",
  "description": "Gamed-price PoseNet inference job for TEST-02 rejection coverage",
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
    std::replace( bin_path.begin(), bin_path.end(), '\\', '/' );
    boost::replace_all( json_data, "[basepath]", bin_path );

    // (8) Post the gamed job through the real wire path (D-08-12 - no
    // hand-built Task proto injection). The stub serves 50.0 from here on;
    // the poster's cache serves the fresh 1.0 quote (L1 hit), so
    // claimed_price and the escrow amount are sized at the 1.0 era while
    // both validators' in-window evidence is 50.0-only.
    std::string gamed_task_id;
    {
        const ScopedStubPrice era_50_vote( price_stub_, R"({"genius-ai":{"usd":50.0}})" );
        const auto            postjob = node_main->ProcessImage( json_data );
        ASSERT_TRUE( postjob ) << "post job error: " << postjob.error().message();

        const auto my_tasks = node_main->GetMyTaskIds();
        ASSERT_FALSE( my_tasks.empty() );
        gamed_task_id = my_tasks.back();

        // (9) Exact refund (D-08-07/D-08-13, regime 1): the certified
        // rejection drives the poster's tracked escrow to FAILED, whose
        // machinery performs RollbackUTXOs, returning the reservation.
        // Generous deadline: certificate formation + FAILED + rollback.
        assertWaitForCondition( [&] { return node_main->GetBalance() == balance_before_gamed; },
                                std::chrono::milliseconds( 120000 ),
                                "Gamed escrow not refunded in time" );
        ASSERT_EQ( balance_before_gamed, node_main->GetBalance() );

        // (10) Regime assertion (08-RESEARCH Open Q1 / assumption A3):
        // the poster's tracked escrow reached FAILED - regime 1 (the
        // escrow never certified; rollback, not a release spend). A
        // CONFIRMED status would mean regime 2 fired instead and must be
        // surfaced, not silently accepted.
        const auto escrow_status
            = node_main->WaitForTransactionOutgoing( postjob.value(), std::chrono::milliseconds( 150000 ) );
        ASSERT_EQ( sgns::TransactionManager::TransactionStatus::FAILED, escrow_status )
            << "Expected regime 1 (FAILED rollback); status was " << static_cast<int>( escrow_status );
    }

    // (11) No processing (D-08-08): after a short grace period, both
    // processors' balances are exactly unchanged and the gamed task has no
    // result - no payout, no subtask completion.
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    ASSERT_EQ( balance_proc1_before, node_proc1->GetBalance() );
    ASSERT_EQ( balance_proc2_before, node_proc2->GetBalance() );
    const auto gamed_result = node_main->GetTaskResult( gamed_task_id );
    EXPECT_TRUE( gamed_result.has_error() ) << "Gamed task must never produce a result";

    std::cout << "Balance main (gamed after):  " << node_main->GetBalance() << std::endl;
    // (12) The stub is restored to the fixture-default 1.0 by the RAII
    // guards above; the env guards restore themselves on scope exit.
}
