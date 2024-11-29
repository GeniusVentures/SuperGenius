/**
* Header file for the distrubuted processing node
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_NODE
#define GRPC_FOR_SUPERGENIUS_PROCESSING_NODE

#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

#include "processing/processing_engine.hpp"
#include "processing/processing_subtask_queue_manager.hpp"
#include "processing/processing_subtask_queue_accessor.hpp"
#include "processing/processing_subtask_state_storage.hpp"
#include "processing/processing_subtask_result_storage.hpp"

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
        ProcessingNode( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>        gossipPubSub,
                        std::shared_ptr<SubTaskStateStorage>                    subTaskStateStorage,
                        std::shared_ptr<SubTaskResultStorage>                   subTaskResultStorage,
                        std::shared_ptr<ProcessingCore>                         processingCore,
                        std::function<void( const SGProcessing::TaskResult & )> taskResultProcessingSink,
                        std::function<void( const std::string & )>              processingErrorSink,
                        std::string                                             node_id,
                        std::string                                             escrow_path );

        ~ProcessingNode();

        /** Attaches the node to the processing channel
    * @param processingQueueChannelId - identifier of a processing queue channel
    * @return flag indicating if the room is joined for block data processing
    */
        void AttachTo( const std::string &processingQueueChannelId, size_t msSubscriptionWaitingDuration = 0 );
        void CreateProcessingHost( const std::string                &processingQueueChannelId,
                                   std::list<SGProcessing::SubTask> &subTasks,
                                   size_t                            msSubscriptionWaitingDuration = 0 );

        bool HasQueueOwnership() const;

    private:
        void Initialize( const std::string &processingQueueChannelId, size_t msSubscriptionWaitingDuration );

        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> m_gossipPubSub;

        std::string                           m_nodeId;
        std::string                           m_escrowId;
        std::shared_ptr<ProcessingCore>       m_processingCore;
        std::shared_ptr<SubTaskStateStorage>  m_subTaskStateStorage;
        std::shared_ptr<SubTaskResultStorage> m_subTaskResultStorage;

        std::shared_ptr<ProcessingEngine>                       m_processingEngine;
        std::shared_ptr<ProcessingSubTaskQueueChannel>          m_queueChannel;
        std::shared_ptr<ProcessingSubTaskQueueManager>          m_subtaskQueueManager;
        std::shared_ptr<SubTaskQueueAccessor>                   m_subTaskQueueAccessor;
        std::function<void( const SGProcessing::TaskResult & )> m_taskResultProcessingSink;
        std::function<void( const std::string & )>              m_processingErrorSink;
    };
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_NODE
