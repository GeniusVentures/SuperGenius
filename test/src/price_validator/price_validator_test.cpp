/**
 * Source file for price_validator_test — the hermetic ValidatePrice suite
 * (Phase 7, TEST-01): one test per PriceValidationReason value plus
 * exact-boundary cases. Zero mocks, zero sockets, zero fakes: the validator
 * is a pure function over plain struct literals. No system_clock::now()
 * anywhere — every timestamp derives from the fixed kEpochBase (the
 * price_manager_test pattern).
 */
#include <gtest/gtest.h>

#include <account/TokenAmount.hpp>
#include <coinprices/PriceValidator.hpp>

#include "testutil/outcome.hpp"
#include "testutil/scoped_env.hpp"

#include <chrono>
#include <cmath>
#include <limits>

namespace
{
    // Fixed epoch base for all timestamp arithmetic — no system_clock::now()
    // anywhere in this suite (hermetic, deterministic; the price_manager_test
    // kEpochBase pattern).
    const auto kEpochBase = std::chrono::system_clock::time_point{} + std::chrono::seconds( 1727712000 );

    /// @brief The shared base case: claimed 1.25 inside the observed
    /// [1.20, 1.30] window, escrow set per test from the oracle, escrow
    /// timestamp 5s before validator-run time. Every field a plain literal.
    sgns::PriceValidationInput BaseInput()
    {
        sgns::PriceValidationInput in;
        in.claimedPrice = 1.25; // inside [1.20, 1.30] below
        in.escrowAmount = 0;    // SET PER TEST from the cost oracle
        in.dagTimestamp = kEpochBase;
        in.blockSize    = 1'000'000;
        in.now          = kEpochBase + std::chrono::seconds( 5 );
        in.stats        = { 3, 1.20, 1.30 }; // count, min, max
        return in;
    }

    /// @brief Recompute the escrow for whatever claimed price a case ended
    /// up with — the oracle owns the number, never a minions literal.
    uint64_t EscrowFor( const sgns::PriceValidationInput &in )
    {
        EXPECT_OUTCOME_TRUE( minions,
            sgns::TokenAmount::CalculateCostMinions( in.blockSize, in.claimedPrice ) );
        return minions;
    }
} // namespace

TEST( PriceValidator, InBandAccepted )
{
    auto       in     = BaseInput();
    in.escrowAmount   = EscrowFor( in );
    const auto result = sgns::ValidatePrice( in );

    EXPECT_TRUE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::Accepted );
    EXPECT_DOUBLE_EQ( result.bandLow, 1.20 * 0.9 );
    EXPECT_DOUBLE_EQ( result.bandHigh, 1.30 * 1.1 );
}

TEST( PriceValidator, AboveBandRejected )
{
    auto in = BaseInput();
    in.claimedPrice = 1.60; // above 1.30 * 1.1 = 1.43
    in.escrowAmount = EscrowFor( in );

    const auto result = sgns::ValidatePrice( in );

    EXPECT_FALSE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::AboveBand );
}

TEST( PriceValidator, BelowBandRejected )
{
    auto in = BaseInput();
    in.claimedPrice = 0.90; // below 1.20 * 0.9 = 1.08
    in.escrowAmount = EscrowFor( in ); // cost binding passes first (D-07-11)

    const auto result = sgns::ValidatePrice( in );

    EXPECT_FALSE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::BelowBand );
}

TEST( PriceValidator, FutureTimestampRejected )
{
    auto in         = BaseInput();
    in.dagTimestamp = in.now + std::chrono::seconds( 31 ); // one past the 30s skew
    in.escrowAmount = EscrowFor( in );

    const auto result = sgns::ValidatePrice( in );

    EXPECT_FALSE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::TimestampFuture );
}

TEST( PriceValidator, FutureEdgeInclusive )
{
    auto in         = BaseInput();
    in.dagTimestamp = in.now + std::chrono::seconds( 30 ); // == clockSkew (D-07-03)
    in.escrowAmount = EscrowFor( in );

    const auto result = sgns::ValidatePrice( in );

    EXPECT_TRUE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::Accepted );
}

