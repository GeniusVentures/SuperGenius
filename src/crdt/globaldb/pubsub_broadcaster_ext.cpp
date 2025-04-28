#include "pubsub_broadcaster_ext.hpp"
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
        std::vector<std::string>                        topicsToListen,
        std::vector<std::string>                        topicsToBroadcast,
        std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
        std::shared_ptr<GossipPubSub>                   pubSub )
    {
        if ( topicsToListen.empty() )
        {
            return nullptr;
        }
        if ( !pubSub )
        {
            return nullptr;
        }
        auto instance = std::shared_ptr<PubSubBroadcasterExt>(
            new PubSubBroadcasterExt( std::move( topicsToListen ),
                                      std::move( topicsToBroadcast ),
                                      std::move( dagSyncer ),
                                      std::move( pubSub ) ) );
        return instance;
    }

    PubSubBroadcasterExt::PubSubBroadcasterExt( std::vector<std::string>                        topicsToListen,
                                                std::vector<std::string>                        topicsToBroadcast,
                                                std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                                                std::shared_ptr<GossipPubSub>                   pubSub ) :
        topicsToListen_( topicsToListen.begin(), topicsToListen.end() ),
        topicsToBroadcast_( topicsToBroadcast.begin(), topicsToBroadcast.end() ),
        dagSyncer_( std::move( dagSyncer ) ),
        dataStore_( nullptr ),
        pubSub_( std::move( pubSub ) )
    {
        m_logger->trace( "Initializing PubSubBroadcasterExt" );
    }

    PubSubBroadcasterExt ::~PubSubBroadcasterExt()
    {
        m_logger->debug( "~PubSubBroadcasterExt CALLED" );
    }

    void PubSubBroadcasterExt::Start()
    {
        std::lock_guard<std::mutex> lock( listenTopicsMutex_ );

        // Subscribe to each topic.
        for ( auto &topicName : topicsToListen_ )
        {
            m_logger->debug( "Subscription request sent to topic: " + topicName );

            // Subscribe and capture the topic name in the lambda.
            std::future<libp2p::protocol::Subscription> future = std::move( pubSub_->Subscribe(
                topicName,
                [weakptr = weak_from_this(), topicName]( boost::optional<const GossipPubSub::Message &> message )
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

            std::lock_guard<std::mutex> lock( queueMutex_ );

            base::Buffer buf;
            buf.put( bmsg.data() );

            // if CIDs don't work or can't map the broadcast the dataStore might try to call logger_ which will be called with nullptr of dataStore.
            BOOST_ASSERT_MSG( dataStore_ != nullptr, "Data store is not set" );
            auto cids = dataStore_->DecodeBroadcast( buf );
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
                    m_logger->debug( "Added Address: {}", addr_res.value().getStringAddress() );
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
            //auto pi = PeerInfoFromString(bmsg.multiaddress());
            for ( const auto &cid : cids.value() )
            {
                auto hb = dagSyncer_->HasBlock( cid );
                if ( !hb.has_value() )
                {
                    m_logger->warn( "HasBlock query failed for CID {}", cid.toString().value() );
                    continue;
                }

                if ( hb.value() )
                {
                    m_logger->trace( "Not adding route node {} from {}",
                                     cid.toString().value(),
                                     addrvector[0].getStringAddress() );
                    continue;
                }
                m_logger->debug( "Request node {} from {} {}",
                                 cid.toString().value(),
                                 addrvector[0].getStringAddress(),
                                 peerId.toBase58() );
                dagSyncer_->AddRoute( cid, peerId, addrvector );
            }
            messageQueue_.emplace( std::move( peerId ), bmsg.data() );
        } while ( 0 );
    }

    void PubSubBroadcasterExt::SetCrdtDataStore( std::shared_ptr<CrdtDatastore> dataStore )
    {
        if ( dataStore_ && dataStore_ == dataStore )
        {
            return; // Avoid resetting if it's already the same instance
        }

        dataStore_ = std::move( dataStore ); // Keeps reference, no ownership transfer
    }

    outcome::result<void> PubSubBroadcasterExt::Broadcast( const base::Buffer        &buff,
                                                           std::optional<std::string> topicName )
    {
        std::set<std::string> broadcastTopicsCopy;
        {
            std::lock_guard<std::mutex> lock( broadcastTopicsMutex_ );
            broadcastTopicsCopy = topicsToBroadcast_;
        }

        if ( broadcastTopicsCopy.empty() )
        {
            m_logger->error( "Broadcast: no topic to broadcast" );
            return outcome::failure( boost::system::error_code{} );
        }

        sgns::crdt::broadcasting::BroadcastMessage bmsg;
        auto                                       bpi = new sgns::crdt::broadcasting::BroadcastMessage_PeerInfo;

        auto peer_info_res = dagSyncer_->GetPeerInfo();
        if ( !peer_info_res )
        {
            m_logger->error( "Dag syncer has no peer info." );
            delete bpi;
            return outcome::failure( boost::system::error_code{} );
        }
        auto peer_info = peer_info_res.value();
        bpi->set_id( std::string( peer_info.id.toVector().begin(), peer_info.id.toVector().end() ) );

        // Add observed addresses and the peer’s own addresses.
        auto pubsubObserved = pubSub_->GetHost()->getObservedAddressesReal();
        for ( auto &address : pubsubObserved )
        {
            bpi->add_addrs( address.getStringAddress() );
        }
        auto mas = peer_info.addresses;
        for ( auto &address : mas )
        {
            bpi->add_addrs( address.getStringAddress() );
            m_logger->info( "Address Broadcast: {}", address.getStringAddress() );
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

        bmsg.SerializeToArray( serialized_proto.data(), serialized_proto.size() );

        for ( auto &topic : broadcastTopicsCopy )
        {
            pubSub_->Publish( topic, serialized_proto );
            m_logger->debug( "CIDs broadcasted by {} to topic {}", peer_info.id.toBase58(), topic );
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

    outcome::result<void> PubSubBroadcasterExt::AddBroadcastTopic( const std::string &topicName )
    {
        {
            std::lock_guard<std::mutex> lock( broadcastTopicsMutex_ );

            if ( topicsToBroadcast_.find( topicName ) != topicsToBroadcast_.end() )
            {
                m_logger->debug( "Topic '{}' already exists. Skipping.", topicName );
                return outcome::success();
            }

            topicsToBroadcast_.insert( topicName );
        }

        return outcome::success();
    }

    bool PubSubBroadcasterExt::HasTopic( const std::string &topic )
    {
        std::lock_guard<std::mutex> lock( broadcastTopicsMutex_ );
        return topicsToBroadcast_.find( topic ) != topicsToBroadcast_.end();
    }

    void PubSubBroadcasterExt::AddListenTopic( const std::string &topic )
    {
        std::lock_guard<std::mutex> lock( listenTopicsMutex_ );

        m_logger->trace( "Listen request on topic: '{}'", topic );

        std::future<libp2p::protocol::Subscription> future = std::move( pubSub_->Subscribe(
            topic,
            [weakptr = weak_from_this(), topic]( boost::optional<const GossipPubSub::Message &> message )
            {
                if ( auto self = weakptr.lock() )
                {
                    self->m_logger->debug( "Message received from topic: " + topic );
                    self->OnMessage( message, topic );
                }
            } ) );
            
        {
            std::lock_guard<std::mutex> lock( subscriptionMutex_ );
            subscriptionFutures_.push_back( std::move( future ) );
        }
    }

    void PubSubBroadcasterExt::Stop()
    {
        std::lock_guard<std::mutex> lock( mapMutex_ );

        for ( auto &topic : topicMap_ )
        {
            topic.second->Unsubscribe(); // You need to implement Unsubscribe in GossipPubSubTopic
        }

        topicMap_.clear();            // Clear all topics
        subscriptionFutures_.clear(); // Clear all pending subscriptions
        dataStore_.reset();           // Drop the reference to CrdtDatastore explicitly
    }


} // namespace sgns::crdt
