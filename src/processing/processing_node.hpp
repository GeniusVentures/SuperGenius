/**
* Header file for the distrubuted processing node
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_NODE
#define GRPC_FOR_SUPERGENIUS_PROCESSING_NODE

#include "processing_engine.hpp"
#include "processing_room.hpp"

#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

namespace sgns::processing
{
/**
* A node for distributed computation.
* Allows to conduct a computation processing by multiple workers 
*/
class ProcessingNode
{
public:
    /** Constructs a processing node
    * @param gossipPubSub - pubsub service
    */
    ProcessingNode(
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
        size_t processingChannelCapacity,
        std::shared_ptr<ProcessingCore> processingCore);

    ~ProcessingNode();
    /** Attaches the node to the processing channel
    * @param processingChannelId - identifier of a processing room channel
    * @return flag indicating if the room is joined for block data processing
    */
    void AttachTo(const std::string& processingChannelId, size_t msSubscriptionWaitingDuration = 0);
    void CreateProcessingHost(const SGProcessing::Task& task, size_t msSubscriptionWaitingDuration = 0);

    /** Returns true if a peer is joined a room
    * @return true if if the current peer in a room
    */
    bool IsRoommate() const;
    bool IsRoomHost() const;

    bool IsAttachingToProcessingRoom() const;

    const ProcessingRoom* GetRoom() const;

private:
    void Initialize(const std::string& processingChannelId, size_t msSubscriptionWaitingDuration);    
    void OnProcessingChannelMessage(boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message);

    void HandleProcessingRoomRequest(SGProcessing::ProcessingChannelMessage& channelMesssage);
    void HandleProcessingRoom(SGProcessing::ProcessingChannelMessage& channelMesssage);
    void HandleSubTaskQueueRequest(SGProcessing::ProcessingChannelMessage& channelMesssage);
    void HandleSubTaskQueue(SGProcessing::ProcessingChannelMessage& channelMesssage);

    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> m_gossipPubSub;
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> m_processingChannel;

    std::string m_nodeId;
    size_t m_processingChannelCapacity;
    std::shared_ptr<ProcessingCore> m_processingCore;

    std::unique_ptr<ProcessingRoom> m_room;
    std::unique_ptr<ProcessingEngine> m_processingEngine;
    std::shared_ptr<ProcessingSubTaskQueue> m_subtaskQueue;
};
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_NODE
