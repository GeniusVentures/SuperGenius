/**
 * @file        fixed_point.hpp
 * @author      Luiz Guilherme Rizzatto Zucchi (luizgrz@gmail.com)
 * @brief       Fixed-point arithmetic utilities (Header file)
 * @version     1.0
 * @date        2025-01-29
 * @copyright   Copyright (c) 2025
 */

#ifndef _FIXED_POINT_HPP
#define _FIXED_POINT_HPP

#include <string>
#include <boost/multiprecision/cpp_int.hpp>
#include "outcome/outcome.hpp"

namespace sgns::fixed_point
{
    /**
     * @brief How many decimal places are needed to store one GNU, thus
     * one minion is 10 ^ GNU_PRECISION
     */
    static constexpr uint64_t GNUS_PRECISION = 6;

    /**
     * @brief Maximum precision for fixed-point calculations.
     */
    static constexpr uint64_t MAX_PRECISION = 16;
    /**
     * @brief       Returns power-of-ten scaling factor for a given precision.
     * @param[in]   precision Number of decimal places.
     * @return      10 raised to the power of precision.
     */
    constexpr uint64_t scaleFactor( uint64_t precision = GNUS_PRECISION );
    /**
     * @brief       Convert a string to fixed-point representation
     * @param[in]   str_value Numeric string with integer and fractional parts
     * @param[in]   precision Number of decimal places
     * @return      Outcome of fixed-point representation or error
     */
    outcome::result<uint64_t> fromString( const std::string &str_value, uint64_t precision = GNUS_PRECISION );

    /**
     * @brief       Convert fixed-point representation to string
     * @param[in]   value Fixed-point number
     * @param[in]   precision Number of decimal places
     * @return      String representation
     */
    std::string toString( uint64_t value, uint64_t precision = GNUS_PRECISION );

    /**
     * @brief       Multiply two fixed-point numbers with optional precision
     * @param[in]   a First number
     * @param[in]   b Second number
     * @param[in]   precision Number of decimal places
     * @return      Outcome of multiplication in fixed-point representation
     */
    outcome::result<uint64_t> multiply( uint64_t a, uint64_t b, uint64_t precision = GNUS_PRECISION );

    /**
     * @brief       Divide two fixed-point numbers with optional precision
     * @param[in]   a Dividend
     * @param[in]   b Divisor
     * @param[in]   precision Number of decimal places
     * @return      Outcome of division in fixed-point representation
     */
    outcome::result<uint64_t> divide( uint64_t a, uint64_t b, uint64_t precision = GNUS_PRECISION );
}

#endif // _FIXED_POINT_HPP
