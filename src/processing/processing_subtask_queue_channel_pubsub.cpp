#include "processing_subtask_queue_channel_pubsub.hpp"
#include <thread>

namespace sgns::processing
{
////////////////////////////////////////////////////////////////////////////////
ProcessingSubTaskQueueChannelPubSub::ProcessingSubTaskQueueChannelPubSub(
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
    const std::string& processingQueueChannelId)
    : m_gossipPubSub(gossipPubSub)
{
    using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;
    m_processingQueueChannel = std::make_shared<GossipPubSubTopic>(m_gossipPubSub, processingQueueChannelId);
}

ProcessingSubTaskQueueChannelPubSub::~ProcessingSubTaskQueueChannelPubSub()
{
    m_logger->debug("[RELEASED] this: {}", reinterpret_cast<size_t>(this));
}

void ProcessingSubTaskQueueChannelPubSub::Listen(size_t msSubscriptionWaitingDuration)
{
    // Run messages processing once all dependent object are created
    m_processingQueueChannel->Subscribe(
        std::bind(
            &ProcessingSubTaskQueueChannelPubSub::OnProcessingChannelMessage, 
            weak_from_this(), std::placeholders::_1));
    if (msSubscriptionWaitingDuration > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(msSubscriptionWaitingDuration));
    }
}

void ProcessingSubTaskQueueChannelPubSub::RequestQueueOwnership(const std::string& nodeId)
{
    // Send a request to grab a subtask queue
    SGProcessing::ProcessingChannelMessage message;
    auto request = message.mutable_subtask_queue_request();
    request->set_node_id(nodeId);
    m_processingQueueChannel->Publish(message.SerializeAsString());
}

void ProcessingSubTaskQueueChannelPubSub::PublishQueue(std::shared_ptr<SGProcessing::SubTaskQueue> queue)
{
    SGProcessing::ProcessingChannelMessage message;
    message.set_allocated_subtask_queue(queue.get());
    m_processingQueueChannel->Publish(message.SerializeAsString());
    message.release_subtask_queue();
}

void ProcessingSubTaskQueueChannelPubSub::SetQueueRequestSink(QueueRequestSink queueRequestSink)
{
    m_queueRequestSink = std::move(queueRequestSink);
}

void ProcessingSubTaskQueueChannelPubSub::SetQueueUpdateSink(QueueUpdateSink queueUpdateSink)
{
    m_queueUpdateSink = std::move(queueUpdateSink);
}

void ProcessingSubTaskQueueChannelPubSub::OnProcessingChannelMessage(
    std::weak_ptr<ProcessingSubTaskQueueChannelPubSub> weakThis,
    boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
{
    auto _this = weakThis.lock();
    if (!_this)
    {
        return;
    }
    
    if (message)
    {
        SGProcessing::ProcessingChannelMessage channelMesssage;
        if (channelMesssage.ParseFromArray(message->data.data(), static_cast<int>(message->data.size())))
        {
            if (channelMesssage.has_subtask_queue_request())
            {
                _this->HandleSubTaskQueueRequest(channelMesssage);
            }
            else if (channelMesssage.has_subtask_queue())
            {
                _this->HandleSubTaskQueue(channelMesssage);
            }
        }
    }
}

void ProcessingSubTaskQueueChannelPubSub::HandleSubTaskQueueRequest(SGProcessing::ProcessingChannelMessage& channelMesssage)
{
    if (m_queueRequestSink)
    {
        m_queueRequestSink(channelMesssage.subtask_queue_request());
    }
}

void ProcessingSubTaskQueueChannelPubSub::HandleSubTaskQueue(SGProcessing::ProcessingChannelMessage& channelMesssage)
{
    if (m_queueUpdateSink)
    {
        m_queueUpdateSink(channelMesssage.release_subtask_queue());
    }
}

////////////////////////////////////////////////////////////////////////////////
}
