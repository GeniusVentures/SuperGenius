/**
 * @file        ScaledInteger.cpp
 * @author      Luiz Guilherme Rizzatto Zucchi (luizgrz@gmail.com)
 * @brief       Fixed-point arithmetic utilities (Implementation file)
 * @version     1.0
 * @date        2025-01-29
 * @copyright   Copyright (c) 2025
 */

#include "ScaledInteger.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>
#include <memory>
#include <boost/multiprecision/cpp_int.hpp>

namespace sgns
{

    outcome::result<std::shared_ptr<ScaledInteger>> ScaledInteger::New( uint64_t raw_value, uint64_t precision )
    {
        auto ptr = std::shared_ptr<ScaledInteger>( new ScaledInteger( raw_value, precision ) );
        return outcome::success( ptr );
    }

    outcome::result<std::shared_ptr<ScaledInteger>> ScaledInteger::New( double raw_value, uint64_t precision )
    {
        OUTCOME_TRY( auto &&from_dbl_value, FromDouble( raw_value, precision ) );
        auto ptr = std::shared_ptr<ScaledInteger>( new ScaledInteger( from_dbl_value, precision ) );
        return outcome::success( ptr );
    }

    outcome::result<std::shared_ptr<ScaledInteger>> ScaledInteger::New( const std::string &str_value,
                                                                        uint64_t           precision )
    {
        OUTCOME_TRY( auto &&from_str_value, FromString( str_value, precision ) );
        auto ptr = std::shared_ptr<ScaledInteger>( new ScaledInteger( from_str_value, precision ) );
        return outcome::success( ptr );
    }

    outcome::result<std::shared_ptr<ScaledInteger>> ScaledInteger::New( const std::string &str_value )
    {
        size_t   dot_pos = str_value.find( '.' );
        uint64_t precision_calc;
        if ( dot_pos == std::string::npos )
        {
            precision_calc = 0;
        }
        else
        {
            precision_calc = str_value.size() - dot_pos - 1;
        }
        OUTCOME_TRY( auto &&raw_value, FromString( str_value, precision_calc ) );
        auto ptr = std::shared_ptr<ScaledInteger>( new ScaledInteger( raw_value, precision_calc ) );
        return outcome::success( ptr );
    }

    ScaledInteger::ScaledInteger( uint64_t value, uint64_t precision ) : value_( value ), precision_( precision ) {}

    constexpr uint64_t ScaledInteger::ScaleFactor( uint64_t precision )
    {
        uint64_t result = 1;
        for ( uint64_t i = 0; i < precision; ++i )
        {
            result *= 10;
        }
        return result;
    }

    outcome::result<uint64_t> ScaledInteger::FromString( const std::string &str_value, uint64_t precision )
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

