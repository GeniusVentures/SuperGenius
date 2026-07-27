/**
 * @file consensus_finalization_test.cpp
 * @brief Deterministic multi-ingress harness for Phase 10 finalization tests.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "blockchain/Consensus.hpp"
#include "testutil/storage/base_crdt_test.hpp"

namespace sgns
{
    /** Friend-only access surface reserved for later finalization hooks. */
    class ConsensusFinalizationTestAccess
    {
    public:
        static constexpr std::string_view Scope()
        {
            return "consensus finalization state machine";
        }
    };
} // namespace sgns

namespace
{
    constexpr auto kSystemClockNow =
        std::chrono::system_clock::time_point( std::chrono::milliseconds( 1'750'000'000'000LL ) );
    constexpr auto kSteadyClockNow =
        std::chrono::steady_clock::time_point( std::chrono::milliseconds( 84'000 ) );

    class ScopedReset
    {
    public:
        explicit ScopedReset( std::function<void()> reset ) : reset_( std::move( reset ) ) {}

        ScopedReset( const ScopedReset & )            = delete;
        ScopedReset &operator=( const ScopedReset & ) = delete;

        ~ScopedReset()
        {
            if ( reset_ )
            {
                reset_();
            }
        }

    private:
        std::function<void()> reset_;
    };

    class DeterministicBarrier
    {
    public:
        void ArriveAndWait()
        {
            std::unique_lock lock( mutex_ );
            arrived_ = true;
            cv_.notify_all();
            cv_.wait( lock, [this]() { return released_; } );
        }

        void WaitUntilArrived()
        {
            std::unique_lock lock( mutex_ );
            cv_.wait( lock, [this]() { return arrived_; } );
        }

        void Release()
        {
            std::lock_guard lock( mutex_ );
            released_ = true;
            cv_.notify_all();
        }

    private:
        std::mutex              mutex_;
        std::condition_variable cv_;
        bool                    arrived_{ false };
        bool                    released_{ false };
    };

    class ScopedWorker
    {
    public:
        ScopedWorker( std::thread worker, DeterministicBarrier &barrier )
            : worker_( std::move( worker ) ), barrier_( barrier )
        {
        }

        ScopedWorker( const ScopedWorker & )            = delete;
        ScopedWorker &operator=( const ScopedWorker & ) = delete;

        ~ScopedWorker()
        {
            barrier_.Release();
            Join();
        }

        void Join()
        {
            if ( worker_.joinable() )
            {
                worker_.join();
            }
        }

    private:
        std::thread           worker_;
        DeterministicBarrier &barrier_;
    };

    struct FinalizationCounters
    {
        std::atomic<uint64_t> signer{ 0 };
        std::atomic<uint64_t> raw_publish{ 0 };
        std::atomic<uint64_t> handler{ 0 };
        std::atomic<uint64_t> cleanup{ 0 };
        std::atomic<uint64_t> subscription{ 0 };
        std::atomic<uint64_t> timer{ 0 };
        std::atomic<uint64_t> certificate_filter{ 0 };
        std::atomic<uint64_t> durable_certificate{ 0 };
        std::atomic<uint64_t> processing{ 0 };
        std::atomic<uint64_t> conflict{ 0 };
        std::atomic<uint64_t> publication{ 0 };

        void Reset()
        {
            signer.store( 0 );
            raw_publish.store( 0 );
            handler.store( 0 );
            cleanup.store( 0 );
            subscription.store( 0 );
            timer.store( 0 );
            certificate_filter.store( 0 );
            durable_certificate.store( 0 );
            processing.store( 0 );
            conflict.store( 0 );
            publication.store( 0 );
        }
    };

    class OrderedEvents
    {
    public:
        void Record( std::string event )
        {
            std::lock_guard lock( mutex_ );
            events_.push_back( std::move( event ) );
        }

        std::vector<std::string> Snapshot() const
        {
            std::lock_guard lock( mutex_ );
            return events_;
        }

        void Reset()
        {
            std::lock_guard lock( mutex_ );
            events_.clear();
        }

    private:
        mutable std::mutex       mutex_;
        std::vector<std::string> events_;
    };

    class ConsensusFinalizationHarness : public test::CRDTFixture
    {
    public:
        ConsensusFinalizationHarness() : CRDTFixture( "consensus_finalization_test" ) {}

    protected:
        FinalizationCounters counters_;
        OrderedEvents        events_;
        const std::chrono::system_clock::time_point system_clock_now_{ kSystemClockNow };
        const std::chrono::steady_clock::time_point steady_clock_now_{ kSteadyClockNow };
    };
} // namespace

TEST_F( ConsensusFinalizationHarness, ConcurrentBarrierJoinsCleanly )
{
    const ScopedReset reset_harness(
        [this]()
        {
            counters_.Reset();
            events_.Reset();
        } );
    ASSERT_LT( steady_clock_now_.time_since_epoch(), system_clock_now_.time_since_epoch() );

    DeterministicBarrier barrier;
    std::atomic<bool>     worker_finished{ false };
    ScopedWorker worker(
        std::thread(
            [&]()
            {
                events_.Record( "handler-blocked" );
                ++counters_.handler;
                barrier.ArriveAndWait();
                events_.Record( "handler-released" );
                worker_finished.store( true );
            } ),
        barrier );

    barrier.WaitUntilArrived();
    EXPECT_FALSE( worker_finished.load() );
    EXPECT_EQ( counters_.handler.load(), 1U );
    barrier.Release();
    worker.Join();

    EXPECT_TRUE( worker_finished.load() );
    EXPECT_EQ( events_.Snapshot(),
               ( std::vector<std::string>{ "handler-blocked", "handler-released" } ) );
}
