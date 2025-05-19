#include <cstdint>
#include <system_error>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <variant>
#include "base/ScaledInteger.hpp"

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
    auto result    = sgns::ScaledInteger::FromString( test_case.input, test_case.precision );

    if ( std::holds_alternative<uint64_t>( test_case.expected ) )
    {
        uint64_t expected_val = std::get<uint64_t>( test_case.expected );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value(), expected_val );
    }
    else
    {
        std::errc expected_err = std::get<std::errc>( test_case.expected );
        ASSERT_FALSE( result.has_value() );
        EXPECT_EQ( result.error(), std::make_error_code( expected_err ) );
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

/**
 * @brief Struct to hold test parameters for FixedPointToStringParamTest.
 */
struct ToStringParam_s
{
    uint64_t    value;     ///< The numeric value that represents fixed-point data.
    uint64_t    precision; ///< The desired precision for the string output.
    std::string expected;  ///< The expected string result.
};

/**
 * @brief Parameterized test fixture for testing `toString` function.
 */
class FixedPointToStringParamTest : public ::testing::TestWithParam<ToStringParam_s>
{
};

/**
 * @test Tests the conversion of fixed-point numbers to string representation.
 */
TEST_P( FixedPointToStringParamTest, ToString )
{
    auto test_case = GetParam();

    auto result = sgns::ScaledInteger::ToString( test_case.value, test_case.precision );

    EXPECT_EQ( result, test_case.expected );
}

/**
 * @test Test suite for `toString` function.
 */
INSTANTIATE_TEST_SUITE_P( FixedPointToStringTests,
                          FixedPointToStringParamTest,
                          ::testing::Values( ToStringParam_s{ 0ULL, 0ULL, "0" },
                                             ToStringParam_s{ 123ULL, 0ULL, "123" },
                                             ToStringParam_s{ 24ULL, 9ULL, "0.000000024" },
                                             ToStringParam_s{ 12345ULL, 3ULL, "12.345" },
                                             ToStringParam_s{ 12345ULL, 4ULL, "1.2345" },
                                             ToStringParam_s{ 999999999ULL, 9ULL, "0.999999999" },
                                             ToStringParam_s{ 999999999999999999ULL, 9ULL, "999999999.999999999" },
                                             ToStringParam_s{ 123ULL, 5ULL, "0.00123" },
                                             ToStringParam_s{ 1000000ULL, 6ULL, "1.000000" },
                                             ToStringParam_s{ 1ULL, 9ULL, "0.000000001" },
                                             ToStringParam_s{ 0ULL, 9ULL, "0.000000000" } ) );

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
    auto tc = GetParam();

    auto static_res = sgns::ScaledInteger::Multiply( tc.a, tc.b, tc.precision );

    if ( std::holds_alternative<uint64_t>( tc.expected ) )
    {
        uint64_t expect = std::get<uint64_t>( tc.expected );
        ASSERT_TRUE( static_res.has_value() );
        EXPECT_EQ( static_res.value(), expect );
    }
    else
    {
        std::errc expect_err = std::get<std::errc>( tc.expected );
        ASSERT_FALSE( static_res.has_value() );
        EXPECT_EQ( static_res.error(), std::make_error_code( expect_err ) );
    }

    auto faRes = sgns::ScaledInteger::New( tc.a, tc.precision );
    ASSERT_TRUE( faRes.has_value() );
    auto faPtr = faRes.value();

    auto fbRes = sgns::ScaledInteger::New( tc.b, tc.precision );
    ASSERT_TRUE( fbRes.has_value() );
    auto fbPtr = fbRes.value();

    auto obj_res = faPtr->Multiply( *fbPtr );

    if ( std::holds_alternative<uint64_t>( tc.expected ) )
    {
        uint64_t expect = std::get<uint64_t>( tc.expected );
        ASSERT_TRUE( obj_res.has_value() );
        auto fp = obj_res.value();
        EXPECT_EQ( fp.Value(), expect );
        EXPECT_EQ( fp.Precision(), tc.precision );
    }
    else
    {
        std::errc expect_err = std::get<std::errc>( tc.expected );
        ASSERT_FALSE( obj_res.has_value() );
        EXPECT_EQ( obj_res.error(), std::make_error_code( expect_err ) );
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

// ======================== FixedPointDivideTests ========================

struct DivideParam_s
{
    uint64_t                          num;
    uint64_t                          den;
    uint64_t                          precision;
    std::variant<uint64_t, std::errc> expected;
};

/**
 * @brief Parameterized test fixture for testing `Divide` function.
 */
class FixedPointDivideParamTest : public ::testing::TestWithParam<DivideParam_s>
{
};

/**
 * @test Tests division of fixed-point numbers.
 */
TEST_P( FixedPointDivideParamTest, Divide )
{
    auto tc = GetParam();

    auto static_res = sgns::ScaledInteger::Divide( tc.num, tc.den, tc.precision );

    if ( std::holds_alternative<uint64_t>( tc.expected ) )
    {
        uint64_t expect = std::get<uint64_t>( tc.expected );
        ASSERT_TRUE( static_res.has_value() );
        EXPECT_EQ( static_res.value(), expect );
    }
    else
    {
        std::errc expect_err = std::get<std::errc>( tc.expected );
        ASSERT_FALSE( static_res.has_value() );
        EXPECT_EQ( static_res.error(), std::make_error_code( expect_err ) );
    }

    auto faRes = sgns::ScaledInteger::New( tc.num, tc.precision );
    ASSERT_TRUE( faRes.has_value() );
    auto faPtr = faRes.value();

    auto fbRes = sgns::ScaledInteger::New( tc.den, tc.precision );
    ASSERT_TRUE( fbRes.has_value() );
    auto fbPtr = fbRes.value();

    auto obj_res = faPtr->Divide( *fbPtr );

    if ( std::holds_alternative<uint64_t>( tc.expected ) )
    {
        uint64_t expect = std::get<uint64_t>( tc.expected );
        ASSERT_TRUE( obj_res.has_value() );
        auto fp = obj_res.value();
        EXPECT_EQ( fp.Value(), expect );
        EXPECT_EQ( fp.Precision(), tc.precision );
    }
    else
    {
        std::errc expect_err = std::get<std::errc>( tc.expected );
        ASSERT_FALSE( obj_res.has_value() );
        EXPECT_EQ( obj_res.error(), std::make_error_code( expect_err ) );
    }
}

/**
 * @test Test suite for `Divide` function.
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

/**
 * @brief Struct to hold test parameters for FixedPointConvertPrecisionParamTest.
 */
struct ConvertPrecisionParam_s
{
    uint64_t                          value;    ///< Raw fixed-point integer input.
    uint64_t                          from;     ///< Original number of decimal places.
    uint64_t                          to;       ///< Target number of decimal places.
    std::variant<uint64_t, std::errc> expected; ///< Expected converted value or error code.
};

/**
 * @brief Parameterized test fixture for testing `ConvertPrecision` functions.
 */
class FixedPointConvertPrecisionParamTest : public ::testing::TestWithParam<ConvertPrecisionParam_s>
{
};

/**
 * @test Tests static and object `ConvertPrecision` functions.
 */
TEST_P( FixedPointConvertPrecisionParamTest, ConvertPrecision )
{
    auto test_case = GetParam();

    auto static_res = sgns::ScaledInteger::ConvertPrecision( test_case.value, test_case.from, test_case.to );

    if ( std::holds_alternative<uint64_t>( test_case.expected ) )
    {
        uint64_t expected_val = std::get<uint64_t>( test_case.expected );
        ASSERT_TRUE( static_res.has_value() );
        EXPECT_EQ( static_res.value(), expected_val );
    }
    else
    {
        std::errc expected_err = std::get<std::errc>( test_case.expected );
        ASSERT_FALSE( static_res.has_value() );
        EXPECT_EQ( static_res.error(), std::make_error_code( expected_err ) );
    }

    auto fpRes = sgns::ScaledInteger::New( test_case.value, test_case.from );
    ASSERT_TRUE( fpRes.has_value() );
    auto fpPtr = fpRes.value();

    auto obj_res = fpPtr->ConvertPrecision( test_case.to );

    if ( std::holds_alternative<uint64_t>( test_case.expected ) )
    {
        uint64_t expected_val = std::get<uint64_t>( test_case.expected );
        ASSERT_TRUE( obj_res.has_value() );
        auto result_fp = obj_res.value();
        EXPECT_EQ( result_fp.Value(), expected_val );
        EXPECT_EQ( result_fp.Precision(), test_case.to );
    }
    else
    {
        std::errc expected_err = std::get<std::errc>( test_case.expected );
        ASSERT_FALSE( obj_res.has_value() );
        EXPECT_EQ( obj_res.error(), std::make_error_code( expected_err ) );
    }
}

/**
 * @test Test suite for `ConvertPrecision` functions.
 */
INSTANTIATE_TEST_SUITE_P(
    FixedPointConvertPrecisionTests,
    FixedPointConvertPrecisionParamTest,
    ::testing::Values(
        // Success Cases
        ConvertPrecisionParam_s{ 12345ULL, 3ULL, 3ULL, 12345ULL },
        ConvertPrecisionParam_s{ 12345ULL, 3ULL, 6ULL, 12345000ULL },
        ConvertPrecisionParam_s{ 123456000ULL, 6ULL, 3ULL, 123456ULL },
        // Losing precision
        ConvertPrecisionParam_s{ 123456789ULL, 9ULL, 6ULL, 123456ULL },
        ConvertPrecisionParam_s{ 1ULL, 9ULL, 0ULL, 0ULL },
        ConvertPrecisionParam_s{ 0ULL, 5ULL, 10ULL, 0ULL },
        // Error Cases
        ConvertPrecisionParam_s{ UINT64_MAX, 0ULL, 1ULL, std::errc::value_too_large },
        ConvertPrecisionParam_s{ ( UINT64_MAX / 10 ) + 1, 1ULL, 2ULL, std::errc::value_too_large },
        ConvertPrecisionParam_s{ 0xFFFFFFFFFFFFFFULL, 0ULL, 4ULL, std::errc::value_too_large },
        ConvertPrecisionParam_s{ 1234567890123456789ULL, 9ULL, 19ULL, std::errc::value_too_large } ) );
