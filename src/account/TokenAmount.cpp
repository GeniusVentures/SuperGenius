#include "TokenAmount.hpp"
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
        uint64_t rateCoeff   = PRICE_PER_FLOP * FLOPS_PER_BYTE;
        int32_t  fpPrecision = PRICE_PER_FLOP_FRACTIONAL_DIGITS;

        std::cout << "[DEBUG] rateCoeff: " << rateCoeff << "\n";
        std::cout << "[DEBUG] totalBytes: " << totalBytes << "\n";

        uint64_t usdRaw = 0;

        // Try to multiply safely, reducing precision if overflow might occur
        while ( fpPrecision >= 6 )
        {
            if ( totalBytes <= std::numeric_limits<uint64_t>::max() / rateCoeff )
            {
                usdRaw = totalBytes * rateCoeff;
                std::cout << "[DEBUG] Safe multiplication succeeded with fpPrecision = " << fpPrecision << "\n";
                break;
            }
            else
            {
                fpPrecision--;
                rateCoeff /= 10;

                std::cout << "[DEBUG] Reducing precision: " << fpPrecision << ", new rateCoeff: " << rateCoeff << "\n";

                if ( rateCoeff == 0 )
                {
                    std::cout << "[ERROR] rateCoeff reduced to zero, aborting.\n";
                    return outcome::failure( std::errc::invalid_argument );
                }
            }
        }

        if ( fpPrecision < 6 )
        {
            std::cout << "[ERROR] Could not compute cost safely within uint64_t limits.\n";
            return outcome::failure( std::errc::value_too_large );
        }

        auto totalUsdFp = fixed_point( usdRaw, fpPrecision );
        std::cout << "[DEBUG] totalUsdFp: " << totalUsdFp.value() << " (fpPrecision: " << fpPrecision << ")\n";

        auto priceFp = fixed_point( priceUsdPerGenius, fpPrecision );
        std::cout << "[DEBUG] priceFp: " << priceFp.value() << "\n";

        auto geniusFpRes = totalUsdFp.divide( priceFp );
        if ( !geniusFpRes )
        {
            std::cout << "[ERROR] Division failed in geniusFpRes: " << geniusFpRes.error().message() << "\n";
            return outcome::failure( geniusFpRes.error() );
        }

        auto geniusFp = geniusFpRes.value();
        std::cout << "[DEBUG] geniusFp: " << geniusFp.value() << "\n";

        auto minionFpRes = geniusFp.convertPrecision( PRECISION );
        if ( !minionFpRes )
        {
            std::cout << "[ERROR] Precision conversion failed in minionFpRes: " << minionFpRes.error().message()
                      << "\n";
            return outcome::failure( minionFpRes.error() );
        }

        auto minionFp = minionFpRes.value();
        std::cout << "[DEBUG] minionFp: " << minionFp.value() << "\n";

        uint64_t rawMinions = minionFp.value();
        if ( rawMinions < MIN_MINION_UNITS )
        {
            std::cout << "[INFO] rawMinions < MIN_MINION_UNITS, enforcing minimum: " << MIN_MINION_UNITS << "\n";
            rawMinions = MIN_MINION_UNITS;
        }

        std::cout << "[SUCCESS] rawMinions: " << rawMinions << "\n";
        return outcome::success( rawMinions );
    }

} // namespace sgns
