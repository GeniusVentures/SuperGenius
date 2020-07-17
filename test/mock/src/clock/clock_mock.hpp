

#ifndef SUPERGENIUS_TEST_CORE_CLOCK_CLOCK_MOCK_HPP
#define SUPERGENIUS_TEST_CORE_CLOCK_CLOCK_MOCK_HPP

#include "clock/clock.hpp"

#include <gmock/gmock.h>

namespace sgns::clock {

  class SystemClockMock : public SystemClock {
   public:
    MOCK_CONST_METHOD0(now, SystemClock::TimePoint());
    MOCK_CONST_METHOD0(nowUint64, uint64_t());
  };

  class SteadyClockMock : public SteadyClock {
   public:
    MOCK_CONST_METHOD0(now, SteadyClock::TimePoint());
    MOCK_CONST_METHOD0(nowUint64, uint64_t());
  };

}  // namespace sgns::clock

#endif  // SUPERGENIUS_TEST_CORE_CLOCK_CLOCK_MOCK_HPP
