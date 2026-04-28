/**
 * @file       GeniusAccount.hpp
 * @brief      Header file of the Genius account class
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>

#include <boost/multiprecision/cpp_int.hpp>

#include <ProofSystem/ElGamalKeyGenerator.hpp>
#include <ProofSystem/EthereumKeyGenerator.hpp>

#include "account/TokenID.hpp"
#include "base/logger.hpp"
#include "local_secure_storage/ISecureStorage.hpp"
#include "outcome/outcome.hpp"
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

namespace sgns
{
    using namespace boost::multiprecision;

    base::Logger genius_account_logger();
    void         set_genius_account_logger( const base::Logger &logger );

    namespace ipfs_pubsub
    {
        class GossipPubSub;
    }

    namespace crdt
    {
        class GlobalDB;
    }
    class AccountMessenger;

    class GeniusAccount : public std::enable_shared_from_this<GeniusAccount>
    {
    public:
        static const std::array<uint8_t, 32> ELGAMAL_PUBKEY_PREDEFINED;      ///< Predefined ElGamal public key
        static constexpr uint64_t            NONCE_CACHE_DURATION_MS = 5000; ///< Cache nonce results for 5 seconds

        /**
         * @brief       Factory constructor of new GeniusAccount
         * @param[in]   token_id Token ID of the account
         * @param[in]   eth_private_key Ethereum private key in hex format (0x...)
         * @return      Valid pointer if succeeds, nullptr otherwise
         */
        static std::shared_ptr<GeniusAccount> New( TokenID               token_id,
                                                   const char           *eth_private_key,
                                                   boost::filesystem::path base_path,
                                                   bool                  full_node = false );

        /**
         * @brief       Initialize the messenger for the account
         * @param[in]   pubsub pubsub instance
         * @return      true if succeeds, false otherwise
         */
        bool InitMessenger( std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub );

        /**
         * @brief       Configures the block response handler
         * @param[in]   broadcaster: the pubsub broadcaster which adds the block CID to be fetched
         * @return      true if successfully configured, false otherwise
         */
        bool ConfigureMessengerHandlers( std::shared_ptr<crdt::GlobalDB> global_db );

        /**
         * @brief       Destroy the Genius Account object
         */
        ~GeniusAccount();

        /**
         * @brief       Get the Address object
         * @return      String representation of the address
         */
        [[nodiscard]] std::string GetAddress() const;

        /**
         * @brief       Get the account's token
         * @return      The token of the account
         */
        [[nodiscard]] TokenID GetToken() const;

        void set_genius_account_logger( const base::Logger &logger )
        {
            sgns::set_genius_account_logger( logger );
        }

        void SetLogger( const base::Logger &logger )
        {
            sgns::set_genius_account_logger( logger );
        }

        /**
         * @brief       Get the confirmed nonce as a string
         * @return      The confirmed nonce in string format
         */
        [[nodiscard]] std::string GetNonce() const
        {
            return std::to_string( GetProposedNonce() );
        }

        /**
         * @brief       Verify a signature using the Genius account's public key
         * @param[in]   address public address to verify the signature
         * @param[in]   sig signature to be verified
         * @param[in]   data data to be verified
         * @return      true if the signature is valid, false otherwise
         */
        static bool VerifySignature( std::string address, std::string sig, std::vector<uint8_t> data );

        /**
         * @brief       Sign data using the Genius account's private key
         * @param[in]   data data to be signed
         * @return      the signature as a vector of bytes
         */
        std::vector<uint8_t> Sign( std::vector<uint8_t> data );

        /**
         * @brief       Set the local confirmed nonce
         * @param[in]   nonce The nonce value to be set
         */
        void SetLocalConfirmedNonce( uint64_t nonce );

        /**
         * @brief       Set the local confirmed nonce for a peer
         * @param[in]   nonce The nonce value to be set
         * @param[in]   address The address of the peer
         */
        void SetPeerConfirmedNonce( uint64_t nonce, std::string address );

        /**
         * @brief       Rollback the local confirmed nonce for a peer
         * @param[in]   nonce The nonce value to be rolled back to
         * @param[in]   address The address of the peer
         */
        void RollBackPeerConfirmedNonce( uint64_t nonce, std::string address );

        /**
         * @brief       Get the confirmed nonce for a peer
         * @param[in]   address The address of the peer
         * @return      The confirmed nonce of the peer if exists, error otherwise
         */
        outcome::result<uint64_t> GetPeerNonce( std::string address ) const;

        /**
         * @brief       Get the local confirmed nonce
         * @return      The local confirmed nonce if exists, error otherwise
         */
        outcome::result<uint64_t> GetLocalConfirmedNonce() const;

        /**
         * @brief       Get confirmed nonce from the network
         * @param[in]   timeout_ms Timeout in miliseconds to get the confirmed nonce
         * @return      The confirmed nonce if success, error otherwise
         */
        outcome::result<uint64_t> GetConfirmedNonce( uint64_t timeout_ms ) const;

        /**
         * @brief       Get the next available nonce without reserving it
         * @return      The nonce that would be assigned to the next transaction
         */
        uint64_t GetProposedNonce() const;

        /**
         * @brief       Reserve the next available nonce
         * @return      The reserved nonce value
         */
        uint64_t ReserveNextNonce();

        /**
         * @brief       Release a previously reserved nonce
         * @param[in]   nonce The nonce to release
         */
        void ReleaseNonce( uint64_t nonce );

        outcome::result<void> RequestGenesis(
            uint64_t                                            timeout_ms = 8000,
            std::function<void( outcome::result<std::string> )> callback   = nullptr ) const;
        outcome::result<void> RequestAccountCreation(
            uint64_t                                            timeout_ms,
            std::function<void( outcome::result<std::string> )> callback ) const;
        outcome::result<void> RequestRegularBlock(
            uint64_t                                            timeout_ms,
            const std::string                                  &cid,
            std::function<void( outcome::result<std::string> )> callback = nullptr ) const;
        /**
         * @brief       Request heads broadcast for specific topics
         * @param[in]   topics Vector of topic names to request heads for
         * @return      outcome::success if request was sent, error otherwise
         */
        outcome::result<void> RequestHeads( const std::set<std::string> &topics ) const;

        /**
         * @brief       Derives a Genius address from a given Ethereum private key
         * @param[in]   eth_private_key Ethereum private key in hex format (0x...)
         * @param       base_path The base path to store/retrieve the key
         * @return      Pair of ElGamal and Ethereum key generators if succeeds, error otherwise
         */
        static outcome::result<std::pair<std::shared_ptr<ISecureStorage>,
                                         std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>>>
        GenerateGeniusAddress( const char *eth_private_key, boost::filesystem::path base_path );

    protected:
        friend class Blockchain;
        void SetGetBlockChainCIDMethod(
            std::function<outcome::result<std::string>( uint8_t, const std::string & )> method );
        void ClearGetBlockChainCIDMethod();
        void SetHasBlockCidMethod( std::function<outcome::result<bool>( const std::string & )> method );
        void ClearHasBlockCidMethod();

    private:
        static constexpr size_t SIGNATURE_EXP_SIZE = 64; ///< Expected size of the signature in bytes

        TokenID token;         ///< Token ID of the account
        bool    is_full_node_; ///< Whether this account is a full node

        std::shared_ptr<ethereum::EthereumKeyGenerator> eth_keypair_;      ///< Ethereum keypair
        std::shared_ptr<KeyGenerator::ElGamal>          elgamal_address_;  ///< ElGamal keypair
        std::shared_ptr<ISecureStorage>                 storage_;          ///< Secure storage instance
        std::unordered_map<std::string, uint64_t>       confirmed_nonces_; ///< Map of the confirmed nonces from peers
        mutable std::shared_mutex                       nonce_mutex_;      ///< Mutex for the nonce map
        std::set<uint64_t>                              pending_nonces_;   ///< Reserved but not confirmed nonces
        std::optional<uint64_t>                         local_confirmed_nonce_; ///< Highest locally confirmed nonce
        std::shared_ptr<AccountMessenger>               messenger_;             ///< Messenger instance

        // Nonce request tracking
        mutable std::mutex              nonce_request_mutex_; ///< Mutex for nonce request tracking
        mutable std::condition_variable nonce_request_cv_;    ///< Condition variable for waiting on nonce requests
        mutable bool nonce_request_in_progress_;              ///< Flag indicating if a nonce request is in progress
        mutable std::optional<outcome::result<uint64_t>>
            cached_nonce_result_; ///< Cached result from in-progress request
        mutable std::chrono::steady_clock::time_point
                   cached_nonce_timestamp_; ///< Timestamp when cached nonce was obtained
        std::mutex get_cids_mutex_;         ///< Mutex for the genesis method
        std::function<outcome::result<std::string>( uint8_t, const std::string & )>
            get_cids_method_; ///< Function to get blockchain CIDs
        std::function<outcome::result<bool>( const std::string & )> has_cid_method_; ///< Function to check CID presence

        uint64_t GetNextNonceLocked() const;

        /**
         * @brief       Private constructor a new Genius Account object
         * @param[in]   token_id
         */
        GeniusAccount( TokenID token_id, std::shared_ptr<ISecureStorage> storage, bool full_node );
    };
}
