#include <chrono>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>
#include <cstdio>
#include <system_error>

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
#include <unordered_map>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "testutil/mint_source_hash.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "storage/rocksdb/rocksdb.hpp"

using namespace sgns;

namespace sgns
{
    class MultiAccountTestAccess
    {
    public:
        static std::shared_ptr<ValidatorRegistry> GetValidatorRegistry( const std::shared_ptr<GeniusNode> &node )
        {
            return node && node->blockchain_ ? node->blockchain_->GetValidatorRegistry() : nullptr;
        }

        static bool ConfigureConsensus( const std::shared_ptr<GeniusNode> &node,
                                        size_t                             certificates_per_batch,
                                        std::chrono::milliseconds          certificate_delay )
        {
            if ( !node || !node->blockchain_ || !node->blockchain_->consensus_manager_ )
            {
                return false;
            }

            auto registry = node->blockchain_->GetValidatorRegistry();
            if ( !registry )
            {
                return false;
            }

            registry->SetCertificatesPerBatch( certificates_per_batch );
            node->blockchain_->consensus_manager_->ConfigureCertificateDelay( certificate_delay );
            return true;
        }

        static inline std::string GetDatabasePath( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->write_base_path_ + node->gnus_network_full_path_ : std::string{};
        }

        static bool RemoveRegistryPersistence( const std::string &database_path,
                                               std::vector<uint8_t> registry_block_key )
        {
            auto datastore_result = storage::rocksdb::create( database_path );
            if ( datastore_result.has_error() )
            {
                return false;
            }
            auto datastore = std::move( datastore_result.value() );

            base::Buffer registry_cid_key;
            registry_cid_key.put( std::string( ValidatorRegistry::RegistryCidKey() ) );
            if ( datastore->remove( registry_cid_key ).has_error() )
            {
                return false;
            }

            const base::Buffer block_key( std::move( registry_block_key ) );
            return datastore->remove( block_key ).has_value() && !datastore->contains( block_key );
        }
    };

} // namespace sgns

class MultiAccountTest : public ::testing::Test
{
protected:
    static constexpr std::string_view FILE_PREFIX = "mat_";

    std::shared_ptr<sgns::GeniusNode> CreateNode( const std::string &self_address,
                                                  bool               isFullNode          = false,
                                                  bool               isProcessor         = false,
                                                  bool               isGenesisAuthorized = false,
                                                  std::string        existingBasePath    = {} )
    {
        static std::atomic<int> nodeCounter{ 0 };
        const bool              reuseStorage = !existingBasePath.empty();
        std::string             outPathStr   = std::move( existingBasePath );
        if ( outPathStr.empty() )
        {
            const auto id         = nodeCounter.fetch_add( 1 );
            const auto binaryPath = boost::dll::program_location().parent_path();
            const auto outPath    = binaryPath / ( std::string( FILE_PREFIX ) + std::to_string( id ) );
            outPathStr            = outPath.generic_string() + '/';
        }
        else if ( outPathStr.back() != '/' )
        {
            outPathStr.push_back( '/' );
        }

        GeniusNodeConfig devConfig = { "0xcafe", "0.65", "1.0", TokenID::FromBytes( { 0x00 } ), outPathStr };

        if ( !reuseStorage )
        {
            sgns::test::removeAllWithRetry( devConfig.BaseWritePath );
            std::filesystem::create_directories( devConfig.BaseWritePath );
            {
                std::ofstream bridgeConfigFile( devConfig.BaseWritePath + "bridge_chains_config.json" );
                bridgeConfigFile << "{}";
            }
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

        if ( !reuseStorage )
        {
            sgns::GeniusNode::WriteNetworkConfig( devConfig.BaseWritePath, 0, /*auto_dht=*/false );
            sgns::GeniusNode::WriteSgnsConfig( devConfig.BaseWritePath,
                                               isFullNode ? "Full" : "Light",
                                               /*is_processor=*/isProcessor,
                                               /*rpc_catchup=*/false );
        }
        auto node = sgns::GeniusNode::New( devConfig, sgns::FromPrivateKey{ key } );
        if ( isGenesisAuthorized )
        {
            sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
        }

        node_base_paths_.insert_or_assign( node.get(), devConfig.BaseWritePath );
        return node;
    }

    const std::string &GetBaseWritePath( const std::shared_ptr<GeniusNode> &node ) const
    {
        return node_base_paths_.at( node.get() );
    }

    void WaitForReady( const std::shared_ptr<GeniusNode> &node )
    {
        sgns::test::assertWaitForCondition( [&]() { return node->GetState() == GeniusNode::NodeState::READY; },
                                            std::chrono::milliseconds( 50000 ),
                                            "node not synced: " + node->GetAddress() );
    }

    void ConfigureConsensus( const std::shared_ptr<GeniusNode> &node,
                             size_t                             certificates_per_batch,
                             std::chrono::milliseconds          certificate_delay )
    {
        sgns::test::assertWaitForCondition(
            [&]()
            {
                return node->GetState() == GeniusNode::NodeState::READY &&
                       sgns::MultiAccountTestAccess::GetValidatorRegistry( node );
            },
            std::chrono::milliseconds( 50000 ),
            "node blockchain not ready for consensus configuration" );

        ASSERT_TRUE(
            sgns::MultiAccountTestAccess::ConfigureConsensus( node, certificates_per_batch, certificate_delay ) );
    }

    void SetUp() override
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        auto binaryPath = boost::dll::program_location().parent_path();

        // Clean up any previous test runs
        for ( auto &entry : boost::filesystem::directory_iterator( binaryPath ) )
        {
            if ( entry.is_directory() && entry.path().filename().string().find( FILE_PREFIX ) != std::string::npos )
            {
                std::error_code ec;
                sgns::test::removeAllWithRetry( entry.path().string(), ec );
            }
        }
    }

