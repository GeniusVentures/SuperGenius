#include "processing_service.hpp"
#include "base/sgns_version.hpp"
#include "base/gossip_auth.hpp"
#include <utility>
#include <thread>

namespace sgns::processing
{
    ProcessingServiceImpl::ProcessingServiceImpl(
        std::shared_ptr<ipfs_pubsub::GossipPubSub> gossipPubSub,
        size_t                                     maximalNodesCount,
        std::shared_ptr<SubTaskEnqueuer>           subTaskEnqueuer,
        std::shared_ptr<SubTaskResultStorage>      subTaskResultStorage,
        std::shared_ptr<ProcessingCore>            processingCore,
        std::function<void( const std::string &subTaskQueueId, const SGProcessing::TaskResult &taskresult )>
                                                                 userCallbackSuccess,
        std::function<void( const std::string &subTaskQueueId )> userCallbackError,
        std::string                                              node_address ) :
        m_gossipPubSub( std::move( gossipPubSub ) ),
        m_context( std::make_shared<boost::asio::io_context>() ),
        m_maximalNodesCount( maximalNodesCount ),
        m_subTaskEnqueuer( std::move( subTaskEnqueuer ) ),
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

    ProcessingServiceImpl::~ProcessingServiceImpl()
    {
        m_logger->debug( "~ProcessingServiceImpl CALLED" );
        StopProcessing();
    }

    void ProcessingServiceImpl::StartProcessing( const std::string &processingGridChannelId )
    {
        if ( !m_isStopped )
        {
            m_logger->debug( "[{}] [SERVICE_WAS_PREVIOUSLY_STARTED]", node_address_ );
            return;
        }

        m_isStopped = false;

        // Reset the io_context and create the work object
        m_context->reset();
        m_context_work = std::make_unique<boost::asio::io_context::work>( *m_context );

        io_thread = std::thread( [this] { m_context->run(); } );

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

        if ( m_gridChannel )
        {
            m_gridChannel->Unsubscribe();
        }

        // Cancel timers before stopping context!
        boost::system::error_code ec;
        m_timerChannelListRequestTimeout.cancel( ec );
        m_nodeCreationTimer.cancel( ec );

        m_context_work.reset();

        m_context->stop();

        if ( io_thread.joinable() )
        {
            io_thread.join();
        }

        {
            std::scoped_lock lock( m_mutexNodes );
            m_processingNodes.clear();
        }

        m_logger->debug( "[{}] [SERVICE_STOPPED]", node_address_ );
    }

    void ProcessingServiceImpl::setMirrorResultCallback( std::function<void( const std::string & )> callback )
    {
        m_mirrorResultCallback = std::move( callback );
        // Apply to all existing nodes
        std::scoped_lock lock( m_mutexNodes );
        for ( auto &[id, node] : m_processingNodes )
        {
            if ( node && m_mirrorResultCallback )
            {
                node->setMirrorResultCallback( m_mirrorResultCallback );
            }
        }
    }

