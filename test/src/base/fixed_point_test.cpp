#include <gtest/gtest.h>
#include "base/util.hpp"
#include "testutil/outcome.hpp"

class FixedPointTest : public ::testing::Test
{
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F( FixedPointTest, FromString_ValidInput )
{
    auto result = sgns::fixed_point::fromString( "123.456", 9ULL );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), 123456000000ULL );
}

TEST_F( FixedPointTest, FromString_InvalidInput )
{
    auto result = sgns::fixed_point::fromString( "abc", 9ULL );
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error(), std::make_error_code( std::errc::invalid_argument ) );
}

TEST_F( FixedPointTest, FromString_EmptyString )
{
    auto result = sgns::fixed_point::fromString( "", 9ULL );
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error(), std::make_error_code( std::errc::invalid_argument ) );
}

TEST_F( FixedPointTest, FromString_ExceedsPrecision )
{
    auto result = sgns::fixed_point::fromString( "1.1234567890", 9ULL );
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error(), std::make_error_code( std::errc::value_too_large ) );
}

TEST_F( FixedPointTest, ToString )
{
    EXPECT_EQ( sgns::fixed_point::toString( 123456000000ULL, 9ULL ), "123.456000000" );
}

TEST_F( FixedPointTest, Multiply_ValidInput )
{
    auto a      = 4000000000ULL;
    auto b      = 2000000000ULL;
    auto result = sgns::fixed_point::multiply( a, b, 9ULL );
    ASSERT_TRUE( result );
    EXPECT_EQ( result.value(), 8000000000ULL );
}

TEST_F( FixedPointTest, Multiply_Overflow )
{
    auto a      = UINT64_MAX;
    auto b      = 1000000001ULL;
    auto result = sgns::fixed_point::multiply( a, b, 9ULL );
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error(), std::make_error_code( std::errc::value_too_large ) );
}

TEST_F( FixedPointTest, Multiply_MaxValues )
{
    auto a      = 9223372036854775807ULL;
    auto b      = 2000000000ULL;
    auto result = sgns::fixed_point::multiply( a, b, 9ULL );
    ASSERT_TRUE( result );
    EXPECT_EQ( result.value(), 18446744073709551614ULL );
}

TEST_F( FixedPointTest, Divide_ValidInput )
{
    auto result = sgns::fixed_point::divide( 2000000000ULL, 2000000000ULL, 9ULL );
    ASSERT_TRUE( result );
    EXPECT_EQ( result.value(), 1000000000ULL );
}

TEST_F( FixedPointTest, Divide_ByZero )
{
    auto result = sgns::fixed_point::divide( 1000000000ULL, 0ULL, 9ULL );
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error(), std::make_error_code( std::errc::result_out_of_range ) );
}

TEST_F( FixedPointTest, Divide_LargeValues )
{
    auto result = sgns::fixed_point::divide( 10000000000ULL, 2000000000ULL, 9ULL );
    ASSERT_TRUE( result );
    EXPECT_EQ( result.value(), 5000000000ULL );
}

TEST_F( FixedPointTest, Divide_MaxValues )
{
    auto result = sgns::fixed_point::divide( UINT64_MAX, 2000000000ULL, 9ULL );
    ASSERT_TRUE( result );
    EXPECT_EQ( result.value(), 9223372036854775807ULL );
}
