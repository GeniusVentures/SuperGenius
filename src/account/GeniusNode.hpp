/**
 * @file       GeniusNode.hpp
 * @brief      Top-level node orchestration API for account, transaction, blockchain, and processing services.
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _GENIUS_NODE_HPP_
#define _GENIUS_NODE_HPP_

#include <chrono>
#include <memory>
#include <cstdint>
#include <functional>
#include <vector>
#include <thread>
#include <optional>

#include <boost/asio.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>

#include "account/GeniusAccount.hpp"
#include "base/buffer.hpp"
#include "account/TransactionManager.hpp"
#include "account/BridgeRelayer.hpp"
#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include "crypto/hasher/hasher_impl.hpp"
#include "processing/impl/processing_core_impl.hpp"
#include "processing/impl/processing_subtask_result_storage_impl.hpp"
#include "processing/processing_service.hpp"
#include "singleton/IComponent.hpp"
#include "processing/processing_task_queue.hpp"
#include "coinprices/coinprices.hpp"
#include "blockchain/Blockchain.hpp"
#include <boost/algorithm/string/replace.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <processingbase/ProcessingManager.hpp>

/**
 * @brief Runtime configuration values used to bootstrap a Genius node instance.
 */
typedef struct DevConfig
{
    std::string   Addr;             ///< Developer payout address.
    std::string   Cut;              ///< Developer or peer cut encoded as a string.
    std::string   TokenValueInGNUS; ///< Conversion rate used for child-token.
    sgns::TokenID TokenID;          ///< Child token identifier configured for this node.
    std::string   BaseWritePath;    ///< Base directory for node databases, logs, and account storage.
} DevConfig_st;

extern DevConfig_st DEV_CONFIG;

constexpr uint64_t kDefaultTimestampToleranceMs = 300000;  // ±5 minutes

#define OUTGOING_TIMEOUT_MILLISECONDS 50000  // just communication time
#define INCOMING_TIMEOUT_MILLISECONDS 150000 // communication + verify proof

namespace sgns
{
    /**
     * @brief High-level facade that initializes and coordinates account, networking,
     *        transaction, blockchain, and processing subsystems.
     */
    class GeniusNode : public IComponent, public std::enable_shared_from_this<GeniusNode>
    {
    public:
        /**
         * @brief Creates a node using a generated or persisted account identity.
         * @param[in] dev_config Runtime configuration for paths, token settings, and payout data.
         * @param[in] autodht Whether to start DHT discovery.
         * @param[in] isprocessor Whether this node should run processing services.
         * @param[in] base_port Base pubsub port used to derive the node listening port.
         * @param[in] is_full_node Whether the node should run in full-node mode.
         * @param[in] use_upnp Whether to attempt UPnP port mapping.
         * @return Shared node instance after asynchronous database initialization is scheduled.
         */
        static std::shared_ptr<GeniusNode> New( const DevConfig_st &dev_config,
                                                bool                autodht      = true,
                                                bool                isprocessor  = true,
                                                uint16_t            base_port    = 40001,
                                                bool                is_full_node = false,
                                                bool                use_upnp     = true );

        /**
         * @brief Creates a node bound to the provided Ethereum private key.
         * @param[in] dev_config Runtime configuration for paths, token settings, and payout data.
         * @param[in] eth_private_key Ethereum private key used to derive the account identity.
         * @param[in] autodht Whether to start DHT discovery.
         * @param[in] isprocessor Whether this node should run processing services.
         * @param[in] base_port Base pubsub port used to derive the node listening port.
         * @param[in] is_full_node Whether the node should run in full-node mode.
         * @param[in] use_upnp Whether to attempt UPnP port mapping.
         * @return Shared node instance after asynchronous database initialization is scheduled.
         */
        static std::shared_ptr<GeniusNode> New( const DevConfig_st &dev_config,
                                                const char         *eth_private_key,
                                                bool                autodht      = true,
                                                bool                isprocessor  = true,
                                                uint16_t            base_port    = 40001,
                                                bool                is_full_node = false,
                                                bool                use_upnp     = true );

