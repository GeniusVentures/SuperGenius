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