    std::unordered_map<const GeniusNode *, std::string> node_base_paths_;
};

class ValidatorRegistryTest : public MultiAccountTest
{
};

TEST_F( ValidatorRegistryTest, MissingRegistryBlockIsFetchedFromPeerByCid )
{
    auto node_full   = CreateNode( "registry_cid_full", true, true, true );
    auto node_client = CreateNode( "registry_cid_client" );
    node_client->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    WaitForReady( node_full );
    WaitForReady( node_client );

    auto full_registry   = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_full );
    auto client_registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_client );
    ASSERT_TRUE( full_registry );
    ASSERT_TRUE( client_registry );

    ConfigureConsensus( node_full, 1, std::chrono::milliseconds( 100 ) );
    ConfigureConsensus( node_client, 1, std::chrono::milliseconds( 100 ) );

    auto registry_before = full_registry->LoadCurrentRegistry();
    ASSERT_TRUE( registry_before.has_value() );
    const auto initial_epoch = registry_before.value().epoch();

    auto mint_result = node_client->MintTokens( 100,
                                                sgns::test::NextMintSourceHash(),
                                                "test",
                                                TokenID::FromBytes( { 0x00 } ),
                                                "",
                                                std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() );

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto full_state   = full_registry->LoadCurrentRegistry();
            auto client_state = client_registry->LoadCurrentRegistry();
            return full_state.has_value() && client_state.has_value() && full_state.value().epoch() > initial_epoch &&
                   client_state.value().epoch() == full_state.value().epoch() &&
                   client_registry->GetRegistryCid() == full_registry->GetRegistryCid();
        },
        std::chrono::milliseconds( 30000 ),
        "client did not receive the updated validator registry" );

    const auto registry_cid = full_registry->GetRegistryCid();
    auto       parsed_cid   = CID::fromString( registry_cid );
    ASSERT_TRUE( parsed_cid.has_value() );
    auto cid_bytes = parsed_cid.value().toBytes();
    ASSERT_TRUE( cid_bytes.has_value() );

    const auto client_base_path = GetBaseWritePath( node_client );
    const auto client_database_path = sgns::MultiAccountTestAccess::GetDatabasePath( node_client );

    client_registry.reset();
    node_client.reset();

    ASSERT_TRUE( sgns::MultiAccountTestAccess::RemoveRegistryPersistence( client_database_path,
                                                                          std::move( cid_bytes.value() ) ) );

    const auto full_address = node_full->GetPubSub()->GetInterfaceAddress();
    {
        std::ofstream network_config( client_base_path + "network_config.json" );
        ASSERT_TRUE( network_config.good() );
        network_config << "{ \"port_seed\": 0, \"auto_dht\": false, \"upnp_enabled\": false, "
                          "\"bootstrap_addresses\": [\"" << full_address
                       << "\"] }";
    }

    node_client = CreateNode( "registry_cid_client", false, false, false, client_base_path );
    ASSERT_TRUE( node_client );
    WaitForReady( node_client );

    client_registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_client );
    ASSERT_TRUE( client_registry );

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto loaded = client_registry->LoadRegistryByCid( registry_cid );
            return loaded.has_value();
        },
        std::chrono::milliseconds( 30000 ),
        "missing validator registry block was not fetched from the full node" );
}

