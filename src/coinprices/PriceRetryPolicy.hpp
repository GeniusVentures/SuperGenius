#pragma once

#include "PriceFetchError.hpp"
#include <HTTPTypes.hpp>

#include <array>
#include <chrono>
#include <functional>
#include <mutex>
#include <utility>

namespace sgns
{
    /// @brief Fixed retry/backoff schedule (D-12): 1s -> 2s -> (4s, never
    /// reached under the default cap of 3 attempts). Tests inject zeros for
    /// deterministic zero-backoff runs.
    struct RetryConfig
    {
        int maxAttempts = 3;

        /// @brief Backoff waited BEFORE each retry, indexed by (attempt - 1).
        /// @brief Index 2 (4s) is documented as the would-be next step that
        /// the default cap never consumes.
        std::array<std::chrono::milliseconds, 3> backoffBeforeRetry{ std::chrono::milliseconds( 1000 ),
                                                                     std::chrono::milliseconds( 2000 ),
                                                                     std::chrono::milliseconds( 4000 ) };
    };

    /// @brief Outcome of a retry decision.
    enum class RetryDecision
    {
        Retry,
        GiveUp
    };

    /// @brief True only for transport-class failures (D-11): timeouts,
    /// connection resets, DNS failures. NEVER true for a status-bearing
    /// failure (403/429/any HTTP status), parse errors, or input errors.
    bool IsTransient( const PriceFetchFailure &error );

    /// @brief Pre-mapping classifier over the transport's own error enum —
    /// true for ClientError::TIMEOUT / CONNECT_FAILED / RESOLVE_FAILED only.
    bool IsTransientTransport( http::ClientError error );

    /// @brief Decide whether to make another attempt after a failure.
    /// @param error The failure from the just-finished attempt
    /// @param attempt 1-based index of the attempt that just failed
    /// @param config The retry schedule in force
    /// @return Retry iff the error is transient AND attempt < maxAttempts;
    /// GiveUp otherwise (403/429 fall through immediately by construction)
    RetryDecision ShouldRetry( const PriceFetchFailure &error, int attempt, const RetryConfig &config );

    /// @brief Client-side CoinGecko rate-limit hold-off (D-13): a 429 sets a
    /// hold-until timestamp; while held, the CoinGecko tier is skipped
    /// entirely (fallback goes straight to the next tier).
    /// @brief The duration is a LOCAL policy constant (>= 60s default) — it is
    /// NEVER parsed from Retry-After / x-ratelimit-* headers: the anonymous
    /// CoinGecko endpoint sends none.
    class RateLimitHoldOff
    {
    public:
        /// @brief Injectable clock for hermetic tests.
        using Clock = std::function<std::chrono::system_clock::time_point()>;

        /// @param now Clock source; defaults to the real system clock.
        explicit RateLimitHoldOff( Clock now = [] { return std::chrono::system_clock::now(); } )
            : now_( std::move( now ) )
        {
        }

        /// @brief Arm the hold-off window from now for the given duration.
        void TriggerHoldOff( std::chrono::seconds duration )
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            holdUntil_ = now_() + duration;
        }

        /// @brief True while the hold-off window is active; false before any
        /// trigger and after the window expires.
        bool IsHeldOff() const
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            return now_() < holdUntil_;
        }

    private:
        mutable std::mutex                        mutex_;
        std::chrono::system_clock::time_point     holdUntil_{};
        Clock                                     now_;
    };
} // namespace sgns
