#include "pubsub_broadcaster_ext.hpp"
#include "base/sgns_version.hpp"
#include "crdt/globaldb/proto/broadcast.pb.h"
#include "crdt/crdt_datastore.hpp"
#include <ipfs_lite/ipld/ipld_node.hpp>
#include <regex>
#include <utility>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>

using namespace boost;

namespace sgns::crdt
{
    namespace
    {
        boost::optional<libp2p::peer::PeerInfo> PeerInfoFromString(
            const google::protobuf::RepeatedPtrField<std::string> &addresses )
        {
            std::vector<libp2p::multi::Multiaddress> valid_addresses;
            boost::optional<libp2p::peer::PeerId>    peer_id;

            for ( const auto &address : addresses )
            {
                auto server_ma_res = libp2p::multi::Multiaddress::create( address );
                if ( !server_ma_res )
                {
                    continue; // Skip invalid addresses
                }
                auto server_ma = std::move( server_ma_res.value() );

                auto server_peer_id_str = server_ma.getPeerId();
                if ( !server_peer_id_str )
                {
                    continue; // Skip addresses without a peer ID
                }

                auto server_peer_id_res = libp2p::peer::PeerId::fromBase58( *server_peer_id_str );
                if ( !server_peer_id_res )
                {
                    continue; // Skip invalid peer IDs
                }

                if ( !peer_id )
                {
                    peer_id = server_peer_id_res.value(); // Set peer ID for the first valid address
                }
                else if ( peer_id.value() != server_peer_id_res.value() )
                {
                    return boost::none; // Peer IDs must match
                }

                valid_addresses.push_back( std::move( server_ma ) );
            }

            if ( valid_addresses.empty() || !peer_id )
            {
                return boost::none; // No valid addresses or no peer ID
            }

            return libp2p::peer::PeerInfo{ peer_id.value(), std::move( valid_addresses ) };
        }
    }

    std::shared_ptr<PubSubBroadcasterExt> PubSubBroadcasterExt::New(
        std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
        std::shared_ptr<GossipPubSub>                   pubSub )
    {
        if ( !dagSyncer )
        {
            return nullptr;
        }
        if ( !pubSub )
        {
            return nullptr;
        }
        auto instance = std::shared_ptr<PubSubBroadcasterExt>(
            new PubSubBroadcasterExt( std::move( dagSyncer ), std::move( pubSub ) ) );
        return instance;
    }

    PubSubBroadcasterExt::PubSubBroadcasterExt( std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                                                std::shared_ptr<GossipPubSub>                   pubSub ) :

        dagSyncer_( std::move( dagSyncer ) ), pubSub_( std::move( pubSub ) ), started_{ false }
    {
        m_logger->trace( "Initializing PubSubBroadcasterExt" );
    }

    PubSubBroadcasterExt ::~PubSubBroadcasterExt()
    {
        m_logger->debug( "~PubSubBroadcasterExt CALLED" );
    }

    void PubSubBroadcasterExt::Start()
    {
        if ( !started_ )
        {
            started_ = true;
            std::lock_guard<std::mutex> lock( listenTopicsMutex_ );

            // Subscribe to each topic.
            for ( auto &topicName : topicsToListen_ )
            {
                m_logger->debug( "Subscription request sent to topic: " + topicName );

                // Subscribe and capture the topic name in the lambda.
                std::shared_future<std::shared_ptr<ipfs_pubsub::GossipPubSub::Subscription>> future = std::move(
                    pubSub_->Subscribe( topicName,
                                        [weakptr = weak_from_this(),
                                         topicName]( boost::optional<const GossipPubSub::Message &> message )
                                        {
                                            if ( auto self = weakptr.lock() )
                                            {
                                                self->m_logger->debug( "Message received on topic: {}", topicName );
                                                self->OnMessage( message, topicName );
                                            }
                                        } ) );
                {
                    std::lock_guard lk( subscriptionMutex_ );
                    subscriptionFutures_.push_back( std::move( future ) );
                }
            }
        }
    }

    void PubSubBroadcasterExt::OnMessage( boost::optional<const GossipPubSub::Message &> message,
                                          const std::string                             &incomingTopic )
    {
        // Log that a message has been received (the incoming parameter is not used for filtering).
        m_logger->trace( "Received a message from topic {}", incomingTopic );
        do
        {
            if ( !message )
            {
                m_logger->error( "No message to process" );
                break;
            }
            sgns::crdt::broadcasting::BroadcastMessage bmsg;
            if ( !bmsg.ParseFromArray( message->data.data(), message->data.size() ) )
            {
                m_logger->error( "Failed to parse BroadcastMessage" );
                break;
            }

            auto peer_id_res = libp2p::peer::PeerId::fromBytes(
                gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( bmsg.peer().id().data() ),
                                          bmsg.peer().id().size() ) );

            if ( !peer_id_res )
            {
                m_logger->error( "Failed to construct PeerId from bytes" );
                break;
            }

            auto peerId = peer_id_res.value();
            m_logger->trace( "Message from peer {}", peerId.toBase58() );

            // Membership gate (15-11): when a private-network membership
            // filter is installed, drop the message BEFORE any CID decode,
            // route, or queueing unless BOTH identities are authorized
            // members: the declared protobuf peer (peerId above) and the
            // transport sender (message->from). Checking both defends
            // against spoofing either field. This is deliberately a
            // line-for-line MIRROR of sgns::networkregistry::
            // AuthorizeGossipSender rather than a call: the layering rule
            // forbids this .cpp from including any networkregistry/ header.
            // Fail-closed: an EMPTY or malformed `from` fails
            // PeerId::fromBytes and is DENIED -- there is no emptiness skip
            // branch. No filter installed -> public pass-through (zero
            // behavior change).
            std::function<bool( const libp2p::peer::PeerId & )> membershipFilter;
            {
                std::lock_guard<std::mutex> lock( membership_filter_mutex_ );
                membershipFilter = membership_filter_;
            }
            if ( membershipFilter )
            {
                if ( !membershipFilter( peerId ) )
                {
                    m_logger->debug( "Dropping message from non-member peer {}", peerId.toBase58() );
                    break;
                }
                auto from_peer_res = libp2p::peer::PeerId::fromBytes(
                    gsl::span<const uint8_t>( message->from.data(), message->from.size() ) );
                if ( !from_peer_res || !membershipFilter( from_peer_res.value() ) )
                {
                    m_logger->debug( "Dropping message with unauthorized transport sender (from)" );
                    break;
                }
            }

            base::Buffer buf;
            buf.put( bmsg.data() );

            auto cids = CrdtDatastore::DecodeBroadcast( buf );
            if ( cids.has_failure() )
            {
                m_logger->error( "Failed to decode broadcast payload" );
                break;
            }
            auto                                     addresses = bmsg.peer().addrs();
            std::vector<libp2p::multi::Multiaddress> addrvector;
            for ( auto &addr : addresses )
            {
                auto addr_res = libp2p::multi::Multiaddress::create( addr );
                if ( addr_res )
                {
                    addrvector.push_back( addr_res.value() );
                    m_logger->trace( "Added Address: {}", addr_res.value().getStringAddress() );
                }
            }
            if ( addrvector.empty() )
            {
                m_logger->trace( "No addresses available for CIDs broadcast" );
                break;
            }
            if ( dagSyncer_->IsOnBlackList( peerId ) )
            {
                m_logger->debug( "The peer {} is blacklisted", peerId.toBase58() );
                break;
            }
            bool new_content = AddMultiCIDInfo( cids.value(), peerId, addrvector );

            if ( new_content )
            {
                {
                    std::lock_guard<std::mutex> lock( queueMutex_ );
                    messageQueue_.emplace( std::move( peerId ), bmsg.data() );
                }
                queueCv_.notify_one();
            }
            else
            {
                m_logger->debug( "No new content from message" );
            }
        } while ( 0 );
    }

    void PubSubBroadcasterExt::SetMembershipFilter( std::function<bool( const libp2p::peer::PeerId & )> filter )
    {
        std::lock_guard<std::mutex> lock( membership_filter_mutex_ );
        membership_filter_ = std::move( filter );
    }

    bool PubSubBroadcasterExt::HasMembershipFilter() const
    {
        std::lock_guard<std::mutex> lock( membership_filter_mutex_ );
        return static_cast<bool>( membership_filter_ );
    }

    void PubSubBroadcasterExt::ClearMembershipFilter()
    {
        std::lock_guard<std::mutex> lock( membership_filter_mutex_ );
        membership_filter_ = nullptr;
    }

    outcome::result<void> PubSubBroadcasterExt::Broadcast( const base::Buffer                     &buff,
                                                           std::string                             topic,
                                                           boost::optional<libp2p::peer::PeerInfo> peerInfo )
    {
        std::set<std::string> broadcastTopicsCopy;
        {
            std::lock_guard<std::mutex> lock( broadcastTopicsMutex_ );
            broadcastTopicsCopy = topicsToBroadcast_;
        }
        if ( !topic.empty() )
        {
            auto full_topic = topic + version::GetNetAndVersionAppendix();
            broadcastTopicsCopy.emplace( full_topic );
        }

        if ( broadcastTopicsCopy.empty() )
        {
            m_logger->error( "Broadcast: no topic to broadcast" );
            return outcome::failure( boost::system::error_code{} );
        }

        sgns::crdt::broadcasting::BroadcastMessage bmsg;
        auto                                       bpi = new sgns::crdt::broadcasting::BroadcastMessage_PeerInfo;

        // Get peer_id - determine which branch to use first, then initialize
        boost::optional<libp2p::peer::PeerId> peer_id_opt;

        if ( peerInfo )
        {
            peer_id_opt = peerInfo->id;
        }
        else
        {
            auto peer_id_res = dagSyncer_->GetId();
            if ( !peer_id_res )
            {
                m_logger->error( "Dag syncer has no peer ID." );
                delete bpi;
                return outcome::failure( boost::system::error_code{} );
            }
            peer_id_opt = peer_id_res.value();
        }

        auto &peer_id = peer_id_opt.value();
        bpi->set_id( std::string( peer_id.toVector().begin(), peer_id.toVector().end() ) );

        // Add addresses from PeerInfo (which already includes observed, interface, and relay addresses)
        if ( peerInfo )
        {
            for ( auto &address : peerInfo->addresses )
            {
                bpi->add_addrs( address.getStringAddress() );
                m_logger->trace( "Added address from PeerInfo: {}", address.getStringAddress() );
            }
        }

        if ( bpi->addrs_size() <= 0 )
        {
            m_logger->warn( "No addresses found for broadcasting." );
            delete bpi;
            return outcome::success();
        }

        bmsg.set_allocated_peer( bpi );
        std::string data( buff.toString() );
        bmsg.set_data( data );

        size_t               size = bmsg.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        if ( !bmsg.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            m_logger->error( "Failed to serialize broadcast message" );
            return std::errc::bad_message;
        }

        for ( auto &topic : broadcastTopicsCopy )
        {
            pubSub_->PublishBuffered( topic, serialized_proto );
            if ( m_logger->level() <= spdlog::level::trace )
            {
                m_logger->trace( "CIDs broadcasted by {} to topic {}, at this {}",
                                 peer_id.toBase58(),
                                 topic,
                                 reinterpret_cast<size_t>( this ) );
            }
        }

        return outcome::success();
    }

    outcome::result<base::Buffer> PubSubBroadcasterExt::Next()
    {
        std::lock_guard<std::mutex> lock( queueMutex_ );

        if ( messageQueue_.empty() )
        {
            // Broadcaster::ErrorCode::ErrNoMoreBroadcast
            return outcome::failure( boost::system::error_code{} );
        }

        std::string strBuffer = std::get<1>( messageQueue_.front() );
        messageQueue_.pop();

        base::Buffer buffer;
        buffer.put( strBuffer );
        return buffer;
    }

    void PubSubBroadcasterExt::WaitForNext( std::chrono::milliseconds timeout )
    {
        std::unique_lock<std::mutex> lock( queueMutex_ );
        // Checking the queue in the predicate is what makes a push racing against the
        // caller's last Next() safe: the notify may land with no waiter, but the queue
        // is already non-empty by then, so the wait returns instead of missing it.
        queueCv_.wait_for( lock, timeout, [this] { return wait_cancelled_ || !messageQueue_.empty(); } );
    }

    void PubSubBroadcasterExt::CancelWait()
    {
        {
            std::lock_guard<std::mutex> lock( queueMutex_ );
            wait_cancelled_ = true;
        }
        queueCv_.notify_all();
    }

    outcome::result<void> PubSubBroadcasterExt::AddBroadcastTopic( const std::string &topicName )
    {
        auto full_topic = topicName + version::GetNetAndVersionAppendix();
        {
            std::lock_guard<std::mutex> lock( broadcastTopicsMutex_ );

            if ( topicsToBroadcast_.find( full_topic ) != topicsToBroadcast_.end() )
            {
                m_logger->trace( "Topic '{}' already exists. Skipping.", full_topic );
                return outcome::success();
            }

            topicsToBroadcast_.insert( full_topic );
        }

        return outcome::success();
    }

    bool PubSubBroadcasterExt::HasTopic( const std::string &topic )
    {
        std::lock_guard<std::mutex> lock( broadcastTopicsMutex_ );
        return topicsToBroadcast_.find( topic ) != topicsToBroadcast_.end();
    }

    void PubSubBroadcasterExt::AddListenTopic( std::string topic )
    {
        auto            full_topic = std::move( topic ) + version::GetNetAndVersionAppendix();
        std::lock_guard lock( listenTopicsMutex_ );
        if ( topicsToListen_.find( full_topic ) != topicsToListen_.end() )
        {
            this->m_logger->debug( "Already listening to topic {}", full_topic );
            return;
        }

        topicsToListen_.insert( full_topic );
        m_logger->debug( "Listen request on topic: '{}'", full_topic );
        if ( started_ )
        {
            std::shared_future<std::shared_ptr<ipfs_pubsub::GossipPubSub::Subscription>> future = std::move(
                pubSub_->Subscribe(
                    full_topic,
                    [weakptr = weak_from_this(), full_topic]( boost::optional<const GossipPubSub::Message &> message )
                    {
                        if ( auto self = weakptr.lock() )
                        {
                            self->m_logger->debug( "Message received from topic: " + full_topic );
                            self->OnMessage( message, full_topic );
                        }
                    } ) );

            {
                std::lock_guard lock( subscriptionMutex_ );
                subscriptionFutures_.push_back( std::move( future ) );
            }
        }
    }

    void PubSubBroadcasterExt::Stop()
    {
        std::lock_guard lock( subscriptionMutex_ );
        // Wait for any pending futures to complete before clearing
        for ( auto &future : subscriptionFutures_ )
        {
            if ( future.valid() )
            {
                try
                {
                    // Check if the future is ready without blocking indefinitely
                    if ( future.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready )
                    {
                        // Future is ready, safe to access
                        future.get();
                    }
                    // If not ready, just let it be destroyed naturally
                }
                catch ( ... )
                {
                    // Ignore any exceptions during cleanup
                }
            }
        }
        subscriptionFutures_.clear(); // Clear all pending subscriptions
    }

    bool PubSubBroadcasterExt::AddSingleCIDInfo( const std::string &cid,
                                                 const std::string  peer_id,
                                                 const std::string  address )
    {
        bool ret = false;
        do
        {
            auto cidResult = CID::fromString( cid );
            if ( cidResult.has_error() )
            {
                m_logger->error( "{}: Failed to construct CID from string", __func__ );
                break;
            }
            auto peer_id_res = libp2p::peer::PeerId::fromBytes(
                gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( peer_id.data() ), peer_id.size() ) );
            if ( !peer_id_res )
            {
                m_logger->error( "{}: Failed to construct PeerId from string", __func__ );
                break;
            }
            auto addr_res = libp2p::multi::Multiaddress::create( address );
            if ( !addr_res )
            {
                m_logger->error( "{}: Failed to construct Address from string", __func__ );
                break;
            }

            if ( AddMultiCIDInfo( { cidResult.value() }, peer_id_res.value(), { addr_res.value() } ) == false )
            {
                break;
            }
            auto cid_buffer = sgns::crdt::CrdtDatastore::EncodeBroadcastStatic( { cidResult.value() } );
            if ( cid_buffer.has_error() )
            {
                break;
            }
            {
                std::lock_guard<std::mutex> lock( queueMutex_ );
                messageQueue_.emplace( peer_id_res.value(), std::string( cid_buffer.value().toString() ) );
            }
            ret = true;
        } while ( 0 );

        if ( ret )
        {
            queueCv_.notify_one();
        }
        return ret;
    }

    bool PubSubBroadcasterExt::AddMultiCIDInfo( const std::vector<CID>                         &cids,
                                                const libp2p::peer::PeerId                     &peer_id,
                                                const std::vector<libp2p::multi::Multiaddress> &addr_vector )
    {
        bool new_content = false;
        for ( const auto &cid : cids )
        {
            auto hb = dagSyncer_->HasBlock( cid );
            if ( !hb.has_value() )
            {
                m_logger->debug( "{}: HasBlock query failed for CID {}", __func__, cid.toString().value() );
                continue;
            }

            if ( hb.value() )
            {
                m_logger->trace( "{}: Not adding route node {} from {} (already have block)",
                                 __func__,
                                 cid.toString().value(),
                                 addr_vector[0].getStringAddress() );
                continue;
            }
            new_content = true;
            if ( dagSyncer_->IsCIDInCache( cid ) )
            {
                m_logger->debug( "{}: CID {} already cached without data, refreshing route from {} {}",
                                 __func__,
                                 cid.toString().value(),
                                 addr_vector[0].getStringAddress(),
                                 peer_id.toBase58() );
            }
            else
            {
                m_logger->debug( "{}: Request node {} from {} {}",
                                 __func__,
                                 cid.toString().value(),
                                 addr_vector[0].getStringAddress(),
                                 peer_id.toBase58() );
            }
            dagSyncer_->AddRoute( cid, peer_id, addr_vector );
        }
        return new_content;
    }

} // namespace sgns::crdt
