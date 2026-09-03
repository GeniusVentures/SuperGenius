#ifndef SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP
#define SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP

#include "crdt/broadcaster.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/crdt_datastore.hpp"
#include "base/logger.hpp"
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <functional>
#include <queue>
#include <tuple>
#include <vector>
#include <future>
#include <unordered_map>
#include <string>
#include <optional>
#include <mutex>
#include <condition_variable>

namespace sgns::crdt
{

    /**
     * @brief Extended PubSub broadcaster that integrates with a CRDT datastore and Graphsync DAG syncer.
     *
     * Manages multiple gossip topics, broadcasting messages and processing incoming payloads.
     */
    class PubSubBroadcasterExt : public Broadcaster, public std::enable_shared_from_this<PubSubBroadcasterExt>
    {
    public:
        using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
        ~PubSubBroadcasterExt();

        /**
         * @brief Factory method to create a broadcaster for multiple topics.
         * @param dagSyncer    Graphsync DAG syncer for block exchange.
         * @param pubSub       PubSub instance used to subscribe and publish.
         * @return Shared pointer to the new PubSubBroadcasterExt.
         */
        static std::shared_ptr<PubSubBroadcasterExt> New( std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                                                          std::shared_ptr<GossipPubSub>                   pubSub );

        /**
         * @brief Sends the given buffer as a broadcast to peers.
         * @param buff       Buffer containing the data to broadcast.
         * @param topic      Topic to broadcast to.
         * @param peerInfo   Optional peer info to avoid repeated GetPeerInfo calls.
         * @return outcome::success on successful publish, or outcome::failure on error.
         */
        outcome::result<void> Broadcast( const base::Buffer                     &buff,
                                         std::string                             topic,
                                         boost::optional<libp2p::peer::PeerInfo> peerInfo = boost::none ) override;

        /**
         * @brief Retrieves the next incoming broadcast payload.
         * @return buffer value or outcome::failure on error
         */
        outcome::result<base::Buffer> Next() override;

        /**
         * @brief Blocks until a message is queued or \p timeout elapses.
         * @param timeout Longest time to block.
         */
        void WaitForNext( std::chrono::milliseconds timeout ) override;

        /**
         * @brief Releases any waiter and makes later waits return at once.
         */
        void CancelWait() override;

        /**
         * @brief Subscribes to all configured topics and starts message processing.
         * Must be called before using Next() to receive incoming messages.
         * @note Ensures message processing is ready before any CRDT operations run.
         */
        void Start();

        /**
         * @brief Adds a new topic by name
         * @param topicName Name of the topic to add.
         * @return outcome::success() on success (or if topic already existed), outcome::failure() on error.
         */
        outcome::result<void> AddBroadcastTopic( const std::string &topicName );

        /**
         * @brief  Subscribe to a given topic and store its future.
         * @param  topic  Name of the topic to listen to.
         */
        void AddListenTopic( std::string topic );

        /**
         * @brief Checks whether the given topic is already registered.
         * @param topic Name of the topic to check.
         * @return True if the topic exists, false otherwise.
         */
        bool HasTopic( const std::string &topic ) override;

        /**
         * @brief Get the underlying GraphsyncDAGSyncer instance.
         * @return Shared pointer to the GraphsyncDAGSyncer (as void pointer).
         */
        std::shared_ptr<void> GetDagSyncer() const override
        {
            return dagSyncer_;
        }

        void Stop();

        /**
         * @brief Installs (or replaces) the private-network membership filter
         *        consulted by OnMessage for EVERY inbound gossip message.
         *
         *        When set, a message is dropped before any CID decode, route,
         *        or queueing unless BOTH its declared protobuf peer
         *        (bmsg.peer().id()) AND its transport sender
         *        (Gossip::Message::from) pass the predicate. An empty or
         *        malformed transport `from` is denied under a set filter
         *        (fail-closed -- mirrors
         *        sgns::networkregistry::AuthorizeGossipSender without
         *        including any networkregistry header; layering rule).
         *
         *        With no filter installed, OnMessage is byte-identical to the
         *        pre-filter behavior (public pass-through).
         * @param[in] filter Membership predicate; an empty std::function
         *            behaves like ClearMembershipFilter().
         */
        void SetMembershipFilter( std::function<bool( const libp2p::peer::PeerId & )> filter );

        /**
         * @brief Reports whether a membership filter is currently installed.
         * @return true when OnMessage enforces membership.
         */
        bool HasMembershipFilter() const;

        /**
         * @brief Removes the membership filter, restoring public pass-through
         *        ingest (teardown counterpart of SetMembershipFilter).
         */
        void ClearMembershipFilter();

        bool AddSingleCIDInfo( const std::string &cid, const std::string peer_id, const std::string address );

    private:
        /**
         * @brief Private constructor initializing members with provided topics and syncer.
         *
         * @param dagSyncer    Graphsync DAG syncer instance.
         * @param pubSub       PubSub instance used to subscribe and publish.
         */
        PubSubBroadcasterExt( std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> dagSyncer,
                              std::shared_ptr<GossipPubSub>                   pubSub );

        void OnMessage( boost::optional<const GossipPubSub::Message &> message, const std::string &incomingTopic );

        std::set<std::string>                                     topicsToListen_;
        std::set<std::string>                                     topicsToBroadcast_;
        std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer>           dagSyncer_;
        std::queue<std::tuple<libp2p::peer::PeerId, std::string>> messageQueue_;

        std::shared_ptr<GossipPubSub> pubSub_; ///< Pubsub used to broadcast/receive messages

        std::mutex              queueMutex_;             ///< protects messageQueue_ and wait_cancelled_
        std::condition_variable queueCv_;                ///< signals messageQueue_ arrivals and CancelWait
        bool                    wait_cancelled_ = false; ///< sticky once shut down; guarded by queueMutex_
        std::mutex              listenTopicsMutex_;      ///< protects topicsToListen_
        std::mutex              broadcastTopicsMutex_;   ///< protects topicsToListen_
        std::mutex              subscriptionMutex_;      ///< protects subscriptionFutures_

        /// Membership gate state (15-11): OnMessage snapshots the filter under
        /// this mutex on the pubsub callback threads while the setters run on
        /// node init/teardown -- the mutex is required.
        mutable std::mutex membership_filter_mutex_; ///< protects membership_filter_
        std::function<bool( const libp2p::peer::PeerId & )>
            membership_filter_; ///< set -> inbound gossip requires membership (fail-closed)

        std::atomic_bool        started_;

        sgns::base::Logger m_logger = sgns::base::createLogger( "PubSubBroadcasterExt" );
        std::vector<std::shared_future<std::shared_ptr<ipfs_pubsub::GossipPubSub::Subscription>>> subscriptionFutures_;

        bool AddMultiCIDInfo( const std::vector<CID>                         &cids,
                              const libp2p::peer::PeerId                     &peer_id,
                              const std::vector<libp2p::multi::Multiaddress> &addr_vector );
    };
}

#endif // SUPERGENIUS_CRDT_PUBSUB_BROADCASTER_EXT_HPP
