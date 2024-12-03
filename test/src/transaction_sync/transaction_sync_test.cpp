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
    node_proc1->MintTokens( 10, "", "", "" );
    node_proc1->MintTokens( 21, "", "", "" );
    node_proc1->MintTokens( 32, "", "", "" );
    node_proc1->MintTokens( 43, "", "", "" );
    node_proc1->MintTokens( 54, "", "", "" );
    node_proc1->MintTokens( 65, "", "", "" );
    node_proc1->GetPubSub()->AddPeers( { node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc2->GetPubSub()->AddPeers( { node_proc1->GetPubSub()->GetLocalAddress() } );

    std::this_thread::sleep_for( std::chrono::milliseconds( 20000 ) );

    EXPECT_EQ( node_proc1->GetBlocks().size(), node_proc2->GetBlocks().size() ) << "Same number of blocks";
}
