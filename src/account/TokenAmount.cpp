/**
 * @file TokenAmount.cpp
 * @brief Implementation of TokenAmount arithmetic helpers.
 */

#include "TokenAmount.hpp"
#include "base/fixed_point.hpp"
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
        try
        {
            auto ptr = std::shared_ptr<TokenAmount>( new TokenAmount( value ) );
            return outcome::success( ptr );
        }
        catch ( const std::overflow_error & /*e*/ )
        {
            return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
        }
    }

    outcome::result<std::shared_ptr<TokenAmount>> TokenAmount::New( const std::string &str )
    {
        auto raw = fixed_point::FromString( str, PRECISION );
        if ( !raw )
        {
            return outcome::failure( raw.error() );
        }
        auto ptr = std::shared_ptr<TokenAmount>( new TokenAmount( raw.value() ) );
        return outcome::success( ptr );
    }

    TokenAmount::TokenAmount( uint64_t minion_units ) : minions_( minion_units ) {}

    TokenAmount::TokenAmount( double value )
    {
        minions_ = fixed_point::FromDouble( value, PRECISION );
    }

    outcome::result<TokenAmount> TokenAmount::Multiply( const TokenAmount &other ) const
    {
        auto raw = fixed_point::Multiply( minions_, other.minions_, PRECISION );
        if ( !raw )
        {
            return outcome::failure( raw.error() );
        }
        return outcome::success( TokenAmount( raw.value() ) );
    }

    outcome::result<TokenAmount> TokenAmount::Divide( const TokenAmount &other ) const
    {
        auto raw = fixed_point::Divide( minions_, other.minions_, PRECISION );
        if ( !raw )
        {
            return outcome::failure( raw.error() );
        }
        return outcome::success( TokenAmount( raw.value() ) );
    }

    uint64_t TokenAmount::Value() const
    {
        return minions_;
    }

    outcome::result<uint64_t> TokenAmount::ParseMinions( const std::string &str )
    {
        return fixed_point::FromString( str, PRECISION );
    }

    std::string TokenAmount::FormatMinions( uint64_t minions )
    {
        return fixed_point::ToString( minions, PRECISION );
    }

    outcome::result<uint64_t> TokenAmount::CalculateCostMinions( uint64_t totalBytes, double priceUsdPerGenius )
    {
        uint128_t product = static_cast<uint128_t>( totalBytes ) * static_cast<uint128_t>( PRICE_PER_FLOP ) *
                            static_cast<uint128_t>( FLOPS_PER_BYTE );

        std::shared_ptr<sgns::fixed_point> costFp;
        uint128_t                          divisor = 1;

        for ( uint64_t prec = PRICE_PER_FLOP_FRACTIONAL_DIGITS; prec >= PRECISION; prec-- )
        {
            uint128_t scaled = product / divisor;
            divisor          = divisor * 10;

            if ( scaled > std::numeric_limits<uint64_t>::max() )
            {
                continue;
            }

            auto totalRes = sgns::fixed_point::New( static_cast<uint64_t>( scaled ), prec );
            if ( !totalRes )
            {
                continue;
            }

            auto priceRes = sgns::fixed_point::New( priceUsdPerGenius, prec );
            if ( !priceRes )
            {
                continue;
            }

            auto divRes = totalRes.value()->Divide( *priceRes.value() );
            if ( !divRes )
            {
                continue;
            }

            costFp = std::make_shared<sgns::fixed_point>( std::move( divRes.value() ) );
            break;
        }

        if ( !costFp || costFp->Precision() < PRECISION )
        {
            return outcome::failure( std::errc::value_too_large );
        }

        auto minionRes = costFp->ConvertPrecision( PRECISION );
        if ( !minionRes )
        {
            return outcome::failure( minionRes.error() );
        }

        uint64_t rawMinions = minionRes.value().Value();
        if ( rawMinions < MIN_MINION_UNITS )
        {
            rawMinions = MIN_MINION_UNITS;
        }

        return outcome::success( rawMinions );
    }

} // namespace sgns
