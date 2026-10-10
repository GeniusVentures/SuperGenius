/**
 * @file       Migration3_6_0To3_7_0.hpp
 * @brief      Migration step that upgrades account and balance data from schema version 3.6.0 to 3.7.0.
 * @date       2026-05-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_MIGRATION_3_6_0_TO_3_7_0_HPP
#define SGNS_MIGRATION_3_6_0_TO_3_7_0_HPP

#include "IMigrationStep.hpp"
#include "account/NodeType.hpp"
#include "base/logger.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/globaldb/globaldb.hpp"

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace sgns
{
    class GeniusAccount;
    class TransactionManager;

    /**
     * @brief Executes the 3.6.0 to 3.7.0 migration, including legacy balance recovery.
     */
    class Migration3_6_0To3_7_0 : public IMigrationStep, public std::enable_shared_from_this<Migration3_6_0To3_7_0>
    {
    public:
        /**
         * @brief Address and balance pair recovered from the legacy database.
         */
        using AddressBalance = std::pair<std::string, uint64_t>;

        /**
         * @brief Constructs the migration step with access to legacy and target storage plus account services.
         * @param[in] ioContext Shared IO context used by GlobalDB and migration services.
         * @param[in] pubSub PubSub service used by the migrated GlobalDB instances.
         * @param[in] graphsync GraphSync network used for CRDT data exchange.
         * @param[in] scheduler libp2p scheduler used by GraphSync.
         * @param[in] generator Request ID generator used by GraphSync.
         * @param[in] writeBasePath Base path containing versioned node database directories.
         * @param[in] base58key Base58 node key suffix used to locate the legacy and target databases.
         * @param[in] account Local account used to configure storage and submit the migration claim.
         * @param[in] node_type Deployment role of the node during migration.
         */
        Migration3_6_0To3_7_0( std::shared_ptr<boost::asio::io_context>                        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
                               std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                               std::string                                                     writeBasePath,
                               std::string                                                     base58key,
                               std::shared_ptr<GeniusAccount>                                  account,
                               NodeType                                                        node_type );

        /**
         * @brief Returns the source schema version handled by this step.
         * @return Source version string, "3.6.0".
         */
        std::string FromVersion() const override;
        /**
         * @brief Returns the target schema version produced by this step.
         * @return Target version string, "3.7.0".
         */
        std::string ToVersion() const override;
        /**
         * @brief Initializes migration resources before applying the step.
         * @return Success after opening the legacy database and, when present, the target database.
         */
        outcome::result<void> Init() override;
        /**
         * @brief Applies the migration and writes the upgraded balance state.
         *
         * Migrates blockchain CIDs, computes legacy balances, persists the migration allow-list,
         * submits a local migration claim when this account has a legacy balance, and records
         * the target version on success.
         *
         * @return Success when the migration is complete or no legacy database exists; failure on database,
         *         blockchain initialization, transaction confirmation, or serialization errors.
         */
        outcome::result<void> Apply() override;
        /**
         * @brief Releases resources allocated during migration.
         * @return Success after stopping migration services and releasing database references.
         */
        outcome::result<void> ShutDown() override;
        /**
         * @brief Determines whether the 3.6.0 to 3.7.0 migration must run.
         * @return True when the target database is older than 3.7.0 or has no version marker; false otherwise.
         */
        outcome::result<bool> IsRequired() const override;

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
         * @brief Opens the legacy 3.6.0 database view.
         * @return Legacy GlobalDB, nullptr when the legacy database path does not exist, or an initialization error.
         */
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitLegacyDb() const;
        /**
         * @brief Opens the target 3.7.0 database view.
         * @return Target GlobalDB, or an initialization error.
         */
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitTargetDb() const;
        /**
         * @brief Scans the legacy dataset and computes the balances to migrate.
         *
         * Reads legacy UTXO snapshots when present. If no snapshots are found, reconstructs
         * balances from migrated transactions by collecting produced and consumed outpoints.
         *
         * @return Migratable address balances sorted by address, or an error when legacy data cannot be decoded.
         */
        outcome::result<std::vector<AddressBalance>> ComputeLegacyBalances() const;

        base::Logger logger_ = base::createLogger( "MigrationStep" ); ///< Logger for migration progress and errors.

        std::shared_ptr<boost::asio::io_context>             ioContext_; ///< IO context for GlobalDB services.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>           pubSub_;    ///< PubSub service for CRDT sync.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network> graphsync_; ///< GraphSync network.
        std::shared_ptr<libp2p::basic::Scheduler>            scheduler_; ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< GraphSync request ID generator.
        std::string writeBasePath_;                                                 ///< Base path for versioned DBs.
        std::string base58key_;                                                     ///< Node key suffix for DB paths.

        /// The account must outlive Blockchain and TransactionManager, which both
        /// borrow it during teardown (~TransactionManager clears the account's CID method;
        /// Blockchain::Stop reads its address).
        std::shared_ptr<GeniusAccount>      account_;             ///< Local account being migrated.
        std::shared_ptr<crdt::GlobalDB>     db_3_6_0_;            ///< Legacy 3.6.0 database.
        std::shared_ptr<crdt::GlobalDB>     db_3_7_0_;            ///< Target 3.7.0 database.
        std::shared_ptr<Blockchain>         blockchain_;          ///< Blockchain instance used during migration.
        std::shared_ptr<TransactionManager> transaction_manager_; ///< Transaction manager used for the migration claim.
        NodeType                            node_type_ = NodeType::Light;          ///< Deployment role of this node.
        std::atomic<Status>                 blockchain_status_{ Status::ST_INIT }; ///< Async blockchain startup status.
    };
}

#endif // SGNS_MIGRATION_3_6_0_TO_3_7_0_HPP
