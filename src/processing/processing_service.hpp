#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_SERVICE
#define GRPC_FOR_SUPERGENIUS_PROCESSING_SERVICE

#include "processing_node.hpp"
#include "processing_task_queue.hpp"

#include <map>

namespace sgns::processing
{
class ProcessingServiceImpl
{
public:
    /** Constructs a processing service.
    * @param gossipPubSub - pubsub service
    * @param maximalNodesCount - maximal number of processing nodes allowed to be handled by the service
    * @param processingChannelCapacity - maximal number of nodes that can be joined to a processing channel
    */
    ProcessingServiceImpl(
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub, 
        size_t maximalNodesCount,
        size_t processingChannelCapacity,
        std::shared_ptr<ProcessingTaskQueue> taskQueue,
        std::shared_ptr<ProcessingCore> processingCore);

    /** Listen to data feed channel.
    * @param dataChannelId - identifier of a data feed channel
    */
    void Listen(const std::string& processingGridChannelId);

    void SendChannelListRequest();

    size_t GetProcessingNodesCount() const;

    void SetChannelListRequestTimeout(
        boost::posix_time::time_duration channelListRequestTimeout);

private:
    std::map<std::string, std::shared_ptr<ProcessingNode>>& GetProcessingNodes();

    /** Asynschonous callback to process received messages other processing services.
    * @param message - a message structure containing the messsage data and its sender peer information.
    * @return None
    */
    void OnMessage(boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message);

    void AcceptProcessingChannel(const std::string& channelId, size_t channelCapacity);

    void PublishLocalChannelList();

    void HandleRequestTimeout();

    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> m_gossipPubSub;
    std::shared_ptr<boost::asio::io_context> m_context;
    size_t m_maximalNodesCount;
    size_t m_processingChannelCapacity;

    std::shared_ptr<ProcessingTaskQueue> m_taskQueue;
    std::shared_ptr<ProcessingCore> m_processingCore;

    std::unique_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> m_gridChannel;
    std::map<std::string, std::shared_ptr<ProcessingNode>> m_processingNodes;

    boost::asio::deadline_timer m_timerChannelListRequestTimeout;
    boost::posix_time::time_duration m_channelListRequestTimeout;
    base::Logger m_logger = base::createLogger("ProcessingService");
};
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_SERVICE