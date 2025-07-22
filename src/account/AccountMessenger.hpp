/**
 * @file       AccountMessenger.hpp
 * @brief      
 * @date       2025-07-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <string>
#include <memory>
#include <optional>
#include <functional>
#include <vector>
#include <cstdlib>
#include <future>
#include <boost/optional.hpp>
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "outcome/outcome.hpp"


namespace sgns
{
    class AccountMessenger : public std::enable_shared_from_this<AccountMessenger>
    {
    private:
        const std::string                                address_;
        const std::string                                account_comm_topic_;
        const std::string                                full_node_comm_topic_;
        const bool                                       is_full_node_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub_;
        std::future<libp2p::protocol::Subscription>      subs_acc_future_;
        std::future<libp2p::protocol::Subscription>      subs_full_future_;

        std::optional<uint64_t> current_nonce_request_id_;
        std::promise<uint64_t>  nonce_result_promise_;
        std::mutex              nonce_mutex_;

        enum class Error
        {
            PROTO_DESERIALIZATION = 0,
            NONCE_REQUEST_IN_PROGRESS,
            NONCE_FUTURE_ERROR,
            NONCE_GET_ERROR,
        };

        struct InterfaceMethods
        {
            std::function<std::vector<uint8_t>( std::vector<uint8_t> data )>                       sign_;
            std::function<bool( std::string address, std::string sig, std::vector<uint8_t> data )> verify_signature_;
            std::function<uint64_t()>                                                              get_local_nonce_;
        };

        InterfaceMethods methods_;

        AccountMessenger( std::string                                      address,
                          std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub,
                          InterfaceMethods                                 methods,
                          bool                                             is_full_node );

        void OnMessage( boost::optional<const GossipPubSub::Message &> message, const std::string &topic );
        outcome::result<void> SendMessage( const base::Buffer &buff );

        void HandleNonceRequest( const account::NonceRequest &req );
        void HandleNonceResponse( const account::NonceResponse &resp );

    public:
        static constexpr std::string_view        ACCOUNT_COMM   = ".comm.%02d";
        static constexpr std::string_view        FULL_NODE_COMM = "SGNUSFullNode.comm.%02d";
        static std::shared_ptr<AccountMessenger> New( std::string                                      address,
                                                      std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub,
                                                      InterfaceMethods methods bool                    is_full_node );
        ~AccountMessenger();
        outcome::result<uint64_t> GetLatestNonce( /* timeout maybe?*/ ); //TODO
        outcome::result<void>     RequestNonce();                        //TODO
    };
}

/**
 * @brief       Macro for declaring error handling in the AccountMessenger class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns, AccountMessenger::Error );
