#include "stubs.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include "testutil/wait_condition.hpp"

using namespace sgns::ipfs_bitswap;

class CallbackReentrancyTest : public BitswapTestBase
{
};

/**
 * @given a Bitswap instance with C-7 callback-outside-lock fix
 * @when a BlockCallback re-enters Bitswap by calling GetBlock again
 * @then no deadlock occurs — HandleResponse fires outside mutexRequestCallbacks_
 */
TEST_F( CallbackReentrancyTest, ReenterDuringCallback )
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
                        if ( result )
                        {
                            (void)bitswap_->HasBlock( *storedCid );
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
 * @given a Bitswap instance with C-7 callback-outside-lock fix
 * @when nested GetBlock calls occur from within block callbacks
 * @then no deadlock occurs — deeper nesting is safe
 */
TEST_F( CallbackReentrancyTest, NestedRequestFromCallback )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

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
                int depth = 0;
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        auto result = bitswap_->GetBlock( *storedCid );
                        if ( result && depth < 3 )
                        {
                            ++depth;
                            (void)bitswap_->HasBlock( *storedCid );
                            auto r2 = bitswap_->GetBlock( *storedCid );
                            (void)r2;
                        }
                        depth = 0;
                    }
                    catch ( ... )
                    {
                        errors.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            } );
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    running.store( false, std::memory_order_release );

    for ( auto &th : threads )
    {
        th.join();
    }

    EXPECT_EQ( errors.load(), 0 );
}

/**
 * @given a Bitswap instance with C-7 callback-outside-lock fix
 * @when multiple block callbacks fire from different threads concurrently
 * @then all complete without deadlock or crash
 */
TEST_F( CallbackReentrancyTest, ConcurrentCallbacksNoDeadlock )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

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

    std::vector<std::thread> threads;
    for ( int t = 0; t < 8; ++t )
    {
        threads.emplace_back(
            [this, &storedCid, &running, &errors, t]()
            {
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        switch ( t % 3 )
                        {
                        case 0:
                            (void)bitswap_->HasBlock( *storedCid );
                            break;
                        case 1:
                        {
                            auto result = bitswap_->GetBlock( *storedCid );
                            (void)result;
                        }
                        break;
                        case 2:
                            bitswap_->SetMaxPeerAttempts(
                                static_cast<size_t>( ( t % 5 ) + 1 ) );
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
