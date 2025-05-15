/**
 * @file TokenAmount.hpp
 * @brief Fixed-precision (6 decimals) token amount type with arithmetic helpers.
 * @copyright 2025
 */

#pragma once

#include <string>
#include <memory>
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
        static constexpr uint64_t PRECISION = 6;

        /// USD per FLOP rate scaled by 10^15
        static constexpr uint64_t PRICE_PER_FLOP = 500;

        /// Fractional digits used in USD/FLOP rate
        static constexpr uint64_t PRICE_PER_FLOP_FRACTIONAL_DIGITS = 15;

        /// Estimated number of FLOPs required per byte
        static constexpr uint64_t FLOPS_PER_BYTE = 20;

        /// Minimum representable non-zero minion units
        static constexpr uint64_t MIN_MINION_UNITS = 1;

        /**
         * @brief Creates a TokenAmount from already-scaled minion units.
         *
         * @param raw_minions Value in 10^6-scaled integer units.
         * @return A shared pointer to a TokenAmount instance or error code.
         */
        static outcome::result<std::shared_ptr<TokenAmount>> New( uint64_t raw_minions );

        /**
         * @brief Creates a TokenAmount from a floating point value.
         * The value is rounded to 6 decimal digits internally.
         *
         * @param value Floating point value representing token amount.
         * @return A shared pointer to a TokenAmount instance or error code.
         */
        static outcome::result<std::shared_ptr<TokenAmount>> New( double value );

        /**
         * @brief Creates a TokenAmount from a decimal string.
         * @param str String representation of the token amount.
         * @return A shared pointer to a TokenAmount instance or error code.
         */
        static outcome::result<std::shared_ptr<TokenAmount>> New( const std::string &str );

        /**
         * @brief Parses a decimal string into a TokenAmount.
         * @param str Token string in decimal format (e.g., "2.000123").
         * @return A TokenAmount instance or an error.
         */
        static outcome::result<TokenAmount> FromString( const std::string &str );

        /**
         * @brief Converts this TokenAmount to a string with exactly 6 fractional digits.
         *
         * @return String representation of the token amount (e.g., "1.234567").
         */
        std::string ToString() const;

        /**
         * @brief Multiplies this TokenAmount by another.
         *
         * Result is rounded back to fixed 6-digit precision.
         *
         * @param other Another TokenAmount instance.
         * @return Result of the multiplication or error.
         */
        outcome::result<TokenAmount> Multiply( const TokenAmount &other ) const;

        /**
         * @brief Divides this TokenAmount by another.
         *
         * @param other Another TokenAmount instance.
         * @return Result of the division or error.
         */
        outcome::result<TokenAmount> Divide( const TokenAmount &other ) const;

        /**
         * @brief Returns the raw scaled integer (minion) value.
         *
         * @return Value in raw minion units (10^6 scaled).
         */
        uint64_t Raw() const;

        /**
         * @brief Parses a token amount string into raw minion units.
         *
         * @param str Decimal string representation (e.g., "0.001000").
         * @return Parsed minion value or error.
         */
        static outcome::result<uint64_t> ParseMinions( const std::string &str );

        /**
         * @brief Converts raw minion units to a token string.
         *
         * Ensures exactly 6 digits of precision in the result.
         *
         * @param minions Raw integer minion units.
         * @return Formatted string with fixed-point representation.
         */
        static std::string FormatMinions( uint64_t minions );

        /**
         * @brief Calculates token cost for a given byte size and USD price.
         *
         * Internally estimates FLOPs based on bytes, applies the FLOP pricing and
         * converts to GNUS token units using the provided USD price.
         *
         * @param totalBytes Total number of bytes processed.
         * @param priceUsdPerGenius GNUS token price in USD.
         * @return Total cost in minion units, or error on overflow/underflow.
         */
        static outcome::result<uint64_t> CalculateCostMinions( uint64_t totalBytes, double priceUsdPerGenius );

    private:
        /// Internal representation in minion units (GNUS * 10^6)
        uint64_t minions_;

        /**
         * @brief Constructs a TokenAmount directly from raw minion units.
         *
         * @param minion_units Scaled token value.
         */
        explicit TokenAmount( uint64_t minion_units );

        /**
         * @brief Constructs a TokenAmount from a floating point value.
         * Value is converted to fixed-point with rounding.
         *
         * @param value Floating-point token value.
         */
        explicit TokenAmount( double value );
    };

} // namespace sgns
