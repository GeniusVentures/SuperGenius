/**
* Header file for subtask queue channel implementation 
* @author creativeid00
*/

#ifndef SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_CHANNEL_PUBSUB_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_CHANNEL_PUBSUB_HPP

#include <future>
#include "outcome/outcome.hpp"

#include "processing/processing_subtask_queue_channel.hpp"

#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include "base/logger.hpp"

using namespace sgns::ipfs_pubsub;

namespace sgns::processing
{
    /** Subtask queue channel implementation that uses pubsub as a data transport protocol
*/
    class ProcessingSubTaskQueueChannelPubSub : public ProcessingSubTaskQueueChannel,
                                                public std::enable_shared_from_this<ProcessingSubTaskQueueChannelPubSub>
    {
    public:
        using QueueRequestSink = std::function<bool( const SGProcessing::SubTaskQueueRequest & )>;
        using QueueUpdateSink  = std::function<bool( SGProcessing::SubTaskQueue * )>;

        /** Constructs subtask queue channel object
    * @param gossipPubSub - ipfs pubsub
    * @param processingQueueChannelId - a unique id of queue data channel
    */
        ProcessingSubTaskQueueChannelPubSub( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
                                             const std::string &processingQueueChannelId );

        ~ProcessingSubTaskQueueChannelPubSub() override;

        /** ProcessingSubTaskQueueChannel overrides
    */
        void RequestQueueOwnership( const std::string &nodeId ) override;
        void PublishQueue( std::shared_ptr<SGProcessing::SubTaskQueue> queue ) override;

        /** Sets a handler for remote queue requests processing
        * @param queueRequestSink - request handler
        */
        void SetQueueRequestSink( QueueRequestSink queueRequestSink );

        /** Sets a handler for remote queue updates processing
        */
        void SetQueueUpdateSink( QueueUpdateSink queueUpdateSink );

        /** Starts a listening to pubsub channel
         * @param msSubscriptionWaitingDuration - Duration to wait for subscription, 0 means no waiting
         * @return If msSubscriptionWaitingDuration > 0: outcome with success/failure and actual wait time
         *         If msSubscriptionWaitingDuration = 0: outcome with future that completes when subscription is established
         */
        outcome::result<std::variant<std::chrono::milliseconds, std::future<GossipPubSub::Subscription>>> Listen(
            std::chrono::milliseconds msSubscriptionWaitingDuration = std::chrono::milliseconds(2000));

        /** Retrieves the count of active nodes in the subtask queue channel.
         * @return The number of active nodes currently participating in the channel.
         */
        size_t GetActiveNodesCount() const override;

        /**
         * Retrieves the list of active node IDs currently participating in the subtask queue channel.
         * @return A vector of strings containing the IDs of active nodes in the channel.
         */
        std::vector<libp2p::peer::PeerId> GetActiveNodes() const override;


    private:
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> m_processingQueueChannel;

        void OnProcessingChannelMessage( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message );

        void HandleSubTaskQueueRequest( SGProcessing::ProcessingChannelMessage &channelMesssage );
        void HandleSubTaskQueue( SGProcessing::ProcessingChannelMessage &channelMesssage );

        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> m_gossipPubSub;
        std::shared_ptr<boost::asio::io_context>         m_context;

        std::function<bool( const SGProcessing::SubTaskQueueRequest & )> m_queueRequestSink;
        std::function<bool( SGProcessing::SubTaskQueue * )>              m_queueUpdateSink;

        base::Logger m_logger = base::createLogger( "ProcessingSubTaskQueueChannelPubSub" );


    };
}
#endif // SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_CHANNEL_PUBSUB_HPP
