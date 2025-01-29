#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "base/fixed_point.hpp"

// ======================== FixedPointParamTest ========================
struct FixedPointParamTestCase
{
    std::string input;
    uint64_t    precision;
    uint64_t    expected_value;
};

class FixedPointParamTest : public ::testing::TestWithParam<FixedPointParamTestCase>
{
};

TEST_P( FixedPointParamTest, FromString )
{
    auto test_case = GetParam();
    auto result    = sgns::fixed_point::fromString( test_case.input, test_case.precision );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), test_case.expected_value );
}

INSTANTIATE_TEST_SUITE_P(
    ValidInputs,
    FixedPointParamTest,
    ::testing::Values(
        FixedPointParamTestCase{ .input = "123.456", .precision = 9ULL, .expected_value = 123456000000ULL },
        FixedPointParamTestCase{ .input = "000123.00456000", .precision = 9ULL, .expected_value = 123004560000ULL },
        FixedPointParamTestCase{ .input = "123", .precision = 9ULL, .expected_value = 123000000000ULL },
        FixedPointParamTestCase{ .input = "123.45", .precision = 9ULL, .expected_value = 123450000000ULL },
        FixedPointParamTestCase{ .input = "1.123456789", .precision = 9ULL, .expected_value = 1123456789ULL } ) );

// ======================== FixedPointInvalidParamTest ========================
struct FixedPointInvalidParamTestCase
{
    std::string input;
    uint64_t    precision;
    std::errc   expected_error;
};

class FixedPointInvalidParamTest : public ::testing::TestWithParam<FixedPointInvalidParamTestCase>
{
};

TEST_P( FixedPointInvalidParamTest, FromString_InvalidInputs )
{
    auto test_case = GetParam();
    auto result    = sgns::fixed_point::fromString( test_case.input, test_case.precision );
    ASSERT_FALSE( result.has_value() );
    EXPECT_EQ( result.error(), std::make_error_code( test_case.expected_error ) );
}

INSTANTIATE_TEST_SUITE_P( InvalidInputs,
                          FixedPointInvalidParamTest,
                          ::testing::Values(
                              FixedPointInvalidParamTestCase{
                                  .input          = "abc",                      //
                                  .precision      = 9ULL,                       //
                                  .expected_error = std::errc::invalid_argument //
                              },
                              FixedPointInvalidParamTestCase{
                                  .input          = "",                         //
                                  .precision      = 9ULL,                       //
                                  .expected_error = std::errc::invalid_argument //
                              },
                              FixedPointInvalidParamTestCase{
                                  .input          = "1.1234567890",            //
                                  .precision      = 9ULL,                      //
                                  .expected_error = std::errc::value_too_large //
                              } ) );

// ======================== FixedPointMultiplySuccessTest ========================
struct FixedPointMultiplySuccessTestCase
{
    uint64_t a;
    uint64_t b;
    uint64_t precision;
    uint64_t expected_value;
};

class FixedPointMultiplySuccessTest : public ::testing::TestWithParam<FixedPointMultiplySuccessTestCase>
{
};

TEST_P( FixedPointMultiplySuccessTest, Multiply )
{
    auto test_case = GetParam();
    auto result    = sgns::fixed_point::multiply( test_case.a, test_case.b, test_case.precision );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), test_case.expected_value );
}

INSTANTIATE_TEST_SUITE_P( MultiplySuccessTests,
                          FixedPointMultiplySuccessTest,
                          ::testing::Values(
                              FixedPointMultiplySuccessTestCase{
                                  .a              = 4000000000ULL, //
                                  .b              = 2000000000ULL, //
                                  .precision      = 9ULL,          //
                                  .expected_value = 8000000000ULL  //
                              },
                              FixedPointMultiplySuccessTestCase{
                                  .a              = 9223372036854775807ULL, //
                                  .b              = 2000000000ULL,          //
                                  .precision      = 9ULL,                   //
                                  .expected_value = 18446744073709551614ULL //
                              } ) );

