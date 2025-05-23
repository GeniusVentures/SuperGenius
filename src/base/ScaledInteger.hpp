/**
 * @file        ScaledInteger.hpp
 * @author      Luiz Guilherme Rizzatto Zucchi (luizgrz@gmail.com)
 * @brief       Utilities for decimal arithmetic using scaled integers.
 * @version     1.0
 * @date        2025-01-29
 * @copyright   Copyright (c) 2025
 */

#pragma once

#include <string>
#include <memory>
#include <boost/multiprecision/cpp_int.hpp>
#include "outcome/outcome.hpp"

namespace sgns
{
    /**
     * @class ScaledInteger
     * @brief Represents a decimal value using an integer scaled by 10^precision.
     *
     * Internally stores the decimal as an integer multiplied by 10^precision.
     * Example: raw_value = 12345, precision = 2 => decimal value 123.45
     */
    class ScaledInteger
    {
    public:
        /**
         * @brief Create a ScaledInteger from a raw integer and precision.
         * @param[in] raw_value  Integer representation scaled by 10^precision.
         * @param[in] precision  Number of decimal places (scale factor).
         * @return Outcome containing shared_ptr to ScaledInteger or error.
         */
        static outcome::result<std::shared_ptr<ScaledInteger>> New( uint64_t raw_value, uint64_t precision );

        /**
         * @brief Create a ScaledInteger from a double and precision.
         * @param[in] value      Double value to convert.
         * @param[in] precision  Number of decimal places (scale factor).
         * @return Outcome containing shared_ptr to ScaledInteger or error.
         */
        static outcome::result<std::shared_ptr<ScaledInteger>> New( double value, uint64_t precision );

        /**
         * @brief Create a ScaledInteger from a string and precision.
         * @param[in] str        String representation of the decimal value.
         * @param[in] precision  Number of decimal places (scale factor).
         * @return Outcome containing shared_ptr to ScaledInteger or error.
         */
        static outcome::result<std::shared_ptr<ScaledInteger>> New( const std::string &str, uint64_t precision );

        /**
         * @brief Create a ScaledInteger from a decimal string by inferring precision.
         * @param[in] str  String representation of the decimal value (e.g., "123.45").
         * @return Outcome containing shared_ptr to ScaledInteger or error.
         */
        static outcome::result<std::shared_ptr<ScaledInteger>> New( const std::string &str );

        /**
         * @brief Compute 10^precision as the scale factor.
         * @param[in] precision  Number of decimal places.
         * @return Scale factor (10^precision).
         */
        static constexpr uint64_t ScaleFactor( uint64_t precision );

        /**
         * @brief Convert a string to a scaled integer representation.
         * @param[in] str        Numeric string with integer and fractional parts.
         * @param[in] precision  Number of decimal places.
         * @return Outcome containing raw scaled integer or error.
         */
        static outcome::result<uint64_t> FromString( const std::string &str, uint64_t precision );

        /**
         * @brief Convert a scaled integer to a string representation.
         * @param[in] value      Raw scaled integer.
         * @param[in] precision  Number of decimal places.
         * @return String representation of the decimal value.
         */
        static std::string ToString( uint64_t value, uint64_t precision );

        /**
         * @brief Convert a double to a raw scaled integer.
         * @param[in] value      Double value to convert.
         * @param[in] precision  Number of decimal places.
         * @return Outcome containing raw scaled integer or error.
         */
        static outcome::result<uint64_t> FromDouble( double value, uint64_t precision );

        /**
         * @brief Multiply two raw scaled integers.
         * @param[in] a          First raw scaled integer.
         * @param[in] b          Second raw scaled integer.
         * @param[in] precision  Number of decimal places.
         * @return Outcome containing raw scaled integer product or error.
         */
        static outcome::result<uint64_t> Multiply( uint64_t a, uint64_t b, uint64_t precision );

        /**
         * @brief Divide two raw scaled integers.
         * @param[in] a          Dividend as raw scaled integer.
         * @param[in] b          Divisor as raw scaled integer.
         * @param[in] precision  Number of decimal places.
         * @return Outcome containing raw scaled integer quotient or error.
         */
        static outcome::result<uint64_t> Divide( uint64_t a, uint64_t b, uint64_t precision );

        /**
         * @brief Change the precision of a raw scaled integer.
         * @param[in] value      Raw scaled integer at original precision.
         * @param[in] from       Original number of decimal places.
         * @param[in] to         Target number of decimal places.
         * @return Outcome containing raw scaled integer at new precision or error.
         */
        static outcome::result<uint64_t> ConvertPrecision( uint64_t value, uint64_t from, uint64_t to );

        /**
         * @brief Get the raw scaled integer value.
         * @return Raw scaled integer.
         */
        uint64_t Value() const noexcept;

        /**
         * @brief Get the precision (number of decimal places).
         * @return Number of decimal places.
         */
        uint64_t Precision() const noexcept;

        /**
         * @brief  Return this value as a string.
         * @param  fixedDecimals
         *         - true: always show all fractional digits (pad with zeros up to precision())
         *         - false: trim trailing '0's in the fractional part (and drop the '.' if no fraction remains)
         * @return formatted string
         */
        std::string ToString( bool fixedDecimals = true ) const;

        /**
         * @brief Add another ScaledInteger with matching precision.
         * @param[in] other      ScaledInteger to add.
         * @return Outcome containing sum ScaledInteger or error.
         */
        outcome::result<ScaledInteger> Add( const ScaledInteger &other ) const;

        /**
         * @brief Subtract another ScaledInteger with matching precision.
         * @param[in] other      ScaledInteger to subtract.
         * @return Outcome containing difference ScaledInteger or error.
         */
        outcome::result<ScaledInteger> Subtract( const ScaledInteger &other ) const;

        /**
         * @brief Multiply by another ScaledInteger with matching precision.
         * @param[in] other      ScaledInteger to multiply.
         * @return Outcome containing product ScaledInteger or error.
         */
        outcome::result<ScaledInteger> Multiply( const ScaledInteger &other ) const;

        /**
         * @brief Divide by another ScaledInteger with matching precision.
         * @param[in] other      ScaledInteger to divide by.
         * @return Outcome containing quotient ScaledInteger or error.
         */
        outcome::result<ScaledInteger> Divide( const ScaledInteger &other ) const;

        /**
         * @brief Convert this ScaledInteger to a different precision.
         * @param[in] to         Target number of decimal places.
         * @return Outcome containing new ScaledInteger or error.
         */
        outcome::result<ScaledInteger> ConvertPrecision( uint64_t to ) const;

    private:
        /**
         * @brief Private constructor storing raw scaled integer and precision.
         * @param[in] value      Raw scaled integer.
         * @param[in] precision  Number of decimal places.
         */
        explicit ScaledInteger( uint64_t value, uint64_t precision );

        uint64_t value_;
        uint64_t precision_;
    };

} // namespace sgns
