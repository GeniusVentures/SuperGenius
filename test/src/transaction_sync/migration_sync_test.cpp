#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <system_error>
#include <chrono>
#include <atomic>

#include <gtest/gtest.h>
#include <boost/dll.hpp>

#include "account/MigrationAllowList.hpp"
#include "account/MigrationInputValidator.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"

namespace fs = std::filesystem;

using namespace sgns;

namespace
{
    std::string ConfiguredFixtureAddress( const GeniusNodeConfig &config, const char *key_hex )
    {
        const auto account = GeniusAccount::NewFromPrivateKey(
            config.TokenID, key_hex, boost::filesystem::path( config.BaseWritePath ) / "configured-identity", true );
        return account ? account->GetAddress() : std::string{};
    }

    uint64_t RequireActiveBalance( GeniusNode &node )
    {
        const auto address = node.GetActiveAccountAddress();
        if ( address.has_error() )
        {
            ADD_FAILURE() << "expected active account address: " << address.error().message();
            return 0;
        }
        return node.GetBalance();
    }
} // namespace

TEST( MigrationInputValidatorTest, RegisteredWithoutLocalUTXOWitnessRequirement )
{
    const auto *validator = IInputValidator::Get( "migration" );
    ASSERT_NE( validator, nullptr );
    EXPECT_FALSE( validator->RequiresConsensusUTXOData() );
}

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
    static inline GeniusNodeConfig DEV_CONFIG = {
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
    static constexpr char     FULL_NODE_KEY[]    = "feedbeeffeedbeeffeedbeeffeedbeeffeedbeeffeedbeeffeedbeeffeedbeef";

    void SetEligibilityCheckEnabled( bool enabled )
    {
        sgns::MigrationAllowList::SetEligibilityCheckEnabledForTests( enabled );
    }

    void SetUp() override
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
        SetEligibilityCheckEnabled( true );
    }

    void TearDown() override
    {
        SetEligibilityCheckEnabled( true );
    }

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
                    sgns::test::removeAllWithRetry( entry.path() );
                }
            }
        }
    }

    static std::shared_ptr<sgns::GeniusNode> CreateNodeInstance( const std::string &binaryParent,
                                                                 const std::string &subdir,
                                                                 const char        *key_hex,
                                                                 bool               is_full_node = false )
    {
        fs::path nodeDir = fs::path{ binaryParent } / subdir;
        RemovePrefixedSubdirs( nodeDir );

        std::string baseWrite    = binaryParent + "/" + subdir + "/";
        DEV_CONFIG.BaseWritePath = baseWrite;

        // All nodes in this test are non-processors (is_processor=false). Config-driven (Phase 3).
        std::filesystem::create_directories( DEV_CONFIG.BaseWritePath );
        sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG.BaseWritePath,
                                           is_full_node ? "Full" : "Light",
                                           /*is_processor=*/false,
                                           /*rpc_catchup=*/false );

        auto instance = sgns::GeniusNode::New( DEV_CONFIG, sgns::FromPrivateKey{ key_hex } );
        return instance;
    }

    static std::shared_ptr<sgns::GeniusNode> CreateFullNodeInstance()
    {
        static std::atomic<int> nodeCounter{ 0 };
        int                     id = nodeCounter.fetch_add( 1 );

        std::string     binaryPath = boost::dll::program_location().parent_path().string();
        std::string     outPath    = ( binaryPath + '/' ).append( FULL_NODE_SUBDIR ) + '_' + std::to_string( id ) + '/';
        std::error_code ec;
        sgns::test::removeAllWithRetry( outPath, ec );
        fs::create_directories( outPath, ec );

        GeniusNodeConfig devConfig = { std::string( FULL_NODE_ADDR ),
                                   "0.65",
                                   "1.0",
                                   TokenID::FromBytes( { 0x00 } ),
                                   outPath };

        // Full node is not a processor (is_processor=false). Config-driven (Phase 3).
        std::filesystem::create_directories( devConfig.BaseWritePath );
        GeniusNode::WriteNetworkConfig( devConfig.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        GeniusNode::WriteSgnsConfig( devConfig.BaseWritePath,
                                      /*node_type=*/"Full",
                                      /*is_processor=*/false,
                                      /*rpc_catchup=*/false );
        const auto configured_address = ConfiguredFixtureAddress( devConfig, FULL_NODE_KEY );
        if ( configured_address.empty() )
        {
            return nullptr;
        }
        auto instance = GeniusNode::New( devConfig, FromPrivateKey{ FULL_NODE_KEY } );
        Blockchain::SetAuthorizedFullNodeAddress( configured_address );

        return instance;
    }
};

TEST_P( MigrationParamTest, BalanceAfterMigration )
{
    SetEligibilityCheckEnabled( false );

    auto params    = GetParam();
    auto full_node = CreateFullNodeInstance();

    auto binaryParent = boost::dll::program_location().parent_path().string();
    auto node         = CreateNodeInstance( binaryParent, params.subdir, params.key_hex );

    node->AddPeers( { full_node->GetPubSub()->GetInterfaceAddress() } );

    const std::string readiness_message = params.subdir + " node not ready";

    test::assertWaitForCondition( [full_node] { return full_node->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 100000 ),
                                  "Full node not synced" );
    test::assertWaitForCondition( [node] { return node && node->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 100000 ),
                                  readiness_message );

    EXPECT_EQ( RequireActiveBalance( *node ), params.expected_balance );
}

TEST_F( MigrationParamTest, RejectsOverclaimWhenAllowListEnabled )
{
    namespace fs = std::filesystem;
    using sgns::MigrationAllowList;
    using sgns::storage::rocksdb;

    const auto      unique_suffix = std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() );
    const fs::path  db_path       = fs::temp_directory_path() / ( "migration_allowlist_rejects_test_" + unique_suffix );
    std::error_code ec;
    sgns::test::removeAllWithRetry( db_path, ec );
    fs::create_directories( db_path, ec );
    ASSERT_FALSE( ec ) << "Failed to create temp DB directory: " << ec.message();

    rocksdb::Options options;
    options.create_if_missing = true;

    auto db_result = rocksdb::create( db_path.string(), options );
    ASSERT_TRUE( db_result.has_value() ) << db_result.error().message();

    MigrationAllowList allow_list( db_result.value(), "3.6.0" );
    ASSERT_TRUE( allow_list.StoreObservedBalance( "eligible-address", 100 ).has_value() );

    auto eligible = allow_list.IsEligible( "eligible-address", 201 );
    ASSERT_TRUE( eligible.has_value() ) << eligible.error().message();
    EXPECT_FALSE( eligible.value() );

    sgns::test::removeAllWithRetry( db_path, ec );
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
