/**
* Header file for the distrubuted processing node
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_NODE
#define GRPC_FOR_SUPERGENIUS_PROCESSING_NODE

#include <chrono>
#include <thread>
#include <optional>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

#include "processing/processing_engine.hpp"
#include "processing/processing_subtask_queue_manager.hpp"
#include "processing/processing_subtask_queue_accessor.hpp"
#include "processing/processing_subtask_result_storage.hpp"

namespace sgns::ipfs_bitswap
{
    class Bitswap;
}

namespace sgns::processing
{
    /**
     * @brief Node for distributed computation.
     *
     * Coordinates subtask queue ownership, processing, and result publication.
     */
    class ProcessingNode : public std::enable_shared_from_this<ProcessingNode>
    {
    public:
        /**
         * @brief Creates a processing node instance.
         * @param gossipPubSub PubSub service for queue coordination.
         * @param subTaskResultStorage Storage for subtask results.
         * @param processingCore Processing core to execute subtasks.
         * @param taskResultProcessingSink Callback for task result processing.
         * @param processingErrorSink Callback for processing errors.
         * @param processingDoneSink Callback when processing is done.
         * @param node_id Identifier of the processing node.
         * @param processingQueueChannelId Queue channel identifier.
         * @param subTasks Optional initial subtask list.
         * @param msSubscriptionWaitingDuration Wait duration for queue subscription.
         * @param ttl Time-to-live for node ownership.
         */
        static std::shared_ptr<ProcessingNode> New(
            std::shared_ptr<ipfs_pubsub::GossipPubSub>              gossipPubSub,
            std::shared_ptr<SubTaskResultStorage>                   subTaskResultStorage,
            std::shared_ptr<ProcessingCore>                         processingCore,
            std::function<void( const SGProcessing::TaskResult & )> taskResultProcessingSink,
            std::function<void( const std::string & )>              processingErrorSink,
            std::function<void( void )>                             processingDoneSink,
            std::string                                             node_id,
            const std::string                                      &processingQueueChannelId,
            std::list<SGProcessing::SubTask>                        subTasks = {},
            std::chrono::milliseconds msSubscriptionWaitingDuration          = std::chrono::milliseconds( 2000 ),
            std::chrono::seconds      ttl                                    = std::chrono::minutes( 2 ) );

        ~ProcessingNode();

        bool HasQueueOwnership() const;

        /** Set callback for mirroring results from other nodes */
        void setMirrorResultCallback( std::function<void( const std::string & )> callback );

        /** Set bitswap instance for data availability checks */
        void setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap );

        /** Get current processing progress
        * @return Progress percentage (0.0 to 100.0)
        */
        float GetProgress() const;

    private:
        /** Constructs a processing node.
        * @param gossipPubSub PubSub service.
        * @param subTaskResultStorage Storage for subtask results.
        * @param processingCore Processing core to execute subtasks.
        * @param taskResultProcessingSink Callback for task result processing.
        * @param processingErrorSink Callback for processing errors.
        * @param processingDoneSink Callback when processing is done.
        * @param node_id Identifier of the processing node.
        * @param ttl Time-to-live for node ownership.
        */
        ProcessingNode( std::shared_ptr<ipfs_pubsub::GossipPubSub>              gossipPubSub,
                        std::shared_ptr<SubTaskResultStorage>                   subTaskResultStorage,
                        std::shared_ptr<ProcessingCore>                         processingCore,
                        std::function<void( const SGProcessing::TaskResult & )> taskResultProcessingSink,
                        std::function<void( const std::string & )>              processingErrorSink,
                        std::function<void( void )>                             processingDoneSink,
                        std::string                                             node_id,
                        std::chrono::seconds                                    ttl );

        bool AttachTo( const std::string &processingQueueChannelId );
        bool CreateSubTaskQueue( std::list<SGProcessing::SubTask> subTasks );
        void Initialize( const std::string        &processingQueueChannelId,
                         std::chrono::milliseconds msSubscriptionWaitingDuration );

        void InitTTL();
        void StartTTLTimer();
        void CheckTTL( const std::error_code &ec );

        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> m_gossipPubSub;

        std::string                           m_nodeId;
        std::string                           m_escrowId;
        std::shared_ptr<ProcessingCore>       m_processingCore;
        std::shared_ptr<SubTaskResultStorage> m_subTaskResultStorage;

        std::shared_ptr<ProcessingEngine>                       m_processingEngine;
        std::shared_ptr<ProcessingSubTaskQueueChannel>          m_queueChannel;
        std::shared_ptr<ProcessingSubTaskQueueManager>          m_subtaskQueueManager;
        std::shared_ptr<SubTaskQueueAccessor>                   m_subTaskQueueAccessor;
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap>            m_bitswap; ///< Cached for accessor when created.
        std::function<void( const SGProcessing::TaskResult & )> m_taskResultProcessingSink;
        std::function<void( const std::string & )>              m_processingErrorSink;
        std::function<void( void )>                             m_processingDoneSink;

        std::chrono::steady_clock::time_point                  m_creationTime;
        std::chrono::seconds                                   m_ttl;
        std::unique_ptr<boost::asio::steady_timer>             m_ttlTimer;
        std::function<void( std::shared_ptr<ProcessingNode> )> m_selfDestructCallback;

        std::shared_ptr<boost::asio::io_context> m_localContext;
        using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
        std::optional<WorkGuard> m_localWorkGuard;
        std::thread              m_localIoThread;

        base::Logger m_logger = base::createLogger( "ProcessingNode" );
    };
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_NODE
