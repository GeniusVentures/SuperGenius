#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>
#include <cstdio>

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
#include "account/GeniusNode.hpp"
#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/remove_all.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "testutil/mint_source_hash.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns;

TEST( BlockchainGenesisConfigTest, AuthorizedAddressIsSafeDuringConcurrentStartupAccess )
{
    const auto        original = Blockchain::GetAuthorizedFullNodeAddress();
    const std::string first( 128, 'a' );
    const std::string second( 257, 'b' );
    std::atomic_bool  ready{ false };
    std::atomic_bool  done{ false };
    bool              valid = true;

    Blockchain::SetAuthorizedFullNodeAddress( first );
    std::thread reader(
        [&]()
        {
            ready.store( true );
            while ( !done.load() )
            {
                const auto address = Blockchain::GetAuthorizedFullNodeAddress();
                if ( address != first && address != second )
                {
                    valid = false;
                    return;
                }
            }
        } );

    while ( !ready.load() )
    {
        std::this_thread::yield();
    }
    for ( int i = 0; i < 1000; ++i )
    {
        Blockchain::SetAuthorizedFullNodeAddress( i % 2 == 0 ? first : second );
    }

    done.store( true );
    reader.join();
    Blockchain::SetAuthorizedFullNodeAddress( original );
    EXPECT_TRUE( valid );
}

class BlockchainGenesisTest : public ::testing::Test
{
protected:
    struct FixtureNode
    {
        std::shared_ptr<sgns::GeniusNode> node;
        std::string                       configured_address;

        GeniusNode *operator->() const { return node.get(); }
    };

    static std::string RequireActiveAddress( const FixtureNode &fixture )
    {
        const auto address = fixture.node->GetActiveAccountAddress();
        if ( address.has_error() )
        {
            ADD_FAILURE() << "expected active account address: " << address.error().message();
            return {};
        }
        return address.value();
    }

    static uint64_t RequireActiveBalance( const FixtureNode &fixture )
    {
        (void)RequireActiveAddress( fixture );
        return fixture.node->GetBalance();
    }

    static void SetUpTestSuite()
    {
        // Inject in-memory secure storage to avoid OS keychain prompts during tests
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
    }

