#pragma once

#include <string>
#include "base/fixed_point.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{

    /**
     * @class TokenAmount
     * @brief Strongly-typed wrapper for GNUS token amounts and cost calculation.
     */
    class TokenAmount
    {
    public:
        /// Fixed-point precision for token amounts (minion = 10^-6)
        static constexpr uint32_t PRECISION = 6;

        /// USD per FLOP rate coefficient (5 × 10^-15 USD per FLOP)
        static constexpr uint64_t PRICE_PER_FLOP = 500;

        /// Number of decimal places for USD-per-FLOP rate (fractional digits = 15)
        static constexpr uint64_t PRICE_PER_FLOP_FRACTIONAL_DIGITS = 15;

        /// Estimated FLOPs consumed per byte of data (20 flops/byte)
        static constexpr uint64_t FLOPS_PER_BYTE = 20;

        /// Minimum non-zero cost in minion units (1)
        static constexpr uint64_t MIN_MINION_UNITS = 1;

        /**
         * @brief Parse a human-readable GNUS token string into raw minion units.
         * @param str e.g. "1.234567"
         * @return Raw minion units (1e-6 token) on success
         */
        static outcome::result<uint64_t> ParseMinions( const std::string &str ) noexcept
        {
            return fixed_point::fromString( str, PRECISION );
        }

        /**
         * @brief Format raw minion units to a GNUS token string.
         * @param minions Raw minion units (1e-6 token)
         * @return String representation (e.g., "1.234567")
         */
        static std::string FormatMinions( uint64_t minions ) noexcept
        {
            return fixed_point::toString( minions, PRECISION );
        }

        /**
         * @brief Calculate cost in minion units for a given data size and GNUS price.
         * @param totalBytes Data size in bytes
         * @param priceUsdPerToken Price of one GNUS token in USD
         * @return Cost in minion units, at least MIN_MINION_UNITS
         */
        static outcome::result<uint64_t> CalculateCostMinions( uint64_t totalBytes, double priceUsdPerGenius )
        {
            auto rateCoeff   = PRICE_PER_FLOP * FLOPS_PER_BYTE;
            auto fpPrecision = PRICE_PER_FLOP_FRACTIONAL_DIGITS;

            /// Total USD cost represented in fixed-point
            auto totalUsdFp = fixed_point( totalBytes * rateCoeff, fpPrecision );

            /// Price per GNUS token represented in the same fixed-point precision
            auto priceFp = fixed_point( priceUsdPerGenius, fpPrecision );

            /// Compute token cost: USD / (USD/GNUS)
            auto geniusFpRes = totalUsdFp.divide( priceFp );
            if ( !geniusFpRes )
            {
                return outcome::failure( geniusFpRes.error() );
            }
            auto geniusFp = geniusFpRes.value();

            /// Convert token cost to minion precision
            auto minionFpRes = geniusFp.convertPrecision( PRECISION );
            if ( !minionFpRes )
            {
                return outcome::failure( minionFpRes.error() );
            }
            auto minionFp = minionFpRes.value();

            /// Enforce minimum non-zero cost
            uint64_t rawMinions = minionFp.value();
            if ( rawMinions < MIN_MINION_UNITS )
            {
                rawMinions = MIN_MINION_UNITS;
            }

            return outcome::success( rawMinions );
        }

    private:
        TokenAmount() = delete;
    };

} // namespace sgns
