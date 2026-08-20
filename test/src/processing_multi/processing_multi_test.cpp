#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <iostream>
#include <cstdint>
#include <cstdio>

#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <functional>
#include <thread>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TransactionManager.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/globaldb/GlobalDbNetworkComposition.hpp"
#include "FileManager.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "testutil/mint_source_hash.hpp"
#include "testutil/TestMintInputValidator.hpp"

namespace
{
    constexpr auto kReadyTimeout = std::chrono::milliseconds( 30000 );

    struct TrustedNodeFixture
    {
        std::shared_ptr<sgns::GeniusNode>    node;
        std::shared_ptr<sgns::GeniusAccount> authority;
        std::filesystem::path                path;
    };

    TrustedNodeFixture CreateTrustedNode( GeniusNodeConfig config,
                                          const char       *private_key,
                                          const char       *node_type,
                                          bool              is_processor,
                                          bool              is_full_node )
    {
        sgns::test::removeAllWithRetry( config.BaseWritePath );
        std::filesystem::create_directories( config.BaseWritePath );
        sgns::GeniusNode::WriteNetworkConfig( config.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );

        auto authority = sgns::GeniusAccount::NewFromPrivateKey(
            sgns::TokenID::FromBytes( { 0x00 } ), private_key, config.BaseWritePath, is_full_node );
        EXPECT_TRUE( authority );
        if ( !authority )
        {
            return {};
        }

        std::ofstream node_config( config.BaseWritePath + "sgns_config.json" );
        EXPECT_TRUE( node_config.good() );
        if ( !node_config.good() )
        {
            return {};
        }
        node_config << "{\"net_id\":144,\"subnet_id\":144,\"node_type\":\"" << node_type
                    << "\",\"is_processor\":" << ( is_processor ? "true" : "false" )
                    << ",\"rpc_catchup\":false,\"trusted_peers\":[\"" << authority->GetAddress()
                    << "\"],\"bootstrapper_node\":\"" << authority->GetAddress()
                    << "\",\"trusted_peer_quorum_threshold\":1,\"burn_config_quorum_threshold\":1}";
        node_config.close();

        return { sgns::GeniusNode::New( config, sgns::FromPrivateKey{ private_key } ),
                 std::move( authority ),
                 config.BaseWritePath };
    }

    void SubmitReviewedTrustAndAwaitReady( const TrustedNodeFixture &fixture )
    {
        ASSERT_TRUE( fixture.node );
        ASSERT_TRUE( fixture.authority );
        sgns::test::assertWaitForCondition(
            [&] { return fixture.node->GetState() == sgns::GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS; },
            kReadyTimeout,
            "node did not reach the reviewed-trust checkpoint" );

        const auto network_config = fixture.path / "reviewed-trust-network.json";
        const auto database_path  = fixture.path / "reviewed-trust-globaldb";
        std::filesystem::create_directories( database_path );
        std::ofstream composition_file( network_config );
        ASSERT_TRUE( composition_file.good() );
        composition_file << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","high_water":20,"low_water":1,"bootstrap_addresses":[")"
                         << fixture.node->GetPubSub()->GetInterfaceAddress() << R"("]})";
        composition_file.close();

        const std::string topic( sgns::TransactionManager::GNUS_FULL_NODES_TOPIC );
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
        auto store = sgns::trustedpeer::TrustStateStore::Open( ( fixture.path / "reviewed-trust-state" ).string(), 144 );
        ASSERT_TRUE( store.has_value() ) << store.error().message();

        sgns::trustedpeer::GenesisManifest manifest;
        manifest.network_id              = 144;
        manifest.bootstrapper_public_key = fixture.authority->GetAddress();
        manifest.peers                   = { fixture.authority->GetAddress() };
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
            fixture.authority->Sign( *manifest_bytes ),
            fixture.authority->GetAddress(),
            [authority = fixture.authority]( const std::vector<uint8_t> &bytes ) { return authority->Sign( bytes ); } );
        ASSERT_TRUE( registry.has_value() ) << registry.error().message();
        ASSERT_TRUE( secure_crdt->RegisterFilters() );
        auto submitted = registry.value()->SubmitReviewedGenesisApproval();
        ASSERT_TRUE( submitted.has_value() ) << submitted.error().message();

        sgns::test::assertWaitForCondition(
            [&] { return fixture.node->GetState() == sgns::GeniusNode::NodeState::READY; },
            kReadyTimeout,
            "reviewed trust and deterministic initial burn did not reach READY" );
    }

    bool RequireActiveGeneration( const std::shared_ptr<sgns::GeniusNode> &node )
    {
        const auto address = node->GetActiveAccountAddress();
        if ( address.has_error() )
        {
            ADD_FAILURE() << "expected active account generation: " << address.error().message();
            return false;
        }
        return true;
    }

    std::optional<uint64_t> ActiveBalance( const std::shared_ptr<sgns::GeniusNode> &node )
    {
        if ( !RequireActiveGeneration( node ) )
        {
            return std::nullopt;
        }
        return node->GetBalance();
    }

    bool SubmitProcessImage( const std::shared_ptr<sgns::GeniusNode> &node, const std::string &json_data )
    {
        const auto submitted = node->ProcessImage( json_data );
        if ( submitted.has_error() )
        {
            ADD_FAILURE() << "processing submission failed: " << submitted.error().message();
            return false;
        }
        return true;
    }
} // namespace

