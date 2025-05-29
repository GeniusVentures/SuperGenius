#pragma once

#include <memory>
#include <deque>
#include <cstdint>
#include <map>
#include <unordered_map>

#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/atomic_transaction.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "account/IGeniusTransactions.hpp"
#include "account/TransferTransaction.hpp"
#include "account/MintTransaction.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/ProcessingTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "base/logger.hpp"
#include "base/buffer.hpp"
#include "crypto/hasher.hpp"
#ifdef _PROOF_ENABLED
#include "proof/proto/SGProof.pb.h"
#endif
#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"
#include "upnp.hpp"

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
    };

    /**
     * @brief Handles registration and execution of multiple migration steps.
     */
    class MigrationManager
    {
    public:
        /**
         * @brief Register a new migration step.
         * @param step Unique pointer to the migration step.
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
        std::vector<std::unique_ptr<IMigrationStep>> steps_;
        base::Logger                                 m_logger = sgns::base::createLogger( "MigrationManager" );
    };

    /**
     * @brief Migration step from version 0 to 1.0.0
     */
    class Migration0To1_0_0 : public IMigrationStep
    {
    public:
        /**
         * @brief Construct a new Migration0To1_0_0 object.
         * @param newDb Destination GlobalDB to populate.
         * @param ioContext IO context for async operations.
         * @param pubSub GossipPubSub instance.
         * @param graphsync Graphsync network.
         * @param scheduler Asio scheduler for libp2p.
         * @param generator Graphsync request ID generator.
         * @param basePort Base port for network configuration.
         * @param basePath Base path for DB storage.
         */
        Migration0To1_0_0( std::shared_ptr<crdt::GlobalDB>                                       newDb,
                           std::shared_ptr<boost::asio::io_context>                              ioContext,
                           std::shared_ptr<ipfs_pubsub::GossipPubSub>                            pubSub,
                           std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsync,
                           std::shared_ptr<libp2p::protocol::Scheduler>                          scheduler,
                           std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                           uint16_t                                                              basePort,
                           const std::string                                                    &basePath);

        std::string           FromVersion() const override;
        std::string           ToVersion() const override;
        outcome::result<void> Apply() override;

    private:
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitLegacyDb( const std::string &basePath, uint16_t port );
        outcome::result<void>                            MigrateDb( const std::shared_ptr<crdt::GlobalDB> &oldDb,
                                                                    const std::shared_ptr<crdt::GlobalDB> &newDb );

        std::shared_ptr<crdt::GlobalDB>                                       newDb_;
        std::shared_ptr<boost::asio::io_context>                              ioContext_;
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                            pubSub_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsync_;
        std::shared_ptr<libp2p::protocol::Scheduler>                          scheduler_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_;
        uint16_t                                                              basePort_;
        std::string                                                           basePath_;
    };

} // namespace sgns::migration
