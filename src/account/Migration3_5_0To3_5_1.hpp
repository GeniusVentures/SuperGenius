#pragma once

#include "IMigrationStep.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "local_secure_storage/SecureStorage.hpp"
#include <memory>

namespace sgns
{
    class Migration3_5_0To3_5_1 : public IMigrationStep, public std::enable_shared_from_this<Migration3_5_0To3_5_1>
    {
    public:
        Migration3_5_0To3_5_1( std::shared_ptr<boost::asio::io_context>                        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
                               std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                               std::string                                                     writeBasePath,
                               std::string                                                     base58key );

        std::string           FromVersion() const override;
        std::string           ToVersion() const override;
        outcome::result<void> Init() override;
        outcome::result<void> Apply() override;
        outcome::result<void> ShutDown() override;
        outcome::result<bool> IsRequired() const override;

    private:
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitLegacyDb() const;

        base::Logger logger_ = base::createLogger( "MigrationStep" ); ///< Logger for this step.

        std::shared_ptr<boost::asio::io_context>                        ioContext_; ///< IO context for DB I/O.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub_;    ///< PubSub instance for legacy DB.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync_; ///< GraphSync network.
        std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler_; ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< Request ID generator.
        std::string writeBasePath_;                                                 ///< Base path for writing DB files.
        std::string base58key_;                                                     ///< Key to build legacy paths.
        
        std::shared_ptr<crdt::GlobalDB> db_3_5_0_; ///< Legacy DB
        std::shared_ptr<sgns::JSONSecureStorage> json_storage_;
        std::shared_ptr<sgns::SecureStorageImpl> secure_storage_;
    };
}
