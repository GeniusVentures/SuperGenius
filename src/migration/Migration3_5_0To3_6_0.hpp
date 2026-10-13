/**
 * @file       Migration3_5_0To3_6_0.hpp
 * @brief      Migration step that upgrades account data from schema version 3.5.1 to 3.6.0.
 * @date       2026-01-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_MIGRATION_3_5_0_TO_3_6_0_HPP
#define SGNS_MIGRATION_3_5_0_TO_3_6_0_HPP

#include "IMigrationStep.hpp"
#include "base/logger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include <memory>

namespace sgns
{
    /**
     * @brief Executes the storage migration from database version 3.5.1 to 3.6.0.
     */
    class Migration3_5_0To3_6_0 : public IMigrationStep
    {
    public:
        /**
         * @brief Constructs the migration step with the services required to read and write both database versions.
         * @param[in] ioContext Shared IO context used by GlobalDB services.
         * @param[in] pubSub PubSub service used by the legacy and target GlobalDB instances.
         * @param[in] graphsync GraphSync network used for CRDT data exchange.
         * @param[in] scheduler libp2p scheduler used by GraphSync.
         * @param[in] generator Request ID generator used by GraphSync.
         * @param[in] writeBasePath Base path containing versioned node database directories.
         * @param[in] base58key Base58 node key suffix used to locate the legacy and target databases.
         */
        Migration3_5_0To3_6_0( std::shared_ptr<boost::asio::io_context>                        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
                               std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                               std::string                                                     writeBasePath,
                               std::string                                                     base58key );

        /**
         * @brief Returns the source schema version handled by this step.
         * @return Source version string, "3.5.1".
         */
        std::string FromVersion() const override;
        /**
         * @brief Returns the target schema version produced by this step.
         * @return Target version string, "3.6.0".
         */
        std::string ToVersion() const override;
        /**
         * @brief Initializes migration resources before the step is applied.
         * @return Success after opening the legacy database and, when present, the target database.
         */
        outcome::result<void> Init() override;
        /**
         * @brief Applies the migration logic and persists the upgraded data.
         *
         * Migrates validator registry CIDs, blockchain CIDs, transaction records for all monitored
         * networks, transaction sync topics, and the target database version marker.
         *
         * @return Success when the migration is complete or no legacy database exists; failure on database,
         *         transaction fetch, serialization, or commit errors.
         */
        outcome::result<void> Apply() override;
        /**
         * @brief Releases any temporary migration resources.
         * @return Success after releasing legacy and target database references.
         */
        outcome::result<void> ShutDown() override;
        /**
         * @brief Determines whether the migration needs to run for the current node state.
         * @return True when the target database is older than 3.6.0 or has no version marker; false otherwise.
         */
        outcome::result<bool> IsRequired() const override;

    private:
        /**
         * @brief Opens the legacy 3.5.1 database view.
         * @return Legacy GlobalDB, nullptr when the legacy database path does not exist, or an initialization error.
         */
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitLegacyDb() const;
        /**
         * @brief Opens the target 3.6.0 database view.
         * @return Target GlobalDB, or an initialization error.
         */
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitTargetDb() const;

        base::Logger logger_ = base::createLogger( "MigrationStep" ); ///< Logger for migration progress and errors.

        std::shared_ptr<boost::asio::io_context>             ioContext_; ///< IO context for GlobalDB services.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>           pubSub_;    ///< PubSub service for CRDT sync.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network> graphsync_; ///< GraphSync network.
        std::shared_ptr<libp2p::basic::Scheduler>            scheduler_; ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< GraphSync request ID generator.
        std::string writeBasePath_;                                                 ///< Base path for versioned DBs.
        std::string base58key_;                                                     ///< Node key suffix for DB paths.

        std::shared_ptr<crdt::GlobalDB> db_3_5_1_; ///< Legacy 3.5.1 database.
        std::shared_ptr<crdt::GlobalDB> db_3_6_0_; ///< Target 3.6.0 database.
    };
}

#endif // SGNS_MIGRATION_3_5_0_TO_3_6_0_HPP