TEST( PriceValidator, StaleTimestampRejected )
{
    auto in         = BaseInput();
    in.dagTimestamp = in.now - std::chrono::seconds( 601 ); // one past the 600s max age
    in.escrowAmount = EscrowFor( in );

    const auto result = sgns::ValidatePrice( in );

    EXPECT_FALSE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::TimestampStale );
}

TEST( PriceValidator, StaleEdgeInclusive )
{
    auto in         = BaseInput();
    in.dagTimestamp = in.now - std::chrono::seconds( 600 ); // == maxAge (D-07-03)
    in.escrowAmount = EscrowFor( in );

    const auto result = sgns::ValidatePrice( in );

    EXPECT_TRUE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::Accepted );
}

TEST( PriceValidator, CostMismatchRejected )
{
    // Escrow one BELOW the honest number.
    {
        auto       in     = BaseInput();
        in.escrowAmount   = EscrowFor( in ) - 1;
        const auto result = sgns::ValidatePrice( in );

        EXPECT_FALSE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::CostMismatch );
    }
    // Escrow one ABOVE the honest number — both directions mismatch.
    {
        auto       in     = BaseInput();
        in.escrowAmount   = EscrowFor( in ) + 1;
        const auto result = sgns::ValidatePrice( in );

        EXPECT_FALSE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::CostMismatch );
    }
}

TEST( PriceValidator, BandEdgeInclusive )
{
    const auto base = BaseInput();
    // Exactly at bandHigh: max * (1 + tol) — same arithmetic the chain uses.
    {
        auto       in     = base;
        in.claimedPrice   = in.stats.max * ( 1.0 + sgns::kDefaultPriceTolerance );
        in.escrowAmount   = EscrowFor( in );
        const auto result = sgns::ValidatePrice( in );

        EXPECT_TRUE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::Accepted );
    }
    // Exactly at bandLow: min * (1 - tol).
    {
        auto       in     = base;
        in.claimedPrice   = in.stats.min * ( 1.0 - sgns::kDefaultPriceTolerance );
        in.escrowAmount   = EscrowFor( in );
        const auto result = sgns::ValidatePrice( in );

        EXPECT_TRUE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::Accepted );
    }
    // One step beyond the high edge (x1.001).
    {
        auto       in     = base;
        in.claimedPrice   = in.stats.max * ( 1.0 + sgns::kDefaultPriceTolerance ) * 1.001;
        in.escrowAmount   = EscrowFor( in );
        const auto result = sgns::ValidatePrice( in );

        EXPECT_FALSE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::AboveBand );
    }
    // One step beyond the low edge (x0.999).
    {
        auto       in     = base;
        in.claimedPrice   = in.stats.min * ( 1.0 - sgns::kDefaultPriceTolerance ) * 0.999;
        in.escrowAmount   = EscrowFor( in );
        const auto result = sgns::ValidatePrice( in );

        EXPECT_FALSE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::BelowBand );
    }
}

TEST( PriceValidator, NoCoverageRejected )
{
    auto in = BaseInput();
    in.stats        = { 0, 0.0, 0.0 }; // count == 0 (D-07-05)
    in.escrowAmount = EscrowFor( in );

    const auto result = sgns::ValidatePrice( in );

    EXPECT_FALSE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::NoCoverage );
}

TEST( PriceValidator, SingleObservationCovered )
{
    auto in = BaseInput();
    in.stats        = { 1, 1.25, 1.25 }; // count == 1 is coverage (D-07-07)
    in.escrowAmount = EscrowFor( in );

    const auto result = sgns::ValidatePrice( in );

    EXPECT_TRUE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::Accepted );
}

TEST( PriceValidator, CoveredZeroMinStillBanded )
{
    auto in = BaseInput();
    in.stats        = { 2, 0.0, 1.30 }; // min == 0.0 with count >= 1: covered
    in.escrowAmount = EscrowFor( in );

    const auto result = sgns::ValidatePrice( in );

    EXPECT_TRUE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::Accepted );
}

TEST( PriceValidator, LegacyNoPriceRejected )
{
    auto       in     = BaseInput();
    in.claimedPrice   = 0.0; // proto3 absent field (D-07-08)
    const auto result = sgns::ValidatePrice( in );

    EXPECT_FALSE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::LegacyNoPrice );
}

