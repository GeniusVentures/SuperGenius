#ifndef SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP
#define SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP

#include "crdt/broadcaster.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "base/logger.hpp"
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <queue>
#include <vector>
#include <future>
#include <unordered_map>
#include <string>

namespace sgns::crdt
{
    class CrdtDatastore;

    class PubSubBroadcasterExt : public Broadcaster, public std::enable_shared_from_this<PubSubBroadcasterExt>
    {
    public:
        using GossipPubSub      = sgns::ipfs_pubsub::GossipPubSub;
        using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

        // Static factory method that accepts a vector of topics.
        static std::shared_ptr<PubSubBroadcasterExt> New(
            const std::vector<std::shared_ptr<GossipPubSubTopic>> &pubSubTopics,
            std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer>        dagSyncer,
            libp2p::multi::Multiaddress                            dagSyncerMultiaddress );

        // Overload for backward compatibility that accepts a single topic.
        static std::shared_ptr<PubSubBroadcasterExt> New( std::shared_ptr<GossipPubSubTopic>              pubSubTopic,
                                                          std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                                                          libp2p::multi::Multiaddress dagSyncerMultiaddress )
        {
            return New( std::vector<std::shared_ptr<GossipPubSubTopic>>{ pubSubTopic },
                        dagSyncer,
                        dagSyncerMultiaddress );
        }

        void SetCrdtDataStore( std::shared_ptr<CrdtDatastore> dataStore );

        /**
         * Sends the payload to other replicas.
         * @return outcome::success on success or outcome::failure on error.
         */
        outcome::result<void> Broadcast( const base::Buffer &buff, std::optional<std::string> topic_name ) override;

        /**
         * Obtains the next payload received from the network.
         * @return buffer value or outcome::failure on error.
         */
        outcome::result<base::Buffer> Next() override;

        /**
         * Initializes the PubSubBroadcasterExt by subscribing to the associated gossip topics
         * to handle incoming messages. Ensures that message processing is set up before
         * CRDT-related operations are invoked.
         */
        void Start();
        void AddTopic(const std::shared_ptr<GossipPubSubTopic> &newTopic);

    private:
        // Constructor now accepts a vector of topics.
        PubSubBroadcasterExt( const std::vector<std::shared_ptr<GossipPubSubTopic>> &pubSubTopics,
                              std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer>        dagSyncer,
                              libp2p::multi::Multiaddress                            dagSyncerMultiaddress );

        void OnMessage( boost::optional<const GossipPubSub::Message &> message );

        std::unordered_map<std::string, std::shared_ptr<GossipPubSubTopic>> topicMap_;
        std::string                                                         defaultTopicString_;
        std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer>                     dagSyncer_;
        std::shared_ptr<CrdtDatastore>                                      dataStore_;
        libp2p::multi::Multiaddress                                         dagSyncerMultiaddress_;
        std::queue<std::tuple<libp2p::peer::PeerId, std::string>>           messageQueue_;
        std::mutex                                                          mutex_;
        sgns::base::Logger m_logger = sgns::base::createLogger( "PubSubBroadcasterExt" );
        std::future<void>  subscriptionFuture_;
        std::vector<std::future<libp2p::protocol::Subscription>> subscriptionFutures_;
    };
}

#endif // SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP
