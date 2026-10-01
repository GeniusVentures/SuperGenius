/**
 * Header file for PriceHttpClientSource — the production IPriceSource
 * adapter over PriceHttpClient (Phase 3, D-13/D-04). Header-only: the
 * FetchPrices override is a single forwarding return.
 */
#pragma once

#include "IPriceSource.hpp"
#include "PriceHttpClient.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sgns
{
    /// @brief Production tier adapter: a PriceHttpClient wrapped in the
    /// IPriceSource seam, closing over the manager's io_context (D-04).
    class PriceHttpClientSource : public IPriceSource
    {
    public:
        /// @brief Construct the adapter; forwards every config argument to
        /// the wrapped PriceHttpClient (same defaults).
        /// @param ioc The manager's io_context — closed over at construction
        /// @param baseUrl Tier base URL
        /// @param retryConfig Retry schedule
        /// @param holdOffDuration 429 hold-off window
        /// @param clock Injectable clock for hermetic hold-off tests
        /// @param requestTimeout Per-request connect/handshake/read timeout
        PriceHttpClientSource( std::shared_ptr<boost::asio::io_context> ioc,
                               std::string                              baseUrl,
                               RetryConfig                              retryConfig     = {},
                               std::chrono::seconds                     holdOffDuration = std::chrono::seconds( 60 ),
                               RateLimitHoldOff::Clock                  clock = [] { return std::chrono::system_clock::now(); },
                               std::chrono::milliseconds                requestTimeout = std::chrono::milliseconds( 5000 ) )
            : ioc_( std::move( ioc ) ),
              client_( std::move( baseUrl ),
                       std::move( retryConfig ),
                       holdOffDuration,
                       std::move( clock ),
                       requestTimeout )
        {
        }

        /// @brief Forward to the facade. The ioc parameter of
        /// PriceHttpClient::FetchPrices is currently unused inside the facade
        /// (a fresh per-attempt context is built at PriceHttpClient.cpp:109) —
        /// the adapter closes over the manager's ioc to satisfy D-02/D-04's
        /// construction-time wiring with zero Phase 2 signature changes and
        /// zero runtime effect today; do not build machinery around that
        /// parameter.
        /// @param tokenIds Asset ids
        /// @param currency Target currency
        /// @return One PriceQuote per id the source returned, or a failure
        PriceResult<std::vector<PriceQuote>> FetchPrices( const std::vector<std::string> &tokenIds,
                                                          const std::string              &currency = "usd" ) override
        {
            return client_.FetchPrices( ioc_, tokenIds, currency );
        }

    private:
        std::shared_ptr<boost::asio::io_context> ioc_;
        PriceHttpClient                          client_;
    };
} // namespace sgns
