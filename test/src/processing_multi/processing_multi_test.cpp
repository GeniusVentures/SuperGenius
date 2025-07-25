#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>

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
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>

class ProcessingMultiTest : public ::testing::Test
{
protected:
    static sgns::GeniusNode *node_main;
    static sgns::GeniusNode *node_proc1;
    static sgns::GeniusNode *node_proc2;

    static DevConfig_st DEV_CONFIG;
    static DevConfig_st DEV_CONFIG2;
    static DevConfig_st DEV_CONFIG3;

    static std::string binary_path;

    static void SetUpTestSuite()
    {
        std::string binary_path = boost::dll::program_location().parent_path().string();
        std::strncpy( DEV_CONFIG.BaseWritePath,
                      ( binary_path + "/node1/" ).c_str(),
                      sizeof( DEV_CONFIG.BaseWritePath ) );
        std::strncpy( DEV_CONFIG2.BaseWritePath,
                      ( binary_path + "/node2/" ).c_str(),
                      sizeof( DEV_CONFIG2.BaseWritePath ) );
        std::strncpy( DEV_CONFIG3.BaseWritePath,
                      ( binary_path + "/node3/" ).c_str(),
                      sizeof( DEV_CONFIG3.BaseWritePath ) );

        // Ensure null termination in case the string is too long
        DEV_CONFIG.BaseWritePath[sizeof( DEV_CONFIG.BaseWritePath ) - 1]   = '\0';
        DEV_CONFIG2.BaseWritePath[sizeof( DEV_CONFIG2.BaseWritePath ) - 1] = '\0';
        DEV_CONFIG3.BaseWritePath[sizeof( DEV_CONFIG3.BaseWritePath ) - 1] = '\0';

        node_main = new sgns::GeniusNode( DEV_CONFIG,
                                          "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                          false,
                                          false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        node_proc1 = new sgns::GeniusNode( DEV_CONFIG2,
                                           "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           true );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        node_proc2 = new sgns::GeniusNode( DEV_CONFIG3,
                                           "fecabeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           true );

        node_proc1->StopProcessing();
        node_proc2->StopProcessing();
        //Connect to each other
        std::vector bootstrappers = { node_proc1->GetPubSub()->GetLocalAddress(),
                                      node_proc2->GetPubSub()->GetLocalAddress() };
        node_main->GetPubSub()->AddPeers( bootstrappers );

        bootstrappers = { node_main->GetPubSub()->GetLocalAddress(), node_proc2->GetPubSub()->GetLocalAddress() };
        node_proc1->GetPubSub()->AddPeers( bootstrappers );

        // bootstrappers = { node_main->GetPubSub()->GetLocalAddress(), node_proc1->GetPubSub()->GetLocalAddress() };
        // node_proc2->GetPubSub()->AddPeers( bootstrappers );
    }

    static void TearDownTestSuite()
    {
        std::cout << "Tear down main" << std::endl;
        delete node_main;
        // if ( !std::filesystem::remove_all( DEV_CONFIG.BaseWritePath ) )
        // {
        //     std::cerr << "Could not delete main node files\n";
        // }

        std::cout << "Tear down 2" << std::endl;
        delete node_proc1;
        // if ( !std::filesystem::remove_all( DEV_CONFIG2.BaseWritePath ) )
        // {
        //     std::cerr << "Could not delete node 2 files\n";
        // }

        std::cout << "Tear down 3" << std::endl;
        delete node_proc2;
        // if ( !std::filesystem::remove_all( DEV_CONFIG3.BaseWritePath ) )
        // {
        //     std::cerr << "Could not delete node 3 files\n";
        // }
    }
};

// Static member initialization
sgns::GeniusNode *ProcessingMultiTest::node_main  = nullptr;
sgns::GeniusNode *ProcessingMultiTest::node_proc1 = nullptr;
sgns::GeniusNode *ProcessingMultiTest::node_proc2 = nullptr;

DevConfig_st ProcessingMultiTest::DEV_CONFIG  = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node1" };
DevConfig_st ProcessingMultiTest::DEV_CONFIG2 = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node2" };
DevConfig_st ProcessingMultiTest::DEV_CONFIG3 = { "0xcafe",
                                                  "0.65",
                                                  "1.0",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "./node3" };

std::string ProcessingMultiTest::binary_path = "";

TEST_F( ProcessingMultiTest, MintTokens )
{
    node_main->MintTokens( 50000000000, "", "", sgns::TokenID::FromBytes( { 0x00 } ) );
    node_main->MintTokens( 50000000000, "", "", sgns::TokenID::FromBytes( { 0x00 } ) );
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
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
    node_main->ProcessImage( json_data );
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
    node_proc1->StartProcessing();
    auto cost = node_main->GetProcessCost( json_data );
    boost::replace_all( json_data, "[basepath]", bin_path );
    std::cout << "Json Data: " << json_data << std::endl;
    auto balance_main  = node_main->GetBalance();
    auto balance_node1 = node_proc1->GetBalance();
    auto balance_node2 = node_proc2->GetBalance();
    //node_main->ProcessImage(json_data);

    std::this_thread::sleep_for( std::chrono::milliseconds( 40000 ) );
    std::cout << "Balance main (Before):  " << balance_main << std::endl;
    std::cout << "Balance node1 (Before): " << balance_node1 << std::endl;
    std::cout << "Balance node2 (Before): " << balance_node2 << std::endl;
    std::cout << "Cost:                   " << cost << std::endl;
    std::cout << "Balance main (After):   " << node_main->GetBalance() << std::endl;
    std::cout << "Balance node1 (After):  " << node_proc1->GetBalance() << std::endl;
    std::cout << "Balance node2 (After):  " << node_proc2->GetBalance() << std::endl;

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
    auto        cost      = node_main->GetProcessCost( json_data );
    boost::replace_all( json_data, "[basepath]", bin_path );
    std::cout << "Json Data: " << json_data << std::endl;
    auto balance_main  = node_main->GetBalance();
    auto balance_node1 = node_proc1->GetBalance();
    auto balance_node2 = node_proc2->GetBalance();
    node_proc1->StopProcessing();
    node_proc2->StartProcessing();
    std::vector bootstrappers = { node_main->GetPubSub()->GetLocalAddress(),
                                  node_proc1->GetPubSub()->GetLocalAddress() };
    node_proc2->GetPubSub()->AddPeers( bootstrappers );
    node_main->ProcessImage( json_data );
    //node_main->ProcessImage(json_data);

    std::this_thread::sleep_for( std::chrono::milliseconds( 40000 ) );
    std::cout << "Balance main (Before):  " << balance_main << std::endl;
    std::cout << "Balance node1 (Before): " << balance_node1 << std::endl;
    std::cout << "Balance node2 (Before): " << balance_node2 << std::endl;
    std::cout << "Cost:                   " << cost << std::endl;
    std::cout << "Balance main (After):   " << node_main->GetBalance() << std::endl;
    std::cout << "Balance node1 (After):  " << node_proc1->GetBalance() << std::endl;
    std::cout << "Balance node2 (After):  " << node_proc2->GetBalance() << std::endl;

    // ASSERT_EQ( balance_main - cost, node_main->GetBalance() );
    //TODO: convert DEV_CONFIG.Cut from string to fixed and use below
    // ASSERT_EQ( balance_node1 + balance_node2 + ( cost * 65 ) / 100,
    //            node_proc1->GetBalance() + node_proc2->GetBalance() );

    auto gameDeveloperPayment = cost - ( ( cost * 65 ) / 100 );
    // ASSERT_EQ( balance_main + balance_node1 + balance_node2,
    //            node_main->GetBalance() + node_proc1->GetBalance() + node_proc2->GetBalance() + gameDeveloperPayment );
}
