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
        node_address_( m_gossipPubSub->GetLocalAddress() ),
        m_nodeCreationTimer( *m_context ),
        m_nodeCreationTimeout( boost::posix_time::milliseconds( 1000 ) )

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
        node_address_( std::move( node_address ) ),
        m_nodeCreationTimer( *m_context ),
        m_nodeCreationTimeout( boost::posix_time::milliseconds( 1000 ) )
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
        m_gridChannel->Subscribe(
            [weakSelf = weak_from_this()]( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message )
            {
                if ( auto self = weakSelf.lock() ) // Check if object still exists
                {
                    self->OnMessage( message );
                }
            } );
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
        m_timerChannelListRequestTimeout.async_wait(
            [instance = shared_from_this()]( const boost::system::error_code & )
            { instance->HandleRequestTimeout(); } );
    }

    void ProcessingServiceImpl::OnMessage( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message )
    {
        m_logger->trace( "[{}] On Message.", node_address_ );
        if ( message )
        {
            m_logger->trace( "[{}] Valid message.", node_address_ );
            SGProcessing::GridChannelMessage gridMessage;
            if ( gridMessage.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
            {
                if ( gridMessage.has_processing_channel_response() )
                {
                    auto response = gridMessage.processing_channel_response();
                    m_logger->trace( "[{}] Processing channel received. id:{}", node_address_, response.channel_id() );
                    AcceptProcessingChannel( response.channel_id() );
                }
                else if ( gridMessage.has_processing_channel_request() )
                {
                    m_logger->trace( "[{}] PUBLISH.", node_address_ );
                    PublishLocalChannelList();
                }
                else if ( gridMessage.has_node_creation_intent() )
                {
                    // Handle intent from another peer
                    auto intent = gridMessage.node_creation_intent();
                    OnNodeCreationIntent( intent.peer_address(), intent.subtask_queue_id() );
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

        m_logger->debug( "[{}] AcceptProcessingChannel for queue {}", node_address_, processingQueuelId );

        // Check if we're currently in the process of creating any node
        {
            std::lock_guard<std::mutex> lockCreation( m_mutexPendingCreation );

            // Check if our pending creation is stale
            if ( !m_pendingSubTaskQueueId.empty() && IsPendingCreationStale() )
            {
                m_logger->debug( "[{}] Clearing stale pending creation for queue {}",
                                 node_address_,
                                 m_pendingSubTaskQueueId );
                m_pendingSubTaskQueueId.clear();
                m_pendingSubTasks.clear();
                m_pendingTask.reset();
                m_competingPeers.clear();
            }

            // If we still have a pending creation, don't accept this channel
            if ( !m_pendingSubTaskQueueId.empty() )
            {
                m_logger->debug( "[{}] Not accepting channel {} as we're negotiating for queue {}",
                                 node_address_,
                                 processingQueuelId,
                                 m_pendingSubTaskQueueId );
                return;
            }

            // If this is the queue we were just negotiating for (and lost), wait a bit
            // This helps prevent race conditions where we immediately try to join a queue
            // that another peer is just in the process of creating
            // In practice, this is rare since the winning peer will have already created the node
            if ( m_pendingSubTaskQueueId == processingQueuelId )
            {
                m_logger->debug( "[{}] Not accepting channel {} as we just lost negotiation for it",
                                 node_address_,
                                 processingQueuelId );
                return;
            }
        }

        // Also check if we already have this queue
        std::scoped_lock lock( m_mutexNodes );
        if ( m_processingNodes.find( processingQueuelId ) != m_processingNodes.end() )
        {
            m_logger->debug( "[{}] Not accepting channel {} as we already have a node for it",
                             node_address_,
                             processingQueuelId );
            return;
        }

        m_logger->debug( "[{}] Number of nodes: {}, Max nodes: {}",
                         node_address_,
                         m_processingNodes.size(),
                         m_maximalNodesCount );

        if ( m_processingNodes.size() < m_maximalNodesCount )
        {
            m_logger->debug( "[{}] Accept Channel: Creating Node for queue {}", node_address_, processingQueuelId );

            auto node = ProcessingNode::New(
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
                processingQueuelId );
            if ( node != nullptr )
            {
                m_processingNodes[processingQueuelId] = node;
            }
        }

        if ( m_processingNodes.size() == m_maximalNodesCount )
        {
            m_timerChannelListRequestTimeout.expires_at( boost::posix_time::pos_infin );
        }
    }

    void ProcessingServiceImpl::PublishLocalChannelList()
    {
        m_logger->trace( "[{}] Publishing local channels", node_address_ );
        std::scoped_lock lock( m_mutexNodes );
        for ( auto &itNode : m_processingNodes )
        {
            m_logger->trace( "[{}] Channel {}: Owns Channel? {}",
                             node_address_,
                             itNode.first,
                             itNode.second->HasQueueOwnership() );

            // Only channel host answers to reduce a number of published messages
            if ( itNode.second->HasQueueOwnership() )
            {
                SGProcessing::GridChannelMessage gridMessage;
                auto                             channelResponse = gridMessage.mutable_processing_channel_response();
                channelResponse->set_channel_id( itNode.first );

                m_gridChannel->Publish( gridMessage.SerializeAsString() );
                m_logger->trace( "[{}] Channel published: {}", node_address_, channelResponse->channel_id() );
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

        // Check if we're already waiting for a node creation to resolve
        {
            std::lock_guard<std::mutex> lockCreation( m_mutexPendingCreation );

            // Check if our pending creation is stale and should be cleared
            if ( !m_pendingSubTaskQueueId.empty() )
            {
                if ( IsPendingCreationStale() )
                {
                    m_logger->debug( "[{}] Clearing stale pending creation for queue {}",
                                     node_address_,
                                     m_pendingSubTaskQueueId );
                    m_pendingSubTaskQueueId.clear();
                    m_pendingSubTasks.clear();
                    m_pendingTask.reset();
                    m_competingPeers.clear();
                }
                else
                {
                    m_logger->debug( "[{}] Already waiting for node creation to resolve for queue {}",
                                     node_address_,
                                     m_pendingSubTaskQueueId );
                    return;
                }
            }
        }
        m_logger->trace( "[{}] [Trying to create node]", node_address_ );

        // Check if we are at max capacity
        {
            std::scoped_lock lock( m_mutexNodes );
            if ( m_processingNodes.size() >= m_maximalNodesCount )
            {
                m_logger->debug( "[{}] At maximum node capacity ({}) - not attempting to grab tasks",
                                 node_address_,
                                 m_maximalNodesCount );
                return;
            }
        }
        std::string                      subTaskQueueId;
        std::list<SGProcessing::SubTask> subTasks;
        auto                             maybe_task = m_subTaskEnqueuer->EnqueueSubTasks( subTaskQueueId, subTasks );

        if ( maybe_task )
        {
            // Mark ourselves as busy with this potential node creation
            {
                std::scoped_lock lock( m_mutexNodes, m_mutexPendingCreation );

                // Double-check we're still under the limit
                if ( m_processingNodes.size() >= m_maximalNodesCount )
                {
                    m_logger->debug( "[{}] Maximum nodes reached while grabbing task - abandoning", node_address_ );
                    return;
                }

                m_pendingSubTaskQueueId = subTaskQueueId;
                m_pendingSubTasks       = subTasks;
                m_pendingTask           = maybe_task.value();
            }

            // Instead of immediately creating a ProcessingNode, we'll broadcast our intent
            // and wait for responses from other peers
            m_logger->debug( "[{}] Grabbed task, broadcasting intent to create node for queue {}",
                             node_address_,
                             subTaskQueueId );

            BroadcastNodeCreationIntent( subTaskQueueId );
        }
        else
        {
            m_logger->trace( "[{}] No tasks available, requesting channel list", node_address_ );
            SendChannelListRequest();
        }
    }

    void ProcessingServiceImpl::BroadcastNodeCreationIntent( const std::string &subTaskQueueId )
    {
        SGProcessing::GridChannelMessage gridMessage;
        auto                             intent = gridMessage.mutable_node_creation_intent();
        intent->set_peer_address( node_address_ );
        intent->set_subtask_queue_id( subTaskQueueId );

        // Add ourselves to competing peers
        {
            std::lock_guard<std::mutex> lockCreation( m_mutexPendingCreation );
            m_competingPeers.insert( node_address_ );
            m_pendingCreationTimestamp = std::chrono::steady_clock::now();
        }

        m_gridChannel->Publish( gridMessage.SerializeAsString() );
        m_logger->debug( "[{}] Broadcasting intent to create node for queue {}", node_address_, subTaskQueueId );

        // Set timer to wait for other peers' responses
        m_nodeCreationTimer.expires_from_now( m_nodeCreationTimeout );
        m_nodeCreationTimer.async_wait(
            [instance = shared_from_this()]( const boost::system::error_code &error )
            {
                if ( !error )
                { // Only proceed if not cancelled
                    instance->HandleNodeCreationTimeout();
                }
            } );
    }

    void ProcessingServiceImpl::OnNodeCreationIntent( const std::string &peerAddress,
                                                      const std::string &subTaskQueueId )
    {
        if ( peerAddress == node_address_ )
        {
            // Ignore our own message
            return;
        }

        m_logger->debug( "[{}] Received node creation intent from {} for queue {}",
                         node_address_,
                         peerAddress,
                         subTaskQueueId );

        bool        shouldCancel = false;
        std::string lowestPeer;

        {
            std::lock_guard<std::mutex> lockCreation( m_mutexPendingCreation );

            // Only process if this is for our pending queue
            if ( m_pendingSubTaskQueueId == subTaskQueueId )
            {
                m_competingPeers.insert( peerAddress );
                m_pendingCreationTimestamp = std::chrono::steady_clock::now(); // Reset timeout

                if ( !HasLowestAddress() )
                {
                    shouldCancel = true;
                    lowestPeer   = *m_competingPeers.begin();
                }
            }
        }

        if ( shouldCancel )
        {
            // Cancel our timer (do this outside the lock to avoid potential deadlocks)
            m_nodeCreationTimer.cancel();

            std::string reason = "peer " + lowestPeer + " has lower address";
            CancelPendingCreation( reason );
        }
    }

    void ProcessingServiceImpl::CancelPendingCreation( const std::string &reason )
    {
        std::lock_guard<std::mutex> lockCreation( m_mutexPendingCreation );

        if ( !m_pendingSubTaskQueueId.empty() )
        {
            m_logger->debug( "[{}] Cancelling node creation for queue {} because {}",
                             node_address_,
                             m_pendingSubTaskQueueId,
                             reason );

            m_pendingSubTaskQueueId.clear();
            m_pendingSubTasks.clear();
            m_pendingTask.reset();
            m_competingPeers.clear();
        }
    }

    bool ProcessingServiceImpl::HasLowestAddress() const
    {
        if ( m_competingPeers.empty() )
        {
            return true;
        }

        return *m_competingPeers.begin() == node_address_;
    }

    void ProcessingServiceImpl::HandleNodeCreationTimeout()
    {
        std::string                         subTaskQueueId;
        std::list<SGProcessing::SubTask>    subTasks;
        boost::optional<SGProcessing::Task> task;
        bool                                shouldCreate = false;

        {
            std::lock_guard<std::mutex> lockCreation( m_mutexPendingCreation );

            if ( m_pendingSubTaskQueueId.empty() )
            {
                // Creation was already cancelled
                m_logger->debug( "[{}] Node creation attempt was already cancelled", node_address_ );
                return;
            }

            subTaskQueueId = m_pendingSubTaskQueueId;
            subTasks       = m_pendingSubTasks;
            task           = m_pendingTask;

            // Check if we still have the lowest address
            if ( !HasLowestAddress() )
            {
                auto lowestPeer = *m_competingPeers.begin();
                m_logger->debug( "[{}] Not creating node for queue {} as peer {} has lower address",
                                 node_address_,
                                 subTaskQueueId,
                                 lowestPeer );

                // Clear pending data
                m_pendingSubTaskQueueId.clear();
                m_pendingSubTasks.clear();
                m_pendingTask.reset();
                m_competingPeers.clear();
                return;
            }

            // Clear pending data since we're going to use it now
            m_pendingSubTaskQueueId.clear();
            m_pendingSubTasks.clear();
            m_pendingTask.reset();
            m_competingPeers.clear();
            shouldCreate = true;
        }

        if ( shouldCreate )
        {
            m_logger->debug( "[{}] Timeout elapsed, creating node for queue {} as we have lowest address",
                             node_address_,
                             subTaskQueueId );

            // Check if we can still add more nodes
            std::unique_lock<std::recursive_mutex> lock( m_mutexNodes );

            // Check if we already have this node (could have been created passively)
            if ( m_processingNodes.find( subTaskQueueId ) != m_processingNodes.end() )
            {
                m_logger->debug( "[{}] Not creating node for queue {} as it already exists",
                                 node_address_,
                                 subTaskQueueId );
                return;
            }

            if ( m_processingNodes.size() >= m_maximalNodesCount )
            {
                m_logger->debug( "[{}] Cannot create node for queue {} as maximum nodes limit reached",
                                 node_address_,
                                 subTaskQueueId );
                return;
            }

            // Create the ProcessingNode
            auto node = ProcessingNode::New(
                m_gossipPubSub,
                m_subTaskStateStorage,
                m_subTaskResultStorage,
                m_processingCore,
                std::bind( &ProcessingServiceImpl::OnQueueProcessingCompleted,
                           this,
                           subTaskQueueId,
                           std::placeholders::_1 ),
                std::bind( &ProcessingServiceImpl::OnProcessingError, this, subTaskQueueId, std::placeholders::_1 ),
                node_address_,
                subTaskQueueId,
                subTasks );
            if ( node != nullptr )
            {
                m_processingNodes[subTaskQueueId] = node;
            }

            lock.unlock(); // Release the mutex before potentially long operations

            m_logger->debug( "[{}] New processing channel created: {}", node_address_, subTaskQueueId );

            // Notify other peers that this channel is now available
            PublishLocalChannelList();

            // Send a new channel list request to continue processing
            SendChannelListRequest();
        }
    }

    bool ProcessingServiceImpl::IsPendingCreationStale() const
    {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>( now - m_pendingCreationTimestamp );
        return elapsed > m_pendingCreationTimeout;
    }

}