    void ProcessingServiceImpl::setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap )
    {
        m_bitswap = std::move( bitswap );
        // Apply to all existing nodes
        std::scoped_lock lock( m_mutexNodes );
        for ( auto &[id, node] : m_processingNodes )
        {
            if ( node && m_bitswap )
            {
                node->setBitswap( m_bitswap );
            }
        }
    }

    void ProcessingServiceImpl::SetMembershipFilter( sgns::networkregistry::MembershipFilter filter )
    {
        {
            std::scoped_lock lockFilter( m_membershipFilterMutex );
            m_membershipFilter = filter;
        }
        // Apply to all existing nodes (mirrors setBitswap); nodes created later
        // receive the filter INSIDE ProcessingNode::New (pre-subscription
        // install at both creation sites), so there is no enrollment window.
        std::scoped_lock lock( m_mutexNodes );
        for ( auto &[id, node] : m_processingNodes )
        {
            if ( node && filter )
            {
                node->SetMembershipFilter( filter );
            }
        }
    }

    void ProcessingServiceImpl::SetGossipSigningKey( std::shared_ptr<const libp2p::crypto::KeyPair> key )
    {
        // Store the key, then propagate a copy to all existing nodes symmetric
        // with the filter (CR-G01); nodes created later receive the key INSIDE
        // ProcessingNode::New (pre-subscription install at both creation
        // sites). The filter mutex is released before m_mutexNodes
        // is acquired (same ordering discipline as SetMembershipFilter).
        std::shared_ptr<const libp2p::crypto::KeyPair> key_copy = key;
        {
            std::scoped_lock lockFilter( m_membershipFilterMutex );
            m_gossipSigningKey = std::move( key );
        }
        std::scoped_lock lock( m_mutexNodes );
        for ( auto &[id, node] : m_processingNodes )
        {
            if ( node && key_copy )
            {
                node->SetGossipSigningKey( key_copy );
            }
        }
    }

    void ProcessingServiceImpl::Listen( const std::string &processingGridChannelId )
    {
        using GossipPubSubTopic = ipfs_pubsub::GossipPubSubTopic;
        auto processing_topic   = processingGridChannelId + version::GetNetAndVersionAppendix();
        m_gridChannel           = std::make_unique<GossipPubSubTopic>( m_gossipPubSub, processing_topic );
        m_gridChannel->Subscribe(
            [weakSelf = weak_from_this()]( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
            {
                if ( auto self = weakSelf.lock() ) // Check if object still exists
                {
                    self->OnMessage( message );
                }
            } );
    }

    void ProcessingServiceImpl::SendChannelListRequest()
    {
        if ( m_waitingChannelRequest )
        {
            return;
        }
        m_waitingChannelRequest = true;
        SGProcessing::GridChannelMessage gridMessage;
        auto                             channelRequest = gridMessage.mutable_processing_channel_request();
        channelRequest->set_environment( "any" );

        // Private-network publish sealing (CR-G01): seal under a set filter
        // with the gossip host keypair; fail closed when a filter is set but
        // no key is wired. No filter -> raw publish, byte-identical.
        {
            const std::string raw_payload = gridMessage.SerializeAsString();
            sgns::networkregistry::MembershipFilter membershipFilter;
            std::shared_ptr<const libp2p::crypto::KeyPair> signingKey;
            {
                std::scoped_lock lockFilter( m_membershipFilterMutex );
                membershipFilter = m_membershipFilter;
                signingKey       = m_gossipSigningKey;
            }
            if ( !membershipFilter )
            {
                m_gridChannel->Publish( raw_payload );
            }
            else if ( !signingKey )
            {
                m_logger->error( "[{}] Grid channel publish FAILED CLOSED: membership filter set but no "
                                 "gossip signing key wired",
                                 node_address_ );
            }
            else
            {
                auto from_bytes = sgns::base::DeriveGossipFromBytes( *signingKey );
                if ( from_bytes.has_error() )
                {
                    m_logger->error( "[{}] Grid channel publish FAILED CLOSED: cannot derive from-bytes "
                                     "from the gossip signing key",
                                     node_address_ );
                }
                else
                {
                    auto sealed = sgns::base::SealGossipPayload( *signingKey, from_bytes.value(), sgns::base::detail::StringSpan( raw_payload ) );
                    if ( sealed.has_error() )
                    {
                        m_logger->error( "[{}] Grid channel publish FAILED CLOSED: sealing failed ({})",
                                         node_address_,
                                         static_cast<int>( sealed.error() ) );
                    }
                    else
                    {
                        m_gridChannel->Publish( sealed.value() );
                    }
                }
            }
        }
        m_logger->debug( "List of processing channels requested" );
        m_timerChannelListRequestTimeout.expires_from_now( m_channelListRequestTimeout );
        m_timerChannelListRequestTimeout.async_wait(
            [instance = weak_from_this()]( const boost::system::error_code & )
            {
                if ( auto strong = instance.lock() )
                {
                    strong->HandleRequestTimeout();
                }
            } );
    }

    void ProcessingServiceImpl::OnMessage( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
    {
        m_logger->trace( "[{}] On Message.", node_address_ );
        if ( !message )
        {
            m_logger->trace( "[{}] Invalid message.", node_address_ );
            return;
        }

        // Membership gate (15-13) + payload authentication (15-14, CR-G01):
        // under a set membership filter the message is FIRST authenticated
        // (sgns::base::OpenGossipPayload: envelope present, embedded public
        // key derives the from-field PeerId, signature covers from+payload)
        // and only THEN authorized (AuthorizeGossipSender on the verified
        // identity) before ANY grid handling. Unsigned/unverifiable messages
        // are denied under a set filter even when `from` names a member --
        // a same-PSK forger cannot pass by writing a member id into the wire
        // fields. Empty filter = public pass-through (raw parse,
        // byte-identical); empty/malformed `from` fails OpenGossipPayload
        // itself (fail-closed, T-15-13-04).
        gsl::span<const uint8_t> grid_parse_source( message->data.data(), message->data.size() );
        {
            sgns::networkregistry::MembershipFilter membershipFilter;
            {
                std::scoped_lock lockFilter( m_membershipFilterMutex );
                membershipFilter = m_membershipFilter;
            }
            if ( membershipFilter )
            {
                auto opened = sgns::base::OpenGossipPayload(
                    gsl::span<const uint8_t>( message->from.data(), message->from.size() ),
                    gsl::span<const uint8_t>( message->data.data(), message->data.size() ) );
                if ( opened.has_error() )
                {
                    m_logger->debug( "[{}] Grid channel message failed payload authentication ({}) -- ignored",
                                     node_address_,
                                     static_cast<int>( opened.error() ) );
                    return;
                }
                grid_parse_source = opened.value().payload;
                if ( !sgns::networkregistry::AuthorizeGossipSender( membershipFilter, message->from ) )
                {
                    m_logger->debug( "[{}] Grid channel message from unauthorized sender ignored", node_address_ );
                    return;
                }
            }
        }

        m_logger->trace( "[{}] Valid message.", node_address_ );
        SGProcessing::GridChannelMessage gridMessage;
        if ( !gridMessage.ParseFromArray( grid_parse_source.data(),
                                          static_cast<int>( grid_parse_source.size() ) ) )
        {
            m_logger->error( "[{}] Could not deserialize message", node_address_ );
            return;
        }

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

    void ProcessingServiceImpl::OnQueueProcessingCompleted( const std::string              &subTaskQueueId,
                                                            const SGProcessing::TaskResult &taskResult )
    {
        m_logger->debug( "[{}] SUBTASK_QUEUE_PROCESSING_COMPLETED: {}", node_address_, subTaskQueueId );

        if ( userCallbackSuccess_ )
        {
            userCallbackSuccess_( subTaskQueueId, taskResult );
        }

        {
            std::scoped_lock lock( m_mutexNodes );
            m_processingNodes.erase( subTaskQueueId );
        }

        if ( !m_isStopped )
        {
            SendChannelListRequest();
        }
    }

    void ProcessingServiceImpl::OnProcessingError( const std::string &subTaskQueueId, const std::string &errorMessage )
    {
        m_logger->error( "[{:.8}] {} PROCESSING_ERROR reason: {} ID: {}",
                         node_address_,
                         __func__,
                         errorMessage,
                         subTaskQueueId );

        // Add this channel to blacklist to prevent repeated processing attempts
        {
            std::lock_guard lockBlacklist( m_mutexBlacklist );
            m_blacklistedChannels.insert( subTaskQueueId );
            m_logger->info( "[{:.8}] Blacklisted channel {} due to processing error (total blacklisted: {})",
                            node_address_,
                            subTaskQueueId,
                            m_blacklistedChannels.size() );
        }

        if ( userCallbackError_ )
        {
            userCallbackError_( subTaskQueueId );
        }
        if ( errorMessage.find( "timed out" ) == std::string::npos )
        {
            m_logger->error( "[{:.8}] {}: Marking task as bad {}", node_address_, __func__, subTaskQueueId );
            m_subTaskEnqueuer->MarkTaskBad( subTaskQueueId );
        }
        {
            std::scoped_lock lock( m_mutexNodes );
            m_processingNodes.erase( subTaskQueueId );
        }

        if ( !m_isStopped )
        {
            SendChannelListRequest();
        }
    }

    void ProcessingServiceImpl::OnProcessingDone( const std::string &taskId )
    {
        m_logger->debug( "[{}] PROCESSING_DONE: for task {}", node_address_, taskId );

        {
            std::scoped_lock lock( m_mutexNodes );
            m_processingNodes.erase( taskId );
        }

        if ( !m_isStopped )
        {
            SendChannelListRequest();
        }
    }

    void ProcessingServiceImpl::AcceptProcessingChannel( const std::string &channelId )
    {
        if ( m_isStopped )
        {
            return;
        }

        // Check if this channel is blacklisted
        {
            std::lock_guard lockBlacklist( m_mutexBlacklist );
            if ( m_blacklistedChannels.find( channelId ) != m_blacklistedChannels.end() )
            {
                m_logger->debug( "[{}] Not accepting blacklisted channel {}", node_address_, channelId );
                return;
            }
        }

        m_logger->debug( "[{}] AcceptProcessingChannel for queue {}", node_address_, channelId );

        // Check if we're currently in the process of creating any node
        {
            std::lock_guard lockCreation( m_mutexPendingCreation );

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
                                 channelId,
                                 m_pendingSubTaskQueueId );
                return;
            }

            // If this is the queue we were just negotiating for (and lost), wait a bit
            // This helps prevent race conditions where we immediately try to join a queue
            // that another peer is just in the process of creating
            // In practice, this is rare since the winning peer will have already created the node
            if ( m_pendingSubTaskQueueId == channelId )
            {
                m_logger->debug( "[{}] Not accepting channel {} as we just lost negotiation for it",
                                 node_address_,
                                 channelId );
                return;
            }
        }

        // Also check if we already have this queue
        std::scoped_lock lock( m_mutexNodes );
        if ( m_processingNodes.find( channelId ) != m_processingNodes.end() )
        {
            m_logger->debug( "[{}] Not accepting channel {} as we already have a node for it",
                             node_address_,
                             channelId );
            return;
        }

        m_logger->debug( "[{}] Number of nodes: {}, Max nodes: {}",
                         node_address_,
                         m_processingNodes.size(),
                         m_maximalNodesCount );

        if ( m_processingNodes.size() < m_maximalNodesCount )
        {
            m_logger->debug( "[{}] Accept Channel: Creating Node for queue {}", node_address_, channelId );

            // CR-G02a: snapshot the membership filter + gossip signing key
            // BEFORE creating the node and pass them INTO ProcessingNode::New,
            // which installs them on the queue channel before Listen() and on
            // the results accessor before CreateResultsChannel/
            // ConnectToSubTaskQueue -- no subscription goes live ungated.
            sgns::networkregistry::MembershipFilter membershipFilter;
            std::shared_ptr<const libp2p::crypto::KeyPair> signingKey;
            {
                std::scoped_lock lockFilter( m_membershipFilterMutex );
                membershipFilter = m_membershipFilter;
                signingKey       = m_gossipSigningKey;
            }

            auto weakSelf = weak_from_this();

            auto node = ProcessingNode::New(
                m_gossipPubSub,
                m_subTaskResultStorage,
                m_processingCore,
                [weakSelf, channelId]( const SGProcessing::TaskResult &result )
                {
                    if ( auto self = weakSelf.lock() )
                    {
                        self->OnQueueProcessingCompleted( channelId, result );
                    }
                },
                [weakSelf, channelId]( const std::string &error )
                {
                    if ( auto self = weakSelf.lock() )
                    {
                        self->OnProcessingError( channelId, error );
                    }
                },
                [weakSelf, channelId]()
                {
                    if ( auto self = weakSelf.lock() )
                    {
                        self->OnProcessingDone( channelId );
                    }
                },
                node_address_,
                channelId,
                /*subTasks=*/{},
                /*msSubscriptionWaitingDuration=*/std::chrono::milliseconds( 2000 ),
                /*ttl=*/std::chrono::minutes( 2 ),
                membershipFilter,
                signingKey );

            if ( node != nullptr )
            {
                m_processingNodes[channelId] = node;
                // Apply mirror callback to newly created node
                if ( m_mirrorResultCallback )
                {
                    node->setMirrorResultCallback( m_mirrorResultCallback );
                }
                // Apply bitswap to newly created node
                if ( m_bitswap )
                {
                    node->setBitswap( m_bitswap );
                }
                // Membership filter + gossip signing key were passed INTO
                // ProcessingNode::New above (pre-subscription install; the
                // set-time propagation in SetMembershipFilter/
                // SetGossipSigningKey refreshes EXISTING nodes).
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

                // Private-network publish sealing (CR-G01): seal under a set
                // filter; fail closed when a filter is set but no key is
                // wired. No filter -> raw publish, byte-identical.
                const std::string raw_payload = gridMessage.SerializeAsString();
                sgns::networkregistry::MembershipFilter membershipFilter;
                std::shared_ptr<const libp2p::crypto::KeyPair> signingKey;
                {
                    std::scoped_lock lockFilter( m_membershipFilterMutex );
                    membershipFilter = m_membershipFilter;
                    signingKey       = m_gossipSigningKey;
                }
                if ( !membershipFilter )
                {
                    m_gridChannel->Publish( raw_payload );
                }
                else if ( !signingKey )
                {
                    m_logger->error( "[{}] Grid channel publish FAILED CLOSED: membership filter set but no "
                                     "gossip signing key wired",
                                     node_address_ );
                }
                else
                {
                    auto from_bytes = sgns::base::DeriveGossipFromBytes( *signingKey );
                    if ( from_bytes.has_error() )
                    {
                        m_logger->error( "[{}] Grid channel publish FAILED CLOSED: cannot derive from-bytes "
                                         "from the gossip signing key",
                                         node_address_ );
                    }
                    else
                    {
                        auto sealed = sgns::base::SealGossipPayload( *signingKey, from_bytes.value(), sgns::base::detail::StringSpan( raw_payload ) );
                        if ( sealed.has_error() )
                        {
                            m_logger->error( "[{}] Grid channel publish FAILED CLOSED: sealing failed ({})",
                                             node_address_,
                                             static_cast<int>( sealed.error() ) );
                        }
                        else
                        {
                            m_gridChannel->Publish( sealed.value() );
                        }
                    }
                }
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

    ProcessingServiceImpl::ProcessingStatus ProcessingServiceImpl::GetProcessingStatus() const
    {
        if ( m_isStopped )
        {
            return { Status::DISABLED, 0.0f };
        }

        float  totalProgress = 0.0f;
        size_t nodeCount     = 0;

        {
            std::lock_guard lock( m_mutexNodes );
            if ( m_processingNodes.empty() )
            {
                return { Status::IDLE, 0.0f };
            }

            // Calculate average progress across all processing nodes
            for ( const auto &[queueId, node] : m_processingNodes )
            {
                if ( node )
                {
                    totalProgress += node->GetProgress();
                    ++nodeCount;
                }
            }
        }

        float averageProgress = ( nodeCount > 0 ) ? ( totalProgress / nodeCount ) : 0.0f;
        averageProgress = std::round( averageProgress * 100.0f ) / 100.0f;

        return { Status::PROCESSING, averageProgress };
    }

    void ProcessingServiceImpl::HandleRequestTimeout()
    {
        m_waitingChannelRequest = false;
        m_logger->debug( "QUEUE_REQUEST_TIMEOUT" );
        m_timerChannelListRequestTimeout.expires_at( boost::posix_time::pos_infin );

        if ( m_isStopped )
        {
            return;
        }

        // Check if we're already waiting for a node creation to resolve
        {
            std::lock_guard lockCreation( m_mutexPendingCreation );

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
            std::lock_guard lockCreation( m_mutexPendingCreation );
            m_competingPeers.insert( node_address_ );
            m_pendingCreationTimestamp = std::chrono::steady_clock::now();
        }

        // Private-network publish sealing (CR-G01): seal under a set filter;
        // fail closed when a filter is set but no key is wired. No filter ->
        // raw publish, byte-identical.
        {
            const std::string raw_payload = gridMessage.SerializeAsString();
            sgns::networkregistry::MembershipFilter membershipFilter;
            std::shared_ptr<const libp2p::crypto::KeyPair> signingKey;
            {
                std::scoped_lock lockFilter( m_membershipFilterMutex );
                membershipFilter = m_membershipFilter;
                signingKey       = m_gossipSigningKey;
            }
            if ( !membershipFilter )
            {
                m_gridChannel->Publish( raw_payload );
            }
            else if ( !signingKey )
            {
                m_logger->error( "[{}] Grid channel publish FAILED CLOSED: membership filter set but no "
                                 "gossip signing key wired",
                                 node_address_ );
            }
            else
            {
                auto from_bytes = sgns::base::DeriveGossipFromBytes( *signingKey );
                if ( from_bytes.has_error() )
                {
                    m_logger->error( "[{}] Grid channel publish FAILED CLOSED: cannot derive from-bytes "
                                     "from the gossip signing key",
                                     node_address_ );
                }
                else
                {
                    auto sealed = sgns::base::SealGossipPayload( *signingKey, from_bytes.value(), sgns::base::detail::StringSpan( raw_payload ) );
                    if ( sealed.has_error() )
                    {
                        m_logger->error( "[{}] Grid channel publish FAILED CLOSED: sealing failed ({})",
                                         node_address_,
                                         static_cast<int>( sealed.error() ) );
                    }
                    else
                    {
                        m_gridChannel->Publish( sealed.value() );
                    }
                }
            }
        }
        m_logger->debug( "[{}] Broadcasting intent to create node for queue {}", node_address_, subTaskQueueId );

        // Set timer to wait for other peers' responses
        m_nodeCreationTimer.expires_from_now( m_nodeCreationTimeout );
        m_nodeCreationTimer.async_wait(
            [instance = weak_from_this()]( const boost::system::error_code &error )
            {
                if ( !error )
                { // Only proceed if not canceled
                    if ( auto self = instance.lock() )
                    {
                        self->HandleNodeCreationTimeout();
                    }
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
            std::lock_guard lockCreation( m_mutexPendingCreation );

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
        std::lock_guard lockCreation( m_mutexPendingCreation );

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
        std::string                      subTaskQueueId;
        std::list<SGProcessing::SubTask> subTasks;

        {
            std::lock_guard lockCreation( m_mutexPendingCreation );

            if ( m_pendingSubTaskQueueId.empty() )
            {
                // Creation was already canceled
                m_logger->debug( "[{}] Node creation attempt was already cancelled", node_address_ );
                return;
            }

            subTaskQueueId = m_pendingSubTaskQueueId;
            subTasks       = m_pendingSubTasks;

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
        }

        m_logger->debug( "[{}] Timeout elapsed, creating node for queue {} as we have lowest address",
                         node_address_,
                         subTaskQueueId );

        // Check if we can still add more nodes
        std::unique_lock lock( m_mutexNodes );

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

        // CR-G02a: snapshot the membership filter + gossip signing key
        // BEFORE creating the node and pass them INTO ProcessingNode::New,
        // which installs them on the queue channel before Listen() and on
        // the results accessor before CreateResultsChannel/
        // ConnectToSubTaskQueue -- no subscription goes live ungated.
        // (Taken while m_mutexNodes is held; the filter mutex is never held
        // across m_mutexNodes -- same ordering discipline as the setters.)
        sgns::networkregistry::MembershipFilter membershipFilter;
        std::shared_ptr<const libp2p::crypto::KeyPair> signingKey;
        {
            std::scoped_lock lockFilter( m_membershipFilterMutex );
            membershipFilter = m_membershipFilter;
            signingKey       = m_gossipSigningKey;
        }

        // Create the ProcessingNode
        auto weakSelf = weak_from_this();

        auto node = ProcessingNode::New(
            m_gossipPubSub,
            m_subTaskResultStorage,
            m_processingCore,
            [weakSelf, subTaskQueueId]( const SGProcessing::TaskResult &result )
            {
                if ( auto self = weakSelf.lock() )
                {
                    self->OnQueueProcessingCompleted( subTaskQueueId, result );
                }
            },
            [weakSelf, subTaskQueueId]( const std::string &error )
            {
                if ( auto self = weakSelf.lock() )
                {
                    self->OnProcessingError( subTaskQueueId, error );
                }
            },
            [weakSelf, subTaskQueueId]()
            {
                if ( auto self = weakSelf.lock() )
                {
                    self->OnProcessingDone( subTaskQueueId );
                }
            },
            node_address_,
            subTaskQueueId,
            subTasks,
            /*msSubscriptionWaitingDuration=*/std::chrono::milliseconds( 2000 ),
            /*ttl=*/std::chrono::minutes( 2 ),
            membershipFilter,
            signingKey );

        if ( node != nullptr )
        {
            m_processingNodes[subTaskQueueId] = node;
            // Membership filter + gossip signing key were passed INTO
            // ProcessingNode::New above (pre-subscription install; the
            // set-time propagation in SetMembershipFilter/
            // SetGossipSigningKey refreshes EXISTING nodes).
        }

        lock.unlock(); // Release the mutex before potentially long operations

        m_logger->debug( "[{}] New processing channel created: {}", node_address_, subTaskQueueId );

        // Notify other peers that this channel is now available
        PublishLocalChannelList();

        // Send a new channel list request to continue processing
        SendChannelListRequest();
    }

    bool ProcessingServiceImpl::IsPendingCreationStale() const
    {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>( now - m_pendingCreationTimestamp );
        return elapsed > m_pendingCreationTimeout;
    }
}
