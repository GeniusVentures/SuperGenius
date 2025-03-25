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

bool ProcessingSubTaskQueue::LockItem(size_t& lockedItemIndex, uint64_t now)
{
    // The method has to be called in scoped lock of queue mutex
    m_queue->set_last_update_timestamp(now);
    for (auto itemIdx : m_enabledItemIndices)
    {
        auto item = m_queue->items(itemIdx);
        if (item.lock_node_id().empty())
        {
            auto mItem = m_queue->mutable_items(itemIdx);

            mItem->set_lock_node_id(m_localNodeId);
            mItem->set_lock_timestamp( now );
            mItem->set_lock_expiration_timestamp( now + m_queue->processing_timeout_length());

            LogQueue();
            lockedItemIndex = itemIdx;
            return true;
        }
    }

    return false;
}

bool ProcessingSubTaskQueue::GrabItem(size_t& grabbedItemIndex, uint64_t now)
{
    if (HasOwnership())
    {
        return LockItem(grabbedItemIndex, now);
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

bool ProcessingSubTaskQueue::UnlockExpiredItems(uint64_t now)
{
    bool unlocked = false;

    m_queue->set_last_update_timestamp(now);
    if (HasOwnership())
    {
        for (auto itemIdx : m_enabledItemIndices)
        {
            auto item = m_queue->items(itemIdx);
            // Check if a subtask is locked, expired and no result was obtained for it
            if (!item.lock_node_id().empty())
            {
                if (now > item.lock_expiration_timestamp())
                {
                    auto mItem = m_queue->mutable_items(itemIdx);
                    m_logger->debug("EXPIRED_SUBTASK_UNLOCKED: index {} for {} expired at {}ms", itemIdx, item.lock_node_id(), item.lock_expiration_timestamp());
                    // Unlock the item
                    mItem->set_lock_node_id("");
                    mItem->set_lock_timestamp(0);
                    mItem->set_lock_expiration_timestamp(0);
                    unlocked = true;
                }
            }
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
            ss << ",\"lock_expiration_timestamp\":" << m_queue->items(itemIdx).lock_expiration_timestamp();
            ss << "},";
        }
        ss << "]}";

        m_logger->trace(ss.str());
    }

}
    bool ProcessingSubTaskQueue::AddOwnershipRequest(const std::string& nodeId, uint64_t timestamp)
    {
        if (m_queue != nullptr)
        {
            auto request = m_queue->add_ownership_requests();
            request->set_node_id(nodeId);
            request->set_request_timestamp(timestamp);

            // Update the queue timestamp
            m_queue->set_last_update_timestamp(timestamp);

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