        /**
         * @brief Creates a node from an existing mnemonic phrase.
         * @param[in] dev_config Runtime configuration for paths, token settings, and payout data.
         * @param[in] mnemonic Mnemonic phrase used to restore the account identity.
         * @param[in] autodht Whether to start DHT discovery.
         * @param[in] isprocessor Whether this node should run processing services.
         * @param[in] base_port Base pubsub port used to derive the node listening port.
         * @param[in] is_full_node Whether the node should run in full-node mode.
         * @param[in] use_upnp Whether to attempt UPnP port mapping.
         * @return Shared node instance after asynchronous database initialization is scheduled, or nullptr on restore failure.
         */
        static std::shared_ptr<GeniusNode> NewFromMnemonic( const DevConfig_st &dev_config,
                                                            const std::string  &mnemonic,
                                                            bool                autodht      = true,
                                                            bool                isprocessor  = true,
                                                            uint16_t            base_port    = 40001,
                                                            bool                is_full_node = false,
                                                            bool                use_upnp     = true );

        /**
         * @brief Stops node services, joins background threads, and releases processing callbacks.
         */
        ~GeniusNode() override;

        /**
         * @brief Lifecycle states reported while the node is bootstrapping.
         */
        enum class NodeState : uint8_t
        {
            CREATING = 0,              ///< Object construction is in progress.
            MIGRATING_DATABASE,        ///< Versioned database migrations are running.
            INITIALIZING_DATABASE,     ///< Primary CRDT database is being initialized.
            INITIALIZING_PROCESSING,   ///< Processing modules are being initialized.
            INITIALIZING_BLOCKCHAIN,   ///< Blockchain service is being initialized.
            INITIALIZING_TRANSACTIONS, ///< Transaction manager is being initialized.
            READY,                     ///< Node is ready for external operations.
        };

        /**
         * @brief Error codes returned by GeniusNode operations.
         */
        enum class Error : uint8_t
        {
            INSUFFICIENT_FUNDS        = 1,  ///< Insufficient funds for a transaction.
            DATABASE_WRITE_ERROR      = 2,  ///< Error writing data into the database.
            INVALID_TRANSACTION_HASH  = 3,  ///< Input transaction hash is invalid.
            INVALID_CHAIN_ID          = 4,  ///< Chain ID is invalid.
            INVALID_TOKEN_ID          = 5,  ///< Token ID is invalid.
            TOKEN_ID_MISMATCH         = 6,  ///< Provided token ID does not match the configured token.
            PROCESS_COST_ERROR        = 7,  ///< Processing cost could not be calculated.
            PROCESS_INFO_MISSING      = 8,  ///< Processing information is missing from the JSON request.
            INVALID_JSON              = 9,  ///< JSON cannot be parsed.
            INVALID_BLOCK_PARAMETERS  = 10, ///< JSON block parameters are incorrect or missing.
            NO_PROCESSOR              = 11, ///< No processor is available for this request type.
            NO_PRICE                  = 12, ///< GNUS price could not be retrieved.
            TRANSACTIONS_NOT_READY    = 13, ///< Transaction manager is not ready.
            TRANSACTION_NOT_FINALIZED = 14, ///< Requested transaction did not finalize within the timeout.
            TRANSACTION_FAILED        = 15, ///< Requested transaction failed.
        };

#ifdef SGNS_DEBUG
        static constexpr std::chrono::milliseconds TIMEOUT_ESCROW_PAY{ 50000 }; ///< Debug escrow payout timeout.
        static constexpr std::chrono::milliseconds TIMEOUT_TRANSFER{ 50000 };   ///< Debug transfer timeout.
        static constexpr std::chrono::milliseconds TIMEOUT_MINT{ 50000 };       ///< Debug mint timeout.
#else
        static constexpr std::chrono::milliseconds TIMEOUT_ESCROW_PAY{ 30000 }; ///< Escrow payout timeout.
        static constexpr std::chrono::milliseconds TIMEOUT_TRANSFER{ 30000 };   ///< Transfer timeout.
        static constexpr std::chrono::milliseconds TIMEOUT_MINT{ 30000 };       ///< Mint timeout.
#endif
        /**
         * @brief Lists the account addresses currently available in local storage.
         * @return Public addresses stored under the configured base write path.
         */
        std::vector<std::string> GetAvailableAccounts();

        /**
         * @brief Selects the active account for subsequent node operations.
         * @param[in] public_address Stored account address to activate.
         * @return Success after services are reset and database initialization is restarted, or an address error.
         */
        outcome::result<void> SelectAccount( std::string_view public_address );

        /**
         * @brief Transfers node ownership to another stored account address.
         * @param[in] public_address Stored account address that should receive the current balance and become active.
         * @return Success after funds are transferred and the target account is selected, or an address/transaction error.
         */
        outcome::result<void> TransferAccount( std::string_view public_address );

