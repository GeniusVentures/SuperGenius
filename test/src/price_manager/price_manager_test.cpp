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

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
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

        sgns::PriceResult<std::vector<sgns::PriceQuote>> FetchPrices( const std::vector<std::string> &ids,
                                                                      const std::string              &currency ) override
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            ++callCount_;
            calls_.push_back( Call{ ids, currency } );
            cv_.notify_all();
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
                const size_t idx = calls_.size() - 1 < sequence_.size() ? calls_.size() - 1 : sequence_.size() - 1;
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
