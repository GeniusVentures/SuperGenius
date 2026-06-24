#include <gtest/gtest.h>

#include <cmath>
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
#include <tuple>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "testutil/mint_source_hash.hpp"
#include "testutil/wait_condition.hpp"
#include "blockchain/ValidatorRegistry.hpp"

using namespace sgns;

namespace sgns
{
    class MultiAccountTestAccess
    {
    public:
        static std::shared_ptr<Blockchain> GetBlockchain( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->blockchain_ : nullptr;
        }

        static std::shared_ptr<ConsensusManager> GetConsensusManager( const std::shared_ptr<Blockchain> &blockchain )
        {
            return blockchain ? blockchain->consensus_manager_ : nullptr;
        }
    };
} // namespace sgns

class MultiAccountTest : public ::testing::Test
{
protected:
    static constexpr std::string_view FILE_PREFIX = "node_multi_account_";

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

        // is_processor is now read from sgns_config.json; the parameter is retained
        // for source compatibility with existing test call sites.
        (void)isProcessor;

        auto binaryPath = boost::dll::program_location().parent_path();
        auto outPath    = binaryPath / ( std::string( FILE_PREFIX ) + std::to_string( id ) );
        auto outPathStr = outPath.generic_string() + '/';

        DevConfig_st devConfig = { dev_addr, "0.65", tokenValue, tokenId, outPathStr };

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
        auto     node = sgns::GeniusNode::NewFromPrivateKey( devConfig, key.c_str(), false, uniquePort, isFullNode );
        if ( isGenesisAuthorized )
        {
            sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        return node;
    }

    void SetUp() override
    {
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

        auto binaryPath = boost::dll::program_location().parent_path();

        // Clean up any previous test runs
        for ( auto &entry : boost::filesystem::directory_iterator( binaryPath ) )
        {
            if ( entry.is_directory() && entry.path().filename().string().find( FILE_PREFIX ) != std::string::npos )
            {
                removeWithRetry( entry.path().string() );
            }
        }
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
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true, // is full node
                                 true, // is processor
                                 true );
    sgns::test::assertWaitForCondition( [&]() { return node_full->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 30000 ),
                                  "node_full not synced" );
    auto node_original = CreateNode( "node_multi_1",
                                     "0xcafe",
                                     "1.0",
                                     sgns::TokenID::FromBytes( { 0x00 } ),
                                     false, // not full node
                                     false  // not processor
    );

    node_original->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    sgns::test::assertWaitForCondition( [&]() { return node_original->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 30000 ),
                                  "node_original not synced" );

