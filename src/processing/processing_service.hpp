#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_SERVICE
#define GRPC_FOR_SUPERGENIUS_PROCESSING_SERVICE

#include <map>

#include "processing/processing_node.hpp"
#include "processing/processing_subtask_enqueuer.hpp"

namespace sgns::processing
{
    class ProcessingServiceImpl : public std::enable_shared_from_this<ProcessingServiceImpl>
    {
    public:
        /** Constructs a processing service.
    * @param gossipPubSub - pubsub service
    * @param maximalNodesCount - maximal number of processing nodes allowed to be handled by the service
    */
        ProcessingServiceImpl( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
                               size_t                                           maximalNodesCount,
                               std::shared_ptr<SubTaskEnqueuer>                 subTaskEnqueuer,
                               std::shared_ptr<SubTaskStateStorage>             subTaskStateStorage,
                               std::shared_ptr<SubTaskResultStorage>            subTaskResultStorage,
                               std::shared_ptr<ProcessingCore>                  processingCore );
        ProcessingServiceImpl( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>                  gossipPubSub,
                               size_t                                                            maximalNodesCount,
                               std::shared_ptr<SubTaskEnqueuer>                                  subTaskEnqueuer,
                               std::shared_ptr<SubTaskStateStorage>                              subTaskStateStorage,
                               std::shared_ptr<SubTaskResultStorage>                             subTaskResultStorage,
                               std::shared_ptr<ProcessingCore>                                   processingCore,
                               std::function<void( const std::string              &subTaskQueueId,
                                                   const SGProcessing::TaskResult &taskresult )> userCallbackSuccess,
                               std::function<void( const std::string &subTaskQueueId )>          userCallbackError,
                               std::string                                                       node_address );

        ~ProcessingServiceImpl();

        void StartProcessing( const std::string &processingGridChannelId );
        void StopProcessing();

        size_t GetProcessingNodesCount() const;

        void SetChannelListRequestTimeout( boost::posix_time::time_duration channelListRequestTimeout );

    private:
        /** Listen to data feed channel.
    * @param dataChannelId - identifier of a data feed channel
    */
        void Listen( const std::string &processingGridChannelId );
        void SendChannelListRequest();
        /** Asynschonous callback to process received messages other processing services.
    * @param message - a message structure containing the messsage data and its sender peer information.
    * @return None
    */
        void OnMessage( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message );
        void OnQueueProcessingCompleted( const std::string              &subTaskQueueId,
                                         const SGProcessing::TaskResult &taskResult );
        void OnProcessingError( const std::string &subTaskQueueId, const std::string &errorMessage );
        void OnProcessingDone( const std::string &taskId );

        void AcceptProcessingChannel( const std::string &channelId );

        void PublishLocalChannelList();

        void HandleRequestTimeout();

        void BroadcastNodeCreationIntent( const std::string &subTaskQueueId );
        void HandleNodeCreationTimeout();
        void OnNodeCreationIntent( const std::string &peerAddress, const std::string &subTaskQueueId );
        bool HasLowestAddress() const;
        bool IsPendingCreationStale() const;
        void CancelPendingCreation( const std::string &reason );

        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>       m_gossipPubSub;
        std::shared_ptr<boost::asio::io_context>               m_context;
        std::unique_ptr<boost::asio::io_context::work>         m_context_work; // Keep context alive
        std::thread                                            io_thread;
        size_t                                                 m_maximalNodesCount;
        std::shared_ptr<SubTaskEnqueuer>                       m_subTaskEnqueuer;
        std::shared_ptr<SubTaskStateStorage>                   m_subTaskStateStorage;
        std::shared_ptr<SubTaskResultStorage>                  m_subTaskResultStorage;
        std::shared_ptr<ProcessingCore>                        m_processingCore;
        std::unique_ptr<sgns::ipfs_pubsub::GossipPubSubTopic>  m_gridChannel;
        std::map<std::string, std::shared_ptr<ProcessingNode>> m_processingNodes;
        boost::asio::deadline_timer                            m_timerChannelListRequestTimeout;
        boost::posix_time::time_duration                       m_channelListRequestTimeout;
        bool                                                   m_waitingCHannelRequest = false;
        std::atomic<bool>                                      m_isStopped;
        mutable std::recursive_mutex                           m_mutexNodes;
        std::function<void( const std::string &subTaskQueueId, const SGProcessing::TaskResult &taskresult )>
                                                                 userCallbackSuccess_;
        std::function<void( const std::string &subTaskQueueId )> userCallbackError_;
        std::string                                              node_address_;

        std::set<std::string>                 m_competingPeers;
        std::chrono::steady_clock::time_point m_pendingCreationTimestamp;
        std::chrono::seconds                  m_pendingCreationTimeout{ 10 };

        boost::asio::deadline_timer         m_nodeCreationTimer;
        boost::posix_time::time_duration    m_nodeCreationTimeout;
        std::string                         m_pendingSubTaskQueueId;
        std::list<SGProcessing::SubTask>    m_pendingSubTasks;
        boost::optional<SGProcessing::Task> m_pendingTask;
        std::mutex                          m_mutexPendingCreation;

        base::Logger m_logger = base::createLogger( "ProcessingService" );
    };
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_SERVICE
