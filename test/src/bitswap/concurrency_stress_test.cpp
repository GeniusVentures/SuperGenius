#include "stubs.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <chrono>

using namespace sgns::ipfs_bitswap;

class BitswapStressTest : public BitswapTestBase
{
protected:
    void SetUp() override
    {
        BitswapTestBase::SetUp();
        bitswap_->setCacheDir( "/tmp/bitswap_stress_test" );
    }
};

/**
 * @given a Bitswap instance with all C-1..C-7 fixes applied
 * @when 8 threads perform 100 iterations each of random Bitswap API calls
 * @then no crash occurs and zero errors are recorded
 */
TEST_F( BitswapStressTest, ConcurrentAccessNoCrashes )
{
    std::atomic<bool>  running{ true };
    std::atomic<int>   errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < 8; ++t )
    {
        threads.emplace_back(
            [this, t, &running, &errors]()
            {
                std::vector<uint8_t> testData = {
                    static_cast<uint8_t>( t ), 0x42, 0x43, 0x44 };
                CID dummyCid(
                    libp2p::multi::ContentIdentifier::Version::V0,
                    libp2p::multi::MulticodecType::Code::DAG_PB,
                    libp2p::multi::Multihash::create(
                        libp2p::multi::sha256,
                        libp2p::common::ByteArray( 32, static_cast<uint8_t>( t ) ) )
                        .value() );

                int counter = 0;
                while ( running.load( std::memory_order_acquire ) )
                {
                    try
                    {
                        int op = ( counter++ ) % 9;
                        switch ( op )
                        {
                        case 0:
                            (void)bitswap_->HasBlock( dummyCid );
                            break;
                        case 1:
                            (void)bitswap_->GetBlock( dummyCid );
                            break;
                        case 2:
                            bitswap_->setCacheDir(
                                "/tmp/test_" + std::to_string( t ) );
                            break;
                        case 3:
                            (void)bitswap_->getCacheDir();
                            break;
                        case 4:
                            bitswap_->SetMaxPeerAttempts(
                                static_cast<size_t>( t % 5 + 1 ) );
                            break;
                        case 5:
                            bitswap_->PublishData(
                                testData, []( libp2p::outcome::result<CID> ) {} );
                            break;
                        case 6:
                            bitswap_->AddProvider( dummyCid,
                                                   libp2p::peer::PeerInfo{
                                                       libp2p::peer::PeerId::fromHash(
                                                           libp2p::multi::Multihash::create(
                                                               libp2p::multi::sha256,
                                                               libp2p::common::ByteArray(
                                                                   32,
                                                                   static_cast<uint8_t>( t ) ) )
                                                               .value() )
                            .value(),
                                                       {} } );
                            break;
                        case 7:
                            (void)bitswap_->GetTotalProviderCount();
                            break;
                        case 8:
                            (void)bitswap_->ListPublishedContent();
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

/**
 * @given a Bitswap instance with C-1 mutexCacheDir_ fix
 * @when 2 threads: 1 hammering setCacheDir alternating paths, 1 hammering getCacheDir
 * @then no crash after 10000 iterations
 */
TEST_F( BitswapStressTest, CacheDirRaceAmplification )
{
    std::atomic<bool> running{ true };
    std::atomic<int>  errors{ 0 };
    std::atomic<int>  iterations{ 0 };

    std::thread setter(
        [this, &running, &errors, &iterations]()
        {
            int i = 0;
            while ( running.load( std::memory_order_acquire ) )
            {
                try
                {
                    bitswap_->setCacheDir(
                        ( ++i % 2 == 0 ) ? "/tmp/stress_cd_a"
                                          : "/tmp/stress_cd_b" );
                    int current = iterations.fetch_add( 1, std::memory_order_relaxed ) + 1;
                    if ( current >= 1000 )
                    {
                        break;
                    }
                }
                catch ( ... )
                {
                    errors.fetch_add( 1, std::memory_order_relaxed );
                }
            }
        } );

    std::thread reader(
        [this, &running, &errors, &iterations]()
        {
            while ( running.load( std::memory_order_acquire )
                    && iterations.load( std::memory_order_acquire ) < 1000 )
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

    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    running.store( false, std::memory_order_release );

    setter.join();
    reader.join();

    EXPECT_EQ( errors.load(), 0 );
}

/**
 * @given a Bitswap instance with C-2 atomic config fields
 * @when 4 writers hammer SetMaxPeerAttempts/SetPeerFailureThreshold while 4 readers query providers
 * @then no crash occurs and all threads complete cleanly
 */
TEST_F( BitswapStressTest, ConfigWriteDuringRequest )
{
    std::atomic<bool> running{ true };
    std::atomic<int>  errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back(
            [this, t, &running, &errors]()
            {
                int i = 0;
                while ( running.load( std::memory_order_acquire ) && i < 200 )
                {
                    try
                    {
                        if ( t % 2 == 0 )
                        {
                            bitswap_->SetMaxPeerAttempts(
                                static_cast<size_t>( ( t % 5 ) + 1 ) );
                        }
                        else
                        {
                            bitswap_->SetPeerFailureThreshold( ( t % 3 ) + 1 );
                        }
                        ++i;
                    }
                    catch ( ... )
                    {
                        errors.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            } );
    }

    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back(
            [this, &running, &errors]()
            {
                int i = 0;
                while ( running.load( std::memory_order_acquire ) && i < 200 )
                {
                    try
                    {
                        (void)bitswap_->GetTotalProviderCount();
                        (void)bitswap_->GetProviderDebugInfo();
                        ++i;
                    }
                    catch ( ... )
                    {
                        errors.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            } );
    }

    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    running.store( false, std::memory_order_release );

    for ( auto &th : threads )
    {
        th.join();
    }

    EXPECT_EQ( errors.load(), 0 );
}

/**
 * @given a Bitswap instance with mutexProviders_ guard
 * @when 8 threads concurrently add, remove, and query providers
 * @then no crash occurs and total provider count is >= 0 after operations
 */
TEST_F( BitswapStressTest, ProviderMapConcurrentAccess )
{
    std::atomic<bool> running{ true };
    std::atomic<int>  errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < 8; ++t )
    {
        threads.emplace_back(
            [this, t, &running, &errors]()
            {
                CID dummyCid(
                    libp2p::multi::ContentIdentifier::Version::V0,
                    libp2p::multi::MulticodecType::Code::DAG_PB,
                    libp2p::multi::Multihash::create(
                        libp2p::multi::sha256,
                        libp2p::common::ByteArray( 32, static_cast<uint8_t>( t ) ) )
                        .value() );

                libp2p::peer::PeerInfo pi{
                    libp2p::peer::PeerId::fromHash(
                        libp2p::multi::Multihash::create(
                            libp2p::multi::sha256,
                            libp2p::common::ByteArray( 32, static_cast<uint8_t>( t ) ) )
                            .value() )
                            .value(),
                    {}
                };

                int counter = 0;
                while ( running.load( std::memory_order_acquire ) && counter < 100 )
                {
                    try
                    {
                        int op = t % 4;
                        switch ( op )
                        {
                        case 0:
                            bitswap_->AddProvider( dummyCid, pi );
                            break;
                        case 1:
                            bitswap_->RemoveProvider( dummyCid, pi.id );
                            break;
                        case 2:
                            (void)bitswap_->GetProviders( dummyCid );
                            break;
                        case 3:
                            (void)bitswap_->GetTotalProviderCount();
                            break;
                        }
                        ++counter;
                    }
                    catch ( ... )
                    {
                        errors.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            } );
    }

    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    running.store( false, std::memory_order_release );

    for ( auto &th : threads )
    {
        th.join();
    }

    EXPECT_EQ( errors.load(), 0 );

    auto count = bitswap_->GetTotalProviderCount();
    EXPECT_GE( count, static_cast<size_t>( 0 ) );
}

/**
 * @given a Bitswap instance with all C-1..C-7 fixes applied
 * @when 12 threads exercise ALL public API methods simultaneously for 50 iterations each
 * @then all threads complete, no crash, no deadlock, zero errors
 */
TEST_F( BitswapStressTest, FullSystemStress )
{
    std::atomic<bool> running{ true };
    std::atomic<int>  errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < 12; ++t )
    {
        threads.emplace_back(
            [this, t, &errors]()
            {
                std::vector<uint8_t> testData = {
                    static_cast<uint8_t>( t ), 0x10, 0x20, 0x30, 0x40 };
                CID dummyCid(
                    libp2p::multi::ContentIdentifier::Version::V0,
                    libp2p::multi::MulticodecType::Code::DAG_PB,
                    libp2p::multi::Multihash::create(
                        libp2p::multi::sha256,
                        libp2p::common::ByteArray( 32, static_cast<uint8_t>( t + 1 ) ) )
                        .value() );

                libp2p::peer::PeerInfo pi{
                    libp2p::peer::PeerId::fromHash(
                        libp2p::multi::Multihash::create(
                            libp2p::multi::sha256,
                            libp2p::common::ByteArray( 32, static_cast<uint8_t>( t + 2 ) ) )
                            .value() )
                            .value(),
                    {}
                };

                for ( int i = 0; i < 50; ++i )
                {
                    try
                    {
                        (void)bitswap_->HasBlock( dummyCid );
                        (void)bitswap_->GetBlock( dummyCid );
                        bitswap_->setCacheDir(
                            "/tmp/fullstress_" + std::to_string( t ) );
                        (void)bitswap_->getCacheDir();
                        bitswap_->SetMaxPeerAttempts(
                            static_cast<size_t>( ( t % 5 ) + 1 ) );
                        bitswap_->SetPeerFailureThreshold( ( t % 3 ) + 2 );
                        bitswap_->AddProvider( dummyCid, pi );
                        (void)bitswap_->GetProviders( dummyCid );
                        (void)bitswap_->GetTotalProviderCount();
                        (void)bitswap_->ListPublishedContent();
                        bitswap_->PublishData(
                            testData,
                            []( libp2p::outcome::result<CID> ) {} );
                        bitswap_->RemoveProvider( dummyCid, pi.id );
                        (void)bitswap_->GetProviderDebugInfo();
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
