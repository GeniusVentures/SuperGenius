#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_SERVICE
#define GRPC_FOR_SUPERGENIUS_PROCESSING_SERVICE

#include <unordered_map>

#include <libp2p/crypto/key.hpp>

#include "networkregistry/NetworkMembershipFilter.hpp"
#include "processing/processing_node.hpp"
#include "processing/processing_subtask_enqueuer.hpp"

namespace sgns::ipfs_bitswap
{
    class Bitswap;
}

namespace sgns::processing
{
    class ProcessingServiceImpl : public std::enable_shared_from_this<ProcessingServiceImpl>
    {
    public:
        enum class Status : uint8_t {
            DISABLED,
            IDLE,
            PROCESSING,
        };

        struct ProcessingStatus {
            Status status;
            float percentage; // 0.0 to 100.0

            ProcessingStatus(Status s = Status::DISABLED, float p = 0.0f)
                : status(s), percentage(p) {}
        };

        /**
         * @brief Constructs a processing service with user callbacks.
         * @param gossipPubSub PubSub service.
         * @param maximalNodesCount Max number of processing nodes handled by the service.
         * @param subTaskEnqueuer Subtask enqueuer used to dispatch tasks.
         * @param subTaskResultStorage Storage for subtask results.
         * @param processingCore Processing core used to execute subtasks.
         * @param userCallbackSuccess Callback invoked on successful task completion.
         * @param userCallbackError Callback invoked on task error.
         * @param node_address Local node address used in coordination.
         */
        ProcessingServiceImpl( std::shared_ptr<ipfs_pubsub::GossipPubSub>                        gossipPubSub,
                               size_t                                                            maximalNodesCount,
                               std::shared_ptr<SubTaskEnqueuer>                                  subTaskEnqueuer,
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

        [[nodiscard]] ProcessingStatus GetProcessingStatus() const;

        /// @brief Set callback for mirroring processing results. When set, results with mirror_result=true trigger a fetch.
        void setMirrorResultCallback( std::function<void( const std::string & )> callback );

        /// @brief Set bitswap instance propagated to all processing nodes for data availability checks.
        void setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap );

        /// @brief Set membership filter enforced at every processing-path message handler
        ///        (grid, results, and processing queue channels). Empty filter = public
        ///        pass-through. The filter is snapshotted BEFORE node creation at both
        ///        creation sites and passed INTO ProcessingNode::New, where it is
        ///        installed BEFORE any subscription goes live -- before the
        ///        queue-channel Listen() and before the results-channel
        ///        CreateResultsChannel/ConnectToSubTaskQueue -- so there is no
        ///        creation-time enrollment window (T-15-13-06, delivered by the
        ///        pre-subscription install; CR-G02a closed). Set-time propagation
        ///        (this call) refreshes existing nodes.
        void SetMembershipFilter( sgns::networkregistry::MembershipFilter filter );

        /// @brief Set the gossip host keypair used to SEAL private-network
        ///        processing-channel publishes and authenticate inbound ones
        ///        (CR-G01). Under a set membership filter every publish is
        ///        sealed (sgns::base::SealGossipPayload) and every inbound
        ///        message must open a verifiable envelope whose embedded key
        ///        derives the from-field PeerId (sgns::base::OpenGossipPayload)
        ///        BEFORE the membership predicate runs. Filter set + no key =
        ///        publishes fail closed. Propagates to all existing processing
        ///        nodes and, symmetric with SetMembershipFilter, is snapshotted
        ///        BEFORE node creation and passed INTO ProcessingNode::New to be
        ///        installed BEFORE any subscription goes live. No filter ->
        ///        raw, byte-identical.
        void SetGossipSigningKey( std::shared_ptr<const libp2p::crypto::KeyPair> key );

    private:
        /** Listen to data feed channel.
        * @param processingGridChannelId - identifier of a data feed channel
        */
        void Listen( const std::string &processingGridChannelId );

        void SendChannelListRequest();

        /** Asynchronous callback to process received messages from other processing services.
        * @param message - Message data with sender peer information.
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
        std::shared_ptr<SubTaskResultStorage>                  m_subTaskResultStorage;
        std::shared_ptr<ProcessingCore>                        m_processingCore;
        std::unique_ptr<sgns::ipfs_pubsub::GossipPubSubTopic>  m_gridChannel;
        std::unordered_map<std::string, std::shared_ptr<ProcessingNode>> m_processingNodes;
        boost::asio::deadline_timer                            m_timerChannelListRequestTimeout;
        boost::posix_time::time_duration                       m_channelListRequestTimeout;
        bool                                                   m_waitingChannelRequest = false;
        std::atomic<bool>                                      m_isStopped;
        mutable std::recursive_mutex                           m_mutexNodes;
        std::function<void( const std::string &subTaskQueueId, const SGProcessing::TaskResult &taskresult )>
                                                                 userCallbackSuccess_;
        std::function<void( const std::string &subTaskQueueId )> userCallbackError_;
        std::string                                              node_address_;

        std::function<void( const std::string & )> m_mirrorResultCallback; ///< Mirror callback propagated to all nodes.
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap> m_bitswap;             ///< Bitswap for data availability checks.

        sgns::networkregistry::MembershipFilter m_membershipFilter; ///< Membership gate for grid-channel messages (empty = public).
        mutable std::mutex                      m_membershipFilterMutex; ///< Guards m_membershipFilter and m_gossipSigningKey (setters vs pubsub callback threads).

        /// Gossip host keypair sealing private-network grid-channel publishes
        /// (CR-G01); guarded by m_membershipFilterMutex.
        std::shared_ptr<const libp2p::crypto::KeyPair> m_gossipSigningKey;

        std::set<std::string>                 m_competingPeers;
        std::chrono::steady_clock::time_point m_pendingCreationTimestamp;
        std::chrono::seconds                  m_pendingCreationTimeout{ 10 };

        boost::asio::deadline_timer         m_nodeCreationTimer;
        boost::posix_time::time_duration    m_nodeCreationTimeout;
        std::string                         m_pendingSubTaskQueueId;
        std::list<SGProcessing::SubTask>    m_pendingSubTasks;
        boost::optional<SGProcessing::Task> m_pendingTask;
        std::mutex                          m_mutexPendingCreation;

        // Blacklist for failed tasks/channels to prevent repeated processing attempts
        std::set<std::string> m_blacklistedChannels;
        std::mutex            m_mutexBlacklist;

        base::Logger m_logger = base::createLogger( "ProcessingService" );
    };
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_SERVICE
