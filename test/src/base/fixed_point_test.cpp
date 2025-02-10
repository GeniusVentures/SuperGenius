#include <cstdint>      
#include <system_error> 
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <variant>
#include "base/fixed_point.hpp"

// ======================== FixedPointFromStringTests ========================

/**
 * @brief Struct to hold test parameters for FixedPointParamTest.
 */
struct FromStrParam_s
{
    std::string                       input;     ///< Input string representing a fixed-point number.
    uint64_t                          precision; ///< Precision level for conversion.
    std::variant<uint64_t, std::errc> expected;  ///< Expected output, either a valid result or an error code.
};

/**
 * @brief Parameterized test fixture for testing `fromString` function.
 */
class FixedPointParamTest : public ::testing::TestWithParam<FromStrParam_s>
{
};

/**
 * @test Tests the conversion of string to fixed-point representation.
 */
TEST_P( FixedPointParamTest, FromString )
{
    auto test_case = GetParam();
    auto result    = sgns::fixed_point::fromString( test_case.input, test_case.precision );

    if ( std::holds_alternative<uint64_t>( test_case.expected ) )
    {
        uint64_t expected_val = std::get<uint64_t>( test_case.expected );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value(), expected_val );
    }
    else if ( std::holds_alternative<std::errc>( test_case.expected ) )
    {
        std::errc expected_err = std::get<std::errc>( test_case.expected );
        ASSERT_FALSE( result.has_value() );
        EXPECT_EQ( result.error(), std::make_error_code( expected_err ) );
    }
    else
    {
        FAIL() << "Unknown test case type.";
    }
}

/**
 * @test Test suite for `fromString` function.
 */
INSTANTIATE_TEST_SUITE_P( FixedPointFromStringTests,
                          FixedPointParamTest,
                          ::testing::Values(
                              // Valid cases
                              FromStrParam_s{ "123.456", 9ULL, 123456000000ULL },
                              FromStrParam_s{ "000123.00456000", 9ULL, 123004560000ULL },
                              FromStrParam_s{ "123", 9ULL, 123000000000ULL },
                              FromStrParam_s{ "123.45", 9ULL, 123450000000ULL },
                              FromStrParam_s{ "1.123456789", 9ULL, 1123456789ULL },
                              FromStrParam_s{ "0.000000001", 9ULL, 1ULL },
                              FromStrParam_s{ "999999999.999999999", 9ULL, 999999999999999999ULL },
                              FromStrParam_s{ "123.4", 1ULL, 1234ULL },
                              FromStrParam_s{ "123", 0ULL, 123ULL },
                              FromStrParam_s{ "0", 0ULL, 0ULL },
                              FromStrParam_s{ "1.1", 1ULL, 11ULL },
                              FromStrParam_s{ "1.123", 3ULL, 1123ULL },
                              // Error Cases
                              FromStrParam_s{ "abc", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ "", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ "1.1234567890", 9ULL, std::errc::value_too_large },
                              FromStrParam_s{ "-123.456", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ "123..456", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ "123.45.67", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ "1.123456789", 8ULL, std::errc::value_too_large },
                              FromStrParam_s{ " 123.456", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ "A123.456", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ "123.456 ", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ "123.456A", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ ".", 9ULL, std::errc::invalid_argument },
                              FromStrParam_s{ "0.0000000001", 9ULL, std::errc::value_too_large } ) );

// ======================== FixedPointMultiplyTests ========================

/**
 * @brief Struct to hold test parameters for FixedPointMultiplyParamTest.
 */
struct MultiplyParam_s
{
    uint64_t                          a;         ///< First operand.
    uint64_t                          b;         ///< Second operand.
    uint64_t                          precision; ///< Precision for multiplication.
    std::variant<uint64_t, std::errc> expected;  ///< Expected result or error code.
};

/**
 * @brief Parameterized test fixture for testing `multiply` function.
 */
class FixedPointMultiplyParamTest : public ::testing::TestWithParam<MultiplyParam_s>
{
};

/**
 * @test Tests multiplication of fixed-point numbers.
 */
