#include <gtest/gtest.h>
#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif
#include <thread>
#include <chrono>
#include <boost/dll.hpp>
#include "account/GeniusNode.hpp"

namespace sgns
{

    /**
 * @file transaction_crash_sync_test_updated.cpp
 * @brief Verifies transaction synchronization after a node crash and recovery,
 *        including minting tokens before transfers.
 */

    /**
 * @class CrashRecoverySyncTest
 * @brief Test fixture for crash and recovery transaction synchronization.
 */
    class CrashRecoverySyncTest : public ::testing::Test
    {
    protected:
        static inline sgns::GeniusNode *node1 = nullptr;
        static inline sgns::GeniusNode *node2 = nullptr;

        // Configuration for node instances
        static inline DevConfig_st CONFIG1 = { "0xcafe",
                                               "0.65",
                                               "1.0",
                                               sgns::TokenID::FromBytes( { 0x00 } ),
                                               "./node_crash1" };
        static inline DevConfig_st CONFIG2 = { "0xcafe",
                                               "0.65",
                                               "1.0",
                                               sgns::TokenID::FromBytes( { 0x00 } ),
                                               "./node_crash2" };

        // Constants for iterations
        static constexpr int TOTAL_TRANSFERS        = 20;
        static constexpr int INITIAL_WAIT_TRANSFERS = 5;

        /**
     * @brief Initialize nodes before all tests.
     */
        static void SetUpTestSuite()
        {
            std::string binary_path = boost::dll::program_location().parent_path().string();
            std::strncpy( CONFIG1.BaseWritePath,
                          ( binary_path + "/node_crash1/" ).c_str(),
                          sizeof( CONFIG1.BaseWritePath ) );
            std::strncpy( CONFIG2.BaseWritePath,
                          ( binary_path + "/node_crash2/" ).c_str(),
                          sizeof( CONFIG2.BaseWritePath ) );
            CONFIG1.BaseWritePath[sizeof( CONFIG1.BaseWritePath ) - 1] = '\0';
            CONFIG2.BaseWritePath[sizeof( CONFIG2.BaseWritePath ) - 1] = '\0';

            node1 = new sgns::GeniusNode( CONFIG1,
                                          "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                          false,
                                          false );
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
            node2 = new sgns::GeniusNode( CONFIG2,
                                          "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                          false,
                                          false );
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        }

        /**
     * @brief Clean up nodes after all tests.
     */
        static void TearDownTestSuite()
        {
            delete node1;
            delete node2;
        }

        /**
     * @brief Restart node2 to simulate crash and recovery.
     */
        void RestartNode2()
        {
            delete node2;
            std::this_thread::sleep_for( std::chrono::milliseconds( 5000 ) );
            node2 = new sgns::GeniusNode( CONFIG2,
                                          "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                          false,
                                          false );
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        }
    };

    /**
 * @brief Test transaction synchronization after node crash and recovery.
 */
    TEST_F( CrashRecoverySyncTest, TransactionSyncAfterCrash )
    {
        std::cout << "Recording initial balance for verification" << std::endl;
        auto initial_balance = node1->GetBalance();

        std::cout << "node1->GetBalance(): " << node1->GetBalance() << std::endl;
        std::cout << "node2->GetBalance(): " << node2->GetBalance() << std::endl;

        std::cout << "Calculating total amount to mint" << std::endl;
        uint64_t total_amount = 0;
        for ( int i = 0; i < TOTAL_TRANSFERS; i++ )
        {
            total_amount += 1000;
        }

        std::cout << "Minting the required tokens" << std::endl;
        auto mint_result = node1->MintTokens( total_amount,
                                              "",
                                              "",
                                              TokenID::FromBytes( { 0x00 } ),
                                              std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";
        auto [mint_tx_id, mint_duration] = mint_result.value();
        std::cout << "Mint transaction " << mint_tx_id << " completed in " << mint_duration << " ms" << std::endl;
        EXPECT_EQ( node1->GetBalance(), initial_balance + total_amount ) << "Balance mismatch after minting";

        std::cout << "Executing transfers and collecting transaction IDs" << std::endl;
        std::vector<std::string> tx_ids;
        for ( int i = 0; i < TOTAL_TRANSFERS; i++ )
        {
            auto transfer_result = node1->TransferFunds( 1000,
                                                         node2->GetAddress(),
                                                         TokenID::FromBytes( { 0x00 } ),
                                                         std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
            ASSERT_TRUE( transfer_result.has_value() ) << "Transfer " << i << " failed";
            tx_ids.push_back( transfer_result.value().first );
        }

        std::cout << "Reconnecting nodes for transaction propagation" << std::endl;
        node1->GetPubSub()->AddPeers( { node2->GetPubSub()->GetLocalAddress() } );
        node2->GetPubSub()->AddPeers( { node1->GetPubSub()->GetLocalAddress() } );

        std::cout << "Waiting for the first batch of incoming transactions" << std::endl;
        for ( int i = 0; i < INITIAL_WAIT_TRANSFERS ; i++ )
        {
            ASSERT_TRUE(
                node2->WaitForTransactionIncoming( tx_ids[i],
                                                   std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ) )
                << "Failed to receive initial transaction " << tx_ids[i] << " on node2";
        }

        std::cout << "Simulating crash and recovery" << std::endl;
        RestartNode2();
        node1->GetPubSub()->AddPeers( { node2->GetPubSub()->GetLocalAddress() } );
        node2->GetPubSub()->AddPeers( { node1->GetPubSub()->GetLocalAddress() } );

        std::cout << "****************************Waiting for the remaining transactions after recovery****************************" << std::endl;
        for ( int i = 0; i < TOTAL_TRANSFERS; i++ )
        {
            ASSERT_TRUE(
                node2->WaitForTransactionIncoming( tx_ids[i],
                                                   std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ) )
                << "Missing post-recovery transaction " << tx_ids[i];
        }
    }

} // namespace sgns
