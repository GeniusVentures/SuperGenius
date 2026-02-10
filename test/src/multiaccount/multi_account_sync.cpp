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
#include <atomic>
#include <random>
#include <ctime>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "testutil/wait_condition.hpp"

class MultiAccountTest : public ::testing::Test
{
protected:
    std::shared_ptr<sgns::GeniusNode> CreateNode( const std::string &self_address,
                                                  const std::string &dev_addr,
                                                  const std::string &tokenValue,
                                                  sgns::TokenID      tokenId,
                                                  bool               isFullNode          = false,
                                                  bool               isProcessor         = false,
                                                  bool               isGenesisAuthorized = false )
    {
        static std::atomic<int> nodeCounter{ 0 };
        int                     id = nodeCounter.fetch_add( 1 );

        std::string binaryPath = boost::dll::program_location().parent_path().string();
        const char *filePath   = ::testing::UnitTest::GetInstance()->current_test_info()->file();
        std::string fileStem   = std::filesystem::path( filePath ).stem().string();
        auto        outPath    = binaryPath + "/node_multi_account_" + std::to_string( id ) + "/";

        DevConfig_st devConfig = { "", "0.65", tokenValue, tokenId, "" };
        std::strncpy( devConfig.Addr, dev_addr.c_str(), sizeof( devConfig.Addr ) - 1 );
        std::strncpy( devConfig.BaseWritePath, outPath.c_str(), sizeof( devConfig.BaseWritePath ) - 1 );
        devConfig.Addr[sizeof( devConfig.Addr ) - 1]                   = '\0';
        devConfig.BaseWritePath[sizeof( devConfig.BaseWritePath ) - 1] = '\0';

        // Generate deterministic key from self_address
        std::string key;
        key.reserve( 64 );

        // Create a hash of the self_address to make it deterministic
        std::hash<std::string> hasher;
        size_t                 address_hash = hasher( self_address );

        // Use the hash as seed for deterministic random generation
        std::mt19937                    rng( static_cast<uint32_t>( address_hash ) );
        std::uniform_int_distribution<> dist( 0, 15 );
        std::generate_n( std::back_inserter( key ),
                         64,
                         [&]()
                         {
                             static constexpr std::string_view hexChars = "0123456789abcdef";
                             return hexChars[dist( rng )];
                         } );

        if ( isGenesisAuthorized )
        {
            auto response = GeniusAccount::GenerateGeniusAddress( key.c_str(), outPath );
            if ( !response.has_value() )
            {
                ADD_FAILURE() << "Failed to generate full-node address for authorization";
            }
            else
            {
                const auto &pub_address = response.value().second.second.GetEntirePubValue();
                sgns::Blockchain::SetAuthorizedFullNodeAddress( pub_address );
            }
        }

        uint16_t uniquePort = static_cast<uint16_t>( 40001 + id );
        auto     node = sgns::GeniusNode::New( devConfig, key.c_str(), false, isProcessor, uniquePort, isFullNode );

        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        return node;
    }

    void SetUp() override
    {
        // Clean up any previous test runs
        std::string binaryPath = boost::dll::program_location().parent_path().string();

        // Helper to remove directory with retry on Windows (file locks may not be immediately released)
        auto removeWithRetry = []( const std::string &path )
        {
            std::error_code ec;
            std::filesystem::remove_all( path, ec );

            // On Windows, retry if removal fails due to file locks
            if ( ec && std::filesystem::exists( path ) )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
                ec.clear();
                std::filesystem::remove_all( path, ec );

                // Final attempt after longer delay
                if ( ec && std::filesystem::exists( path ) )
                {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
                    ec.clear();
                    std::filesystem::remove_all( path, ec );
                }
            }
        };

        removeWithRetry( binaryPath + "/node_multi_account_0/" );
        removeWithRetry( binaryPath + "/node_multi_account_1/" );
        removeWithRetry( binaryPath + "/node_multi_account_2/" );
    }

    void TearDown() override
    {
        // Cleanup is automatic when shared_ptrs go out of scope
        // On Windows, give time for file handles to be released before next test
        std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    }
};

