/**
 * Header file for IPriceSource — the pure-virtual tier seam behind the
 * LocalPriceManager (Phase 3, D-13). Production adapters (PriceHttpClientSource)
 * and test fakes (FakePriceSource) both target this one interface.
 */
#pragma once

#include "PriceFetchError.hpp"
#include "PriceQuote.hpp"

#include <string>
#include <vector>

namespace sgns
{
    /// @brief One price-fetch tier behind the LocalPriceManager (D-13).
    /// @note The shape matches PriceHttpClient::FetchPrices minus the
    /// (currently vestigial, dead) ioc parameter — the manager owns the
    /// executor and hands it to the adapters at construction (D-04).
    class IPriceSource
    {
    public:
        /// @brief Virtual destructor to prevent memory leaks from derived classes
        virtual ~IPriceSource() = default;

        /// @brief Fetch current prices for the given asset ids.
        /// @param tokenIds Asset ids, e.g. {"genius-ai"}
        /// @param currency Target currency, default "usd"
        /// @return One PriceQuote per id the source returned (unknown ids are
        /// absent, not errors — Phase-1 D-09 semantics), or a status-bearing
        /// PriceFetchFailure
        virtual PriceResult<std::vector<PriceQuote>> FetchPrices( const std::vector<std::string> &tokenIds,
                                                                  const std::string              &currency = "usd" ) = 0;
    };
} // namespace sgns
