/**
 * @file       MigrationManager.hpp
 * @brief      Versioned migration manager.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef SGNS_MIGRATION_MANAGER_HPP
#define SGNS_MIGRATION_MANAGER_HPP

#include <memory>
#include <deque>
#include <atomic>
#include <string>

#include <boost/asio/io_context.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include "base/logger.hpp"
#include "outcome/outcome.hpp"
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>

#include "IMigrationStep.hpp"
#include "account/NodeType.hpp"

namespace sgns
{
    class GeniusAccount;

    /**
     * @brief   Executes a sequence of migration steps to update a CRDT store.
     */
    class MigrationManager
    {
    public:
        enum class Error : uint8_t
        {
            BLOCKCHAIN_INIT_FAILED = 1,
        };
        /**
         * @brief   Factory function to create a MigrationManager and register all known steps.
         * @param   ioContext     Shared io_context for both legacy and new DB.
         * @param   pubSub        Shared GossipPubSub instance.
         * @param   graphsync     Shared GraphSync network object.
         * @param   scheduler     Shared libp2p scheduler.
         * @param   generator     Shared GraphSync request ID generator.
         * @param   writeBasePath Base path for writing DB files.
         * @param   base58key     Key to build legacy paths.
         * @param   account       GeniusAccount used during migration (if required).
         * @return  std::shared_ptr<MigrationManager> to the created instance.
         */
        static std::shared_ptr<MigrationManager> New(
            std::shared_ptr<boost::asio::io_context>                        ioContext,
            std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
            std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
            std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
            std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
            std::string                                                     writeBasePath,
            std::string                                                     base58key,
            std::shared_ptr<GeniusAccount>                                  account,
            NodeType                                                        node_type );

        /**
         * @brief   Register a migration step.
         * @param   step  IMigrationStep to add.
         */
        void RegisterStep( std::shared_ptr<IMigrationStep> step );

        /**
         * @brief Perform all registered migration steps in sequence.
         * @return Outcome of the migration process.
         */
        outcome::result<void> Migrate();

        /// @return 1-based index of the migration step currently being processed, or 0 if not started.
        size_t GetCurrentStepIndex() const
        {
            return current_step_index_.load();
        }

        /// @return Total number of registered migration steps.
        size_t GetTotalSteps() const
        {
            return total_steps_;
        }

        /// @return Human-readable description of the current migration step.
        std::string GetCurrentStepDescription() const;

        static constexpr std::string_view VERSION_INFO_KEY = "kSGNSCRDTVersion";

    private:
        /**
         * @brief   Private default constructor.
         */
        MigrationManager();

        std::deque<std::shared_ptr<IMigrationStep>> steps_;               ///< Queue of registered migration steps.
        base::Logger m_logger = base::createLogger( "MigrationManager" ); ///< Logger instance.

        std::atomic<size_t> current_step_index_{ 0 }; ///< 1-based index of the step currently being migrated.
        size_t              total_steps_{ 0 };        ///< Total number of steps (set before migration begins).
    };
} // namespace sgns

OUTCOME_HPP_DECLARE_ERROR_2( sgns, MigrationManager::Error );

#endif // SGNS_MIGRATION_MANAGER_HPP
