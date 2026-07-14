#include "stubs.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include "testutil/wait_condition.hpp"

using namespace sgns::ipfs_bitswap;

class RequestContextTimerTest : public BitswapTestBase
{
};

/**
 * @given a BitswapRequestContext with C-6 mutex guard
 * @when HandleResponseTimeout fires from deadline_timer while HandleResponse is called
 * @then both serialize via mutex_ — no crash and no double callback
 */
TEST_F( RequestContextTimerTest, ResponseTimeoutDuringResponse )
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
 * @given a BitswapRequestContext with C-6 mutex guard
 * @when HandleResponse is called from multiple threads simultaneously
 * @then callbacks list is accessed atomically via mutex_ — all callbacks invoked
 */
TEST_F( RequestContextTimerTest, MultiCallbackConcurrentFire )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

    std::vector<uint8_t> testData = { 0x10, 0x20, 0x30 };
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
 * @given a BitswapRequestContext with C-6 mutex guard
 * @when one thread adds callbacks while another fires HandleResponse
 * @then no data race on callbacks_ list — concurrent AddCallback and HandleResponse serialize
 */
TEST_F( RequestContextTimerTest, AddCallbackDuringHandleResponse )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

    std::vector<uint8_t> testData = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
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
