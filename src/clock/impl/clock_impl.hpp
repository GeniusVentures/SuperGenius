#ifndef SUPERGENIUS_CORE_CLOCK_IMPL_CLOCK_IMPL_HPP
#define SUPERGENIUS_CORE_CLOCK_IMPL_CLOCK_IMPL_HPP

#include "clock/clock.hpp"

namespace sgns::clock {

  template <typename ClockType>
  class ClockImpl : public Clock<ClockType> {
   public:
    typename Clock<ClockType>::TimePoint now() const override;
    uint64_t nowUint64() const override;

    std::string GetName() override
    {
      return "ClockImpl";
    }
  };

  // aliases for implementations
  using SteadyClockImpl = ClockImpl<std::chrono::steady_clock>;
  using SystemClockImpl = ClockImpl<std::chrono::system_clock>;

}  // namespace sgns::clock

#endif  // SUPERGENIUS_CORE_CLOCK_IMPL_CLOCK_IMPL_HPP
