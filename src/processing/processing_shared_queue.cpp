#include "processing_shared_queue.hpp"

#include <numeric>
#include <sstream>

namespace sgns::processing
{
////////////////////////////////////////////////////////////////////////////////
SharedQueue::SharedQueue(
    const std::string& localNodeId)
    : m_localNodeId(localNodeId)
    , m_queue(nullptr)
{
}

void SharedQueue::CreateQueue(SGProcessing::ProcessingQueue* queue)
{
    m_queue = queue;
    m_validItemIndices = std::vector<int>(m_queue->items_size());
    std::iota(m_validItemIndices.begin(), m_validItemIndices.end(), 0);
    ChangeOwnershipTo(m_localNodeId);
}

bool SharedQueue::UpdateQueue(SGProcessing::ProcessingQueue* queue)
{
    if (!m_queue
        || (m_queue->last_update_timestamp() < queue->last_update_timestamp()))
    {
        m_queue = queue;
        LogQueue();
        return true;
    }
    return false;
}

bool SharedQueue::LockItem(size_t& lockedItemIndex)
{
    // The method has to be called in scoped lock of queue mutex
    for (auto itemIdx : m_validItemIndices)
    {
        if (m_queue->items(itemIdx).lock_node_id().empty())
        {
            auto timestamp = std::chrono::system_clock::now();

            m_queue->mutable_items(itemIdx)->set_lock_node_id(m_localNodeId);
            m_queue->mutable_items(itemIdx)->set_lock_timestamp(timestamp.time_since_epoch().count());

            m_queue->set_last_update_timestamp(timestamp.time_since_epoch().count());

            LogQueue();

            lockedItemIndex = itemIdx;
            return true;
        }
    }

    return false;
}

bool SharedQueue::GrabItem(size_t& grabbedItemIndex)
{
    if (HasOwnership())
    {
        return LockItem(grabbedItemIndex);
    }

    return false;
}

bool SharedQueue::MoveOwnershipTo(const std::string& nodeId)
{
    if (HasOwnership())
    {
        ChangeOwnershipTo(nodeId);
        return true;
    }
    return false;
}

void SharedQueue::ChangeOwnershipTo(const std::string& nodeId)
{
    auto timestamp = std::chrono::system_clock::now();
    m_queue->set_owner_node_id(nodeId);
    m_queue->set_last_update_timestamp(timestamp.time_since_epoch().count());
    LogQueue();
}

bool SharedQueue::RollbackOwnership()
{
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
                else
                {
                    // Another node should take the ownership
                    return false;
                }
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
                else
                {
                    // Another node should take the ownership
                    return false;
                }
            }
        }
    }

    // No locked items found
    // Allow the local node to take the ownership
    ChangeOwnershipTo(m_localNodeId);
    return true;
}

bool SharedQueue::HasOwnership() const
{
    return (m_queue && m_queue->owner_node_id() == m_localNodeId);
}

bool SharedQueue::UnlockExpiredItems(std::chrono::system_clock::duration expirationTimeout)
{

    bool unlocked = false;

    if (HasOwnership())
    {
        auto timestamp = std::chrono::system_clock::now();
        for (auto itemIdx : m_validItemIndices)
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

std::chrono::system_clock::time_point SharedQueue::GetLastLockTimestamp() const
{
    std::chrono::system_clock::time_point lastLockTimestamp;

    for (auto itemIdx : m_validItemIndices)
    {
        // Check if an item is locked and no result was obtained for it
        if (!m_queue->items(itemIdx).lock_node_id().empty())
        {
            lastLockTimestamp = std::max(lastLockTimestamp,
                std::chrono::system_clock::time_point(
                    std::chrono::system_clock::duration(m_queue->items(itemIdx).lock_timestamp())));
        }
    }

    return lastLockTimestamp;
}

void SharedQueue::SetValidItemIndices(std::vector<int>&& indices)
{
    m_validItemIndices = std::move(indices);
}

void SharedQueue::LogQueue() const
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
}

////////////////////////////////////////////////////////////////////////////////
