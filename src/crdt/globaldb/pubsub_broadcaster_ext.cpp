#include "pubsub_broadcaster_ext.hpp"
#include "crdt/globaldb/proto/broadcast.pb.h"

#include "crdt/crdt_datastore.hpp"
#include <ipfs_lite/ipld/ipld_node.hpp>
#include <regex>
#include <utility>

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
        std::shared_ptr<GossipPubSubTopic>              pubSubTopic,
        std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
        libp2p::multi::Multiaddress                     dagSyncerMultiaddress )
    {
        auto pubsub_broadcaster_instance = std::shared_ptr<PubSubBroadcasterExt>(
            new PubSubBroadcasterExt( std::move( pubSubTopic ),
                                      std::move( dagSyncer ),
                                      std::move( dagSyncerMultiaddress ) ) );
        if ( pubsub_broadcaster_instance->gossipPubSubTopic_ != nullptr )
        {
            pubsub_broadcaster_instance->gossipPubSubTopic_->Subscribe(
                [weakptr = std::weak_ptr<PubSubBroadcasterExt>( pubsub_broadcaster_instance )](
                    boost::optional<const GossipPubSub::Message &> message )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        self->OnMessage( message );
                    };
                } );
        }
        return pubsub_broadcaster_instance;
    }

    PubSubBroadcasterExt::PubSubBroadcasterExt( std::shared_ptr<GossipPubSubTopic>              pubSubTopic,
                                                std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                                                libp2p::multi::Multiaddress dagSyncerMultiaddress ) :
        gossipPubSubTopic_( std::move( pubSubTopic ) ),
        dagSyncer_( std::move( dagSyncer ) ),
        dataStore_( nullptr ),
        dagSyncerMultiaddress_( std::move( dagSyncerMultiaddress ) )
    {
        m_logger->trace( "Intializing Pubsub Broadcaster" );
    }

    void PubSubBroadcasterExt::OnMessage( boost::optional<const GossipPubSub::Message &> message )
    {
        m_logger->trace( "Got a message" );
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
                            if ( addrvector.size() > 0 )
                            {
                                auto hb = dagSyncer_->HasBlock( cid );
                                if ( hb.has_value() && !hb.value() )
                                {
                                    m_logger->debug( "Request node {} from {} {}",
                                                     cid.toString().value(),
                                                     addrvector[0].getStringAddress(),
                                                     peerId.toBase58() );
                                    dagSyncer_->AddRoute( cid, peerId, addrvector );
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

    void PubSubBroadcasterExt::SetCrdtDataStore( CrdtDatastore *dataStore )
    {
        dataStore_ = dataStore;
    }

    outcome::result<void> PubSubBroadcasterExt::Broadcast( const base::Buffer &buff )
    {
        if ( this->gossipPubSubTopic_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto bmsg = new sgns::crdt::broadcasting::BroadcastMessage;

        auto bpi = new sgns::crdt::broadcasting::BroadcastMessage_PeerInfo;

        //Get the port from the dagsyncer
        auto port_opt = dagSyncerMultiaddress_.getFirstValueForProtocol( libp2p::multi::Protocol::Code::TCP );
        if ( !port_opt )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto port = port_opt.value();

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
            m_logger->error( "Dag syncer has no peer info" );
            return outcome::failure( boost::system::error_code{} );
        }
        auto peer_info = peer_info_res.value();
        bpi->set_id( std::string( peer_info.id.toVector().begin(), peer_info.id.toVector().end() ) );

        auto pubsubObserved = gossipPubSubTopic_->GetPubsub()->GetHost()->getObservedAddressesReal();
        for ( auto &address : pubsubObserved )
        {
            auto ip_address_opt = address.getFirstValueForProtocol( libp2p::multi::Protocol::Code::IP4 );
            if ( ip_address_opt )
            {
                auto new_address = libp2p::multi::Multiaddress::create(
                    fmt::format( "/ip4/{}/tcp/{}/p2p/{}", ip_address_opt.value(), port, peer_info.id.toBase58() ) );
                auto addrstr = new_address.value().getStringAddress();
                if ( new_address )
                {
                    m_logger->info( "Address Broadcast Converted {}", new_address.value().getStringAddress() );
                    bpi->add_addrs( new_address.value().getStringAddress() );
                }
            }
        }

        auto mas = peer_info.addresses;
        for ( auto &address : mas )
        {
            bpi->add_addrs( address.getStringAddress() );
            m_logger->info( "Address Broadcast {}", address.getStringAddress() );
            //auto ip_address_opt = address.getFirstValueForProtocol(libp2p::multi::Protocol::Code::IP4);
            //if (ip_address_opt) {
            //    auto new_address = libp2p::multi::Multiaddress::create(fmt::format("/ip4/{}/tcp/{}/p2p/{}", ip_address_opt.value(), port, peer_id));
            //    auto addrstr = new_address.value().getStringAddress();
            //    bmsg.add_multiaddress(std::string(addrstr.begin(), addrstr.end()));
            //}
        }

        //If no addresses existed, we don't have anything to broadcast that is not otherwise a local address.
        if ( bpi->addrs_size() <= 0 )
        {
            return outcome::success();
        }
        bmsg->set_allocated_peer( bpi );
        //bmsg.set_multiaddress(std::string(multiaddress.begin(), multiaddress.end()));
        std::string data( buff.toString() );
        bmsg->set_data( data );
        gossipPubSubTopic_->Publish( bmsg->SerializeAsString() );
        //std::vector<std::string> address_vector(bmsg.multiaddress().begin(), bmsg.multiaddress().end());
        m_logger->debug( "CIDs broadcasted by {}", peer_info.id.toBase58() );

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
}
