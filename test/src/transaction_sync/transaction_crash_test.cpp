#include <gtest/gtest.h>
#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdio>
#include <boost/dll.hpp>
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/TestMintInputValidator.hpp"

namespace sgns
{
    namespace
    {
        std::string RequireActiveAddress( const GeniusNode &node )
        {
            const auto address = node.GetActiveAccountAddress();
            if ( address.has_error() )
            {
                ADD_FAILURE() << "expected active account address: " << address.error().message();
                return {};
            }
            return address.value();
        }

        uint64_t RequireActiveBalance( GeniusNode &node )
        {
            (void)RequireActiveAddress( node );
            return node.GetBalance();
        }

        TransactionManager::TransactionStatus RequireIncomingStatus( GeniusNode &node,
                                                                       const std::string &transaction_id,
                                                                       std::chrono::milliseconds timeout )
        {
            (void)RequireActiveAddress( node );
            return node.WaitForTransactionIncoming( transaction_id, timeout );
        }
    } // namespace

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
        static inline std::shared_ptr<sgns::GeniusNode> node1 = nullptr;
        static inline std::shared_ptr<sgns::GeniusNode> node2 = nullptr;

        // Configuration for node instances
        static inline GeniusNodeConfig CONFIG1 = { "0xcafe",
                                               "0.65",
                                               "1.0",
                                               sgns::TokenID::FromBytes( { 0x00 } ),
                                               "./transaction_crash_node1" };
        static inline GeniusNodeConfig CONFIG2 = { "0xcafe",
                                               "0.65",
                                               "1.0",
                                               sgns::TokenID::FromBytes( { 0x00 } ),
                                               "./transaction_crash_node2" };

        // Constants for iterations
        static constexpr int TOTAL_TRANSFERS        = 20;
        static constexpr int INITIAL_WAIT_TRANSFERS = 5;

        /**
     * @brief Initialize nodes before all tests.
     */
        static void SetUpTestSuite()
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );

            std::string binary_path = boost::dll::program_location().parent_path().string();

            CONFIG1.BaseWritePath = ( binary_path + "/transaction_crash_node1/" );
            CONFIG2.BaseWritePath = ( binary_path + "/transaction_crash_node2/" );

            test::removeAllWithRetry( CONFIG1.BaseWritePath );
            test::removeAllWithRetry( CONFIG2.BaseWritePath );

            // All nodes in this test are non-processors.
            // is_processor is now read exclusively from sgns_config.json (defaults to true).
            std::filesystem::create_directories( CONFIG1.BaseWritePath );
            sgns::GeniusNode::WriteNetworkConfig( CONFIG1.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
            sgns::GeniusNode::WriteSgnsConfig( CONFIG1.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/false, /*rpc_catchup=*/false );
            std::filesystem::create_directories( CONFIG2.BaseWritePath );
            sgns::GeniusNode::WriteNetworkConfig( CONFIG2.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
            sgns::GeniusNode::WriteSgnsConfig( CONFIG2.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/false, /*rpc_catchup=*/false );

            node1 = sgns::GeniusNode::New( CONFIG1,
                           sgns::FromPrivateKey{ "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
            node2 = sgns::GeniusNode::New( CONFIG2,
                           sgns::FromPrivateKey{ "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        }

        /**
     * @brief Clean up nodes after all tests.
     */
        static void TearDownTestSuite()
        {
            node1.reset();
            node2.reset();
        }

        /**
     * @brief Restart node2 to simulate crash and recovery.
     */
        void RestartNode2()
        {
            node2.reset();
            std::this_thread::sleep_for( std::chrono::milliseconds( 5000 ) );
            node2 = sgns::GeniusNode::New( CONFIG2,
                           sgns::FromPrivateKey{ "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } );
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        }
    };

    /**
 * @brief Test transaction synchronization after node crash and recovery.
 */
    TEST_F( CrashRecoverySyncTest, DISABLED_TransactionSyncAfterCrash )
    {
        std::cout << "Recording initial balance for verification" << std::endl;
        auto initial_balance = RequireActiveBalance( *node1 );

        std::cout << "node1->GetBalance(): " << RequireActiveBalance( *node1 ) << std::endl;
        std::cout << "node2->GetBalance(): " << RequireActiveBalance( *node2 ) << std::endl;

        std::cout << "Calculating total amount to mint" << std::endl;
        uint64_t total_amount = 0;
        for ( int i = 0; i < TOTAL_TRANSFERS; i++ )
        {
            total_amount += 1000;
        }

        std::cout << "Minting the required tokens" << std::endl;
        auto mint_result = node1->MintTokens( total_amount,
                                              sgns::test::NextMintSourceHash(),
                                              "test",
                                              TokenID::FromBytes( { 0x00 } ),
                                              "",
                                              std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
        ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";
        auto [mint_tx_id, mint_duration] = mint_result.value();
        std::cout << "Mint transaction " << mint_tx_id << " completed in " << mint_duration << " ms" << std::endl;
        EXPECT_EQ( RequireActiveBalance( *node1 ), initial_balance + total_amount ) << "Balance mismatch after minting";

        std::cout << "Executing transfers and collecting transaction IDs" << std::endl;
        std::vector<std::string> tx_ids;
        for ( int i = 0; i < TOTAL_TRANSFERS; i++ )
        {
            auto transfer_result = node1->TransferFunds( 1000,
                                                         RequireActiveAddress( *node2 ),
                                                         TokenID::FromBytes( { 0x00 } ),
                                                         std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
            ASSERT_TRUE( transfer_result.has_value() ) << "Transfer " << i << " failed";
            tx_ids.push_back( transfer_result.value().first );
        }

        std::cout << "Reconnecting nodes for transaction propagation" << std::endl;
        node1->AddPeers( { node2->GetPubSub()->GetLocalAddress() } );
        node2->AddPeers( { node1->GetPubSub()->GetLocalAddress() } );

        std::cout << "Waiting for the first batch of incoming transactions" << std::endl;
        for ( int i = 0; i < INITIAL_WAIT_TRANSFERS; i++ )
        {
            EXPECT_EQ( RequireIncomingStatus( *node2, tx_ids[i],
                                                          std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ),
                       TransactionManager::TransactionStatus::CONFIRMED )
                << "Failed to receive initial transaction " << tx_ids[i] << " on node2";
        }

        std::cout << "Simulating crash and recovery" << std::endl;
        RestartNode2();
        node1->AddPeers( { node2->GetPubSub()->GetLocalAddress() } );
        node2->AddPeers( { node1->GetPubSub()->GetLocalAddress() } );

        std::cout
            << "****************************Waiting for the remaining transactions after recovery****************************"
            << std::endl;
        for ( int i = 0; i < TOTAL_TRANSFERS; i++ )
        {
            EXPECT_EQ( RequireIncomingStatus( *node2, tx_ids[i],
                                                          std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ),
                       TransactionManager::TransactionStatus::CONFIRMED )
                << "Missing post-recovery transaction " << tx_ids[i];
        }
    }

} // namespace sgns