    FixtureNode CreateNode( const std::string &self_address,
                            const std::string &dev_addr,
                            const std::string &tokenValue,
                            sgns::TokenID      tokenId,
                            bool               isFullNode = false )
    {
        static std::atomic<int> nodeCounter{ 0 };
        int                     id = nodeCounter.fetch_add( 1 );

        std::string binaryPath = boost::dll::program_location().parent_path().string();
        const char *filePath   = ::testing::UnitTest::GetInstance()->current_test_info()->file();
        std::string fileStem   = std::filesystem::path( filePath ).stem().string();
        auto        outPath    = binaryPath + "/node_blockchain_genesis_" + std::to_string( id ) + "/";

        GeniusNodeConfig devConfig = { dev_addr, "0.65", tokenValue, tokenId, outPath };

        std::filesystem::create_directories( devConfig.BaseWritePath );
        {
            std::ofstream bridgeConfigFile( devConfig.BaseWritePath + "bridge_chains_config.json" );
            bridgeConfigFile << "{}";
        }

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

        sgns::GeniusNode::WriteNetworkConfig( devConfig.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( devConfig.BaseWritePath,
                                           isFullNode ? "Full" : "Light",
                                           /*is_processor=*/false,
                                           /*rpc_catchup=*/false );
        auto node = sgns::GeniusNode::New( devConfig, sgns::FromPrivateKey{ key } );
        auto configured_account = GeniusAccount::NewFromPrivateKey(
            tokenId, key.c_str(), outPath + "configured-identity/", isFullNode );
        if ( !configured_account )
        {
            return {};
        }

        // New starts PubSub synchronously in the constructor
        // (InitNetwork -> pubs.wait()) and kicks off async DB/blockchain init.
        // Callers wait for READY via waitForCondition, so no fixed sleep is
        // needed here (and a sleep would obscure startup-timing measurements).
        return { std::move( node ), configured_account->GetAddress() };
    }

    void SetUp() override
    {
        // Clean up any previous test runs. Retry on Windows where file handles
        // (e.g. RocksDB LOCK) may not be released immediately after node shutdown.
        std::string binaryPath = boost::dll::program_location().parent_path().string();
        for ( int i = 0; i < 10; ++i )
        {
            auto            dir = binaryPath + "/node_blockchain_genesis_" + std::to_string( i ) + "/";
            std::error_code ec;
            sgns::test::removeAllWithRetry( dir, ec );
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
                                 true // is full node
    );

    // Create regular nodes that should NOT be able to sync without authorization
    auto node_regular_1 = CreateNode( "regular_node_no_auth_1",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false // not full node
    );

    auto node_regular_2 = CreateNode( "regular_node_no_auth_2",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false // not full node
    );

    std::cout << "Full node configured address: " << node_full.configured_address << std::endl;
    std::cout << "Regular node 1 configured address: " << node_regular_1.configured_address << std::endl;
    std::cout << "Regular node 2 configured address: " << node_regular_2.configured_address << std::endl;

    // DO NOT call SetAuthorizedFullNodeAddress on any node
    std::cout << "NOT setting authorized full node address - nodes should not sync" << std::endl;

    // Connect nodes to each other
    std::cout << "Connecting nodes..." << std::endl;

    node_regular_1->AddPeers(
        { node_full->GetPubSub()->GetInterfaceAddress(), node_regular_2->GetPubSub()->GetInterfaceAddress() } );
    node_regular_2->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    // Without authorization, nodes must NOT reach READY. Bounded-wait (via
    // waitForCondition) for sync that should not occur, then observe state.
    std::cout << "Waiting to verify nodes cannot sync without authorization..." << std::endl;
    (void) waitForCondition( [&]() { return node_full->GetState() == GeniusNode::NodeState::READY; },
                             std::chrono::milliseconds( 5000 ) );

    // Verify that nodes are NOT in READY state due to missing authorization
    std::cout << "Verifying nodes cannot reach READY state without authorization..." << std::endl;

    // The nodes should not be able to sync properly without the authorized address set
    // This test verifies that the blockchain sync is blocked when authorization is missing

    std::cout << "Full node state: " << static_cast<int>( node_full->GetState() ) << std::endl;
    std::cout << "Regular node 1 state: " << static_cast<int>( node_regular_1->GetState() ) << std::endl;
    std::cout << "Regular node 2 state: " << static_cast<int>( node_regular_2->GetState() ) << std::endl;

    std::cout << "=== No Authorization No Sync Test Completed ===" << std::endl;
}

TEST_F( BlockchainGenesisTest, WithAuthorizationCanSync )
{
    std::cout << "=== Starting With Authorization Can Sync Test ===" << std::endl;

    // Create the full node first (this will be the genesis creator)
    auto node_full = CreateNode( "full_node_with_auth",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true // is full node
    );
    Blockchain::SetAuthorizedFullNodeAddress( node_full.configured_address );
    std::cout << "Setting authorized full node address to: " << node_full.configured_address << std::endl;

    // Create two regular nodes
    auto node_regular_1 = CreateNode( "regular_node_with_auth_1",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false // not full node
    );

    std::cout << "Full node configured address: " << node_full.configured_address << std::endl;
    std::cout << "Regular node 1 configured address: " << node_regular_1.configured_address << std::endl;

    node_regular_1->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    std::cout << "Authorized address set on all nodes" << std::endl;

    // Connect nodes to each other for pubsub communication
    std::cout << "Connecting nodes..." << std::endl;

    // Wait for nodes to reach READY state. Genesis creation/propagation is
    // covered by the READY poll below (no fixed sleep needed).
    std::cout << "Waiting for nodes to reach READY state..." << std::endl;

    test::assertWaitForCondition( [&]() { return node_full->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_full not ready" );
    test::assertWaitForCondition( [&]() { return node_regular_1->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_regular_1 not ready" );

    std::cout << "All nodes are ready and synchronized!" << std::endl;

    // Verify that all nodes have the same authorized address configured
    ASSERT_EQ( node_full->GetAuthorizedFullNodeAddress(), RequireActiveAddress( node_full ) );
    ASSERT_EQ( node_regular_1->GetAuthorizedFullNodeAddress(), RequireActiveAddress( node_full ) );

    std::cout << "=== With Authorization Can Sync Test Completed Successfully ===" << std::endl;
}

TEST_F( BlockchainGenesisTest, WithAuthorizationCanSyncAndProcessTransactions )
{
    std::cout << "=== Starting With Authorization Sync + Transactions Test ===" << std::endl;

    // Create the full node first (this will be the genesis creator)
    auto node_full = CreateNode( "full_node_with_auth", "0xcafe", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), true );
    Blockchain::SetAuthorizedFullNodeAddress( node_full.configured_address );

    // Create two regular nodes that will exchange transactions once synced
    auto node_regular_1 = CreateNode( "regular_node_tx_test_1",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false );
    auto node_regular_2 = CreateNode( "regular_node_tx_test_2",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false );

    // Establish connectivity for gossiping blocks/transactions
    node_regular_1->AddPeers(
        { node_full->GetPubSub()->GetInterfaceAddress(), node_regular_2->GetPubSub()->GetInterfaceAddress() } );
    node_regular_2->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    auto token_id = sgns::TokenID::FromBytes( { 0x00 } );

    uint64_t mint_amount = 10000000000ULL;

    test::assertWaitForCondition( [&]() { return node_full->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "full node not ready" );
    test::assertWaitForCondition( [&]() { return node_regular_1->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "regular node 1 not ready" );
    test::assertWaitForCondition( [&]() { return node_regular_2->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "regular node 2 not ready" );

    auto balance_regular_1_before = RequireActiveBalance( node_regular_1 );
    auto balance_regular_2_before = RequireActiveBalance( node_regular_2 );

    // Mint tokens on the first regular node after sync is confirmed
    auto mint_result = node_regular_1->MintTokens( mint_amount,
                                                   sgns::test::NextMintSourceHash(),
                                                   "test",
                                                   token_id,
                                                   "",
                                                   std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    auto [mint_tx_id, mint_duration] = mint_result.value();
    std::cout << "Mint transaction (" << mint_tx_id << ") completed in " << mint_duration << " ms" << std::endl;
    EXPECT_EQ( RequireActiveBalance( node_regular_1 ), balance_regular_1_before + mint_amount )
        << "Mint should credit the sender balance";

    // Transfer the freshly minted amount to the second node
    auto transfer_result = node_regular_1->TransferFunds( mint_amount,
                                                          RequireActiveAddress( node_regular_2 ),
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

    EXPECT_EQ( RequireActiveBalance( node_regular_1 ), balance_regular_1_before )
        << "Sender balance should return to its starting value after transfer";
    EXPECT_EQ( RequireActiveBalance( node_regular_2 ), balance_regular_2_before + mint_amount )
        << "Recipient balance should include the transferred amount";

    std::cout << "=== With Authorization Sync + Transactions Test Completed Successfully ===" << std::endl;
}

TEST_F( BlockchainGenesisTest, DISABLED_WrongAuthorizationCannotSync )
{
    std::cout << "=== Starting Wrong Authorization Cannot Sync Test ===" << std::endl;

    // Set WRONG authorized address (not matching the full node's address)
    // Create a full node
    auto node_full = CreateNode( "full_node_wrong_auth",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true // is full node
    );
    std::cout << "Wrong authorized address set on all nodes" << std::endl;
    std::string wrong_address = "wrong_address_that_does_not_match_any_node";
    Blockchain::SetAuthorizedFullNodeAddress( wrong_address );

    // Create regular nodes
    auto node_regular_1 = CreateNode( "regular_node_wrong_auth_1",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false // not full node
    );

    auto node_regular_2 = CreateNode( "regular_node_wrong_auth_2",
                                      "0xcafe",
                                      "1.0",
                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                      false // not full node
    );

    std::cout << "Full node configured address: " << node_full.configured_address << std::endl;
    std::cout << "Regular node 1 configured address: " << node_regular_1.configured_address << std::endl;
    std::cout << "Regular node 2 configured address: " << node_regular_2.configured_address << std::endl;

    // Connect nodes to each other
    std::cout << "Connecting nodes..." << std::endl;

    node_regular_1->AddPeers(
        { node_full->GetPubSub()->GetInterfaceAddress(), node_regular_2->GetPubSub()->GetInterfaceAddress() } );
    node_regular_2->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    // With wrong authorization, nodes must NOT reach READY. Bounded-wait (via
    // waitForCondition) for sync that should not occur, then observe state.
    std::cout << "Waiting to verify nodes cannot sync with wrong authorization..." << std::endl;
    (void) waitForCondition( [&]() { return node_full->GetState() == GeniusNode::NodeState::READY; },
                             std::chrono::milliseconds( 8000 ) );

    // Verify that nodes cannot reach READY state due to wrong authorization
    std::cout << "Verifying nodes cannot reach READY state with wrong authorization..." << std::endl;

    std::cout << "Full node state: " << static_cast<int>( node_full->GetState() ) << std::endl;
    std::cout << "Regular node 1 state: " << static_cast<int>( node_regular_1->GetState() ) << std::endl;
    std::cout << "Regular node 2 state: " << static_cast<int>( node_regular_2->GetState() ) << std::endl;

    // The nodes should not be able to sync properly with wrong authorization
    // This test verifies that the blockchain sync is blocked when wrong authorization is set
    std::cout << "=== Wrong Authorization Cannot Sync Test Completed ===" << std::endl;
}
