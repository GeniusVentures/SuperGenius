/**
 * Header file for PriceResponseParsers — body→PriceQuote assembly for the
 * two price tiers (Phase 3, 03-03): ParseCoinGeckoSimplePrice (extracted
 * behavior-identically from PriceHttpClient.cpp's parse branch) and
 * ParseGnusPriceEnvelope (the Phase-1 envelope contract, D-16).
 */
#pragma once

#include "PriceFetchError.hpp"
#include "PriceQuote.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace sgns
{
    /// @brief Which wire format a PriceHttpClient instance speaks.
    enum class ResponseFormat
    {
        CoinGeckoSimplePrice, ///< tier 1 — CoinGecko direct: {"id":{"usd":0.19}}
        GnusEnvelope          ///< tier 2 — token.gnus.ai: {currency, prices, fetchedAt, age, source, stale}
    };

    /// @brief Parse a CoinGecko /simple/price 200-body into quotes.
    /// @param body The 200 response body
    /// @param tokenIds Requested ids — quotes are emitted for requested ids
    /// present in the body; unknown ids stay absent, not errors (D-09)
    /// @param currency Target currency
    /// @param fetchTime D-14 fetch time stamped on every quote
    /// @return One PriceQuote per requested id found in the body, or
    /// NoDataFound when none matched, or JsonParseError on malformed JSON
    /// (status 0 — the HTTP status belongs to the facade layer)
    PriceResult<std::vector<PriceQuote>> ParseCoinGeckoSimplePrice( const std::string              &body,
                                                                    const std::vector<std::string> &tokenIds,
                                                                    const std::string              &currency,
                                                                    std::chrono::system_clock::time_point fetchTime );

    /// @brief Parse a token.gnus.ai price envelope into quotes.
    /// @param body The 200 response body (Phase-1 envelope.ts contract)
    /// @param tokenIds Requested ids — quotes are emitted for requested ids
    /// present in `prices`; absent ids stay absent, not errors (D-09)
    /// @param currency Expected target currency (envelope `currency`)
    /// @return One PriceQuote per requested id found in `prices`, or
    /// NoDataFound when none matched, or JsonParseError on malformed JSON /
    /// an unknown `source` string (status 0 — the HTTP status belongs to the
    /// facade layer)
    /// @note LANDMINE: envelope `fetchedAt` is epoch SECONDS (never ms) and
    /// is the max across the returned ids, SHARED by all quotes — do not
    /// invent per-id timestamps (Phase-1 D-11).
    /// @note LANDMINE: the envelope source names the upstream that PRODUCED
    /// the price, not the serving tier — "coingecko" maps to
    /// PriceSource::CoinGecko and "coingecko-cache" to
    /// PriceSource::GnusPriceService (Phase-1 D-06). Counterintuitive but
    /// contractual; do NOT "fix" this.
    PriceResult<std::vector<PriceQuote>> ParseGnusPriceEnvelope( const std::string              &body,
                                                                 const std::vector<std::string> &tokenIds,
                                                                 const std::string              &currency );
} // namespace sgns
