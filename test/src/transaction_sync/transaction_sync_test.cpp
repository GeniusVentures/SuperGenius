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

    static inline DevConfig_st DEV_CONFIG  = { "0xcafe", "0.65", 1.0, 0, "./node10" };
    static inline DevConfig_st DEV_CONFIG2 = { "0xcafe", "0.65", 1.0, 0, "./node20" };

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

TEST_F( TransactionSyncTest, TransactionSimpleTransfer )
{
    auto balance_1_before = node_proc1->GetBalance();
    auto balance_2_before = node_proc2->GetBalance();

    // Connect the nodes
    node_proc1->GetPubSub()->AddPeers( { node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc2->GetPubSub()->AddPeers( { node_proc1->GetPubSub()->GetLocalAddress() } );

    // Mint tokens with timeout
    auto mint_result = node_proc1->MintTokens( 10000000000, "", "", "", std::chrono::milliseconds( 50000 ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    auto [mint_tx_id, mint_duration] = mint_result.value();
    std::cout << "Mint transaction completed in " << mint_duration << " ms" << std::endl;

    // Verify balance after minting
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 10000000000 ) << "Correct Balance of outgoing transactions";

    // Transfer funds with timeout
    auto transfer_result = node_proc1->TransferFunds( 10000000000,
                                                      node_proc2->GetAddress(),
                                                      std::chrono::milliseconds( 50000 ) );
    ASSERT_TRUE( transfer_result.has_value() ) << "Transfer transaction failed or timed out";
    auto [transfer_tx_id, transfer_duration] = transfer_result.value();
    std::cout << "Transfer transaction completed in " << transfer_duration << " ms" << std::endl;

    auto start_time        = std::chrono::steady_clock::now();
    auto transfer_received = node_proc2->WaitForTransactionIncoming( transfer_tx_id,
                                                                     std::chrono::milliseconds( 50000 ) );
    ASSERT_TRUE( transfer_received );
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start_time ).count();
    std::cout << "Transfer Received transaction completed in " << duration << " ms" << std::endl;

    // Verify node_proc1's balance decreased
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before ) << "Transfer should decrease node_proc1's balance";

    // Verify node_proc2's balance increased
    EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 10000000000 )
        << "Transfer should increase node_proc2's balance";
}

TEST_F( TransactionSyncTest, DISABLED_TransactionMintSync )
{
    auto balance_1_before = node_proc1->GetBalance();
    auto balance_2_before = node_proc2->GetBalance();

    // Connect the nodes
    node_proc1->GetPubSub()->AddPeers( { node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc2->GetPubSub()->AddPeers( { node_proc1->GetPubSub()->GetLocalAddress() } );

    // Mint tokens on node_proc1
    std::vector<uint64_t> mint_amounts =
        { 10000000000, 20000000000, 30000000000, 40000000000, 50000000000, 60000000000 };

    for ( auto amount : mint_amounts )
    {
        auto mint_result = node_proc1->MintTokens( amount, "", "", "", std::chrono::milliseconds( 50000 ) );
        ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction of " << amount << " failed or timed out";

        auto [tx_id, duration] = mint_result.value();
        std::cout << "Mint transaction of " << amount << " completed in " << duration << " ms" << std::endl;
    }

    // Mint tokens on node_proc2
    auto mint_result1 = node_proc2->MintTokens( 10000000000, "", "", "", std::chrono::milliseconds( 50000 ) );
    ASSERT_TRUE( mint_result1.has_value() ) << "Mint transaction failed or timed out";

    auto mint_result2 = node_proc2->MintTokens( 20000000000, "", "", "", std::chrono::milliseconds( 50000 ) );
    ASSERT_TRUE( mint_result2.has_value() ) << "Mint transaction failed or timed out";

    // Verify balances after minting
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 210000000000 )
        << "Correct Balance of outgoing transactions";
    EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 30000000000 ) << "Correct Balance of outgoing transactions";

    // Transfer funds
    auto transfer_result1 = node_proc1->TransferFunds( 10000000000,
                                                       node_proc2->GetAddress(),
                                                       std::chrono::milliseconds( 50000 ) );
    ASSERT_TRUE( transfer_result1.has_value() ) << "Transfer transaction failed or timed out";
    auto [transfer_tx_id1, transfer_duration1] = transfer_result1.value();

    auto transfer_result2 = node_proc1->TransferFunds( 20000000000,
                                                       node_proc2->GetAddress(),
                                                       std::chrono::milliseconds( 50000 ) );
    ASSERT_TRUE( transfer_result2.has_value() ) << "Transfer transaction failed or timed out";
    auto [transfer_tx_id2, transfer_duration2] = transfer_result2.value();

    // wait for both transfers to happen or timeout.
    auto start_time        = std::chrono::steady_clock::now();
    auto transfer_received = node_proc2->WaitForTransactionIncoming( transfer_tx_id1,
                                                                     std::chrono::milliseconds( 50000 ) );
    ASSERT_TRUE( transfer_received );
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start_time ).count();
    std::cout << "node2 Transfer Received transaction completed in " << duration << " ms" << std::endl;

    // wait for both transfers to happen or timeout.
    start_time        = std::chrono::steady_clock::now();
    transfer_received = node_proc1->WaitForTransactionIncoming( transfer_tx_id2,
                                                                std::chrono::milliseconds( 50000 ) );
    ASSERT_TRUE( transfer_received );
    duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start_time )
                   .count();
    std::cout << "node1 Transfer Received transaction completed in " << duration << " ms" << std::endl;

    // Verify balances after transfers
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 180000000000 )
        << "Correct Balance of outgoing transactions";
    EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 60000000000 ) << "Correct Balance of outgoing transactions";
}