TEST( PriceValidator, LegacyNegativeZero )
{
    auto       in     = BaseInput();
    in.claimedPrice   = -0.0; // -0.0 == 0.0 -> same legacy bucket
    const auto result = sgns::ValidatePrice( in );

    EXPECT_FALSE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::LegacyNoPrice );
}

TEST( PriceValidator, NaNRejectedDeterministically )
{
    auto       in     = BaseInput();
    in.claimedPrice   = std::numeric_limits<double>::quiet_NaN();
    const auto result = sgns::ValidatePrice( in );

    // Never Accepted/AboveBand/BelowBand — the guard runs BEFORE the
    // oracle so no UB can leak a platform-dependent verdict (Pitfall 1).
    EXPECT_FALSE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::CostMismatch );
}

TEST( PriceValidator, NonFiniteRejected )
{
    const auto base = BaseInput();
    // +inf.
    {
        auto       in     = base;
        in.claimedPrice   = std::numeric_limits<double>::infinity();
        const auto result = sgns::ValidatePrice( in );

        EXPECT_FALSE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::CostMismatch );
    }
    // -inf.
    {
        auto       in     = base;
        in.claimedPrice   = -std::numeric_limits<double>::infinity();
        const auto result = sgns::ValidatePrice( in );

        EXPECT_FALSE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::CostMismatch );
    }
    // Negative finite price.
    {
        auto       in     = base;
        in.claimedPrice   = -5.0;
        const auto result = sgns::ValidatePrice( in );

        EXPECT_FALSE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::CostMismatch );
    }
}

TEST( PriceValidator, ChainOrderFirstReasonWins )
{
    // Legacy beats a future timestamp: legacy runs first (D-07-11).
    {
        auto       in     = BaseInput();
        in.claimedPrice   = 0.0;
        in.dagTimestamp   = in.now + std::chrono::seconds( 31 );
        const auto result = sgns::ValidatePrice( in );

        EXPECT_FALSE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::LegacyNoPrice );
    }
    // Timestamp sanity beats a wrong escrow: cost binding runs after.
    {
        auto       in     = BaseInput();
        in.dagTimestamp   = in.now + std::chrono::seconds( 31 );
        in.escrowAmount   = EscrowFor( in ) - 1;
        const auto result = sgns::ValidatePrice( in );

        EXPECT_FALSE( result.accepted );
        EXPECT_EQ( result.reason, sgns::PriceValidationReason::TimestampFuture );
    }
}

TEST( PriceValidatorConfig, ResolverDefaults )
{
    // Neutralize any ambient operator setting: an empty value unsets on the
    // Windows CRT and is treated as unset by the resolver elsewhere, so the
    // defaults are what remains.
    const sgns::testutil::ScopedEnvVar t{ "SGNS_PRICEVAL_TOLERANCE_PCT", "" };
    const sgns::testutil::ScopedEnvVar w{ "SGNS_PRICEVAL_WINDOW_TTL_S", "" };
    const sgns::testutil::ScopedEnvVar s{ "SGNS_PRICEVAL_CLOCK_SKEW_S", "" };
    const sgns::testutil::ScopedEnvVar m{ "SGNS_PRICEVAL_MAX_AGE_S", "" };

    const auto cfg = sgns::ResolvePriceValidatorConfig();

    EXPECT_DOUBLE_EQ( cfg.tolerancePct, sgns::kDefaultPriceTolerance );
    EXPECT_EQ( cfg.windowTtl, sgns::kStaleMaxAge );
    EXPECT_EQ( cfg.clockSkew, sgns::kDefaultValidatorClockSkew );
    EXPECT_EQ( cfg.maxAge, sgns::kDefaultValidatorMaxAge );
}

