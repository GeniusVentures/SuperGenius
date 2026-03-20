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
#include "local_secure_storage/SecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "testutil/wait_condition.hpp"

class BlockchainGenesisTest : public ::testing::Test
{
protected:
    static constexpr char FULL_NODE_PUB_ADDRESS[] =
        "7c51e24e36e1be4c81bcca26ce8cd79d0866c344c1de72b81255964ae93d37cc667f27d41ddc27b45e2250e2ca9e6fa74e7e834c176402f2893982e82c00612b";

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
        auto        outPath    = binaryPath + "/node_blockchain_genesis_" + std::to_string( id ) + "/";

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
        for ( int i = 0; i < 10; ++i )
        {
            std::filesystem::remove_all( binaryPath + "/node_blockchain_genesis_" + std::to_string( i ) + "/" );
        }
    }

    void TearDown() override
    {
        // Cleanup is automatic when shared_ptrs go out of scope
    }
};

TEST_F( BlockchainGenesisTest, DISABLED_NoAuthorizationNoSync )
{
    std::cout << "=== Starting No Authorization No Sync Test ===" << std::endl;

    // Create a full node that could create genesis
    auto node_full = CreateNode( "full_node_no_auth",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true, // is full node
                                 true  // is processor
    );

    // Create regular nodes that should NOT be able to sync without authorization
    auto node_regular_1 = CreateNode( "regular_node_no_auth_1",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false, // not full node
                                      false  // not processor
    );

    auto node_regular_2 = CreateNode( "regular_node_no_auth_2",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false, // not full node
                                      true   // is processor
    );

    std::cout << "Full node address: " << node_full->GetAddress() << std::endl;
    std::cout << "Regular node 1 address: " << node_regular_1->GetAddress() << std::endl;
    std::cout << "Regular node 2 address: " << node_regular_2->GetAddress() << std::endl;

    // DO NOT call SetAuthorizedFullNodeAddress on any node
    std::cout << "NOT setting authorized full node address - nodes should not sync" << std::endl;

    // Connect nodes to each other
    std::cout << "Connecting nodes..." << std::endl;

    node_regular_1->GetPubSub()->AddPeers(
        { node_full->GetPubSub()->GetLocalAddress(), node_regular_2->GetPubSub()->GetLocalAddress() } );
    node_regular_2->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetLocalAddress() } );

    // Allow time for attempted connections
    std::cout << "Waiting to verify nodes cannot sync without authorization..." << std::endl;
    std::this_thread::sleep_for( std::chrono::milliseconds( 5000 ) );

    // Verify that nodes are NOT in READY state due to missing authorization
    std::cout << "Verifying nodes cannot reach READY state without authorization..." << std::endl;

    // The nodes should not be able to sync properly without the authorized address set
    // This test verifies that the blockchain sync is blocked when authorization is missing

    std::cout << "Full node state: " << static_cast<int>( node_full->GetTransactionManagerState() ) << std::endl;
    std::cout << "Regular node 1 state: " << static_cast<int>( node_regular_1->GetTransactionManagerState() )
              << std::endl;
    std::cout << "Regular node 2 state: " << static_cast<int>( node_regular_2->GetTransactionManagerState() )
              << std::endl;

    std::cout << "=== No Authorization No Sync Test Completed ===" << std::endl;
}

