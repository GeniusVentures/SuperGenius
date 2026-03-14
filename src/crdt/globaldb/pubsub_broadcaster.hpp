#ifndef SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_HPP
#define SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_HPP

#include "crdt/broadcaster.hpp"
#include "base/logger.hpp"
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <queue>

namespace sgns::crdt
{
    class PubSubBroadcaster : public Broadcaster
    {
    public:
        using GossipPubSub      = sgns::ipfs_pubsub::GossipPubSub;
        using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

        PubSubBroadcaster( std::shared_ptr<GossipPubSubTopic> pubSubTopic );

        void SetLogger( base::Logger logger )
        {
            logger_ = std::move( logger );
        }

        /**
    * Send buffer payload to other replicas.
    * @param buff Buffer containing the data to broadcast.
    * @param topic Topic to broadcast to.
    * @param peerInfo Optional peer info.
    * @return outcome::success on success or outcome::failure on error
    */
        outcome::result<void> Broadcast( const base::Buffer                     &buff,
                                         std::string                             topic,
                                         boost::optional<libp2p::peer::PeerInfo> peerInfo = boost::none ) override;
        /**
    * Obtain the next payload received from the network.
    * @return buffer value or outcome::failure on error
    */
        outcome::result<base::Buffer> Next() override;

        bool HasTopic( const std::string &topic ) override
        {
            return true;
        }

    private:
        std::shared_ptr<GossipPubSubTopic>                        gossipPubSubTopic_;
        std::queue<std::tuple<libp2p::peer::PeerId, std::string>> listOfMessages_;
        base::Logger                                              logger_ = nullptr;
        std::mutex                                                mutex_;
    };
}

#endif // SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_HPP
