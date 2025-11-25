/**
 * @file       AccountMessenger.hpp
 * @brief      Header file of the account messenger class
 * @date       2025-07-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once
#include <string>
#include <cstdint>
#include <memory>
#include <functional>
#include <vector>
#include <cstdlib>
#include <future>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <set>
#include <chrono>
#include <boost/optional.hpp>
#include "base/logger.hpp"
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "outcome/outcome.hpp"
#include "account/proto/SGAccountComm.pb.h"

namespace sgns
{
    class AccountMessenger : public std::enable_shared_from_this<AccountMessenger>
    {
    public:
        /**
         * @brief      Account Messenger errors
         */
        enum class Error
        {
            PROTO_DESERIALIZATION = 0, ///< Error in protobuf data deserialization
            PROTO_SERIALIZATION,       ///< Error in protobuf data serialization
            NONCE_REQUEST_IN_PROGRESS, ///< Nonce request already in progress
            NONCE_GET_ERROR,           ///< Nonce couldn't be fetched
            NO_RESPONSE_RECEIVED,      ///< No response received from network
            RESPONSE_WITHOUT_NONCE,    ///< Response received but without nonce data
            GENESIS_REQUEST_ERROR,     ///< Genesis request failed
        };

        /**
         * @brief      Interface methods the user needs to define
         */
        struct InterfaceMethods
        {
            /// @brief Signing method
            std::function<outcome::result<std::vector<uint8_t>>( std::vector<uint8_t> data )> sign_;

            /// @brief Verify signature method
            std::function<outcome::result<bool>( std::string address, std::string sig, std::vector<uint8_t> data )>
                verify_signature_;

            /// @brief Get local nonce method
            std::function<outcome::result<uint64_t>( std::string address )> get_local_nonce_;

            /// @brief Get local genesis block method
            std::function<outcome::result<std::string>( uint8_t block_index, const std::string &address )>
                get_block_cid_;
        };

        // Global block response handler type
        using BlockResponseHandler =
            std::function<bool( const std::string &cid, const std::string &peer_id, const std::string &address )>;

        /**
         * @brief       Factory constructor of new AccountMessenger
         * @param[in]   address Own address 
         * @param[in]   pubsub pubsub instance
         * @param[in]   methods interface methods @ref InterfaceMethods
         * @return      Valid pointer if succeeds, nullptr otherwise
         */
        static std::shared_ptr<AccountMessenger> New( std::string                                address,
                                                      std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                      InterfaceMethods                           methods );
        /**
         * @brief      Destroy the Account Messenger object
         */
        ~AccountMessenger();
        /**
         * @brief       Get the Latest Nonce from the network
         * @param[in]   timeout_ms Timeout in miliseconds to get the latest nonce 
         * @param[in]   silent_time_ms Time tyo wait for subsequential nonce responses after first was received
         * @return      Nonce value if success, error otherwise
         */
        outcome::result<uint64_t> GetLatestNonce( uint64_t timeout_ms, uint64_t silent_time_ms = 150 );

        /**
         * @brief       Request genesis block from the network (retries until timeout)
         * @param[in]   timeout_ms Total timeout in milliseconds to wait for responses
         * @param[in]   callback Function to be called for each CID found (empty string if none)
         * @return      success if at least one response arrives before timeout, error otherwise
         */
        outcome::result<void> RequestGenesis(
            uint64_t timeout_ms,
            std::function<void( outcome::result<std::string> )> callback = nullptr );

        /**
         * @brief       Request account creation from the network and invoke callback with found CIDs
         * @param[in]   timeout_ms Total timeout in milliseconds to wait for responses
         * @param[in]   callback Function to be called for each CID found (signature: void(std::string))
         * @return      success on scheduled request, error otherwise
         */
        outcome::result<void> RequestAccountCreation(
            uint64_t timeout_ms,
            std::function<void( outcome::result<std::string> )> callback );

        /**
         * @brief       Register global block response handler
         * @param[in]   handler Function to call for all block responses
         */
        void RegisterBlockResponseHandler( BlockResponseHandler handler );

        /**
         * @brief      Clears the block response handler
         */
        void ClearBlockResponseHandler();

    private:
        /// Basis of the account receiving topic
        static constexpr std::string_view ACCOUNT_COMM = ".comm";
        /// Basis of the global requests topic
        static constexpr std::string_view REQUESTS_COMM = "SGNUS.BC.Requests.comm";

        const std::string                          address_;            ///< Own address
        const std::string                          account_comm_topic_; ///< Account receiving topic
        const std::string                          requests_topic_;     ///< Global requests topic
        std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub_;             ///< Pubsub instance

        /// Future of the subscription to the receiving topic
        std::shared_future<std::shared_ptr<libp2p::protocol::Subscription>> subs_acc_future_;
        /// Future of the subscription to the requests topic
        std::shared_future<std::shared_ptr<libp2p::protocol::Subscription>> subs_requests_future_;

        std::unordered_map<uint64_t, std::set<uint64_t>> nonce_responses_; ///< All current nonce responses
        std::unordered_map<uint64_t, std::set<std::string>>
            no_nonce_responses_; ///< Addresses that responded with no nonce
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point>
                   first_response_time_;   ///< Timestamp of the first response
        std::mutex nonce_responses_mutex_; ///< Mutex of the nonce_responses_

        // Block responses storage for account/genesis requests
        std::unordered_map<uint64_t, std::set<std::string>> block_responses_; ///< collected block CIDs per req_id
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point>
                   block_first_response_time_; ///< Timestamp of the first block response
        std::mutex block_responses_mutex_;     ///< Mutex protecting block_responses_

        InterfaceMethods methods_; ///< Interface methods

        std::random_device rd_; ///< Random device for request IDs

        /// Global block response handler
        BlockResponseHandler global_block_handler_;
        std::mutex           global_handler_mutex_;

        // Worker thread state
        enum class RequestType
        {
            Nonce,
            Genesis,
            AccountCreation
        };

        struct RequestTask
        {
            RequestType                                         type;
            uint64_t                                            timeout_ms;
            uint64_t                                            silent_time_ms{ 150 };
            uint8_t                                             block_index{ 0 };
            std::function<void( outcome::result<std::string> )> callback;
            std::shared_ptr<std::promise<outcome::result<uint64_t>>> nonce_promise;
        };

        std::thread                     worker_thread_;
        std::mutex                      queue_mutex_;
        std::condition_variable         queue_cv_;
        std::queue<RequestTask>         request_queue_;
        std::atomic<bool>               stop_worker_{ false };

        void WorkerLoop();
        void EnqueueTask( RequestTask task );
        outcome::result<uint64_t> PerformNonceRequest( uint64_t timeout_ms, uint64_t silent_time_ms );
        outcome::result<std::set<std::string>> PerformBlockRequest( uint64_t timeout_ms, uint8_t block_index );

        /**
         * @brief       Private constructor of the Account Messenger 
         * @param[in]   address Own address
         * @param[in]   pubsub pubsub instance
         * @param[in]   methods interface methods
         */
        AccountMessenger( std::string                                address,
                          std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                          InterfaceMethods                           methods );
        /**
         * @brief       Requests the nonce to the network
         * @param[in]   req_id The current request ID
         * @return      Success if requested, error otherwise
         */
        outcome::result<void> RequestNonce( uint64_t req_id );

        /**
         * @brief       Request a block (by index) from the network (no callback)
         * @param[in]   block_index index of the requested block (0 = genesis, 1 = account, ...)
         */
        outcome::result<void> RequestBlock( uint64_t req_id, uint8_t block_index );

        /**
         * @brief       Callback of pubsub message when a response was received
         * @param[in]   message Pubsub messaged to be parsed to proto data type
         */
        void OnResponse( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message );
        /**
         * @brief       Callback of pubsub message when a request was received
         * @param[in]   message Pubsub messaged to be parsed to proto data type
         */
        void OnRequest( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message );
        /**
         * @brief       Sends a pubsub message to a set of topics
         * @param[in]   msg The message to be sent
         * @param[in]   topics the destination topics
         * @return      Success in case it sends, error otherwise
         */
        outcome::result<void> SendAccountMessage( const accountComm::AccountMessage &msg,
                                                  const std::set<std::string>       &topics );

        /**
         * @brief       Handles the Nonce request package
         * @param[in]   req The proto nonce request package
         */
        void HandleNonceRequest( const accountComm::SignedNonceRequest &req );
        /**
         * @brief       Handles the Nonce response package
         * @param[in]   req The proto nonce response package
         */
        void HandleNonceResponse( const accountComm::SignedNonceResponse &resp );

        /**
         * @brief       Handles the Genesis request package
         * @param[in]   req The proto genesis request package
         */
        void HandleBlockRequest( const accountComm::SignedBlockRequest &req );
        /**
         * @brief       Handles the Block response package (calls global handler)
         * @param[in]   resp The proto block response package
         */
        void HandleBlockResponse( const accountComm::SignedBlockResponse &resp );

        /// The logger instance
        base::Logger logger_ = sgns::base::createLogger( "AccountMessenger" );
    };
}

/**
 * @brief       Macro for declaring error handling in the AccountMessenger class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns, AccountMessenger::Error );
