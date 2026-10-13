/**
 * @file       Migration3_4_0To3_5_0.hpp
 * @brief      Migration step that upgrades account data from schema version 3.4.0 to 3.5.0.
 * @date       2025-11-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_MIGRATION_3_4_0_TO_3_5_0_HPP
#define SGNS_MIGRATION_3_4_0_TO_3_5_0_HPP

#include <string>
#include <memory>
#include <atomic>

#include "IMigrationStep.hpp"
#include "account/NodeType.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "outcome/outcome.hpp"
#include "base/logger.hpp"

namespace sgns
{
    class GeniusAccount;

    /**
     * @brief      Migration step for version 3.4.0 to 3.5.0.
     *             Updates persisted data required by the 3.5.0 node layout.
     */
    class Migration3_4_0To3_5_0 : public IMigrationStep, public std::enable_shared_from_this<Migration3_4_0To3_5_0>
    {
    public:
        /**
         * @brief Constructs the migration step with the services required to read and write both database versions.
         * @param[in] ioContext Shared IO context used by GlobalDB and migration services.
         * @param[in] pubSub PubSub service used by the legacy and target GlobalDB instances.
         * @param[in] graphsync GraphSync network used for CRDT data exchange.
         * @param[in] scheduler libp2p scheduler used by GraphSync.
         * @param[in] generator Request ID generator used by GraphSync.
         * @param[in] writeBasePath Base path containing versioned node database directories.
         * @param[in] base58key Base58 node key suffix used to locate the legacy and target databases.
         * @param[in] account Local account used to configure target storage and sign filler transactions.
         */
        Migration3_4_0To3_5_0( std::shared_ptr<boost::asio::io_context>                        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
                               std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                               std::string                                                     writeBasePath,
                               std::string                                                     base58key,
                               std::shared_ptr<GeniusAccount>                                  account,
                               NodeType                                                        node_type );

        /**
         * @brief Destroys the migration step.
         */
        ~Migration3_4_0To3_5_0() override;

        /**
         * @brief Returns the source schema version handled by this step.
         * @return Source version string, "3.4.0".
         */
        std::string FromVersion() const override;

        /**
         * @brief Returns the target schema version produced by this step.
         * @return Target version string, "3.5.0".
         */
        std::string ToVersion() const override;

        /**
         * @brief Initializes migration resources before the step is applied.
         * @return Success after opening the legacy database and, when present, the target database.
         */
        outcome::result<void> Init() override;

        /**
         * @brief Determines whether the 3.4.0 to 3.5.0 migration must run.
         * @return True when the target database is older than 3.5.0 or has no version marker; false otherwise.
         */
        outcome::result<bool> IsRequired() const override;

        /**
         * @brief Applies the migration and writes the upgraded transaction state.
         *
         * Configures account storage against the target database, starts the target blockchain,
         * migrates transactions for all monitored networks, synthesizes zero-value mint
         * transactions for missing local nonces, commits sync topics in batches, and records
         * the target version on success. Transactions that cannot be fetched or validated are skipped.
         *
         * @return Success when the migration is complete or no legacy database exists; failure on database,
         *         blockchain initialization, serialization, or commit errors.
         */
        outcome::result<void> Apply() override;

        /**
         * @brief Releases resources allocated during migration.
         * @return Success after deconfiguring account storage and releasing database and blockchain references.
         */
        outcome::result<void> ShutDown() override;

    private:
        /**
         * @brief Internal status used while waiting for blockchain-backed migration tasks.
         */
        enum class Status
        {
            ST_INIT = 0, ///< Blockchain initialization is pending.
            ST_ERROR,    ///< Blockchain initialization failed.
            ST_SUCCESS,  ///< Blockchain initialization completed successfully.
        };

        /**
         * @brief Opens the legacy 3.4.0 database view.
         * @return Legacy GlobalDB, nullptr when the legacy database path does not exist, or an initialization error.
         */
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitLegacyDb();

        /**
         * @brief Opens the target 3.5.0 database view.
         * @return Target GlobalDB, or an initialization error.
         */
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitTargetDb();

        std::shared_ptr<boost::asio::io_context>             ioContext_; ///< IO context for GlobalDB services.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>           pubSub_;    ///< PubSub service for CRDT sync.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network> graphsync_; ///< GraphSync network.
        std::shared_ptr<libp2p::basic::Scheduler>            scheduler_; ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< GraphSync request ID generator.
        std::string  writeBasePath_;                                                ///< Base path for versioned DBs.
        std::string  base58key_;                                                    ///< Node key suffix for DB paths.
        base::Logger logger_ = base::createLogger( "MigrationStep" );               ///< Logger for this step.
        /// The account must outlive Blockchain, which reads its address in Blockchain::Stop().
        std::shared_ptr<GeniusAccount>  account_;    ///< Local account being migrated.
        /// Deployment role. Forwarded to Blockchain so an Archive does not self-vote during the
        /// several minutes this migration keeps a live consensus manager running.
        NodeType                        node_type_ = NodeType::Light;
        std::shared_ptr<crdt::GlobalDB> db_3_5_0_;   ///< Target 3.5.0 database.
        std::shared_ptr<crdt::GlobalDB> db_3_4_0_;   ///< Legacy 3.4.0 database.
        std::shared_ptr<Blockchain>     blockchain_; ///< Blockchain instance used during migration.
        std::atomic<Status>             blockchain_status_{ Status::ST_INIT }; ///< Async blockchain startup status.
    };

}

#endif // SGNS_MIGRATION_3_4_0_TO_3_5_0_HPP
