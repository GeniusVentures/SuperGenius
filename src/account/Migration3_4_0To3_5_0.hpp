/**
 * @file       Migration3_4_0To3_5_0.hpp
 * @brief      
 * @date       2025-11-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <string>
#include <memory>
#include <atomic>

#include "IMigrationStep.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "outcome/outcome.hpp"
#include "base/logger.hpp"

namespace sgns
{
    class GeniusAccount;

    /**
     * @brief      Migration step for version 1.0.0 to 3.4.0.
     *             Changes the full node topic from CRDT heads 
     */
    class Migration3_4_0To3_5_0 : public IMigrationStep, public std::enable_shared_from_this<Migration3_4_0To3_5_0>
    {
    public:
        Migration3_4_0To3_5_0( std::shared_ptr<boost::asio::io_context>                        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
                               std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                               std::string                                                     writeBasePath,
                               std::string                                                     base58key,
                               std::shared_ptr<GeniusAccount>                                  account );
        ~Migration3_4_0To3_5_0() override;

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
        enum class Status
        {
            ST_INIT = 0,
            ST_ERROR,
            ST_SUCCESS,
        };
        /**
         * @brief   Open a legacy GlobalDB from 1.0.0
         * @return  Opened DB, nullptr if absent, or error.
         */
        outcome::result<std::shared_ptr<crdt::GlobalDB>>                InitLegacyDb();
        outcome::result<std::shared_ptr<crdt::GlobalDB>>                InitTargetDb();
        std::shared_ptr<boost::asio::io_context>                        ioContext_; ///< IO context for DB I/O.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub_;    ///< PubSub instance for legacy DB.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync_; ///< GraphSync network.
        std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler_; ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< Request ID generator.
        std::string                     writeBasePath_;                             ///< Base path for writing DB files.
        std::string                     base58key_;                                 ///< Key to build legacy paths.
        base::Logger                    logger_ = base::createLogger( "MigrationStep" ); ///< Logger for this step.
        std::shared_ptr<crdt::GlobalDB> db_3_5_0_;                                       ///< Target GlobalDB.
        std::shared_ptr<crdt::GlobalDB> db_3_4_0_;                                       ///< Legacy DB
        std::shared_ptr<Blockchain>     blockchain_;
        std::shared_ptr<GeniusAccount>  account_;
        std::atomic<Status>             blockchain_status_{ Status::ST_INIT };
    };

}
