/**
 * @file token_amount_tests.cpp
 * @brief Parameterized unit tests for sgns::TokenAmount.
 */

#include <cstdint>
#include <string>
#include <variant>
#include <tuple>
#include <limits>
#include <system_error>
#include <gtest/gtest.h>
#include "account/TokenAmount.hpp"
#include "outcome/outcome.hpp"

using outcome::result;
using sgns::TokenAmount;

// Convenient byte-size constants
static constexpr uint64_t KB = 1024ULL;
static constexpr uint64_t MB = KB * KB;
static constexpr uint64_t GB = MB * KB;
static constexpr uint64_t TB = GB * KB;

// ======================== ParseMinions Tests ========================
struct ParseMinionsParam
{
    std::string                       input;
    std::variant<uint64_t, std::errc> expected;
};

class ParseMinionsTest : public ::testing::TestWithParam<ParseMinionsParam>
{
};

TEST_P( ParseMinionsTest, HandlesValidAndInvalidStrings )
{
    auto [input, expected] = GetParam();
    auto r                 = TokenAmount::ParseMinions( input );
    if ( std::holds_alternative<uint64_t>( expected ) )
    {
        EXPECT_TRUE( r.has_value() );
        EXPECT_EQ( r.value(), std::get<uint64_t>( expected ) );
    }
    else
    {
        EXPECT_TRUE( r.has_error() );
        EXPECT_EQ( r.error(), std::make_error_code( std::get<std::errc>( expected ) ) );
    }
}

INSTANTIATE_TEST_SUITE_P( ParseMinionsCases,
                          ParseMinionsTest,
                          ::testing::Values( ParseMinionsParam{ "0.000001", 1ULL },
                                             ParseMinionsParam{ "123.456789", 123456789ULL },
                                             ParseMinionsParam{ "10.000000", 10000000ULL },
                                             ParseMinionsParam{ "invalid", std::errc::invalid_argument },
                                             ParseMinionsParam{ "", std::errc::invalid_argument } ) );

// ======================== FormatMinions Tests ========================
struct FormatMinionsParam
{
    uint64_t    value;
    std::string expected;
};

class FormatMinionsTest : public ::testing::TestWithParam<FormatMinionsParam>
{
};

TEST_P( FormatMinionsTest, FormatsIntoString )
{
    auto [value, expected] = GetParam();
    EXPECT_EQ( TokenAmount::FormatMinions( value ), expected );
}

INSTANTIATE_TEST_SUITE_P( FormatMinionsCases,
                          FormatMinionsTest,
                          ::testing::Values( FormatMinionsParam{ 0ULL, "0.000000" },
                                             FormatMinionsParam{ 1ULL, "0.000001" },
                                             FormatMinionsParam{ 1000000ULL, "1.000000" },
                                             FormatMinionsParam{ 999999999ULL, "999.999999" } ) );

// ======================== CalculateCostMinions Zero Rate Tests ========================
TEST( CalculateCostMinionsZeroRate, ReturnsErrorOnZeroPrice )
{
    auto r = TokenAmount::CalculateCostMinions( 500ULL, 0.0 );
    EXPECT_TRUE( r.has_error() );
}

// ======================== CalculateCostMinions Fallback Tests ========================
using MinCostParam = std::tuple<uint64_t, double>;

class MinionCostFallbackTest : public ::testing::TestWithParam<MinCostParam>
{
};

TEST_P( MinionCostFallbackTest, AlwaysMinimumUnits )
{
    auto [bytes, rate] = GetParam();
    auto r             = TokenAmount::CalculateCostMinions( bytes, rate );
    EXPECT_TRUE( r.has_value() );
    EXPECT_EQ( r.value(), TokenAmount::MIN_MINION_UNITS );
}

INSTANTIATE_TEST_SUITE_P( FallbackCases,
                          MinionCostFallbackTest,
                          ::testing::Values( MinCostParam{ 1ULL, 1.0 },
                                             MinCostParam{ 10ULL, 1.0 },
                                             MinCostParam{ 100ULL, 1.0 },
                                             MinCostParam{ 1000ULL, 1.0 },
                                             MinCostParam{ 10000ULL, 1.0 },
                                             MinCostParam{ 100000ULL - 1ULL, 1.0 } ) );

// ======================== CalculateCostMinions Proportional Tests ========================
using CostCalcParam = std::tuple<uint64_t, double, uint64_t>;

class MinionCostCalculationTest : public ::testing::TestWithParam<CostCalcParam>
{
};

TEST_P( MinionCostCalculationTest, ComputesExpectedUnits )
{
    auto [bytes, rate, expected] = GetParam();
    auto r                       = TokenAmount::CalculateCostMinions( bytes, rate );
    EXPECT_TRUE( r.has_value() );
    EXPECT_EQ( r.value(), expected );
}

INSTANTIATE_TEST_SUITE_P(
    CalculationCases,
    MinionCostCalculationTest,
    ::testing::Values( CostCalcParam{ 1 * MB, 1.0, 10ULL },
                       CostCalcParam{ 1 * MB, 2.0, 5ULL },
                       CostCalcParam{ 10 * MB, 1.0, 104ULL },
                       CostCalcParam{ 100 * MB, 1.0, 1048ULL },
                       CostCalcParam{ 1 * MB, 10.0, 1ULL },
                       CostCalcParam{ 500 * MB, 1.0, 5242ULL },
                       CostCalcParam{ 5 * GB, 5.0, 10737ULL },
                       CostCalcParam{ 100 * GB, 0.1, 10737418ULL },
                       CostCalcParam{ 100 * TB, 1.0, 1099511627ULL },
                       CostCalcParam{ 100 * TB, 0.1, 10995116277ULL },
                       CostCalcParam{ 100 * TB, 0.0001, 10995116277760ULL },
                       // edge case: maximum bytes
                       CostCalcParam{ std::numeric_limits<uint64_t>::max(), 1.0, 184467440737095ULL } ) );
