#ifndef SUPERGENIUS_TESTUTIL_REMOVE_ALL_HPP
#define SUPERGENIUS_TESTUTIL_REMOVE_ALL_HPP

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace sgns::test
{
    namespace detail
    {
        // Names the files still undeletable after retries so the error names
        // the leaking holder (sgnslog.log -> soralog sink, LOCK/.sst ->
        // RocksDB, key files -> key storage) instead of a bare system error.
        inline std::string describeHeldFiles( const std::filesystem::path &path )
        {
            std::vector<std::string> held;
            std::error_code          walk_ec;
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( path, walk_ec ) )
            {
                std::error_code remove_ec;
                std::filesystem::remove( entry.path(), remove_ec );
                if ( remove_ec )
                {
                    held.push_back( entry.path().filename().string() );
                    if ( held.size() >= 5 )
                    {
                        break;
                    }
                }
            }
            if ( held.empty() )
            {
                return {};
            }
            std::string listed;
            for ( size_t i = 0; i < held.size(); ++i )
            {
                if ( i != 0 )
                {
                    listed += ", ";
                }
                listed += held[i];
            }
            return " (still held: " + listed + ")";
        }
    } // namespace detail

    inline void removeAllWithRetry( const std::filesystem::path &path, std::error_code &ec )
    {
#ifdef _WIN32
        // RocksDB LOCK files and soralog sinks can be released a beat after the
        // owning objects are destroyed (deferred handle close on Windows), and
        // CI runners add filter-driver latency on top. Exponential backoff over
        // ~3s absorbs transient holds; anything longer is a genuine leak and
        // should fail fast so it is investigated, not waited out.
        constexpr size_t MAX_ATTEMPTS = 5;
        auto             delay        = std::chrono::milliseconds( 200 );
        for ( size_t attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt )
        {
            std::filesystem::remove_all( path, ec );
            if ( !ec )
            {
                return;
            }
            if ( attempt != MAX_ATTEMPTS )
            {
                std::this_thread::sleep_for( delay );
                delay *= 2;
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
#ifdef _WIN32
            const auto held = detail::describeHeldFiles( path );
            throw std::filesystem::filesystem_error( "remove_all retries exhausted" + held, path, ec );
#else
            throw std::filesystem::filesystem_error( "remove_all retries exhausted", path, ec );
#endif
        }
    }
} // namespace sgns::test

#endif // SUPERGENIUS_TESTUTIL_REMOVE_ALL_HPP
