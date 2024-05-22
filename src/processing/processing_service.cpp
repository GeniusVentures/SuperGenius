#include "processing_service.hpp"

namespace sgns::processing
{
    ProcessingServiceImpl::ProcessingServiceImpl( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
                                                  size_t                                           maximalNodesCount,
                                                  std::shared_ptr<SubTaskEnqueuer>                 subTaskEnqueuer,
                                                  std::shared_ptr<SubTaskStateStorage>             subTaskStateStorage,
                                                  std::shared_ptr<SubTaskResultStorage>            subTaskResultStorage,
                                                  std::shared_ptr<ProcessingCore>                  processingCore ) :
        m_gossipPubSub( gossipPubSub ),                                 //
        m_context( gossipPubSub->GetAsioContext() ),                    //
        m_maximalNodesCount( maximalNodesCount ),                       //
        m_subTaskEnqueuer( subTaskEnqueuer ),                           //
        m_subTaskStateStorage( subTaskStateStorage ),                   //
        m_subTaskResultStorage( subTaskResultStorage ),                 //
        m_processingCore( processingCore ),                             //
        m_timerChannelListRequestTimeout( *m_context.get() ),           //
        m_channelListRequestTimeout( boost::posix_time::seconds( 5 ) ), //
        m_isStopped( true )                                             //
    {
    }

    void ProcessingServiceImpl::StartProcessing( const std::string &processingGridChannelId )
    {
        if ( !m_isStopped )
        {
            m_logger->debug( "[SERVICE_WAS_PREVIOUSLY_STARTED]" );
            return;
        }

        m_isStopped = false;

        Listen( processingGridChannelId );
        SendChannelListRequest();
        m_logger->debug( "[SERVICE_STARTED]" );
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

        m_logger->debug( "[SERVICE_STOPPED]" );
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
        if ( message )
        {
            SGProcessing::GridChannelMessage gridMessage;
            if ( gridMessage.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
            {
                if ( gridMessage.has_processing_channel_response() )
                {
                    auto response = gridMessage.processing_channel_response();

                    m_logger->debug( "Processing channel received. id:{}", response.channel_id() );

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
        m_logger->debug( "SUBTASK_QUEUE_PROCESSING_COMPLETED: {}", subTaskQueueId );

        {
            std::scoped_lock lock( m_mutexNodes );
            m_processingNodes.erase( subTaskQueueId );
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
        m_logger->error( "PROCESSING_ERROR reason: {}", errorMessage );

        {
            std::scoped_lock lock( m_mutexNodes );
            m_processingNodes.erase( subTaskQueueId );
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

        std::scoped_lock lock( m_mutexNodes );
        if ( m_processingNodes.size() < m_maximalNodesCount )
        {
            auto node = std::make_shared<ProcessingNode>( m_gossipPubSub, m_subTaskStateStorage, m_subTaskResultStorage,
                                                          m_processingCore,
                                                          std::bind( &ProcessingServiceImpl::OnQueueProcessingCompleted,
                                                                     this, processingQueuelId, std::placeholders::_1 ),
                                                          std::bind( &ProcessingServiceImpl::OnProcessingError, this,
                                                                     processingQueuelId, std::placeholders::_1 ) );

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
        std::scoped_lock lock( m_mutexNodes );
        for ( auto itNode = m_processingNodes.begin(); itNode != m_processingNodes.end(); ++itNode )
        {
            // Only channel host answers to reduce a number of published messages
            if ( itNode->second->HasQueueOwnership() )
            {
                SGProcessing::GridChannelMessage gridMessage;
                auto                             channelResponse = gridMessage.mutable_processing_channel_response();
                channelResponse->set_channel_id( itNode->first );

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

        std::scoped_lock lock( m_mutexNodes );
        while ( m_processingNodes.size() < m_maximalNodesCount )
        {
            std::string                      subTaskQueueId;
            std::list<SGProcessing::SubTask> subTasks;
            if ( m_subTaskEnqueuer->EnqueueSubTasks( subTaskQueueId, subTasks ) )
            {
                auto node = std::make_shared<ProcessingNode>(
                    m_gossipPubSub, m_subTaskStateStorage, m_subTaskResultStorage, m_processingCore,
                    std::bind( &ProcessingServiceImpl::OnQueueProcessingCompleted, this, subTaskQueueId,
                               std::placeholders::_1 ),
                    std::bind( &ProcessingServiceImpl::OnProcessingError, this, subTaskQueueId,
                               std::placeholders::_1 ) );

                // @todo Figure out if the task is still available for other peers
                // @todo Check if it is better to call EnqueueSubTasks within host
                // and return subTaskQueueId from processing host?
                node->CreateProcessingHost( subTaskQueueId, subTasks );

                m_processingNodes[subTaskQueueId] = node;
                m_logger->debug( "New processing channel created. {}", subTaskQueueId );
            }
            else
            {
                // If no tasks enquued, try to get available slots in existent queues
                SendChannelListRequest();
                break;
            }
        }
    }
}
