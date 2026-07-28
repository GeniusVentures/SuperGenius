/**
 * @file consensus_burn_reservation_test.cpp
 * @brief Deterministic persistent-storage harness for Phase 11 burn reservations.
 */

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "base/buffer.hpp"
#include "blockchain/ConsensusStateStore.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "testutil/storage/base_crdt_test.hpp"

namespace sgns
{
    /**
     * Test-only surface reserved for private Phase 11 hooks. Production hooks
     * added by later plans must friend this class rather than becoming public.
     */
    class ConsensusBurnReservationTestAccess
    {
    public:
        static constexpr std::string_view Scope()
        {
            return "slot-owned bridge burn reservation";
        }
    };
} // namespace sgns

namespace
{
    constexpr auto kFixedSystemNow =
        std::chrono::system_clock::time_point( std::chrono::milliseconds( 1'750'000'000'000LL ) );
    constexpr auto kFixedSteadyNow =
        std::chrono::steady_clock::time_point( std::chrono::milliseconds( 126'000 ) );

    enum class HarnessCaseGroup : uint8_t
    {
        Store,
        Admission,
        Restart,
        Final,
        Application,
        Race,
        Stale,
        ABA,
        Horizon,
    };

    constexpr std::array<std::string_view, 9> kHarnessCaseGroups = {
        "Store", "Admission", "Restart", "Final", "Application",
        "Race", "Stale", "ABA", "Horizon"
    };

    sgns::base::Buffer BufferOf( std::string_view value )
    {
        sgns::base::Buffer buffer;
        buffer.put( value );
        return buffer;
    }

    class ScopedHookReset
    {
    public:
        explicit ScopedHookReset( std::function<void()> reset ) : reset_( std::move( reset ) ) {}

        ScopedHookReset( const ScopedHookReset & )            = delete;
        ScopedHookReset &operator=( const ScopedHookReset & ) = delete;

        ~ScopedHookReset()
        {
            if ( reset_ )
            {
                reset_();
            }
        }

    private:
        std::function<void()> reset_;
    };

    class PredicateBarrier
    {
    public:
        void ArriveAndWait()
        {
            std::unique_lock lock( mutex_ );
            arrived_ = true;
            condition_.notify_all();
            condition_.wait( lock, [this]() { return released_; } );
        }

        void WaitUntilArrived()
        {
            std::unique_lock lock( mutex_ );
            condition_.wait( lock, [this]() { return arrived_; } );
        }

        void Release()
        {
            std::lock_guard lock( mutex_ );
            released_ = true;
            condition_.notify_all();
        }

    private:
        std::mutex              mutex_;
        std::condition_variable condition_;
        bool                    arrived_{ false };
        bool                    released_{ false };
    };

    class ScopedWorker
    {
    public:
        ScopedWorker( std::thread worker, PredicateBarrier &barrier )
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
        std::thread       worker_;
        PredicateBarrier &barrier_;
    };

    struct BurnReservationCounters
    {
        std::atomic<uint64_t> subscribe{ 0 };
        std::atomic<uint64_t> timer{ 0 };
        std::atomic<uint64_t> replay{ 0 };
        std::atomic<uint64_t> admission{ 0 };
        std::atomic<uint64_t> handler{ 0 };
        std::atomic<uint64_t> cleanup{ 0 };
        std::atomic<uint64_t> application{ 0 };

        void Reset()
        {
            subscribe.store( 0 );
            timer.store( 0 );
            replay.store( 0 );
            admission.store( 0 );
            handler.store( 0 );
            cleanup.store( 0 );
            application.store( 0 );
        }
    };

    class ConsensusBurnReservationHarness : public test::CRDTFixture
    {
    public:
        ConsensusBurnReservationHarness() : CRDTFixture( "consensus_burn_reservation_test" ) {}

    protected:
        std::shared_ptr<sgns::storage::rocksdb> CloseOwnersAndReopenSamePath()
        {
            const auto exact_path = db_path_;
            db_->ShutdownNow();
            db_.reset();

            auto reopened = sgns::storage::rocksdb::create( exact_path );
            EXPECT_TRUE( reopened.has_value() );
            return reopened ? reopened.value() : nullptr;
        }

        sgns::storage::rocksdb::QueryResult InspectRaw(
            const std::shared_ptr<sgns::storage::rocksdb> &storage,
            std::string_view prefix )
        {
            auto raw = storage->query( BufferOf( prefix ) );
            EXPECT_TRUE( raw.has_value() );
            return raw ? raw.value() : sgns::storage::rocksdb::QueryResult{};
        }

        BurnReservationCounters counters_;
        const std::chrono::system_clock::time_point system_now_{ kFixedSystemNow };
        const std::chrono::steady_clock::time_point steady_now_{ kFixedSteadyNow };
    };
} // namespace

TEST_F( ConsensusBurnReservationHarness, PersistentDatabaseAndBarrierReopenCleanly )
{
    const ScopedHookReset reset_hooks( [this]() { counters_.Reset(); } );
    ASSERT_EQ( kHarnessCaseGroups.size(), 9U );
    EXPECT_EQ( static_cast<uint8_t>( HarnessCaseGroup::Horizon ), 8U );
    EXPECT_EQ( sgns::ConsensusBurnReservationTestAccess::Scope(),
               "slot-owned bridge burn reservation" );
    ASSERT_LT( steady_now_.time_since_epoch(), system_now_.time_since_epoch() );

    auto datastore = db_->GetDataStore();
    ASSERT_TRUE( datastore );
    sgns::ConsensusStateStore store( datastore );

    sgns::ConsensusStateStore::ProcessRecord marker;
    marker.set_schema_version( 2 );
    marker.set_state( sgns::ConsensusStateStore::ProcessRecord::PENDING );
    marker.set_slot_id( std::string( 64, '1' ) );
    marker.set_certificate_digest( std::string( 64, '2' ) );
    marker.set_proposal_id( std::string( 64, '3' ) );
    marker.set_winner_id( "phase-11-harness-marker" );
    marker.set_updated_at_ms( 1'750'000'000'000ULL );
    ASSERT_TRUE( store.PutPendingProcess( marker ).has_value() );

    const auto process_key = sgns::ConsensusStateStore::ProcessKey( marker.slot_id() );
    const auto raw_before  = InspectRaw( datastore, process_key );
    ASSERT_EQ( raw_before.size(), 1U );
    datastore.reset();

    auto reopened = CloseOwnersAndReopenSamePath();
    ASSERT_TRUE( reopened );
    sgns::ConsensusStateStore reopened_store( reopened );
    auto restored = reopened_store.GetProcess( marker.slot_id() );
    ASSERT_TRUE( restored.has_value() );
    ASSERT_TRUE( restored.value().has_value() );
    EXPECT_EQ( restored.value()->winner_id(), marker.winner_id() );
    EXPECT_EQ( InspectRaw( reopened, process_key ).size(), 1U );

    PredicateBarrier barrier;
    std::atomic<bool> worker_finished{ false };
    ScopedWorker worker(
        std::thread(
            [&]()
            {
                ++counters_.handler;
                barrier.ArriveAndWait();
                worker_finished.store( true );
            } ),
        barrier );

    barrier.WaitUntilArrived();
    EXPECT_FALSE( worker_finished.load() );
    EXPECT_EQ( counters_.handler.load(), 1U );
    barrier.Release();
    worker.Join();
    EXPECT_TRUE( worker_finished.load() );
}
