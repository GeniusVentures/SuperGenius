#include "pubsub_broadcaster_ext.hpp"
#include <crdt/globaldb/proto/broadcast.pb.h>

#include <crdt/crdt_datastore.hpp>
#include <ipfs_lite/ipld/ipld_node.hpp>
#include <regex>
#include <utility>

namespace sgns::crdt
{
namespace
{
    boost::optional<libp2p::peer::PeerInfo> PeerInfoFromString(const google::protobuf::RepeatedPtrField<std::string>& addresses)
    {
        std::vector<libp2p::multi::Multiaddress> valid_addresses;
        boost::optional<libp2p::peer::PeerId> peer_id;

        for (const auto& address : addresses)
        {
            auto server_ma_res = libp2p::multi::Multiaddress::create(address);
            if (!server_ma_res)
            {
                continue; // Skip invalid addresses
            }
            auto server_ma = std::move(server_ma_res.value());

            auto server_peer_id_str = server_ma.getPeerId();
            if (!server_peer_id_str)
            {
                continue; // Skip addresses without a peer ID
            }

            auto server_peer_id_res = libp2p::peer::PeerId::fromBase58(*server_peer_id_str);
            if (!server_peer_id_res)
            {
                continue; // Skip invalid peer IDs
            }

            if (!peer_id)
            {
                peer_id = server_peer_id_res.value(); // Set peer ID for the first valid address
            }
            else if (peer_id.value() != server_peer_id_res.value())
            {
                return boost::none; // Peer IDs must match
            }

            valid_addresses.push_back(std::move(server_ma));
        }

        if (valid_addresses.empty() || !peer_id)
        {
            return boost::none; // No valid addresses or no peer ID
        }

        return libp2p::peer::PeerInfo{ peer_id.value(), std::move(valid_addresses) };
    }
}

PubSubBroadcasterExt::PubSubBroadcasterExt( std::shared_ptr<GossipPubSubTopic>              pubSubTopic,
                                            std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                                            libp2p::multi::Multiaddress                     dagSyncerMultiaddress) :
    gossipPubSubTopic_( std::move( pubSubTopic ) ), dagSyncer_( std::move( dagSyncer ) ), dataStore_( nullptr ),
    dagSyncerMultiaddress_(std::move(dagSyncerMultiaddress))
{
    if (gossipPubSubTopic_ != nullptr)
    {
        gossipPubSubTopic_->Subscribe(std::bind(&PubSubBroadcasterExt::OnMessage, this, std::placeholders::_1));
    }
}

void PubSubBroadcasterExt::OnMessage(boost::optional<const GossipPubSub::Message&> message)
{
    if (message)
    {
        sgns::crdt::broadcasting::BroadcastMessage bmsg;
        if (bmsg.ParseFromArray(message->data.data(), message->data.size()))
        {
            auto peerId = libp2p::peer::PeerId::fromBytes(message->from);
            if (peerId.has_value())
            {
                std::scoped_lock lock(mutex_);
                base::Buffer buf;
                buf.put(bmsg.data());
                auto cids = dataStore_->DecodeBroadcast(buf);
                if (!cids.has_failure())
                {
                    auto pi = PeerInfoFromString(bmsg.multiaddress());
                    for (const auto& cid : cids.value())
                    {
                        if (pi.has_value())
                        {
                            auto hb = dagSyncer_->HasBlock(cid);
                            if (hb.has_value() && !hb.value())
                            {
                                m_logger->debug("Request node {} from {}", cid.toString().value(), pi.value().addresses[0].getStringAddress());
                                dagSyncer_->AddRoute(cid, pi.value().id, pi.value().addresses[0]);
                            }
                        }
                    }
                    
                }
                messageQueue_.emplace( std::move( peerId.value() ), bmsg.data() );
            }
        }
    }
}

void PubSubBroadcasterExt::SetLogger(const sgns::base::Logger& logger)
{ 
    logger_ = logger; 
}

void PubSubBroadcasterExt::SetCrdtDataStore(CrdtDatastore* dataStore)
{
    dataStore_ = dataStore;
}

outcome::result<void> PubSubBroadcasterExt::Broadcast(const base::Buffer& buff)
{
    if (this->gossipPubSubTopic_ == nullptr)
    {
        return outcome::failure(boost::system::error_code{});
    }

    sgns::crdt::broadcasting::BroadcastMessage bmsg;
    
    //Get the port from the dagsyncer
    auto port_opt = dagSyncerMultiaddress_.getFirstValueForProtocol(libp2p::multi::Protocol::Code::TCP);
    if (!port_opt) {
        return outcome::failure(boost::system::error_code{});
    }
    auto port = port_opt.value();
    
    //Get peer ID from dagsyncer
    auto peer_id_opt = dagSyncerMultiaddress_.getPeerId();
    if (!peer_id_opt)
    {
        return outcome::failure(boost::system::error_code{});
    }
    auto peer_id = peer_id_opt.value();

    //Get observed addresses from pubsub and use them with this port and peer id. 
    // We are trusting that observed addresses from pubsub are correct to avoid doing this twice.
    // We still probably need autonat/circuit relay/holepunching on the dagsyncer, which may remove this code.
    // Add them to the broadcast message
    auto mas = gossipPubSubTopic_->GetPubsub()->GetHost()->getObservedAddresses();
    for (auto& address : mas)
    {
        auto ip_address_opt = address.getFirstValueForProtocol(libp2p::multi::Protocol::Code::IP4);
        if (ip_address_opt) {
            auto new_address = libp2p::multi::Multiaddress::create(fmt::format("/ip4/{}/tcp/{}/p2p/{}", ip_address_opt.value(), port, peer_id));
            auto addrstr = new_address.value().getStringAddress();
            bmsg.add_multiaddress(std::string(addrstr.begin(), addrstr.end()));
        }
    }

    //If no addresses existed, we don't have anything to broadcast that is not otherwise a local address.
    if (bmsg.multiaddress_size() <= 0)
    {
        return outcome::failure(boost::system::error_code{});
    }
    //bmsg.set_multiaddress(std::string(multiaddress.begin(), multiaddress.end()));
    std::string data(buff.toString());
    bmsg.set_data(data);
    gossipPubSubTopic_->Publish(bmsg.SerializeAsString());
    std::vector<std::string> address_vector(bmsg.multiaddress().begin(), bmsg.multiaddress().end());
    m_logger->debug("CIDs broadcasted by {}", address_vector[0]);

    return outcome::success();
}

outcome::result<base::Buffer> PubSubBroadcasterExt::Next()
{
    std::scoped_lock lock(mutex_);
    if (messageQueue_.empty())
    {
        //Broadcaster::ErrorCode::ErrNoMoreBroadcast
        return outcome::failure(boost::system::error_code{});
    }

    std::string strBuffer = std::get<1>(messageQueue_.front());
    messageQueue_.pop();

    base::Buffer buffer;
    buffer.put(strBuffer);
    return buffer;
}
}