class ProcessingMultiTest : public ::testing::Test
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
        DEV_CONFIG.BaseWritePath  = binary_path + "/node1/";
        DEV_CONFIG2.BaseWritePath = binary_path + "/node2/";
        DEV_CONFIG3.BaseWritePath = binary_path + "/node3/";

        auto proc1_fixture = CreateTrustedNode(
            DEV_CONFIG2,
            "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
            "Full",
            true,
            true );
        sgns::Blockchain::SetAuthorizedFullNodeAddress( proc1_fixture.authority->GetAddress() );
        SubmitReviewedTrustAndAwaitReady( proc1_fixture );

        auto main_fixture = CreateTrustedNode(
            DEV_CONFIG,
            "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
            "Light",
            false,
            false );
        main_fixture.node->AddPeer( proc1_fixture.node->GetPubSub()->GetInterfaceAddress() );
        SubmitReviewedTrustAndAwaitReady( main_fixture );

        auto proc2_fixture = CreateTrustedNode(
            DEV_CONFIG3,
            "fecabeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
            "Light",
            true,
            false );
        proc2_fixture.node->AddPeer( proc1_fixture.node->GetPubSub()->GetInterfaceAddress() );
        SubmitReviewedTrustAndAwaitReady( proc2_fixture );
        node_main  = std::move( main_fixture.node );
        node_proc1 = std::move( proc1_fixture.node );
        node_proc2 = std::move( proc2_fixture.node );

        ASSERT_TRUE( RequireActiveGeneration( node_proc1 ) );
        node_proc1->StopProcessing();
        ASSERT_TRUE( RequireActiveGeneration( node_proc2 ) );
        node_proc2->StopProcessing();
        //Connect to each other
        std::vector bootstrappers = { node_proc1->GetPubSub()->GetLocalAddress(),
                                      node_proc2->GetPubSub()->GetLocalAddress() };
        node_main->AddPeers( bootstrappers );

        bootstrappers = { node_main->GetPubSub()->GetLocalAddress(), node_proc2->GetPubSub()->GetLocalAddress() };
        node_proc1->AddPeers( bootstrappers );

        // bootstrappers = { node_main->GetPubSub()->GetLocalAddress(), node_proc1->GetPubSub()->GetLocalAddress() };
        // node_proc2->AddPeers( bootstrappers );
    }

    static void TearDownTestSuite()
    {
        std::cout << "Tear down main" << std::endl;
        node_main.reset();
        // if ( !std::filesystem::remove_all( DEV_CONFIG.BaseWritePath ) )
        // {
        //     std::cerr << "Could not delete main node files\n";
        // }

        std::cout << "Tear down 2" << std::endl;
        node_proc1.reset();
        // if ( !std::filesystem::remove_all( DEV_CONFIG2.BaseWritePath ) )
        // {
        //     std::cerr << "Could not delete node 2 files\n";
        // }

        std::cout << "Tear down 3" << std::endl;
        node_proc2.reset();
        // if ( !std::filesystem::remove_all( DEV_CONFIG3.BaseWritePath ) )
        // {
        //     std::cerr << "Could not delete node 3 files\n";
        // }
    }
};

