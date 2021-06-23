/**
* Header file for the distrubuted processing Room
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_ROOM
#define GRPC_FOR_SUPERGENIUS_PROCESSING_ROOM

#include <processing/proto/SGProcessing.pb.h>

#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

namespace sgns::processing
{
/**
* A room of distributed processing nodes
*/
class ProcessingRoom
{
public:
    /** Constructs a processing room
    * @param processingChannelCapacity - maximal number of nodes allowed to join a room
    */
    ProcessingRoom(
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> processingChannel,
        std::shared_ptr<boost::asio::io_context> context,
        const std::string& localNodeId,
        uint32_t processingRoomCapacity);

    void Create();
    void AttachLocalNodeToRemoteRoom();
    bool IsLocalNodeAttachingToRemoteRoom() const;
    bool AttachNode(const std::string& nodeId);
    /**
    * @return true is the room has been changed
    */
    bool UpdateRoom(SGProcessing::ProcessingRoom* room);

    /** Checks if a node belongs to the room
    * @param nodeId - id of a checked node
    * @return true if a node belongs to the room
    */
    bool IsRoommate(const std::string& nodeId) const;

    /** Checks if a node is the room host
    * @param nodeId - id of a checked node
    * @return true if a node is the room host
    */
    bool IsHost(const std::string& nodeId) const;

    size_t GetCapacity() const;

    size_t GetNodesCount() const;

    /**
    * @return set of room nodes
    */
    std::set<std::string> GetNodeIds() const;

private:
    bool IsRoommateUnlocked(const std::string& nodeId) const;
    bool IsHostUnlocked(const std::string& nodeId) const;

    void HandleAttachingTimeout();

    mutable std::mutex m_roomMutex;
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> m_processingChannel;
    std::shared_ptr<boost::asio::io_context> m_context;
    std::string m_localNodeId;
    uint32_t m_processingRoomCapacity;

    std::unique_ptr<SGProcessing::ProcessingRoom> m_room;
    std::set<std::string> m_nodeIds;

    bool m_isLocalNodeAttachingToRemoteRoom;
    boost::asio::deadline_timer m_timerAttachToRemoteRoomTimeout;
    boost::posix_time::time_duration m_attachToRemoteRoomTimeout;

};

}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_ROOM