TEST_F( MultiAccountTest, SyncThroughEachOther )
{
    // Create nodes dynamically
    auto node_full = CreateNode( "node_multi_full",
                                 "0xcafe",
                                 "1.0",
                                 TokenID::FromBytes( { 0x00 } ),
                                 true, // is full node
                                 true, // is processor
                                 true );
    test::assertWaitForCondition(
        [&]() { return node_full->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_full not synced" );
    auto node_main = CreateNode( "node_multi_1",
                                 "0xcafe",
                                 "1.0",
                                 TokenID::FromBytes( { 0x00 } ),
                                 false, // not full node
                                 false  // not processor
    );

    auto node_proc1 = CreateNode( "node_multi_1",
                                  "0xcafe",
                                  "1.0",
                                  TokenID::FromBytes( { 0x00 } ),
                                  false, // not full node
                                  true   // is processor
    );

    node_main->GetPubSub()->AddPeers(
        { node_proc1->GetPubSub()->GetInterfaceAddress(), node_full->GetPubSub()->GetInterfaceAddress() } );

    node_full->GetPubSub()->AddPeers( { node_proc1->GetPubSub()->GetInterfaceAddress() } );

    test::assertWaitForCondition(
        [&]() { return node_proc1->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_proc1 not synced" );
    test::assertWaitForCondition(
        [&]() { return node_main->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_main not synced" );

    // Get initial state
    auto transcount_main_start  = node_main->GetOutTransactions().size();
    auto transcount_node1_start = node_proc1->GetOutTransactions().size();
    auto main_balance_start     = node_main->GetBalance();
    auto node1_balance_start    = node_proc1->GetBalance();

    // Mint tokens on each node
    auto mint_result = node_main->MintTokens( 50000000000,
                                              "",
                                              "",
                                              TokenID::FromBytes( { 0x00 } ),
                                              std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_main";

    std::cout << "Mint transaction on main node completed, waiting for sync..." << std::endl;

    test::assertWaitForCondition( [&] { return node_proc1->GetBalance() == 50000000000; },
                                  std::chrono::milliseconds( 30000 ),
                                  "node_proc1 balance not synced" );

    //TODO - this is not working at the moment
    //auto mint_received = node_proc1->WaitForTransactionOutgoing(
    //    mint_result.value().first,
    //    std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    //EXPECT_EQ( mint_received, TransactionManager::TransactionStatus::CONFIRMED );
    mint_result = node_proc1->MintTokens( 50000000000,
                                          "",
                                          "",
                                          TokenID::FromBytes( { 0x00 } ),
                                          std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_proc1";

    test::assertWaitForCondition( [&] { return node_main->GetBalance() == 100000000000; },
                                  std::chrono::milliseconds( 30000 ),
                                  "node_main balance not synced" );
    //TODO - this is not working at the moment
    //auto mint_received2 = node_main->WaitForTransactionOutgoing(
    //    mint_result.value().first,
    //    std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );

    // Get final state
    auto transcount_main  = node_main->GetOutTransactions().size();
    auto transcount_node1 = node_proc1->GetOutTransactions().size();

    std::cout << "Count main: " << transcount_main << std::endl;
    std::cout << "Count node1: " << transcount_node1 << std::endl;

    double balance_main  = node_main->GetBalance();
    double balance_node1 = node_proc1->GetBalance();

    std::cout << "Balance main: " << balance_main << std::endl;
    std::cout << "Balance node1: " << balance_node1 << std::endl;

    // Verify results
    ASSERT_EQ( transcount_main, transcount_main_start + 2 );
    ASSERT_EQ( transcount_node1, transcount_node1_start + 2 );
    ASSERT_EQ( balance_main, main_balance_start + 100000000000 );
    ASSERT_EQ( balance_node1, node1_balance_start + 100000000000 );

    // Nodes will be automatically destroyed when they go out of scope
}

TEST_F( MultiAccountTest, CRDTFilterDuplicateTx )
{
    // Create 3 nodes - 2 with the same address, 1 different (full node for network)
    auto node_full = CreateNode( "full_node_address_unique", // different self_address
                                 "0xcafe",                   // dev_addr
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true, // is full node
                                 true, // is processor
                                 true );

    test::assertWaitForCondition(
        [&]() { return node_full->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_full not synched" );
    auto node_same_addr_1 = CreateNode( "duplicate_address_12345", // same self_address
                                        "0xcafe",                  // dev_addr
                                        "1.0",
                                        sgns::TokenID::FromBytes( { 0x00 } ),
                                        false, //
                                        false  // not processor
    );

    node_same_addr_1->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    auto node_same_addr_2 = CreateNode( "duplicate_address_12345", // same self_address
                                        "0xcafe",                  // dev_addr
                                        "1.0",
                                        sgns::TokenID::FromBytes( { 0x00 } ),
                                        false, //
                                        true   // is processor
    );

    node_same_addr_2->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    test::assertWaitForCondition(
        [&]() { return node_same_addr_1->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 20000 ),
        "node_same_addr_1 not synced" );
    test::assertWaitForCondition(
        [&]() { return node_same_addr_2->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 20000 ),
        "node_same_addr_2 not synced" );

    // Verify nodes have the same address (they should since they use same self_address)
    ASSERT_EQ( node_same_addr_1->GetAddress(), node_same_addr_2->GetAddress() )
        << "Nodes with same self_address should have same address";

    std::cout << "Node 1 address: " << node_same_addr_1->GetAddress() << std::endl;
    std::cout << "Node 2 address: " << node_same_addr_2->GetAddress() << std::endl;
    std::cout << "Full node address: " << node_full->GetAddress() << std::endl;

    // Get initial balances (should be 0)
    auto balance_node1_start = node_same_addr_1->GetBalance();
    auto balance_node2_start = node_same_addr_2->GetBalance();
    auto balance_full_start  = node_full->GetBalance();

    std::cout << "Initial balances - Node1: " << balance_node1_start << ", Node2: " << balance_node2_start
              << ", Full: " << balance_full_start << std::endl;

    // Get initial transaction counts
    auto tx_count_node1_start = node_same_addr_1->GetOutTransactions().size();
    auto tx_count_node2_start = node_same_addr_2->GetOutTransactions().size();
    auto tx_count_full_start  = node_full->GetOutTransactions().size();

    std::cout << "Initial tx counts - Node1: " << tx_count_node1_start << ", Node2: " << tx_count_node2_start
              << ", Full: " << tx_count_full_start << std::endl;

    // Mint tokens on both nodes with same address BEFORE connecting them
    std::cout << "Minting tokens on isolated nodes..." << std::endl;

    auto mint_result_1 = node_same_addr_1->MintTokens( 50000000000, // 50 GNUS
                                                       "",
                                                       "",
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result_1.has_value() ) << "Mint transaction failed on node_same_addr_1";

    std::cout << "Mint transaction 1 ID: " << mint_result_1.value().first << std::endl;

    test::assertWaitForCondition( [&]() { return node_same_addr_2->GetBalance() == balance_node2_start + 50000000000; },
                                  std::chrono::milliseconds( 30000 ),
                                  "node_same_addr_2 balance not synched" );
    //TODO - this is not working at the moment
    //auto mint_received = node_same_addr_2->WaitForTransactionOutgoing(
    //    mint_result_1.value().first,
    //    std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    //EXPECT_EQ( mint_received, TransactionManager::TransactionStatus::CONFIRMED );

    // Check balances after minting but before connecting
    auto balance_node1_after_mint = node_same_addr_1->GetBalance();
    auto balance_node2_after_mint = node_same_addr_2->GetBalance();

    std::cout << "Balances after minting (isolated) - Node1: " << balance_node1_after_mint
              << ", Node2: " << balance_node2_after_mint << std::endl;

    // Both nodes should have their respective minted amounts since they're isolated
    ASSERT_EQ( balance_node1_after_mint, balance_node1_start + 50000000000 );
    ASSERT_EQ( balance_node2_after_mint, balance_node2_start + 50000000000 );

    // Now connect the nodes - this should trigger CRDT filter to resolve conflicts
    std::cout << "Creating conflicting transfers..." << std::endl;

    auto transfer1_res = node_same_addr_1->TransferFunds( 10000000000, // 10 GNUS
                                                          "0x00",
                                                          sgns::TokenID::FromBytes( { 0x00 } ) );

    ASSERT_TRUE( transfer1_res.has_value() ) << "Transfer 1 failed on node_same_addr_1";
    //std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    auto transfer2_res = node_same_addr_2->TransferFunds( 13000000000, // 13 GNUS
                                                          "0x00",
                                                          sgns::TokenID::FromBytes( { 0x00 } ) );

    ASSERT_TRUE( transfer2_res.has_value() ) << "Transfer 2 failed on node_same_addr_2";

    // Add peers to each node
    node_same_addr_2->GetPubSub()->AddPeers( { node_same_addr_1->GetPubSub()->GetInterfaceAddress() } );

    auto tx1_received = node_full->WaitForTransactionIncoming(
        transfer1_res.value(),
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );

    test::assertWaitForCondition( [&]() { return node_same_addr_2->GetBalance() == node_same_addr_1->GetBalance(); },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_same_addr_2 balance not synched" );

    // Get final balances after CRDT resolution
    auto balance_node1_final = node_same_addr_1->GetBalance();
    auto balance_node2_final = node_same_addr_2->GetBalance();
    auto balance_full_final  = node_full->GetBalance( node_same_addr_1->GetAddress() );

    std::cout << "Final balances after CRDT resolution - Node1: " << balance_node1_final
              << ", Node2: " << balance_node2_final << ", Full: " << balance_full_final << std::endl;

    // Get final transaction counts
    auto tx_count_node1_final = node_same_addr_1->GetOutTransactions().size();
    auto tx_count_node2_final = node_same_addr_2->GetOutTransactions().size();

    std::cout << "Final tx counts - Node1: " << tx_count_node1_final << ", Node2: " << tx_count_node2_final
              << std::endl;

    // Since both nodes have the same address, they should have the same final balance
    ASSERT_EQ( balance_node1_final, balance_node2_final )
        << "Nodes with same address should have same balance after CRDT resolution";
    ASSERT_EQ( balance_full_final, balance_node2_final )
        << "Nodes with same address should have same balance after CRDT resolution";

    std::cout << "CRDT Filter test completed successfully!" << std::endl;
}