// Static member initialization
std::shared_ptr<sgns::GeniusNode> ProcessingMultiTest::node_main  = nullptr;
std::shared_ptr<sgns::GeniusNode> ProcessingMultiTest::node_proc1 = nullptr;
std::shared_ptr<sgns::GeniusNode> ProcessingMultiTest::node_proc2 = nullptr;

GeniusNodeConfig ProcessingMultiTest::DEV_CONFIG  = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node1" };
GeniusNodeConfig ProcessingMultiTest::DEV_CONFIG2 = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node2" };
GeniusNodeConfig ProcessingMultiTest::DEV_CONFIG3 = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node3" };

std::string ProcessingMultiTest::binary_path = "";

TEST_F( ProcessingMultiTest, MintTokens )
{
    const auto first_mint = node_main->MintTokens( 50000000000,
                                                   sgns::test::NextMintSourceHash(),
                                                   "test",
                                                   sgns::TokenID::FromBytes( { 0x00 } ),
                                                   "",
                                                   std::chrono::milliseconds( sgns::GeniusNode::TIMEOUT_MINT ) );
    ASSERT_FALSE( first_mint.has_error() ) << first_mint.error().message();
    const auto second_mint = node_main->MintTokens( 50000000000,
                                                    sgns::test::NextMintSourceHash(),
                                                    "test",
                                                    sgns::TokenID::FromBytes( { 0x00 } ),
                                                    "",
                                                    std::chrono::milliseconds( sgns::GeniusNode::TIMEOUT_MINT ) );
    ASSERT_FALSE( second_mint.has_error() ) << second_mint.error().message();
    std::this_thread::sleep_for( std::chrono::milliseconds( 10000 ) );
}

TEST_F( ProcessingMultiTest, PostJobs )
{
    std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
    std::string json_data = R"(
                {
                "data": {
                    "type": "file",
                    "URL": "file://[basepath]../../../../test/src/processing_nodes/"
                },
                "model": {
                    "name": "mnnimage",
                    "file": "model.mnn"
                },
                "input": [
                    {
                        "image": "data/ballet.data",
                        "block_len": 4860000 ,
                        "block_line_stride": 5400,
                        "block_stride": 0,
                        "chunk_line_stride": 1080,
                        "chunk_offset": 0,
                        "chunk_stride": 4320,
                        "chunk_subchunk_height": 5,
                        "chunk_subchunk_width": 5,
                        "chunk_count": 25,
                        "channels": 4
                    },
                    {
                        "image": "data/frisbee3.data",
                        "block_len": 786432 ,
                        "block_line_stride": 1536,
                        "block_stride": 0,
                        "chunk_line_stride": 384,
                        "chunk_offset": 0,
                        "chunk_stride": 1152,
                        "chunk_subchunk_height": 4,
                        "chunk_subchunk_width": 4,
                        "chunk_count": 16,
                        "channels": 3
                    }
                ]
                }
               )";
    boost::replace_all( json_data, "[basepath]", bin_path );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
}

