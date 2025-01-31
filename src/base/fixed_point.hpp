/**
 * @file        fixed_point.hpp
 * @author      Luiz Guilherme Rizzatto Zucchi (luizgrz@gmail.com)
 * @brief       Fixed-point arithmetic utilities
 * @version     1.0
 * @date        2025-01-29
 * @copyright   Copyright (c) 2025
 */
#ifndef _FIXED_POINT_HPP
#define _FIXED_POINT_HPP

#include <string>
#include <vector>
#include <type_traits>
#include <optional>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <boost/multiprecision/cpp_int.hpp>
#include "outcome/outcome.hpp"

namespace sgns
{
    namespace fixed_point
    {
        /**
         * @brief Maximum precision for fixed-point calculations.
         */
        static constexpr uint64_t MAX_PRECISION = 16;

        /**
         * @brief Generates a table of scaling factors for different precision levels.
         * @return A constexpr array of scaling factors.
         */
        static constexpr std::array<uint64_t, MAX_PRECISION> generateScaleTable()
        {
            std::array<uint64_t, MAX_PRECISION> table{};
            uint64_t                            value = 1;
            for ( size_t i = 0; i < MAX_PRECISION; i++ )
            {
                table[i]  = value;
                value    *= 10;
            }
            return table;
        }

        /**
         * @brief Precomputed table of scaling factors.
         */
        static constexpr std::array<uint64_t, MAX_PRECISION> scale_table = generateScaleTable();

        /**
         * @brief       Convert a string to fixed-point representation
         * @param[in]   str_value Numeric string with integer and fractional parts
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      Outcome of fixed-point representation or error
         */
        static outcome::result<uint64_t> fromString( const std::string &str_value, uint64_t precision = 9 )
        {
            if ( precision > MAX_PRECISION )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }
            if ( str_value.empty() )
            {
                return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
            }

            size_t           dot_pos = str_value.find( '.' );
            std::string_view integer_str, fractional_str;

            if ( dot_pos != std::string::npos )
            {
                integer_str    = std::string_view( str_value ).substr( 0, dot_pos );
                fractional_str = std::string_view( str_value ).substr( dot_pos + 1 );
            }
            else
            {
                integer_str = str_value;
            }

            uint64_t integer_part    = 0;
            uint64_t fractional_part = 0;

            auto [ptr_int, ec_int] = std::from_chars( integer_str.data(),
                                                      integer_str.data() + integer_str.size(),
                                                      integer_part );

            if ( ec_int != std::errc() || ( ptr_int != integer_str.data() + integer_str.size() ) )
            {
                return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
            }

            if ( !fractional_str.empty() )
            {
                auto [ptr_frac, ec_frac] = std::from_chars( fractional_str.data(),
                                                            fractional_str.data() + fractional_str.size(),
                                                            fractional_part );
                if ( ec_frac != std::errc() || ptr_frac != fractional_str.data() + fractional_str.size() )
                {
                    return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
                }
            }

            if ( fractional_str.length() > precision )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }

            for ( uint64_t i = fractional_str.length(); i < precision; ++i )
            {
                fractional_part *= 10;
            }

            return outcome::success( ( integer_part * scale_table[precision] ) + fractional_part );
        }

        /**
         * @brief       Convert fixed-point representation to string
         * @param[in]   value Fixed-point number
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      String representation
         */
        static std::string toString( uint64_t value, uint64_t precision = 9 )
        {
            if ( precision > MAX_PRECISION )
            {
                return "Error: Precision too large";
            }
            uint64_t integer_part    = value / scale_table[precision];
            uint64_t fractional_part = value % scale_table[precision];
            return std::to_string( integer_part ) + "." + std::to_string( fractional_part );
        }

        /**
         * @brief       Multiply two fixed-point numbers with optional precision
         * @param[in]   a First number
         * @param[in]   b Second number
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      Outcome of multiplication in fixed-point representation
         */
        static outcome::result<uint64_t> multiply( uint64_t a, uint64_t b, uint64_t precision = 9 )
        {
            using namespace boost::multiprecision;
            if ( precision > MAX_PRECISION )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }
            uint128_t result = static_cast<uint128_t>( a ) * static_cast<uint128_t>( b );
            result           = result / static_cast<uint128_t>( scale_table[precision] );

            if ( result > std::numeric_limits<uint64_t>::max() )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }
            return outcome::success( static_cast<uint64_t>( result ) );
        }

        /**
         * @brief       Divide two fixed-point numbers with optional precision
         * @param[in]   a Dividend
         * @param[in]   b Divisor
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      Outcome of division in fixed-point representation
         */
        static outcome::result<uint64_t> divide( uint64_t a, uint64_t b, uint64_t precision = 9 )
        {
            using namespace boost::multiprecision;
            if ( precision > MAX_PRECISION )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }
            if ( b == 0 )
            {
                return outcome::failure( std::make_error_code( std::errc::result_out_of_range ) );
            }

            uint128_t result = ( static_cast<uint128_t>( a ) * static_cast<uint128_t>( scale_table[precision] ) ) /
                               static_cast<uint128_t>( b );
            if ( result > std::numeric_limits<uint64_t>::max() )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }

            return outcome::success( static_cast<uint64_t>( result ) );
        }
    }
}

#endif // _FIXED_POINT_HPP
