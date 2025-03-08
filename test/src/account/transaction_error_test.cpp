
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

class TransactionRecoveryTest : public ::testing::Test
{
protected:
    static inline sgns::GeniusNode *node_proc1 = nullptr;
    static inline sgns::GeniusNode *node_proc2 = nullptr;

    static inline DevConfig_st DEV_CONFIG  = { "0xcafe", "0.65", 1.0, 0, "./node21" };
    static inline DevConfig_st DEV_CONFIG2 = { "0xcafe", "0.65", 1.0, 0, "./node22" };

    static inline std::string binary_path = "";

    static void SetUpTestSuite()
    {
        binary_path = boost::dll::program_location().parent_path().string();
        std::strncpy( DEV_CONFIG.BaseWritePath,
                      ( binary_path + "/node21/" ).c_str(),
                      sizeof( DEV_CONFIG.BaseWritePath ) );
        std::strncpy( DEV_CONFIG2.BaseWritePath,
                      ( binary_path + "/node22/" ).c_str(),
                      sizeof( DEV_CONFIG2.BaseWritePath ) );
        DEV_CONFIG.BaseWritePath[sizeof( DEV_CONFIG.BaseWritePath ) - 1]   = '\0';
        DEV_CONFIG2.BaseWritePath[sizeof( DEV_CONFIG2.BaseWritePath ) - 1] = '\0';
    }

    static void TearDownTestSuite()
    {
        if ( node_proc1 != nullptr )
        {
            delete node_proc1;
            node_proc1 = nullptr;
        }
        if ( node_proc2 != nullptr )
        {
            delete node_proc2;
            node_proc2 = nullptr;
        }
    }
};

TEST_F( TransactionRecoveryTest, MintTransactionLowTimeout )
{
    node_proc1 = new sgns::GeniusNode( DEV_CONFIG,
                                       "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                       false,
                                       false );
    node_proc2 = new sgns::GeniusNode( DEV_CONFIG2,
                                       "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                       false,
                                       false );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    auto balance_before       = node_proc1->GetBalance();
    auto out_txs_before       = node_proc1->GetOutTransactions();
    auto out_txs_count_before = out_txs_before.size();

    auto mint_result = node_proc1->MintTokens( 10000000000, "", "", "", std::chrono::milliseconds( 1 ) );
    ASSERT_FALSE( mint_result.has_value() );

    auto start_time = std::chrono::steady_clock::now();
    auto end_time   = start_time + std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS );

    while ( std::chrono::steady_clock::now() < end_time )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        auto current_out_txs = node_proc1->GetOutTransactions();
        auto current_balance = node_proc1->GetBalance();

        if ( current_out_txs.size() > out_txs_count_before )
        {
            break;
        }
        if ( current_balance >= balance_before + 10000000000 )
        {
            break;
        }
    }

    EXPECT_EQ( node_proc1->GetBalance(), balance_before ) << "Balance changed despite mint call with very low timeout";

    EXPECT_EQ( node_proc1->GetOutTransactions().size(), out_txs_count_before )
        << "Outgoing transactions changed despite mint call with very low timeout";
}
