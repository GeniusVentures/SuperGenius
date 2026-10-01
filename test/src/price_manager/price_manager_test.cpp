/**
 * Source file for price_manager_test — hermetic LocalPriceManager suite
 * (TEST-02, first slice): FakePriceSource fixture + L1 behavior cases.
 * Zero sockets by construction: no HttpStubServer, no price_test_support,
 * no AsyncIOManager link — injected fakes only. No sleep_for anywhere:
 * every synchronization is a future (GetQuotes blocks) or the fake's
 * condition-variable predicate.
 */
#include <gtest/gtest.h>

#include <coinprices/IPriceSource.hpp>
#include <coinprices/LocalPriceManager.hpp>
#include <coinprices/PriceFetchError.hpp>
#include <coinprices/PriceQuote.hpp>
#include <coinprices/PriceRetryPolicy.hpp>
#include <coinprices/PriceFreshness.hpp>
#include <HTTPTypes.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    // Fixed epoch base for all timestamp arithmetic — no system_clock::now()
    // anywhere in this suite (hermetic, deterministic; the price_quote_test
    // kEpochBase pattern).
    const auto kEpochBase = std::chrono::system_clock::time_point{} + std::chrono::seconds( 1727712000 );

    /// @brief Scriptable, call-counting IPriceSource fake — the TEST-02 seam.
    /// FetchPrices runs on the manager's runner thread while test threads
    /// observe, so all state lives under the mutex; the cv supports
    /// wait-for-n-calls predicates.
    class FakePriceSource : public sgns::IPriceSource
    {
    public:
        struct Call
        {
            std::vector<std::string> ids;
            std::string              currency;
        };

        /// @brief Fixed result returned by every call (default: empty success).
        void SetResult( sgns::PriceResult<std::vector<sgns::PriceQuote>> r )
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            fixedResult_ = std::move( r );
            sequence_.clear();
        }

        /// @brief Per-call result sequence (last repeats) — for D-07 style
        /// scenarios in 03-02.
        void SetResults( std::vector<sgns::PriceResult<std::vector<sgns::PriceQuote>>> sequence )
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            sequence_ = std::move( sequence );
            fixedResult_.reset();
            synthetic_ = false;
        }

        /// @brief Succeed for ANY id set: synthesize one success quote per
        /// requested id (stamping the call's currency) on every call — the
        /// "succeed for any ids" scripting the concurrency/dedupe tests need.
        void SetSyntheticSuccess( double price = 1.0 )
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            synthetic_      = true;
            syntheticPrice_ = price;
            fixedResult_.reset();
            sequence_.clear();
        }

        /// @brief Park selected calls inside FetchPrices until released —
        /// the D-07 "in-flight" scenario. The gate runs on the manager's
        /// runner thread and receives only the zero-based call index; the
        /// test's lambda owns the latch (e.g. a shared promise/future pair).
        void SetCallGate( std::function<void( int callIndex )> gate )
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            gate_ = std::move( gate );
        }

        sgns::PriceResult<std::vector<sgns::PriceQuote>> FetchPrices( const std::vector<std::string> &ids,
                                                                      const std::string              &currency ) override
        {
            std::function<void( int )> gate;
            {
                std::lock_guard<std::mutex> lock( mutex_ );
                ++callCount_;
                calls_.push_back( Call{ ids, currency } );
                cv_.notify_all();
                gate = gate_;
            }
            if ( gate )
            {
                // Runs OUTSIDE the lock: the parked call must not block the
                // test thread from reading the recorded call state.
                gate( static_cast<int>( calls_.size() ) - 1 );
            }
            std::lock_guard<std::mutex> lock( mutex_ );
            const size_t callIndex = calls_.size() - 1;
            if ( synthetic_ )
            {
                std::vector<sgns::PriceQuote> quotes;
                for ( const auto &id : ids )
                {
                    sgns::PriceQuote quote;
                    quote.asset     = id;
                    quote.currency  = currency;
                    quote.price     = syntheticPrice_;
                    quote.source    = sgns::PriceSource::CoinGecko;
                    quote.stale     = false;
                    quotes.push_back( std::move( quote ) );
                }
                return outcome::success( std::move( quotes ) );
            }
            if ( !sequence_.empty() )
            {
                const size_t idx = callIndex < sequence_.size() ? callIndex : sequence_.size() - 1;
                return sequence_[idx];
            }
            if ( fixedResult_.has_value() )
            {
                return *fixedResult_;
            }
            return outcome::success( std::vector<sgns::PriceQuote>{} );
        }

        int CallCount()
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            return callCount_;
        }

        std::vector<Call> Calls()
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            return calls_;
        }

        /// @brief Wait until at least n calls have been recorded (house
        /// wait-condition template — cv + predicate, never sleep_for).
        bool WaitForCalls( size_t n, std::chrono::milliseconds timeout )
        {
            std::unique_lock<std::mutex> lock( mutex_ );
            return cv_.wait_for( lock, timeout, [&] { return callCount_ >= static_cast<int>( n ); } );
        }

        void Reset()
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            callCount_   = 0;
            calls_.clear();
        }

    private:

    private:
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        int                                                     callCount_ = 0;
        std::vector<Call>                                       calls_;
        std::optional<sgns::PriceResult<std::vector<sgns::PriceQuote>>> fixedResult_;
        std::vector<sgns::PriceResult<std::vector<sgns::PriceQuote>>>   sequence_;
        std::function<void( int )>                                     gate_;
        bool   synthetic_      = false;
        double syntheticPrice_ = 1.0;
    };
} // namespace

class LocalPriceManagerTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {}

    // Construct the manager INSIDE the test body scope, never as a
    // fixture-lifetime member: the manager must die before the fixture's
    // fakes and the clock lambda capture (destruction-order safety).
    sgns::LocalPriceManager MakeManager( std::chrono::milliseconds window = std::chrono::milliseconds( 0 ) )
    {
        return sgns::LocalPriceManager(
            tier1_, tier2_, window, [this]() { return now_; } );
    }

    sgns::PriceQuote MakeQuote( std::string id, double price )
    {
        sgns::PriceQuote quote;
        quote.asset     = std::move( id );
        quote.currency  = "usd";
        quote.price     = price;
        quote.timestamp = now_;
        quote.source    = sgns::PriceSource::CoinGecko;
        quote.stale     = false;
        return quote;
    }

    std::shared_ptr<FakePriceSource> tier1_ = std::make_shared<FakePriceSource>();
    std::shared_ptr<FakePriceSource> tier2_ = std::make_shared<FakePriceSource>();
    std::chrono::system_clock::time_point now_ = kEpochBase;
};

TEST_F( LocalPriceManagerTest, FreshL1HitServesWithZeroTierCalls )
{
    tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "genius-ai", 0.19 ) } ) );
    {
        auto manager = MakeManager();
        auto first   = manager.GetQuotes( { "genius-ai" }, "usd" );
        ASSERT_TRUE( first );
        ASSERT_EQ( first.value().size(), size_t{ 1 } );
        // Hold the fetch-time timestamp: the later L1 serve must keep it
        // byte-identically (D-14 — no timestamp re-basing on serve).
        const auto fetchedAt = first.value()[0].timestamp;
        tier1_->Reset();
        now_ += std::chrono::seconds( 30 );

        auto second = manager.GetQuotes( { "genius-ai" }, "usd" );
        ASSERT_TRUE( second );
        EXPECT_EQ( tier1_->CallCount(), 0 ); // zero network on an all-fresh hit (LPM-01)
        ASSERT_EQ( second.value().size(), size_t{ 1 } );
        EXPECT_EQ( second.value()[0].source, sgns::PriceSource::LocalCache );
        EXPECT_FALSE( second.value()[0].stale );
        EXPECT_DOUBLE_EQ( second.value()[0].price, 0.19 );
        EXPECT_EQ( second.value()[0].timestamp, fetchedAt );
    }
}

