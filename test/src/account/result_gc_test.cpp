/**
 * @file       result_gc_test.cpp
 * @brief      Unit tests for result-cache GC logic (Phase 5).
 * @date       2026-06-30
 *
 * Covers verification items:
 *   7. TTL-based eviction — expired files removed, non-expired remain.
 *   8. Space-cap eviction — oldest files evicted first when over limit.
 *
 * ponytail: Tests the GC algorithm in isolation with temp directories rather
 *           than through GeniusNode::RunResultGC() directly because
 *           constructing a GeniusNode requires the full network stack.
 *           The algorithm mirrors RunResultGC() line-for-line.
 *           Upgrade path: add a GeniusNode integration test that starts a
 *           node, publishes results, and verifies GC via the timer callback.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <vector>

#include "testutil/remove_all.hpp"

namespace fs = std::filesystem;

/// @brief Mirrors the core logic of GeniusNode::RunResultGC() in isolation.
///
/// @param resultsDir       Path to the results/ directory.
/// @param retentionHours   Files older than this are evicted.  0 = skip (keep forever).
/// @param maxMb            Space cap in MB.  0 = no cap.
/// @return                 Number of files evicted.
static size_t runGC( const fs::path &resultsDir, int retentionHours, int maxMb )
{
    if ( retentionHours == 0 )
    {
        return 0; // GC disabled
    }

    std::error_code ec;
    if ( !fs::exists( resultsDir, ec ) )
    {
        return 0;
    }

    // Collect all files, sorted by mtime (oldest first).
    struct FileEntry
    {
        fs::path                      path;
        fs::file_time_type            mtime;
    };
    std::vector<FileEntry> files;

    for ( const auto &entry : fs::recursive_directory_iterator( resultsDir, ec ) )
    {
        if ( ec || !entry.is_regular_file() )
        {
            continue;
        }
        FileEntry fe;
        fe.path  = entry.path();
        fe.mtime = entry.last_write_time();
        files.push_back( fe );
    }

    std::sort( files.begin(), files.end(),
               []( const auto &a, const auto &b ) { return a.mtime < b.mtime; } );

    // Compute cutoff using file_time_type clock (matches production code).
    using FT = fs::file_time_type;
    auto now    = FT::clock::now();
    auto cutoff = now - std::chrono::hours( retentionHours );

    // Compute total bytes.
    uintmax_t totalBytes = 0;
    for ( const auto &f : files )
    {
        totalBytes += fs::file_size( f.path, ec );
    }

    size_t deletedCount = 0;

    for ( auto it = files.begin(); it != files.end() && totalBytes > 0; ++it )
    {
        bool expired = it->mtime < cutoff;
        bool overCap = ( maxMb > 0 ) &&
                       ( totalBytes > static_cast<uintmax_t>( maxMb ) * 1024 * 1024 );

        if ( !expired && !overCap )
        {
            break; // Sorted by age: no more candidates.
        }

        auto fileSize = fs::file_size( it->path, ec );
        fs::remove( it->path, ec );
        if ( !ec )
        {
            ++deletedCount;
            totalBytes -= fileSize;
        }
    }

    return deletedCount;
}

/// @brief Create a file with the given content and set its mtime to an offset
///        from "now" in file_time_type clock.
static void createFileWithAge( const fs::path &filePath,
                               const std::string &content,
                               std::chrono::hours ageHours )
{
    fs::create_directories( filePath.parent_path() );
    {
        std::ofstream f( filePath, std::ios::binary );
        f.write( content.data(), content.size() );
    }

    using FT = fs::file_time_type;
    auto mtime = FT::clock::now() - ageHours;
    fs::last_write_time( filePath, mtime );
}

// ═══════════════════════════════════════════════════════════════════════
// Verification 7: TTL-based eviction.
// ═══════════════════════════════════════════════════════════════════════

class ResultGCTest : public ::testing::Test
{
public:
    void SetUp() override
    {
        tempDir_ = fs::temp_directory_path() / "sgns_test_gc";
        fs::create_directories( tempDir_ );
        resultsDir_ = tempDir_ / "results";
        fs::create_directories( resultsDir_ );
    }

    void TearDown() override
    {
        std::error_code ec;
        sgns::test::removeAllWithRetry( tempDir_, ec );
    }

    fs::path tempDir_;
    fs::path resultsDir_;
};

/**
 * @given A results directory with files of varying ages.
 * @when GC runs with retention=10 hours.
 * @then Files older than 10h are evicted; newer files remain.
 */
TEST_F( ResultGCTest, TTL_EvictsExpiredFiles )
{
    // Create files with ages: 1h, 5h, 12h, 24h.
    createFileWithAge( resultsDir_ / "cid_recent",     "aaaa", std::chrono::hours( 1 ) );
    createFileWithAge( resultsDir_ / "cid_mid",        "bbbb", std::chrono::hours( 5 ) );
    createFileWithAge( resultsDir_ / "cid_old",        "cccc", std::chrono::hours( 12 ) );
    createFileWithAge( resultsDir_ / "cid_veryold",    "dddd", std::chrono::hours( 24 ) );

    size_t deleted = runGC( resultsDir_, /*retentionHours=*/10, /*maxMb=*/0 );

    EXPECT_EQ( deleted, 2u ); // cid_old and cid_veryold should be gone.

    // Recent files survive.
    EXPECT_TRUE( fs::exists( resultsDir_ / "cid_recent" ) );
    EXPECT_TRUE( fs::exists( resultsDir_ / "cid_mid" ) );
    EXPECT_FALSE( fs::exists( resultsDir_ / "cid_old" ) );
    EXPECT_FALSE( fs::exists( resultsDir_ / "cid_veryold" ) );
}

