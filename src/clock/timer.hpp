#ifndef SUPERGENIUS_TIMER_HPP
#define SUPERGENIUS_TIMER_HPP

#include <functional>
#include <system_error>

#include "clock/clock.hpp"

namespace sgns::clock {
  /**
   * Interface for asynchronous timer
   */
  struct Timer {
    virtual ~Timer() = default;

    /**
     * Set an expire time for this timer
     * @param at - timepoint, at which the timer expires
     */
    virtual void expiresAt(clock::SystemClock::TimePoint at) = 0;

    /**
     * Wait for the timer expiration
     * @param h - handler, which is called, when the timer is expired, or error
     * happens
     */
    virtual void asyncWait(
        const std::function<void(const std::error_code &)> &h) = 0;
  };
}  // namespace sgns::clock

#endif  // SUPERGENIUS_TIMER_HPP
