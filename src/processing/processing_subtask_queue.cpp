#include "processing_subtask_queue.hpp"

#include <sstream>
#include <utility>

namespace sgns::processing
{
////////////////////////////////////////////////////////////////////////////////
ProcessingSubTaskQueue::ProcessingSubTaskQueue( std::string localNodeId, TimestampProvider timestampProvider ) :
    m_localNodeId( std::move( localNodeId ) ),
    m_queue( nullptr ),
    m_timestampProvider(timestampProvider)
{
}

void ProcessingSubTaskQueue::CreateQueue(
    SGProcessing::ProcessingQueue* queue, const std::vector<int>& enabledItemIndices)
{
    m_queue = queue;
    m_enabledItemIndices = enabledItemIndices;
    ChangeOwnershipTo(m_localNodeId);
}

bool ProcessingSubTaskQueue::UpdateQueue(
    SGProcessing::ProcessingQueue* queue, const std::vector<int>& enabledItemIndices)
{
    if ( ( m_queue == nullptr ) || ( m_queue->last_update_timestamp() <= queue->last_update_timestamp() ) )
    {
        m_queue = queue;
        m_enabledItemIndices = enabledItemIndices;
        LogQueue();
        return true;
    }
    return false;
}

bool ProcessingSubTaskQueue::LockItem(size_t& lockedItemIndex)
{
    // The method has to be called in scoped lock of queue mutex
    for (auto itemIdx : m_enabledItemIndices)
    {
        if (m_queue->items(itemIdx).lock_node_id().empty())
        {
            auto timestamp = std::chrono::system_clock::now();

            m_queue->mutable_items(itemIdx)->set_lock_node_id(m_localNodeId);
            m_queue->mutable_items(itemIdx)->set_lock_timestamp( m_queue->last_update_timestamp() );

            LogQueue();
            lockedItemIndex = itemIdx;
            return true;
        }
    }

    return false;
}

bool ProcessingSubTaskQueue::GrabItem(size_t& grabbedItemIndex)
{
    if (HasOwnership())
    {
        return LockItem(grabbedItemIndex);
    }

    return false;
}

bool ProcessingSubTaskQueue::MoveOwnershipTo(const std::string& nodeId)
{
    if (HasOwnership())
    {
        ChangeOwnershipTo(nodeId);
        return true;
    }
    return false;
}

void ProcessingSubTaskQueue::ChangeOwnershipTo(const std::string& nodeId)
{
    m_queue->set_owner_node_id(nodeId);

    if (m_timestampProvider) {
        // Use the manager's timestamp if provider is available
        m_queue->set_last_update_timestamp(m_timestampProvider());
    }
    LogQueue();
}

bool ProcessingSubTaskQueue::RollbackOwnership()
{
    if(m_queue == nullptr)
    {
        //TODO: Why can this be nullptr?
        return false;
    }
    if (HasOwnership())
    {
        // Do nothing. The node is already the queue owner
        return true;
    }

    // Find the current queue owner
    auto ownerNodeId = m_queue->owner_node_id();
    int ownerNodeIdx = -1;
    for (int itemIdx = 0; itemIdx < (int)m_queue->items_size(); ++itemIdx)
    {
        if (m_queue->items(itemIdx).lock_node_id() == ownerNodeId)
        {
            ownerNodeIdx = itemIdx;
            break;
        }
    }

    if (ownerNodeIdx >= 0)
    {
        // Check if the local node is the previous queue owner
        for (int idx = 1; idx < m_queue->items_size(); ++idx)
        {
            // Loop cyclically over queue items in backward direction starting from item[ownerNodeIdx - 1]
            // and excluding the item[ownerNodeIdx]
            int itemIdx = (m_queue->items_size() + ownerNodeIdx - idx) % m_queue->items_size();
            if (!m_queue->items(itemIdx).lock_node_id().empty())
            {
                if (m_queue->items(itemIdx).lock_node_id() == m_localNodeId)
                {
                    // The local node is the previous queue owner
                    ChangeOwnershipTo(m_localNodeId);
                    return true;
                }

                // Another node should take the ownership
                return false;
            }
        }
    }
    else
    {
        // The queue owner didn't lock any subtask
        // Find the last locked item
        for (int idx = 1; idx <= m_queue->items_size(); ++idx)
        {
            int itemIdx = (m_queue->items_size() - idx);
            if (!m_queue->items(itemIdx).lock_node_id().empty())
            {
                if (m_queue->items(itemIdx).lock_node_id() == m_localNodeId)
                {
                    // The local node is the last locked item
                    ChangeOwnershipTo(m_localNodeId);
                    return true;
                }

                // Another node should take the ownership
                return false;
            }
        }
    }

    // No locked items found
    // Allow the local node to take the ownership
    ChangeOwnershipTo(m_localNodeId);
    return true;
}

bool ProcessingSubTaskQueue::HasOwnership() const
{
    return ( m_queue != nullptr ) && m_queue->owner_node_id() == m_localNodeId;
}

bool ProcessingSubTaskQueue::UnlockExpiredItems(std::chrono::system_clock::duration expirationTimeout)
{
    bool unlocked = false;

    if (HasOwnership())
    {
        auto timestamp = std::chrono::system_clock::now();
        for (auto itemIdx : m_enabledItemIndices)
        {
            // Check if a subtask is locked, expired and no result was obtained for it
            // @todo replace the result channel with subtask id to identify a subtask that should be unlocked
            if (!m_queue->items(itemIdx).lock_node_id().empty())
            {
                auto expirationTime =
                    std::chrono::system_clock::time_point(
                        std::chrono::system_clock::duration(m_queue->items(itemIdx).lock_timestamp())) + expirationTimeout;
                if (timestamp > expirationTime)
                {
                    // Unlock the item
                    m_queue->mutable_items(itemIdx)->set_lock_node_id("");
                    m_queue->mutable_items(itemIdx)->set_lock_timestamp(0);
                    unlocked = true;
                    m_logger->debug("EXPIRED_SUBTASK_UNLOCKED {}", itemIdx);
                }
            }
        }

        if (unlocked)
        {
            m_queue->set_last_update_timestamp(timestamp.time_since_epoch().count());
        }
    }

    return unlocked;
}