// ======================== FixedPointMultiplyErrorTest ========================
struct FixedPointMultiplyErrorTestCase
{
    uint64_t  a;
    uint64_t  b;
    uint64_t  precision;
    std::errc expected_error;
};

class FixedPointMultiplyErrorTest : public ::testing::TestWithParam<FixedPointMultiplyErrorTestCase>
{
};

TEST_P( FixedPointMultiplyErrorTest, Multiply )
{
    auto test_case = GetParam();
    auto result    = sgns::fixed_point::multiply( test_case.a, test_case.b, test_case.precision );
    ASSERT_FALSE( result.has_value() );
    EXPECT_EQ( result.error(), std::make_error_code( test_case.expected_error ) );
}

INSTANTIATE_TEST_SUITE_P( MultiplyErrorTests,
                          FixedPointMultiplyErrorTest,
                          ::testing::Values( FixedPointMultiplyErrorTestCase{
                              .a              = UINT64_MAX,                //
                              .b              = 1000000001ULL,             //
                              .precision      = 9ULL,                      //
                              .expected_error = std::errc::value_too_large //
                          } ) );

// ======================== FixedPointDivideSuccessTest ========================
struct FixedPointDivideSuccessTestCase
{
    uint64_t numerator;
    uint64_t denominator;
    uint64_t precision;
    uint64_t expected_value;
};

class FixedPointDivideSuccessTest : public ::testing::TestWithParam<FixedPointDivideSuccessTestCase>
{
};

TEST_P( FixedPointDivideSuccessTest, Divide )
{
    auto test_case = GetParam();
    auto result    = sgns::fixed_point::divide( test_case.numerator, test_case.denominator, test_case.precision );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), test_case.expected_value );
}

INSTANTIATE_TEST_SUITE_P( DivideSuccessTests,
                          FixedPointDivideSuccessTest,
                          ::testing::Values(
                              FixedPointDivideSuccessTestCase{
                                  .numerator      = 2000000000ULL, //
                                  .denominator    = 2000000000ULL, //
                                  .precision      = 9ULL,          //
                                  .expected_value = 1000000000ULL  //
                              },
                              FixedPointDivideSuccessTestCase{
                                  .numerator      = 10000000000ULL, //
                                  .denominator    = 2000000000ULL,  //
                                  .precision      = 9ULL,           //
                                  .expected_value = 5000000000ULL   //
                              },
                              FixedPointDivideSuccessTestCase{
                                  .numerator      = UINT64_MAX,            //
                                  .denominator    = 2000000000ULL,         //
                                  .precision      = 9ULL,                  //
                                  .expected_value = 9223372036854775807ULL //
                              } ) );

// ======================== FixedPointDivideErrorTest ========================
struct FixedPointDivideErrorTestCase
{
    uint64_t  numerator;
    uint64_t  denominator;
    uint64_t  precision;
    std::errc expected_error;
};

class FixedPointDivideErrorTest : public ::testing::TestWithParam<FixedPointDivideErrorTestCase>
{
};

TEST_P( FixedPointDivideErrorTest, Divide )
{
    auto test_case = GetParam();
    auto result    = sgns::fixed_point::divide( test_case.numerator, test_case.denominator, test_case.precision );
    ASSERT_FALSE( result.has_value() );
    EXPECT_EQ( result.error(), std::make_error_code( test_case.expected_error ) );
}

INSTANTIATE_TEST_SUITE_P( DivideErrorTests,
                          FixedPointDivideErrorTest,
                          ::testing::Values(
                              FixedPointDivideErrorTestCase{
                                  .numerator      = 1000000000ULL,                 //
                                  .denominator    = 0ULL,                          //
                                  .precision      = 9ULL,                          //
                                  .expected_error = std::errc::result_out_of_range //
                              },
                              FixedPointDivideErrorTestCase{
                                  .numerator      = UINT64_MAX,                //
                                  .denominator    = 999999999ULL,              //
                                  .precision      = 9ULL,                      //
                                  .expected_error = std::errc::value_too_large //
                              } ) );