        return outcome::success( ( integer_part * ScaleFactor( precision ) ) + fractional_part );
    }

    std::string ScaledInteger::ToString( uint64_t value, uint64_t precision )
    {
        uint64_t integer_part    = value / ScaleFactor( precision );
        uint64_t fractional_part = value % ScaleFactor( precision );

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

    outcome::result<uint64_t> ScaledInteger::FromDouble( double raw_value, uint64_t precision )
    {
        double factor  = static_cast<double>( ScaleFactor( precision ) );
        double scaled  = raw_value * factor;
        double rounded = std::round( scaled );

        if ( rounded < 0.0 || rounded > static_cast<double>( std::numeric_limits<uint64_t>::max() ) )
        {
            return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
        }
        return outcome::success( static_cast<uint64_t>( rounded ) );
    }

    outcome::result<uint64_t> ScaledInteger::Multiply( uint64_t a, uint64_t b, uint64_t precision )
    {
        using namespace boost::multiprecision;
        uint128_t result = static_cast<uint128_t>( a ) * static_cast<uint128_t>( b );
        result           = result / static_cast<uint128_t>( ScaleFactor( precision ) );

        if ( result > std::numeric_limits<uint64_t>::max() )
        {
            return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
        }
        return outcome::success( static_cast<uint64_t>( result ) );
    }

    outcome::result<uint64_t> ScaledInteger::Divide( uint64_t a, uint64_t b, uint64_t precision )
    {
        using namespace boost::multiprecision;
        if ( b == 0 )
        {
            return outcome::failure( std::make_error_code( std::errc::result_out_of_range ) );
        }

        uint128_t result = ( static_cast<uint128_t>( a ) * static_cast<uint128_t>( ScaleFactor( precision ) ) ) /
                           static_cast<uint128_t>( b );

        if ( result > std::numeric_limits<uint64_t>::max() )
        {
            return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
        }

        return outcome::success( static_cast<uint64_t>( result ) );
    }

    outcome::result<uint64_t> ScaledInteger::ConvertPrecision( uint64_t value, uint64_t from, uint64_t to )
    {
        using namespace boost::multiprecision;
        if ( from == to )
        {
            return outcome::success( value );
        }

        if ( to > from )
        {
            uint64_t  delta  = to - from;
            uint128_t result = static_cast<uint128_t>( value ) * static_cast<uint128_t>( ScaleFactor( delta ) );
            if ( result > std::numeric_limits<uint64_t>::max() )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }
            return outcome::success( static_cast<uint64_t>( result ) );
        }
        uint64_t delta     = from - to;
        uint64_t converted = value / ScaleFactor( delta );
        return outcome::success( converted );
    }

    uint64_t ScaledInteger::Value() const noexcept
    {
        return value_;
    }

    uint64_t ScaledInteger::Precision() const noexcept
    {
        return precision_;
    }

    std::string ScaledInteger::ToString( bool fixedDecimals ) const
    {
        std::string s = ToString( value_, precision_ );

        if ( !fixedDecimals )
        {
            auto dotPos = s.find( '.' );
            if ( dotPos != std::string::npos )
            {
                auto lastNonZero = s.find_last_not_of( '0' );
                if ( lastNonZero != std::string::npos )
                {
                    s.erase( lastNonZero + 1 );
                }
                if ( !s.empty() && s.back() == '.' )
                {
                    s.pop_back();
                }
            }
        }

        return s;
    }

    outcome::result<ScaledInteger> ScaledInteger::Add( const ScaledInteger &other ) const
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
        return outcome::success( ScaledInteger( static_cast<uint64_t>( sum ), precision_ ) );
    }

    outcome::result<ScaledInteger> ScaledInteger::Subtract( const ScaledInteger &other ) const
    {
        if ( precision_ != other.precision_ )
        {
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }
        if ( other.value_ > value_ )
        {
            return outcome::failure( std::make_error_code( std::errc::result_out_of_range ) );
        }
        return outcome::success( ScaledInteger( value_ - other.value_, precision_ ) );
    }

    outcome::result<ScaledInteger> ScaledInteger::Multiply( const ScaledInteger &other ) const
    {
        if ( precision_ != other.precision_ )
        {
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }
        OUTCOME_TRY( auto &&multiply_res, ScaledInteger::Multiply( value_, other.value_, precision_ ) );
        return outcome::success( ScaledInteger( multiply_res, precision_ ) );
    }

    outcome::result<ScaledInteger> ScaledInteger::Divide( const ScaledInteger &other ) const
    {
        if ( precision_ != other.precision_ )
        {
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }
        OUTCOME_TRY( auto &&divide_res, ScaledInteger::Divide( value_, other.value_, precision_ ) );
        return outcome::success( ScaledInteger( divide_res, precision_ ) );
    }

    outcome::result<ScaledInteger> ScaledInteger::ConvertPrecision( uint64_t to ) const
    {
        OUTCOME_TRY( auto &&convert_res, ScaledInteger::ConvertPrecision( value_, precision_, to ) );
        return outcome::success( ScaledInteger( convert_res, to ) );
    }
} // namespace sgns
