#include "pubsub_broadcaster_ext.hpp"
#include <globaldb/proto/broadcast.pb.h>

#include <crdt/crdt_datastore.hpp>
#include <ipfs_lite/ipld/ipld_node.hpp>

namespace sgns::crdt
{
namespace
{
    boost::optional<libp2p::peer::PeerInfo> PeerInfoFromString(const std::string& str)
    {
        auto server_ma_res = libp2p::multi::Multiaddress::create(str);
        if (!server_ma_res)
        {
            return boost::none;
        }
        auto server_ma = std::move(server_ma_res.value());

        auto server_peer_id_str = server_ma.getPeerId();
        if (!server_peer_id_str)
        {
            return boost::none;
        }

        auto server_peer_id_res = libp2p::peer::PeerId::fromBase58(*server_peer_id_str);
        if (!server_peer_id_res)
        {
            return boost::none;
        }

        return libp2p::peer::PeerInfo{ server_peer_id_res.value(), {server_ma} };
    }
}

PubSubBroadcasterExt::PubSubBroadcasterExt(
    std::shared_ptr<GossipPubSubTopic> pubSubTopic,
    std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
    libp2p::multi::Multiaddress dagSyncerMultiaddress)
    : gossipPubSubTopic_(pubSubTopic)
    , dagSyncer_(dagSyncer)
    , dataStore_(nullptr)
    , dagSyncerMultiaddress_(dagSyncerMultiaddress)
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
                messageQueue_.push(std::make_tuple(std::move(peerId.value()), bmsg.data()));
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
    auto multiaddress = dagSyncerMultiaddress_.getStringAddress();
    bmsg.set_multiaddress(std::string(multiaddress.begin(), multiaddress.end()));
    std::string data(buff.toString());
    bmsg.set_data(data);
    gossipPubSubTopic_->Publish(bmsg.SerializeAsString());
    m_logger->debug("CIDs broadcasted by {}", bmsg.multiaddress());

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