TEST_F( ProcessingMultiTest, ProcessOne )
{
    std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
    std::string json_data = R"(
{
  "name": "processing-multi-single-job",
  "version": "1.0.0",
  "gnus_spec_version": 1.0,
  "author": "test",
  "description": "Processing-multi single job",
  "tags": ["test"],
  "inputs": [{
    "name": "ballet_image",
    "source_uri_param": "file://[basepath]../../../../test/src/processing_nodes/data/ballet.data",
    "type": "texture2D",
    "description": "Ballet pose image input",
    "dimensions": {
      "width": 1350, "height": 900, "block_len": 4860000,
      "block_line_stride": 5400, "block_stride": 0, "chunk_line_stride": 1080,
      "chunk_offset": 0, "chunk_stride": 4320, "chunk_subchunk_height": 5,
      "chunk_subchunk_width": 5, "chunk_count": 25
    },
    "format": "RGBA8"
  }],
  "outputs": [{
    "name": "ballet_keypoints", "source_uri_param": "dummy", "type": "tensor",
    "description": "Detected keypoints", "dimensions": {"width": 17, "height": 3}, "format": "FLOAT32"
  }],
  "passes": [{
    "name": "ballet_pose_inference", "type": "inference", "description": "Run PoseNet inference",
    "model": {
      "source_uri_param": "file://[basepath]../../../../test/src/processing_nodes/model.mnn",
      "format": "MNN", "batch_size": 1,
      "input_nodes": [{"name": "input", "type": "texture2D", "source": "input:ballet_image", "shape": [1, 256, 256, 4]}],
      "output_nodes": [{"name": "output", "type": "tensor", "target": "output:ballet_keypoints", "shape": [1, 17, 3]}]
    }
  }]
}
               )";
    ASSERT_TRUE( RequireActiveGeneration( node_proc1 ) );
    node_proc1->StartProcessing();
    boost::replace_all( json_data, "[basepath]", bin_path );
    auto procmgr = sgns::sgprocessing::ProcessingManager::Create( json_data );
    ASSERT_FALSE( procmgr.has_error() ) << procmgr.error().message();
    auto cost = node_main->GetProcessCost( procmgr.value() );
    std::cout << "Json Data: " << json_data << std::endl;
    const auto balance_main  = ActiveBalance( node_main );
    const auto balance_node1 = ActiveBalance( node_proc1 );
    const auto balance_node2 = ActiveBalance( node_proc2 );
    ASSERT_TRUE( balance_main.has_value() );
    ASSERT_TRUE( balance_node1.has_value() );
    ASSERT_TRUE( balance_node2.has_value() );
    //node_main->ProcessImage(json_data);

    std::this_thread::sleep_for( std::chrono::milliseconds( 40000 ) );
    std::cout << "Balance main (Before):  " << *balance_main << std::endl;
    std::cout << "Balance node1 (Before): " << *balance_node1 << std::endl;
    std::cout << "Balance node2 (Before): " << *balance_node2 << std::endl;
    std::cout << "Cost:                   " << cost << std::endl;
    const auto balance_main_after  = ActiveBalance( node_main );
    const auto balance_node1_after = ActiveBalance( node_proc1 );
    const auto balance_node2_after = ActiveBalance( node_proc2 );
    ASSERT_TRUE( balance_main_after.has_value() );
    ASSERT_TRUE( balance_node1_after.has_value() );
    ASSERT_TRUE( balance_node2_after.has_value() );
    std::cout << "Balance main (After):   " << *balance_main_after << std::endl;
    std::cout << "Balance node1 (After):  " << *balance_node1_after << std::endl;
    std::cout << "Balance node2 (After):  " << *balance_node2_after << std::endl;

    // ASSERT_EQ( balance_main - cost, node_main->GetBalance() );
    //TODO: convert DEV_CONFIG.Cut from string to fixed and use below
    // ASSERT_EQ( balance_node1 + balance_node2 + ( cost * 65 ) / 100,
    //            node_proc1->GetBalance() + node_proc2->GetBalance() );

    auto gameDeveloperPayment = cost - ( ( cost * 65 ) / 100 );
    // ASSERT_EQ( balance_main + balance_node1 + balance_node2,
    //            node_main->GetBalance() + node_proc1->GetBalance() + node_proc2->GetBalance() + gameDeveloperPayment );
}

