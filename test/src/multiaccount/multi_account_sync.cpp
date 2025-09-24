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

class MultiAccountTest : public ::testing::Test
{
protected:
    std::shared_ptr<sgns::GeniusNode> CreateNode( const std::string &self_address,
                                                  const std::string &dev_addr,
                                                  const std::string &tokenValue,
                                                  sgns::TokenID      tokenId,
                                                  bool               isFullNode  = false,
                                                  bool               isProcessor = false )
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

        uint16_t uniquePort = static_cast<uint16_t>( 40001 + id );
        auto     node = sgns::GeniusNode::New( devConfig, key.c_str(), false, isProcessor, uniquePort, isFullNode );

        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        return node;
    }

    void SetUp() override
    {
        // Clean up any previous test runs
        std::string binaryPath = boost::dll::program_location().parent_path().string();
        std::filesystem::remove_all( binaryPath + "/node_multi_account_0/" );
        std::filesystem::remove_all( binaryPath + "/node_multi_account_1/" );
        std::filesystem::remove_all( binaryPath + "/node_multi_account_2/" );
    }

    void TearDown() override
    {
        // Cleanup is automatic when shared_ptrs go out of scope
    }
};

TEST_F( MultiAccountTest, SyncThroughEachOther )
{
    // Create nodes dynamically
    auto node_main = CreateNode( "node_multi_1",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 false, // not full node
                                 false  // not processor
    );

    auto node_proc1 = CreateNode( "node_multi_1",
                                  "0xcafe",
                                  "1.0",
                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                  false, // not full node
                                  true   // is processor
    );

    auto node_full = CreateNode( "node_multi_full",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true, // is full node
                                 true  // is processor
    );

    // Connect nodes to each other
    std::vector bootstrappers = { node_proc1->GetPubSub()->GetLocalAddress(),
                                  node_full->GetPubSub()->GetLocalAddress() };
    node_main->GetPubSub()->AddPeers( bootstrappers );

    bootstrappers = { node_proc1->GetPubSub()->GetLocalAddress(), node_main->GetPubSub()->GetLocalAddress() };
    node_full->GetPubSub()->AddPeers( bootstrappers );

    // Allow time for connections to establish
    std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );

    // Get initial state
    auto transcount_main_start  = node_main->GetOutTransactions().size();
    auto transcount_node1_start = node_proc1->GetOutTransactions().size();
    auto main_balance_start     = node_main->GetBalance();
    auto node1_balance_start    = node_proc1->GetBalance();

    // Mint tokens on each node
    auto mint_result = node_main->MintTokens( 50000000000,
                                              "",
                                              "",
                                              sgns::TokenID::FromBytes( { 0x00 } ),
                                              std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_main";

    mint_result = node_proc1->MintTokens( 50000000000,
                                          "",
                                          "",
                                          sgns::TokenID::FromBytes( { 0x00 } ),
                                          std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_proc1";

    // Allow time for synchronization
    std::this_thread::sleep_for( std::chrono::milliseconds( 3000 ) );

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

TEST_F( MultiAccountTest, CRDTFilterDuplicateMint )
{
    // Create 3 nodes - 2 with the same address, 1 different (full node for network)
    auto node_same_addr_1 = CreateNode( "duplicate_address_12345", // same self_address
                                        "0xcafe",                  // dev_addr
                                        "1.0",
                                        sgns::TokenID::FromBytes( { 0x00 } ),
                                        true, // full node to confirm the mint
                                        false // not processor
    );

    auto node_same_addr_2 = CreateNode( "duplicate_address_12345", // same self_address
                                        "0xcafe",                  // dev_addr
                                        "1.0",
                                        sgns::TokenID::FromBytes( { 0x00 } ),
                                        true, // full node to confirm the mint
                                        true  // is processor
    );

    auto node_full = CreateNode( "full_node_address_unique", // different self_address
                                 "0xcafe",                   // dev_addr
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true, // is full node
                                 true  // is processor
    );

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

    auto mint_result_2 = node_same_addr_2->MintTokens( 75000000000, // 75 GNUS (different amount)
                                                       "",
                                                       "",
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result_2.has_value() ) << "Mint transaction failed on node_same_addr_2";

    std::cout << "Mint transaction 1 ID: " << mint_result_1.value().first << std::endl;
    std::cout << "Mint transaction 2 ID: " << mint_result_2.value().first << std::endl;

    // Allow mints to complete locally
    std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );

    // Check balances after minting but before connecting
    auto balance_node1_after_mint = node_same_addr_1->GetBalance();
    auto balance_node2_after_mint = node_same_addr_2->GetBalance();

    std::cout << "Balances after minting (isolated) - Node1: " << balance_node1_after_mint
              << ", Node2: " << balance_node2_after_mint << std::endl;

    // Both nodes should have their respective minted amounts since they're isolated
    ASSERT_EQ( balance_node1_after_mint, balance_node1_start + 50000000000 );
    ASSERT_EQ( balance_node2_after_mint, balance_node2_start + 75000000000 );

    // Now connect the nodes - this should trigger CRDT filter to resolve conflicts
    std::cout << "Connecting nodes to trigger CRDT filter..." << std::endl;

    // Add peers to each node
    node_same_addr_1->GetPubSub()->AddPeers(
        { node_same_addr_2->GetPubSub()->GetLocalAddress(), node_full->GetPubSub()->GetLocalAddress() } );
    node_same_addr_2->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetLocalAddress() } );

    // Allow significant time for CRDT synchronization and conflict resolution
    std::cout << "Waiting for CRDT synchronization and conflict resolution..." << std::endl;
    std::this_thread::sleep_for( std::chrono::milliseconds( 10000 ) );

    auto status_node1 = node_same_addr_2->WaitForTransactionIncoming( mint_result_1.value().first,
                                                                      std::chrono::milliseconds( 2000 ) );
    auto status_node2 = node_same_addr_2->WaitForTransactionIncoming( mint_result_2.value().first,
                                                                      std::chrono::milliseconds( 2000 ) );

    //ASSERT_EQ( status_node1, TransactionManager::TransactionStatus::CONFIRMED );
    //ASSERT_EQ( status_node2, TransactionManager::TransactionStatus::FAILED );

    // Get final balances after CRDT resolution
    auto balance_node1_final = node_same_addr_1->GetBalance();
    auto balance_node2_final = node_same_addr_2->GetBalance();
    auto balance_full_final  = node_full->GetBalance();

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

    // The final balance should be either 50 GNUS or 75 GNUS (whichever mint was accepted)
    // Based on our filter logic, the earlier timestamp should win
    bool accepted_first_mint  = ( balance_node1_final == balance_node1_start + 50000000000 );
    bool accepted_second_mint = ( balance_node1_final == balance_node1_start + 75000000000 );

    ASSERT_TRUE( accepted_first_mint || accepted_second_mint )
        << "Final balance should match exactly one of the minted amounts. "
        << "Expected: " << ( balance_node1_start + 50000000000 ) << " or " << ( balance_node1_start + 75000000000 )
        << ", Got: " << balance_node1_final;

    // Full node should remain unchanged (different address)
    ASSERT_EQ( balance_full_final, balance_full_start ) << "Full node balance should remain unchanged";

    // Log which mint was accepted
    if ( accepted_first_mint )
    {
        std::cout << "CRDT Filter accepted first mint (50 GNUS) - transaction: " << mint_result_1.value().first
                  << std::endl;
    }
    else
    {
        std::cout << "CRDT Filter accepted second mint (75 GNUS) - transaction: " << mint_result_2.value().first
                  << std::endl;
    }

    std::cout << "CRDT Filter test completed successfully!" << std::endl;
}
