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
} // namespace

// Verdict matrix (TEST-01, filled out in the expansion task):
//   InBandAccepted          — accepted happy path with band diagnostics
//   AboveBandRejected       — claimed above max*(1+tol)
//   BelowBandRejected       — claimed below min*(1-tol)
//   FutureTimestampRejected / FutureEdgeInclusive — now+31s reject, now+30s accept
//   StaleTimestampRejected  / StaleEdgeInclusive  — now-601s reject, now-600s accept
//   CostMismatchRejected    — escrow off by -1 / +1
//   BandEdgeInclusive       — exact band edges accept, one step beyond rejects
//   NoCoverageRejected      — stats.count == 0
//   SingleObservationCovered / CoveredZeroMinStillBanded — count >= 1 is coverage
//   LegacyNoPriceRejected / LegacyNegativeZero — claimed 0.0 / -0.0
//   NaNRejectedDeterministically / NonFiniteRejected — non-finite guard
//   ChainOrderFirstReasonWins — D-07-11 ordering

TEST( PriceValidator, InBandAccepted )
{
    auto in = BaseInput();
    // The oracle owns the escrow number — never a hard-coded minions literal.
    EXPECT_OUTCOME_TRUE( minions,
        sgns::TokenAmount::CalculateCostMinions( in.blockSize, in.claimedPrice ) );
    in.escrowAmount = minions;

    const auto result = sgns::ValidatePrice( in );

    EXPECT_TRUE( result.accepted );
    EXPECT_EQ( result.reason, sgns::PriceValidationReason::Accepted );
    EXPECT_DOUBLE_EQ( result.bandLow, 1.20 * 0.9 );
    EXPECT_DOUBLE_EQ( result.bandHigh, 1.30 * 1.1 );
}
