/**
 * @file       GeniusAccount.hpp
 * @brief      Header file of the Genius account class
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _GENIUS_ACCOUNT_HPP_
#define _GENIUS_ACCOUNT_HPP_
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <shared_mutex>
#include <tuple>
#include <functional>
#include <optional>
#include <set>

#include <ProofSystem/ElGamalKeyGenerator.hpp>
#include <ProofSystem/EthereumKeyGenerator.hpp>

#include "account/GeniusUTXO.hpp"
#include "account/UTXOTxParameters.hpp"
#include "account/TokenID.hpp"
#include "base/logger.hpp"

#include "outcome/outcome.hpp"

namespace sgns
{
    using namespace boost::multiprecision;

    namespace ipfs_pubsub
    {
        class GossipPubSub;
    }

    namespace crdt
    {
        class PubSubBroadcasterExt;
    }
    class AccountMessenger;

    class GeniusAccount : public std::enable_shared_from_this<GeniusAccount>
    {
    public:
        static const std::array<uint8_t, 32> ELGAMAL_PUBKEY_PREDEFINED; ///< Predefined ElGamal public key

        /**
         * @brief       Factory constructor of new GeniusAccount
         * @param[in]   token_id Token ID of the account
         * @param[in]   base_path Base path of the account
         * @param[in]   eth_private_key Ethereum private key in hex format (0x...)
         * @return      Valid pointer if succeeds, nullptr otherwise
         */
        static std::shared_ptr<GeniusAccount> New( TokenID          token_id,
                                                   std::string_view base_path,
                                                   const char      *eth_private_key,
                                                   bool             full_node = false );
        /**
         * @brief       Initialize the messenger for the account
         * @param[in]   pubsub pubsub instance
         * @param[in]   full_node parameter to indicate if the account is a full node
         * @return      true if succeeds, false otherwise
         */
        bool InitMessenger( std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub );

        /**
         * @brief       Configures the block response handler
         * @param[in]   broadcaster: the pubsub broadcaster which adds the block CID to be fetched
         * @return      true if successfully configured, false otherwise
         */
        bool ConfigureBlockResponseHandler( std::shared_ptr<crdt::PubSubBroadcasterExt> broadcaster );

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
         * @brief       Get the account's balance
         * @return      The total balance of the account
         */
        [[nodiscard]] uint64_t GetBalance() const;

        [[nodiscard]] uint64_t GetBalance( const std::string &address ) const;

        /**
         * @brief       Get the accounts balance for a specific token
         * @param[in]   token_id Token ID to get the balance
         * @return      The balance of the account for the specific token
         */
        uint64_t GetBalance( const TokenID token_id ) const;

        uint64_t GetBalance( const TokenID token_id, const std::string &address ) const;

        /**
         * @brief       Get the account's token
         * @return      The token of the account
         */
        [[nodiscard]] TokenID GetToken() const;

        /**
         * @brief       Get the confirmed nonce as a string
         * @return      The confirmed nonce in string format
         */
        [[nodiscard]] std::string GetNonce() const
        {
            return std::to_string( GetProposedNonce() );
        }

        /**
         * @brief       Add a new UTXO to the account
         * @param[in]   new_utxo The new UTXO to be added
         * @return      true if the UTXO was added, false otherwise
         */
        bool PutUTXO( const GeniusUTXO &new_utxo, const std::string &address );

        bool PutUTXO( const GeniusUTXO &new_utxo )
        {
            return PutUTXO( new_utxo, GetAddress() );
        }

        /**
         * @brief       Delete a UTXO from the account
         * @param[in]   utxo_id The ID of the UTXO to be deleted
         */
        void DeleteUTXO( const base::Hash256 &utxo_id, const std::string &address );

        void DeleteUTXO( const base::Hash256 &utxo_id )
        {
            DeleteUTXO( utxo_id, GetAddress() );
        }

        /**
         * @brief       Consume UTXOs from the account
         * @param[in]   infos Vector of UTXO information to be consumed
         * @return      true if all UTXOs were consumed, false otherwise
         */
        bool ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos );

        /**
         * @brief       Get UTXOs for a specific address
         * @param[in]   address The address to get UTXOs for (defaults to current account address)
         * @return      Vector of UTXOs for the address
         */
        std::vector<GeniusUTXO> GetUTXOs( const std::string &address ) const;

        std::vector<GeniusUTXO> GetUTXOs() const
        {
            return GetUTXOs( GetAddress() );
        }

        /**
         * @brief       Set UTXOs for a specific address (replaces existing UTXOs)
         * @param[in]   utxos Vector of UTXOs to set for the address
         * @param[in]   address The address to set UTXOs for
         */
        void SetUTXOs( const std::vector<GeniusUTXO> &utxos, const std::string &address );

        void SetUTXOs( const std::vector<GeniusUTXO> &utxos )
        {
            SetUTXOs( utxos, GetAddress() );
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

        outcome::result<void> RequestGenesis() const;
        outcome::result<void> RequestAccountCreation( uint64_t                           timeout_ms,
                                                      std::function<void( std::string )> callback ) const;

    protected:
        friend class Blockchain;
        void SetGetBlockChainCIDMethod(
            std::function<outcome::result<std::string>( uint8_t, const std::string & )> method );
        void ClearGetBlockChainCIDMethod();

    private:
        static constexpr size_t SIGNATURE_EXP_SIZE = 64; ///< Expected size of the signature in bytes

        TokenID token;         ///< Token ID of the account
        bool    is_full_node_; ///< Whether this account is a full node

        std::unordered_map<std::string, std::vector<GeniusUTXO>> utxos_;       ///< Map of UTXOs by address
        mutable std::shared_mutex                                utxos_mutex_; ///< Mutex for the UTXOs map

        std::shared_ptr<ethereum::EthereumKeyGenerator> eth_keypair;       ///< Ethereum keypair
        std::shared_ptr<KeyGenerator::ElGamal>          elgamal_address;   ///< ElGamal keypair
        std::unordered_map<std::string, uint64_t>       confirmed_nonces_; ///< Map of the confirmed nonces from peers
        mutable std::shared_mutex                       nonce_mutex_;      ///< Mutex for the nonce map
        std::set<uint64_t>                              pending_nonces_;   ///< Reserved but not confirmed nonces
        std::optional<uint64_t>                         local_confirmed_nonce_; ///< Highest locally confirmed nonce
        std::shared_ptr<AccountMessenger>               messenger_;             ///< Messenger instance
        std::mutex                                      get_cids_mutex_;        ///< Mutex for the genesis method
        std::function<outcome::result<std::string>( uint8_t, const std::string & )>
            get_cids_method_; ///< Function to get blockchain CIDs

        uint64_t GetNextNonceLocked() const;

        /**
         * @brief       Private constructor a new Genius Account object
         * @param[in]   token_id
         */
        GeniusAccount( TokenID token_id, bool full_node );

        /**
         * @brief       Derives a Genius address from a given Ethereum private key
         * @param[in]   base_path The base path to store/retrieve the key
         * @param[in]   eth_private_key Ethereum private key in hex format (0x...)
         * @return      Pair of ElGamal and Ethereum key generators if succeeds, error otherwise
         */
        static outcome::result<std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>> GenerateGeniusAddress(
            std::string_view base_path,
            const char      *eth_private_key );
    };
}

#endif
