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
        using namespace boost::multiprecision;

        /**
         * @brief       Convert a string to fixed-point representation
         * @param[in]   str_value Numeric string with integer and fractional parts
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      Outcome of fixed-point representation or error
         */
        static outcome::result<uint64_t> fromString( const std::string &str_value, uint64_t precision = 9 )
        {
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

            // Parse the integer and fractional parts
            uint64_t integer_part    = 0;
            uint64_t fractional_part = 0;

            auto [ptr_int, ec_int] = std::from_chars( integer_str.data(),
                                                      integer_str.data() + integer_str.size(),
                                                      integer_part );
            if ( ec_int != std::errc() )
            {
                return outcome::failure( std::make_error_code( ec_int ) );
            }

            if ( !fractional_str.empty() )
            {
                auto [ptr_frac, ec_frac] = std::from_chars( fractional_str.data(),
                                                            fractional_str.data() + fractional_str.size(),
                                                            fractional_part );
                if ( ec_frac != std::errc() )
                {
                    return outcome::failure( std::make_error_code( ec_frac ) );
                }
            }

            // Adjust fractional part to match required precision
            if ( fractional_str.length() > precision )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }

            for ( uint64_t i = fractional_str.length(); i < precision; ++i )
            {
                fractional_part *= 10;
            }

            uint64_t scale = 1;
            for ( uint64_t i = 0; i < precision; ++i )
            {
                scale *= 10;
            }

            return outcome::success( ( integer_part * scale ) + fractional_part );
        }

        /**
         * @brief       Convert fixed-point representation to string
         * @param[in]   value Fixed-point number
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      String representation
         */
        static std::string toString( uint64_t value, uint64_t precision = 9 )
        {
            uint64_t scale = 1;
            for ( uint64_t i = 0; i < precision; ++i )
            {
                scale *= 10;
            }
            uint64_t integer_part    = value / scale;
            uint64_t fractional_part = value % scale;
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
            uint64_t scale = 1;
            for ( uint64_t i = 0; i < precision; ++i )
            {
                scale *= 10;
            }

            uint128_t result = static_cast<uint128_t>( a ) * static_cast<uint128_t>( b );
            result           = result / static_cast<uint128_t>( scale );

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
            if ( b == 0 )
            {
                return outcome::failure( std::make_error_code( std::errc::result_out_of_range ) );
            }

            uint128_t scale = 1;
            for ( uint64_t i = 0; i < precision; ++i )
            {
                scale *= 10;
            }

            uint128_t result = ( static_cast<uint128_t>( a ) * scale ) / b;

            if ( result > std::numeric_limits<uint64_t>::max() )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }

            return outcome::success( static_cast<uint64_t>( result ) );
        }
    }
}
#endif
