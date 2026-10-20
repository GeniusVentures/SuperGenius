/**
 * @file       bounded_defer.cpp
 * @brief      Implementation of the generic bounded defer-and-hold tracker (D-03).
 */

#include "base/bounded_defer.hpp"

namespace sgns::base
{
    BoundedDeferTracker::BoundedDeferTracker( std::chrono::seconds window,
                                              size_t               max_entries,
                                              NowProvider          now_provider ) :
        window_( window ), max_entries_( max_entries ), now_provider_( std::move( now_provider ) )
    {
    }

    void BoundedDeferTracker::SetWindow( std::chrono::seconds window )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        window_ = window;
    }

    // RED scaffolding: API shape only, no tracking behavior yet.
    bool BoundedDeferTracker::Defer( const std::string &key )
    {
        (void) key;
        std::lock_guard<std::mutex> lock( mutex_ );
        return false;
    }

    bool BoundedDeferTracker::Expired( const std::string &key ) const
    {
        (void) key;
        std::lock_guard<std::mutex> lock( mutex_ );
        return false;
    }

    void BoundedDeferTracker::Clear( const std::string &key )
    {
        (void) key;
        std::lock_guard<std::mutex> lock( mutex_ );
    }

    size_t BoundedDeferTracker::Size() const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        return 0;
    }

    void BoundedDeferTracker::EvictOverCapLocked()
    {
    }
} // namespace sgns::base
