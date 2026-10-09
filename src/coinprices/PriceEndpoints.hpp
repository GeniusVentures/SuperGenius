/**
 * Header file for PriceEndpoints — production price-tier base-URL
 * configuration via environment variables (Phase 4, LPM-09, D-01/D-02).
 * Header-only: both accessors are tiny free functions over std::getenv.
 */
#pragma once

#include <cstdlib>
#include <string>

namespace sgns
{
    /// @brief Production default base URL for tier 1 (CoinGecko direct).
    /// The client appends "/api/v3/simple/price".
    inline constexpr const char *kCoinGeckoBaseUrlDefault = "https://api.coingecko.com";

    /// @brief Production default base URL for tier 2 (token.gnus.ai
    /// fallback). The client appends "/v1/prices".
    inline constexpr const char *kFallbackBaseUrlDefault = "https://token.gnus.ai";

    /// @brief Resolve a price-tier base URL from the environment.
    /// @param envName Environment variable name to read
    /// @param productionDefault Base URL used when the variable is unset
    /// or empty
    /// @return The env value when non-null AND non-empty, otherwise the
    /// production default
    /// @note The read is intentionally NOT cached in a function-local
    /// static: D-03 requires a fresh read at every LocalPriceManager
    /// construction so per-test stub-port redirection takes effect; a
    /// static would freeze the first value process-wide.
    inline std::string GetPriceBaseUrl( const char *envName, const char *productionDefault )
    {
        const char *env = std::getenv( envName );
        if ( env != nullptr && *env != '\0' )
        {
            return std::string( env );
        }
        return std::string( productionDefault );
    }

    /// @brief Tier 1 base URL: SGNS_COINGECKO_URL when set and non-empty,
    /// otherwise https://api.coingecko.com (LPM-09 CoinGecko defaults).
    /// @return The resolved tier 1 base URL
    inline std::string GetCoinGeckoBaseUrl()
    {
        return GetPriceBaseUrl( "SGNS_COINGECKO_URL", kCoinGeckoBaseUrlDefault );
    }

    /// @brief Tier 2 base URL: SGNS_PRICE_FALLBACK_URL when set and
    /// non-empty, otherwise https://token.gnus.ai (LPM-09 defaults).
    /// @return The resolved tier 2 base URL
    inline std::string GetFallbackBaseUrl()
    {
        return GetPriceBaseUrl( "SGNS_PRICE_FALLBACK_URL", kFallbackBaseUrlDefault );
    }
} // namespace sgns