        /**
         * @brief Deletes a locally stored account.
         * @param[in] public_address Stored account address to delete.
         * @return Success when the account is deleted; failure when the address is active or unavailable.
         */
        outcome::result<void> DeleteAccount( std::string_view public_address );

        /**
         * @brief Merges data from another account into the currently selected one.
         * @param[in] public_address Stored account address to transfer into and then delete.
         * @return Success when transfer and delete both complete.
         */
        outcome::result<void> MergeAccount( std::string_view public_address );

        /**
         * @brief Updates the payout address used by processing rewards.
         * @param[in] payout_address Address to save as the processing payout destination.
         * @return Success when the address is persisted and processing reinitialization is scheduled.
         */
        outcome::result<void> SetPayoutAddress( std::string_view payout_address );

        /**
         * @brief Submits an image-processing request described by JSON input.
         * @param[in] jsondata Processing request JSON.
         * @return Escrow transaction hash on success, or a validation, balance, or database error.
         */
        outcome::result<std::string> ProcessImage( const std::string &jsondata );

        /**
         * @brief Estimates the GNUS cost of a processing request manager.
         * @param[in] procmgr Processing manager containing parsed request data.
         * @return Estimated cost in minions, or 0 when the request size, price, or cost calculation fails.
         */
        uint64_t GetProcessCost( std::shared_ptr<sgns::sgprocessing::ProcessingManager> &procmgr );

        /**
         * @brief Retrieves the current GNUS market price from the configured pricing service.
         * @return Current GNUS price in USD, or Error::NO_PRICE when unavailable.
         */
        outcome::result<double> GetGNUSPrice();

        /**
         * @brief Returns the component name used by the component framework.
         * @return Static component name "GeniusNode".
         */
        std::string GetName() override
        {
            return "GeniusNode";
        }

        /**
         * @brief Returns the full SuperGenius version string.
         * @return Version string built from the compiled version metadata.
         */
        std::string GetVersion();

        /**
         * @brief Creates and submits a mint transaction.
         * @param[in] amount Amount to mint in token base units.
         * @param[in] transaction_hash Source-chain transaction hash that justifies the mint.
         * @param[in] chainid Source chain identifier where the burn or lock event occurred.
         * @param[in] tokenid Token identifier to mint.
         * @param[in] destination Recipient address; defaults to the active account address when empty.
         * @return Mint transaction hash on success, or a transaction readiness/submission error.
         */
        outcome::result<std::string> MintTokens( uint64_t           amount,
                                                 const std::string &transaction_hash,
                                                 const std::string &chainid,
                                                 TokenID            tokenid,
                                                 std::string        destination = "" );

        /**
         * @brief Creates a mint transaction and waits for it to finalize.
         * @param[in] amount Amount to mint in token base units.
         * @param[in] transaction_hash Source-chain transaction hash that justifies the mint.
         * @param[in] chainid Source chain identifier where the burn or lock event occurred.
         * @param[in] tokenid Token identifier to mint.
         * @param[in] destination Recipient address for the minted tokens.
         * @param[in] timeout Maximum time to wait for finalization.
         * @return Pair of transaction hash and elapsed milliseconds on success, or a transaction/finalization error.
         */
        outcome::result<std::pair<std::string, uint64_t>> MintTokens( uint64_t                  amount,
                                                                      const std::string        &transaction_hash,
                                                                      const std::string        &chainid,
                                                                      TokenID                   tokenid,
                                                                      std::string               destination,
                                                                      std::chrono::milliseconds timeout );

        /**
         * @brief Adds a peer address to the underlying PubSub service.
         * @param[in] peer Peer multiaddress to add.
         */
        void AddPeer( const std::string &peer );

        /**
         * @brief Starts or restarts the background UPnP port refresh thread.
         * @param[in] pubsubport TCP port to keep mapped through UPnP.
         */
        void RefreshUPNP( uint16_t pubsubport );

        /**
         * @brief Returns the active account balance across all tokens.
         * @return Total local UTXO balance for the active account.
         */
        uint64_t GetBalance();

        /**
         * @brief Returns the active account balance for a token.
         * @param[in] token_id Token identifier to filter by.
         * @return Local UTXO balance for @p token_id.
         */
        uint64_t GetBalance( TokenID token_id );

        /**
         * @brief Returns an address balance across all tokens.
         * @param[in] address Address whose UTXO balance should be queried.
         * @return Total local UTXO balance for @p address.
         */
        uint64_t GetBalance( const std::string &address );

