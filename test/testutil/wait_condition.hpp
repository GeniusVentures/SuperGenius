#ifndef SUPERGENIUS_GTEST_WAIT_CONDITION_HPP
#define SUPERGENIUS_GTEST_WAIT_CONDITION_HPP

#include <functional>
#include <thread>
#include <string>
#include <gtest/gtest.h>
#include "testutil/color_support.hpp"

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
 * Wait for a condition to become true with timeout
 * @param condition a callable that returns bool, true when condition is met
 * @param timeout maximum time to wait
 * @param actualDuration optional pointer to store the actual wait duration
 * @param check_interval time to wait between condition checks
 * @return true if condition became true before timeout, false if timeout occurred
 */
template <typename Condition>
bool waitForCondition(Condition condition,
                     std::chrono::milliseconds timeout,
                     std::chrono::milliseconds* actualDuration = nullptr,
                     std::chrono::milliseconds check_interval = std::chrono::milliseconds(10)) {
    auto startTime = std::chrono::steady_clock::now();
    while (!condition()) {
        if (std::chrono::steady_clock::now() - startTime > timeout) {
            if (actualDuration) {
                *actualDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime);
            }
            return false; // Timeout occurred
        }
        std::this_thread::sleep_for(check_interval);
    }

    if (actualDuration) {
        *actualDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime);
    }
    return true; // Condition met within timeout
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
                           std::chrono::milliseconds check_interval = std::chrono::milliseconds(10)) {
    bool result = waitForCondition(condition, timeout, actualDuration, check_interval);

    if (!result) {
        std::string message = "Timed out waiting for condition";
        if (!description.empty()) {
            message += ": " + description;
        }
        message += " (timeout: " + std::to_string(timeout.count()) + "ms)";

        GTEST_FATAL_FAILURE_(message.c_str());
    }
}

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
                           std::chrono::milliseconds check_interval = std::chrono::milliseconds(10)) {
    bool result = waitForCondition(condition, timeout, actualDuration, check_interval);

    if (!result) {
        std::string message = "Timed out waiting for condition";
        if (!description.empty()) {
            message += ": " + description;
        }
        message += " (timeout: " + std::to_string(timeout.count()) + "ms)";

        GTEST_NONFATAL_FAILURE_(message.c_str());
    }
}

} // namespace sgns::test

#endif // SUPERGENIUS_GTEST_WAIT_CONDITION_HPP
