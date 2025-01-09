#include "processing_service.hpp"

#include <utility>

namespace sgns::processing
{
    ProcessingServiceImpl::ProcessingServiceImpl( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
                                                  size_t                                           maximalNodesCount,
                                                  std::shared_ptr<SubTaskEnqueuer>                 subTaskEnqueuer,
                                                  std::shared_ptr<SubTaskStateStorage>             subTaskStateStorage,
                                                  std::shared_ptr<SubTaskResultStorage>            subTaskResultStorage,
                                                  std::shared_ptr<ProcessingCore>                  processingCore ) :
        m_gossipPubSub( std::move( gossipPubSub ) ),
        m_context( m_gossipPubSub->GetAsioContext() ),
        m_maximalNodesCount( maximalNodesCount ),
        m_subTaskEnqueuer( std::move( subTaskEnqueuer ) ),
        m_subTaskStateStorage( std::move( subTaskStateStorage ) ),
        m_subTaskResultStorage( std::move( subTaskResultStorage ) ),
        m_processingCore( std::move( processingCore ) ),
        m_timerChannelListRequestTimeout( *m_context ),
        m_channelListRequestTimeout( boost::posix_time::seconds( 5 ) ),
        m_isStopped( true ),
        node_address_( m_gossipPubSub->GetLocalAddress() )
    {
    }

    ProcessingServiceImpl::ProcessingServiceImpl(
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
        size_t                                           maximalNodesCount,
        std::shared_ptr<SubTaskEnqueuer>                 subTaskEnqueuer,
        std::shared_ptr<SubTaskStateStorage>             subTaskStateStorage,
        std::shared_ptr<SubTaskResultStorage>            subTaskResultStorage,
        std::shared_ptr<ProcessingCore>                  processingCore,
        std::function<void( const std::string &subTaskQueueId, const SGProcessing::TaskResult &taskresult )>
                                                                 userCallbackSuccess,
        std::function<void( const std::string &subTaskQueueId )> userCallbackError,
        std::string                                              node_address ) :
        m_gossipPubSub( std::move( gossipPubSub ) ),
        m_context( m_gossipPubSub->GetAsioContext() ),
        m_maximalNodesCount( maximalNodesCount ),
        m_subTaskEnqueuer( std::move( subTaskEnqueuer ) ),
        m_subTaskStateStorage( std::move( subTaskStateStorage ) ),
        m_subTaskResultStorage( std::move( subTaskResultStorage ) ),
        m_processingCore( std::move( processingCore ) ),
        m_timerChannelListRequestTimeout( *m_context ),
        m_channelListRequestTimeout( boost::posix_time::seconds( 1 ) ),
        m_isStopped( true ),
        userCallbackSuccess_( std::move( userCallbackSuccess ) ),
        userCallbackError_( std::move( userCallbackError ) ),
        node_address_( std::move( node_address ) )
    {
    }

    void ProcessingServiceImpl::StartProcessing( const std::string &processingGridChannelId )
    {
        if ( !m_isStopped )
        {
            m_logger->debug( "[{}] [SERVICE_WAS_PREVIOUSLY_STARTED]", node_address_ );
            return;
        }

        m_isStopped = false;

        Listen( processingGridChannelId );
        SendChannelListRequest();
        m_logger->debug( "[{}] [SERVICE_STARTED]", node_address_ );
    }

    void ProcessingServiceImpl::StopProcessing()
    {
        if ( m_isStopped )
        {
            return;
        }

        m_isStopped = true;

        m_gridChannel->Unsubscribe();

        {
            std::scoped_lock lock( m_mutexNodes );
            m_processingNodes = {};
        }

        m_logger->debug( "[{}] [SERVICE_STOPPED]", node_address_ );
    }

