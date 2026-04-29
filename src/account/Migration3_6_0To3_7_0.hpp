#pragma once

#include "IMigrationStep.hpp"
#include "base/logger.hpp"
#include "crdt/globaldb/globaldb.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace sgns
{
    class Migration3_6_0To3_7_0 : public IMigrationStep, public std::enable_shared_from_this<Migration3_6_0To3_7_0>
    {
    public:
        using AddressBalance = std::pair<std::string, uint64_t>;

        Migration3_6_0To3_7_0( std::shared_ptr<boost::asio::io_context>                        ioContext,
                               std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
                               std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
                               std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
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
        outcome::result<std::shared_ptr<crdt::GlobalDB>> InitTargetDb() const;
        outcome::result<std::vector<AddressBalance>>     ComputeLegacyBalances() const;

        base::Logger logger_ = base::createLogger( "Migration3_6_0To3_7_0" );

        std::shared_ptr<boost::asio::io_context>                        ioContext_;
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub_;
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync_;
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler_;
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_;
        std::string                                                     writeBasePath_;
        std::string                                                     base58key_;

        std::shared_ptr<crdt::GlobalDB> db_3_6_0_;
        std::shared_ptr<crdt::GlobalDB> db_3_7_0_;
    };
}