        /**
         * @brief Returns an address balance for a token.
         * @param[in] token_id Token identifier to filter by.
         * @param[in] address Address whose UTXO balance should be queried.
         * @return Local UTXO balance for @p address and @p token_id.
         */
        uint64_t GetBalance( TokenID token_id, const std::string &address );

        /**
         * @brief Returns serialized incoming transactions known to the transaction manager.
         * @return Incoming transaction byte vectors, or an empty vector when transactions are not ready.
         */
        [[nodiscard]] std::vector<std::vector<uint8_t>> GetInTransactions() const;

        /**
         * @brief Returns serialized outgoing transactions known to the transaction manager.
         * @return Outgoing transaction byte vectors, or an empty vector when transactions are not ready.
         */
        [[nodiscard]] std::vector<std::vector<uint8_t>> GetOutTransactions() const;

        /**
         * @brief Returns serialized transactions filtered by optional status.
         * @param[in] tx_status Optional transaction status filter.
         * @return Transaction byte vectors, or an empty vector when transactions are not ready.
         */
        [[nodiscard]] const std::vector<std::vector<uint8_t>> GetTransactions(
            std::optional<TransactionManager::TransactionStatus> tx_status = std::nullopt ) const;

        /**
         * @brief Returns the active account public address.
         * @return Public address of the active account.
         */
        std::string GetAddress() const
        {
            return account_->GetAddress();
        }

        /**
         * @brief Returns the configured child token identifier.
         * @return Token identifier from the node runtime configuration.
         */
        TokenID GetTokenID() const
        {
            return dev_config_.TokenID;
        }

        /**
         * @brief Returns the current processing service status.
         * @return Processing status, or DISABLED when the service is not initialized.
         */
        [[nodiscard]] processing::ProcessingServiceImpl::ProcessingStatus GetProcessingStatus() const
        {
            return processing_service_ == nullptr ? processing::ProcessingServiceImpl::ProcessingStatus(
                                                        processing::ProcessingServiceImpl::Status::DISABLED,
                                                        0.0f )
                                                  : processing_service_->GetProcessingStatus();
        }

        /**
         * @brief Transfers funds and waits for the transaction to finalize.
         * @param[in] amount Amount to transfer in token base units.
         * @param[in] destination Recipient address.
         * @param[in] token_id Token identifier to transfer.
         * @param[in] timeout Maximum time to wait for finalization.
         * @return Pair of transaction hash and elapsed milliseconds on success, or a transfer/finalization error.
         */
        outcome::result<std::pair<std::string, uint64_t>> TransferFunds( uint64_t                  amount,
                                                                         const std::string        &destination,
                                                                         TokenID                   token_id,
                                                                         std::chrono::milliseconds timeout );

        /**
         * @brief Transfers funds without waiting for finalization.
         * @param[in] amount Amount to transfer in token base units.
         * @param[in] destination Recipient address.
         * @param[in] token_id Token identifier to transfer.
         * @return Transfer transaction hash on success, or a readiness, balance, or submission error.
         */
        outcome::result<std::string> TransferFunds( uint64_t amount, const std::string &destination, TokenID token_id );

        /**
         * @brief Transfers funds to the configured developer address.
         * @param[in] amount Amount to transfer in token base units.
         * @param[in] token_id Token identifier to transfer.
         * @return Transfer transaction hash on success, or a readiness, balance, or submission error.
         */
        outcome::result<std::string> PayDev( uint64_t amount, TokenID token_id );

        /**
         * @brief Transfers funds to the configured developer address and waits for finalization.
         * @param[in] amount Amount to transfer in token base units.
         * @param[in] token_id Token identifier to transfer.
         * @param[in] timeout Maximum time to wait for finalization.
         * @return Pair of transaction hash and elapsed milliseconds on success, or a transfer/finalization error.
         */
        outcome::result<std::pair<std::string, uint64_t>> PayDev( uint64_t                  amount,
                                                                  TokenID                   token_id,
                                                                  std::chrono::milliseconds timeout );

        /**
         * @brief Waits until an outgoing transaction reaches a terminal state.
         * @param[in] tx_id Transaction hash to poll.
         * @param[in] timeout Maximum time to wait.
         * @return Pair of terminal status and elapsed milliseconds, or Error::TRANSACTION_NOT_FINALIZED on timeout.
         */
        outcome::result<std::pair<TransactionManager::TransactionStatus, uint64_t>> WaitForFinalized(
            const std::string        &tx_id,
            std::chrono::milliseconds timeout );

