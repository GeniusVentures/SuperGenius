/**
* Header file for subtask queue channel implementation 
* @author creativeid00
*/

#ifndef SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_CHANNEL_PUBSUB_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_CHANNEL_PUBSUB_HPP

#include "processing/processing_subtask_queue_channel.hpp"

#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include "base/logger.hpp"

namespace sgns::processing
{
/** Subtask queue channel implementation that uses pubsub as a data transport protocol
*/
class ProcessingSubTaskQueueChannelPubSub : public ProcessingSubTaskQueueChannel,
    public std::enable_shared_from_this<ProcessingSubTaskQueueChannelPubSub>
{
public:
    typedef std::function<bool(const SGProcessing::SubTaskQueueRequest&)> QueueRequestSink;
    typedef std::function<bool(SGProcessing::SubTaskQueue*)> QueueUpdateSink;

    /** Constructs subtask queue channel object
    * @param gossipPubSub - ipfs pubsub
    * @param processingQueueChannelId - a unique id of queue data channel
    */
    ProcessingSubTaskQueueChannelPubSub(
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
        const std::string& processingQueueChannelId);

    virtual ~ProcessingSubTaskQueueChannelPubSub();

    /** ProcessingSubTaskQueueChannel overrides
    */
    void RequestQueueOwnership(const std::string& nodeId) override;
    void PublishQueue(std::shared_ptr<SGProcessing::SubTaskQueue> queue) override;

    /** Sets a handler for remote queue requests processing
    * @param queueRequestSink - request handler
    */
    void SetQueueRequestSink(QueueRequestSink queueRequestSink);

    /** Sets a handler for remote queue updates processing
    */
    void SetQueueUpdateSink(QueueUpdateSink queueUpdateSink);

    /** Starts a listening to pubsub channel
    */
    void Listen(size_t msSubscriptionWaitingDuration);

private:
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> m_processingQueueChannel;

    static void OnProcessingChannelMessage(
        std::weak_ptr<ProcessingSubTaskQueueChannelPubSub> weakThis,
        boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message);

    void HandleSubTaskQueueRequest(SGProcessing::ProcessingChannelMessage& channelMesssage);
    void HandleSubTaskQueue(SGProcessing::ProcessingChannelMessage& channelMesssage);

    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> m_gossipPubSub;
    std::shared_ptr<boost::asio::io_context> m_context;

    std::function<bool(const SGProcessing::SubTaskQueueRequest&)> m_queueRequestSink;
    std::function<bool(SGProcessing::SubTaskQueue*)> m_queueUpdateSink;

    base::Logger m_logger = base::createLogger("ProcessingSubTaskQueueChannelPubSub");
};
}
#endif // SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_CHANNEL_PUBSUB_HPP