#ifndef SUPERGENIUS_GTEST_WAIT_CONDITION_HPP
#define SUPERGENIUS_GTEST_WAIT_CONDITION_HPP

#include <functional>
#include <thread>
#include <string>
#include <gtest/gtest.h>
#include "testutil/color_support.hpp"
#include "base/util.hpp"

namespace sgns::test {

    namespace Color {
        // ANSI color escape codes
        const std::string GREEN = "\033[32m";     // Info
        const std::string RED = "\033[31m";       // Error
        const std::string YELLOW = "\033[33m";    // Warning
        const std::string BLUE = "\033[34m";      // Debug
        const std::string MAGENTA = "\033[35m";   // Trace
        const std::string RESET = "\033[0m";
        const bool color_support = IsTerminalColorSupported();

        // Utility function for colored output with gtest-like prefix
        template<typename... Args>
        void ColoredPrint(const std::string& color,
                                  const std::string& prefix,
                                  const Args&... args) {
            std::ostringstream message;
            (message << ... << args);

            if (IsTerminalColorSupported()) {
                std::cout << color << prefix << Color::RESET << " "
                          << message.str() << std::endl;
            } else {
                std::cout << prefix << " " << message.str() << std::endl;
            }
        }

        // Convenience functions for different log levels
        template<typename... Args>
         void PrintInfo(const Args&... args) {
            ColoredPrint(Color::GREEN, "[     INFO ]", args...);
        }

        template<typename... Args>
        void PrintDebug(const Args&... args) {
            ColoredPrint(Color::BLUE, "[    DEBUG ]", args...);
        }

        template<typename... Args>
        void PrintError(const Args&... args) {
            ColoredPrint(Color::RED, "[    ERROR ]", args...);
        }

        template<typename... Args>
        void PrintTrace(const Args&... args) {
            ColoredPrint(Color::MAGENTA, "[    TRACE ]", args...);
        }

        template<typename... Args>
        void PrintWarning(const Args&... args) {
            ColoredPrint(Color::YELLOW, "[  WARNING ]", args...);

        }
    }

/**
 * Assert that a condition becomes true within the timeout (fatal assertion)
 * @param condition a callable that returns bool, true when condition is met
 * @param timeout maximum time to wait
 * @param description optional description of what we're waiting for
 * @param actualDuration optional pointer to store the actual wait duration
 * @param check_interval time to wait between condition checks
 */
    template <typename Condition>
    void assertWaitForCondition(Condition condition,
                               std::chrono::milliseconds timeout,
                               const std::string& description = "",
                               std::chrono::milliseconds* actualDuration = nullptr,
                               std::chrono::milliseconds check_interval = std::chrono::milliseconds(10),
                               const char* file_name = __FILE__,
                               int line_number = __LINE__)
    {
        bool result = waitForCondition(condition, timeout, actualDuration, check_interval);

        if (!result)
        {
            std::string message = "Timed out waiting for condition";
            if (!description.empty())
            {
                message += ": " + description;
            }
            message += " (timeout: " + std::to_string(timeout.count()) + "ms)";

            // This will report the failure at the caller's source location
            // Report the failure with the correct source location and terminate the test
            GTEST_MESSAGE_AT_(file_name, line_number, message.c_str(), ::testing::TestPartResult::kFatalFailure);
        }
    }

#define ASSERT_WAIT_FOR_CONDITION(condition, timeout, description, actual_duration) \
sgns::test::assertWaitForCondition(condition, timeout, description, actual_duration, std::chrono::milliseconds(10), __FILE__, __LINE__)

#define ASSERT_WAIT_FOR_CONDITION_INTERVAL(condition, timeout, description, actual_duration) \
sgns::test::assertWaitForCondition(condition, timeout, description, actual_duration, check_interval, __FILE__, __LINE__)


/**
 * Expect that a condition becomes true within the timeout (non-fatal assertion)
 * @param condition a callable that returns bool, true when condition is met
 * @param timeout maximum time to wait
 * @param description optional description of what we're waiting for
 * @param actualDuration optional pointer to store the actual wait duration
 * @param check_interval time to wait between condition checks
 */
    template <typename Condition>
    void expectWaitForCondition(Condition condition,
                               std::chrono::milliseconds timeout,
                               const std::string& description = "",
                               std::chrono::milliseconds* actualDuration = nullptr,
                               std::chrono::milliseconds check_interval = std::chrono::milliseconds(10),
                               const char* file_name = __FILE__,
                               int line_number = __LINE__)
    {
        bool result = waitForCondition(condition, timeout, actualDuration, check_interval);

        if (!result)
        {
            std::string message = "Timed out waiting for condition";
            if (!description.empty())
            {
                message += ": " + description;
            }
            message += " (timeout: " + std::to_string(timeout.count()) + "ms)";

            // Report the failure with the correct source location and terminate the test
            GTEST_MESSAGE_AT_(file_name, line_number, message.c_str(), ::testing::TestPartResult::kFatalFailure);
        }
    }

#define EXPECT_WAIT_FOR_CONDITION(condition, timeout, description, actual_duration) \
sgns::test::expectWaitForCondition(condition, timeout, description, actual_duration, std::chrono::milliseconds(10), __FILE__, __LINE__)

#define EXPECT_WAIT_FOR_CONDITION_INTERVAL(condition, timeout, description, actual_duration, check_interval) \
sgns::test::expectWaitForCondition(condition, timeout, description, actual_duration, check_interval, __FILE__, __LINE__)

} // namespace sgns::test

#endif // SUPERGENIUS_GTEST_WAIT_CONDITION_HPP
