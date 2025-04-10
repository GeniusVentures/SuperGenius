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

    // Static factory method that accepts a vector of topics.
    std::shared_ptr<PubSubBroadcasterExt> PubSubBroadcasterExt::New(
        const std::vector<std::shared_ptr<GossipPubSubTopic>> &pubSubTopics,
        std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer>        dagSyncer )
    {
        auto instance = std::shared_ptr<PubSubBroadcasterExt>(
            new PubSubBroadcasterExt( pubSubTopics, std::move( dagSyncer ), std::move( dagSyncerMultiaddress ) ) );
        return instance;
    }

    PubSubBroadcasterExt::PubSubBroadcasterExt( const std::vector<std::shared_ptr<GossipPubSubTopic>> &pubSubTopics,
                                                std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer>        dagSyncer ) :
        dagSyncer_( std::move( dagSyncer ) ),
        dataStore_( nullptr )
    {
        m_logger->trace( "Initializing PubSubBroadcasterExt" );
        if (!pubSubTopics.empty())
        {
            firstTopic_ = pubSubTopics.front()->GetTopic();
        }
        for (const auto& topic : pubSubTopics)
        {
            topics_.insert({topic->GetTopic(), topic});
        }
    }

    void PubSubBroadcasterExt::Start()
    {
        if ( topics_.empty() )
        {
            m_logger->debug( "No topics available at start. Waiting for topics to be added." );
            return;
        }
        // Subscribe to each topic.
        for ( auto &pair : topics_ )
        {
            auto &topic = pair.second;
            std::string topicName = topic->GetTopic();
            m_logger->debug( "Subscription request sent to topic: " + topicName );

            // Subscribe and capture the topic name in the lambda.
            std::future<libp2p::protocol::Subscription> future = std::move( topic->Subscribe(
                [weakptr = weak_from_this(), topicName]( boost::optional<const GossipPubSub::Message &> message )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        // Log the topic from which the message is received.
                        self->m_logger->debug( "Message received from topic: " + topicName );
                        self->OnMessage( message );
                    }
                } ) );
            subscriptionFutures_.push_back( std::move( future ) );
        }
    }

    void PubSubBroadcasterExt::OnMessage( boost::optional<const GossipPubSub::Message &> message )
    {
        m_logger->trace( "Received a message" );
        if ( message )
        {
            sgns::crdt::broadcasting::BroadcastMessage bmsg;
            if ( bmsg.ParseFromArray( message->data.data(), message->data.size() ) )
            {
                auto peer_id_res = libp2p::peer::PeerId::fromBytes(
                    gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( bmsg.peer().id().data() ),
                                              bmsg.peer().id().size() ) );
                //auto peerId = libp2p::peer::PeerId::fromBytes(message->from);
                if ( peer_id_res )
                {
                    auto peerId = peer_id_res.value();
                    m_logger->trace( "Message from peer {}", peerId.toBase58() );
                    std::scoped_lock lock( mutex_ );
                    base::Buffer     buf;
                    buf.put( bmsg.data() );
                    // if CIDs don't work or can't map the broadcast the dataStore might try to call logger_ which will be called with nullptr of dataStore.
                    BOOST_ASSERT_MSG( dataStore_ != nullptr, "Data store is not set" );
                    auto cids = dataStore_->DecodeBroadcast( buf );
                    if ( !cids.has_failure() )
                    {
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
                        //auto pi = PeerInfoFromString(bmsg.multiaddress());
                        for ( const auto &cid : cids.value() )
                        {
                            if ( !addrvector.empty() )
                            {
                                auto hb = dagSyncer_->HasBlock( cid );
                                if ( hb.has_value() && !hb.value() )
                                {
                                    if ( !dagSyncer_->IsOnBlackList( peerId ) )
                                    {
                                        m_logger->debug( "Request node {} from {} {}",
                                                         cid.toString().value(),
                                                         addrvector[0].getStringAddress(),
                                                         peerId.toBase58() );
                                        dagSyncer_->AddRoute( cid, peerId, addrvector );
                                    }
                                    else
                                    {
                                        m_logger->debug( "The peer {} that has CID {} is blacklisted",
                                                         peerId.toBase58(),
                                                         cid.toString().value() );
                                    }
                                }
                                else
                                {
                                    m_logger->trace( "Not adding route node {} from {} {} {}",
                                                     cid.toString().value(),
                                                     addrvector[0].getStringAddress(),
                                                     hb.has_value(),
                                                     hb.value() );
                                }
                            }
                        }
                    }
                    messageQueue_.emplace( std::move( peerId ), bmsg.data() );
                }
            }
        }
    }

    //void PubSubBroadcasterExt::SetLogger(const sgns::base::Logger& logger)
    //{
    //    logger_ = logger;
    //}

    void PubSubBroadcasterExt::SetCrdtDataStore( std::shared_ptr<CrdtDatastore> dataStore )
    {
        if ( dataStore_ && dataStore_ == dataStore )
        {
            return; //  Avoid resetting if it's already the same instance
        }

        dataStore_ = std::move( dataStore ); // Keeps reference, no ownership transfer
    }

    outcome::result<void> PubSubBroadcasterExt::Broadcast( const base::Buffer          &buff,
                                                           std::optional<std::string> topic_name )
    {
        if ( topics_.empty() )
        {
            m_logger->error( "No topics available for broadcasting." );
            return outcome::failure( boost::system::error_code{} );
        }

        // Determine the target topic.
        std::shared_ptr<GossipPubSubTopic> targetTopic = nullptr;
        if ( topic_name )
        {
            auto it = topics_.find(topic_name.value());
            if (it != topics_.end())
            {
                targetTopic = it->second;
            }
            if ( !targetTopic )
            {
                m_logger->warn( "Broadcast: no topic matching '" + topic_name.value() + "' was found" );
                return outcome::failure( boost::system::error_code{} );
            }
        }
        else
        {
            // Use the first topic added if no topic_name is provided.
            if ( firstTopic_.empty() || topics_.find(firstTopic_) == topics_.end() )
            {
                m_logger->error( "First topic is not available." );
                return outcome::failure( boost::system::error_code{} );
            }
            targetTopic = topics_.at(firstTopic_);
        }

        sgns::crdt::broadcasting::BroadcastMessage bmsg;
        auto bpi = new sgns::crdt::broadcasting::BroadcastMessage_PeerInfo;

        //Get the port from the dagsyncer
        //auto port_opt = dagSyncerMultiaddress_.getFirstValueForProtocol( libp2p::multi::Protocol::Code::TCP );
        //if ( !port_opt )
        //{
        //    delete bpi;
        //    return outcome::failure( boost::system::error_code{} );
        //}
        //auto port = port_opt.value();

        //Get peer ID from dagsyncer
        //auto peer_id_opt = dagSyncerMultiaddress_.getPeerId();
        //if (!peer_id_opt)
        //{
        //    return outcome::failure(boost::system::error_code{});
        //}
        //auto peer_id = peer_id_opt.value();

        //Get observed addresses from pubsub and use them with this port and peer id.
        // We are trusting that observed addresses from pubsub are correct to avoid doing this twice.
        // We still probably need autonat/circuit relay/holepunching on the dagsyncer, which may remove this code.
        // Add them to the broadcast message
        auto peer_info_res = dagSyncer_->GetPeerInfo();
        if ( !peer_info_res )
        {
            m_logger->error( "Dag syncer has no peer info." );
            delete bpi;
            return outcome::failure( boost::system::error_code{} );
        }
        auto peer_info = peer_info_res.value();
        bpi->set_id( std::string( peer_info.id.toVector().begin(), peer_info.id.toVector().end() ) );

        // Use observed addresses from the target topic's pubsub host.
        auto pubsubObserved = targetTopic->GetPubsub()->GetHost()->getObservedAddressesReal();
        for ( auto &address : pubsubObserved )
        {
            bpi->add_addrs( address.getStringAddress() );
        //    auto ip_address_opt = address.getFirstValueForProtocol( libp2p::multi::Protocol::Code::IP4 );
        //    if ( ip_address_opt )
        //    {
        //        auto new_address = libp2p::multi::Multiaddress::create(
        //            fmt::format( "/ip4/{}/tcp/{}/p2p/{}", ip_address_opt.value(), port, peer_info.id.toBase58() ) );
        //        auto addrstr = new_address.value().getStringAddress();
        //        if ( new_address )
        //        {
        //            m_logger->info( "Address Broadcast Converted {}", new_address.value().getStringAddress() );
        //            bpi->add_addrs( new_address.value().getStringAddress() );
        //        }
        //    }
        }

        // Also add the peer's own addresses.
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

        // Publish to the determined target topic.
        targetTopic->Publish( bmsg.SerializeAsString() );

        if ( topic_name )
        {
            m_logger->debug( "CIDs broadcasted by {} to SINGLE topic {}", peer_info.id.toBase58(), topic_name.value() );
        }
        else
        {
            std::string frontTopic = targetTopic->GetTopic();
            m_logger->debug( "CIDs broadcasted by {} to the first topic ({})", peer_info.id.toBase58(), frontTopic );
        }

        return outcome::success();
    }

    outcome::result<base::Buffer> PubSubBroadcasterExt::Next()
    {
        std::scoped_lock lock( mutex_ );
        if ( messageQueue_.empty() )
        {
            //Broadcaster::ErrorCode::ErrNoMoreBroadcast
            return outcome::failure( boost::system::error_code{} );
        }

        std::string strBuffer = std::get<1>( messageQueue_.front() );
        messageQueue_.pop();

        base::Buffer buffer;
        buffer.put( strBuffer );
        return buffer;
    }

    void PubSubBroadcasterExt::AddTopic(const std::shared_ptr<GossipPubSubTopic>& newTopic)
    {
        std::string topicName = newTopic->GetTopic();
    
        if ( topics_.find(topicName) != topics_.end() )
        {
            m_logger->debug("Topic '" + topicName + "' already exists. Not adding duplicate.");
            return;
        }
    
        topics_.insert({topicName, newTopic});
        if ( firstTopic_.empty() )
        {
            firstTopic_ = topicName;
        }
    
        m_logger->debug("Subscription request sent to new topic: " + topicName);
    
        std::future<libp2p::protocol::Subscription> future = std::move(newTopic->Subscribe(
            [weakptr = weak_from_this(), topicName](boost::optional<const GossipPubSub::Message &> message)
            {
                if (auto self = weakptr.lock())
                {
                    self->m_logger->debug("Message received from topic: " + topicName);
                    self->OnMessage(message);
                }
            }));
        subscriptionFutures_.push_back(std::move(future));
    }
    
} // namespace sgns::crdt