        /**
         * @brief Checks whether an outgoing transaction has reached a terminal state.
         * @param[in] tx_id Transaction hash to check.
         * @return Terminal transaction status when available; otherwise std::nullopt.
         */
        std::optional<TransactionManager::TransactionStatus> IsFinalized( const std::string &tx_id );

        /**
         * @brief Returns the underlying PubSub service.
         * @return Shared PubSub instance used by the node.
         */
        std::shared_ptr<ipfs_pubsub::GossipPubSub> GetPubSub()
        {
            return pubsub_;
        }

        /**
         * @brief Releases processing service, core, queue, and result-storage references.
         */
        void ResetProcessingMembers();

        /**
         * @brief       Formats a fixed-point amount into a human-readable string.
         * @param[in]   amount  Amount in Minion Tokens (1e-6 GNUS).
         * @param[in]   tokenId Optional token identifier:
         *                         – empty: default (minion to GNUS) formatting
         *                         – matches DevConfig.TokenID: child-token formatting
         *                         – otherwise: returns Error::TOKEN_ID_MISMATCH
         * @return      Outcome result with the formatted string in GNUS or an error.
         */
        outcome::result<std::string> FormatTokens( uint64_t amount, TokenID tokenId );

        /**
         * @brief       Parses a human-readable string into a fixed-point amount.
         * @param[in]   str      String representation of an amount in GNUS.
         * @param[in]   tokenId  Optional token identifier:
         *                          – empty: default (GNUS to minion) parsing
         *                          – matches DevConfig.TokenID: child-token parsing
         *                          – otherwise: returns Error::TOKEN_ID_MISMATCH
         * @return      Outcome result with the parsed amount in Minion Tokens (1e-6 GNUS) or an error.
         */
        outcome::result<uint64_t> ParseTokens( const std::string &str, TokenID tokenId );

        /**
         * @brief Prints the transaction GlobalDB datastore for debugging.
         */
        void PrintDataStore() const;

        /**
         * @brief Stops the processing service if it is initialized.
         */
        void StopProcessing();

        /**
         * @brief Starts the processing service on the configured processing grid channel.
         */
        void StartProcessing();

        /**
         * @brief Retrieves current USD prices for token identifiers, using a short local cache.
         * @param[in] tokenIds CoinGecko token identifiers to price.
         * @return Map from token identifier to current USD price, or a price-retrieval error.
         */
        outcome::result<std::map<std::string, double>> GetCoinprice( const std::vector<std::string> &tokenIds );

        /**
         * @brief Retrieves historical USD prices for token identifiers at exact timestamps.
         * @param[in] tokenIds CoinGecko token identifiers to price.
         * @param[in] timestamps Unix timestamps to query.
         * @return Nested map from token identifier to timestamp to USD price.
         */
        outcome::result<std::map<std::string, std::map<int64_t, double>>> GetCoinPriceByDate(
            const std::vector<std::string> &tokenIds,
            const std::vector<int64_t>     &timestamps );

        /**
         * @brief Retrieves historical USD prices for token identifiers over a date range.
         * @param[in] tokenIds CoinGecko token identifiers to price.
         * @param[in] from Start Unix timestamp for the range.
         * @param[in] to End Unix timestamp for the range.
         * @return Nested map from token identifier to timestamp to USD price.
         */
        outcome::result<std::map<std::string, std::map<int64_t, double>>> GetCoinPricesByDateRange(
            const std::vector<std::string> &tokenIds,
            int64_t                         from,
            int64_t                         to );

        /**
         * @brief Waits for an incoming transaction to be processed.
         * @param[in] txId Transaction hash to wait for.
         * @param[in] timeout Maximum time to wait.
         * @return Incoming transaction status, or INVALID when transactions are not ready.
         */
        TransactionManager::TransactionStatus WaitForTransactionIncoming( const std::string        &txId,
                                                                          std::chrono::milliseconds timeout );

        /**
         * @brief Waits for an outgoing transaction to be processed.
         * @param[in] txId Transaction hash to wait for.
         * @param[in] timeout Maximum time to wait.
         * @return Outgoing transaction status, or INVALID when transactions are not ready.
         */
        TransactionManager::TransactionStatus WaitForTransactionOutgoing( const std::string        &txId,
                                                                          std::chrono::milliseconds timeout );

        /**
         * @brief Waits for an escrow release transaction tied to an escrow hold.
         * @param[in] originalEscrowId Hash of the original escrow hold transaction.
         * @param[in] timeout Maximum time to wait.
         * @return Escrow release transaction status, or INVALID when transactions are not ready.
         */
        TransactionManager::TransactionStatus WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                                    std::chrono::milliseconds timeout );

