#include "processing_node.hpp"

#include <utility>

#include "processing_subtask_queue_channel_pubsub.hpp"
#include "processing/processing_subtask_queue_accessor_impl.hpp"

namespace sgns::processing
{
    ////////////////////////////////////////////////////////////////////////////////
    std::shared_ptr<ProcessingNode> ProcessingNode::New(
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>        gossipPubSub,
        std::shared_ptr<SubTaskStateStorage>                    subTaskStateStorage,
        std::shared_ptr<SubTaskResultStorage>                   subTaskResultStorage,
        std::shared_ptr<ProcessingCore>                         processingCore,
        std::function<void( const SGProcessing::TaskResult & )> taskResultProcessingSink,
        std::function<void( const std::string & )>              processingErrorSink,
        std::string                                             node_id,
        std::chrono::seconds                                    ttl )
    {
        // Create the shared_ptr using the protected constructor
        auto node = std::shared_ptr<ProcessingNode>( new ProcessingNode( std::move( gossipPubSub ),
                                                                         std::move( subTaskStateStorage ),
                                                                         std::move( subTaskResultStorage ),
                                                                         std::move( processingCore ),
                                                                         std::move( taskResultProcessingSink ),
                                                                         std::move( processingErrorSink ),
                                                                         std::move( node_id ),
                                                                         std::move( ttl ) ) );

        // Now that we have a valid shared_ptr, start the TTL timer
        node->InitializeTTLTimer();
        return node;
    }

    ProcessingNode::ProcessingNode( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>        gossipPubSub,
                                    std::shared_ptr<SubTaskStateStorage>                    subTaskStateStorage,
                                    std::shared_ptr<SubTaskResultStorage>                   subTaskResultStorage,
                                    std::shared_ptr<ProcessingCore>                         processingCore,
                                    std::function<void( const SGProcessing::TaskResult & )> taskResultProcessingSink,
                                    std::function<void( const std::string & )>              processingErrorSink,
                                    std::string                                             node_id,
                                    std::chrono::seconds                                    ttl ) :

        m_gossipPubSub( std::move( gossipPubSub ) ),
        m_nodeId( std::move( node_id ) ),
        m_processingCore( std::move( processingCore ) ),
        m_subTaskStateStorage( std::move( subTaskStateStorage ) ),
        m_subTaskResultStorage( std::move( subTaskResultStorage ) ),
        m_taskResultProcessingSink( std::move( taskResultProcessingSink ) ),
        m_processingErrorSink( std::move( processingErrorSink ) ),
        m_creationTime( std::chrono::steady_clock::now() ),
        m_ttl( ttl )
    {
        m_logger->debug( "[{}] Processing node CREATED", m_nodeId );
    }

    ProcessingNode::~ProcessingNode()
    {
        m_logger->debug( "[{}] Processing node DELETED ", m_nodeId );
    }

    void ProcessingNode::Initialize( const std::string &processingQueueChannelId, size_t msSubscriptionWaitingDuration )
    {
        // Subscribe to subtask queue channel
        auto processingQueueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( m_gossipPubSub,
                                                                                             processingQueueChannelId );

        m_subtaskQueueManager = std::make_shared<ProcessingSubTaskQueueManager>( processingQueueChannel,
                                                                                 m_gossipPubSub->GetAsioContext(),
                                                                                 m_nodeId,
                                                                                 m_processingErrorSink );

        m_subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>( m_gossipPubSub,
                                                                             m_subtaskQueueManager,
                                                                             m_subTaskStateStorage,
                                                                             m_subTaskResultStorage,
                                                                             m_taskResultProcessingSink,
                                                                             m_processingErrorSink );

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
        m_logger->debug( "[{}] Processing node INITIALIZED", m_nodeId );
    }

    void ProcessingNode::AttachTo( const std::string &processingQueueChannelId, size_t msSubscriptionWaitingDuration )
    {
        m_logger->debug( "[{}] Processing node AttachTo {} ", m_nodeId, processingQueueChannelId );
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
        ResetTTL();
    }

    void ProcessingNode::CreateProcessingHost( const std::string                &processingQueueChannelId,
                                               std::list<SGProcessing::SubTask> &subTasks,
                                               size_t                            msSubscriptionWaitingDuration )
    {
        m_logger->debug( "[{}] Processing node Create the processing for {} ", m_nodeId, processingQueueChannelId );

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

        ResetTTL();
    }

    bool ProcessingNode::HasQueueOwnership() const
    {
        return m_subtaskQueueManager && m_subtaskQueueManager->HasOwnership();
    }

    void ProcessingNode::ResetTTL()
    {
        // Reset creation time to now
        m_creationTime = std::chrono::steady_clock::now();

        // Restart the timer
        if ( m_ttlTimer )
        {
            StartTTLTimer();
        }
    }

    void ProcessingNode::InitializeTTLTimer()
    {
        if ( m_ttlTimer )
        {
            StartTTLTimer();
        }
    }

    void ProcessingNode::StartTTLTimer()
    {
        if ( !m_ttlTimer )
        {
            return;
        }

        m_ttlTimer->cancel();
        m_ttlTimer->expires_after( m_ttl );

        // Use weak_ptr to avoid circular reference
        std::weak_ptr<ProcessingNode> weakThis = shared_from_this();

        m_ttlTimer->async_wait(
            [weakThis]( const std::error_code &ec )
            {
                if ( auto self = weakThis.lock() )
                {
                    self->CheckTTL( ec );
                }
            } );
    }

    void ProcessingNode::CheckTTL( const std::error_code &ec )
    {
        // If canceled or error, don't do anything
        if ( ec )
        {
            return;
        }

        // Calculate elapsed time
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>( now - m_creationTime );

        // If TTL exceeded, self-destruct
        if ( elapsed >= m_ttl )
        {
            m_logger->error( "Processing node {} TTL expired after {} seconds. Self-destructing...",
                             m_nodeId,
                             elapsed.count() );

            // Cancel the timer to prevent any further callbacks
            m_ttlTimer->cancel();

            // Clean up resources before self-destruction
            m_processingEngine.reset();
            m_subtaskQueueManager.reset();
            m_subTaskQueueAccessor.reset();
            m_queueChannel.reset();

            // Let shared_ptr ownership mechanism handle the actual deletion
            // The object will be deleted when the last shared_ptr reference is released
        }
        else
        {
            // Not yet time to destruct, schedule another check
            m_ttlTimer->expires_after( std::chrono::seconds( 10 ) );

            std::weak_ptr<ProcessingNode> weakThis = weak_from_this();
            m_ttlTimer->async_wait(
                [weakThis]( const std::error_code &ec )
                {
                    if ( auto self = weakThis.lock() )
                    {
                        self->CheckTTL( ec );
                    }
                } );
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
}