    auto balance_original_start = node_original->GetBalance();
    // Mint some tokens
    auto mint_result = node_original->MintTokens( 100,
                                                  sgns::test::NextMintSourceHash(),
                                                  "",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "",
                                                  std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";

    mint_result = node_original->MintTokens( 2000,
                                             sgns::test::NextMintSourceHash(),
                                             "",
                                             sgns::TokenID::FromBytes( { 0x00 } ),
                                             "",
                                             std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";
    mint_result = node_original->MintTokens( 30,
                                             sgns::test::NextMintSourceHash(),
                                             "",
                                             sgns::TokenID::FromBytes( { 0x00 } ),
                                             "",
                                             std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );

    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";

    std::cout << " 3 mint transactions on original node completed, Creating duplicated node..." << std::endl;

    auto node_duplicated = CreateNode( "node_multi_1",
                                       "0xcafe",
                                       "1.0",
                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                       false, // not full node
                                       true   // is processor
    );
    node_duplicated->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    sgns::test::assertWaitForCondition( [&]() { return node_duplicated->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 30000 ),
                                  "node_duplicated not synced" );

    mint_result = node_duplicated->MintTokens( 60000,
                                               sgns::test::NextMintSourceHash(),
                                               "",
                                               sgns::TokenID::FromBytes( { 0x00 } ),
                                               "",
                                               std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_duplicated";

    sgns::test::assertWaitForCondition(
        [&] { return ( balance_original_start + 60000 + 2000 + 100 + 30 ) == node_duplicated->GetBalance(); },
        std::chrono::milliseconds( 30000 ),
        "node_duplicated balance not synced" );
    sgns::test::assertWaitForCondition(
        [&] { return ( balance_original_start + 60000 + 2000 + 100 + 30 ) == node_original->GetBalance(); },
        std::chrono::milliseconds( 30000 ),
        "node_duplicated balance not synced" );

    ASSERT_EQ( node_duplicated->GetBalance(), node_original->GetBalance() );
}

TEST_F( MultiAccountTest, DISABLED_CRDTFilterDuplicateTx )
{
    // Create 3 nodes - 2 with the same address, 1 different (full node for network)
    auto node_full = CreateNode( "full_node_address_unique", // different self_address
                                 "0xcafe",                   // dev_addr
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true, // is full node
                                 true, // is processor
                                 true );

    sgns::test::assertWaitForCondition( [&]() { return node_full->GetState() == GeniusNode::NodeState::READY; },
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

    sgns::test::assertWaitForCondition( [&]() { return node_same_addr_1->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 20000 ),
                                  "node_same_addr_1 not synced" );
    sgns::test::assertWaitForCondition( [&]() { return node_same_addr_2->GetState() == GeniusNode::NodeState::READY; },
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
    auto tx_count_node1_start = node_same_addr_1->GetTransactions( TransactionManager::TransactionStatus::CONFIRMED )
                                    .size();
    auto tx_count_node2_start = node_same_addr_2->GetTransactions( TransactionManager::TransactionStatus::CONFIRMED )
                                    .size();
    auto tx_count_full_start = node_full->GetTransactions( TransactionManager::TransactionStatus::CONFIRMED ).size();

    fmt::println( "Initial tx counts - Node1: {}, Node2: {}, Full: {}",
                  tx_count_node1_start,
                  tx_count_node2_start,
                  tx_count_full_start );

    // Mint tokens on both nodes with same address BEFORE connecting them
    std::cout << "Minting tokens on isolated nodes..." << std::endl;

    auto mint_result_1 = node_same_addr_1->MintTokens( 50000000000, // 50 GNUS
                                                       sgns::test::NextMintSourceHash(),
                                                       "",
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       "",
                                                       std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result_1.has_value() ) << "Mint transaction failed on node_same_addr_1";

    std::cout << "Mint transaction 1 ID: " << mint_result_1.value().first << std::endl;

    sgns::test::assertWaitForCondition( [&]() { return node_same_addr_2->GetBalance() == balance_node2_start + 50000000000; },
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

    fmt::println( "Waiting for the conflict resolution" );

    uint64_t correct_tokens_transferred = 0;
    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto status1 = node_same_addr_1->GetTransactionStatus( transfer1_res.value() );
            if ( status1 == TransactionManager::TransactionStatus::CONFIRMED )
            {
                correct_tokens_transferred = 10000000000;
                return true;
            }

            auto status2 = node_same_addr_2->GetTransactionStatus( transfer2_res.value() );
            if ( status2 == TransactionManager::TransactionStatus::CONFIRMED )
            {
                correct_tokens_transferred = 13000000000;
                return true;
            }

            return false;
        },
        std::chrono::milliseconds( 50000 ),
        "Neither transfer was confirmed" );

    sgns::test::assertWaitForCondition(
        [&]() { return node_same_addr_1->GetBalance() == ( balance_node1_after_mint - correct_tokens_transferred ); },
        std::chrono::milliseconds( 50000 ),
        "node_same_addr_1 balance not synced" );
    sgns::test::assertWaitForCondition( [&]() { return node_same_addr_2->GetBalance() == node_same_addr_1->GetBalance(); },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_same_addr_2 balance not synced" );

    fmt::println( "Balances after bootstrap - Node1: {}, Node2: {}",
                  node_same_addr_2->GetBalance(),
                  node_same_addr_1->GetBalance() );

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
    auto tx_count_node1_final = node_same_addr_1->GetTransactions( TransactionManager::TransactionStatus::CONFIRMED )
                                    .size();
    auto tx_count_node2_final = node_same_addr_2->GetTransactions( TransactionManager::TransactionStatus::CONFIRMED )
                                    .size();

    fmt::println( "Final tx counts - Node1: {}, Node2: {}", tx_count_node1_final, tx_count_node2_final );

    // Since both nodes have the same address, they should have the same final balance
    ASSERT_EQ( balance_node1_final, balance_node2_final )
        << "Nodes with same address should have same balance after CRDT resolution";
    ASSERT_EQ( balance_full_final, balance_node2_final )
        << "Nodes with same address should have same balance after CRDT resolution";

    std::cout << "CRDT Filter test completed successfully!" << std::endl;
}

TEST_F( MultiAccountTest, NodeConsensusTest )
{
    constexpr size_t kCertificatesPerBatch = 1;
    const auto       kCertificateDelay     = std::chrono::seconds( 7 );

    auto configure_consensus_batch_and_delay = [&]( const std::shared_ptr<sgns::GeniusNode> &node )
    {
       sgns::test::assertWaitForCondition(
            [&]()
            {
                auto blockchain = sgns::MultiAccountTestAccess::GetBlockchain( node );
                return node && blockchain && sgns::MultiAccountTestAccess::GetConsensusManager( blockchain ) &&
                       node->GetTransactionManagerState() == TransactionManager::State::READY;
            },
            std::chrono::milliseconds( 30000 ),
            "node blockchain not ready for consensus configuration" );

        auto blockchain    = sgns::MultiAccountTestAccess::GetBlockchain( node );
        auto node_registry = blockchain ? blockchain->GetValidatorRegistry() : nullptr;
        ASSERT_TRUE( node_registry );

        node_registry->SetCertificatesPerBatch( kCertificatesPerBatch );
        auto consensus_manager = sgns::MultiAccountTestAccess::GetConsensusManager( blockchain );
        ASSERT_TRUE( consensus_manager );
        consensus_manager->ConfigureCertificateDelay( kCertificateDelay );
    };

    auto node_full = CreateNode( "node_consensus_full",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true,   // is full node
                                 true,   // is processor
                                 true ); // is genesis authorized

    sgns::test::assertWaitForCondition(
        [&]() { return node_full->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_full not synced" );

    auto node_client = CreateNode( "node_consensus_client",
                                   "0xcafe",
                                   "1.0",
                                   sgns::TokenID::FromBytes( { 0x00 } ),
                                   false, // not full node
                                   false  // not processor
    );

    auto node_peer1 = CreateNode( "node_consensus_peer1",
                                  "0xcafe",
                                  "1.0",
                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                  false,
                                  false );
    auto node_peer2 = CreateNode( "node_consensus_peer2",
                                  "0xcafe",
                                  "1.0",
                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                  false,
                                  false );
    auto node_peer3 = CreateNode( "node_consensus_peer3",
                                  "0xcafe",
                                  "1.0",
                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                  false,
                                  false );

    node_client->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    node_peer1->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    node_peer2->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    node_peer3->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    sgns::test::assertWaitForCondition(
        [&]() { return node_client->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_client not synced" );
    sgns::test::assertWaitForCondition(
        [&]() { return node_peer1->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_peer1 not synced" );
    sgns::test::assertWaitForCondition(
        [&]() { return node_peer2->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_peer2 not synced" );
    sgns::test::assertWaitForCondition(
        [&]() { return node_peer3->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_peer3 not synced" );

    configure_consensus_batch_and_delay( node_full );
    configure_consensus_batch_and_delay( node_client );
    configure_consensus_batch_and_delay( node_peer1 );
    configure_consensus_batch_and_delay( node_peer2 );
    configure_consensus_batch_and_delay( node_peer3 );

    auto full_blockchain = sgns::MultiAccountTestAccess::GetBlockchain( node_full );
    ASSERT_TRUE( full_blockchain );
    auto registry = full_blockchain->GetValidatorRegistry();
    ASSERT_TRUE( registry );

    fmt::println( "Nodes created. Registry loaded" );
    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadRegistry();
            return load.has_value() && !registry->GetRegistryCid().empty();
        },
        std::chrono::milliseconds( 30000 ),
        "validator registry not initialized" );

    fmt::println( "Registry CID: {}", registry->GetRegistryCid() );
    auto assert_registry_updated = [&]( uint64_t epoch_before, const std::string &cid_before )
    {
        sgns::test::assertWaitForCondition(
            [&]()
            {
                auto load = registry->LoadRegistry();
                return load.has_value() &&
                       ( load.value().epoch() > epoch_before || registry->GetRegistryCid() != cid_before );
            },
            std::chrono::milliseconds( 30000 ),
            "validator registry did not update" );

        auto registry_after = registry->LoadRegistry();
        ASSERT_TRUE( registry_after.has_value() );
        EXPECT_GT( registry_after.value().epoch(), epoch_before );
        EXPECT_NE( registry->GetRegistryCid(), cid_before );

        if ( registry_after.value().validators().size() > 0 )
        {
            auto *full_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                           node_full->GetAddress() );
            ASSERT_TRUE( full_validator );
            EXPECT_GT( full_validator->weight(), 0 );
        }
        if ( registry_after.value().validators().size() > 1 )
        {
            auto *client_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                             node_client->GetAddress() );
            ASSERT_TRUE( client_validator );
            EXPECT_GT( client_validator->weight(), 0 );
        }
        if ( registry_after.value().validators().size() > 2 )
        {
            auto *peer1_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                            node_peer1->GetAddress() );
            ASSERT_TRUE( peer1_validator );
            EXPECT_GT( peer1_validator->weight(), 0 );
        }
        if ( registry_after.value().validators().size() > 3 )
        {
            auto *peer2_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                            node_peer2->GetAddress() );
            ASSERT_TRUE( peer2_validator );
            EXPECT_GT( peer2_validator->weight(), 0 );
        }
        if ( registry_after.value().validators().size() > 4 )
        {
            auto *peer3_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                            node_peer3->GetAddress() );
            ASSERT_TRUE( peer3_validator );

            EXPECT_GT( peer3_validator->weight(), 0 );
        }
    };

    auto wait_client_registry_caught_up = [&]()
    {
        auto client_blockchain = sgns::MultiAccountTestAccess::GetBlockchain( node_client );
        ASSERT_TRUE( client_blockchain );
        auto client_registry = client_blockchain->GetValidatorRegistry();
        ASSERT_TRUE( client_registry );

        sgns::test::assertWaitForCondition(
            [&]()
            {
                auto full_load   = registry->LoadRegistry();
                auto client_load = client_registry->LoadRegistry();
                return full_load.has_value() && client_load.has_value() &&
                       client_registry->GetRegistryCid() == registry->GetRegistryCid() &&
                       client_load.value().epoch() >= full_load.value().epoch();
            },
            std::chrono::milliseconds( 30000 ),
            "node_client validator registry not caught up" );
    };

    auto load_registry_state = [&]() -> std::pair<uint64_t, std::string>
    {
        auto state = registry->LoadRegistry();
        EXPECT_TRUE( state.has_value() );
        if ( !state.has_value() )
        {
            return { 0, "" };
        }
        return { state.value().epoch(), registry->GetRegistryCid() };
    };

    auto [epoch_before, cid_before] = load_registry_state();

    auto mint1 = node_client->MintTokens( 100, sgns::test::NextMintSourceHash(), "", TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( mint1.has_value() ) << "Mint 1 failed on node_client";
    fmt::println( "Mint 1 succeeded" );

    assert_registry_updated( epoch_before, cid_before );
    wait_client_registry_caught_up();

    std::tie( epoch_before, cid_before ) = load_registry_state();

    auto mint2 = node_client->MintTokens( 250, sgns::test::NextMintSourceHash(), "", TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( mint2.has_value() ) << "Mint 2 failed on node_client";
    fmt::println( "Mint 2 succeeded" );
    assert_registry_updated( epoch_before, cid_before );
    wait_client_registry_caught_up();
    std::tie( epoch_before, cid_before ) = load_registry_state();

    auto transfer1 = node_client->TransferFunds( 75,
                                                 node_peer1->GetAddress(),
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer1.has_value() ) << "Transfer 1 failed on node_client";
    fmt::println( "Transfer 1 succeeded" );
    assert_registry_updated( epoch_before, cid_before );
    wait_client_registry_caught_up();
    std::tie( epoch_before, cid_before ) = load_registry_state();

    auto transfer2 = node_client->TransferFunds( 40,
                                                 node_peer2->GetAddress(),
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer2.has_value() ) << "Transfer 2 failed on node_client";
    fmt::println( "Transfer 2 succeeded" );
    assert_registry_updated( epoch_before, cid_before );
    wait_client_registry_caught_up();
    std::tie( epoch_before, cid_before ) = load_registry_state();

    auto transfer3 = node_client->TransferFunds( 10,
                                                 node_peer3->GetAddress(),
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer3.has_value() ) << "Transfer 3 failed on node_client";

    fmt::println( "Transfer 3 succeeded" );
    assert_registry_updated( epoch_before, cid_before );
}

TEST_F( MultiAccountTest, NodeConsensusBatch5Test )
{
    constexpr size_t kCertificatesPerBatch = 5;
    const auto       kCertificateDelay     = std::chrono::seconds( 7 );

    auto node_full = CreateNode( "node_consensus_batch5_full",
                                 "0xcafe",
                                 "1.0",
                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                 true,   // is full node
                                 true,   // is processor
                                 true ); // is genesis authorized

    sgns::test::assertWaitForCondition(
        [&]() { return node_full->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_full not synced" );

    auto node_client = CreateNode( "node_consensus_batch5_client",
                                   "0xcafe",
                                   "1.0",
                                   sgns::TokenID::FromBytes( { 0x00 } ),
                                   false, // not full node
                                   false  // not processor
    );

    auto node_peer1 = CreateNode( "node_consensus_batch5_peer1",
                                  "0xcafe",
                                  "1.0",
                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                  false,
                                  false );
    auto node_peer2 = CreateNode( "node_consensus_batch5_peer2",
                                  "0xcafe",
                                  "1.0",
                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                  false,
                                  false );
    auto node_peer3 = CreateNode( "node_consensus_batch5_peer3",
                                  "0xcafe",
                                  "1.0",
                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                  false,
                                  false );

    auto configure_consensus_batch_and_delay = [&]( const std::shared_ptr<sgns::GeniusNode> &node )
    {
        sgns::test::assertWaitForCondition(
            [&]()
            {
                auto blockchain = sgns::MultiAccountTestAccess::GetBlockchain( node );
                return node && blockchain && sgns::MultiAccountTestAccess::GetConsensusManager( blockchain ) &&
                       node->GetTransactionManagerState() == TransactionManager::State::READY;
            },
            std::chrono::milliseconds( 30000 ),
            "node blockchain not ready for consensus configuration" );

        auto blockchain    = sgns::MultiAccountTestAccess::GetBlockchain( node );
        auto node_registry = blockchain ? blockchain->GetValidatorRegistry() : nullptr;
        ASSERT_TRUE( node_registry );

        node_registry->SetCertificatesPerBatch( kCertificatesPerBatch );
        auto consensus_manager = sgns::MultiAccountTestAccess::GetConsensusManager( blockchain );
        ASSERT_TRUE( consensus_manager );
        consensus_manager->ConfigureCertificateDelay( kCertificateDelay );
    };

    node_client->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    node_peer1->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    node_peer2->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    node_peer3->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    sgns::test::assertWaitForCondition(
        [&]() { return node_client->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_client not synced" );
    sgns::test::assertWaitForCondition(
        [&]() { return node_peer1->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_peer1 not synced" );
    sgns::test::assertWaitForCondition(
        [&]() { return node_peer2->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_peer2 not synced" );
    sgns::test::assertWaitForCondition(
        [&]() { return node_peer3->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "node_peer3 not synced" );

    configure_consensus_batch_and_delay( node_full );
    configure_consensus_batch_and_delay( node_client );
    configure_consensus_batch_and_delay( node_peer1 );
    configure_consensus_batch_and_delay( node_peer2 );
    configure_consensus_batch_and_delay( node_peer3 );

    auto full_blockchain = sgns::MultiAccountTestAccess::GetBlockchain( node_full );
    ASSERT_TRUE( full_blockchain );
    auto registry = full_blockchain->GetValidatorRegistry();
    ASSERT_TRUE( registry );

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadRegistry();
            return load.has_value() && !registry->GetRegistryCid().empty();
        },
        std::chrono::milliseconds( 30000 ),
        "validator registry not initialized" );

    auto registry_state = registry->LoadRegistry();
    ASSERT_TRUE( registry_state.has_value() );
    const auto initial_epoch = registry_state.value().epoch();
    const auto initial_cid   = registry->GetRegistryCid();

    auto assert_registry_immutable = [&]( const char *step )
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
        while ( std::chrono::steady_clock::now() < deadline )
        {
            auto load = registry->LoadRegistry();
            ASSERT_TRUE( load.has_value() ) << "registry load failed during " << step;
            EXPECT_EQ( load.value().epoch(), initial_epoch ) << "registry epoch changed unexpectedly at " << step;
            EXPECT_EQ( registry->GetRegistryCid(), initial_cid ) << "registry CID changed unexpectedly at " << step;
            std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
        }
    };

    auto mint1 = node_client->MintTokens( 100, sgns::test::NextMintSourceHash(), "", TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( mint1.has_value() ) << "Mint 1 failed on node_client";
    assert_registry_immutable( "tx1" );

    auto mint2 = node_client->MintTokens( 250, sgns::test::NextMintSourceHash(), "", TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( mint2.has_value() ) << "Mint 2 failed on node_client";
    assert_registry_immutable( "tx2" );

    auto transfer1 = node_client->TransferFunds( 75,
                                                 node_peer1->GetAddress(),
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer1.has_value() ) << "Transfer 1 failed on node_client";
    assert_registry_immutable( "tx3" );

    auto transfer2 = node_client->TransferFunds( 40,
                                                 node_peer2->GetAddress(),
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer2.has_value() ) << "Transfer 2 failed on node_client";
    assert_registry_immutable( "tx4" );

    auto transfer3 = node_client->TransferFunds( 10,
                                                 node_peer3->GetAddress(),
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer3.has_value() ) << "Transfer 3 failed on node_client";

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadRegistry();
            return load.has_value() &&
                   ( load.value().epoch() > initial_epoch || registry->GetRegistryCid() != initial_cid );
        },
        std::chrono::milliseconds( 60000 ),
        "validator registry did not update after 5th certificate" );

    auto registry_after = registry->LoadRegistry();
    ASSERT_TRUE( registry_after.has_value() );
    EXPECT_GT( registry_after.value().epoch(), initial_epoch );
    EXPECT_NE( registry->GetRegistryCid(), initial_cid );

    const std::vector<std::string> expected_validators = { node_full->GetAddress(),
                                                           node_client->GetAddress(),
                                                           node_peer1->GetAddress(),
                                                           node_peer2->GetAddress(),
                                                           node_peer3->GetAddress() };
    for ( const auto &validator_id : expected_validators )
    {
        auto *validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(), validator_id );
        ASSERT_TRUE( validator ) << "missing validator in registry: " << validator_id;
        EXPECT_GT( validator->weight(), 0 ) << "validator has non-positive weight: " << validator_id;
    }
}
