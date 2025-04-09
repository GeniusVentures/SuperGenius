#ifndef SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP
#define SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP

#include "crdt/broadcaster.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "base/logger.hpp"
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <queue>

namespace sgns::crdt
{
    class CrdtDatastore;

    class PubSubBroadcasterExt : public Broadcaster, public std::enable_shared_from_this<PubSubBroadcasterExt>
    {
    public:
        using GossipPubSub      = sgns::ipfs_pubsub::GossipPubSub;
        using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

        static std::shared_ptr<PubSubBroadcasterExt> New( std::shared_ptr<GossipPubSubTopic>              pubSubTopic,
                                                          std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer);

        void SetCrdtDataStore( std::shared_ptr<CrdtDatastore> dataStore );

        /**
         * Send {@param buff} payload to other replicas.
         * @return outcome::success on success or outcome::failure on error
         */
        outcome::result<void> Broadcast( const base::Buffer &buff ) override;
        /**
         * Obtain the next {@return} payload received from the network.
         * @return buffer value or outcome::failure on error
        */
        outcome::result<base::Buffer> Next() override;

        /**
         * Initializes the PubSubBroadcasterExt by subscribing to the associated gossip topic
         * to handle incoming messages. Ensures that message processing is set up before
         * CRDT-related operations are invoked.
         */
        void Start();

    private:
        PubSubBroadcasterExt( std::shared_ptr<GossipPubSubTopic>              pubSubTopic,
                              std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer );
        void OnMessage( boost::optional<const GossipPubSub::Message &> message );

        std::shared_ptr<GossipPubSubTopic>                        gossipPubSubTopic_;
        std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer>           dagSyncer_;
        std::shared_ptr<CrdtDatastore>                            dataStore_;
        std::queue<std::tuple<libp2p::peer::PeerId, std::string>> messageQueue_;
        //sgns::base::Logger logger_ = nullptr;
        std::mutex         mutex_;
        sgns::base::Logger m_logger = sgns::base::createLogger( "PubSubBroadcasterExt" );
        // For async subscription thread control
        std::future<void> subscriptionFuture_;
    };
}

#endif // SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP
