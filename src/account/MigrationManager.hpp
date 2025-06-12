/**
 * @file       MigrationManager.hpp
 * @brief      Versioned migration manager.
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
#include <string>

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
     * @brief   Executes a sequence of migration steps to update a CRDT store.
     */
    class MigrationManager : public std::enable_shared_from_this<MigrationManager>
    {
    public:
        /**
         * @brief   Factory function to create a MigrationManager and register all known steps.
         * @param   newDb         Shared pointer to the target GlobalDB.
         * @param   ioContext     Shared io_context for both legacy and new DB.
         * @param   pubSub        Shared GossipPubSub instance.
         * @param   graphsync     Shared GraphSync network object.
         * @param   scheduler     Shared libp2p scheduler.
         * @param   generator     Shared GraphSync request ID generator.
         * @param   writeBasePath Base path for writing DB files.
         * @param   base58key     Key to build legacy paths.
         * @return  std::shared_ptr<MigrationManager> to the created instance.
         */
        static std::shared_ptr<MigrationManager> New(
            std::shared_ptr<crdt::GlobalDB>                                 newDb,
            std::shared_ptr<boost::asio::io_context>                        ioContext,
            std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
            std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
            std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
            std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
            std::string                                                     writeBasePath,
            std::string                                                     base58key );

        /**
         * @brief   Register a migration step.
         * @param   step  IMigrationStep to add.
         */
        void RegisterStep( std::unique_ptr<IMigrationStep> step );

        /**
         * @brief Perform all registered migration steps in sequence.
         * @return Outcome of the migration process.
         */
        outcome::result<void> Migrate();

    private:
        /**
         * @brief   Private default constructor.
         */
        MigrationManager();

        std::deque<std::unique_ptr<IMigrationStep>> steps_;               ///< Queue of registered migration steps.
        base::Logger m_logger = base::createLogger( "MigrationManager" ); ///< Logger instance.
    };
} // namespace sgns
