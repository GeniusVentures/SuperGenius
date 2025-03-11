
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

        node_proc1 = new sgns::GeniusNode( DEV_CONFIG,
                                           "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           false );
        node_proc2 = new sgns::GeniusNode( DEV_CONFIG2,
                                           "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

        node_proc1->GetPubSub()->AddPeers( { node_proc2->GetPubSub()->GetLocalAddress() } );
        node_proc2->GetPubSub()->AddPeers( { node_proc1->GetPubSub()->GetLocalAddress() } );
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
    node_proc1->PauseTransactionProcessing();

    auto balance_before       = node_proc1->GetBalance();
    auto out_txs_before       = node_proc1->GetOutTransactions();
    auto out_txs_count_before = out_txs_before.size();

    auto mint_result = node_proc1->MintTokens( 100000000000, "", "", "", std::chrono::milliseconds( 1 ) );
    ASSERT_FALSE( mint_result.has_value() );

    std::this_thread::sleep_for( std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );

    EXPECT_EQ( node_proc1->GetOutTransactions().size(), out_txs_count_before );
    EXPECT_EQ( node_proc1->GetBalance(), balance_before );
    node_proc1->ResumeTransactionProcessing();

    std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );
}

TEST_F( TransactionRecoveryTest, MintAfterTimeoutFailure )
{
    node_proc1->PauseTransactionProcessing();
    auto balance_before       = node_proc1->GetBalance();
    auto out_txs_before       = node_proc1->GetOutTransactions();
    auto out_txs_count_before = out_txs_before.size();

    auto mint_result = node_proc1->MintTokens( 100000000000, "", "", "", std::chrono::milliseconds( 1 ) );
    ASSERT_FALSE( mint_result.has_value() );

    node_proc1->ResumeTransactionProcessing();
    mint_result = node_proc1->MintTokens( 1, "", "", "", std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() );

    EXPECT_EQ( node_proc1->GetOutTransactions().size(), out_txs_count_before + 1 );
    EXPECT_EQ( node_proc1->GetBalance(), balance_before + 1 );

    std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );
}

TEST_F( TransactionRecoveryTest, TransferTransactionLowTimeout )
{
    auto mint_result = node_proc1->MintTokens( 1500000000,
                                               "",
                                               "",
                                               "",
                                               std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() );

    node_proc1->PauseTransactionProcessing();
    auto node1_balance_before = node_proc1->GetBalance();
    auto node2_balance_before = node_proc2->GetBalance();
    auto node1_out_txs_before = node_proc1->GetOutTransactions().size();

    auto transfer_result = node_proc1->TransferFunds( 1000000000,
                                                      node_proc2->GetAddress(),
                                                      std::chrono::milliseconds( 1 ) );
    ASSERT_FALSE( transfer_result.has_value() );

    std::this_thread::sleep_for( std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );

    EXPECT_EQ( node_proc2->GetBalance(), node2_balance_before );
    EXPECT_EQ( node_proc1->GetBalance(), node1_balance_before );
    EXPECT_EQ( node_proc1->GetOutTransactions().size(), node1_out_txs_before );

    std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );
    node_proc1->ResumeTransactionProcessing();
}

TEST_F( TransactionRecoveryTest, TransferAfterTimeoutFailure )
{
    auto mint_result = node_proc1->MintTokens( 1500000000,
                                               "",
                                               "",
                                               "",
                                               std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() );

    node_proc1->PauseTransactionProcessing();
    auto node1_balance_before = node_proc1->GetBalance();
    auto node2_balance_before = node_proc2->GetBalance();
    auto node1_out_txs_before = node_proc1->GetOutTransactions().size();
    auto node2_in_txs_before  = node_proc2->GetInTransactions().size();

    auto transfer_result = node_proc1->TransferFunds( 1000000000,
                                                      node_proc2->GetAddress(),
                                                      std::chrono::milliseconds( 1 ) );
    ASSERT_FALSE( transfer_result.has_value() );

    node_proc1->ResumeTransactionProcessing();
    transfer_result = node_proc1->TransferFunds( 1,
                                                 node_proc2->GetAddress(),
                                                 std::chrono::milliseconds( 30000 ) );
    ASSERT_TRUE( transfer_result.has_value() );

    EXPECT_EQ( node_proc1->GetBalance(), node1_balance_before - 1 );
    EXPECT_EQ( node_proc2->GetBalance(), node2_balance_before + 1 );
    EXPECT_EQ( node_proc1->GetOutTransactions().size(), node1_out_txs_before + 1 );
    EXPECT_EQ( node_proc2->GetInTransactions().size(), node2_in_txs_before + 1 );

    std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );
}
