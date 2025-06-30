/**
 * @file       Migration0_2_0To1_0_0.hpp
 * @brief      Versioned migration manager and migration step interface.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once

#include <memory>
#include <string>
#include <deque>
#include <cstdint>

#include <boost/asio/io_context.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include "base/logger.hpp"
#include "upnp.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "outcome/outcome.hpp"
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/protocol/common/asio/asio_scheduler.hpp>

#include "IMigrationStep.hpp"

namespace sgns
{
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
         * @param   newDb         Shared pointer to the target GlobalDB.
         * @param   ioContext     Shared io_context for legacy DB access.
         * @param   pubSub        Shared GossipPubSub instance.
         * @param   graphsync     Shared GraphSync network object.
         * @param   scheduler     Shared libp2p scheduler.
         * @param   generator     Shared RequestIdGenerator for GraphSync.
         * @param   writeBasePath Base path for writing legacy DB files.
         * @param   base58key     Base58-encoded peer key to form legacy paths.
         */
        Migration0_2_0To1_0_0( std::shared_ptr<crdt::GlobalDB>                                 newDb,
                               std::shared_ptr<boost::asio::io_context>                        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
                               std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                               std::string                                                     writeBasePath,
                               std::string                                                     base58key );

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
         * @brief   Check if this migration should run.
         * @return  outcome::result<bool>  true if migration should run; false to skip. On error, returns failure.
         */
        outcome::result<bool> IsRequired() const override;

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
        outcome::result<uint32_t> MigrateDb( const std::shared_ptr<crdt::GlobalDB> &oldDb,
                                             const std::shared_ptr<crdt::GlobalDB> &newDb );

        std::shared_ptr<crdt::GlobalDB>                                 newDb_;     ///< Target GlobalDB.
        std::shared_ptr<boost::asio::io_context>                        ioContext_; ///< IO context for DB I/O.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub_;    ///< PubSub instance for legacy DB.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync_; ///< GraphSync network.
        std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler_; ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< Request ID generator.
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction_; ///< CRDT transaction to make it all atomic
        std::string                              writeBasePath_;    ///< Base path for writing DB files.
        std::string                              base58key_;        ///< Key to build legacy paths.
        std::vector<std::string>                 topics_;
        base::Logger m_logger = base::createLogger( "MigrationStep" ); ///< Logger for this step.
    };
} // namespace sgns
