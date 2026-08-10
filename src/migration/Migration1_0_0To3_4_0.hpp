/**
 * @file       Migration1_0_0To3_4_0.hpp
 * @brief      Header file for Migration1_0_0To3_4_0 class.
 * @date       2025-10-03
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_MIGRATION_1_0_0_TO_3_4_0_HPP
#define SGNS_MIGRATION_1_0_0_TO_3_4_0_HPP

#include <string>
#include <memory>

#include "IMigrationStep.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "outcome/outcome.hpp"
#include "base/logger.hpp"

namespace sgns
{
    /**
     * @brief      Migration step for version 1.0.0 to 3.4.0.
     *             Changes the full node topic from CRDT heads
     */
    class Migration1_0_0To3_4_0 : public IMigrationStep
    {
    public:
        Migration1_0_0To3_4_0( std::shared_ptr<boost::asio::io_context>                        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
                               std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                               std::string                                                     writeBasePath,
                               std::string                                                     base58key );
        ~Migration1_0_0To3_4_0();

        /**
         * @brief   Get the source version for this step.
         * @return  std::string "1.0.0"
         */
        std::string FromVersion() const override;

        /**
         * @brief   Get the target version for this step.
         * @return  std::string "3.4.0"
         */
        std::string ToVersion() const override;

        outcome::result<void> Init() override;

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

        outcome::result<void> ShutDown() override;

    private:
        /**
         * @brief   Open a legacy GlobalDB from 1.0.0
         * @return  Opened DB, nullptr if absent, or error.
         */
        outcome::result<std::shared_ptr<crdt::GlobalDB>>                InitLegacyDb();
        outcome::result<std::shared_ptr<crdt::GlobalDB>>                InitTargetDb();
        std::shared_ptr<boost::asio::io_context>                        ioContext_; ///< IO context for DB I/O.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub_;    ///< PubSub instance for legacy DB.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync_; ///< GraphSync network.
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler_; ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< Request ID generator.
        std::string                     writeBasePath_;                             ///< Base path for writing DB files.
        std::string                     base58key_;                                 ///< Key to build legacy paths.
        base::Logger                    logger_ = base::createLogger( "MigrationStep" ); ///< Logger for this step.
        std::shared_ptr<crdt::GlobalDB> db_3_4_0_;                                       ///< Target GlobalDB.
        std::shared_ptr<crdt::GlobalDB> db_1_0_0_;                                       ///< Legacy GlobalDB.
    };

}

#endif // SGNS_MIGRATION_1_0_0_TO_3_4_0_HPP
