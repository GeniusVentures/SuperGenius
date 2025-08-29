/**
 * @file       crdt_notifier.cpp
 * @brief      CRDT Notifier Class header
 * @date       2025-08-28
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "crdt/crdt_notifier.hpp"
#include <algorithm>

namespace sgns::crdt
{

    uint64_t CRDTNotifier::RegisterCallback( const std::vector<std::string> &topics, TopicCallback callback )
    {
        std::lock_guard<std::mutex> lock( mutex_ );

        uint64_t     id = nextCallbackId_++;
        CallbackInfo info;
        info.id       = id;
        info.callback = std::move( callback );

        // Empty topics vector means "all topics"
        if ( !topics.empty() )
        {
            info.topics.insert( topics.begin(), topics.end() );
        }

        callbacks_.push_back( std::move( info ) );
        return id;
    }

    void CRDTNotifier::UnregisterCallback( uint64_t callbackId )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        callbacks_.erase( std::remove_if( callbacks_.begin(),
                                          callbacks_.end(),
                                          [callbackId]( const CallbackInfo &info ) { return info.id == callbackId; } ),
                          callbacks_.end() );
    }

    void CRDTNotifier::NotifyTopics( const std::set<std::string>                                         &changedTopics,
                                     const std::pair<std::vector<std::string>, std::vector<std::string>> &changes )
    {
        std::lock_guard<std::mutex> lock( mutex_ );

        for ( const auto &callbackInfo : callbacks_ )
        {
            bool shouldExecute = false;

            if ( callbackInfo.topics.empty() )
            {
                // Registered for all topics
                shouldExecute = true;
            }
            else
            {
                // Check if any registered topic matches changed topics
                for ( const auto &topic : changedTopics )
                {
                    if ( callbackInfo.topics.count( topic ) > 0 )
                    {
                        shouldExecute = true;
                        break;
                    }
                }
            }

            if ( shouldExecute )
            {
                try
                {
                    callbackInfo.callback( changes );
                }
                catch ( const std::exception &e )
                {
                    // Log error but continue with other callbacks
                    // logger_->error("Topic callback failed: {}", e.what());
                }
            }
        }
    }

} // namespace sgns::crdt
