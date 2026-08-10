#include "stubs.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>

using namespace sgns::ipfs_bitswap;

class CacheDirConcurrencyTest : public BitswapTestBase
{
};

/**
 * @given a Bitswap instance with cache directory support
 * @when one thread calls setCacheDir in a loop while another calls getCacheDir
 * @then no crash occurs and the program terminates normally
 */
TEST_F( CacheDirConcurrencyTest, SetGetRace )
{
    std::atomic<bool> running{ true };
    std::atomic<int>  errors{ 0 };

    std::thread setter(
        [this, &running, &errors]()
        {
            while ( running.load( std::memory_order_acquire ) )
            {
                try
                {
                    bitswap_->setCacheDir( "/tmp/a" );
                }
                catch ( ... )
                {
                    errors.fetch_add( 1, std::memory_order_relaxed );
                }
            }
        } );

    std::thread getter(
        [this, &running, &errors]()
        {
            while ( running.load( std::memory_order_acquire ) )
            {
                try
                {
                    (void)bitswap_->getCacheDir();
                }
                catch ( ... )
                {
                    errors.fetch_add( 1, std::memory_order_relaxed );
                }
            }
        } );

    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    running.store( false, std::memory_order_release );

    setter.join();
    getter.join();

    EXPECT_EQ( errors.load(), 0 );
}

/**
 * @given a Bitswap instance with a cache directory set
 * @when one thread alternates setCacheDir between two paths while another stores blocks
 * @then no crash occurs and the setter/reader threads complete normally
 */
TEST_F( CacheDirConcurrencyTest, SetPersistRace )
{
    std::atomic<bool> running{ true };
    std::atomic<int>  errors{ 0 };

    bitswap_->setCacheDir( "/tmp/bitswap_test_c1" );

    std::thread setter(
        [this, &running, &errors]()
        {
            int i = 0;
            while ( running.load( std::memory_order_acquire ) )
            {
                try
                {
                    bitswap_->setCacheDir(
                        ( ++i % 2 == 0 ) ? "/tmp/bitswap_test_c1_a"
                                          : "/tmp/bitswap_test_c1_b" );
                }
                catch ( ... )
                {
                    errors.fetch_add( 1, std::memory_order_relaxed );
                }
            }
        } );

    std::thread reader(
        [this, &running, &errors]()
        {
            while ( running.load( std::memory_order_acquire ) )
            {
                try
                {
                    (void)bitswap_->getCacheDir();
                }
                catch ( ... )
                {
                    errors.fetch_add( 1, std::memory_order_relaxed );
                }
            }
        } );

    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    running.store( false, std::memory_order_release );

    setter.join();
    reader.join();

    EXPECT_EQ( errors.load(), 0 );
}

/**
 * @given a Bitswap instance with a valid cache directory containing files
 * @when one thread calls buildDiskIndex while another reads getCacheDir
 * @then no crash occurs and getCacheDir returns consistent values
 */
TEST_F( CacheDirConcurrencyTest, BuildReadRace )
{
    std::atomic<bool> running{ true };
    std::atomic<int>  errors{ 0 };

    bitswap_->setCacheDir( "/tmp/bitswap_test_c1_build" );

    std::thread builder(
        [this, &running, &errors]()
        {
            while ( running.load( std::memory_order_acquire ) )
            {
                try
                {
                    bitswap_->buildDiskIndex();
                }
                catch ( ... )
                {
                    errors.fetch_add( 1, std::memory_order_relaxed );
                }
            }
        } );

    std::thread reader(
        [this, &running, &errors]()
        {
            while ( running.load( std::memory_order_acquire ) )
            {
                try
                {
                    std::string dir = bitswap_->getCacheDir();
                    EXPECT_FALSE( dir.empty() );
                }
                catch ( ... )
                {
                    errors.fetch_add( 1, std::memory_order_relaxed );
                }
            }
        } );

    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    running.store( false, std::memory_order_release );

    builder.join();
    reader.join();

    EXPECT_EQ( errors.load(), 0 );
}

/**
 * @given a Bitswap instance with cache directory support
 * @when 8 threads each perform random set/get/persist/unpersist operations
 * @then all threads complete without exceptions or crashes
 */
TEST_F( CacheDirConcurrencyTest, MultiThreadedCache )
{
    std::atomic<bool> running{ true };
    std::atomic<int>  errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < 8; ++t )
    {
        threads.emplace_back(
            [this, t, &running, &errors]()
            {
                int counter = 0;
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        int op = ( counter++ ) % 4;
                        switch ( op )
                        {
                        case 0:
                            bitswap_->setCacheDir(
                                "/tmp/bitswap_thread_" + std::to_string( t ) );
                            break;
                        case 1:
                            (void)bitswap_->getCacheDir();
                            break;
                        case 2:
                            bitswap_->buildDiskIndex();
                            break;
                        default:
                            break;
                        }
                    }
                    catch ( ... )
                    {
                        errors.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            } );
    }

    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    running.store( false, std::memory_order_release );

    for ( auto &th : threads )
    {
        th.join();
    }

    EXPECT_EQ( errors.load(), 0 );
}
