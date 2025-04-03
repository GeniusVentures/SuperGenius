#ifndef SUPERGENIUS_COLOR_SUPPORT_HPP
#define SUPERGENIUS_COLOR_SUPPORT_HPP

#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sgns::test {

    inline bool IsTerminalColorSupported() {
#ifdef _WIN32
        // Windows color support detection
        // Check if we're running in a console and color is supported
        HANDLE hConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsoleHandle == INVALID_HANDLE_VALUE) {
            return false;
        }

        // Check if it's an actual console
        DWORD consoleMode;
        if (!GetConsoleMode(hConsoleHandle, &consoleMode)) {
            return false;
        }

        // Additional check for Windows 10+ which supports ANSI colors
        DWORD outMode = consoleMode;
        if (GetConsoleMode(hConsoleHandle, &outMode)) {
            outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (SetConsoleMode(hConsoleHandle, outMode)) {
                return true;
            }
        }

        return false;

#else
        // UNIX-like systems color support detection
        // Check if stdout is connected to a terminal
        if (!isatty(STDOUT_FILENO)) {
            return false;
        }

        // Check TERM environment variable
        const char* term = std::getenv("TERM");
        return term &&
               std::string(term) != "dumb" &&
               std::string(term) != "";
#endif
    }

} // namespace sgns::test

#endif // SUPERGENIUS_COLOR_SUPPORT_HPP
