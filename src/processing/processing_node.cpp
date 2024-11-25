#include "processing_node.hpp"

#include <utility>

#include "processing_subtask_queue_channel_pubsub.hpp"
#include "processing/processing_subtask_queue_accessor_impl.hpp"

namespace sgns::processing
{
    ////////////////////////////////////////////////////////////////////////////////
    ProcessingNode::ProcessingNode( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>        gossipPubSub,
                                    std::shared_ptr<SubTaskStateStorage>                    subTaskStateStorage,
                                    std::shared_ptr<SubTaskResultStorage>                   subTaskResultStorage,
                                    std::shared_ptr<ProcessingCore>                         processingCore,
                                    std::function<void( const SGProcessing::TaskResult & )> taskResultProcessingSink,
                                    std::function<void( const std::string & )>              processingErrorSink,
                                    std::string                                             node_id ) :
        m_gossipPubSub( std::move( gossipPubSub ) ),
        m_nodeId( std::move( node_id ) ),
        m_processingCore( std::move( processingCore ) ),
        m_subTaskStateStorage( std::move( subTaskStateStorage ) ),
        m_subTaskResultStorage( std::move( subTaskResultStorage ) ),
        m_taskResultProcessingSink( std::move( taskResultProcessingSink ) ),
        m_processingErrorSink( std::move( processingErrorSink ) )
    {
    }

    ProcessingNode::~ProcessingNode() {}

    void ProcessingNode::Initialize( const std::string &processingQueueChannelId, size_t msSubscriptionWaitingDuration )
    {
        // Subscribe to subtask queue channel
        auto processingQueueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( m_gossipPubSub,
                                                                                             processingQueueChannelId );

        m_subtaskQueueManager = std::make_shared<ProcessingSubTaskQueueManager>( processingQueueChannel,
                                                                                 m_gossipPubSub->GetAsioContext(),
                                                                                 m_nodeId );

        m_subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>( m_gossipPubSub,
                                                                             m_subtaskQueueManager,
                                                                             m_subTaskStateStorage,
                                                                             m_subTaskResultStorage,
                                                                             m_taskResultProcessingSink );

        processingQueueChannel->SetQueueRequestSink(
            [qmWeak( std::weak_ptr<ProcessingSubTaskQueueManager>( m_subtaskQueueManager ) )](
                const SGProcessing::SubTaskQueueRequest &request )
            {
                auto qm = qmWeak.lock();
                if ( qm )
                {
                    return qm->ProcessSubTaskQueueRequestMessage( request );
                }
                return false;
            } );

        processingQueueChannel->SetQueueUpdateSink(
            [qmWeak( std::weak_ptr<ProcessingSubTaskQueueManager>( m_subtaskQueueManager ) )](
                SGProcessing::SubTaskQueue *queue )
            {
                auto qm = qmWeak.lock();
                if ( qm )
                {
                    return qm->ProcessSubTaskQueueMessage( queue );
                }
                return false;
            } );

        m_processingEngine = std::make_shared<ProcessingEngine>( m_nodeId, m_processingCore );

        m_processingEngine->SetProcessingErrorSink( m_processingErrorSink );

        // Run messages processing once all dependent object are created
        processingQueueChannel->Listen( msSubscriptionWaitingDuration );

        // Keep the channel
        m_queueChannel = processingQueueChannel;
    }

    void ProcessingNode::AttachTo( const std::string &processingQueueChannelId, size_t msSubscriptionWaitingDuration )
    {
        Initialize( processingQueueChannelId, msSubscriptionWaitingDuration );

        m_subTaskQueueAccessor->ConnectToSubTaskQueue(
            [engineWeak( std::weak_ptr<ProcessingEngine>( m_processingEngine ) ),
             accessorWeak( std::weak_ptr<SubTaskQueueAccessor>( m_subTaskQueueAccessor ) )]()
            {
                auto engine   = engineWeak.lock();
                auto accessor = accessorWeak.lock();
                if ( engine && accessor )
                {
                    engine->StartQueueProcessing( accessor );
                }
            } );

        // @todo Set timer to handle queue request timeout
    }

    void ProcessingNode::CreateProcessingHost( const std::string                &processingQueueChannelId,
                                               std::list<SGProcessing::SubTask> &subTasks,
                                               size_t                            msSubscriptionWaitingDuration )
    {
        Initialize( processingQueueChannelId, msSubscriptionWaitingDuration );

        m_subTaskQueueAccessor->ConnectToSubTaskQueue(
            [engineWeak( std::weak_ptr<ProcessingEngine>( m_processingEngine ) ),
             accessorWeak( std::weak_ptr<SubTaskQueueAccessor>( m_subTaskQueueAccessor ) )]()
            {
                auto engine   = engineWeak.lock();
                auto accessor = accessorWeak.lock();
                if ( engine && accessor )
                {
                    engine->StartQueueProcessing( accessor );
                }
            } );

        m_subTaskQueueAccessor->AssignSubTasks( subTasks );
    }

    bool ProcessingNode::HasQueueOwnership() const
    {
        return m_subtaskQueueManager && m_subtaskQueueManager->HasOwnership();
    }

    ////////////////////////////////////////////////////////////////////////////////
}