TEST_P( FixedPointMultiplyParamTest, Multiply )
{
    auto test_case = GetParam();
    auto result    = sgns::fixed_point::multiply( test_case.a, test_case.b, test_case.precision );

    if ( std::holds_alternative<uint64_t>( test_case.expected ) )
    {
        uint64_t expected_val = std::get<uint64_t>( test_case.expected );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value(), expected_val );
    }
    else if ( std::holds_alternative<std::errc>( test_case.expected ) )
    {
        std::errc expected_err = std::get<std::errc>( test_case.expected );
        ASSERT_FALSE( result.has_value() );
        EXPECT_EQ( result.error(), std::make_error_code( expected_err ) );
    }
    else
    {
        FAIL() << "Unknown test case type.";
    }
}

/**
 * @test Test suite for `multiply` function.
 */
INSTANTIATE_TEST_SUITE_P( FixedPointMultiplyTests,
                          FixedPointMultiplyParamTest,
                          ::testing::Values(
                              // Valid cases
                              MultiplyParam_s{ 4000000000ULL, 2000000000ULL, 9ULL, 8000000000ULL },
                              MultiplyParam_s{ 9223372036854775807ULL, 2000000000ULL, 9ULL, 18446744073709551614ULL },
                              MultiplyParam_s{ 0ULL, 1234567890ULL, 9ULL, 0ULL },
                              MultiplyParam_s{ 1234567890ULL, 0ULL, 9ULL, 0ULL },
                              MultiplyParam_s{ 1000000000ULL, 999999999ULL, 9ULL, 999999999ULL },
                              MultiplyParam_s{ 1000000000ULL, 1000000000ULL, 9ULL, 1000000000ULL },
                              MultiplyParam_s{ 1ULL, 1ULL, 0ULL, 1ULL },
                              MultiplyParam_s{ 20ULL, 30ULL, 1ULL, 60ULL },

                              // Error Cases
                              MultiplyParam_s{ UINT64_MAX, 1000000001ULL, 9ULL, std::errc::value_too_large } ) );

// ======================== FixedPointDivideSuccessTest ========================

struct DivideParam_s
{
    uint64_t                          num;
    uint64_t                          den;
    uint64_t                          precision;
    std::variant<uint64_t, std::errc> expected;
};

/**
 * @brief Parameterized test fixture for testing `divide` function.
 */
class FixedPointDivideParamTest : public ::testing::TestWithParam<DivideParam_s>
{
};

/**
 * @test Tests division of fixed-point numbers.
 */
TEST_P( FixedPointDivideParamTest, Divide )
{
    auto test_case = GetParam();
    auto result    = sgns::fixed_point::divide( test_case.num, test_case.den, test_case.precision );

    if ( std::holds_alternative<uint64_t>( test_case.expected ) )
    {
        uint64_t expected_val = std::get<uint64_t>( test_case.expected );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value(), expected_val );
    }
    else if ( std::holds_alternative<std::errc>( test_case.expected ) )
    {
        std::errc expected_err = std::get<std::errc>( test_case.expected );
        ASSERT_FALSE( result.has_value() );
        EXPECT_EQ( result.error(), std::make_error_code( expected_err ) );
    }
    else
    {
        FAIL() << "Unknown test case type.";
    }
}

/**
 * @test Test suite for `divide` function.
 */
INSTANTIATE_TEST_SUITE_P( FixedPointDivideTests,
                          FixedPointDivideParamTest,
                          ::testing::Values(
                              // Success Cases
                              DivideParam_s{ 0ULL, 1234567890ULL, 9ULL, 0ULL },
                              DivideParam_s{ 2000000000ULL, 2000000000ULL, 9ULL, 1000000000ULL },
                              DivideParam_s{ 10000000000ULL, 2000000000ULL, 9ULL, 5000000000ULL },
                              DivideParam_s{ UINT64_MAX, 2000000000ULL, 9ULL, 9223372036854775807ULL },

                              // Error Cases
                              DivideParam_s{ 0ULL, 0ULL, 9ULL, std::errc::result_out_of_range },
                              DivideParam_s{ 1000000000ULL, 0ULL, 9ULL, std::errc::result_out_of_range },
                              DivideParam_s{ UINT64_MAX, 999999999ULL, 9ULL, std::errc::value_too_large } ) );
