#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace sgns
{
    /// @brief Where a price quote came from (QUOTE-02).
    /// @note OnChain is reserved for the SRC-01 deferred on-chain fallback tier
    /// and has no implementation in this phase.
    enum class PriceSource
    {
        LocalCache,
        CoinGecko,
        GnusPriceService,
        OnChain // reserved, SRC-01 deferred
    };

    /// @brief Provider-independent price quote (QUOTE-01).
    /// @brief The timestamp is FETCH time per D-14: the moment the price was
    /// fetched from the source, aligned with the Phase-1 envelope's per-id
    /// fetchedAt semantics. Freshness is computed as now - timestamp; the
    /// quote is never re-based on local-store time.
    struct PriceQuote
    {
        /// @brief CoinGecko asset id, e.g. "genius-ai"
        std::string asset;

        /// @brief Target currency, e.g. "usd"
        std::string currency;

        /// @brief Price in the target currency
        double price = 0.0;

        /// @brief D-14: fetch time (not local-store time). Freshness = now - timestamp.
        std::chrono::system_clock::time_point timestamp{};

        /// @brief Which tier produced this quote
        PriceSource source = PriceSource::CoinGecko;

        /// @brief Whether the serving tier marked the value as stale
        bool stale = false;

        /// @brief Interop accessor: the fetch time as seconds since the Unix epoch.
        /// @return timestamp converted to whole seconds since epoch
        /// @note Consumers that exchange epoch-seconds (e.g. the Phase-1 envelope
        /// fetchedAt field) use this instead of poking the time_point directly.
        int64_t FetchedAtEpochSeconds() const
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       timestamp.time_since_epoch() )
                .count();
        }
    };
}
