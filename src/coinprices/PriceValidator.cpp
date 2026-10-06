/**
 * Source file for PriceValidator — the pure fail-fast decision chain
 * (Phase 7, VAL-01) in the locked D-07-11 order: legacy -> timestamp
 * sanity -> cost binding -> coverage -> band. Exactly one reason per
 * input; no clock reads, no I/O, no shared state (purity is the
 * CONS-02 determinism prerequisite).
 */
#include "PriceValidator.hpp"

#include "account/TokenAmount.hpp" // cost-binding oracle (src/ is an include root)

#include <cmath> // std::isfinite

namespace sgns
{
    PriceValidationResult ValidatePrice( const PriceValidationInput &input,
                                         const PriceValidatorConfig &config )
    {
        // Diagnostics are computed up front from the observed stats and the
        // config only (never from the claimed price) and echoed on every
        // path, so Phase 8 logging and tests never re-derive them (Pitfall 7).
        PriceValidationResult result;
        result.observedMin = input.stats.min;
        result.observedMax = input.stats.max;
        const auto window = PriceObservationWindow( input.dagTimestamp, config );
        result.windowFrom = window.from;
        result.windowTo   = window.to;
        const double low  = input.stats.min * ( 1.0 - config.tolerancePct );
        const double high = input.stats.max * ( 1.0 + config.tolerancePct );
        result.bandLow    = low;
        result.bandHigh   = high;

        // 1. Legacy (D-07-08, VAL-04): proto3 absent field defaults the wire
        //    double to 0.0. Fail-closed, no grace flag (D-07-09). -0.0 == 0.0
        //    is true, so negative zero lands here by design.
        if ( input.claimedPrice == 0.0 )
        {
            result.reason = PriceValidationReason::LegacyNoPrice;
            return result;
        }

        // 2. Timestamp sanity (D-07-03): strict comparisons put both
        //    boundaries on the accept side (Pitfall 4) — dagTimestamp ==
        //    now + clockSkew and == now - maxAge are still accepted.
        if ( input.dagTimestamp > input.now + config.clockSkew )
        {
            result.reason = PriceValidationReason::TimestampFuture;
            return result;
        }
        if ( input.dagTimestamp < input.now - config.maxAge )
        {
            result.reason = PriceValidationReason::TimestampStale;
            return result;
        }

        // 3. Cost binding (D-02): guard non-finite/negative BEFORE the
        //    oracle — NaN passes ScaledInteger::FromDouble's comparison
        //    guard into UB and platform-divergent verdicts (Pitfall 1).
        //    Non-finites and negatives bucket into CostMismatch (T-07-01).
        if ( !std::isfinite( input.claimedPrice ) || input.claimedPrice < 0.0 )
        {
            result.reason = PriceValidationReason::CostMismatch;
            return result;
        }
        // Exact integer equality, never a floating-point epsilon: the wire
        // double is bit-preserved from the poster and CalculateCostMinions
        // is pure, so honest input reproduces the escrow number exactly.
        const auto expected = TokenAmount::CalculateCostMinions( input.blockSize, input.claimedPrice );
        if ( !expected || expected.value() != input.escrowAmount )
        {
            result.reason = PriceValidationReason::CostMismatch;
            return result;
        }

        // 4. Coverage (D-07-05/D-07-07): key on count, never on min —
        //    count >= 1 is evidence even when min == max or min == 0.0
        //    (Pitfall 2).
        if ( input.stats.count == 0 )
        {
            result.reason = PriceValidationReason::NoCoverage;
            return result;
        }

        // 5. Band (D-07-01): [min*(1-tol), max*(1+tol)], both edges
        //    inclusive — strict comparisons accept exactly at the edges.
        if ( input.claimedPrice < low )
        {
            result.reason = PriceValidationReason::BelowBand;
            return result;
        }
        if ( input.claimedPrice > high )
        {
            result.reason = PriceValidationReason::AboveBand;
            return result;
        }

        result.accepted = true;
        result.reason   = PriceValidationReason::Accepted;
        return result;
    }
} // namespace sgns
