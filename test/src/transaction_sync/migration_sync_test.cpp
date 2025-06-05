#include <filesystem>
#include <thread>
#include <iostream>
#include <cstring>
#include <system_error>

#include <gtest/gtest.h>
#include <boost/dll.hpp>

#include "account/GeniusNode.hpp"
#include "account/MigrationManager.hpp"
#include "FileManager.hpp"

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
    static inline DevConfig_st DEV_CONFIG = { .Addr             = "0xcafe",
                                              .Cut              = "0.65",
                                              .TokenValueInGNUS = 1.0,
                                              .TokenID          = 0,
                                              .BaseWritePath    = "" };

    std::shared_ptr<sgns::GeniusNode> node;

    static constexpr char DB_PREFIX[]      = "SuperGNUSNode.TestNet.2a.01.";
    static constexpr int  STARTUP_DELAY_MS = 1000;

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
                if ( name.rfind( DB_PREFIX, 0 ) == 0 )
                {
                    fs::remove_all( entry.path(), ec );
                }
            }
        }
    }

    static std::shared_ptr<sgns::GeniusNode> CreateNodeInstance( const std::string &binaryParent,
                                                                 const std::string &subdir,
                                                                 const char        *key_hex )
    {
        fs::path nodeDir = fs::path{ binaryParent } / subdir;
        RemovePrefixedSubdirs( nodeDir );

        std::string baseWrite = binaryParent + "/" + subdir + "/";
        std::strncpy( DEV_CONFIG.BaseWritePath, baseWrite.c_str(), sizeof( DEV_CONFIG.BaseWritePath ) );
        DEV_CONFIG.BaseWritePath[sizeof( DEV_CONFIG.BaseWritePath ) - 1] = '\0';

        auto instance = std::make_shared<sgns::GeniusNode>( DEV_CONFIG, key_hex, false, false );
        std::this_thread::sleep_for( std::chrono::milliseconds( STARTUP_DELAY_MS ) );
        return instance;
    }

    void SetUp() override
    {
        auto        params       = GetParam();
        std::string binaryParent = boost::dll::program_location().parent_path().string();
        node                     = CreateNodeInstance( binaryParent, params.subdir, params.key_hex );
    }

    void TearDown() override
    {
        node.reset();
    }
};

TEST_P( MigrationParamTest, BalanceAfterMigration )
{
    EXPECT_EQ( node->GetBalance(), GetParam().expected_balance );
}

INSTANTIATE_TEST_SUITE_P(
    Nodes,
    MigrationParamTest,
    ::testing::Values( NodeParams{ .subdir  = "node10_0_2_0",
                                   .key_hex = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                   .expected_balance = 238000000000ULL },
                       NodeParams{ .subdir  = "node20_0_2_0",
                                   .key_hex = "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                   .expected_balance = 273000000000ULL } ),
    []( const ::testing::TestParamInfo<NodeParams> &info ) { return info.param.subdir; } );
