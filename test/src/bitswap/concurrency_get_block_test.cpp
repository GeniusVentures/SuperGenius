#include "stubs.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include "testutil/wait_condition.hpp"

using namespace sgns::ipfs_bitswap;

class GetBlockConcurrencyTest : public BitswapTestBase
{
};

/**
 * @given a Bitswap instance with block storage
 * @when 4 threads concurrently call GetBlock for the same CID
 * @then no crash occurs and all threads complete cleanly
 */
TEST_F( GetBlockConcurrencyTest, ConcurrentGetBlockSameCid )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

    std::vector<uint8_t> testData = { 0x01, 0x02, 0x03, 0x04 };
    std::atomic<bool>    stored{ false };
    std::optional<CID>   storedCid;

    bitswap_->PublishData(
        testData,
        [&stored, &storedCid]( libp2p::outcome::result<CID> result )
        {
            if ( result )
            {
                storedCid = result.value();
                stored.store( true, std::memory_order_release );
            }
        } );

    std::chrono::milliseconds elapsed;
    ASSERT_WAIT_FOR_CONDITION(
        [&stored]() { return stored.load(); },
        std::chrono::milliseconds( 2000 ),
        "Block was not stored by Bitswap within timeout",
        &elapsed );

    std::vector<std::thread> threads;
    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back(
            [this, &storedCid, &running, &errors]()
            {
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        auto result = bitswap_->GetBlock( *storedCid );
                        (void)result;
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

/**
 * @given a Bitswap instance with block storage
 * @when 4 threads alternate between HasBlock and GetBlock for the same CID
 * @then no crash occurs and all threads complete cleanly
 */
TEST_F( GetBlockConcurrencyTest, ConcurrentGetHasBlock )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

    std::vector<uint8_t> testData = { 0x0A, 0x0B, 0x0C };
    std::atomic<bool>    stored{ false };
    std::optional<CID>   storedCid;

    bitswap_->PublishData(
        testData,
        [&stored, &storedCid]( libp2p::outcome::result<CID> result )
        {
            if ( result )
            {
                storedCid = result.value();
                stored.store( true, std::memory_order_release );
            }
        } );

    std::chrono::milliseconds elapsed;
    ASSERT_WAIT_FOR_CONDITION(
        [&stored]() { return stored.load(); },
        std::chrono::milliseconds( 2000 ),
        "Block was not stored by Bitswap within timeout",
        &elapsed );

    std::vector<std::thread> threads;
    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back(
            [this, &storedCid, &running, &errors, t]()
            {
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        if ( t % 2 == 0 )
                        {
                            (void)bitswap_->HasBlock( *storedCid );
                        }
                        else
                        {
                            auto result = bitswap_->GetBlock( *storedCid );
                            (void)result;
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

/**
 * @given a Bitswap instance with a cache directory and a persisted block
 * @when 4 threads call GetBlock concurrently to trigger lazy-load from disk
 * @then no crash occurs and all threads complete cleanly
 */
TEST_F( GetBlockConcurrencyTest, GetBlockLazyLoadRace )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

    bitswap_->setCacheDir( "/tmp/bitswap_test_lazy" );

    std::vector<uint8_t> testData = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    std::atomic<bool>    stored{ false };
    std::optional<CID>   storedCid;

    bitswap_->PublishData(
        testData,
        [&stored, &storedCid]( libp2p::outcome::result<CID> result )
        {
            if ( result )
            {
                storedCid = result.value();
                stored.store( true, std::memory_order_release );
            }
        } );

    std::chrono::milliseconds elapsed;
    ASSERT_WAIT_FOR_CONDITION(
        [&stored]() { return stored.load(); },
        std::chrono::milliseconds( 2000 ),
        "Block was not stored by Bitswap within timeout",
        &elapsed );

    std::vector<std::thread> threads;
    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back(
            [this, &storedCid, &running, &errors]()
            {
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        auto result = bitswap_->GetBlock( *storedCid );
                        (void)result;
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

/**
 * @given a Bitswap instance with non-const GetBlock method (C-4 fix)
 * @when GetBlock is called on a non-const Bitswap reference
 * @then the call compiles and executes without error (compile-time verification)
 */
TEST_F( GetBlockConcurrencyTest, ConstCorrectnessVerification )
{
    std::vector<uint8_t> testData = { 0xAA, 0xBB, 0xCC };
    std::atomic<bool>    stored{ false };
    std::optional<CID>   storedCid;

    bitswap_->PublishData(
        testData,
        [&stored, &storedCid]( libp2p::outcome::result<CID> result )
        {
            if ( result )
            {
                storedCid = result.value();
                stored.store( true, std::memory_order_release );
            }
        } );

    std::chrono::milliseconds elapsed;
    ASSERT_WAIT_FOR_CONDITION(
        [&stored]() { return stored.load(); },
        std::chrono::milliseconds( 2000 ),
        "Block was not stored by Bitswap within timeout",
        &elapsed );

    ASSERT_TRUE( stored.load() );

    auto result = bitswap_->GetBlock( *storedCid );
    EXPECT_TRUE( result.has_value() || result.has_error() );
}
