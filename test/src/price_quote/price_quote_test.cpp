#include <gtest/gtest.h>
#include <coinprices/PriceQuote.hpp>
#include <coinprices/PriceFreshness.hpp>

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
    // Fixed epoch base for all timestamp arithmetic — no system_clock::now()
    // anywhere in this suite (hermetic, deterministic).
    const auto kEpochBase = std::chrono::system_clock::time_point{} + std::chrono::seconds( 1727712000 );
} // namespace

class PriceQuoteTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {}
};

TEST_F( PriceQuoteTest, DefaultConstruction )
{
    const sgns::PriceQuote quote;
    EXPECT_DOUBLE_EQ( quote.price, 0.0 );
    EXPECT_FALSE( quote.stale );
    EXPECT_EQ( quote.source, sgns::PriceSource::CoinGecko );
    EXPECT_TRUE( quote.asset.empty() );
    EXPECT_TRUE( quote.currency.empty() );
}

TEST_F( PriceQuoteTest, AllFieldsSet )
{
    sgns::PriceQuote quote;
    quote.asset     = "genius-ai";
    quote.currency  = "usd";
    quote.price     = 0.42;
    quote.timestamp = kEpochBase;
    quote.source    = sgns::PriceSource::GnusPriceService;
    quote.stale     = true;

    EXPECT_EQ( quote.asset, "genius-ai" );
    EXPECT_EQ( quote.currency, "usd" );
    EXPECT_DOUBLE_EQ( quote.price, 0.42 );
    EXPECT_EQ( quote.timestamp, kEpochBase );
    EXPECT_EQ( quote.source, sgns::PriceSource::GnusPriceService );
    EXPECT_TRUE( quote.stale );
}

TEST_F( PriceQuoteTest, PriceSourceEnumeratorsAreDistinct )
{
    const auto local     = static_cast<int>( sgns::PriceSource::LocalCache );
    const auto gecko     = static_cast<int>( sgns::PriceSource::CoinGecko );
    const auto gnus      = static_cast<int>( sgns::PriceSource::GnusPriceService );
    const auto onChain   = static_cast<int>( sgns::PriceSource::OnChain );

    EXPECT_NE( local, gecko );
    EXPECT_NE( local, gnus );
    EXPECT_NE( local, onChain );
    EXPECT_NE( gecko, gnus );
    EXPECT_NE( gecko, onChain );
    EXPECT_NE( gnus, onChain );
}

TEST_F( PriceQuoteTest, FetchedAtEpochSecondsRoundTrip )
{
    sgns::PriceQuote quote;
    quote.timestamp = std::chrono::system_clock::time_point{} + std::chrono::seconds( 1727712000 );
    EXPECT_EQ( quote.FetchedAtEpochSeconds(), int64_t{ 1727712000 } );
}

TEST_F( PriceQuoteTest, FetchedAtEpochSecondsTruncatesSubSecond )
{
    sgns::PriceQuote quote;
    quote.timestamp = kEpochBase + std::chrono::milliseconds( 1500 );
    EXPECT_EQ( quote.FetchedAtEpochSeconds(), int64_t{ 1727712001 } );
}

class PriceFreshnessTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {}
};

TEST_F( PriceFreshnessTest, BandConstantsMatchD16 )
{
    EXPECT_EQ( sgns::kFreshMaxAge, std::chrono::seconds( 60 ) );
    EXPECT_EQ( sgns::kStaleMaxAge, std::chrono::seconds( 300 ) );
}

TEST_F( PriceFreshnessTest, BoundaryTable )
{
    // D-16 closed-on-fresh boundaries: exactly 60s is Fresh, exactly 300s is
    // StaleButUsable. Expected bands derive from the shared constants, not
    // magic numbers.
    const std::vector<std::pair<std::chrono::milliseconds, sgns::FreshnessBand>> cases = {
        // Fresh: age <= kFreshMaxAge (60s)
        { std::chrono::milliseconds( 0 ), sgns::FreshnessBand::Fresh },
        { std::chrono::milliseconds( 1 ), sgns::FreshnessBand::Fresh },
        { std::chrono::milliseconds( 59'000 ), sgns::FreshnessBand::Fresh },
        { std::chrono::milliseconds( 59'999 ), sgns::FreshnessBand::Fresh },
        { sgns::kFreshMaxAge, sgns::FreshnessBand::Fresh },
        // StaleButUsable: kFreshMaxAge < age <= kStaleMaxAge (300s)
        { sgns::kFreshMaxAge + std::chrono::milliseconds( 1 ), sgns::FreshnessBand::StaleButUsable },
        { std::chrono::seconds( 61 ), sgns::FreshnessBand::StaleButUsable },
        { std::chrono::seconds( 299 ), sgns::FreshnessBand::StaleButUsable },
        { sgns::kStaleMaxAge, sgns::FreshnessBand::StaleButUsable },
        // Unavailable: age > kStaleMaxAge
        { sgns::kStaleMaxAge + std::chrono::milliseconds( 1 ), sgns::FreshnessBand::Unavailable },
        { std::chrono::seconds( 301 ), sgns::FreshnessBand::Unavailable },
        { std::chrono::seconds( 3600 ), sgns::FreshnessBand::Unavailable },
    };

    const auto now = kEpochBase + std::chrono::hours( 24 );
    for ( const auto& [age, expected] : cases )
    {
        const auto band = sgns::ClassifyFreshness( now - age, now );
        EXPECT_EQ( band, expected ) << "ageMs=" << age.count();
    }
}

TEST_F( PriceFreshnessTest, ZeroAgeIsFresh )
{
    const auto now = kEpochBase;
    EXPECT_EQ( sgns::ClassifyFreshness( now, now ), sgns::FreshnessBand::Fresh );
}