    void ProcessingServiceImpl::Listen( const std::string &processingGridChannelId )
    {
        using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;
        m_gridChannel           = std::make_unique<GossipPubSubTopic>( m_gossipPubSub, processingGridChannelId );
        m_gridChannel->Subscribe( std::bind( &ProcessingServiceImpl::OnMessage, this, std::placeholders::_1 ) );
    }

    void ProcessingServiceImpl::SendChannelListRequest()
    {
        if ( m_waitingCHannelRequest )
        {
            return;
        }
        m_waitingCHannelRequest = true;
        SGProcessing::GridChannelMessage gridMessage;
        auto                             channelRequest = gridMessage.mutable_processing_channel_request();
        channelRequest->set_environment( "any" );

        m_gridChannel->Publish( gridMessage.SerializeAsString() );
        m_logger->debug( "List of processing channels requested" );
        m_timerChannelListRequestTimeout.expires_from_now( m_channelListRequestTimeout );
        m_timerChannelListRequestTimeout.async_wait( std::bind( &ProcessingServiceImpl::HandleRequestTimeout, this ) );
    }

    void ProcessingServiceImpl::OnMessage( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message )
    {
        m_logger->debug( "[{}] On Message.", node_address_ );
        if ( message )
        {
            SGProcessing::GridChannelMessage gridMessage;
            if ( gridMessage.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
            {
                if ( gridMessage.has_processing_channel_response() )
                {
                    auto response = gridMessage.processing_channel_response();

                    m_logger->debug( "[{}] Processing channel received. id:{}", node_address_, response.channel_id() );

                    AcceptProcessingChannel( response.channel_id() );
                }
                else if ( gridMessage.has_processing_channel_request() )
                {
                    // @todo chenk environment requirements
                    PublishLocalChannelList();
                }
            }
        }
    }

    void ProcessingServiceImpl::OnQueueProcessingCompleted( const std::string              &subTaskQueueId,
                                                            const SGProcessing::TaskResult &taskResult )
    {
        m_logger->debug( "[{}] SUBTASK_QUEUE_PROCESSING_COMPLETED: {}", node_address_, subTaskQueueId );

        {
            std::scoped_lock lock( m_mutexNodes );
            m_processingNodes.erase( subTaskQueueId );
        }

        if ( userCallbackSuccess_ )
        {
            userCallbackSuccess_( subTaskQueueId, taskResult );
        }

        if ( !m_isStopped )
        {
            SendChannelListRequest();
        }
        // @todo finalize task
        // @todo Add notification of finished task
    }

    void ProcessingServiceImpl::OnProcessingError( const std::string &subTaskQueueId, const std::string &errorMessage )
    {
        m_logger->error( "[{}] PROCESSING_ERROR reason: {}", node_address_, errorMessage );

        {
            std::scoped_lock lock( m_mutexNodes );
            m_processingNodes.erase( subTaskQueueId );
        }
        if ( userCallbackError_ )
        {
            userCallbackError_( subTaskQueueId );
        }

        // @todo Stop processing?

        if ( !m_isStopped )
        {
            SendChannelListRequest();
        }
    }

    void ProcessingServiceImpl::AcceptProcessingChannel( const std::string &processingQueuelId )
    {
        if ( m_isStopped )
        {
            return;
        }
        m_logger->debug( "[{}] AcceptProcessingChannel", node_address_ );
        std::scoped_lock lock( m_mutexNodes );
        m_logger->debug( "[{}] Number of nodes: {}, Max nodes:  {}",
                         node_address_,
                         m_processingNodes.size(),
                         m_maximalNodesCount );
        if ( m_processingNodes.size() < m_maximalNodesCount )
        {
            m_logger->debug( "[{}] Accept Channel Create Node", node_address_ );
            auto node = std::make_shared<ProcessingNode>(
                m_gossipPubSub,
                m_subTaskStateStorage,
                m_subTaskResultStorage,
                m_processingCore,
                std::bind( &ProcessingServiceImpl::OnQueueProcessingCompleted,
                           this,
                           processingQueuelId,
                           std::placeholders::_1 ),
                std::bind( &ProcessingServiceImpl::OnProcessingError, this, processingQueuelId, std::placeholders::_1 ),
                node_address_,
                "" );
            m_logger->debug( "Attach to processing Queue" );
            node->AttachTo( processingQueuelId );
            m_processingNodes[processingQueuelId] = node;
        }

        if ( m_processingNodes.size() == m_maximalNodesCount )
        {
            m_timerChannelListRequestTimeout.expires_at( boost::posix_time::pos_infin );
        }
    }

    void ProcessingServiceImpl::PublishLocalChannelList()
    {
        m_logger->debug( "Publish Local Channels." );
        std::scoped_lock lock( m_mutexNodes );
        for ( auto &itNode : m_processingNodes )
        {
            m_logger->debug( "Owns Channel? {}.", itNode.second->HasQueueOwnership() );
            // Only channel host answers to reduce a number of published messages
            if ( itNode.second->HasQueueOwnership() )
            {
                SGProcessing::GridChannelMessage gridMessage;
                auto                             channelResponse = gridMessage.mutable_processing_channel_response();
                channelResponse->set_channel_id( itNode.first );

                m_gridChannel->Publish( gridMessage.SerializeAsString() );
                m_logger->debug( "Channel published. {}", channelResponse->channel_id() );
            }
        }
    }

    size_t ProcessingServiceImpl::GetProcessingNodesCount() const
    {
        std::scoped_lock lock( m_mutexNodes );
        return m_processingNodes.size();
    }

    void ProcessingServiceImpl::SetChannelListRequestTimeout(
        boost::posix_time::time_duration channelListRequestTimeout )
    {
        m_channelListRequestTimeout = channelListRequestTimeout;
    }

    void ProcessingServiceImpl::HandleRequestTimeout()
    {
        m_waitingCHannelRequest = false;
        m_logger->debug( "QUEUE_REQUEST_TIMEOUT" );
        m_timerChannelListRequestTimeout.expires_at( boost::posix_time::pos_infin );

        if ( m_isStopped )
        {
            return;
        }
        bool node_dispatched = false;
        {
            std::scoped_lock lock( m_mutexNodes );
            while ( m_processingNodes.size() < m_maximalNodesCount )
            {
                std::string                      subTaskQueueId;
                std::list<SGProcessing::SubTask> subTasks;
                auto maybe_task = m_subTaskEnqueuer->EnqueueSubTasks( subTaskQueueId, subTasks );
                if ( maybe_task )
                {
                    m_logger->debug( "[{}] Grabbed task, creating first node", node_address_ );
                    auto node = std::make_shared<ProcessingNode>(
                        m_gossipPubSub,
                        m_subTaskStateStorage,
                        m_subTaskResultStorage,
                        m_processingCore,
                        std::bind( &ProcessingServiceImpl::OnQueueProcessingCompleted,
                                   this,
                                   subTaskQueueId,
                                   std::placeholders::_1 ),
                        std::bind( &ProcessingServiceImpl::OnProcessingError,
                                   this,
                                   subTaskQueueId,
                                   std::placeholders::_1 ),
                        node_address_,
                        maybe_task.value().escrow_path() );

                    // @todo Figure out if the task is still available for other peers
                    // @todo Check if it is better to call EnqueueSubTasks within host
                    // and return subTaskQueueId from processing host?
                    node->CreateProcessingHost( subTaskQueueId, subTasks );

                    m_processingNodes[subTaskQueueId] = node;

                    m_logger->debug( "New processing channel created. {}", subTaskQueueId );
                    node_dispatched = true;
                    //PublishLocalChannelList();
                }
                else
                {
                    // If no tasks enquued, try to get available slots in existent queues
                    SendChannelListRequest();
                    break;
                }
            }
        }
        if ( node_dispatched )
        {
            PublishLocalChannelList();
        }
    }
}
