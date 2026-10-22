/**
 * @file       bounded_defer_test.cpp
 * @brief      Unit tests for base::BoundedDeferTracker (D-03 mechanism) with an
 *             injected clock — no sleep_for anywhere.
 *
 * Adversarial+compat discipline (VER-01): each case names the misbehavior the
 * mechanism must resist (deadline refresh by replay, flood-flush via cap,
 * lost oldest-first order) and the compatible behavior honest consumers need.
 */
#include "base/bounded_defer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using sgns::base::BoundedDeferTracker;

    /// Deterministic clock: tests advance time by assignment, never by sleeping.
    class InjectableClock
    {
    public:
        BoundedDeferTracker::TimePoint Now() const
        {
            return current_;
        }

        void Advance( std::chrono::seconds by )
        {
            current_ += by;
        }

    private:
        // Offset from the steady-clock epoch so a zero time_point never coincides
        // with "unset" sentinel habits elsewhere; value itself is arbitrary.
        BoundedDeferTracker::TimePoint current_{ std::chrono::hours( 12 ) };
    };

    class BoundedDeferTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            tracker_ = std::make_unique<BoundedDeferTracker>( std::chrono::minutes( 30 ),
                                                              4,
                                                              [this] { return clock_.Now(); } );
        }

        InjectableClock                      clock_;
        std::unique_ptr<BoundedDeferTracker> tracker_;
    };

    // Behavior: Defer(key) then Expired(key) is false before the window elapses
    // and true after the injected clock advances past it.
    TEST_F( BoundedDeferTest, ExpiredFlipsOnlyAfterWindowElapses )
    {
        EXPECT_FALSE( tracker_->Defer( "burn-slot-1" ) );
        clock_.Advance( std::chrono::minutes( 29 ) + std::chrono::seconds( 59 ) );
        EXPECT_FALSE( tracker_->Expired( "burn-slot-1" ) );
        clock_.Advance( std::chrono::seconds( 1 ) );
        EXPECT_TRUE( tracker_->Expired( "burn-slot-1" ) );
    }

    // Adversarial: a key deferred once keeps its ORIGINAL deadline across
    // repeated Defer calls — replay must not refresh (extend) the holdback.
    TEST_F( BoundedDeferTest, RepeatedDeferKeepsFirstSeenDeadline )
    {
        EXPECT_FALSE( tracker_->Defer( "replayed-key" ) );
        clock_.Advance( std::chrono::minutes( 29 ) );
        EXPECT_FALSE( tracker_->Defer( "replayed-key" ) ); // still pending, no refresh
        clock_.Advance( std::chrono::minutes( 2 ) );       // past the ORIGINAL 30 min
        EXPECT_TRUE( tracker_->Defer( "replayed-key" ) );  // expired under first-seen deadline
        EXPECT_TRUE( tracker_->Expired( "replayed-key" ) );
    }

    // Adversarial: at cap, inserting a new key evicts the oldest key only; the
    // newest survives — flooding cannot flush younger entries wholesale.
    TEST_F( BoundedDeferTest, CapEvictsOldestKeyOnly )
    {
        tracker_ = std::make_unique<BoundedDeferTracker>( std::chrono::minutes( 30 ),
                                                          2,
                                                          [this] { return clock_.Now(); } );
        EXPECT_FALSE( tracker_->Defer( "oldest" ) );
        clock_.Advance( std::chrono::seconds( 1 ) );
        EXPECT_FALSE( tracker_->Defer( "middle" ) );
        EXPECT_EQ( tracker_->Size(), 2U );
        clock_.Advance( std::chrono::seconds( 1 ) );
        EXPECT_FALSE( tracker_->Defer( "newest" ) );
        EXPECT_EQ( tracker_->Size(), 2U );
        EXPECT_FALSE( tracker_->Expired( "oldest" ) ); // evicted -> untracked
        EXPECT_FALSE( tracker_->Expired( "middle" ) ); // middle still tracked, pending
        clock_.Advance( std::chrono::minutes( 31 ) );
        EXPECT_TRUE( tracker_->Expired( "middle" ) );
        EXPECT_TRUE( tracker_->Expired( "newest" ) ); // newest survived the eviction
    }

    // Compat: an evicted key re-deferred starts a FRESH deadline (it is
    // untracked, not poisoned by its previous life).
    TEST_F( BoundedDeferTest, EvictedKeyRedeferStartsFreshDeadline )
    {
        tracker_ = std::make_unique<BoundedDeferTracker>( std::chrono::minutes( 30 ),
                                                          1,
                                                          [this] { return clock_.Now(); } );
        EXPECT_FALSE( tracker_->Defer( "first" ) );
        clock_.Advance( std::chrono::minutes( 29 ) );
        EXPECT_FALSE( tracker_->Defer( "second" ) ); // evicts "first"
        EXPECT_EQ( tracker_->Size(), 1U );
        // "first" was evicted 1 minute before its original deadline; deferring it
        // again must anchor a NEW 30-minute window, not resurrect the old one.
        EXPECT_FALSE( tracker_->Defer( "first" ) ); // evicts "second"
        EXPECT_FALSE( tracker_->Expired( "first" ) );
        clock_.Advance( std::chrono::minutes( 29 ) + std::chrono::seconds( 59 ) );
        EXPECT_FALSE( tracker_->Expired( "first" ) );
        clock_.Advance( std::chrono::seconds( 1 ) );
        EXPECT_TRUE( tracker_->Expired( "first" ) );
    }

    // Behavior: Clear(key) removes tracking; the key defers anew afterwards.
    TEST_F( BoundedDeferTest, ClearRemovesTrackingAndNextDeferReanchors )
    {
        EXPECT_FALSE( tracker_->Defer( "cleared" ) );
        clock_.Advance( std::chrono::minutes( 10 ) );
        tracker_->Clear( "cleared" );
        EXPECT_EQ( tracker_->Size(), 0U );
        EXPECT_FALSE( tracker_->Expired( "cleared" ) ); // untracked -> not expired
        clock_.Advance( std::chrono::minutes( 25 ) );   // past the original deadline
        EXPECT_FALSE( tracker_->Expired( "cleared" ) );
        EXPECT_FALSE( tracker_->Defer( "cleared" ) ); // fresh 30-minute window
        clock_.Advance( std::chrono::minutes( 29 ) );
        EXPECT_FALSE( tracker_->Expired( "cleared" ) );
        clock_.Advance( std::chrono::minutes( 1 ) );
        EXPECT_TRUE( tracker_->Expired( "cleared" ) );
    }

    // Behavior: SetWindow applies to keys deferred AFTER the call; existing
    // first-seen deadlines are untouched.
    TEST_F( BoundedDeferTest, SetWindowAppliesOnlyToNewlyDeferredKeys )
    {
        EXPECT_FALSE( tracker_->Defer( "before-set" ) );
        tracker_->SetWindow( std::chrono::minutes( 1 ) );
        EXPECT_FALSE( tracker_->Defer( "after-set" ) );
        clock_.Advance( std::chrono::seconds( 61 ) );
        EXPECT_FALSE( tracker_->Expired( "before-set" ) ); // still on its 30-minute window
        EXPECT_TRUE( tracker_->Expired( "after-set" ) );
    }

    // Behavior: documented defaults — window 30 minutes, cap 4096 (D-03 user
    // emphasis: generous window; memory backstop cap).
    TEST_F( BoundedDeferTest, DefaultWindowAndCapConstants )
    {
        EXPECT_EQ( BoundedDeferTracker::kDefaultDeferWindow, std::chrono::minutes( 30 ) );
        EXPECT_EQ( BoundedDeferTracker::kDefaultMaxTrackedEntries, 4096U );
        BoundedDeferTracker default_constructed;
        EXPECT_EQ( default_constructed.Size(), 0U );
    }

    // Adversarial (thread-safety smoke): concurrent Defer/Expired/Clear/Size
    // from multiple threads must not corrupt the table (internal mutex).
    TEST_F( BoundedDeferTest, ConcurrentAccessKeepsTableConsistent )
    {
        constexpr unsigned       kThreads   = 4;
        constexpr unsigned       kPerThread = 500;
        std::vector<std::thread> workers;
        for ( unsigned t = 0; t < kThreads; ++t )
        {
            workers.emplace_back(
                [this, t]
                {
                    for ( unsigned i = 0; i < kPerThread; ++i )
                    {
                        const auto key = "k-" + std::to_string( t ) + "-" + std::to_string( i );
                        (void) tracker_->Defer( key );
                        (void) tracker_->Expired( key );
                        if ( i % 7 == 0 )
                        {
                            tracker_->Clear( key );
                        }
                        (void) tracker_->Size();
                    }
                } );
        }
        for ( auto &worker : workers )
        {
            worker.join();
        }
        // 4*500 minus every 7th cleared per thread stays bounded by the cap (4).
        EXPECT_LE( tracker_->Size(), 4U );
    }
} // namespace