TEST_F( LocalPriceManagerTest, L1ExpiryRefetchesFromTier )
{
    tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "genius-ai", 0.19 ) } ) );
    {
        auto manager = MakeManager();
        auto first   = manager.GetQuotes( { "genius-ai" }, "usd" );
        ASSERT_TRUE( first );
        tier1_->Reset();
        now_ += std::chrono::seconds( 61 ); // past the 60s fresh band

        tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "genius-ai", 0.21 ) } ) );
        auto second = manager.GetQuotes( { "genius-ai" }, "usd" );
        ASSERT_TRUE( second );
        EXPECT_EQ( tier1_->CallCount(), 1 ); // stale entry refetches (LPM-01)
    }
}

TEST_F( LocalPriceManagerTest, ExactlySixtySecondsIsStillFresh )
{
    tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "genius-ai", 0.19 ) } ) );
    {
        auto manager = MakeManager();
        auto first   = manager.GetQuotes( { "genius-ai" }, "usd" );
        ASSERT_TRUE( first );
        tier1_->Reset();
        now_ += std::chrono::seconds( 60 ); // exactly the boundary: closed-on-fresh (D-16)

        auto second = manager.GetQuotes( { "genius-ai" }, "usd" );
        ASSERT_TRUE( second );
        EXPECT_EQ( tier1_->CallCount(), 0 );
    }
}

TEST_F( LocalPriceManagerTest, PartialL1HitFetchesOnlyMisses )
{
    tier1_->SetResult( outcome::success(
        std::vector<sgns::PriceQuote>{ MakeQuote( "a", 1.0 ), MakeQuote( "b", 2.0 ) } ) );
    {
        auto manager = MakeManager();
        auto first   = manager.GetQuotes( { "a", "b" }, "usd" );
        ASSERT_TRUE( first );
        tier1_->Reset();

        tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "c", 3.0 ) } ) );
        auto second = manager.GetQuotes( { "a", "c" }, "usd" );
        ASSERT_TRUE( second );
        ASSERT_EQ( tier1_->CallCount(), 1 );
        // Only the miss travels (D-06 partial-hit splitting).
        ASSERT_EQ( tier1_->Calls()[0].ids, ( std::vector<std::string>{ "c" } ) );
        ASSERT_EQ( second.value().size(), size_t{ 2 } );
        bool sawA = false, sawC = false;
        for ( const auto &quote : second.value() )
        {
            if ( quote.asset == "a" )
            {
                sawA = true;
                EXPECT_EQ( quote.source, sgns::PriceSource::LocalCache ); // fresh L1 subset
            }
            if ( quote.asset == "c" )
            {
                sawC = true;
                EXPECT_EQ( quote.source, sgns::PriceSource::CoinGecko ); // tier source preserved
            }
        }
        EXPECT_TRUE( sawA );
        EXPECT_TRUE( sawC );
    }
}

TEST_F( LocalPriceManagerTest, EmptyIdsReturnsEmptyInputWithoutPosting )
{
    auto manager = MakeManager();
    auto result  = manager.GetQuotes( {}, "usd" );
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error().code, sgns::PriceFetchError::EmptyInput );
    EXPECT_EQ( tier1_->CallCount(), 0 );
}

TEST_F( LocalPriceManagerTest, ConcurrentGetQuotesAcrossThreadsAllResolve )
{
    tier1_->SetSyntheticSuccess();
    auto manager = MakeManager();

    // Thread-safety smoke: 8 threads through one strand-serialized manager.
    // (FakePriceSource returns the same quote regardless of the requested id,
    // so every thread's result succeeds — strand serialization is the safety
    // mechanism under test, not per-id fidelity. PriceResult is not
    // default-constructible, so each thread records its own success bool.)
    std::vector<std::thread> threads;
    std::vector<bool>        ok( 8, false );
    for ( int i = 0; i < 8; ++i )
    {
        threads.emplace_back( [&, i]() {
            auto result  = manager.GetQuotes( { "id-" + std::to_string( i ) }, "usd" );
            ok[i]        = static_cast<bool>( result );
        } );
    }
    for ( auto &t : threads )
    {
        t.join();
    }
    for ( const auto &succeeded : ok )
    {
        EXPECT_TRUE( succeeded );
    }
}