TEST_F( ProcessingMultiTest, ProcessTwo )
{
    std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
    std::string json_data = R"(
                {
                "data": {
                    "type": "file",
                    "URL": "file://[basepath]../../../../test/src/processing_nodes/"
                },
                "model": {
                    "name": "mnnimage",
                    "file": "model.mnn"
                },
                "input": [
                    {
                        "image": "data/ballet.data",
                        "block_len": 4860000 ,
                        "block_line_stride": 5400,
                        "block_stride": 0,
                        "chunk_line_stride": 1080,
                        "chunk_offset": 0,
                        "chunk_stride": 4320,
                        "chunk_subchunk_height": 5,
                        "chunk_subchunk_width": 5,
                        "chunk_count": 25,
                        "channels": 4
                    },
                    {
                        "image": "data/frisbee3.data",
                        "block_len": 786432 ,
                        "block_line_stride": 1536,
                        "block_stride": 0,
                        "chunk_line_stride": 384,
                        "chunk_offset": 0,
                        "chunk_stride": 1152,
                        "chunk_subchunk_height": 4,
                        "chunk_subchunk_width": 4,
                        "chunk_count": 16,
                        "channels": 3
                    }
                ]
                }
               )";
    auto        procmgr   = sgns::sgprocessing::ProcessingManager::Create( json_data );
    ASSERT_FALSE( procmgr.has_error() ) << procmgr.error().message();
    auto        cost      = node_main->GetProcessCost( procmgr.value() );
    boost::replace_all( json_data, "[basepath]", bin_path );
    std::cout << "Json Data: " << json_data << std::endl;
    const auto balance_main  = ActiveBalance( node_main );
    const auto balance_node1 = ActiveBalance( node_proc1 );
    const auto balance_node2 = ActiveBalance( node_proc2 );
    ASSERT_TRUE( balance_main.has_value() );
    ASSERT_TRUE( balance_node1.has_value() );
    ASSERT_TRUE( balance_node2.has_value() );
    ASSERT_TRUE( RequireActiveGeneration( node_proc1 ) );
    node_proc1->StopProcessing();
    ASSERT_TRUE( RequireActiveGeneration( node_proc2 ) );
    node_proc2->StartProcessing();
    std::vector bootstrappers = { node_main->GetPubSub()->GetLocalAddress(),
                                  node_proc1->GetPubSub()->GetLocalAddress() };
    node_proc2->AddPeers( bootstrappers );
    ASSERT_TRUE( SubmitProcessImage( node_main, json_data ) );
    //node_main->ProcessImage(json_data);

    std::this_thread::sleep_for( std::chrono::milliseconds( 40000 ) );
    std::cout << "Balance main (Before):  " << *balance_main << std::endl;
    std::cout << "Balance node1 (Before): " << *balance_node1 << std::endl;
    std::cout << "Balance node2 (Before): " << *balance_node2 << std::endl;
    std::cout << "Cost:                   " << cost << std::endl;
    const auto balance_main_after  = ActiveBalance( node_main );
    const auto balance_node1_after = ActiveBalance( node_proc1 );
    const auto balance_node2_after = ActiveBalance( node_proc2 );
    ASSERT_TRUE( balance_main_after.has_value() );
    ASSERT_TRUE( balance_node1_after.has_value() );
    ASSERT_TRUE( balance_node2_after.has_value() );
    std::cout << "Balance main (After):   " << *balance_main_after << std::endl;
    std::cout << "Balance node1 (After):  " << *balance_node1_after << std::endl;
    std::cout << "Balance node2 (After):  " << *balance_node2_after << std::endl;

    // ASSERT_EQ( balance_main - cost, node_main->GetBalance() );
    //TODO: convert DEV_CONFIG.Cut from string to fixed and use below
    // ASSERT_EQ( balance_node1 + balance_node2 + ( cost * 65 ) / 100,
    //            node_proc1->GetBalance() + node_proc2->GetBalance() );

    auto gameDeveloperPayment = cost - ( ( cost * 65 ) / 100 );
    // ASSERT_EQ( balance_main + balance_node1 + balance_node2,
    //            node_main->GetBalance() + node_proc1->GetBalance() + node_proc2->GetBalance() + gameDeveloperPayment );
}
