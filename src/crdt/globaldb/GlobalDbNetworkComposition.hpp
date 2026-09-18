#ifndef SUPERGENIUS_CRDT_GLOBALDB_NETWORK_COMPOSITION_HPP
#define SUPERGENIUS_CRDT_GLOBALDB_NETWORK_COMPOSITION_HPP

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>


#include "base/logger.hpp"
#include "outcome/outcome.hpp"

namespace libp2p::basic
{
    class Scheduler;
}

namespace sgns::ipfs_lite::ipfs::graphsync
{
    class Network;
    class RequestIdGenerator;
}

namespace sgns::ipfs_pubsub
{
    class GossipPubSub;
}

namespace sgns::crdt
{
    class GlobalDB;

    /**
     * @brief Owns the minimal production networking stack required by GlobalDB.
     *
     * This composition is intentionally independent from GeniusNode and account
     * ownership. Its libp2p identity is stored beneath the configured database
     * path and is used only for CRDT transport.
     */
    class GlobalDbNetworkComposition
    {
    public:
        struct Config
        {
            std::string  network_config_path;
            std::string  database_path;
            std::string  listen_topic;
            std::string  broadcast_topic;
            base::Logger logger;
        };

        enum class Error : uint8_t
        {
            INVALID_CONFIG = 0,
            NETWORK_CONFIG_IO,
            NETWORK_CONFIG_PARSE,
            KEYPAIR_LOAD_FAILED,
            PUBSUB_START_FAILED,
            GLOBALDB_CREATE_FAILED,
            TOPIC_CONFIGURATION_FAILED,
        };

        /**
         * @brief Validates configuration and creates a stopped composition.
         * @param config Network configuration, datastore path, existing CRDT
         *               topics, and logger.
         */
        static outcome::result<std::shared_ptr<GlobalDbNetworkComposition>> Create( Config config );

        GlobalDbNetworkComposition( const GlobalDbNetworkComposition & )            = delete;
        GlobalDbNetworkComposition &operator=( const GlobalDbNetworkComposition & ) = delete;
        ~GlobalDbNetworkComposition();

        /** @brief Starts PubSub and GlobalDB; GraphSync runs on PubSub's io_context. */
        outcome::result<void> Start();

        /** @brief Stops and releases owned resources in reverse dependency order. */
        void Stop();

        /** @brief Returns the running GlobalDB instance, or nullptr before Start/after Stop. */
        std::shared_ptr<GlobalDB> db() const;

        /** @brief Returns the running PubSub interface address for bootstrap configuration. */
        std::string interface_address() const;

    private:
        struct NetworkConfig
        {
            uint16_t                 pubsub_port{ 0 };
            std::string              pubsub_bind_address{ "0.0.0.0" };
            std::vector<std::string> bootstrap_addresses;
            int                      high_water{ 300 };
            int                      low_water{ 150 };
        };

        GlobalDbNetworkComposition( Config config, NetworkConfig network_config );
        static outcome::result<NetworkConfig> LoadNetworkConfig( const std::string &path, const base::Logger &logger );

        Config        config_;
        NetworkConfig network_config_;

        mutable std::mutex mutex_;
        bool               started_{ false };

        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubsub_;
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler_;
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync_network_;
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> request_id_generator_;
        std::shared_ptr<GlobalDB>                                       db_;
    };
} // namespace sgns::crdt

OUTCOME_HPP_DECLARE_ERROR_2( sgns::crdt, GlobalDbNetworkComposition::Error );

#endif // SUPERGENIUS_CRDT_GLOBALDB_NETWORK_COMPOSITION_HPP
