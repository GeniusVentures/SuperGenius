#include "processing_room.hpp"

#include <libp2p/protocol/gossip/impl/peer_set.hpp>

namespace sgns::processing
{
////////////////////////////////////////////////////////////////////////////////
ProcessingRoom::ProcessingRoom(
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> processingChannel,
    std::shared_ptr<boost::asio::io_context> context,
    const std::string& localNodeId,
    uint32_t processingRoomCapacity)
    : m_processingChannel(processingChannel)
    , m_context(context)
    , m_localNodeId(localNodeId)
    , m_processingRoomCapacity(processingRoomCapacity)
    , m_isLocalNodeAttachingToRemoteRoom(false)
    , m_timerAttachToRemoteRoomTimeout(*m_context.get())
    , m_attachToRemoteRoomTimeout(boost::posix_time::seconds(3))
{
}

void ProcessingRoom::Create()
{
    std::lock_guard<std::mutex> guard(m_roomMutex);
    auto timestamp = std::chrono::system_clock::now();

    m_room = std::make_unique<SGProcessing::ProcessingRoom>();
    m_room->set_create_timestamp(timestamp.time_since_epoch().count());

    auto node = m_room->add_nodes();
    node->set_timestamp(timestamp.time_since_epoch().count());
    node->set_node_id(m_localNodeId);

    m_room->set_room_host_node_id(m_localNodeId);
    m_room->set_last_update_timestamp(timestamp.time_since_epoch().count());

    m_room->set_room_capacity(m_processingRoomCapacity);

    m_nodeIds.insert(m_localNodeId);
}

void ProcessingRoom::AttachLocalNodeToRemoteRoom()
{
    std::lock_guard<std::mutex> guard(m_roomMutex);
    if (!IsRoommateUnlocked(m_localNodeId))
    {
        SGProcessing::ProcessingChannelMessage channelMesssage;
        auto request = channelMesssage.mutable_processing_room_request();

        request->set_node_id(m_localNodeId);
        request->set_request_type(SGProcessing::ProcessingRoomRequest_Type_JOIN);

        m_processingChannel->Publish(channelMesssage.SerializeAsString());
        m_isLocalNodeAttachingToRemoteRoom = true;
        m_timerAttachToRemoteRoomTimeout.expires_from_now(m_attachToRemoteRoomTimeout);
        m_timerAttachToRemoteRoomTimeout.async_wait(std::bind(&ProcessingRoom::HandleAttachingTimeout, this));
    }
}

bool ProcessingRoom::IsLocalNodeAttachingToRemoteRoom() const
{
    return m_isLocalNodeAttachingToRemoteRoom;
}

bool ProcessingRoom::AttachNode(const std::string& nodeId)
{
    std::lock_guard<std::mutex> guard(m_roomMutex);
    if (IsHostUnlocked(m_localNodeId))
    {
        if (!IsRoommateUnlocked(nodeId))
        {
            if (static_cast<uint32_t>(m_room->nodes_size()) < m_room->room_capacity())
            {
                auto timestamp = std::chrono::system_clock::now();
                auto node = m_room->add_nodes();
                node->set_timestamp(timestamp.time_since_epoch().count());
                node->set_node_id(nodeId);
                m_nodeIds.insert(nodeId);

                m_room->set_last_update_timestamp(timestamp.time_since_epoch().count());
            }

            SGProcessing::ProcessingChannelMessage channelMesssage;
            auto room = channelMesssage.mutable_processing_room();
            room->CopyFrom(*m_room);

            m_processingChannel->Publish(channelMesssage.SerializeAsString());

            return true;
        }
    }

    return false;
}

bool ProcessingRoom::UpdateRoom(SGProcessing::ProcessingRoom* room)
{
    std::lock_guard<std::mutex> guard(m_roomMutex);
    if (room != nullptr)
    {
        std::unique_ptr<SGProcessing::ProcessingRoom> pRoom(room);
        if (!m_room 
            || (m_room->create_timestamp() > room->create_timestamp())
            || ((m_room->create_timestamp() == room->create_timestamp())
                && (m_room->last_update_timestamp() < room->last_update_timestamp())))
        {
            m_room.reset(pRoom.release());
            std::set<std::string> nodeIds;
            for (int nodeIdx = 0; nodeIdx < m_room->nodes_size(); ++nodeIdx)
            {
                nodeIds.insert(m_room->nodes(nodeIdx).node_id());
            }
            m_nodeIds.swap(nodeIds);

            if (m_isLocalNodeAttachingToRemoteRoom && IsRoommateUnlocked(m_localNodeId))
            {
                // The local node attached to the room
                m_timerAttachToRemoteRoomTimeout.expires_at(boost::posix_time::pos_infin);
                m_isLocalNodeAttachingToRemoteRoom = false;
            }

            return true;
        }
    }
    return false;
}

bool ProcessingRoom::IsRoommate(const std::string& nodeId) const
{
    std::lock_guard<std::mutex> guard(m_roomMutex);
    return IsRoommateUnlocked(nodeId);
}

bool ProcessingRoom::IsHost(const std::string& nodeId) const
{
    std::lock_guard<std::mutex> guard(m_roomMutex);
    return IsHostUnlocked(nodeId);
}

bool ProcessingRoom::IsRoommateUnlocked(const std::string& nodeId) const
{
    return (m_nodeIds.find(nodeId) != m_nodeIds.end());
}

bool ProcessingRoom::IsHostUnlocked(const std::string& nodeId) const
{
    return (m_room && (m_room->room_host_node_id() == m_localNodeId));
}

std::set<std::string> ProcessingRoom::GetNodeIds() const
{
    std::lock_guard<std::mutex> guard(m_roomMutex);    
    return m_nodeIds;
}

size_t ProcessingRoom::GetCapacity() const
{
    std::lock_guard<std::mutex> guard(m_roomMutex);
    return m_room ? m_room->room_capacity() : m_processingRoomCapacity;
}

size_t ProcessingRoom::GetNodesCount() const
{
    std::lock_guard<std::mutex> guard(m_roomMutex);
    return m_room ? m_room->nodes_size() : 0;
}

void ProcessingRoom::HandleAttachingTimeout()
{
    m_timerAttachToRemoteRoomTimeout.expires_at(boost::posix_time::pos_infin);
    m_isLocalNodeAttachingToRemoteRoom = false;
}

////////////////////////////////////////////////////////////////////////////////
}

