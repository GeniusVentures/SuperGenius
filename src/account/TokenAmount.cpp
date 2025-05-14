#include "TokenAmount.hpp"
#include "base/fixed_point.hpp"
#include <iostream>
#include <limits>

namespace sgns
{
    outcome::result<uint64_t> TokenAmount::ParseMinions( const std::string &str ) noexcept
    {
        return fixed_point::fromString( str, PRECISION );
    }

    std::string TokenAmount::FormatMinions( uint64_t minions ) noexcept
    {
        return fixed_point::toString( minions, PRECISION );
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

            auto totalRes = sgns::fixed_point::create( static_cast<uint64_t>( scaled ), prec );
            if ( !totalRes )
            {
                continue;
            }

            auto priceRes = sgns::fixed_point::create( priceUsdPerGenius, prec );
            if ( !priceRes )
            {
                continue;
            }

            auto divRes = totalRes.value()->divide( *priceRes.value() );
            if ( !divRes )
            {
                continue;
            }

            costFp = std::make_shared<sgns::fixed_point>( std::move( divRes.value() ) );
            break;
        }

        if ( !costFp || costFp->precision() < PRECISION )
        {
            return outcome::failure( std::errc::value_too_large );
        }

        auto minionRes = costFp->convertPrecision( PRECISION );
        if ( !minionRes )
        {
            return outcome::failure( minionRes.error() );
        }

        uint64_t rawMinions = minionRes.value().value();
        if ( rawMinions < MIN_MINION_UNITS )
        {
            rawMinions = MIN_MINION_UNITS;
        }

        return outcome::success( rawMinions );
    }

} // namespace sgns
