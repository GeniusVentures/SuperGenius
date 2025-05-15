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
#include <memory>
#include <boost/multiprecision/cpp_int.hpp>
#include "outcome/outcome.hpp"

namespace sgns
{
    /**
     * @class       fixed_point
     * @brief       Provides fixed-point arithmetic utilities and value-type support.
     */
    class fixed_point
    {
    public:
        /**
         * @brief       Create a shared_ptr to a fixed_point from raw value and precision.
         * @param[in]   raw_value Raw fixed-point value.
         * @param[in]   precision Number of decimal places.
         * @return      Shared pointer to new fixed_point or error.
         */
        static outcome::result<std::shared_ptr<fixed_point>> create( uint64_t raw_value, uint64_t precision );

        /**
         * @brief       Create a shared_ptr to a fixed_point from double and precision.
         * @param[in]   raw_value Double-precision floating-point value.
         * @param[in]   precision Number of decimal places.
         * @return      Shared pointer to new fixed_point or error.
         */
        static outcome::result<std::shared_ptr<fixed_point>> create( double raw_value, uint64_t precision );

        /**
         * @brief       Create a shared_ptr to a fixed_point from string and precision.
         * @param[in]   str_value String representing the decimal number.
         * @param[in]   precision Number of decimal places.
         * @return      Shared pointer to new fixed_point or error.
         */
        static outcome::result<std::shared_ptr<fixed_point>> create( const std::string &str_value, uint64_t precision );

        /**
         * @brief       Returns power-of-ten scaling factor for a given precision.
         * @param[in]   precision Number of decimal places.
         * @return      10 raised to the power of precision.
         */
        static constexpr uint64_t scaleFactor( uint64_t precision );

        /**
         * @brief       Convert a string to fixed-point representation.
         * @param[in]   str_value Numeric string with integer and fractional parts.
         * @param[in]   precision Number of decimal places.
         * @return      Outcome of fixed-point representation or error.
         */
        static outcome::result<uint64_t> fromString( const std::string &str_value, uint64_t precision );

        /**
         * @brief       Convert fixed-point representation back to string.
         * @param[in]   value     Fixed-point number.
         * @param[in]   precision Number of decimal places.
         * @return      String representation.
         */
        static std::string toString( uint64_t value, uint64_t precision );

        /**
         * @brief       Convert a double to a raw fixed-point integer.
         * @param[in]   raw_value Double-precision floating-point value.
         * @param[in]   precision Number of decimal places.
         * @return      Raw fixed-point integer.
         * @throws      std::overflow_error if the converted value does not fit in uint64_t.
         */
        static uint64_t fromDouble( double raw_value, uint64_t precision );

        /**
         * @brief       Multiply two fixed-point numbers with optional precision.
         * @param[in]   a         First number.
         * @param[in]   b         Second number.
         * @param[in]   precision Number of decimal places.
         * @return      Outcome of multiplication in fixed-point representation.
         */
        static outcome::result<uint64_t> multiply( uint64_t a, uint64_t b, uint64_t precision );

        /**
         * @brief       Divide two fixed-point numbers with optional precision.
         * @param[in]   a         Dividend.
         * @param[in]   b         Divisor.
         * @param[in]   precision Number of decimal places.
         * @return      Outcome of division in fixed-point representation.
         */
        static outcome::result<uint64_t> divide( uint64_t a, uint64_t b, uint64_t precision );

        /**
         * @brief       Convert a raw fixed-point integer from one precision to another.
         * @param[in]   value           Fixed-point integer at original precision.
         * @param[in]   from_precision  Original number of decimal places.
         * @param[in]   to_precision    Target number of decimal places.
         * @return      Outcome with converted raw value or error if overflow.
         */
        static outcome::result<uint64_t> convertPrecision( uint64_t value,
                                                           uint64_t from_precision,
                                                           uint64_t to_precision );

        /**
         * @brief       Get the raw fixed-point value.
         * @return      Raw fixed-point integer.
         */
        uint64_t value() const noexcept;

        /**
         * @brief       Get the precision of this fixed-point value.
         * @return      Number of decimal places.
         */
        uint64_t precision() const noexcept;

        /**
         * @brief       Add another fixed_point with matching precision.
         * @param[in]   other Other fixed_point to add.
         * @return      Sum as fixed_point or failure if precision mismatch or overflow.
         */
        outcome::result<fixed_point> add( const fixed_point &other ) const;

        /**
         * @brief       Subtract another fixed_point with matching precision.
         * @param[in]   other Other fixed_point to subtract.
         * @return      Difference as fixed_point or failure if precision mismatch or underflow.
         */
        outcome::result<fixed_point> subtract( const fixed_point &other ) const;

        /**
         * @brief       Multiply by another fixed_point with matching precision.
         * @param[in]   other Other fixed_point to multiply.
         * @return      Product as fixed_point or failure if precision mismatch or overflow.
         */
        outcome::result<fixed_point> multiply( const fixed_point &other ) const;

        /**
         * @brief       Divide by another fixed_point with matching precision.
         * @param[in]   other Other fixed_point to divide by.
         * @return      Quotient as fixed_point or failure if precision mismatch or division by zero.
         */
        outcome::result<fixed_point> divide( const fixed_point &other ) const;

        /**
         * @brief       Convert this fixed_point object to a different precision.
         * @param[in]   to_precision    Target number of decimal places.
         * @return      Outcome with new fixed_point or error if overflow.
         */
        outcome::result<fixed_point> convertPrecision( uint64_t to_precision ) const;

    private:
        /**
         * @brief       Construct a fixed_point value.
         * @param[in]   value     Raw fixed-point integer.
         * @param[in]   precision Number of decimal places.
         */
        explicit fixed_point( uint64_t value, uint64_t precision );

        /**
         * @brief       Construct a fixed_point from a double value.
         * @param[in]   raw_value Double-precision floating-point value.
         * @param[in]   precision Number of decimal places.
         * @throws      std::overflow_error if the converted value does not fit in uint64_t.
         */
        explicit fixed_point( double raw_value, uint64_t precision );

        uint64_t value_;
        uint64_t precision_;
    };
} // namespace sgns

#endif // _FIXED_POINT_HPP
