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
    auto node_original = CreateNode( "node_multi_1",
                                     "0xcafe",
                                     "1.0",
                                     TokenID::FromBytes( { 0x00 } ),
                                     false, // not full node
                                     false  // not processor
    );

    node_original->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    test::assertWaitForCondition(
        [&]() { return node_original->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_original not synced" );

    auto balance_original_start = node_original->GetBalance();
    // Mint some tokens
    auto mint_result = node_original->MintTokens( 100,
                                                  "",
                                                  "",
                                                  TokenID::FromBytes( { 0x00 } ),
                                                  std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";

    mint_result = node_original->MintTokens( 2000,
                                             "",
                                             "",
                                             TokenID::FromBytes( { 0x00 } ),
                                             std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";
    mint_result = node_original->MintTokens( 30,
                                             "",
                                             "",
                                             TokenID::FromBytes( { 0x00 } ),
                                             std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );

    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";

    std::cout << " 3 mint transactions on original node completed, Creating duplicated node..." << std::endl;

    auto node_duplicated = CreateNode( "node_multi_1",
                                       "0xcafe",
                                       "1.0",
                                       TokenID::FromBytes( { 0x00 } ),
                                       false, // not full node
                                       true   // is processor
    );
    node_duplicated->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    test::assertWaitForCondition(
        [&]() { return node_duplicated->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_duplicated not synced" );

    mint_result = node_duplicated->MintTokens( 60000,
                                               "",
                                               "",
                                               TokenID::FromBytes( { 0x00 } ),
                                               std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_duplicated";

    test::assertWaitForCondition(
        [&] { return ( balance_original_start + 60000 + 2000 + 100 + 30 ) == node_duplicated->GetBalance(); },
        std::chrono::milliseconds( 30000 ),
        "node_duplicated balance not synced" );

    ASSERT_EQ( node_duplicated->GetBalance(), node_original->GetBalance() );
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
        "node_full not synced" );
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

    std::cout << "Node 1 address: " << node_same_addr_1->GetAddress() << '\n';
    std::cout << "Node 2 address: " << node_same_addr_2->GetAddress() << '\n';
    std::cout << "Full node address: " << node_full->GetAddress() << '\n';

    // Get initial balances (should be 0)
    auto balance_node1_start = node_same_addr_1->GetBalance();
    auto balance_node2_start = node_same_addr_2->GetBalance();
    auto balance_full_start  = node_full->GetBalance();

    fmt::println( "Initial balances - Node1: {}, Node2: {}, Full: {}",
                  balance_node1_start,
                  balance_node2_start,
                  balance_full_start );

    // Get initial transaction counts
    auto tx_count_node1_start = node_same_addr_1->GetOutTransactions().size();
    auto tx_count_node2_start = node_same_addr_2->GetOutTransactions().size();
    auto tx_count_full_start  = node_full->GetOutTransactions().size();

    fmt::println( "Initial tx counts - Node1: {}, Node2: {}, Full: {}",
                  tx_count_node1_start,
                  tx_count_node2_start,
                  tx_count_full_start );

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
                                  "node_same_addr_2 balance not synced" );
    //TODO - this is not working at the moment
    //auto mint_received = node_same_addr_2->WaitForTransactionOutgoing(
    //    mint_result_1.value().first,
    //    std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    //EXPECT_EQ( mint_received, TransactionManager::TransactionStatus::CONFIRMED );

    // Check balances after minting but before connecting
    auto balance_node1_after_mint = node_same_addr_1->GetBalance();
    auto balance_node2_after_mint = node_same_addr_2->GetBalance();

    fmt::println( "Balances after minting (isolated) - Node1: {}, Node2: {}",
                  balance_node1_after_mint,
                  balance_node2_after_mint );

    // Both nodes should have their respective minted amounts since they're isolated
    ASSERT_EQ( balance_node1_after_mint, balance_node1_start + 50000000000 );
    ASSERT_EQ( balance_node2_after_mint, balance_node2_start + 50000000000 );

    // Now connect the nodes - this should trigger CRDT filter to resolve conflicts
    std::cout << "Creating conflicting transfers..." << std::endl;

    auto transfer1_res = node_same_addr_1->TransferFunds( 10000000000, // 10 GNUS
                                                          "0x00",
                                                          sgns::TokenID::FromBytes( { 0x00 } ) );

    ASSERT_TRUE( transfer1_res.has_value() ) << "Transfer 1 failed on node_same_addr_1";
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
                                  "node_same_addr_2 balance not synced" );

    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

    // Get final balances after CRDT resolution
    auto balance_node1_final = node_same_addr_1->GetBalance();
    auto balance_node2_final = node_same_addr_2->GetBalance();
    auto balance_full_final  = node_full->GetBalance( node_same_addr_1->GetAddress() );

    fmt::println( "Final balances after CRDT resolution - Node1: {}, Node2: {}, Full: {}",
                  balance_node1_final,
                  balance_node2_final,
                  balance_full_final );

    // Get final transaction counts
    auto tx_count_node1_final = node_same_addr_1->GetOutTransactions().size();
    auto tx_count_node2_final = node_same_addr_2->GetOutTransactions().size();

    fmt::println( "Final tx counts - Node1: {}, Node2: {}", tx_count_node1_final, tx_count_node2_final );

    // Since both nodes have the same address, they should have the same final balance
    ASSERT_EQ( balance_node1_final, balance_node2_final )
        << "Nodes with same address should have same balance after CRDT resolution";
    ASSERT_EQ( balance_full_final, balance_node2_final )
        << "Nodes with same address should have same balance after CRDT resolution";

    std::cout << "CRDT Filter test completed successfully!" << std::endl;
}
