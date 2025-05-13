#pragma once

#include <string>
#include "outcome/outcome.hpp"

namespace sgns
{
    /**
     * @class TokenAmount
     * @brief Utility for GNUS token fixed-point parsing, formatting and cost calculation.
     */
    class TokenAmount
    {
    public:
        /// Fixed-point precision (1 minion = 10^-6 GNUS)
        static constexpr uint32_t PRECISION = 6;

        /// USD per FLOP rate scaled by 1e15
        static constexpr uint64_t PRICE_PER_FLOP = 500;

        /// Fractional digits used in USD/FLOP rate
        static constexpr uint64_t PRICE_PER_FLOP_FRACTIONAL_DIGITS = 15;

        /// Estimated FLOPs per byte
        static constexpr uint64_t FLOPS_PER_BYTE = 20;

        /// Minimum non-zero minion units
        static constexpr uint64_t MIN_MINION_UNITS = 1;

        /**
         * @brief Parses a GNUS token string into minion units.
         * @param[in] str Token string (e.g. "1.234567")
         * @return Raw minion units on success
         */
        static outcome::result<uint64_t> ParseMinions( const std::string &str ) noexcept;

        /**
         * @brief Formats minion units into a GNUS token string.
         * @param[in] minions Raw minion units
         * @return Token string (e.g. "1.234567")
         */
        static std::string FormatMinions( uint64_t minions ) noexcept;

        /**
         * @brief Calculates token cost in minion units.
         * @param[in] totalBytes Data size in bytes
         * @param[in] priceUsdPerGenius GNUS price in USD
         * @return Cost in minion units
         */
        static outcome::result<uint64_t> CalculateCostMinions( uint64_t totalBytes, double priceUsdPerGenius );

    private:
        TokenAmount() = delete;
    };
} // namespace sgns
