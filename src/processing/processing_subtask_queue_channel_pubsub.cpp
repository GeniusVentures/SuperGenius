#include "processing_subtask_queue_channel_pubsub.hpp"
#include <base/util.hpp>
#include "base/sgns_version.hpp"

namespace sgns::processing
{
    ProcessingSubTaskQueueChannelPubSub::ProcessingSubTaskQueueChannelPubSub(
        std::shared_ptr<GossipPubSub> gossipPubSub,
        const std::string            &processingQueueChannelId ) :
        m_gossipPubSub( std::move( gossipPubSub ) )
    {
        auto processing_queue_topic = processingQueueChannelId + sgns::version::GetNetAndVersionAppendix();
        m_processingQueueChannel    = std::make_shared<GossipPubSubTopic>( m_gossipPubSub, processing_queue_topic );
    }

    ProcessingSubTaskQueueChannelPubSub::~ProcessingSubTaskQueueChannelPubSub()
    {
        m_logger->debug( "[RELEASED] this: {}", reinterpret_cast<size_t>( this ) );
    }

    outcome::result<
        std::variant<std::chrono::milliseconds, std::shared_future<std::shared_ptr<GossipPubSubTopic::Subscription>>>>
    ProcessingSubTaskQueueChannelPubSub::Listen( std::chrono::milliseconds msSubscriptionWaitingDuration )
    {
        // Subscribe to the processing queue channel
        auto subscription_future = m_processingQueueChannel->Subscribe(
            [weakSelf = weak_from_this()]( boost::optional<const GossipPubSub::Message &> message )
            {
                if ( auto self = weakSelf.lock() )
                {
                    self->OnProcessingChannelMessage( message );
                }
            },
            msSubscriptionWaitingDuration.count() == 0 // If waiting duration is 0, subscribe now
        );

        if ( msSubscriptionWaitingDuration.count() > 0 )
        {
            // If a waiting duration is provided, wait for the subscription to complete
            std::chrono::milliseconds resultTime;
            bool                      success = waitForCondition(
                [&subscription_future]()
                { return subscription_future.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready; },
                msSubscriptionWaitingDuration,
                &resultTime );

            if ( success )
            {
                m_logger->debug( "Subscription established after {} ms", resultTime.count() );
                // Fixed: Use consistent type (GossipPubSubTopic::Subscription)
                return std::variant<std::chrono::milliseconds,
                                    std::shared_future<std::shared_ptr<GossipPubSubTopic::Subscription>>>( resultTime );
            }
            m_logger->error( "Subscription not established within the specified time ({} ms)",
                             msSubscriptionWaitingDuration.count() );
            return outcome::failure( boost::system::errc::timed_out );
        }

        // If no waiting requested, return the future
        // Fixed: Use std::move for efficiency (though not strictly required for shared_future)
        return std::variant<std::chrono::milliseconds,
                            std::shared_future<std::shared_ptr<GossipPubSubTopic::Subscription>>>(
            std::move( subscription_future ) );
    }

    void ProcessingSubTaskQueueChannelPubSub::RequestQueueOwnership( const std::string &nodeId )
    {
        // Send a request to grab a subtask queue
        SGProcessing::ProcessingChannelMessage message;
        message.mutable_subtask_queue_request()->set_node_id( nodeId );
        m_processingQueueChannel->Publish( message.SerializeAsString() );
    }

    void ProcessingSubTaskQueueChannelPubSub::PublishQueue( std::shared_ptr<SGProcessing::SubTaskQueue> queue )
    {
        SGProcessing::ProcessingChannelMessage message;
        message.set_allocated_subtask_queue( queue.get() );
        m_processingQueueChannel->Publish( message.SerializeAsString() );
        message.release_subtask_queue();
    }

    void ProcessingSubTaskQueueChannelPubSub::SetQueueRequestSink( QueueRequestSink queueRequestSink )
    {
        m_queueRequestSink = std::move( queueRequestSink );
    }

    void ProcessingSubTaskQueueChannelPubSub::SetQueueUpdateSink( QueueUpdateSink queueUpdateSink )
    {
        m_queueUpdateSink = std::move( queueUpdateSink );
    }

    void ProcessingSubTaskQueueChannelPubSub::SetMembershipFilter( sgns::networkregistry::MembershipFilter filter )
    {
        std::lock_guard<std::mutex> guard( m_mutexMembershipFilter );
        m_membershipFilter = std::move( filter );
    }

    void ProcessingSubTaskQueueChannelPubSub::OnProcessingChannelMessage(
        boost::optional<const GossipPubSub::Message &> message )
    {
        // Membership gate (15-13): drop messages whose transport sender is not an
        // authorized member BEFORE any queue ownership/sync handling. Empty filter =
        // public pass-through; empty/malformed `from` is denied under a set filter
        // (fail-closed).
        if ( message )
        {
            sgns::networkregistry::MembershipFilter membershipFilter;
            {
                std::lock_guard<std::mutex> guard( m_mutexMembershipFilter );
                membershipFilter = m_membershipFilter;
            }
            if ( !sgns::networkregistry::AuthorizeGossipSender( membershipFilter, message->from ) )
            {
                m_logger->debug( "Processing queue channel message from unauthorized sender ignored" );
                return;
            }
        }

        if ( message )
        {
            SGProcessing::ProcessingChannelMessage channelMesssage;
            if ( channelMesssage.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
            {
                if ( channelMesssage.has_subtask_queue_request() )
                {
                    HandleSubTaskQueueRequest( channelMesssage );
                }
                else if ( channelMesssage.has_subtask_queue() )
                {
                    HandleSubTaskQueue( channelMesssage );
                }
            }
        }
    }

    void ProcessingSubTaskQueueChannelPubSub::HandleSubTaskQueueRequest(
        SGProcessing::ProcessingChannelMessage &channelMesssage )
    {
        if ( m_queueRequestSink )
        {
            auto message = channelMesssage.subtask_queue_request();
            if ( !m_queueRequestSink( message ) )
            {
                m_logger->debug( "Queue request is pending for node {}", message.node_id() );
            }
            else
            {
                m_logger->debug( "Queue request was immediately fulfilled for node {}", message.node_id() );
            }
        }
    }

    void ProcessingSubTaskQueueChannelPubSub::HandleSubTaskQueue(
        SGProcessing::ProcessingChannelMessage &channelMesssage )
    {
        auto message = channelMesssage.release_subtask_queue();
        if ( m_queueUpdateSink )
        {
            auto queueChanged = m_queueUpdateSink( message );
            m_logger->debug( "Queue changed = {} during release for node", queueChanged );
        }
    }

    size_t ProcessingSubTaskQueueChannelPubSub::GetActiveNodesCount() const
    {
        // include ourselves
        return m_processingQueueChannel->getPeerCount() + 1;
    }

    std::vector<libp2p::peer::PeerId> ProcessingSubTaskQueueChannelPubSub::GetActiveNodes() const
    {
        return m_processingQueueChannel->getAllPeers();
    }

}
