#include "processing_subtask_queue_channel_pubsub.hpp"
#include <base/util.hpp>
#include "base/gossip_auth.hpp"
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

        // Private-network publish sealing (CR-G01): seal under a set filter
        // with the gossip host keypair; fail closed when a filter is set but
        // no key is wired. No filter -> raw publish, byte-identical.
        const std::string raw_payload = message.SerializeAsString();
        sgns::networkregistry::MembershipFilter membershipFilter;
        std::shared_ptr<const libp2p::crypto::KeyPair> signingKey;
        {
            std::lock_guard<std::mutex> guard( m_mutexMembershipFilter );
            membershipFilter = m_membershipFilter;
            signingKey       = m_gossipSigningKey;
        }
        if ( !membershipFilter )
        {
            m_processingQueueChannel->Publish( raw_payload );
        }
        else if ( !signingKey )
        {
            m_logger->error( "Queue channel publish FAILED CLOSED: membership filter set but no "
                             "gossip signing key wired" );
        }
        else
        {
            auto from_bytes = sgns::base::DeriveGossipFromBytes( *signingKey );
            if ( from_bytes.has_error() )
            {
                m_logger->error( "Queue channel publish FAILED CLOSED: cannot derive from-bytes "
                                 "from the gossip signing key" );
            }
            else
            {
                auto sealed = sgns::base::SealGossipPayload( *signingKey, from_bytes.value(), sgns::base::detail::StringSpan( raw_payload ) );
                if ( sealed.has_error() )
                {
                    m_logger->error( "Queue channel publish FAILED CLOSED: sealing failed ({})",
                                     static_cast<int>( sealed.error() ) );
                }
                else
                {
                    m_processingQueueChannel->Publish( sealed.value() );
                }
            }
        }
    }

    void ProcessingSubTaskQueueChannelPubSub::PublishQueue( std::shared_ptr<SGProcessing::SubTaskQueue> queue )
    {
        SGProcessing::ProcessingChannelMessage message;
        message.set_allocated_subtask_queue( queue.get() );

        // Private-network publish sealing (CR-G01): seal under a set filter;
        // fail closed when a filter is set but no key is wired. No filter ->
        // raw publish, byte-identical.
        const std::string raw_payload = message.SerializeAsString();
        message.release_subtask_queue();
        sgns::networkregistry::MembershipFilter membershipFilter;
        std::shared_ptr<const libp2p::crypto::KeyPair> signingKey;
        {
            std::lock_guard<std::mutex> guard( m_mutexMembershipFilter );
            membershipFilter = m_membershipFilter;
            signingKey       = m_gossipSigningKey;
        }
        if ( !membershipFilter )
        {
            m_processingQueueChannel->Publish( raw_payload );
        }
        else if ( !signingKey )
        {
            m_logger->error( "Queue channel publish FAILED CLOSED: membership filter set but no "
                             "gossip signing key wired" );
        }
        else
        {
            auto from_bytes = sgns::base::DeriveGossipFromBytes( *signingKey );
            if ( from_bytes.has_error() )
            {
                m_logger->error( "Queue channel publish FAILED CLOSED: cannot derive from-bytes "
                                 "from the gossip signing key" );
            }
            else
            {
                auto sealed = sgns::base::SealGossipPayload( *signingKey, from_bytes.value(), sgns::base::detail::StringSpan( raw_payload ) );
                if ( sealed.has_error() )
                {
                    m_logger->error( "Queue channel publish FAILED CLOSED: sealing failed ({})",
                                     static_cast<int>( sealed.error() ) );
                }
                else
                {
                    m_processingQueueChannel->Publish( sealed.value() );
                }
            }
        }
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

    void ProcessingSubTaskQueueChannelPubSub::SetGossipSigningKey( std::shared_ptr<const libp2p::crypto::KeyPair> key )
    {
        std::lock_guard<std::mutex> guard( m_mutexMembershipFilter );
        m_gossipSigningKey = std::move( key );
    }

    void ProcessingSubTaskQueueChannelPubSub::OnProcessingChannelMessage(
        boost::optional<const GossipPubSub::Message &> message )
    {
        // Membership gate (15-13) + payload authentication (15-14, CR-G01):
        // under a set membership filter the message is FIRST authenticated
        // (OpenGossipPayload: envelope present, embedded key derives the
        // from-field PeerId, signature covers from+payload) and only THEN
        // authorized BEFORE any queue ownership/sync handling. Unsigned or
        // unverifiable messages are denied under a set filter even when
        // `from` names a member. Empty filter = public pass-through (raw
        // parse, byte-identical); empty/malformed `from` fails
        // OpenGossipPayload itself (fail-closed).
        gsl::span<const uint8_t> channel_parse_source;
        if ( message )
        {
            channel_parse_source = gsl::span<const uint8_t>( message->data.data(), message->data.size() );
            sgns::networkregistry::MembershipFilter membershipFilter;
            {
                std::lock_guard<std::mutex> guard( m_mutexMembershipFilter );
                membershipFilter = m_membershipFilter;
            }
            if ( membershipFilter )
            {
                auto opened = sgns::base::OpenGossipPayload(
                    gsl::span<const uint8_t>( message->from.data(), message->from.size() ),
                    channel_parse_source );
                if ( opened.has_error() )
                {
                    m_logger->debug( "Processing queue channel message failed payload authentication ({}) -- ignored",
                                     static_cast<int>( opened.error() ) );
                    return;
                }
                channel_parse_source = opened.value().payload;
                if ( !sgns::networkregistry::AuthorizeGossipSender( membershipFilter, message->from ) )
                {
                    m_logger->debug( "Processing queue channel message from unauthorized sender ignored" );
                    return;
                }
            }
        }

        if ( message )
        {
            SGProcessing::ProcessingChannelMessage channelMesssage;
            if ( channelMesssage.ParseFromArray( channel_parse_source.data(),
                                                 static_cast<int>( channel_parse_source.size() ) ) )
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
