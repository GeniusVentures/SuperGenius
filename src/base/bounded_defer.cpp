/**
 * @file       bounded_defer.cpp
 * @brief      Implementation of the generic bounded defer-and-hold tracker (D-03).
 */

#include "base/bounded_defer.hpp"

#include <algorithm>

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

    bool BoundedDeferTracker::Defer( const std::string &key )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        const auto                  now = now_provider_();
        const auto                  it  = deadline_by_key_.find( key );
        if ( it != deadline_by_key_.end() )
        {
            // First-seen anchor: the ORIGINAL deadline stands, never refreshed.
            return now >= it->second;
        }
        deadline_by_key_.emplace( key, now + window_ );
        insertion_order_.push_back( key );
        EvictOverCapLocked();
        return false;
    }

    bool BoundedDeferTracker::Expired( const std::string &key ) const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        const auto                  it = deadline_by_key_.find( key );
        return it != deadline_by_key_.end() && now_provider_() >= it->second;
    }

    void BoundedDeferTracker::Clear( const std::string &key )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        if ( deadline_by_key_.erase( key ) == 0 )
        {
            return;
        }
        insertion_order_.erase( std::remove( insertion_order_.begin(), insertion_order_.end(), key ),
                                insertion_order_.end() );
    }

    size_t BoundedDeferTracker::Size() const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        return insertion_order_.size();
    }

    void BoundedDeferTracker::EvictOverCapLocked()
    {
        // Single-oldest eviction per insertion (same shape as the evidence cache:
        // a while-loop, but one insert at cap evicts exactly the front entry —
        // never a wholesale clear).
        while ( insertion_order_.size() > max_entries_ )
        {
            deadline_by_key_.erase( insertion_order_.front() );
            insertion_order_.pop_front();
        }
    }
} // namespace sgns::base
