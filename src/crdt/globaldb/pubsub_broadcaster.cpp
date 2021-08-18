#include "pubsub_broadcaster.hpp"

namespace sgns::crdt
{
PubSubBroadcaster::PubSubBroadcaster(const std::shared_ptr<GossipPubSubTopic>& pubSubTopic)
    : gossipPubSubTopic_(pubSubTopic)
{
    if (gossipPubSubTopic_ != nullptr)
    {
        gossipPubSubTopic_->Subscribe([this](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string cid(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                auto peerId = libp2p::peer::PeerId::fromBytes(message->from);
                if (peerId.has_value())
                {
                    std::scoped_lock lock(mutex_);
                    listOfMessages_.push(std::make_tuple(std::move(peerId.value()), std::move(cid)));
                }
            }
        });
    }
}

void PubSubBroadcaster::SetLogger(const sgns::base::Logger& logger)
{ 
    logger_ = logger; 
}

outcome::result<void> PubSubBroadcaster::Broadcast(const base::Buffer& buff)
{
    if (this->gossipPubSubTopic_ == nullptr)
    {
        return outcome::failure(boost::system::error_code{});
    }
    const std::string bCastData(buff.toString());
    gossipPubSubTopic_->Publish(bCastData);
    return outcome::success();
}

outcome::result<base::Buffer> PubSubBroadcaster::Next()
{
    std::scoped_lock lock(mutex_);
    if (listOfMessages_.empty())
    {
        //Broadcaster::ErrorCode::ErrNoMoreBroadcast
        return outcome::failure(boost::system::error_code{});
    }

    std::string strBuffer = std::get<1>(listOfMessages_.front());
    listOfMessages_.pop();

    base::Buffer buffer;
    buffer.put(strBuffer);
    return buffer;
}
}
