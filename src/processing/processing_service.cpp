#include "processing_service.hpp"

namespace sgns::processing
{
ProcessingServiceImpl::ProcessingServiceImpl(
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
    size_t maximalNodesCount,
    size_t processingChannelCapacity,
    std::shared_ptr<ProcessingTaksQueue> taskQueue,
    std::shared_ptr<ProcessingCore> processingCore)
    : m_gossipPubSub(gossipPubSub)
    , m_context(gossipPubSub->GetAsioContext())
    , m_maximalNodesCount(maximalNodesCount)
    , m_processingChannelCapacity(processingChannelCapacity)
    , m_taskQueue(taskQueue)
    , m_processingCore(processingCore)
    , m_timerChannelListRequestTimeout(*m_context.get())
    , m_channelListRequestTimeout(boost::posix_time::seconds(5))
{
}

void ProcessingServiceImpl::Listen(const std::string& processingGridChannelId)
{
    using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;
    m_gridChannel = std::make_unique<GossipPubSubTopic>(m_gossipPubSub, processingGridChannelId);
    m_gridChannel->Subscribe(std::bind(&ProcessingServiceImpl::OnMessage, this, std::placeholders::_1));
}

void ProcessingServiceImpl::SendChannelListRequest()
{
    SGProcessing::GridChannelMessage gridMessage;
    auto channelRequest = gridMessage.mutable_processing_channel_request();
    channelRequest->set_environment("any");

    m_gridChannel->Publish(gridMessage.SerializeAsString());
    m_logger->debug("List of processing channels requested");

    m_timerChannelListRequestTimeout.expires_from_now(m_channelListRequestTimeout);
    m_timerChannelListRequestTimeout.async_wait(std::bind(&ProcessingServiceImpl::HandleRequestTimeout, this));
}

void ProcessingServiceImpl::OnMessage(boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
{
    if (message)
    {
        SGProcessing::GridChannelMessage gridMessage;
        if (gridMessage.ParseFromArray(message->data.data(), static_cast<int>(message->data.size())))
        {
            if (gridMessage.has_processing_channel_response())
            {
                auto response = gridMessage.processing_channel_response();
                auto channelCapacity = response.channel_capacity();
                auto channelNodesJoined = response.channel_nodes_joined();

                m_logger->debug("Processing channel received. id:{}, capacity:{}, size:{}", 
                    response.channel_id(), channelCapacity, channelNodesJoined);

                if (channelCapacity > channelNodesJoined)
                {
                    AcceptProcessingChannel(response.channel_id(), channelCapacity);
                }
            }
            else if (gridMessage.has_processing_channel_request())
            {
                // @todo chenk environment requirements
                PublishLocalChannelList();
            }
        }
    }
}

void ProcessingServiceImpl::AcceptProcessingChannel(
    const std::string& channelId, size_t processingChannelCapacity)
{
    auto& processingNodes = GetProcessingNodes();
    if (processingNodes.size() < m_maximalNodesCount)
    {
        auto itNode = processingNodes.find(channelId);
        // No multiple processing nodes allowed for a single channel on a host
        if (itNode == processingNodes.end())
        {
            auto node = std::make_shared<ProcessingNode>(
                m_gossipPubSub, processingChannelCapacity, m_processingCore);
            node->AttachTo(channelId);
            processingNodes[channelId] = node;
        }
    }

    if (processingNodes.size() == m_maximalNodesCount)
    {
        m_timerChannelListRequestTimeout.expires_at(boost::posix_time::pos_infin);
    }
}

void ProcessingServiceImpl::PublishLocalChannelList()
{
    const auto& processingNodes = GetProcessingNodes();
    for (auto itNode = processingNodes.begin(); itNode != processingNodes.end(); ++itNode)
    {
        // Only channel host answers to reduce a number of published messages
        if (itNode->second->IsRoomHost())
        {
            SGProcessing::GridChannelMessage gridMessage;
            auto channelResponse = gridMessage.mutable_processing_channel_response();
            channelResponse->set_channel_id(itNode->first);
            channelResponse->set_channel_capacity(static_cast<uint32_t>(itNode->second->GetRoom()->GetCapacity()));
            channelResponse->set_channel_nodes_joined(static_cast<uint32_t>(itNode->second->GetRoom()->GetNodesCount()));

            m_gridChannel->Publish(gridMessage.SerializeAsString());
            m_logger->debug("Channel published. {}", channelResponse->channel_id());
        }
    }
}

std::map<std::string, std::shared_ptr<ProcessingNode>>& ProcessingServiceImpl::GetProcessingNodes()
{
    for (auto itNode = m_processingNodes.begin(); itNode != m_processingNodes.end(); )
    {
        if (!itNode->second->IsAttachingToProcessingRoom() && !itNode->second->IsRoommate())
        {
            itNode = m_processingNodes.erase(itNode);
        }
        else
        {
            ++itNode;
        }
    }
    return m_processingNodes;
}

size_t ProcessingServiceImpl::GetProcessingNodesCount() const
{
    return m_processingNodes.size();
}

void ProcessingServiceImpl::SetChannelListRequestTimeout(
    boost::posix_time::time_duration channelListRequestTimeout)
{
    m_channelListRequestTimeout = channelListRequestTimeout;
}

void ProcessingServiceImpl::HandleRequestTimeout()
{
    m_timerChannelListRequestTimeout.expires_at(boost::posix_time::pos_infin);
    auto& processingNodes = GetProcessingNodes();
    while (processingNodes.size() < m_maximalNodesCount)
    {
        SGProcessing::Task task;
        if (m_taskQueue->PopTask(task))
        {
            auto node = std::make_shared<ProcessingNode>(
                m_gossipPubSub, m_processingChannelCapacity, m_processingCore);

            // @todo Figure out if the task is still available for other peers
            node->CreateProcessingHost(task);
            processingNodes[task.ipfs_block_id()] = node;
            m_logger->debug("New processing channel created. {}", task.ipfs_block_id());
        }
        else
        {
            // If no tasks available in queue, try to get available slots in existent rooms
            SendChannelListRequest();
            break;
        }
    }
}
}
