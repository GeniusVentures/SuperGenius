/**
 * @file        fixed_point.cpp
 * @author      Luiz Guilherme Rizzatto Zucchi (luizgrz@gmail.com)
 * @brief       Fixed-point arithmetic utilities (Implementation file)
 * @version     1.0
 * @date        2025-01-29
 * @copyright   Copyright (c) 2025
 */

#include "fixed_point.hpp"
#include <charconv>
#include <cmath>
#include <algorithm>
#include <limits>
#include <system_error>

namespace sgns
{
    namespace fixed_point
    {
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

        const std::array<uint64_t, MAX_PRECISION> scale_table = generateScaleTable();

        outcome::result<uint64_t> fromString( const std::string &str_value, uint64_t precision )
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

        std::string toString( uint64_t value, uint64_t precision )
        {
            if ( precision > MAX_PRECISION )
            {
                return "Error: Precision too large";
            }
            uint64_t integer_part    = value / scale_table[precision];
            uint64_t fractional_part = value % scale_table[precision];
            return std::to_string( integer_part ) + "." + std::to_string( fractional_part );
        }

        outcome::result<uint64_t> multiply( uint64_t a, uint64_t b, uint64_t precision )
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

        outcome::result<uint64_t> divide( uint64_t a, uint64_t b, uint64_t precision )
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
