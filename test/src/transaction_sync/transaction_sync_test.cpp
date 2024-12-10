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
#include <boost/multiprecision/cpp_int.hpp>
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>

class TransactionSyncTest : public ::testing::Test
{
protected:
    static inline sgns::GeniusNode *node_proc1 = nullptr;
    static inline sgns::GeniusNode *node_proc2 = nullptr;

    static inline DevConfig_st DEV_CONFIG  = { "0xcafe", 0.65, 1.0, 0, "./node10" };
    static inline DevConfig_st DEV_CONFIG2 = { "0xcafe", 0.65, 1.0, 0, "./node20" };

    static inline std::string binary_path = "";

    static void SetUpTestSuite()
    {
        std::string binary_path = boost::dll::program_location().parent_path().string();
        std::strncpy( DEV_CONFIG.BaseWritePath,
                      ( binary_path + "/node10/" ).c_str(),
                      sizeof( DEV_CONFIG.BaseWritePath ) );
        std::strncpy( DEV_CONFIG2.BaseWritePath,
                      ( binary_path + "/node20/" ).c_str(),
                      sizeof( DEV_CONFIG2.BaseWritePath ) );

        // Ensure null termination in case the string is too long
        DEV_CONFIG.BaseWritePath[sizeof( DEV_CONFIG.BaseWritePath ) - 1]   = '\0';
        DEV_CONFIG2.BaseWritePath[sizeof( DEV_CONFIG2.BaseWritePath ) - 1] = '\0';

        node_proc1 = new sgns::GeniusNode( DEV_CONFIG,
                                           "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        node_proc2 = new sgns::GeniusNode( DEV_CONFIG2,
                                           "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           false );
    }

    static void TearDownTestSuite()
    {
        delete node_proc1;
        delete node_proc2;
    }
};

TEST_F( TransactionSyncTest, TransactionMintSync )
{
    auto balance_1_before = node_proc1->GetBalance();
    auto balance_2_before = node_proc2->GetBalance();
    node_proc1->MintTokens( 10, "", "", "" );
    node_proc1->MintTokens( 20, "", "", "" );
    node_proc1->MintTokens( 30, "", "", "" );
    node_proc1->MintTokens( 40, "", "", "" );
    node_proc1->MintTokens( 50, "", "", "" );
    node_proc1->MintTokens( 60, "", "", "" );
    node_proc1->GetPubSub()->AddPeers( { node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc2->GetPubSub()->AddPeers( { node_proc1->GetPubSub()->GetLocalAddress() } );
    node_proc2->MintTokens( 10, "", "", "" );
    node_proc2->MintTokens( 20, "", "", "" );

    std::this_thread::sleep_for( std::chrono::milliseconds( 5000 ) );

    //EXPECT_EQ( node_proc1->GetBlocks().size(), node_proc2->GetBlocks().size() ) << "Same number of blocks";
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 210 ) << "Correct Balance of outgoing transactions";
    EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 30 ) << "Correct Balance of outgoing transactions";

    node_proc1->TransferFunds( 10, node_proc2->GetAddress<boost::multiprecision::uint256_t>());
    node_proc1->TransferFunds( 20, node_proc2->GetAddress<boost::multiprecision::uint256_t>());
    std::this_thread::sleep_for( std::chrono::milliseconds( 5000 ) );
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 180 ) << "Correct Balance of outgoing transactions";
    EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 60 ) << "Correct Balance of outgoing transactions";
}