        /**
         * @brief Returns the current transaction manager lifecycle state.
         * @return Transaction manager state, or CREATING when the manager is not available.
         */
        TransactionManager::State GetTransactionManagerState() const;

        /**
         * @brief Returns a tracked transaction status by transaction hash.
         * @param[in] txId Transaction hash to look up.
         * @return Outgoing status when present, then incoming status, or INVALID when unknown/not ready.
         */
        TransactionManager::TransactionStatus GetTransactionStatus( const std::string &txId ) const;

        /**
         * @brief Sets the authorized full-node address for blockchain genesis verification.
         * @param[in] pub_address Public address authorized to create genesis blocks.
         */
        void SetAuthorizedFullNodeAddress( const std::string &pub_address );

        /**
         * @brief Gets the current authorized full-node public address.
         * @return Public address authorized to create genesis blocks.
         */
        const std::string &GetAuthorizedFullNodeAddress() const;

        /**
         * @brief Returns the current GeniusNode lifecycle state.
         * @return Current node state.
         */
        NodeState GetState() const
        {
            return state_.load();
        }

    protected:
        friend class TransactionSyncTest;
        friend class MultiAccountTestAccess;

        /**
         * @brief Enqueues a transaction and its proof directly through the transaction manager.
         * @param[in] tx Transaction to enqueue.
         * @param[in] proof Serialized proof bytes associated with @p tx.
         */
        void SendTransactionAndProof( std::shared_ptr<GeniusTransaction> tx, std::vector<uint8_t> proof );

        /**
         * @brief Configures transaction filtering time windows for tests.
         * @param[in] timeframe_limit_ms Timestamp tolerance in milliseconds.
         * @param[in] mutability_window_ms Mutability window in milliseconds.
         */
        void ConfigureTransactionFilterTimeoutsMs( uint64_t timeframe_limit_ms, uint64_t mutability_window_ms );

        std::string                    write_base_path_; ///< Base path for node databases, logs, and account storage.
        std::shared_ptr<GeniusAccount> account_;         ///< Active account used by node services.

    private:
        std::shared_ptr<boost::asio::io_context> io_; ///< Shared IO context for async services.
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
                                                              io_work_guard_; ///< Keeps @ref io_ alive.
        std::shared_ptr<crdt::GlobalDB>                       tx_globaldb_;   ///< Transaction/global state CRDT DB.
        std::shared_ptr<crdt::GlobalDB>                       job_globaldb_;  ///< Reserved job CRDT DB handle.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>            pubsub_;        ///< PubSub networking service.
        std::shared_ptr<TransactionManager>                   transaction_manager_; ///< Transaction service.
        std::unique_ptr<BridgeRelayer>                        bridge_relayer_;      ///< Bridge burn→mint relayer.
        std::shared_ptr<processing::ProcessingTaskQueue>      task_queue_;          ///< Processing task queue.
        std::shared_ptr<processing::ProcessingCoreImpl>       processing_core_;     ///< Processing engine core.
        std::shared_ptr<processing::ProcessingServiceImpl>    processing_service_;  ///< Processing network service.
        std::shared_ptr<processing::SubTaskResultStorageImpl> task_result_storage_; ///< Subtask result store.
        std::shared_ptr<soralog::LoggingSystem>               logging_system_;      ///< libp2p logging system.
        bool                                                  autodht_;     ///< Whether DHT discovery is enabled.
        bool                                                  isprocessor_; ///< Whether processing service should run.
        bool                        is_full_node_;                 ///< Whether this node runs in full-node mode.
        base::Logger                node_logger_;                  ///< Main node logger.
        DevConfig_st                dev_config_;                   ///< Runtime node configuration.
        std::string                 gnus_network_full_path_;       ///< Versioned network DB path.
        std::string                 processing_channel_topic_;     ///< Processing task channel topic.
        std::string                 processing_grid_chanel_topic_; ///< Processing grid topic.
        uint16_t                    pubsubport_;                   ///< Active PubSub TCP port.
        std::shared_ptr<Blockchain> blockchain_;                   ///< Blockchain service.

