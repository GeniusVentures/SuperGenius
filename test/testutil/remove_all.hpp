#ifndef SUPERGENIUS_TESTUTIL_REMOVE_ALL_HPP
#define SUPERGENIUS_TESTUTIL_REMOVE_ALL_HPP

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <system_error>
#include <thread>

namespace sgns::test
{
    inline void removeAllWithRetry( const std::filesystem::path &path, std::error_code &ec )
    {
#ifdef _WIN32
        constexpr size_t MAX_ATTEMPTS = 3;
        for ( size_t attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt )
        {
            std::filesystem::remove_all( path, ec );
            if ( !ec )
            {
                return;
            }
            if ( attempt != MAX_ATTEMPTS )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
            }
        }
#else
        std::filesystem::remove_all( path, ec );
#endif
    }

    inline void removeAllWithRetry( const std::filesystem::path &path )
    {
        std::error_code ec;
        removeAllWithRetry( path, ec );
        if ( ec )
        {
            throw std::filesystem::filesystem_error( "remove_all retries exhausted", path, ec );
        }
    }
} // namespace sgns::test

#endif // SUPERGENIUS_TESTUTIL_REMOVE_ALL_HPP
