/**
 * Header file for ScopedEnvVar — CRT-family RAII environment-variable
 * guard for hermetic test suites (Phase 4, TEST-04/TEST-05 support).
 * Header-only: a tiny ctor/dtor pair over the CRT env family.
 */
#pragma once

#include <cstdlib>
#include <string>

namespace sgns::testutil
{
    /**
     * @brief RAII guard that sets an environment variable on construction
     * and restores its exact prior state (set-to-old-value or unset) on
     * destruction.
     *
     * Uses the CRT family (_putenv_s/_putenv on Windows, setenv/unsetenv
     * elsewhere) deliberately: production code reads the environment via
     * std::getenv, which reads the CRT _environ copy on MSVC — Win32
     * SetEnvironmentVariable writes are NOT guaranteed visible to it.
     * Never call SetEnvironmentVariable from test code that production
     * std::getenv reads must observe (Phase 4 RESEARCH Pitfall 1, which
     * corrects D-03's original SetEnvironmentVariable wording).
     */
    class ScopedEnvVar
    {
    public:
        /// @brief Capture prior state, then set name=value via the CRT family.
        /// @param name Environment variable name
        /// @param value Value to set for the guarded scope
        ScopedEnvVar( std::string name, std::string value )
            : name_( std::move( name ) )
        {
            const char *prior = std::getenv( name_.c_str() );
            hadValue_ = prior != nullptr;
            if ( hadValue_ )
            {
                oldValue_ = prior;
            }
#ifdef _WIN32
            _putenv_s( name_.c_str(), value.c_str() );
#else
            setenv( name_.c_str(), value.c_str(), /*overwrite=*/1 );
#endif
        }

        /// @brief Restore the exact prior state: old value when it had one,
        /// unset otherwise (an empty old value removes the variable on the
        /// Windows CRT).
        ~ScopedEnvVar()
        {
#ifdef _WIN32
            _putenv( ( name_ + "=" + oldValue_ ).c_str() );
#else
            if ( hadValue_ )
            {
                setenv( name_.c_str(), oldValue_.c_str(), /*overwrite=*/1 );
            }
            else
            {
                unsetenv( name_.c_str() );
            }
#endif
        }

        ScopedEnvVar( const ScopedEnvVar & )            = delete;
        ScopedEnvVar &operator=( const ScopedEnvVar & ) = delete;

    private:
        std::string name_;
        std::string oldValue_;
        bool        hadValue_ = false;
    };
} // namespace sgns::testutil
