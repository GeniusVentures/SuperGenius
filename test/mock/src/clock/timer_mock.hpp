

#ifndef SUPERGENIUS_TIMER_MOCK_HPP
#define SUPERGENIUS_TIMER_MOCK_HPP

#include <gmock/gmock.h>
#include "clock/timer.hpp"

namespace testutil {
  struct TimerMock : public sgns::clock::Timer {
    MOCK_METHOD1(expiresAt, void(sgns::clock::SystemClock::TimePoint));

    MOCK_METHOD1(asyncWait,
                 void(const std::function<void(const std::error_code &)> &h));
  };
}  // namespace testutil

#endif  // SUPERGENIUS_TIMER_MOCK_HPP
