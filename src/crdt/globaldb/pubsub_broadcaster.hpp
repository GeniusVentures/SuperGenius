#ifndef SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_HPP
#define SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_HPP

#include <crdt/broadcaster.hpp>
#include <base/logger.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <queue>

namespace sgns::crdt
{
class PubSubBroadcaster : public Broadcaster
{
public:
    using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
    using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

    PubSubBroadcaster( std::shared_ptr<GossipPubSubTopic> pubSubTopic );

    void SetLogger(const sgns::base::Logger& logger);

    /**
    * Send {@param buff} payload to other replicas.
    * @return outcome::success on success or outcome::failure on error
    */
        outcome::result<void> Broadcast(const base::Buffer& buff) override;
    /**
    * Obtain the next {@return} payload received from the network.
    * @return buffer value or outcome::failure on error
    */
        outcome::result<base::Buffer> Next() override;
private:
    std::shared_ptr<GossipPubSubTopic> gossipPubSubTopic_;
    std::queue<std::tuple<libp2p::peer::PeerId, std::string>> listOfMessages_;
    sgns::base::Logger logger_ = nullptr;
    std::mutex mutex_;
};
}


#endif // SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_HPP