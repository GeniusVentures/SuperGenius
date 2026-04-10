#include <filesystem>
#include <thread>
#include <iostream>
#include <cstring>
#include <system_error>
#include <chrono>
#include <atomic>

#include <gtest/gtest.h>
#include <boost/dll.hpp>

#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "testutil/wait_condition.hpp"

namespace fs = std::filesystem;

/**
 * @brief Test parameters for migration.
 */
struct NodeParams
{
    std::string subdir;           ///< Node folder name.
    const char *key_hex;          ///< Node key.
    uint64_t    expected_balance; ///< Expected balance after migration.
};

class MigrationParamTest : public ::testing::TestWithParam<NodeParams>
{
protected:
    static inline DevConfig_st DEV_CONFIG = {
        "0xdeef",                             // Addr
        "0.65",                               // Cut
        "1.0",                                // TokenValueInGNUS
        sgns::TokenID::FromBytes( { 0x00 } ), // TokenID
        ""                                    // BaseWritePath
    };

    static constexpr std::string_view DB_PREFIX        = "SuperGNUSNode.TestNet.2a.00.";
    static constexpr int              STARTUP_DELAY_MS = 1000;
    static constexpr std::string_view FULL_NODE_SUBDIR = "migration_full_node";
    static constexpr std::string_view FULL_NODE_ADDR   = "0xcafe";
    static constexpr char FULL_NODE_KEY[] = "feedbeeffeedbeeffeedbeeffeedbeeffeedbeeffeedbeeffeedbeeffeedbeef";
    static constexpr std::string_uview FULL_NODE_PUB_ADDRESS =
        "16fc3a9c86b42bd7e02b4c3276704948211a034b6cddfe024bfaf39dfb51d95a9649c5b149d18956991cc116f148f6441fc8fc60205d499dad35421c1279dd93";
    static constexpr uint16_t FULL_NODE_BASEPORT = 43001;

    static void RemovePrefixedSubdirs( const fs::path &baseDir )
    {
        if ( !fs::exists( baseDir ) || !fs::is_directory( baseDir ) )
        {
            return;
        }
        std::error_code ec;
        for ( auto const &entry : fs::directory_iterator( baseDir, fs::directory_options::skip_permission_denied, ec ) )
        {
            if ( ec )
            {
                return;
            }
            if ( entry.is_directory() )
            {
                auto name = entry.path().filename().string();
                // Remove new database directories, preserving legacy (00) test data
                if ( name.find( DB_PREFIX ) == std::string::npos )
                {
                    fs::remove_all( entry.path(), ec );
                    // On Windows, file locks may not be immediately released
                    // Retry removal if it fails
                    if ( ec && fs::exists( entry.path() ) )
                    {
                        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                        ec.clear();
                        fs::remove_all( entry.path() );
                    }
                }
            }
        }
    }

    static std::shared_ptr<sgns::GeniusNode> CreateNodeInstance( const std::string &binaryParent,
                                                                 const std::string &subdir,
                                                                 const char        *key_hex,
                                                                 bool               is_full_node = false,
                                                                 bool               is_processor = false,
                                                                 uint16_t           base_port    = 40001 )
    {
        fs::path nodeDir = fs::path{ binaryParent } / subdir;
        RemovePrefixedSubdirs( nodeDir );

        std::string baseWrite    = binaryParent + "/" + subdir + "/";
        DEV_CONFIG.BaseWritePath = baseWrite;

        auto instance = sgns::GeniusNode::New( DEV_CONFIG, key_hex, false, is_processor, base_port, is_full_node );
        std::this_thread::sleep_for( std::chrono::milliseconds( STARTUP_DELAY_MS ) );
        return instance;
    }

    static std::shared_ptr<sgns::GeniusNode> CreateFullNodeInstance()
    {
        static std::atomic<int> nodeCounter{ 0 };
        int                     id = nodeCounter.fetch_add( 1 );

        std::string     binaryPath = boost::dll::program_location().parent_path().string();
        std::string     outPath    = ( binaryPath + '/' ).append( FULL_NODE_SUBDIR ) + '_' + std::to_string( id ) + '/';
        std::error_code ec;
        fs::remove_all( outPath, ec );
        fs::create_directories( outPath, ec );

        DevConfig_st devConfig = { std::string( FULL_NODE_ADDR ),
                                   "0.65",
                                   "1.0",
                                   TokenID::FromBytes( { 0x00 } ),
                                   outPath };

        uint16_t unique_port = FULL_NODE_BASEPORT + static_cast<uint16_t>( id );
        auto     instance    = GeniusNode::New( devConfig, FULL_NODE_KEY, false, false, unique_port, true );
        std::this_thread::sleep_for( std::chrono::milliseconds( STARTUP_DELAY_MS ) );
        std::cout << "Full node created" << std::endl;
        return instance;
    }
};

TEST_P( MigrationParamTest, BalanceAfterMigration )
{
    std::string full_node_pub_address{ FULL_NODE_PUB_ADDRESS };
    Blockchain::SetAuthorizedFullNodeAddress( full_node_pub_address );
    auto params    = GetParam();
    auto full_node = CreateFullNodeInstance();
    EXPECT_EQ( full_node->GetAddress(), full_node_pub_address );
    test::assertWaitForCondition(
        [full_node]
        { return full_node && full_node->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 30000 ),
        "Full node not synced" );
    auto binaryParent = boost::dll::program_location().parent_path().string();
    auto node         = CreateNodeInstance( binaryParent, params.subdir, params.key_hex );

    node->GetPubSub()->AddPeers( { full_node->GetPubSub()->GetInterfaceAddress() } );

    const std::string readiness_message = params.subdir + " node not ready";
    test::assertWaitForCondition(
        [node] { return node && node->GetTransactionManagerState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 40000 ),
        readiness_message );

    EXPECT_EQ( node->GetBalance(), params.expected_balance );
}

INSTANTIATE_TEST_SUITE_P(
    Nodes,
    MigrationParamTest,
    ::testing::Values( NodeParams{ "node10_0_2_0",
                                   "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                   238000000000ULL },
                       NodeParams{ "node20_0_2_0",
                                   "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                   262000000000ULL } ),
    []( const ::testing::TestParamInfo<NodeParams> &info ) { return info.param.subdir; } );
