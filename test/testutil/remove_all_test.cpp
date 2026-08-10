#include "testutil/remove_all.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>

TEST( RemoveAllWithRetryTest, RemovesDirectoryAfterFileHandleCloses )
{
    const auto path = std::filesystem::temp_directory_path() / "supergenius_remove_all_with_retry_test";
    sgns::test::removeAllWithRetry( path );
    std::filesystem::create_directories( path );

    std::ofstream locked_file( path / "locked" );
    ASSERT_TRUE( locked_file.good() );

    std::thread close_file(
        [&locked_file]
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            locked_file.close();
        } );

    EXPECT_NO_THROW( sgns::test::removeAllWithRetry( path ) );
    close_file.join();
    EXPECT_FALSE( std::filesystem::exists( path ) );
}