TEST( PriceValidatorConfig, ResolverOverrides )
{
    const sgns::testutil::ScopedEnvVar t{ "SGNS_PRICEVAL_TOLERANCE_PCT", "0.25" };
    const sgns::testutil::ScopedEnvVar w{ "SGNS_PRICEVAL_WINDOW_TTL_S", "120" };
    const sgns::testutil::ScopedEnvVar s{ "SGNS_PRICEVAL_CLOCK_SKEW_S", "15" };
    const sgns::testutil::ScopedEnvVar m{ "SGNS_PRICEVAL_MAX_AGE_S", "300" };

    const auto cfg = sgns::ResolvePriceValidatorConfig();

    EXPECT_DOUBLE_EQ( cfg.tolerancePct, 0.25 );
    EXPECT_EQ( cfg.windowTtl, std::chrono::seconds( 120 ) );
    EXPECT_EQ( cfg.clockSkew, std::chrono::seconds( 15 ) );
    EXPECT_EQ( cfg.maxAge, std::chrono::seconds( 300 ) );
}

TEST( PriceValidatorConfig, ResolverNonsenseFallsBack )
{
    // Tolerance: empty, unparsable, negative, NaN, out-of-range 1.5 —
    // each falls back to the default, never a non-finite or degenerate knob.
    for ( const auto *bad : { "", "abc", "-5", "nan", "1.5" } )
    {
        const sgns::testutil::ScopedEnvVar t{ "SGNS_PRICEVAL_TOLERANCE_PCT", bad };
        const auto cfg = sgns::ResolvePriceValidatorConfig();
        EXPECT_DOUBLE_EQ( cfg.tolerancePct, sgns::kDefaultPriceTolerance ) << "tolerance input: " << bad;
    }
    // Integer seconds: unparsable, negative, zero — each falls back (> 0 is
    // the valid range; a non-positive duration would degenerate the window).
    for ( const auto *bad : { "abc", "-5", "0" } )
    {
        const sgns::testutil::ScopedEnvVar w{ "SGNS_PRICEVAL_WINDOW_TTL_S", bad };
        const auto cfg = sgns::ResolvePriceValidatorConfig();
        EXPECT_EQ( cfg.windowTtl, sgns::kStaleMaxAge ) << "windowTtl input: " << bad;
    }
    {
        const sgns::testutil::ScopedEnvVar s{ "SGNS_PRICEVAL_CLOCK_SKEW_S", "abc" };
        const auto cfg = sgns::ResolvePriceValidatorConfig();
        EXPECT_EQ( cfg.clockSkew, sgns::kDefaultValidatorClockSkew );
    }
    {
        const sgns::testutil::ScopedEnvVar m{ "SGNS_PRICEVAL_MAX_AGE_S", "0" };
        const auto cfg = sgns::ResolvePriceValidatorConfig();
        EXPECT_EQ( cfg.maxAge, sgns::kDefaultValidatorMaxAge );
    }
}

TEST( PriceValidatorConfig, ResolverNotCached )
{
    // Set, resolve, CHANGE, resolve again: the second read must see the new
    // value — no function-local static may freeze the first read (Pitfall 6).
    const sgns::testutil::ScopedEnvVar s{ "SGNS_PRICEVAL_CLOCK_SKEW_S", "15" };
    EXPECT_EQ( sgns::ResolvePriceValidatorConfig().clockSkew, std::chrono::seconds( 15 ) );

    const sgns::testutil::ScopedEnvVar s2{ "SGNS_PRICEVAL_CLOCK_SKEW_S", "45" };
    EXPECT_EQ( sgns::ResolvePriceValidatorConfig().clockSkew, std::chrono::seconds( 45 ) );
}

TEST( PriceValidatorConfig, RefetchOnlyOnNoCoverage )
{
    using R = sgns::PriceValidationReason;
    // True only for NoCoverage (D-07-06) — the self-heal policy hook.
    EXPECT_FALSE( sgns::ShouldTriggerRefetch( R::Accepted ) );
    EXPECT_FALSE( sgns::ShouldTriggerRefetch( R::LegacyNoPrice ) );
    EXPECT_FALSE( sgns::ShouldTriggerRefetch( R::TimestampFuture ) );
    EXPECT_FALSE( sgns::ShouldTriggerRefetch( R::TimestampStale ) );
    EXPECT_FALSE( sgns::ShouldTriggerRefetch( R::CostMismatch ) );
    EXPECT_FALSE( sgns::ShouldTriggerRefetch( R::AboveBand ) );
    EXPECT_FALSE( sgns::ShouldTriggerRefetch( R::BelowBand ) );
    EXPECT_TRUE( sgns::ShouldTriggerRefetch( R::NoCoverage ) );
}
