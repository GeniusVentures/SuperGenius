/**
 * @file       GeniusAccount.hpp
 * @brief      Header file of the Genius account class
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_GENIUS_ACCOUNT_HPP
#define SGNS_GENIUS_ACCOUNT_HPP

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <optional>
#include <set>
#include <string_view>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/filesystem/path.hpp>
#include <WalletCore/PrivateKey.h>

#include "account/TokenID.hpp"
#include "account/GeniusSigner.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "local_secure_storage/ISecureStorage.hpp"
#include "outcome/outcome.hpp"
#include "UTXOManager.hpp"

#include <unordered_set>

namespace sgns
{
    using namespace boost::multiprecision;

    class AccountMessenger;
    class TransactionManager;

    class GeniusAccount : public std::enable_shared_from_this<GeniusAccount>
    {
    public:
        using StorageWithAddress = std::pair<std::shared_ptr<ISecureStorage>, GeniusSigner::PrivateKey>;

        static const std::array<uint8_t, 32> ELGAMAL_PUBKEY_PREDEFINED;      ///< Legacy deterministic seed bytes
        static constexpr int64_t             NONCE_CACHE_DURATION_MS = 5000; ///< Cache nonce results for 5 seconds

        /**
         * @brief   Factory function type for creating secure storage instances.
         * @param   identifier  Storage identifier (typically base58-encoded public key).
         * @return  Shared pointer to the created secure storage backend.
         */
        using SecureStorageFactory = std::function<std::shared_ptr<ISecureStorage>( const std::string &identifier )>;

        /**
         * @brief   Sets the secure storage factory used by all GeniusAccount factory methods.
         *
         * By default, the factory creates OS-specific secure storage (Apple Keychain,
         * Linux secret service, etc.).  Tests can override this to inject
         * MemorySecureStorage, eliminating keychain prompts entirely.
         *
         * @param   factory  Factory function; pass nullptr to restore the default.
         */
        static void SetSecureStorageFactory( SecureStorageFactory factory );

        /**
         * @brief   Returns the current secure storage factory (default if none set).
         */
        static const SecureStorageFactory &GetSecureStorageFactory();

        /**
         * @brief       Try creating an account by first loading it from storage,
         * and if failure, create one with @ref NewFromRandomMnemonic
         * @param[in]   token_id Token ID of the account
         * @param[in]   base_path Base path to store/retrieve keys.
         */
        static std::shared_ptr<GeniusAccount> New( TokenID token_id, const boost::filesystem::path &base_path );

        /**
         * @brief Create a fresh account whose key exists only in memory.
         *
         * This path keeps storage in memory and does not persist key material
         * or update the account index.
         */
        static std::shared_ptr<GeniusAccount> NewEphemeral( TokenID token_id );

        /**
         * @brief       Creates an account from an Ethereum private key
         * @param[in]   token_id Token ID of the account.
         * @param[in]   eth_private_key Ethereum private key in hex format (0x...).
         * @param[in]   base_path Base path to store/retrieve keys.
         * @return      Valid pointer if succeeds, nullptr otherwise.
         */
        static std::shared_ptr<GeniusAccount> NewFromPrivateKey( TokenID                        token_id,
                                                                 const char                    *eth_private_key,
                                                                 const boost::filesystem::path &base_path );

        /**
         * @brief Creates an account by loading directly from storage.
         * If the account wasn't previously stored, returns `nullptr`.
         */
        static std::shared_ptr<GeniusAccount> NewFromPublicKey( TokenID token_id, std::string_view public_key );

        /**
         * @brief       Creates an account from a BIP39 mnemonic phrase.
         * @param[in]   token_id Token ID of the account.
         * @param[in]   mnemonic BIP39 mnemonic phrase.
         * @param[in]   base_path Base path to store/retrieve keys.
         * @return      Valid pointer if succeeds, nullptr otherwise.
         */
        static std::shared_ptr<GeniusAccount> NewFromMnemonic( TokenID                        token_id,
                                                               const std::string             &mnemonic,
                                                               const boost::filesystem::path &base_path );

        /**
         * @brief Creates an account with a newly generated random BIP39 mnemonic.
         * @param[in] token_id Token ID of the account.
         * @param[in] base_path Base path to store/retrieve keys.
         * @return Pair of shared account instance (nullptr on failure) and the generated mnemonic phrase.
         */
        static std::pair<std::shared_ptr<GeniusAccount>, std::string> NewFromRandomMnemonic(
            TokenID                        token_id,
            const boost::filesystem::path &base_path );

        static std::vector<std::string> GetAvailableAccounts( const boost::filesystem::path &base_path );

        static outcome::result<void> DeleteAccount( std::string_view               public_address,
                                                    const boost::filesystem::path &base_path );

        /**
         * @brief       Strips the "0x" prefix from an address if present.
         *              Stored addresses are prefix-free; this normalizes user input for lookups.
         * @param[in]   address The address string, optionally prefixed with "0x".
         * @return      A string_view pointing past the "0x" prefix if one was present.
         */
        static std::string_view NormalizeAddress( std::string_view address ) noexcept;

        /**
         * @brief       Validates that a string is a valid 512-bit public key in hex format.
         *              A valid key is exactly 128 hex characters with no prefix.
         * @param[in]   key The key string to validate.
         * @return      true if the key is a valid 128-character hex string, false otherwise.
         */
        static bool IsValidPublicKey( std::string_view key ) noexcept;

        /**
         * @brief       Initialize the messenger for the account
         * @param[in]   pubsub pubsub instance
         * @return      true if succeeds, false otherwise
         */
        bool InitMessenger( std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub );

        /**
         * @brief       Configures database dependencies: nonce store, block response handler,
         *              head request handler, and block CID lookup method.
         * @param[in]   global_db GlobalDB instance used to store fetched block CIDs.
         * @return      true if successfully configured, false otherwise.
         */
        bool ConfigureDatabaseDependencies( std::shared_ptr<crdt::GlobalDB> global_db );

        /**
         * @brief       Clears handlers and methods set by ConfigureDatabaseDependencies.
         */
        void DeconfigureDatabaseDependencies();

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

        /**
         * @brief       Get the proposed (next available) nonce as a string
         * @return      The proposed nonce in string format
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
        static bool VerifySignature( const std::string          &address,
                                     std::string_view            sig,
                                     const std::vector<uint8_t> &data );

        /**
         * @brief       Verify a byte-vector signature using the Genius account's public key.
         * @param[in]   address public address to verify the signature
         * @param[in]   sig signature bytes to be verified
         * @param[in]   data data to be verified
         * @return      true if the signature is valid, false otherwise
         */
        static bool VerifySignature( const std::string          &address,
                                     const std::vector<uint8_t> &sig,
                                     const std::vector<uint8_t> &data );

        /**
         * @brief       Sign data using the Genius account's private key
         * @param[in]   data data to be signed
         * @return      the signature as a vector of bytes
         */
        std::vector<uint8_t> Sign( const std::vector<uint8_t> &data ) const;

        /**
         * @brief       Build signed transaction inputs from UTXOs
         * @param[in]   utxos UTXOs to turn into transaction inputs
         * @return      Signed input descriptors
         */
        std::vector<InputUTXOInfo> CreateInputsFromUTXOs( const std::vector<GeniusUTXO> &utxos ) const;

        /**
         * @brief       Set the confirmed nonce for an address
         * @param[in]   nonce The nonce value to be set
         * @param[in]   address The address whose nonce is being updated
         * @param[in]   tx_hash The confirmed transaction hash. Persisted only for the local address
         */
        void SetPeerConfirmedNonce( uint64_t nonce, const std::string &address, const std::string &tx_hash = "" );

        /**
         * @brief       Rollback the confirmed nonce for the given address.
         *              Also rolls back local confirmed nonce and tx history when the address is local.
         * @param[in]   nonce The nonce value to be rolled back from (only rolls back if it matches current)
         * @param[in]   address The address whose nonce is being rolled back
         */
        void RollBackPeerConfirmedNonce( uint64_t nonce, const std::string &address );

        /**
         * @brief       Get the confirmed nonce for a peer
         * @param[in]   address The address of the peer
         * @return      The confirmed nonce of the peer if exists, error otherwise
         */
        outcome::result<uint64_t> GetPeerNonce( const std::string &address ) const;

        /**
         * @brief       Get the local confirmed nonce
         * @return      The local confirmed nonce if exists, error otherwise
         */
        outcome::result<uint64_t> GetLocalConfirmedNonce() const;

        /**
         * @brief       Get a locally persisted confirmed transaction hash by nonce
         * @param[in]   nonce The confirmed nonce to search for
         * @return      The confirmed transaction hash if it exists, error otherwise
         */
        outcome::result<std::string> GetLocalConfirmedTxHash( uint64_t nonce ) const;

        /**
         * @brief       Get confirmed nonce from the network
         * @param[in]   timeout_ms Timeout in miliseconds to get the confirmed nonce
         * @return      The confirmed nonce if success, error otherwise
         */
        outcome::result<uint64_t> GetConfirmedNonce( uint64_t timeout_ms ) const;

        /**
         * @brief       Fetch the latest nonce from the network without relying on cached values
         * @param[in]   timeout_ms Timeout in miliseconds to get the confirmed nonce
         * @return      Error if no response received, optional nonce if success
         */
        outcome::result<std::optional<uint64_t>> FetchNetworkNonce( uint64_t timeout_ms ) const;

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
        outcome::result<void> RequestValidatorRegistry(
            uint64_t                                            timeout_ms,
            std::function<void( outcome::result<std::string> )> callback ) const;
        outcome::result<void> RequestRegularBlock(
            uint64_t                                            timeout_ms,
            const std::string                                  &cid,
            std::function<void( outcome::result<std::string> )> callback = nullptr ) const;
        outcome::result<void> RequestTransaction(
            uint64_t                                            timeout_ms,
            const std::string                                  &tx_hash,
            std::function<void( outcome::result<std::string> )> callback = nullptr ) const;
        /**
         * @brief       Request UTXOs for a specific address and return the selected response
         * @param[in]   timeout_ms Total timeout in milliseconds to wait for responses
         * @param[in]   address Address to request UTXOs for
         * @param[in]   silent_time_ms Time to wait for subsequent responses after first one
         * @return      Set of UTXO strings based on selection criteria, or error otherwise
         */
        outcome::result<std::unordered_set<std::string>> RequestUTXOs( uint64_t           timeout_ms,
                                                                       const std::string &address,
                                                                       uint64_t           silent_time_ms = 150 ) const;
        /**
         * @brief       Request heads broadcast for specific topics
         * @param[in]   topics Set of topic names to request heads for
         * @return      outcome::success if request was sent, error otherwise
         */
        outcome::result<void> RequestHeads( const std::unordered_set<std::string> &topics ) const;

        static outcome::result<StorageWithAddress> GenerateGeniusAddress( const char *eth_private_key,
                                                                          const boost::filesystem::path &base_path );

        static outcome::result<StorageWithAddress> GenerateGeniusAddress( const TW::PrivateKey          &private_key,
                                                                          const boost::filesystem::path &base_path );

        outcome::result<void> SaveInSecureStorage( const std::string                      &key,
                                                   const ISecureStorage::SecureBufferType &buffer )
        {
            return storage_->Save( key, buffer );
        }

        outcome::result<ISecureStorage::SecureBufferType> LoadFromSecureStorage( const std::string &key )
        {
            return storage_->Load( key );
        }

        const UTXOManager &GetUTXOManager() const
        {
            return utxo_manager_;
        }

        UTXOManager &GetUTXOManager()
        {
            return utxo_manager_;
        }

    protected:
        friend class Blockchain;
        friend class TransactionManager;
        void SetGetBlockChainCIDMethod(
            std::function<outcome::result<std::string>( uint8_t, const std::string & )> method );
        void ClearGetBlockChainCIDMethod();
        void SetHasBlockCidMethod( std::function<outcome::result<bool>( const std::string & )> method );
        void ClearHasBlockCidMethod();
        void SetGetValidatorWeightMethod(
            std::function<outcome::result<std::optional<uint64_t>>( const std::string & )> method );
        void ClearGetValidatorWeightMethod();
        void SetGetTransactionCIDMethod( std::function<outcome::result<std::string>( const std::string & )> method );
        void ClearGetTransactionCIDMethod();
        void SetNonceStore( std::shared_ptr<storage::rocksdb> db );

    private:
        struct ConfirmedTxRecord
        {
            uint64_t    nonce;
            std::string hash;
        };

        static constexpr size_t LOCAL_CONFIRMED_TX_HISTORY_LIMIT = 5;

        static outcome::result<StorageWithAddress> LoadGeniusAccount( const boost::filesystem::path &base_path );

        static outcome::result<StorageWithAddress> LoadGeniusAccount( std::string_view public_key );

        static std::shared_ptr<GeniusAccount> CreateInstanceFromResponse( TokenID            token_id,
                                                                          StorageWithAddress response_value );

        TokenID                         token;    ///< Token ID of the account
        std::shared_ptr<ISecureStorage> storage_; ///< Secure storage instance

        GeniusSigner                              signer_;                ///< In-memory signing identity
        std::unordered_map<std::string, uint64_t> confirmed_nonces_;      ///< Map of the confirmed nonces from peers
        mutable std::shared_mutex                 nonce_mutex_;           ///< Mutex for the nonce map
        std::set<uint64_t>                        pending_nonces_;        ///< Reserved but not confirmed nonces
        std::optional<uint64_t>                   local_confirmed_nonce_; ///< Highest locally confirmed nonce
        std::deque<ConfirmedTxRecord>             local_confirmed_transactions_; ///< Recent local confirmed txs
        std::shared_ptr<AccountMessenger>         messenger_;                    ///< Messenger instance
        UTXOManager                               utxo_manager_;

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
        std::function<outcome::result<std::vector<std::string>>( const std::string & )>
            get_utxos_method_; ///< Function to get UTXOs for an address
        std::function<outcome::result<std::optional<uint64_t>>( const std::string & )>
            get_validator_weight_method_; ///< Function to get validator weight for an address
        std::function<outcome::result<std::string>( const std::string & )>
                                          get_transaction_cid_method_; ///< Function to get transaction CID by hash
        mutable std::mutex                nonce_db_mutex_;             ///< Mutex for nonce database pointer access
        std::shared_ptr<storage::rocksdb> nonce_db_;                   ///< RocksDB for nonce persistence

        static constexpr std::string_view NONCE_KEY_PREFIX                      = "gnus-confirmed-nonce-";
        static constexpr std::string_view LOCAL_CONFIRMED_TX_HISTORY_KEY_PREFIX = "gnus-local-confirmed-tx-history-";

        void               LoadConfirmedNonces();
        void               PersistConfirmedNonce( const std::string &address, uint64_t nonce );
        static std::string SerializeConfirmedTxHistory( const std::deque<ConfirmedTxRecord> &history );
        static std::deque<ConfirmedTxRecord> DeserializeConfirmedTxHistory( const std::string &serialized );
        void UpdateLocalConfirmedTxHistoryLocked( uint64_t nonce, const std::string &tx_hash );
        void RollbackLocalConfirmedTxHistoryLocked( uint64_t nonce );

        uint64_t GetNextNonceLocked() const;

        /**
         * @brief       Private constructor for a new GeniusAccount.
         * @param[in]   signer In-memory signing identity.
         * @param[in]   token_id Token ID for the account.
         * @param[in]   storage Secure storage instance.
         */
        GeniusAccount( GeniusSigner signer, TokenID token_id, std::shared_ptr<ISecureStorage> storage );
    };
}

#endif // SGNS_GENIUS_ACCOUNT_HPP
