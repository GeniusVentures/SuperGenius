#pragma once

#include "PriceFetchError.hpp"
#include "PriceQuote.hpp"
#include "PriceRetryPolicy.hpp"

#include "boost/asio.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "base/logger.hpp"

namespace sgns
{
    /// @brief Price-fetch facade over the status-aware sgns::HTTPClient
    /// transport (02-02): applies the status-before-parse gate (LPM-05),
    /// CoinGecko-friendly UA and 5s timeouts (LPM-06, D-08/D-10), the
    /// transient-only retry schedule with 429 hold-off (LPM-07, D-11..D-13),
    /// and produces PriceQuote objects with fetch-time timestamps (D-14).
    class PriceHttpClient
    {
    public:
        /// @brief Construct a price client against a base URL.
        /// @param baseUrl e.g. "https://api.coingecko.com" (TLS on, pinned CA
        /// via SGNS_DEFAULT_CACERT_PATH) or "http://127.0.0.1:<port>" (TLS
        /// off — how tests redirect; also D-07's construction-time override)
        /// @param retryConfig Retry schedule; tests inject zero backoff
        /// @param holdOffDuration 429 hold-off window (D-13, >= 60s default)
        /// @param clock Injectable clock for hermetic hold-off tests
        /// @param requestTimeout Per-request connect/handshake/read timeout
        /// (LPM-06 default 5000ms; tests may shorten to force timeouts)
        PriceHttpClient( std::string                         baseUrl,
                         RetryConfig                          retryConfig          = {},
                         std::chrono::seconds                holdOffDuration      = std::chrono::seconds( 60 ),
                         RateLimitHoldOff::Clock              clock                = [] { return std::chrono::system_clock::now(); },
                         std::chrono::milliseconds           requestTimeout      = std::chrono::milliseconds( 5000 ) );

        /// @brief Fetch current prices for the given CoinGecko ids.
        /// @param ioc Caller-supplied io_context (D-09 — the caller owns the
        /// executor and the run-loop)
        /// @param tokenIds CoinGecko asset ids, e.g. {"genius-ai"}
        /// @param currency Target currency, default "usd"
        /// @return One PriceQuote per id the source returned (unknown ids are
        /// absent, not errors — Phase-1 D-09 semantics), or a status-bearing
        /// PriceFetchFailure
        PriceResult<std::vector<PriceQuote>> FetchPrices( std::shared_ptr<boost::asio::io_context> ioc,
                                                          const std::vector<std::string>          &tokenIds,
                                                          const std::string                       &currency = "usd" );

        /// @brief Attempts made by the most recent FetchPrices call
        /// (test-visible retry accounting).
        int AttemptsLastFetch() const
        {
            return attemptsLastFetch_.load();
        }

        /// @brief The CoinGecko-friendly UA this facade sends (LPM-06/D-08).
        static constexpr const char *kUserAgent = "SGNS-PriceClient/1.0 (+https://gnus.ai; SuperGenius node price fetch)";

    private:
        std::string          baseUrl_;
        RetryConfig          retryConfig_;
        std::chrono::seconds holdOffDuration_;
        RateLimitHoldOff     holdOff_;
        std::chrono::milliseconds requestTimeout_;
        std::atomic<int>     attemptsLastFetch_{ 0 };
        base::Logger         m_logger = sgns::base::createLogger( "PriceHttpClient" );
    };
} // namespace sgns
