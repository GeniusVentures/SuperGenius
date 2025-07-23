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
#include "account/proto/SGAccountComm.pb.h"

namespace sgns
{
    class AccountMessenger : public std::enable_shared_from_this<AccountMessenger>
    {
    public:
        enum class Error
        {
            PROTO_DESERIALIZATION = 0,
            PROTO_SERIALIZATION,
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

        static std::shared_ptr<AccountMessenger> New( std::string                                address,
                                                      std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                      InterfaceMethods                           methods,
                                                      bool                                       is_full_node );
        ~AccountMessenger();
        outcome::result<uint64_t> GetLatestNonce( uint64_t timeout_ms );

    private:
        static constexpr std::string_view ACCOUNT_COMM   = ".comm.%02d";
        static constexpr std::string_view FULL_NODE_COMM = "SGNUSFullNode.comm.%02d";

        const std::string                           address_;
        const std::string                           account_comm_topic_;
        const std::string                           full_node_comm_topic_;
        const bool                                  is_full_node_;
        std::shared_ptr<ipfs_pubsub::GossipPubSub>  pubsub_;
        std::future<libp2p::protocol::Subscription> subs_acc_future_;
        std::future<libp2p::protocol::Subscription> subs_full_future_;

        std::optional<uint64_t> current_nonce_request_id_;
        std::promise<uint64_t>  nonce_result_promise_;
        std::mutex              nonce_mutex_;
        InterfaceMethods        methods_;

        AccountMessenger( std::string                                address,
                          std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                          InterfaceMethods                           methods,
                          bool                                       is_full_node );
        outcome::result<void> RequestNonce( uint64_t req_id );

        void OnMessage( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message, const std::string &topic );
        outcome::result<void> SendAccountMessage( const accountComm::AccountMessage &msg );

        void HandleNonceRequest( const accountComm::SignedNonceRequest &req );
        void HandleNonceResponse( const accountComm::SignedNonceResponse &resp );

        base::Logger logger_ = sgns::base::createLogger( "AccountMessenger" );
    };
}

/**
 * @brief       Macro for declaring error handling in the AccountMessenger class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns, AccountMessenger::Error );
