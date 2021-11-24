#include "processing_node.hpp"

namespace sgns::processing
{
////////////////////////////////////////////////////////////////////////////////
ProcessingNode::ProcessingNode(
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
    size_t processingChannelCapacity,
    std::shared_ptr<ProcessingCore> processingCore,
    std::function<void(const SGProcessing::TaskResult&)> taskResultProcessingSink)
    : m_gossipPubSub(std::move(gossipPubSub))
    , m_nodeId(m_gossipPubSub->GetLocalAddress())
    , m_processingChannelCapacity(processingChannelCapacity)
    , m_processingCore(processingCore)
    , m_taskResultProcessingSink(taskResultProcessingSink)
{    
}

ProcessingNode::~ProcessingNode()
{
}

void ProcessingNode::Initialize(const std::string& processingChannelId, size_t msSubscriptionWaitingDuration)
{
    using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

    // Subscribe to the dataBlock channel
    m_processingChannel = std::make_shared<GossipPubSubTopic>(m_gossipPubSub, processingChannelId);

    m_room = std::make_unique<ProcessingRoom>(
        m_processingChannel, m_gossipPubSub->GetAsioContext(), m_nodeId, m_processingChannelCapacity);

    m_subtaskQueue = std::make_shared<ProcessingSubTaskQueue>(
        m_processingChannel, m_gossipPubSub->GetAsioContext(), m_nodeId, m_processingCore);
    m_processingEngine = std::make_unique<ProcessingEngine>(
        m_gossipPubSub, m_nodeId, m_processingCore, m_taskResultProcessingSink);

    // Run messages processing once all dependent object are created
    m_processingChannel->Subscribe(std::bind(&ProcessingNode::OnProcessingChannelMessage, this, std::placeholders::_1));
    if (msSubscriptionWaitingDuration > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(msSubscriptionWaitingDuration));
    }
}

void ProcessingNode::AttachTo(const std::string& processingChannelId, size_t msSubscriptionWaitingDuration)
{
    Initialize(processingChannelId, msSubscriptionWaitingDuration);
    m_room->AttachLocalNodeToRemoteRoom();
}

void ProcessingNode::CreateProcessingHost(
    const SGProcessing::Task& task, 
    size_t msSubscriptionWaitingDuration)
{
    Initialize(task.ipfs_block_id(), msSubscriptionWaitingDuration);

    m_room->Create();
    m_subtaskQueue->CreateQueue(task);

    m_processingEngine->StartQueueProcessing(m_subtaskQueue);
}

void ProcessingNode::OnProcessingChannelMessage(boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
{
    if (message)
    {
        SGProcessing::ProcessingChannelMessage channelMesssage;
        if (channelMesssage.ParseFromArray(message->data.data(), static_cast<int>(message->data.size())))
        {
            if (channelMesssage.has_processing_room_request())
            {
                HandleProcessingRoomRequest(channelMesssage);
            }
            else if (channelMesssage.has_processing_room())
            {
                HandleProcessingRoom(channelMesssage);
            }
            else if (channelMesssage.has_subtask_queue_request())
            {
                HandleSubTaskQueueRequest(channelMesssage);
            }
            else if (channelMesssage.has_subtask_queue())
            {
                HandleSubTaskQueue(channelMesssage);
            }
        }
    }
}

void ProcessingNode::HandleProcessingRoomRequest(SGProcessing::ProcessingChannelMessage& channelMesssage)
{
    if (m_room)
    {
        auto request = channelMesssage.processing_room_request();
        m_room->AttachNode(request.node_id());
    }
}

void ProcessingNode::HandleProcessingRoom(SGProcessing::ProcessingChannelMessage& channelMesssage)
{
    if (m_room)
    {
        if (m_room->UpdateRoom(channelMesssage.release_processing_room()))
        {
            if (!m_room->IsRoommate(m_nodeId))
            {
                if (m_room->GetNodesCount() < m_room->GetCapacity())
                {
                    // Room has available slots, try to attach again
                    m_room->AttachLocalNodeToRemoteRoom();
                }
                else
                {
                    m_processingChannel->Unsubscribe();
                }
                if (m_processingEngine && m_processingEngine->IsQueueProcessingStarted())
                {
                    m_processingEngine->StopQueueProcessing();
                }
            }
            else
            {
                if (m_processingEngine && !m_processingEngine->IsQueueProcessingStarted())
                {
                    m_processingEngine->StartQueueProcessing(m_subtaskQueue);
                }
            }
        }
    }
}

void ProcessingNode::HandleSubTaskQueueRequest(SGProcessing::ProcessingChannelMessage& channelMesssage)
{
    if (m_subtaskQueue)
    {
        m_subtaskQueue->ProcessSubTaskQueueRequestMessage(
            channelMesssage.subtask_queue_request());
    }
}

void ProcessingNode::HandleSubTaskQueue(SGProcessing::ProcessingChannelMessage& channelMesssage)
{
    if (m_subtaskQueue)
    {
        m_subtaskQueue->ProcessSubTaskQueueMessage(
            channelMesssage.release_subtask_queue());
    }
}


bool ProcessingNode::IsRoommate() const
{
    return (m_room && m_room->IsRoommate(m_nodeId));
}

bool ProcessingNode::IsAttachingToProcessingRoom() const
{
    return (m_room && m_room->IsLocalNodeAttachingToRemoteRoom());
}

bool ProcessingNode::IsRoomHost() const
{
    return (m_room && m_room->IsHost(m_nodeId));
}

const ProcessingRoom* ProcessingNode::GetRoom() const
{
    return m_room.get();
}

////////////////////////////////////////////////////////////////////////////////
}
