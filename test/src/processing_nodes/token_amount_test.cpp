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

TEST( CalculateCostMinionsTest, CostIsMinimumWhenBelowThreshold )
{
    for ( auto i = UINT64_C( 1 ); i < TokenAmount::MIN_MINION_UNITS * UINT64_C( 100000 ); i++ )
    {
        SCOPED_TRACE( ::testing::Message() << "totalBytes=" << i );
        auto r = TokenAmount::CalculateCostMinions( i, 1.0 );
        ASSERT_TRUE( r.has_value() );
        EXPECT_EQ( r.value(), TokenAmount::MIN_MINION_UNITS );
        i *= 10;
    }
}

TEST( CalculateCostMinionsTest, CostScalesProportionallyAboveThreshold )
{
    for ( auto i = UINT64_C( 100000 ); i < std::numeric_limits<uint64_t>::max() / 10; i *= 10 )
    {
        SCOPED_TRACE( ::testing::Message() << "totalBytes=" << i );
        auto r = TokenAmount::CalculateCostMinions( i, 1.0 );
        ASSERT_TRUE( r.has_value() );

        auto expected = i / UINT64_C( 100000 );
        EXPECT_EQ( r.value(), expected );
    }
}

TEST( CalculateCostMinionsTest, ZeroPriceError )
{
    auto r = TokenAmount::CalculateCostMinions( 1000ULL, 0.0 );
    EXPECT_TRUE( r.has_error() );
}
