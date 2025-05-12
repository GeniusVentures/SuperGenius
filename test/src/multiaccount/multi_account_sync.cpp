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

    static DevConfig_st DEV_CONFIG;
    static DevConfig_st DEV_CONFIG2;

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

        // Ensure null termination in case the string is too long
        DEV_CONFIG.BaseWritePath[sizeof( DEV_CONFIG.BaseWritePath ) - 1]   = '\0';
        DEV_CONFIG2.BaseWritePath[sizeof( DEV_CONFIG2.BaseWritePath ) - 1] = '\0';

        // clean out any previous runs
        std::filesystem::remove_all( DEV_CONFIG.BaseWritePath );
        std::filesystem::remove_all( DEV_CONFIG2.BaseWritePath );

        node_main  = new sgns::GeniusNode( DEV_CONFIG,
                                          "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                          false,
                                          false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        node_proc1 = new sgns::GeniusNode( DEV_CONFIG2,
                                           "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           true );

        //Connect to each other
        std::vector bootstrappers = { node_proc1->GetPubSub()->GetLocalAddress() };
        node_main->GetPubSub()->AddPeers( bootstrappers );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    }

    static void TearDownTestSuite()
    {
        std::cout << "Tear down main" << std::endl;
        delete node_main;

        std::cout << "Tear down 2" << std::endl;
        delete node_proc1;

    }
};

// Static member initialization
sgns::GeniusNode *MultiAccountTest::node_main  = nullptr;
sgns::GeniusNode *MultiAccountTest::node_proc1 = nullptr;

DevConfig_st MultiAccountTest::DEV_CONFIG  = { "0xcafe", "0.65", 1.0, 0, "./node1" };
DevConfig_st MultiAccountTest::DEV_CONFIG2 = { "0xcafe", "0.65", 1.0, 1, "./node2" };

std::string MultiAccountTest::binary_path = "";


TEST_F( MultiAccountTest, SyncThroughEachOther )
{
    //Just making sure they connect
    auto transcount_main_start  = node_main->GetOutTransactions().size();
    auto transcount_node1_start = node_proc1->GetOutTransactions().size();
    auto main_balance_start = node_main->GetBalance();
    auto node1_balance_start = node_proc1->GetBalance();

    //Mint On each
    auto mint_result = node_main->MintTokens( 50000000000 , "", "", "", std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    mint_result = node_proc1->MintTokens( 50000000000 , "", "", "", std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";
    auto transcount_main  = node_main->GetOutTransactions().size();
    auto transcount_node1 = node_proc1->GetOutTransactions().size();
    std::cout << "Count 1" << transcount_main << std::endl;
    std::cout << "Count 2" << transcount_node1 << std::endl;
    double balance_main = node_main->GetBalance();
    double balance_node1 = node_proc1->GetBalance();
    std::cout << "Balance 1" << balance_main << std::endl;
    std::cout << "Balance 2" << balance_node1 << std::endl;  

    // TODO: in reality, one of the mint function should get rejected with same nonce.
    ASSERT_EQ( transcount_main, transcount_main_start + 1);
    ASSERT_EQ( transcount_node1, transcount_node1_start + 1);
    ASSERT_EQ( balance_main, main_balance_start + 50000000000);
    ASSERT_EQ( balance_node1, node1_balance_start + 50000000000);

}