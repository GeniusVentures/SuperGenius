#include <gtest/gtest.h>
#include "account/TokenAmount.hpp"
#include "base/fixed_point.hpp"
#include "outcome/outcome.hpp"

using outcome::result;
using sgns::TokenAmount;

TEST( ParseMinionsTest, ValidStrings )
{
    auto r1 = TokenAmount::ParseMinions( "0.000001" );
    ASSERT_TRUE( r1.has_value() );
    EXPECT_EQ( r1.value(), 1ULL );

    auto r2 = TokenAmount::ParseMinions( "123.456789" );
    ASSERT_TRUE( r2.has_value() );
    EXPECT_EQ( r2.value(), 123456789ULL );
}

TEST( ParseMinionsTest, InvalidStrings )
{
    auto r = TokenAmount::ParseMinions( "not_a_number" );
    EXPECT_TRUE( r.has_error() );
}

TEST( FormatMinionsTest, BasicValues )
{
    EXPECT_EQ( TokenAmount::FormatMinions( 0 ), "0.000000" );
    EXPECT_EQ( TokenAmount::FormatMinions( 1 ), "0.000001" );
    EXPECT_EQ( TokenAmount::FormatMinions( 123456789 ), "123.456789" );
}

TEST( CalculateCostMinionsTest, MinimumEnforced )
{
    // Very small cost should be rounded up to MIN_MINION_UNITS
    auto r = TokenAmount::CalculateCostMinions( 1ULL, 1.0 );
    ASSERT_TRUE( r.has_value() );
    EXPECT_EQ( r.value(), TokenAmount::MIN_MINION_UNITS );
}

TEST( CalculateCostMinionsTest, ScalingByFactorTen )
{
    // Increase totalBytes by ×10 until overflow, checking cost each time
    uint64_t totalBytes = 1ULL;
    while ( true )
    {
        SCOPED_TRACE( ::testing::Message() << "totalBytes=" << totalBytes );
        auto r = TokenAmount::CalculateCostMinions( totalBytes, 1.0 );
        ASSERT_TRUE( r.has_value() );

        uint64_t expected = ( totalBytes < 100000ULL ? TokenAmount::MIN_MINION_UNITS : totalBytes / 100000ULL );
        EXPECT_EQ( r.value(), expected );

        // Prevent overflow
        if ( totalBytes > std::numeric_limits<uint64_t>::max() / 10 )
        {
            break;
        }
        totalBytes *= 10;
    }
}

TEST( CalculateCostMinionsTest, LargerCost )
{
    // totalBytes=1e12, price=1 => rawMinions should be 10
    uint64_t totalBytes = 1000000000000ULL;
    auto     r          = TokenAmount::CalculateCostMinions( totalBytes, 1.0 );
    ASSERT_TRUE( r.has_value() );
    EXPECT_EQ( r.value(), 10ULL );
}

TEST( CalculateCostMinionsTest, ZeroPriceError )
{
    // Division by zero should produce error
    auto r = TokenAmount::CalculateCostMinions( 1000ULL, 0.0 );
    EXPECT_TRUE( r.has_error() );
}