        /**
         * @brief Constructs a node around an already-created account.
         * @param[in] dev_config Runtime configuration for paths, token settings, and payout data.
         * @param[in] account Account instance to bind to this node.
         * @param[in] autodht Whether to start DHT discovery.
         * @param[in] isprocessor Whether this node should run processing services.
         * @param[in] base_port Base pubsub port used to derive the node listening port.
         * @param[in] is_full_node Whether the node should run in full-node mode.
         * @param[in] use_upnp Whether to attempt UPnP port mapping.
         */
        GeniusNode( const DevConfig_st            &dev_config,
                    std::shared_ptr<GeniusAccount> account,
                    bool                           autodht,
                    bool                           isprocessor,
                    uint16_t                       base_port,
                    bool                           is_full_node,
                    bool                           use_upnp );

        /**
         * @brief Initializes OpenSSL library state used by networking dependencies.
         */
        void InitOpenSSL();

        /**
         * @brief Initializes application and dependency loggers.
         * @param[in] base_path Base directory used for log files.
         * @return True when logging configuration succeeds.
         */
        bool InitLoggers( const std::string &base_path );

        /**
         * @brief Creates a tagged logger with the requested sink and level.
         * @param[in] tag Logger tag.
         * @param[in] logdir Optional log file path.
         * @param[in] level Logger severity threshold.
         * @return Configured logger instance.
         */
        base::Logger ConfigureLogger( const std::string        &tag,
                                      const std::string        &logdir,
                                      spdlog::level::level_enum level );

        /**
         * @brief Initializes PubSub, GraphSync networking, and optional DHT discovery.
         * @param[in] base_port Base pubsub port used to derive the node listening port.
         * @param[in] is_full_node Whether to use full-node connection limits.
         * @return True when network initialization succeeds.
         */
        bool InitNetwork( uint16_t base_port, bool is_full_node );

        /**
         * @brief Attempts initial UPnP port mapping for the PubSub port.
         * @return True when no gateway exists or a usable port is mapped.
         */
        bool InitUPNP();

        /**
         * @brief Initializes and starts the transaction GlobalDB.
         * @return True when the database is opened and started.
         */
        bool InitDatabase();

        /**
         * @brief Initializes processing queue, core, and result storage components.
         * @return True when processing modules are constructed.
         */
        bool InitProcessingModules();

        /**
         * @brief Begins the asynchronous database migration and initialization state flow.
         */
        void BeginDBInitialization();

        /**
         * @brief Moves the node to the next lifecycle state and runs state-specific work.
         * @param[in] next_state State to enter.
         */
        void StateTransition( NodeState next_state );

        /**
         * @brief Runs versioned database migrations on a detached thread.
         * @param[in] callback Callback invoked with the migration result.
         */
        void MigrateDatabase( std::function<void( outcome::result<void> )> callback );

        /**
         * @brief Schedules a delayed migration retry after migration bootstrap failure.
         */
        void ScheduleMigrationRetry();

        /**
         * @brief Schedules a delayed blockchain initialization retry.
         */
        void ScheduleBlockchainRetry();

        /**
         * @brief Loads RPC endpoints from the evmrelay ChainList provider and wires
         *        them into the transaction manager's public-chain input validator.
         *
         * This is called once during startup after the transaction manager reaches
         * the READY state and before processing modules are initialized.  It reads
         * @c chains_config.json to discover configured chains, maps their names to
         * well-known EVM chain IDs, parses the ChainList data to extract verified
         * public RPC endpoint URLs, and calls @c PublicChainInputValidator::SetRpcEndpoints
         * for each configured chain.
         */
        void InitializeRpcEndpoints();

        /**
         * @brief Returns the transaction manager when initialized.
         * @return Shared transaction manager, or Error::TRANSACTIONS_NOT_READY.
         */
        outcome::result<std::shared_ptr<TransactionManager>>      GetTransactionManager() const;
        outcome::result<std::shared_ptr<crdt::AtomicTransaction>> CreateEscrowInfoCRDTTransaction(
            std::string        path,
            sgns::base::Buffer value );

        /**
         * @brief Starts DHT provider discovery for the processing grid topic.
         */
        void DHTInit();

        struct PriceInfo
        {
            double                                             price;      ///< Cached USD token price.
            std::chrono::time_point<std::chrono::system_clock> lastUpdate; ///< Time when @ref price was fetched.
        };

        std::map<std::string, PriceInfo>                   m_tokenPriceCache; ///< Cached token price data by token id.
        const std::chrono::minutes                         m_cacheValidityDuration{ 1 }; ///< Price cache TTL.
        std::chrono::time_point<std::chrono::system_clock> m_lastApiCall{}; ///< Last external price API call time.
        static constexpr std::chrono::seconds              MIN_API_CALL_INTERVAL{ 5 }; ///< Minimum price API interval.