    uint64_t ProcessingSubTaskQueue::GetLastLockTimestamp() const
    {
        uint64_t lastLockTimestamp = 0;
        for (auto itemIdx : m_enabledItemIndices) {
            if (!m_queue->items(itemIdx).lock_node_id().empty()) {
                lastLockTimestamp = std::max(lastLockTimestamp,
                                             static_cast<uint64_t>(m_queue->items(itemIdx).lock_timestamp()));
            }
        }
        return lastLockTimestamp;
    }

void ProcessingSubTaskQueue::LogQueue() const
{
    if (m_logger->level() <= spdlog::level::trace)
    {
        std::stringstream ss;
        ss << "{";
        ss << "\"owner_node_id\":\"" << m_queue->owner_node_id() << "\"";
        ss << "," << "\"last_update_timestamp\":" << m_queue->last_update_timestamp();
        ss << "," << "\"items\":[";
        for (int itemIdx = 0; itemIdx < m_queue->items_size(); ++itemIdx)
        {
            ss << "{\"lock_node_id\":\"" << m_queue->items(itemIdx).lock_node_id() << "\"";
            ss << ",\"lock_timestamp\":" << m_queue->items(itemIdx).lock_timestamp();
            ss << "},";
        }
        ss << "]}";

        m_logger->trace(ss.str());
    }

}
    bool ProcessingSubTaskQueue::AddOwnershipRequest(const std::string& nodeId, int64_t timestamp)
    {
        if (m_queue != nullptr)
        {
            // Check if the request already exists
            for (int i = 0; i < m_queue->ownership_requests_size(); i++)
            {
                if (m_queue->ownership_requests(i).node_id() == nodeId)
                {
                    return false;  // Request already exists
                }
            }

            auto request = m_queue->add_ownership_requests();
            request->set_node_id(nodeId);
            request->set_request_timestamp(timestamp);

            // Update the queue timestamp
            auto current_time = std::chrono::system_clock::now();
            m_queue->set_last_update_timestamp(current_time.time_since_epoch().count());

            LogQueue();
            return true;
        }
        return false;
    }

    bool ProcessingSubTaskQueue::ProcessNextOwnershipRequest()
    {
        if (HasOwnership() && m_queue != nullptr && m_queue->ownership_requests_size() > 0)
        {
            // Get the first request
            auto request = m_queue->ownership_requests(0);

            // Remove it from the queue (by moving all others up)
            for (int i = 0; i < m_queue->ownership_requests_size() - 1; i++)
            {
                m_queue->mutable_ownership_requests(i)->CopyFrom(m_queue->ownership_requests(i + 1));
            }
            m_queue->mutable_ownership_requests()->RemoveLast();

            // Transfer ownership
            ChangeOwnershipTo(request.node_id());
            LogQueue();
            return true;
        }
        return false;
    }

}

////////////////////////////////////////////////////////////////////////////////