/**
 * @given A results directory with all files younger than retention.
 * @when GC runs.
 * @then No files are evicted.
 */
TEST_F( ResultGCTest, TTL_KeepsAllWhenNoneExpired )
{
    createFileWithAge( resultsDir_ / "cid_a", "aaaa", std::chrono::hours( 1 ) );
    createFileWithAge( resultsDir_ / "cid_b", "bbbb", std::chrono::hours( 2 ) );

    size_t deleted = runGC( resultsDir_, /*retentionHours=*/24, /*maxMb=*/0 );

    EXPECT_EQ( deleted, 0u );
    EXPECT_TRUE( fs::exists( resultsDir_ / "cid_a" ) );
    EXPECT_TRUE( fs::exists( resultsDir_ / "cid_b" ) );
}

/**
 * @given retention_hours = 0 (disabled).
 * @when GC runs.
 * @then No files are evicted (keep forever mode).
 */
TEST_F( ResultGCTest, TTL_DisabledRetentionKeepsAll )
{
    createFileWithAge( resultsDir_ / "cid_x", "xxxx", std::chrono::hours( 100 ) );
    createFileWithAge( resultsDir_ / "cid_y", "yyyy", std::chrono::hours( 200 ) );

    size_t deleted = runGC( resultsDir_, /*retentionHours=*/0, /*maxMb=*/0 );

    EXPECT_EQ( deleted, 0u );
    EXPECT_TRUE( fs::exists( resultsDir_ / "cid_x" ) );
    EXPECT_TRUE( fs::exists( resultsDir_ / "cid_y" ) );
}

/**
 * @given An empty results directory.
 * @when GC runs.
 * @then No-op, no crashes.
 */
TEST_F( ResultGCTest, TTL_EmptyDirectoryNoOp )
{
    size_t deleted = runGC( resultsDir_, /*retentionHours=*/24, /*maxMb=*/0 );
    EXPECT_EQ( deleted, 0u );
}

/**
 * @given A non-existent results directory.
 * @when GC runs.
 * @then No-op, no crashes.
 */
TEST_F( ResultGCTest, TTL_MissingDirectoryNoOp )
{
    fs::path nonexistent = tempDir_ / "nonexistent_results";
    size_t   deleted     = runGC( nonexistent, /*retentionHours=*/24, /*maxMb=*/0 );
    EXPECT_EQ( deleted, 0u );
}

// ═══════════════════════════════════════════════════════════════════════
// Verification 8: Space-cap eviction.
// ═══════════════════════════════════════════════════════════════════════

/**
 * @given A results directory exceeding the space cap.
 * @when GC runs with maxMb=1.
 * @then Oldest files are evicted first until under the cap.
 */
TEST_F( ResultGCTest, SpaceCap_EvictsOldestFirst )
{
    // Create files totaling > 2 MB.  Each file is ~600 KB.
    std::string data600k( 600 * 1024, 'X' );

    createFileWithAge( resultsDir_ / "cid_oldest",  data600k, std::chrono::hours( 5 ) );
    createFileWithAge( resultsDir_ / "cid_older",   data600k, std::chrono::hours( 3 ) );
    createFileWithAge( resultsDir_ / "cid_newer",   data600k, std::chrono::hours( 1 ) );
    createFileWithAge( resultsDir_ / "cid_newest",  data600k, std::chrono::hours( 0 ) );

    // Cap at 1 MB → need to evict until under ~1 MB.
    size_t deleted = runGC( resultsDir_, /*retentionHours=*/100, /*maxMb=*/1 );

    // Oldest files should be evicted first.
    EXPECT_GT( deleted, 0u );
    EXPECT_FALSE( fs::exists( resultsDir_ / "cid_oldest" ) ) << "Oldest file should be evicted first";
}

/**
 * @given Space cap is 0 (disabled).
 * @when GC runs with files over a typical limit.
 * @then No space-based eviction.
 */
TEST_F( ResultGCTest, SpaceCap_DisabledKeepsAll )
{
    std::string data1MB( 1024 * 1024, 'Y' ); // 1 MB file
    createFileWithAge( resultsDir_ / "cid_big", data1MB, std::chrono::hours( 50 ) );

    size_t deleted = runGC( resultsDir_, /*retentionHours=*/100, /*maxMb=*/0 );

    EXPECT_EQ( deleted, 0u );
    EXPECT_TRUE( fs::exists( resultsDir_ / "cid_big" ) );
}

/**
 * @given Files all under both the TTL and space cap.
 * @when GC runs.
 * @then No eviction occurs.
 */
TEST_F( ResultGCTest, SpaceCap_UnderLimitNoEviction )
{
    std::string tinyData( 100, 'Z' ); // 100 bytes each
    createFileWithAge( resultsDir_ / "cid_tiny1", tinyData, std::chrono::hours( 1 ) );
    createFileWithAge( resultsDir_ / "cid_tiny2", tinyData, std::chrono::hours( 0 ) );

    size_t deleted = runGC( resultsDir_, /*retentionHours=*/100, /*maxMb=*/10 );

    EXPECT_EQ( deleted, 0u );
    EXPECT_TRUE( fs::exists( resultsDir_ / "cid_tiny1" ) );
    EXPECT_TRUE( fs::exists( resultsDir_ / "cid_tiny2" ) );
}
