/**
 * @file account_storage_file_test.cpp
 * @brief Tests the on-disk account list (secure_storage_id) survives a failed rewrite.
 */
#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "account/GeniusAccount.hpp"

#ifndef _WIN32
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

using namespace sgns;

namespace
{
    // 128 lowercase hex chars: the exact shape GeniusAccount::IsValidPublicKey accepts.
    std::string MakePublicKey( unsigned seed )
    {
        std::string key;
        key.reserve( 128 );
        for ( unsigned i = 0; i < 64; ++i )
        {
            char byte[4];
            std::snprintf( byte, sizeof( byte ), "%02x", ( seed + i ) & 0xff );
            key += byte;
        }
        return key;
    }

    boost::filesystem::path MakeTempDir()
    {
        static std::atomic<unsigned> counter{ 0 };
        const auto unique = std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() % 1000000 )
                          + "_" + std::to_string( counter.fetch_add( 1 ) );
        auto dir = boost::filesystem::temp_directory_path() / ( "acct_storage_" + unique );
        boost::filesystem::create_directories( dir );
        return dir;
    }

    void SeedStorageFile( const boost::filesystem::path &base_dir, const std::vector<std::string> &keys )
    {
        std::ofstream file( ( base_dir / "secure_storage_id" ).string(), std::ios::trunc );
        for ( const auto &key : keys )
        {
            file << key << '\n';
        }
    }
} // namespace

#ifndef _WIN32

// A rewrite that cannot finish (here: RLIMIT_FSIZE cuts the stream mid-write,
// SIGXFSZ ignored) must report failure and leave the previous account list
// fully intact, instead of truncating the live file and returning success.
TEST( AccountStorageFile, FailedRewriteLeavesAccountListIntact )
{
    const auto key_a = MakePublicKey( 0x11 );
    const auto key_b = MakePublicKey( 0x22 );
    auto        dir  = MakeTempDir();
    SeedStorageFile( dir, { key_a, key_b } );

    // Cap file writes at 100 bytes; the rewritten list needs 258. SIGXFSZ's
    // default action kills the process, so ignore it and let the write fail.
    rlimit original_limit{};
    ASSERT_EQ( getrlimit( RLIMIT_FSIZE, &original_limit ), 0 );
    const auto original_handler = signal( SIGXFSZ, SIG_IGN );

    rlimit capped_limit = original_limit;
    capped_limit.rlim_cur = 100;
    ASSERT_EQ( setrlimit( RLIMIT_FSIZE, &capped_limit ), 0 );

    const auto failed_delete = GeniusAccount::DeleteAccount( key_b, dir );

    ASSERT_EQ( setrlimit( RLIMIT_FSIZE, &original_limit ), 0 );
    signal( SIGXFSZ, original_handler );

    ASSERT_TRUE( failed_delete.has_error() ) << "a truncated write must not be reported as success";

    const auto accounts = GeniusAccount::GetAvailableAccounts( dir );
    ASSERT_EQ( accounts.size(), 2u ) << "the account list must survive a failed rewrite";
    ASSERT_EQ( accounts[0], key_a );
    ASSERT_EQ( accounts[1], key_b );

    // The file is still consistent: retrying the delete once the disk allows it works.
    ASSERT_TRUE( GeniusAccount::DeleteAccount( key_b, dir ).has_value() );
    const auto remaining = GeniusAccount::GetAvailableAccounts( dir );
    ASSERT_EQ( remaining.size(), 1u );
    ASSERT_EQ( remaining[0], key_a );

    boost::system::error_code ec;
    boost::filesystem::remove_all( dir, ec );
}

#else

TEST( AccountStorageFile, FailedRewriteLeavesAccountListIntact )
{
    GTEST_SKIP() << "RLIMIT_FSIZE harness is POSIX-only";
}

#endif
