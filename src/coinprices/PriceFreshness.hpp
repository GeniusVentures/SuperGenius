#pragma once

#include <chrono>

namespace sgns
{
    /// @brief Shared freshness band constants (D-16) consumed by code and tests.
    inline constexpr std::chrono::seconds kFreshMaxAge{ 60 };
    inline constexpr std::chrono::seconds kStaleMaxAge{ 300 };

    /// @brief Freshness classification for a PriceQuote (FRESH-02).
    enum class FreshnessBand
    {
        Fresh,
        StaleButUsable,
        Unavailable
    };

    /// @brief Classify the freshness of a fetched quote.
    /// @param fetchedAt D-14 fetch time of the quote
    /// @param now Current time to classify against
    /// @return The freshness band for age = now - fetchedAt
    /// @note Boundary rule (D-16): bands are closed on the fresh side.
    /// An age of exactly kFreshMaxAge (60s) classifies Fresh, and an age of
    /// exactly kStaleMaxAge (300s) classifies StaleButUsable.
    /// @note The age is compared at the clock's native precision (no truncation
    /// to whole seconds) so sub-second boundary cases classify exactly.
    inline FreshnessBand ClassifyFreshness( std::chrono::system_clock::time_point fetchedAt,
                                             std::chrono::system_clock::time_point now )
    {
        const auto age = now - fetchedAt;
        if ( age <= kFreshMaxAge )
        {
            return FreshnessBand::Fresh;
        }
        if ( age <= kStaleMaxAge )
        {
            return FreshnessBand::StaleButUsable;
        }
        return FreshnessBand::Unavailable;
    }
}
