#include <filesystem>
#include <thread>

#include <gtest/gtest.h>
#include <boost/dll.hpp>

#include "account/GeniusNode.hpp"
#include "account/MigrationManager.hpp"
#include "FileManager.hpp"

namespace fs = std::filesystem;

namespace sgns
{
    class MigrationSyncTest : public ::testing::Test
    {
    protected:
        static inline std::shared_ptr<GeniusNode> node10 = nullptr;
        static inline std::shared_ptr<GeniusNode> node20 = nullptr;

        static inline DevConfig_st DEV_CONFIG10 = { "0xcafe", "0.65", 1.0, 0, "./node10_0_2_0" };
        static inline DevConfig_st DEV_CONFIG20 = { "0xcafe", "0.65", 1.0, 0, "./node20_0_2_0" };

        static void SetUpTestSuite()
        {
            std::string binary_path = boost::dll::program_location().parent_path().string();

            fs::copy_options opt = fs::copy_options::overwrite_existing | fs::copy_options::recursive;
            fs::copy("./node10", DEV_CONFIG10.BaseWritePath, opt);
            fs::copy("./node20", DEV_CONFIG20.BaseWritePath, opt);

            std::strncpy( DEV_CONFIG10.BaseWritePath,
                          ( binary_path + "/node10_0_2_0/" ).c_str(),
                          sizeof( DEV_CONFIG10.BaseWritePath ) );
            std::strncpy( DEV_CONFIG20.BaseWritePath,
                          ( binary_path + "/node20_0_2_0/" ).c_str(),
                          sizeof( DEV_CONFIG20.BaseWritePath ) );
            DEV_CONFIG10.BaseWritePath[sizeof( DEV_CONFIG10.BaseWritePath ) - 1] = '\0';
            DEV_CONFIG20.BaseWritePath[sizeof( DEV_CONFIG20.BaseWritePath ) - 1] = '\0';

            node10 = std::make_shared<GeniusNode>( DEV_CONFIG10,
                                                   "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                                   false,
                                                   false );
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

            node20 = std::make_shared<GeniusNode>( DEV_CONFIG20,
                                                   "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                                   false,
                                                   false );
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        }

        static void TearDownTestSuite()
        {
            node10.reset();
            node20.reset();
        }
    };

    TEST_F( MigrationSyncTest, BalanceAfterMigration )
    {
        EXPECT_EQ( node10->GetBalance(), 238000000000 );
        EXPECT_EQ( node20->GetBalance(), 273000000000 );
    }
}
