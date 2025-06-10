/**
 * @file TokenAmount.cpp
 * @brief Implementation of TokenAmount arithmetic helpers.
 */

#include "TokenAmount.hpp"
#include "base/ScaledInteger.hpp"
#include <limits>

namespace sgns
{

    outcome::result<std::shared_ptr<TokenAmount>> TokenAmount::New( uint64_t raw_minions )
    {
        auto ptr = std::shared_ptr<TokenAmount>( new TokenAmount( raw_minions ) );
        return outcome::success( ptr );
    }

    outcome::result<std::shared_ptr<TokenAmount>> TokenAmount::New( double value )
    {
        OUTCOME_TRY( auto &&from_dbl_value, ScaledInteger::FromDouble( value, PRECISION ) );
        auto ptr = std::shared_ptr<TokenAmount>( new TokenAmount( from_dbl_value ) );
        return outcome::success( ptr );
    }

    outcome::result<std::shared_ptr<TokenAmount>> TokenAmount::New( const std::string &str )
    {
        OUTCOME_TRY( auto &&from_str_value, ScaledInteger::FromString( str, PRECISION ) );
        auto ptr = std::shared_ptr<TokenAmount>( new TokenAmount( from_str_value ) );
        return outcome::success( ptr );
    }

    TokenAmount::TokenAmount( uint64_t minions ) : minions_( minions ) {}

    outcome::result<TokenAmount> TokenAmount::Multiply( const TokenAmount &other ) const
    {
        OUTCOME_TRY( auto &&multiply_res, ScaledInteger::Multiply( minions_, other.minions_, PRECISION ) );
        return outcome::success( TokenAmount( multiply_res ) );
    }

    outcome::result<TokenAmount> TokenAmount::Divide( const TokenAmount &other ) const
    {
        OUTCOME_TRY( auto &&divide_res, ScaledInteger::Divide( minions_, other.minions_, PRECISION ) );
        return outcome::success( TokenAmount( divide_res ) );
    }

    uint64_t TokenAmount::Value() const
    {
        return minions_;
    }

    outcome::result<uint64_t> TokenAmount::ParseMinions( const std::string &str )
    {
        return ScaledInteger::FromString( str, PRECISION );
    }

    std::string TokenAmount::FormatMinions( uint64_t minions )
    {
        return ScaledInteger::ToString( minions, PRECISION );
    }

    outcome::result<uint64_t> TokenAmount::CalculateCostMinions( uint64_t total_bytes, double price_usd_per_genius )
    {
        uint128_t product = static_cast<uint128_t>( total_bytes ) * static_cast<uint128_t>( PRICE_PER_FLOP ) *
                            static_cast<uint128_t>( FLOPS_PER_BYTE );
        std::shared_ptr<ScaledInteger> cost_fp;
        uint128_t                      divisor = 1;

        for ( uint64_t prec = PRICE_PER_FLOP_FRACTIONAL_DIGITS; prec >= PRECISION; prec-- )
        {
            uint128_t scaled = product / divisor;
            divisor          = divisor * 10;
            if ( scaled > std::numeric_limits<uint64_t>::max() )
            {
                continue;
            }

            auto total_fp = ScaledInteger::New( static_cast<uint64_t>( scaled ), prec );
            if ( !total_fp )
            {
                continue;
            }

            auto price_fp = ScaledInteger::New( price_usd_per_genius, prec );
            if ( !price_fp )
            {
                continue;
            }

            auto div_res = total_fp.value()->Divide( *price_fp.value() );
            if ( !div_res )
            {
                continue;
            }

            cost_fp = std::make_shared<sgns::ScaledInteger>( std::move( div_res.value() ) );
            break;
        }

        if ( !cost_fp || cost_fp->Precision() < PRECISION )
        {
            return outcome::failure( std::errc::value_too_large );
        }

        OUTCOME_TRY( auto &&minions_res, cost_fp->ConvertPrecision( PRECISION ) );
        uint64_t raw_minions = minions_res.Value();
        raw_minions          = std::max( raw_minions, MIN_MINION_UNITS );

        return outcome::success( raw_minions );
    }

    outcome::result<std::string> TokenAmount::ConvertToChildToken( uint64_t in, std::string ratio )
    {
        OUTCOME_TRY( auto ratio_fp, ScaledInteger::New( ratio, PRECISION ) );

        OUTCOME_TRY( auto minion_fp, ScaledInteger::New( in, PRECISION ) );

        OUTCOME_TRY( auto child_fp, minion_fp->Divide( *ratio_fp ) );

        return outcome::success( child_fp.ToString() );
    }

    outcome::result<uint64_t> TokenAmount::ConvertFromChildToken( std::string in, std::string ratio )
    {
        OUTCOME_TRY( auto ratio_fp, ScaledInteger::New( ratio, PRECISION ) );

        OUTCOME_TRY( auto child_fp, ScaledInteger::New( in, PRECISION, ScaledInteger::ParseMode::Truncate ) );

        OUTCOME_TRY( auto minion_fp, child_fp->Multiply( *ratio_fp ) );

        return outcome::success( minion_fp.Value() );
    }

} // namespace sgns