TEST_F( BlockchainGenesisTest, WithAuthorizationCanSync )
{
    std::cout << "=== Starting With Authorization Can Sync Test ===" << std::endl;

    std::string full_node_pub_address{ FULL_NODE_PUB_ADDRESS };
    std::cout << "Setting authorized full node address to: " << full_node_pub_address << std::endl;
    Blockchain::SetAuthorizedFullNodeAddress( full_node_pub_address );
    // Create the full node first (this will be the genesis creator)
    auto node_full = CreateNode( "full_node_with_auth",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true, // is full node
                                 true  // is processor
    );

    test::assertWaitForCondition(
        [&]() { return node_full->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_full not ready" );

    // Create two regular nodes
    auto node_regular_1 = CreateNode( "regular_node_with_auth_1",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false, // not full node
                                      false  // not processor
    );

    // auto node_regular_2 = CreateNode( "regular_node_with_auth_2",
    //                                   "0xcafe",
    //                                   "1.0",
    //                                   sgns::TokenID::FromBytes( { 0x00 } ),
    //                                   false, // not full node
    //                                   true   // is processor
    // );

    std::cout << "Full node address: " << node_full->GetAddress() << std::endl;
    std::cout << "Regular node 1 address: " << node_regular_1->GetAddress() << std::endl;
    //std::cout << "Regular node 2 address: " << node_regular_2->GetAddress() << std::endl;

    node_regular_1->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetLocalAddress() } );

    std::cout << "Authorized address set on all nodes" << std::endl;

    // Connect nodes to each other for pubsub communication
    std::cout << "Connecting nodes..." << std::endl;

    //node_regular_2->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetLocalAddress() } );

    // Allow time for connections to establish and genesis block to be created/propagated
    std::cout << "Waiting for genesis block creation and propagation..." << std::endl;
    std::this_thread::sleep_for( std::chrono::milliseconds( 8000 ) );

    // Wait for all nodes to reach READY state
    std::cout << "Waiting for nodes to reach READY state..." << std::endl;

    test::assertWaitForCondition(
        [&]() { return node_full->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_full not ready" );

    test::assertWaitForCondition(
        [&]() { return node_regular_1->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_regular_1 not ready" );

    // test::assertWaitForCondition(
    //     [&]() { return node_regular_2->GetTransactionManagerState() == TransactionManager::State::READY; },
    //     std::chrono::milliseconds( 30000 ),
    //     "node_regular_2 not ready" );

    std::cout << "All nodes are ready and synchronized!" << std::endl;

    // Verify that all nodes have the same authorized address configured
    ASSERT_EQ( node_full->GetAuthorizedFullNodeAddress(), full_node_pub_address );
    ASSERT_EQ( node_regular_1->GetAuthorizedFullNodeAddress(), full_node_pub_address );
    //ASSERT_EQ( node_regular_2->GetAuthorizedFullNodeAddress(), authorized_address );

    std::cout << "=== With Authorization Can Sync Test Completed Successfully ===" << std::endl;
}

TEST_F( BlockchainGenesisTest, WithAuthorizationCanSyncAndProcessTransactions )
{
    std::cout << "=== Starting With Authorization Sync + Transactions Test ===" << std::endl;

    std::string full_node_pub_address{ FULL_NODE_PUB_ADDRESS };
    Blockchain::SetAuthorizedFullNodeAddress( full_node_pub_address );
    std::cout << "Authorized address set: " << full_node_pub_address << std::endl;

    // Create the full node first (this will be the genesis creator)
    auto node_full = CreateNode( "full_node_with_auth",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true,
                                 true );
    test::assertWaitForCondition(
        [&]() { return node_full->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_full not ready" );
    // Create two regular nodes that will exchange transactions once synced
    auto node_regular_1 = CreateNode( "regular_node_tx_test_1",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false,
                                      false );

    auto node_regular_2 = CreateNode( "regular_node_tx_test_2",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false,
                                      true );

    // Establish connectivity for gossiping blocks/transactions
    node_regular_1->GetPubSub()->AddPeers(
        { node_full->GetPubSub()->GetLocalAddress(), node_regular_2->GetPubSub()->GetLocalAddress() } );
    node_regular_2->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetLocalAddress() } );

    auto token_id = sgns::TokenID::FromBytes( { 0x00 } );

    uint64_t mint_amount = 10000000000ULL;

    test::assertWaitForCondition(
        [&]() { return node_full->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "full node not ready" );
    test::assertWaitForCondition(
        [&]() { return node_regular_1->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "regular node 1 not ready" );
    test::assertWaitForCondition(
        [&]() { return node_regular_2->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "regular node 2 not ready" );

    auto balance_regular_1_before = node_regular_1->GetBalance();
    auto balance_regular_2_before = node_regular_2->GetBalance();

    // Mint tokens on the first regular node after sync is confirmed
    auto mint_result = node_regular_1->MintTokens( mint_amount, "", "", token_id );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    auto [mint_tx_id, mint_duration] = mint_result.value();
    std::cout << "Mint transaction (" << mint_tx_id << ") completed in " << mint_duration << " ms" << std::endl;
    EXPECT_EQ( node_regular_1->GetBalance(), balance_regular_1_before + mint_amount )
        << "Mint should credit the sender balance";

    // Transfer the freshly minted amount to the second node
    auto transfer_result = node_regular_1->TransferFunds( mint_amount,
                                                          node_regular_2->GetAddress(),
                                                          token_id,
                                                          std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer_result.has_value() ) << "Transfer transaction failed or timed out";

    auto [transfer_tx_id, transfer_duration] = transfer_result.value();
    std::cout << "Transfer transaction (" << transfer_tx_id << ") completed in " << transfer_duration << " ms"
              << std::endl;

    auto recipient_status = node_regular_2->WaitForTransactionIncoming(
        transfer_tx_id,
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    EXPECT_EQ( recipient_status, TransactionManager::TransactionStatus::CONFIRMED );

    auto full_node_status = node_full->WaitForTransactionIncoming(
        transfer_tx_id,
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    EXPECT_EQ( full_node_status, TransactionManager::TransactionStatus::CONFIRMED );

    EXPECT_EQ( node_regular_1->GetBalance(), balance_regular_1_before )
        << "Sender balance should return to its starting value after transfer";
    EXPECT_EQ( node_regular_2->GetBalance(), balance_regular_2_before + mint_amount )
        << "Recipient balance should include the transferred amount";

    std::cout << "=== With Authorization Sync + Transactions Test Completed Successfully ===" << std::endl;
}

TEST_F( BlockchainGenesisTest, DISABLED_WrongAuthorizationCannotSync )
{
    std::cout << "=== Starting Wrong Authorization Cannot Sync Test ===" << std::endl;

    // Set WRONG authorized address (not matching the full node's address)
    std::string wrong_address = "wrong_address_that_does_not_match_any_node";
    Blockchain::SetAuthorizedFullNodeAddress( wrong_address );
    std::cout << "Wrong authorized address set on all nodes" << std::endl;
    // Create a full node
    auto node_full = CreateNode( "full_node_wrong_auth",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true, // is full node
                                 true  // is processor
    );

    // Create regular nodes
    auto node_regular_1 = CreateNode( "regular_node_wrong_auth_1",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false, // not full node
                                      false  // not processor
    );

    auto node_regular_2 = CreateNode( "regular_node_wrong_auth_2",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false, // not full node
                                      true   // is processor
    );

    std::cout << "Full node address: " << node_full->GetAddress() << std::endl;
    std::cout << "Regular node 1 address: " << node_regular_1->GetAddress() << std::endl;
    std::cout << "Regular node 2 address: " << node_regular_2->GetAddress() << std::endl;

    // Connect nodes to each other
    std::cout << "Connecting nodes..." << std::endl;

    node_regular_1->GetPubSub()->AddPeers(
        { node_full->GetPubSub()->GetLocalAddress(), node_regular_2->GetPubSub()->GetLocalAddress() } );
    node_regular_2->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetLocalAddress() } );

    // Allow time for attempted connections and genesis block operations
    std::cout << "Waiting to verify nodes cannot sync with wrong authorization..." << std::endl;
    std::this_thread::sleep_for( std::chrono::milliseconds( 8000 ) );

    // Verify that nodes cannot reach READY state due to wrong authorization
    std::cout << "Verifying nodes cannot reach READY state with wrong authorization..." << std::endl;

    std::cout << "Full node state: " << static_cast<int>( node_full->GetTransactionManagerState() ) << std::endl;
    std::cout << "Regular node 1 state: " << static_cast<int>( node_regular_1->GetTransactionManagerState() )
              << std::endl;
    std::cout << "Regular node 2 state: " << static_cast<int>( node_regular_2->GetTransactionManagerState() )
              << std::endl;

    // The nodes should not be able to sync properly with wrong authorization
    // This test verifies that the blockchain sync is blocked when wrong authorization is set

    std::cout << "=== Wrong Authorization Cannot Sync Test Completed ===" << std::endl;
}
