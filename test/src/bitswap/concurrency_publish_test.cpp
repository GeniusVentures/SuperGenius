#include "stubs.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

using namespace sgns::ipfs_bitswap;

class PublishConcurrencyTest : public BitswapTestBase
{
};

/**
 * @given a Bitswap instance with io_context::post-based PublishFile
 * @when PublishFile is called with a test file path
 * @then the callback fires via io_context post without crash
 */
TEST_F( PublishConcurrencyTest, PublishFileCallbackOnIoContext )
{
    std::atomic<bool> callbackFired{ false };

    bitswap_->PublishFile(
        "/tmp/test_file.txt",
        [&callbackFired]( libp2p::outcome::result<CID> )
        {
            callbackFired.store( true, std::memory_order_release );
        } );

    for ( int i = 0; i < 100; ++i )
    {
        if ( callbackFired.load( std::memory_order_acquire ) )
        {
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }

    EXPECT_TRUE( callbackFired.load() );
}

/**
 * @given a Bitswap instance with io_context::post-based PublishDirectory
 * @when PublishDirectory is called with a test directory path
 * @then the callback fires via io_context post without crash
 */
TEST_F( PublishConcurrencyTest, PublishDirectoryCallbackOnIoContext )
{
    std::atomic<bool> callbackFired{ false };

    bitswap_->PublishDirectory(
        "/tmp/test_dir",
        [&callbackFired]( libp2p::outcome::result<CID> )
        {
            callbackFired.store( true, std::memory_order_release );
        } );

    for ( int i = 0; i < 100; ++i )
    {
        if ( callbackFired.load( std::memory_order_acquire ) )
        {
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }

    EXPECT_TRUE( callbackFired.load() );
}

/**
 * @given a Bitswap instance with synchronous PublishData
 * @when PublishData is called with test data
 * @then the callback fires synchronously without crash
 */
TEST_F( PublishConcurrencyTest, PublishDataCallbackInvoked )
{
    std::atomic<bool> callbackFired{ false };
    std::vector<uint8_t> testData = { 0x01, 0x02, 0x03 };

    bitswap_->PublishData(
        testData,
        [&callbackFired]( libp2p::outcome::result<CID> )
        {
            callbackFired.store( true, std::memory_order_release );
        } );

    EXPECT_TRUE( callbackFired.load() );
}

/**
 * @given a Bitswap instance with both sync and async publish methods
 * @when 4 threads call PublishData and 2 threads call PublishFile concurrently
 * @then all threads complete and all PublishFile callbacks fire
 */
TEST_F( PublishConcurrencyTest, MultiConcurrentPublish )
{
    std::atomic<int>  asyncCallbacksFired{ 0 };
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };
    std::vector<std::thread> threads;

    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back(
            [this, &errors, &running, t]()
            {
                std::vector<uint8_t> data = {
                    static_cast<uint8_t>( t ), 0x42, 0x43 };
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        bitswap_->PublishData(
                            data, []( libp2p::outcome::result<CID> ) {} );
                    }
                    catch ( ... )
                    {
                        errors.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            } );
    }

    for ( int t = 0; t < 2; ++t )
    {
        threads.emplace_back(
            [this, &asyncCallbacksFired, &errors, &running]()
            {
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        bitswap_->PublishFile(
                            "/tmp/test_async_file.txt",
                            [&asyncCallbacksFired]( libp2p::outcome::result<CID> )
                            {
                                asyncCallbacksFired.fetch_add(
                                    1, std::memory_order_relaxed );
                            } );
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
    EXPECT_GT( asyncCallbacksFired.load(), 0 );
}