TEST_F( LocalPriceManagerTest, ConstructDestroyLoopDoesNotHang )
{
    tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "genius-ai", 0.19 ) } ) );
    for ( int i = 0; i < 10; ++i )
    {
        auto manager = MakeManager();
        auto result  = manager.GetQuotes( { "genius-ai" }, "usd" );
        ASSERT_TRUE( result );
    }
    // The test completing IS the assertion (whole-suite timeout backs it):
    // 10 ctor/dtor cycles with the drain-then-join dtor prove clean teardown.
}

// ---- Coalescing cases (03-02 Task 1; window two-regime discipline) ----

TEST_F( LocalPriceManagerTest, NConcurrentRequestsCollapseIntoOneCall )
{
    tier1_->SetSyntheticSuccess();
    auto manager = MakeManager( std::chrono::milliseconds( 1000 ) );

    // 4 overlapping requests within the 1s window: two {bitcoin, ethereum},
    // one {ethereum, tether}, one {cardano}. The window must collapse them
    // into exactly ONE tier call covering the 4-id union (LPM-02).
    std::vector<std::thread> threads;
    std::vector<bool>        ok( 4, false );
    std::vector<size_t>      counts( 4, 0 );
    threads.emplace_back( [&]() {
        auto r  = manager.GetQuotes( { "bitcoin", "ethereum" }, "usd" );
        ok[0]   = static_cast<bool>( r );
        counts[0] = r ? r.value().size() : 0;
    } );
    threads.emplace_back( [&]() {
        auto r  = manager.GetQuotes( { "bitcoin", "ethereum" }, "usd" );
        ok[1]   = static_cast<bool>( r );
        counts[1] = r ? r.value().size() : 0;
    } );
    threads.emplace_back( [&]() {
        auto r  = manager.GetQuotes( { "ethereum", "tether" }, "usd" );
        ok[2]   = static_cast<bool>( r );
        counts[2] = r ? r.value().size() : 0;
    } );
    threads.emplace_back( [&]() {
        auto r  = manager.GetQuotes( { "cardano" }, "usd" );
        ok[3]   = static_cast<bool>( r );
        counts[3] = r ? r.value().size() : 0;
    } );
    for ( auto &t : threads )
    {
        t.join();
    }

    ASSERT_EQ( tier1_->CallCount(), 1 );
    const auto calls = tier1_->Calls();
    // Set-equality on the union (order-independent).
    std::set<std::string> received( calls[0].ids.begin(), calls[0].ids.end() );
    const std::set<std::string> expected{ "bitcoin", "ethereum", "tether", "cardano" };
    EXPECT_EQ( received, expected );
    // Every waiter receives exactly its requested subset: 2/2/2/1.
    EXPECT_TRUE( ok[0] );
    EXPECT_EQ( counts[0], size_t{ 2 } );
    EXPECT_TRUE( ok[1] );
    EXPECT_EQ( counts[1], size_t{ 2 } );
    EXPECT_TRUE( ok[2] );
    EXPECT_EQ( counts[2], size_t{ 2 } );
    EXPECT_TRUE( ok[3] );
    EXPECT_EQ( counts[3], size_t{ 1 } );
}

