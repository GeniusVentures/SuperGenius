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
#include "FileManager.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "testutil/mint_source_hash.hpp"
#include "testutil/TestMintInputValidator.hpp"

namespace
{
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

        // node_main: non-processor (is_processor=false), light node. Config-driven construction (Phase 3).
        std::filesystem::create_directories( DEV_CONFIG.BaseWritePath );
        sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/false, /*rpc_catchup=*/false );

        node_main = sgns::GeniusNode::New( DEV_CONFIG,
                           sgns::FromPrivateKey{ "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG2.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG2.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/true, /*rpc_catchup=*/false );
        node_proc1 = sgns::GeniusNode::New( DEV_CONFIG2,
                            sgns::FromPrivateKey{ "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG3.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG3.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/true, /*rpc_catchup=*/false );
        node_proc2 = sgns::GeniusNode::New( DEV_CONFIG3,
                            sgns::FromPrivateKey{ "fecabeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );

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
    ASSERT_TRUE( RequireActiveGeneration( node_proc1 ) );
    node_proc1->StartProcessing();
    auto procmgr = sgns::sgprocessing::ProcessingManager::Create( json_data );
    ASSERT_FALSE( procmgr.has_error() ) << procmgr.error().message();
    auto cost = node_main->GetProcessCost( procmgr.value() );
    boost::replace_all( json_data, "[basepath]", bin_path );
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
