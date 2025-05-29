/**
 * @file MigrationManager.hpp
 * @brief Versioned migration manager and migration step interface.
 * @date 2025-05-29
 * @author Luiz Guilherme
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <utility>

#include <boost/asio/io_context.hpp>
#include <ipfs_pubsub/GossipPubSubTopic.hpp>
#include <upnp/UPNP.hpp>
#include <crdt/GlobalDB.hpp>
#include <outcome/result.hpp>

namespace sgns
{
    namespace migration
    {

        /**
         * @brief Defines a single migration step between two versions.
         */
        class IMigrationStep
        {
        public:
            virtual ~IMigrationStep() = default;

            /**
             * @brief Get the source version identifier.
             * @return Version string from which this step migrates (e.g. "0").
             */
            virtual std::string FromVersion() const = 0;

            /**
             * @brief Get the target version identifier.
             * @return Version string to which this step migrates (e.g. "1.0.0").
             */
            virtual std::string ToVersion() const = 0;

            /**
             * @brief Execute the migration logic for this step.
             * @return outcome::result<void> indicating success or error.
             */
            virtual outcome::result<void> Apply() = 0;
        };

        /**
         * @brief Manages and runs a sequence of migration steps.
         */
        class MigrationManager
        {
        public:
            /**
             * @brief Register a new migration step.
             * @param step Unique pointer to an IMigrationStep implementation.
             */
            void RegisterStep( std::unique_ptr<IMigrationStep> step );

            /**
             * @brief Migrate from one version to another by applying registered steps.
             * @param currentVersion The starting version identifier.
             * @param targetVersion The desired version identifier.
             * @return outcome::result<void> indicating overall success or error.
             */
            outcome::result<void> Migrate( const std::string &currentVersion, const std::string &targetVersion );

        private:
            std::vector<std::unique_ptr<IMigrationStep>> steps_;
        };

        /**
         * @brief Concrete migration from version 0 to version 1.0.0.
         */
        class Migration0To1_0_0 : public IMigrationStep
        {
        public:
            /**
             * @brief Construct the migration step.
             * @param newDb Shared ptr to the new unified GlobalDB.
             * @param ioContext Shared ptr to Boost ASIO io_context.
             * @param pubSub Shared ptr to the IPFS pubsub topic.
             * @param upnp Shared ptr to the UPnP manager.
             * @param basePort Base port number for legacy DBs.
             * @param basePath File system path prefix for legacy DBs.
             */
            Migration0To1_0_0( std::shared_ptr<crdt::GlobalDB>                 newDb,
                               std::shared_ptr<boost::asio::io_context>        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSubTopic> pubSub,
                               std::shared_ptr<upnp::UPNP>                     upnp,
                               uint16_t                                        basePort,
                               const std::string                              &basePath );

            /**
             * @copydoc IMigrationStep::FromVersion
             */
            std::string FromVersion() const override;

            /**
             * @copydoc IMigrationStep::ToVersion
             */
            std::string ToVersion() const override;

            /**
             * @copydoc IMigrationStep::Apply
             */
            outcome::result<void> Apply() override;

        private:
            /**
             * @brief Initialize a legacy GlobalDB instance.
             * @param ioContext ASIO context for network operations.
             * @param pubSub PubSub topic for the DB.
             * @param upnp UPnP manager for port mapping.
             * @param port TCP port on which to open the DB.
             * @param basePath File path prefix for the DB.
             * @param suffix Suffix to append to basePath (e.g. "out" or "in").
             * @return Shared ptr to initialized GlobalDB or error.
             */
            static outcome::result<std::shared_ptr<crdt::GlobalDB>> InitLegacyDb(
                std::shared_ptr<boost::asio::io_context>        ioContext,
                std::shared_ptr<ipfs_pubsub::GossipPubSubTopic> pubSub,
                std::shared_ptr<upnp::UPNP>                     upnp,
                uint16_t                                        port,
                const std::string                              &basePath,
                const std::string                              &suffix );

            /**
             * @brief Migrate all key/value pairs from an old DB into the new DB.
             * @param oldDb Shared ptr to the legacy GlobalDB.
             * @param newDb Shared ptr to the new unified GlobalDB.
             * @return outcome::result<void> indicating success or error.
             */
            static outcome::result<void> MigrateDb( const std::shared_ptr<crdt::GlobalDB> &oldDb,
                                                    const std::shared_ptr<crdt::GlobalDB> &newDb );

            std::shared_ptr<crdt::GlobalDB>                 newDb_;
            std::shared_ptr<boost::asio::io_context>        ioContext_;
            std::shared_ptr<ipfs_pubsub::GossipPubSubTopic> pubSub_;
            std::shared_ptr<upnp::UPNP>                     upnp_;
            uint16_t                                        basePort_;
            std::string                                     basePath_;
        };

    } // namespace migration
} // namespace sgns