TEST_F( LocalPriceManagerTest, EachWaiterReceivesItsSubsetWithCorrectSource )
{
    tier1_->SetSyntheticSuccess();
    auto manager = MakeManager( std::chrono::milliseconds( 1000 ) );

    std::thread t1( [&]() { (void) manager.GetQuotes( { "a", "b" }, "usd" ); } );
    std::thread t2( [&]() { (void) manager.GetQuotes( { "b", "c" }, "usd" ); } );
    t1.join();
    t2.join();

    ASSERT_EQ( tier1_->CallCount(), 1 );
    // Quotes served straight after a fetch keep the TIER's source per the
    // serving-source rule (only cache service rewrites to LocalCache).
    const auto calls = tier1_->Calls();
    std::set<std::string> received( calls[0].ids.begin(), calls[0].ids.end() );
    const std::set<std::string> expected{ "a", "b", "c" };
    EXPECT_EQ( received, expected );
}

TEST_F( LocalPriceManagerTest, MultiIdRequestIsOneBatch )
{
    tier1_->SetSyntheticSuccess();
    auto manager = MakeManager( std::chrono::milliseconds( 0 ) );

    auto result = manager.GetQuotes( { "a", "b", "c" }, "usd" );
    ASSERT_TRUE( result );
    ASSERT_EQ( tier1_->CallCount(), 1 ); // LPM-03: one call, all ids in one batch
    EXPECT_EQ( tier1_->Calls()[0].ids, ( std::vector<std::string>{ "a", "b", "c" } ) );
}

TEST_F( LocalPriceManagerTest, PerCurrencyWindowsAreSeparate )
{
    tier1_->SetSyntheticSuccess();
    auto manager = MakeManager( std::chrono::milliseconds( 1000 ) );

    std::thread ta( [&]() { (void) manager.GetQuotes( { "x" }, "usd" ); } );
    std::thread tb( [&]() { (void) manager.GetQuotes( { "x" }, "eur" ); } );
    ta.join();
    tb.join();

    // D-08: windows are keyed by currency — never a shared batch.
    ASSERT_EQ( tier1_->CallCount(), 2 );
    const auto calls = tier1_->Calls();
    EXPECT_NE( calls[0].currency, calls[1].currency );
}

TEST_F( LocalPriceManagerTest, NewMissesDuringInflightOpenNewWindow )
{
    tier1_->SetSyntheticSuccess();
    // Call 1 parks on the promise until released; call 2 returns instantly.
    auto release = std::make_shared<std::promise<void>>();
    auto released = release->get_future().share();
    tier1_->SetCallGate( [released]( int callIndex ) {
        if ( callIndex == 0 )
        {
            released.wait(); // park call 1 (0-based index) until the test releases
        }
    } );
    auto manager = MakeManager( std::chrono::milliseconds( 0 ) );

    std::thread a( [&]() { (void) manager.GetQuotes( { "p" }, "usd" ); } );
    // Wait until call 1 is parked in the gate, THEN post request q: it must
    // not join call 1's batch (dispatch already began — the window was moved
    // out before the walk); it lands in a NEW window that dispatches only
    // after the in-flight walk completes (D-07).
    ASSERT_TRUE( tier1_->WaitForCalls( 1, std::chrono::milliseconds( 2000 ) ) );
    std::thread b( [&]() { (void) manager.GetQuotes( { "q" }, "usd" ); } );

    release->set_value();
    a.join();
    b.join();

    ASSERT_EQ( tier1_->CallCount(), 2 );
    const auto calls = tier1_->Calls();
    EXPECT_EQ( calls[0].ids, ( std::vector<std::string>{ "p" } ) );
    EXPECT_EQ( calls[1].ids, ( std::vector<std::string>{ "q" } ) ); // disjoint batches — no id fetched twice
}

TEST_F( LocalPriceManagerTest, FirstCallerPaysTheWindowCost )
{
    tier1_->SetSyntheticSuccess();
    auto manager = MakeManager( std::chrono::milliseconds( 1000 ) );

    // Assert on cause, not wall time: the single request resolves
    // successfully with exactly 1 tier call after the window fires — the
    // future IS the synchronization (D-05's accepted first-caller cost).
    auto result = manager.GetQuotes( { "solo" }, "usd" );
    ASSERT_TRUE( result );
    ASSERT_EQ( result.value().size(), size_t{ 1 } );
    EXPECT_EQ( tier1_->CallCount(), 1 );
}

