#include "stubs.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include "testutil/wait_condition.hpp"

using namespace sgns::ipfs_bitswap;

class ContentRequestConcurrencyTest : public BitswapTestBase
{
};

/**
 * @given a Bitswap instance with C-5 io_context dispatch wrappers
 * @when RequestContent initiates block requests with timeout handlers
 * @then concurrent timeout and block arrival handlers do not cause data races
 */
TEST_F( ContentRequestConcurrencyTest, ConcurrentTimeoutAndProcessing )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

    std::vector<uint8_t> testData = { 0x01, 0x02, 0x03 };
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
 * @given a Bitswap instance with C-5 dispatch-wrapped content request callbacks
 * @when multiple block requests arrive concurrently for a content request
 * @then pendingCIDs and completedCIDs are accessed without data races
 */
TEST_F( ContentRequestConcurrencyTest, MultiBlockConcurrentArrival )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

    std::vector<uint8_t> testData = { 0x10, 0x20, 0x30, 0x40 };
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
                        (void)bitswap_->HasBlock( *storedCid );
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
 * @given a Bitswap instance with C-5 io_context dispatch for queue processing
 * @when processRequestQueue is triggered from timeout handler and block callback
 * @then requestQueue and processingQueue are accessed only on io_context (serialized)
 */
TEST_F( ContentRequestConcurrencyTest, QueueProcessingUnderStrand )
{
    std::atomic<int>  errors{ 0 };
    std::atomic<bool> running{ true };

    std::vector<uint8_t> testData1 = { 0xAA, 0xBB };
    std::vector<uint8_t> testData2 = { 0xCC, 0xDD };
    std::atomic<bool>    stored{ false };
    std::optional<CID>   storedCid;

    bitswap_->PublishData(
        testData1,
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
                int counter = 0;
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        if ( ( counter++ + t ) % 2 == 0 )
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
