/**
 * @file       AccountPubSub.hpp
 * @brief      Header file of the pubsub used for account communications
 * @date       2025-03-03
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _ACCOUNT_PUBSUB_HPP_
#define _ACCOUNT_PUBSUB_HPP_
#include <string>
#include <memory>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

namespace sgns
{
    class AccountPubSub : public std::enable_shared_from_this<AccountPubSub>
    {
    public:
        std::shared_ptr<AccountPubSub> New( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
                                            std::string                                      account_address );
        ~AccountPubSub() = default;

    private:
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>      pubsub_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> account_channel_;
        base::Logger                                          logger_ = base::createLogger( "AccountPubSub" );

        AccountPubSub( std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub, std::string account_address );
        void OnAccountMessage( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message );
    };
}

#endif