// ---- Fallback-chain cases (03-03 Task 3; LPM-04, FRESH-01/02) ----

namespace
{
    sgns::PriceFetchFailure TimeoutFailure()
    {
        return sgns::PriceFetchFailure{ sgns::PriceFetchError::NetworkError, 0,
                                        sgns::http::ClientError::TIMEOUT };
    }
} // namespace

TEST_F( LocalPriceManagerTest, Tier1WholesaleFailureEscalatesFullMissSetToTier2 )
{
    tier1_->SetResult( outcome::failure( sgns::PriceFetchFailure{ sgns::PriceFetchError::Blocked, 403 } ) );
    tier2_->SetSyntheticSuccess();
    auto manager = MakeManager();

    auto result = manager.GetQuotes( { "a", "b" }, "usd" );
    ASSERT_TRUE( result );
    EXPECT_EQ( tier1_->CallCount(), 1 );
    ASSERT_EQ( tier2_->CallCount(), 1 ); // full miss-set escalates (D-09)
    EXPECT_EQ( tier2_->Calls()[0].ids, ( std::vector<std::string>{ "a", "b" } ) );
}

TEST_F( LocalPriceManagerTest, Tier1PartialSuccessGapChasesOnlyMissingIds )
{
    tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "a", 1.0 ) } ) );
    tier2_->SetSyntheticSuccess();
    auto manager = MakeManager();

    auto result = manager.GetQuotes( { "a", "b" }, "usd" );
    ASSERT_TRUE( result );
    ASSERT_EQ( tier2_->CallCount(), 1 );
    EXPECT_EQ( tier2_->Calls()[0].ids, ( std::vector<std::string>{ "b" } ) ); // gap-chase only the gap
    // completed id a never re-fetched at tier 2
    const auto tier2Ids = tier2_->Calls()[0].ids;
    EXPECT_EQ( std::find( tier2Ids.begin(), tier2Ids.end(), "a" ), tier2Ids.end() );
}

TEST_F( LocalPriceManagerTest, BothTiersFailStaleEntryServesLastKnownGood )
{
    {
        tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "x", 0.42 ) } ) );
        auto manager = MakeManager();
        auto primed  = manager.GetQuotes( { "x" }, "usd" );
        ASSERT_TRUE( primed );
        const auto storedTimestamp = primed.value()[0].timestamp;

        now_ += std::chrono::seconds( 120 ); // StaleButUsable band
        tier1_->SetResult( outcome::failure( TimeoutFailure() ) );
        tier2_->SetResult( outcome::failure( TimeoutFailure() ) );

        auto result = manager.GetQuotes( { "x" }, "usd" );
        ASSERT_TRUE( result ); // SUCCESS — LKG serve (D-10)
        ASSERT_EQ( result.value().size(), size_t{ 1 } );
        EXPECT_EQ( result.value()[0].source, sgns::PriceSource::LocalCache );
        EXPECT_TRUE( result.value()[0].stale );
        EXPECT_DOUBLE_EQ( result.value()[0].price, 0.42 );
        EXPECT_EQ( result.value()[0].timestamp, storedTimestamp ); // D-11: unchanged
    }
}

TEST_F( LocalPriceManagerTest, BothTiersFailOverFiveMinutesIsUnavailable )
{
    {
        tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "x", 0.42 ) } ) );
        auto manager = MakeManager();
        ASSERT_TRUE( manager.GetQuotes( { "x" }, "usd" ) );

        now_ += std::chrono::seconds( 301 ); // Unavailable band
        tier1_->SetResult( outcome::failure( TimeoutFailure() ) );
        tier2_->SetResult( outcome::failure( TimeoutFailure() ) );

        auto result = manager.GetQuotes( { "x" }, "usd" );
        ASSERT_FALSE( result ); // never served from the unavailable band (FRESH-02)
    }
}

