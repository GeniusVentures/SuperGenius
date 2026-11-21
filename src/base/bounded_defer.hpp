/**
 * @file       bounded_defer.hpp
 * @brief      Generic bounded defer-and-hold tracker (assessment decision D-03).
 *
 * Mechanism only, policy-free: which authorization boundary defers a key, what
 * the key waits for, and what an expired key means all stay with the consumers
 * (consensus trust deferral in 21-04, mint ingress in 21-06). This class only
 * tracks, per string key, the FIRST deadline the key was deferred with, answers
 * whether that deadline has elapsed, and bounds its own memory.
 *
 * Contract highlights:
 *   - Deadlines are first-seen anchored: a repeated Defer never refreshes the
 *     window, so replaying a key cannot extend its own holdback.
 *   - At the entry cap, inserting a NEW key evicts exactly the OLDEST tracked
 *     key (single-oldest eviction, never a wholesale clear), so a flooding
 *     peer cannot flush younger entries out of the table.
 *   - An evicted or cleared key is untracked again; deferring it later starts
 *     a fresh deadline.
 *   - Thread-safe: consumers call from filter/consensus threads.
 *
 * The clock is injectable (NowProvider over steady-clock time points) so tests
 * advance time deterministically instead of sleeping.
 */

#ifndef SUPERGENIUS_BASE_BOUNDED_DEFER_HPP
#define SUPERGENIUS_BASE_BOUNDED_DEFER_HPP

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace sgns::base
{
    /**
     * @brief Bounded FIFO defer tracker with first-seen deadlines (D-03).
     *
     * Records per-key defer deadlines with a configurable window and a capped
     * entry table (oldest-first eviction at cap). Holds no verdicts.
     */
    class BoundedDeferTracker
    {
    public:
        using Clock       = std::chrono::steady_clock;
        using TimePoint   = Clock::time_point;
        using NowProvider = std::function<TimePoint()>;

        /// D-03 user emphasis: the deferral window must be generous (30 min default).
        static constexpr std::chrono::minutes kDefaultDeferWindow{ 30 };
        /// Memory backstop cap (T-21-0303): bounded FIFO with oldest-first eviction.
        static constexpr size_t kDefaultMaxTrackedEntries = 4096;

        /**
         * @brief Constructs the tracker.
         * @param[in] window Defer window applied to newly deferred keys.
         * @param[in] max_entries Entry cap; inserting beyond it evicts the oldest key.
         * @param[in] now_provider Injectable clock; defaults to the real steady clock.
         */
        explicit BoundedDeferTracker(
            std::chrono::seconds window       = kDefaultDeferWindow,
            size_t               max_entries  = kDefaultMaxTrackedEntries,
            NowProvider          now_provider = [] { return Clock::now(); } );

        /**
         * @brief Replaces the defer window for keys deferred AFTER this call.
         * @param[in] window New defer window; existing deadlines are untouched
         *            (first-seen anchoring).
         */
        void SetWindow( std::chrono::seconds window );

        /**
         * @brief Defers @p key, anchoring its deadline on first sight.
         * @param[in] key Caller-defined identity of the deferred item.
         * @return Whether @p key was ALREADY expired under its first-seen
         *         deadline. A newly deferred key returns false. Repeated calls
         *         never extend the deadline.
         */
        bool Defer( const std::string &key );

        /**
         * @brief Reports whether @p key's first-seen deadline has elapsed.
         * @param[in] key Deferred identity to check.
         * @return True when @p key is tracked and its deadline elapsed; false
         *         for untracked keys or pending ones.
         */
        bool Expired( const std::string &key ) const;

        /**
         * @brief Removes @p key from tracking (no deadline remains).
         * @param[in] key Deferred identity to stop tracking.
         */
        void Clear( const std::string &key );

        /**
         * @brief Number of currently tracked keys.
         * @return Tracked-entry count (bounded by the configured cap).
         */
        size_t Size() const;

    private:
        /// Evicts oldest-first while the table exceeds the cap. Caller holds mutex_.
        void EvictOverCapLocked();

        mutable std::mutex                         mutex_;           ///< Guards both containers below.
        std::unordered_map<std::string, TimePoint> deadline_by_key_; ///< First-seen deadline per key.
        std::deque<std::string>                    insertion_order_; ///< FIFO order for oldest-first eviction.
        std::chrono::seconds                       window_;          ///< Window applied to new deadlines.
        size_t                                     max_entries_;     ///< Entry cap.
        NowProvider                                now_provider_;    ///< Injectable clock.
    };
} // namespace sgns::base

#endif // SUPERGENIUS_BASE_BOUNDED_DEFER_HPP
