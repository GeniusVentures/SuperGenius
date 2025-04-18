#ifndef SUPERGENIUS_CLOCK_HPP
#define SUPERGENIUS_CLOCK_HPP

#include <chrono>
#include "singleton/IComponent.hpp"

namespace sgns::clock {

  /**
   * An interface for a clock
   * @tparam clock type is an underlying clock type, such as std::steady_clock
   */
  template <typename ClockType>
  class Clock: public IComponent {
   public:
    /**
     * Difference between two time points
     */
    using Duration = typename ClockType::duration;
    /**
     * A moment in time, stored in milliseconds since Unix epoch start
     */
    using TimePoint = typename ClockType::time_point;

    ~Clock() override = default;

    /**
     * @return a time point representing the current time
     */
    virtual TimePoint now() const = 0;

    /**
     * @return uint64_t representing number of seconds since the beginning of
     * epoch (Jan 1, 1970)
     */
    virtual uint64_t nowUint64() const = 0;
  };

  /**
   * SteadyClock alias over Clock. Should be used when we need to measure
   * interval between two moments in time
   */
  using SteadyClock = Clock<std::chrono::steady_clock>;

  /**
   * SystemClock alias over Clock. Should be used when we need to watch current
   * time
   */
  using SystemClock = Clock<std::chrono::system_clock>;

}  // namespace sgns::clock

#endif  // SUPERGENIUS_CLOCK_HPP
