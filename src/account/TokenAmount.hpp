#pragma once
#include <iostream>
#include <string>
#include "base/fixed_point.hpp"
#include "outcome/outcome.hpp"
#include <boost/multiprecision/cpp_int.hpp>

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
            uint64_t rateCoeff   = PRICE_PER_FLOP * FLOPS_PER_BYTE;
            int32_t  fpPrecision = PRICE_PER_FLOP_FRACTIONAL_DIGITS;

            std::cout << "[DEBUG] rateCoeff: " << rateCoeff << "\n";
            std::cout << "[DEBUG] totalBytes: " << totalBytes << "\n";

            uint64_t usdRaw = 0;

            // Try to multiply safely, reducing precision if overflow might occur
            while ( fpPrecision >= 6 )
            {
                // Check for overflow risk: totalBytes * rateCoeff must fit into uint64_t
                if ( totalBytes <= UINT64_MAX / rateCoeff )
                {
                    usdRaw = totalBytes * rateCoeff;
                    std::cout << "[DEBUG] Safe multiplication succeeded with fpPrecision = " << fpPrecision << "\n";
                    break;
                }
                else
                {
                    // Reduce precision and adjust rateCoeff accordingly
                    fpPrecision--;
                    rateCoeff /= 10;

                    std::cout << "[DEBUG] Reducing precision: " << fpPrecision << ", new rateCoeff: " << rateCoeff
                              << "\n";

                    if ( rateCoeff == 0 )
                    {
                        std::cout << "[ERROR] rateCoeff reduced to zero, aborting.\n";
                        return outcome::failure( std::errc::invalid_argument );
                    }
                }
            }

            if ( fpPrecision < 6 )
            {
                std::cout << "[ERROR] Could not compute cost safely within uint64_t limits.\n";
                return outcome::failure( std::errc::value_too_large );
            }

            auto totalUsdFp = fixed_point( usdRaw, fpPrecision );
            std::cout << "[DEBUG] totalUsdFp: " << totalUsdFp.value() << " (fpPrecision: " << fpPrecision << ")\n";

            auto priceFp = fixed_point( priceUsdPerGenius, fpPrecision );
            std::cout << "[DEBUG] priceFp: " << priceFp.value() << "\n";

            auto geniusFpRes = totalUsdFp.divide( priceFp );
            if ( !geniusFpRes )
            {
                std::cout << "[ERROR] Division failed in geniusFpRes: " << geniusFpRes.error().message() << "\n";
                return outcome::failure( geniusFpRes.error() );
            }
            auto geniusFp = geniusFpRes.value();
            std::cout << "[DEBUG] geniusFp: " << geniusFp.value() << "\n";

            auto minionFpRes = geniusFp.convertPrecision( PRECISION );
            if ( !minionFpRes )
            {
                std::cout << "[ERROR] Precision conversion failed in minionFpRes: " << minionFpRes.error().message()
                          << "\n";
                return outcome::failure( minionFpRes.error() );
            }
            auto minionFp = minionFpRes.value();
            std::cout << "[DEBUG] minionFp: " << minionFp.value() << "\n";

            uint64_t rawMinions = minionFp.value();
            if ( rawMinions < MIN_MINION_UNITS )
            {
                std::cout << "[INFO] rawMinions < MIN_MINION_UNITS, enforcing minimum: " << MIN_MINION_UNITS << "\n";
                rawMinions = MIN_MINION_UNITS;
            }

            std::cout << "[SUCCESS] rawMinions: " << rawMinions << "\n";
            return outcome::success( rawMinions );
        }

    private:
        TokenAmount() = delete;
    };

} // namespace sgns