TEST_F( MultiAccountTest, SyncThroughEachOther )
{
    // Create nodes dynamically
    auto node_full     = CreateNode( "node_multi_full", true, true, true );
    auto node_original = CreateNode( "node_multi_1" );
    node_original->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    WaitForReady( node_full );
    WaitForReady( node_original );

    auto balance_original_start = node_original->GetBalance();
    // Mint some tokens
    auto mint_result = node_original->MintTokens( 100,
                                                  sgns::test::NextMintSourceHash(),
                                                  "test",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "",
                                                  std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";

    mint_result = node_original->MintTokens( 2000,
                                             sgns::test::NextMintSourceHash(),
                                             "test",
                                             sgns::TokenID::FromBytes( { 0x00 } ),
                                             "",
                                             std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";
    mint_result = node_original->MintTokens( 30,
                                             sgns::test::NextMintSourceHash(),
                                             "test",
                                             sgns::TokenID::FromBytes( { 0x00 } ),
                                             "",
                                             std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );

    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";

    std::cout << " 3 mint transactions on original node completed, Creating duplicated node..." << std::endl;

    auto node_duplicated = CreateNode( "node_multi_1", false, true );
    node_duplicated->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    WaitForReady( node_duplicated );

    mint_result = node_duplicated->MintTokens( 60000,
                                               sgns::test::NextMintSourceHash(),
                                               "test",
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

TEST_F( MultiAccountTest, CRDTFilterDuplicateTx )
{
    // Create 3 nodes - 2 with the same address, 1 different (full node for network)
    auto node_full        = CreateNode( "full_node_address_unique", true, true, true );
    auto node_same_addr_1 = CreateNode( "duplicate_address_12345" );
    node_same_addr_1->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    auto node_same_addr_2 = CreateNode( "duplicate_address_12345", false, true );
    node_same_addr_2->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    WaitForReady( node_full );
    WaitForReady( node_same_addr_1 );
    WaitForReady( node_same_addr_2 );

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
    auto tx_count_node1_start = node_same_addr_1->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );
    auto tx_count_node2_start = node_same_addr_2->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );
    auto tx_count_full_start  = node_full->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );

    fmt::println( "Initial tx counts - Node1: {}, Node2: {}, Full: {}",
                  tx_count_node1_start,
                  tx_count_node2_start,
                  tx_count_full_start );

    // Mint tokens on both nodes with same address BEFORE connecting them
    std::cout << "Minting tokens on isolated nodes..." << std::endl;

    auto mint_result_1 = node_same_addr_1->MintTokens( 50000000000, // 50 GNUS
                                                       sgns::test::NextMintSourceHash(),
                                                       "test",
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       "",
                                                       std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result_1.has_value() ) << "Mint transaction failed on node_same_addr_1";

    std::cout << "Mint transaction 1 ID: " << mint_result_1.value().first << std::endl;

    sgns::test::assertWaitForCondition( [&]()
                                        { return node_same_addr_2->GetBalance() == balance_node2_start + 50000000000; },
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
    node_same_addr_2->AddPeers( { node_same_addr_1->GetPubSub()->GetInterfaceAddress() } );

    auto tx1_received = node_full->WaitForTransactionIncoming(
        transfer1_res.value(),
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );

    fmt::println( "Waiting for the conflict resolution" );

    // Both transfers claim the same address+nonce, so exactly one can be certified. The
    // losing peer must learn that from the winner's certificate and fail its own
    // transaction, rather than sitting in VERIFYING until the proposal TTL expires.
    const auto conflict_start = std::chrono::steady_clock::now();

    uint64_t           correct_tokens_transferred = 0;
    sgns::GeniusNode  *losing_node                = nullptr;
    std::string        losing_tx;
    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto status1 = node_same_addr_1->GetTransactionStatus( transfer1_res.value() );
            if ( status1 == TransactionManager::TransactionStatus::CONFIRMED )
            {
                correct_tokens_transferred = 10000000000;
                losing_node                = node_same_addr_2.get();
                losing_tx                  = transfer2_res.value();
                return true;
            }

            auto status2 = node_same_addr_2->GetTransactionStatus( transfer2_res.value() );
            if ( status2 == TransactionManager::TransactionStatus::CONFIRMED )
            {
                correct_tokens_transferred = 13000000000;
                losing_node                = node_same_addr_1.get();
                losing_tx                  = transfer1_res.value();
                return true;
            }

            return false;
        },
        std::chrono::milliseconds( 50000 ),
        "Neither transfer was confirmed" );

    ASSERT_NE( losing_node, nullptr );

    // The whole point of the fix: the loser fails off the winner's certificate. The bound
    // is deliberately far below ConsensusManager::PendingLifecycleConfig::pending_ttl
    // (3 minutes), so a regression to "wait for the TTL" fails this test instead of
    // merely slowing it down.
    static constexpr auto kFailFastBudget = std::chrono::seconds( 60 );
    static_assert( kFailFastBudget < std::chrono::minutes( 3 ), "budget must beat the proposal TTL" );

    sgns::test::assertWaitForCondition(
        [&]()
        { return losing_node->GetTransactionStatus( losing_tx ) == TransactionManager::TransactionStatus::FAILED; },
        kFailFastBudget,
        "Losing transaction did not fail after the winner's certificate arrived" );

    const auto conflict_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - conflict_start );
    fmt::println( "Losing transaction {} failed {} ms after the conflict started", losing_tx, conflict_elapsed.count() );
    EXPECT_LT( conflict_elapsed, std::chrono::minutes( 3 ) )
        << "loser only resolved around the proposal TTL -- the certificate shortcut did not work";

    sgns::test::assertWaitForCondition(
        [&]() { return node_same_addr_1->GetBalance() == ( balance_node1_after_mint - correct_tokens_transferred ); },
        std::chrono::milliseconds( 50000 ),
        "node_same_addr_1 balance not synced" );
    sgns::test::assertWaitForCondition( [&]()
                                        { return node_same_addr_2->GetBalance() == node_same_addr_1->GetBalance(); },
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
    auto tx_count_node1_final = node_same_addr_1->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );
    auto tx_count_node2_final = node_same_addr_2->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );

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
    const auto       kCertificateDelay     = std::chrono::seconds( 1 );

    auto node_full   = CreateNode( "node_consensus_full", true, true, true );
    auto node_client = CreateNode( "node_consensus_client" );
    auto node_peer1  = CreateNode( "node_consensus_peer1" );
    auto node_peer2  = CreateNode( "node_consensus_peer2" );
    auto node_peer3  = CreateNode( "node_consensus_peer3" );

    const std::array nodes = { node_full, node_client, node_peer1, node_peer2, node_peer3 };
    for ( size_t i = 1; i < nodes.size(); ++i )
    {
        nodes[i]->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    }
    for ( const auto &node : nodes )
    {
        ConfigureConsensus( node, kCertificatesPerBatch, kCertificateDelay );
    }

    auto registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_full );
    ASSERT_TRUE( registry );

    fmt::println( "Nodes created. Registry loaded" );
    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadCurrentRegistry();
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
                auto load = registry->LoadCurrentRegistry();
                return load.has_value() && load.value().epoch() > epoch_before &&
                       registry->GetRegistryCid() != cid_before;
            },
            std::chrono::milliseconds( 30000 ),
            "validator registry did not update" );

        auto registry_after = registry->LoadCurrentRegistry();
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
        auto client_registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_client );
        ASSERT_TRUE( client_registry );

        sgns::test::assertWaitForCondition(
            [&]()
            {
                auto full_load   = registry->LoadCurrentRegistry();
                auto client_load = client_registry->LoadCurrentRegistry();
                return full_load.has_value() && client_load.has_value() &&
                       client_registry->GetRegistryCid() == registry->GetRegistryCid() &&
                       client_load.value().epoch() >= full_load.value().epoch();
            },
            std::chrono::milliseconds( 30000 ),
            "node_client validator registry not caught up" );
    };

    auto load_registry_state = [&]() -> std::pair<uint64_t, std::string>
    {
        auto state = registry->LoadCurrentRegistry();
        EXPECT_TRUE( state.has_value() );
        if ( !state.has_value() )
        {
            return { 0, "" };
        }
        return { state.value().epoch(), registry->GetRegistryCid() };
    };

    auto [epoch_before, cid_before] = load_registry_state();

    auto mint1 = node_client->MintTokens( 100,
                                          sgns::test::NextMintSourceHash(),
                                          "test",
                                          TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( mint1.has_value() ) << "Mint 1 failed on node_client";
    fmt::println( "Mint 1 succeeded" );

    assert_registry_updated( epoch_before, cid_before );
    wait_client_registry_caught_up();

    std::tie( epoch_before, cid_before ) = load_registry_state();

    auto mint2 = node_client->MintTokens( 250,
                                          sgns::test::NextMintSourceHash(),
                                          "test",
                                          TokenID::FromBytes( { 0x00 } ) );
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
    const auto       kCertificateDelay     = std::chrono::seconds( 1 );

    auto node_full   = CreateNode( "node_consensus_batch5_full", true, true, true );
    auto node_client = CreateNode( "node_consensus_batch5_client" );
    auto node_peer1  = CreateNode( "node_consensus_batch5_peer1" );
    auto node_peer2  = CreateNode( "node_consensus_batch5_peer2" );
    auto node_peer3  = CreateNode( "node_consensus_batch5_peer3" );

    const std::array nodes = { node_full, node_client, node_peer1, node_peer2, node_peer3 };
    for ( size_t i = 1; i < nodes.size(); ++i )
    {
        nodes[i]->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    }
    for ( const auto &node : nodes )
    {
        ConfigureConsensus( node, kCertificatesPerBatch, kCertificateDelay );
    }

    auto registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_full );
    ASSERT_TRUE( registry );

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadCurrentRegistry();
            return load.has_value() && !registry->GetRegistryCid().empty();
        },
        std::chrono::milliseconds( 30000 ),
        "validator registry not initialized" );

    auto registry_state = registry->LoadCurrentRegistry();
    ASSERT_TRUE( registry_state.has_value() );
    const auto initial_epoch = registry_state.value().epoch();
    const auto initial_cid   = registry->GetRegistryCid();

    auto assert_registry_immutable = [&]( const char *step )
    {
        const auto deadline = std::chrono::steady_clock::now() + kCertificateDelay + std::chrono::seconds( 1 );
        while ( std::chrono::steady_clock::now() < deadline )
        {
            auto load = registry->LoadCurrentRegistry();
            ASSERT_TRUE( load.has_value() ) << "registry load failed during " << step;
            EXPECT_EQ( load.value().epoch(), initial_epoch ) << "registry epoch changed unexpectedly at " << step;
            EXPECT_EQ( registry->GetRegistryCid(), initial_cid ) << "registry CID changed unexpectedly at " << step;
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }
    };

    auto mint1 = node_client->MintTokens( 100,
                                          sgns::test::NextMintSourceHash(),
                                          "test",
                                          TokenID::FromBytes( { 0x00 } ),
                                          "",
                                          std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint1.has_value() ) << "Mint 1 failed on node_client";

    auto mint2 = node_client->MintTokens( 250,
                                          sgns::test::NextMintSourceHash(),
                                          "test",
                                          TokenID::FromBytes( { 0x00 } ),
                                          "",
                                          std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint2.has_value() ) << "Mint 2 failed on node_client";

    auto transfer1 = node_client->TransferFunds( 75,
                                                 node_peer1->GetAddress(),
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer1.has_value() ) << "Transfer 1 failed on node_client";

    auto transfer2 = node_client->TransferFunds( 40,
                                                 node_peer2->GetAddress(),
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer2.has_value() ) << "Transfer 2 failed on node_client";
    // Waiting once after the fourth certificate detects any premature update from the partial batch.
    assert_registry_immutable( "tx4" );

    auto transfer3 = node_client->TransferFunds( 10,
                                                 node_peer3->GetAddress(),
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer3.has_value() ) << "Transfer 3 failed on node_client";

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadCurrentRegistry();
            return load.has_value() && load.value().epoch() > initial_epoch &&
                   registry->GetRegistryCid() != initial_cid;
        },
        std::chrono::milliseconds( 60000 ),
        "validator registry did not update after 5th certificate" );

    auto registry_after = registry->LoadCurrentRegistry();
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
