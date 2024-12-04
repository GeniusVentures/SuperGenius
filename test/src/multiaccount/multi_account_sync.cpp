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

class MultiAccountTest : public ::testing::Test
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
                      ( binary_path + "/node100/" ).c_str(),
                      sizeof( DEV_CONFIG.BaseWritePath ) );
        std::strncpy( DEV_CONFIG2.BaseWritePath,
                      ( binary_path + "/node200/" ).c_str(),
                      sizeof( DEV_CONFIG2.BaseWritePath ) );
        std::strncpy( DEV_CONFIG3.BaseWritePath,
                      ( binary_path + "/node300/" ).c_str(),
                      sizeof( DEV_CONFIG3.BaseWritePath ) );

        // Ensure null termination in case the string is too long
        DEV_CONFIG.BaseWritePath[sizeof( DEV_CONFIG.BaseWritePath ) - 1]   = '\0';
        DEV_CONFIG2.BaseWritePath[sizeof( DEV_CONFIG2.BaseWritePath ) - 1] = '\0';
        DEV_CONFIG3.BaseWritePath[sizeof( DEV_CONFIG3.BaseWritePath ) - 1] = '\0';

        node_main  = new sgns::GeniusNode( DEV_CONFIG,
                                          "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                          false,
                                          false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        node_proc1 = new sgns::GeniusNode( DEV_CONFIG2,
                                           "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           true );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        node_proc2 = new sgns::GeniusNode( DEV_CONFIG3,
                                           "fecabeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           true );

        //Connect to each other
        std::vector bootstrappers = { node_proc2->GetPubSub()->GetLocalAddress() };
        node_main->GetPubSub()->AddPeers( bootstrappers );

        bootstrappers = { node_proc2->GetPubSub()->GetLocalAddress() };
        node_proc1->GetPubSub()->AddPeers( bootstrappers );

        bootstrappers = { node_main->GetPubSub()->GetLocalAddress(), node_proc1->GetPubSub()->GetLocalAddress() };
        node_proc2->GetPubSub()->AddPeers( bootstrappers );
    }

    static void TearDownTestSuite()
    {
        std::cout << "Tear down main" << std::endl;
        delete node_main;
        if ( !std::filesystem::remove_all( DEV_CONFIG.BaseWritePath ) )
        {
            std::cerr << "Could not delete main node files\n";
        }

        std::cout << "Tear down 2" << std::endl;
        delete node_proc1;
        if ( !std::filesystem::remove_all( DEV_CONFIG2.BaseWritePath ) )
        {
            std::cerr << "Could not delete node 2 files\n";
        }

        std::cout << "Tear down 3" << std::endl;
        delete node_proc2;
        if ( !std::filesystem::remove_all( DEV_CONFIG3.BaseWritePath ) )
        {
            std::cerr << "Could not delete node 3 files\n";
        }
    }
};

// Static member initialization
sgns::GeniusNode *MultiAccountTest::node_main  = nullptr;
sgns::GeniusNode *MultiAccountTest::node_proc1 = nullptr;
sgns::GeniusNode *MultiAccountTest::node_proc2 = nullptr;

DevConfig_st MultiAccountTest::DEV_CONFIG  = { "0xcafe", 0.65, 1.0, 0, "./node1" };
DevConfig_st MultiAccountTest::DEV_CONFIG2 = { "0xcafe", 0.65, 1.0, 1, "./node2" };
DevConfig_st MultiAccountTest::DEV_CONFIG3 = { "0xcafe", 0.65, 1.0, 0, "./node3" };

std::string MultiAccountTest::binary_path = "";


TEST_F( MultiAccountTest, SyncThroughThirdNode )
{
    node_main->MintTokens( 50, "", "", "" );
    std::this_thread::sleep_for( std::chrono::milliseconds( 10000 ) );
    int transcount_main  = node_main->GetTransactions().size();
    int transcount_node1 = node_proc1->GetTransactions().size();
    //int transcount_node2 = node_proc2->GetTransactions().size();
    std::cout << "Count 1" << transcount_main << std::endl;
    std::cout << "Count 2" << transcount_node1 << std::endl;
    //std::cout << "Count 3" << transcount_node2 << std::endl;
    int balance_main = node_main->GetBalance();
    int balance_node1 = node_proc1->GetBalance();
    std::cout << "Balance 1" << balance_main << std::endl;
    std::cout << "Balance 2" << balance_node1 << std::endl;  
    //ASSERT_EQ( transcount_main, 2 );
    ASSERT_EQ( transcount_node1, transcount_main );
    ASSERT_EQ( balance_node1, balance_main );
}