TEST_F( LocalPriceManagerTest, ExactlyThreeHundredSecondsStillServesLastKnownGood )
{
    {
        tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "x", 0.42 ) } ) );
        auto manager = MakeManager();
        ASSERT_TRUE( manager.GetQuotes( { "x" }, "usd" ) );

        now_ += std::chrono::seconds( 300 ); // exactly the boundary: closed-on-stale (D-16)
        tier1_->SetResult( outcome::failure( TimeoutFailure() ) );
        tier2_->SetResult( outcome::failure( TimeoutFailure() ) );

        auto result = manager.GetQuotes( { "x" }, "usd" );
        ASSERT_TRUE( result );
        ASSERT_EQ( result.value().size(), size_t{ 1 } );
        EXPECT_TRUE( result.value()[0].stale );
    }
}

TEST_F( LocalPriceManagerTest, MixedFreshImmediateAndStaleLkgAssembly )
{
    {
        // Staggered priming: s enters L1 first, ages into StaleButUsable;
        // then f enters L1 fresh.
        tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "s", 2.0 ) } ) );
        auto manager = MakeManager();
        ASSERT_TRUE( manager.GetQuotes( { "s" }, "usd" ) );
        now_ += std::chrono::seconds( 120 ); // s ages into StaleButUsable
        tier1_->SetResult( outcome::success( std::vector<sgns::PriceQuote>{ MakeQuote( "f", 1.0 ) } ) );
        ASSERT_TRUE( manager.GetQuotes( { "f" }, "usd" ) );

        tier1_->Reset();
        tier2_->Reset();
        tier1_->SetResult( outcome::failure( TimeoutFailure() ) );
        tier2_->SetResult( outcome::failure( TimeoutFailure() ) );

        // One assembly, both service modes: f fresh-immediate rides along
        // (D-06), s serves as LKG (D-10).
        auto result = manager.GetQuotes( { "f", "s" }, "usd" );
        ASSERT_TRUE( result );
        ASSERT_EQ( result.value().size(), size_t{ 2 } );
        for ( const auto &quote : result.value() )
        {
            EXPECT_EQ( quote.source, sgns::PriceSource::LocalCache );
            if ( quote.asset == "f" )
            {
                EXPECT_FALSE( quote.stale );
            }
            if ( quote.asset == "s" )
            {
                EXPECT_TRUE( quote.stale );
            }
        }
    }
}

TEST_F( LocalPriceManagerTest, Blocked403NeverRetriesTier1AndGoesStraightToTier2 )
{
    tier1_->SetResult( outcome::failure( sgns::PriceFetchFailure{ sgns::PriceFetchError::Blocked, 403 } ) );
    tier2_->SetSyntheticSuccess();
    auto manager = MakeManager();

    auto result = manager.GetQuotes( { "a" }, "usd" );
    ASSERT_TRUE( result );
    EXPECT_EQ( tier1_->CallCount(), 1 ); // 403 escalates immediately, never re-queried
    EXPECT_EQ( tier2_->CallCount(), 1 );
}

TEST_F( LocalPriceManagerTest, NothingServableSurfacesLastTierFailure )
{
    tier1_->SetResult( outcome::failure( sgns::PriceFetchFailure{ sgns::PriceFetchError::HttpStatus, 502 } ) );
    tier2_->SetResult( outcome::failure( sgns::PriceFetchFailure{ sgns::PriceFetchError::HttpStatus, 502 } ) );
    auto manager = MakeManager();

    auto result = manager.GetQuotes( { "z" }, "usd" );
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error().code, sgns::PriceFetchError::HttpStatus );
    EXPECT_EQ( result.error().httpStatus, unsigned{ 502 } ); // last tier failure surfaced
}
