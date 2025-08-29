/**
 * @file       crdt_notifier.hpp
 * @brief      CRDT Notifier Class header
 * @date       2025-08-28
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <mutex>
#include <cstdint>
#include <set>

namespace sgns::crdt
{

    // Callback receives pair of <element_keys, tombstone_keys>
    using TopicCallback = std::function<void( const std::pair<std::vector<std::string>, std::vector<std::string>> & )>;

    class CRDTNotifier
    {
    public:
        // Register callback for specific topics (empty vector = all topics)
        uint64_t RegisterCallback( const std::vector<std::string> &topics, TopicCallback callback );

        // Unregister callback
        void UnregisterCallback( uint64_t callbackId );

        // Notify for topics that had changes with the actual changed elements/tombstones
        void NotifyTopics( const std::set<std::string>                                         &changedTopics,
                           const std::pair<std::vector<std::string>, std::vector<std::string>> &changes );

    private:
        struct CallbackInfo
        {
            uint64_t                        id;
            std::unordered_set<std::string> topics; // empty set means "all topics"
            TopicCallback                   callback;
        };

        std::mutex                mutex_;
        std::vector<CallbackInfo> callbacks_;
        uint64_t                  nextCallbackId_ = 1;
    };

} // namespace sgns::crdt
