#include "stubs.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>

using namespace sgns::ipfs_bitswap;

class ConfigConcurrencyTest : public BitswapTestBase
{
};

/**
 * @given a Bitswap instance with atomic configuration fields
 * @when 4 threads concurrently call SetMaxPeerAttempts with different values
 * @then no crash occurs and all threads complete cleanly
 */
TEST_F( ConfigConcurrencyTest, ConcurrentSetAndRead_NoCrash )
{
    std::atomic<int> errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back(
            [this, t, &errors]()
            {
                for ( int i = 0; i < 5000; ++i )
                {
                    try
                    {
                        bitswap_->SetMaxPeerAttempts(
                            static_cast<size_t>( t + 1 ) );
                    }
                    catch ( ... )
                    {
                        errors.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            } );
    }

    for ( auto &th : threads )
    {
        th.join();
    }

    EXPECT_EQ( errors.load(), 0 );
}

/**
 * @given a Bitswap instance with atomic config and mutex-guarded providers
 * @when 2 writer threads set config values while 2 reader threads query providers
 * @then no crash occurs and all threads complete cleanly
 */
TEST_F( ConfigConcurrencyTest, WriteDuringRead_ConsistentValues )
{
    std::atomic<int> errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < 2; ++t )
    {
        threads.emplace_back(
            [this, t, &errors]()
            {
                for ( int i = 0; i < 2000; ++i )
                {
                    try
                    {
                        if ( t == 0 )
                        {
                            bitswap_->SetMaxPeerAttempts(
                                static_cast<size_t>( 5 ) );
                        }
                        else
                        {
                            bitswap_->SetPeerFailureThreshold( 5 );
                        }
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
            [this, &errors]()
            {
                for ( int i = 0; i < 2000; ++i )
                {
                    try
                    {
                        (void)bitswap_->GetTotalProviderCount();
                    }
                    catch ( ... )
                    {
                        errors.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            } );
    }

    for ( auto &th : threads )
    {
        th.join();
    }

    EXPECT_EQ( errors.load(), 0 );
}

/**
 * @given a Bitswap instance with concurrent-safe configuration API
 * @when 8 threads hammer SetMaxPeerAttempts, SetPeerFailureThreshold,
 *      GetTotalProviderCount, and GetProviderDebugInfo concurrently
 * @then no crash occurs and all threads complete cleanly
 */
TEST_F( ConfigConcurrencyTest, StressConfigAccess )
{
    std::atomic<int> errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < 8; ++t )
    {
        threads.emplace_back(
            [this, t, &errors]()
            {
                for ( int i = 0; i < 1000; ++i )
                {
                    try
                    {
                        int op = ( t + i ) % 4;
                        switch ( op )
                        {
                        case 0:
                            bitswap_->SetMaxPeerAttempts(
                                static_cast<size_t>( ( t % 5 ) + 1 ) );
                            break;
                        case 1:
                            bitswap_->SetPeerFailureThreshold( ( t % 3 ) + 1 );
                            break;
                        case 2:
                            (void)bitswap_->GetTotalProviderCount();
                            break;
                        case 3:
                            (void)bitswap_->GetProviderDebugInfo();
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

    for ( auto &th : threads )
    {
        th.join();
    }

    EXPECT_EQ( errors.load(), 0 );
}
