/**
 * @file       MigrationManager.hpp
 * @brief      Versioned migration manager and migration step interface.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once

#include <memory>
#include <deque>
#include <cstdint>
#include <map>
#include <unordered_map>

#include <boost/asio/io_context.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include "base/logger.hpp"
#include "upnp.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "outcome/outcome.hpp"
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/protocol/common/asio/asio_scheduler.hpp>

namespace sgns
{
    /**
     * @brief Interface for a migration step between two schema versions.
     */
    class IMigrationStep
    {
    public:
        virtual ~IMigrationStep() = default;

        /**
         * @brief Get the version from which the migration starts.
         * @return The source version string.
         */
        virtual std::string FromVersion() const = 0;

        /**
         * @brief Get the version to which the migration applies.
         * @return The target version string.
         */
        virtual std::string ToVersion() const = 0;

        /**
         * @brief Execute the migration logic.
         * @return Outcome of the operation.
         */
        virtual outcome::result<void> Apply() = 0;

        /**
         * @brief   Check if migration is required.
         * @return  bool  true if migration should run; false to skip.
         */
        virtual bool IsRequired() const = 0;
    };

    /**
     * @brief   Executes a sequence of migration steps to update a CRDT store.
     */
    class MigrationManager : public std::enable_shared_from_this<MigrationManager>
    {
    public:
        /**
         * @brief   Factory function to create a MigrationManager and register all known steps.
         * @param   newDb        Shared pointer to the target GlobalDB.
         * @param   ioContext    Shared io_context for both legacy and new DB.
         * @param   pubSub       Shared GossipPubSub instance.
         * @param   graphsync    Shared GraphSync network object.
         * @param   scheduler    Shared libp2p scheduler.
         * @param   generator    Shared RequestIdGenerator for GraphSync.
         * @param   writeBasePath Base filesystem path for legacy/new DB files.
         * @param   base58key    Base58-encoded peer key for legacy DB path.
         * @return  std::shared_ptr<MigrationManager> to the created instance.
         */
        static std::shared_ptr<MigrationManager> New(
            std::shared_ptr<crdt::GlobalDB>                                 newDb,
            std::shared_ptr<boost::asio::io_context>                        ioContext,
            std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
            std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
            std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
            std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
            const std::string                                              &writeBasePath,
            const std::string                                              &base58key );

        /**
         * @brief   Register a migration step.
         * @param   step  IMigrationStep to add.
         */
        void RegisterStep( std::unique_ptr<IMigrationStep> step );

        /**
         * @brief Perform migration from the current version to the target version.
         * @param currentVersion The current schema version.
         * @param targetVersion The desired schema version.
         * @return Outcome of the migration process.
         */
        outcome::result<void> Migrate( const std::string &currentVersion, const std::string &targetVersion );

    private:
        /**
         * @brief   Private default constructor.
         */
        MigrationManager();

        std::deque<std::unique_ptr<IMigrationStep>> steps_;               ///< Queue of registered migration steps.
        base::Logger m_logger = base::createLogger( "MigrationManager" ); ///< Logger instance.
    };

    /**
     * @brief   Migration step for version 0.2.0 to 1.0.0.
     *
     * Copies transactions and proofs from legacy DB (“out” and “in”) into the new CRDT store.
     */
    class Migration0_2_0To1_0_0 : public IMigrationStep
    {
    public:
        /**
         * @brief   Construct the step with all resources needed to open legacy DBs.
         * @param   newDb        Shared pointer to the target GlobalDB.
         * @param   ioContext    Shared io_context for legacy DB access.
         * @param   pubSub       Shared GossipPubSub instance.
         * @param   graphsync    Shared GraphSync network object.
         * @param   scheduler    Shared libp2p scheduler.
         * @param   generator    Shared RequestIdGenerator for GraphSync.
         * @param   writeBasePath Base path for writing legacy DB files.
         * @param   base58key    Base58-encoded peer key to form legacy paths.
         */
        Migration0_2_0To1_0_0( std::shared_ptr<crdt::GlobalDB>                                 newDb,
                               std::shared_ptr<boost::asio::io_context>                        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
                               std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                               const std::string                                              &writeBasePath,
                               const std::string                                              &base58key );

        /**
         * @brief   Get the source version for this step.
         * @return  std::string "0.2.0"
         */
        std::string FromVersion() const override;

        /**
         * @brief   Get the target version for this step.
         * @return  std::string "1.0.0"
         */
        std::string ToVersion() const override;

        /**
         * @brief   Check if migration is required (newDb is empty).
         * @return  bool  true if migration should run; false to skip.
         */
        bool IsRequired() const override;

        /**
         * @brief   Apply the migration: initialize legacy DBs and migrate data.
         * @return  outcome::result<void>  success on completion; failure on error.
         */
        outcome::result<void> Apply() override;

    private:
        /**
         * @brief   Open a legacy GlobalDB given a suffix ("out" or "in").
         * @param   suffix  Suffix string for legacy path.
         * @return  outcome::result<std::shared_ptr<crdt::GlobalDB>>  opened DB or error.
         */
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitLegacyDb( const std::string &suffix );

        /**
         * @brief   Migrate all transactions and proofs from oldDb into newDb.
         * @param   oldDb  Shared pointer to legacy GlobalDB.
         * @param   newDb  Shared pointer to target GlobalDB.
         * @return  outcome::result<void> success on commit; failure otherwise.
         */
        outcome::result<void> MigrateDb( const std::shared_ptr<crdt::GlobalDB> &oldDb,
                                         const std::shared_ptr<crdt::GlobalDB> &newDb );

        std::shared_ptr<crdt::GlobalDB>                                 newDb_;     ///< Target GlobalDB.
        std::shared_ptr<boost::asio::io_context>                        ioContext_; ///< IO context for DB I/O.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub_;    ///< PubSub instance for legacy DB.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync_; ///< GraphSync network.
        std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler_; ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< Request ID generator.
        std::string  writeBasePath_;                                                ///< Base path for writing DB files.
        std::string  base58key_;                                                    ///< Key to build legacy paths.
        base::Logger m_logger = base::createLogger( "MigrationStep" );              ///< Logger for this step.
    };

} // namespace sgns
