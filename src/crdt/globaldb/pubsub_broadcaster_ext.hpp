#ifndef SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP
#define SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP

#include "crdt/broadcaster.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "base/logger.hpp"
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <queue>
#include <tuple>
#include <vector>
#include <future>
#include <unordered_map>
#include <string>
#include <optional>
#include <mutex>

namespace sgns::crdt
{
    class CrdtDatastore;

    /**
     * @brief Extended PubSub broadcaster that integrates with a CRDT datastore and Graphsync DAG syncer.
     *
     * Manages multiple gossip topics, broadcasting messages and processing incoming payloads.
     */
    class PubSubBroadcasterExt : public Broadcaster, public std::enable_shared_from_this<PubSubBroadcasterExt>
    {
    public:
        using GossipPubSub      = sgns::ipfs_pubsub::GossipPubSub;
        using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

        /**
         * @brief Factory method to create a broadcaster for multiple topics.
         * @param pubSubTopics Vector of pubsub topic instances to subscribe to.
         * @param dagSyncer    Graphsync DAG syncer for block exchange.
         * @param dagSyncerMultiaddress Multiaddress of the DAG syncer node.
         * @return Shared pointer to the new PubSubBroadcasterExt.
         */
        static std::shared_ptr<PubSubBroadcasterExt> New( std::vector<std::shared_ptr<GossipPubSubTopic>> pubSubTopics,
                                                          std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                                                          libp2p::multi::Multiaddress dagSyncerMultiaddress );

        /**
         * @brief Factory method for a single topic
         * @param pubSubTopic Single pubsub topic instance.
         * @param dagSyncer   Graphsync DAG syncer.
         * @param dagSyncerMultiaddress Multiaddress of the DAG syncer.
         * @return Shared pointer to the new PubSubBroadcasterExt.
         */
        static std::shared_ptr<PubSubBroadcasterExt> New( std::shared_ptr<GossipPubSubTopic>              pubSubTopic,
                                                          std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                                                          libp2p::multi::Multiaddress dagSyncerMultiaddress )
        {
            return New( std::vector<std::shared_ptr<GossipPubSubTopic>>{ pubSubTopic },
                        std::move( dagSyncer ),
                        std::move( dagSyncerMultiaddress ) );
        }

        /**
         * @brief Sets the CRDT datastore used for decoding broadcasts.
         * @param dataStore Shared pointer to the CrdtDatastore instance.
         */
        void SetCrdtDataStore( std::shared_ptr<CrdtDatastore> dataStore );

        /**
         * @brief Sends the given buffer as a broadcast to peers.
         * @param buff       Buffer containing the data to broadcast.
         * @param topic_name Optional specific topic to broadcast on; uses default if not provided.
         * @return outcome::success on successful publish, or outcome::failure on error.
         */
        outcome::result<void> Broadcast( const base::Buffer &buff, std::optional<std::string> topic_name ) override;

        /**
         * @brief Retrieves the next incoming broadcast payload.
         * @return Tuple of buffer and topic string, or outcome::failure if none available.
         */
        outcome::result<std::tuple<base::Buffer, std::string>> Next() override;

        /**
         * @brief Subscribes to all configured topics and starts message processing.
         * Must be called before using Next() to receive incoming messages.
         * @note Ensures message processing is ready before any CRDT operations run.
         */
        void Start();

        /**
         * @brief Adds a new topic and subscribes to its messages.
         * @param newTopic Shared pointer to the new pubsub topic.
         */
        void AddTopic( const std::shared_ptr<GossipPubSubTopic> &newTopic );

        /**
         * @brief Checks whether the given topic is already registered.
         * @param topic Name of the topic to check.
         * @return True if the topic exists, false otherwise.
         */
        bool HasTopic( const std::string &topic ) override;

    private:
        /**
         * @brief Private constructor initializing members with provided topics and syncer.
         *
         * @param pubSubTopics Vector of pubsub topics to subscribe to.
         * @param dagSyncer    Graphsync DAG syncer instance.
         * @param dagSyncerMultiaddress Multiaddress for DAG syncer node.
         */
        PubSubBroadcasterExt( std::vector<std::shared_ptr<GossipPubSubTopic>> pubSubTopics,
                              std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                              libp2p::multi::Multiaddress                     dagSyncerMultiaddress );

        /**
         * @brief Internal handler invoked on incoming pubsub messages.
         * Parses raw data and enqueues it for processing by Next().
         * @param message       Optional received GossipPubSub message.
         * @param incomingTopic Name of the topic on which the message arrived.
         */
        void OnMessage( boost::optional<const GossipPubSub::Message &> message, const std::string &incomingTopic );

        std::unordered_map<std::string, std::shared_ptr<GossipPubSubTopic>>    topicMap_;
        std::string                                                            defaultTopicString_;
        std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer>                        dagSyncer_;
        std::shared_ptr<CrdtDatastore>                                         dataStore_;
        libp2p::multi::Multiaddress                                            dagSyncerMultiaddress_;
        std::queue<std::tuple<libp2p::peer::PeerId, std::string, std::string>> messageQueue_;

        std::mutex queueMutex_; ///< protects messageQueue_
        std::mutex mapMutex_;   ///< protects topicMap_, defaultTopicString_, subscriptionFutures_

        sgns::base::Logger m_logger = sgns::base::createLogger( "PubSubBroadcasterExt" );
        std::vector<std::future<libp2p::protocol::Subscription>> subscriptionFutures_;
    };
}

#endif // SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP
