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
        auto rateCoeff   = PRICE_PER_FLOP * FLOPS_PER_BYTE;
        auto usdRaw      = UINT64_C( 0 );
        auto fpPrecision = PRICE_PER_FLOP_FRACTIONAL_DIGITS;

        for ( ; fpPrecision >= 6; fpPrecision--, rateCoeff /= 10 )
        {
            auto product = static_cast<int128_t>( totalBytes ) * static_cast<int128_t>( rateCoeff );
            if ( product <= std::numeric_limits<uint64_t>::max() )
            {
                usdRaw = static_cast<uint64_t>( product );
                break;
            }
            if ( rateCoeff % 10 )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
        }

        if ( fpPrecision < 6 )
        {
            return outcome::failure( std::errc::value_too_large );
        }

        fixed_point totalUsdFp( usdRaw, fpPrecision );
        fixed_point priceFp( priceUsdPerGenius, fpPrecision );

        auto geniusFpRes = totalUsdFp.divide( priceFp );
        if ( !geniusFpRes )
        {
            return outcome::failure( geniusFpRes.error() );
        }

        auto geniusFp    = geniusFpRes.value();
        auto minionFpRes = geniusFp.convertPrecision( PRECISION );
        if ( !minionFpRes )
        {
            return outcome::failure( minionFpRes.error() );
        }

        auto minionFp   = minionFpRes.value();
        auto rawMinions = minionFp.value();
        if ( rawMinions < MIN_MINION_UNITS )
        {
            rawMinions = MIN_MINION_UNITS;
        }

        return outcome::success( rawMinions );
    }

} // namespace sgns
