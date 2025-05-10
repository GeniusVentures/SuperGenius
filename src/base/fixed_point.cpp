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
#include <limits>
#include <system_error>
#include <boost/multiprecision/cpp_int.hpp>

namespace sgns
{

    fixed_point::fixed_point( uint64_t value, uint64_t precision ) :
        value_( std::move( value ) ), precision_( std::move( precision ) )
    {
    }

    fixed_point::fixed_point( double raw_value, uint64_t precision ) :
        value_( fromDouble( raw_value, precision ) ), precision_( precision )
    {
    }

    constexpr uint64_t fixed_point::scaleFactor( uint64_t precision )
    {
        uint64_t result = 1;
        for ( uint64_t i = 0; i < precision; ++i )
        {
            result *= 10;
        }
        return result;
    }

    outcome::result<uint64_t> fixed_point::fromString( const std::string &str_value, uint64_t precision )
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

        uint64_t integer_part    = 0;
        uint64_t fractional_part = 0;

        auto [ptr_int,
              ec_int] = std::from_chars( integer_str.data(), integer_str.data() + integer_str.size(), integer_part );
        if ( ec_int != std::errc() || ptr_int != integer_str.data() + integer_str.size() )
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

        return outcome::success( ( integer_part * scaleFactor( precision ) ) + fractional_part );
    }

    std::string fixed_point::toString( uint64_t value, uint64_t precision )
    {
        uint64_t integer_part    = value / scaleFactor( precision );
        uint64_t fractional_part = value % scaleFactor( precision );

        if ( precision == 0 )
        {
            return std::to_string( integer_part );
        }

        std::string fractional_str = std::to_string( fractional_part );
        if ( fractional_str.size() < precision )
        {
            fractional_str.insert( 0, precision - fractional_str.size(), '0' );
        }

        return std::to_string( integer_part ) + "." + fractional_str;
    }

    uint64_t fixed_point::fromDouble( double raw_value, uint64_t precision )
    {
        double factor  = static_cast<double>( scaleFactor( precision ) );
        double scaled  = raw_value * factor;
        double rounded = std::round( scaled );

        if ( rounded < 0.0 || rounded > static_cast<double>( std::numeric_limits<uint64_t>::max() ) )
        {
            throw std::overflow_error( "fixed_point: double to fixed conversion overflow" );
        }
        return static_cast<uint64_t>( rounded );
    }

    outcome::result<uint64_t> fixed_point::multiply( uint64_t a, uint64_t b, uint64_t precision )
    {
        using namespace boost::multiprecision;
        uint128_t result = static_cast<uint128_t>( a ) * static_cast<uint128_t>( b );
        result           = result / static_cast<uint128_t>( scaleFactor( precision ) );

        if ( result > std::numeric_limits<uint64_t>::max() )
        {
            return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
        }
        return outcome::success( static_cast<uint64_t>( result ) );
    }

    outcome::result<uint64_t> fixed_point::divide( uint64_t a, uint64_t b, uint64_t precision )
    {
        using namespace boost::multiprecision;
        if ( b == 0 )
        {
            return outcome::failure( std::make_error_code( std::errc::result_out_of_range ) );
        }

        uint128_t result = ( static_cast<uint128_t>( a ) * static_cast<uint128_t>( scaleFactor( precision ) ) ) /
                           static_cast<uint128_t>( b );

        if ( result > std::numeric_limits<uint64_t>::max() )
        {
            return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
        }

        return outcome::success( static_cast<uint64_t>( result ) );
    }

    outcome::result<uint64_t> fixed_point::convertPrecision( uint64_t value,
                                                             uint64_t from_precision,
                                                             uint64_t to_precision )
    {
        using namespace boost::multiprecision;
        if ( from_precision == to_precision )
        {
            return outcome::success( value );
        }

        if ( to_precision > from_precision )
        {
            uint64_t  delta  = to_precision - from_precision;
            uint128_t result = static_cast<uint128_t>( value ) * static_cast<uint128_t>( scaleFactor( delta ) );
            if ( result > std::numeric_limits<uint64_t>::max() )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }
            return outcome::success( static_cast<uint64_t>( result ) );
        }
        uint64_t delta     = from_precision - to_precision;
        uint64_t converted = value / scaleFactor( delta );
        return outcome::success( converted );
    }

    uint64_t fixed_point::value() const noexcept
    {
        return value_;
    }

    uint64_t fixed_point::precision() const noexcept
    {
        return precision_;
    }

    outcome::result<fixed_point> fixed_point::add( const fixed_point &other ) const
    {
        if ( precision_ != other.precision_ )
        {
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }
        using namespace boost::multiprecision;
        uint128_t sum = static_cast<uint128_t>( value_ ) + static_cast<uint128_t>( other.value_ );
        if ( sum > std::numeric_limits<uint64_t>::max() )
        {
            return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
        }
        return outcome::success( fixed_point( static_cast<uint64_t>( sum ), precision_ ) );
    }

    outcome::result<fixed_point> fixed_point::subtract( const fixed_point &other ) const
    {
        if ( precision_ != other.precision_ )
        {
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }
        if ( other.value_ > value_ )
        {
            return outcome::failure( std::make_error_code( std::errc::result_out_of_range ) );
        }
        return outcome::success( fixed_point( value_ - other.value_, precision_ ) );
    }

    outcome::result<fixed_point> fixed_point::multiply( const fixed_point &other ) const
    {
        if ( precision_ != other.precision_ )
        {
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }
        auto raw = fixed_point::multiply( value_, other.value_, precision_ );
        if ( !raw )
        {
            return outcome::failure( raw.error() );
        }
        return outcome::success( fixed_point( raw.value(), precision_ ) );
    }

    outcome::result<fixed_point> fixed_point::divide( const fixed_point &other ) const
    {
        if ( precision_ != other.precision_ )
        {
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }
        auto raw = fixed_point::divide( value_, other.value_, precision_ );
        if ( !raw )
        {
            return outcome::failure( raw.error() );
        }
        return outcome::success( fixed_point( raw.value(), precision_ ) );
    }

    outcome::result<fixed_point> fixed_point::convertPrecision( uint64_t to_precision ) const
    {
        auto raw = fixed_point::convertPrecision( value_, precision_, to_precision );
        if ( !raw )
        {
            return outcome::failure( raw.error() );
        }
        return outcome::success( fixed_point( raw.value(), to_precision ) );
    }
} // namespace sgns