        static constexpr size_t                   DEFAULT_IO_THREADS = 4;                 ///< Default IO thread count.
        size_t                                    io_thread_count_{ DEFAULT_IO_THREADS }; ///< IO thread count.
        std::vector<std::thread>                  io_threads_;                            ///< Threads running @ref io_.
        std::thread                               upnp_thread;                      ///< Background UPnP refresh thread.
        std::atomic<bool>                         stop_upnp{ false };               ///< UPnP thread stop flag.
        std::string                               base58key_;                       ///< Base58 key suffix for DB paths.
        std::shared_ptr<libp2p::basic::Scheduler> scheduler_;                       ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< GraphSync request ID generator.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork_; ///< GraphSync network.

        std::unique_ptr<boost::asio::thread_pool> processing_callback_pool_; ///< Processing callback execution pool.

        std::atomic<NodeState> state_{ NodeState::CREATING }; ///< Current node lifecycle state.
        bool                   use_upnp_;                     ///< Whether UPnP mapping is enabled.

        /**
         * @brief Submits an escrow payout transaction and waits for confirmation.
         * @param[in] escrow_path Escrow address/path associated with the completed task.
         * @param[in] taskresult Processing task result that defines payout recipients.
         * @param[in] crdt_transaction Atomic CRDT transaction that marks task completion.
         * @param[in] timeout Maximum time to wait for escrow payout confirmation.
         * @return Pair of payout transaction hash and elapsed milliseconds, or a payout/timeout error.
         */
        outcome::result<std::pair<std::string, uint64_t>> PayEscrow(
            const std::string                       &escrow_path,
            const SGProcessing::TaskResult          &taskresult,
            std::shared_ptr<crdt::AtomicTransaction> crdt_transaction,
            std::chrono::milliseconds                timeout = std::chrono::milliseconds( TIMEOUT_ESCROW_PAY ) );

        /**
         * @brief Handles successful processing completion and triggers escrow payout.
         * @param[in] task_id Completed task identifier.
         * @param[in] taskresult Processing task result to persist and pay out.
         */
        void ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult );

        /**
         * @brief Handles processing failure notifications.
         * @param[in] task_id Failed task identifier.
         */
        void ProcessingError( const std::string &task_id );

        /**
         * @brief Rotates existing node log files before logger initialization.
         * @param[in] base_path Directory containing node log files.
         */
        void RotateLogFiles( const std::string &base_path );

        /**
         * @brief Parse and sum all "block_len" values from the JSON.
         * @param[in] json_data JSON string containing an "input" array.
         * @return outcome::result<uint64_t> with total bytes, or an error code.
         */
        outcome::result<uint64_t> ParseBlockSize( const std::string &json_data );

        /**
         * @brief Reacts to transaction manager state changes by starting or stopping processing.
         * @param[in] old_state Previous transaction manager state.
         * @param[in] new_state Current transaction manager state.
         */
        void TransactionStateChanged( TransactionManager::State old_state, TransactionManager::State new_state );

        static constexpr std::string_view DB_PATH         = "bc-%d/"; ///< Blockchain DB path format.
        static constexpr std::uint16_t    MAIN_NET        = 369;      ///< Main network identifier.
        static constexpr std::uint16_t    TEST_NET        = 963;      ///< Test network identifier.
        static constexpr std::size_t      MAX_NODES_COUNT = 1;        ///< Processing service node count limit.

        static constexpr std::string_view PROCESSING_GRID_CHANNEL = "SGNUS.Jobs.Channel";  ///< Processing job topic.
        static constexpr std::string_view PROCESSING_CHANNEL = "SGNUS.Processing.Channel"; ///< Processing result topic.
        static constexpr std::string_view GNUS_NETWORK_PATH  = "SuperGNUSNode.Node";       ///< Base network DB path.

        /**
         * @brief Builds the YAML logging configuration used by the node.
         * @param[in] base_path Directory where the main log file should be written.
         * @return YAML logging configuration with @p base_path substituted.
         */
        static std::string GetLoggingSystem( const std::string &base_path )
        {
            std::string config( R"(
# ----------------
sinks:
    - name: file
      type: file
      capacity: 1000
      path: [basepath]/sgnslog.log
groups:
    - name: SuperGeniusNode
      sink: file
      level: error
      children:
        - name: libp2p
        - name: Gossip
        - name: yx-stream
# ----------------
  )" );

            boost::replace_all( config, "[basepath]", base_path );
            return config;
        }
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, GeniusNode::Error );

#endif
