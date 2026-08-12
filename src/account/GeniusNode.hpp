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
#include <variant>
#include <mutex>
#include <atomic>

#include <boost/asio.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>

#include "account/GeniusAccount.hpp"
#include "account/NodeType.hpp"
#include "base/buffer.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "account/TransactionManager.hpp"
#include "account/BridgeRelayer.hpp"
#include "account/ChainRpcEndpointProvider.hpp"
#include "eth/eth_watch_service.hpp"
#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include "crypto/hasher.hpp"
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
#include <libp2p/peer/peer_info.hpp>
#include <libp2p/event/bus.hpp>
#include <libp2p/network/connection_manager.hpp>

// Forward declaration for bitswap
namespace sgns::ipfs_bitswap
{
    class Bitswap;
}

// Forward declarations for BURN-02/BURN-03 quorum-wiring types (full includes live in GeniusNode.cpp).
namespace sgns::securecrdt
{
    class SecureCrdt;
}

namespace sgns::trustedpeer
{
    class TrustStateStore;
    class TrustedPeerRegistry;
}

namespace sgns::account
{
    class BurnConfig;
    class TrustStartupController;
}

/**
 * @brief Runtime configuration values used to bootstrap a Genius node instance.
 */
typedef struct DevConfig
{
    std::string   Addr;             ///< Developer payout address.
    std::string   DevFraction;      ///< Developer's share of each subtask payout, as a decimal string ("0.35" = 35%).
    std::string   TokenValueInGNUS; ///< Conversion rate used for child-token.
    sgns::TokenID TokenID;          ///< Child token identifier configured for this node.
    std::string   BaseWritePath;    ///< Base directory for node databases, logs, and account storage.
} GeniusNodeConfig;

extern GeniusNodeConfig DEV_CONFIG;

constexpr uint64_t kDefaultTimestampToleranceMs = 300000; // ±5 minutes

#define OUTGOING_TIMEOUT_MILLISECONDS 50000  // just communication time
#define INCOMING_TIMEOUT_MILLISECONDS 150000 // communication + verify proof

namespace sgns
{
    class MigrationManager;

    namespace evmwatcher
    {
        class BridgeCatchupWatcher;
    }

    /**
     * @brief Account-creation source for GeniusNode::New(dev_config, AccountSource).
     *
     * Owned std::string payloads — a std::variant owns its active alternative, so
     * non-owning views such as const char* or std::string_view would dangle once the
     * variant is stored or passed. TokenID and other dev_config fields are NOT part of
     * the variant; they come from dev_config.
     */
    struct NewAccount
    {
    }; ///< Generate a new identity.

    struct FromPrivateKey
    {
        std::string eth_private_key;
    }; ///< Restore from an Ethereum hex private key.

    struct FromMnemonic
    {
        std::string mnemonic;
    }; ///< Restore from a BIP39 mnemonic.

    struct FromPublicKey
    {
        std::string public_address;
    }; ///< Load from storage by public address (read-only).

    using AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>;

    /**
     * @brief High-level facade that initializes and coordinates account, networking,
     *        transaction, blockchain, and processing subsystems.
     */
    class GeniusNode : public IComponent, public IBridgeInitObserver, public std::enable_shared_from_this<GeniusNode>
    {
    public:
        /**
         * @brief Canonical node factory (INTF-01). Account identity is chosen via
         *        AccountSource; node role (node_type_) is read from node_type in
         *        sgns_config.json, not a param. Old factories are retained this phase
         *        (deleted in Phase 3 per 02-CONTEXT.md D-01).
         * @param[in] dev_config Runtime configuration (paths, token, payout data).
         * @param[in] source Account-creation source (NewAccount / FromPrivateKey / FromMnemonic / FromPublicKey).
         * @return Shared node instance after asynchronous DB init is scheduled, or nullptr
         *         on account-restore or initialization failure (D-04).
         */
        static std::shared_ptr<GeniusNode> New( const GeniusNodeConfig &dev_config, AccountSource source );

        /**
         * @brief Writes a minimal network_config.json for test/example setup (MIG-02).
         * @param[in] base_path Directory whose network_config.json will be (over)written (dev_config.BaseWritePath).
         * @param[in] port_seed Numeric port seed (Phase-1 key "port_seed").
         * @param[in] auto_dht  Whether DHT discovery is enabled (key "auto_dht").
         * @return Failure on file I/O error; success otherwise. Truncates/rewrites the file and disables UPnP so
         *         tests and examples do not depend on the host LAN.
         */
        static outcome::result<void> WriteNetworkConfig( const std::string &base_path,
                                                         uint16_t           port_seed,
                                                         bool               auto_dht );

        /**
         * @brief Writes a minimal sgns_config.json for test/example setup; validates node_type (MIG-02).
         * @param[in] base_path Directory whose sgns_config.json will be (over)written.
         * @param[in] node_type Role string — validated case-insensitively (Full/Light/Archive); any other value returns Error::INVALID_NODE_TYPE.
         * @param[in] is_processor Whether processing services run (key "is_processor").
         * @param[in] rpc_catchup Whether the bridge catchup scan watcher starts at bridge init (key "rpc_catchup"). Defaults true; pass false for tests that do not exercise bridge/RPC/catchup paths.
         * @return Error::INVALID_NODE_TYPE on an unrecognized node_type; failure on I/O error; success otherwise.
         */
        static outcome::result<void> WriteSgnsConfig( const std::string &base_path,
                                                      const std::string &node_type,
                                                      bool               is_processor,
                                                      bool               rpc_catchup = true );

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
            INITIALIZING_BLOCKCHAIN,   ///< Blockchain service is being initialized.
            INITIALIZING_TRANSACTIONS, ///< Transaction manager is being initialized.
            WAITING_FOR_TRUST_GENESIS, ///< Networking is live but no durable trust genesis exists.
            WAITING_FOR_BURN_GENESIS,  ///< Trust genesis is durable but initial burn quorum is pending.
            FATAL_TRUST_MISMATCH,      ///< Durable trust state cannot safely start for this network.
            INITIALIZING_PROCESSING,   ///< Processing modules are being initialized.
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
            INVALID_NODE_TYPE         = 16, ///< sgns_config.json node_type string was not Full/Light/Archive.
        };

        /**
         * @brief Deployment node role, read from sgns_config.json ("node_type").
         *
         * Defined in account/NodeType.hpp so the lower layers that consume it
         * (TransactionManager, MigrationManager) need not include this facade.
         * Aliased here for source compatibility with GeniusNode::NodeType call sites.
         */
        using NodeType = ::sgns::NodeType;

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
         * @brief Returns the resolved PubSub listening port.
         * @return The TCP port selected during @ref InitNetwork (from @c pubsub_port override
         *         or derived from @c port_seed). Test/read-only observable; does not mutate state.
         */
        uint16_t GetPubsubPort() const noexcept;

        /**
         * @brief Returns whether DHT discovery is enabled after config resolution.
         * @return The resolved @c autodht_ value (constructor param, or the @c auto_dht key
         *         from @c network_config.json when present — config wins). Read-only observable.
         */
        bool IsAutodhtEnabled() const noexcept;

        /**
         * @brief Returns whether this node's role is Full.
         * @return True only for @c NodeType::Full. Derived from @c node_type_;
         *         test/read-only observable; does not mutate state.
         *
         * @note This is a role check, not a capability check. Archive replicates
         *       network-wide data just like Full but is not a Full node — for the
         *       "does it store everything" question use @c ReplicatesAllAccounts,
         *       and for "does it do the work" use @c ParticipatesInConsensus
         *       (both in account/NodeType.hpp, taking @ref GetNodeType).
         */
        bool IsFullNode() const noexcept
        {
            return node_type_ == NodeType::Full;
        }

        /**
         * @brief Returns the resolved node role.
         * @return The @c node_type_ read from sgns_config.json (default Light). Read-only observable.
         */
        NodeType GetNodeType() const noexcept;

        /**
         * @brief Returns whether processing services run after config resolution.
         * @return The resolved @c isprocessor_ (the @c is_processor key, forced to false for
         *         Archive nodes). Test/read-only observable; does not mutate state.
         */
        bool IsProcessor() const noexcept;

        /**
         * @brief Adds an account to local storage using an Ethereum private key.
         * @param[in] private_key Ethereum private key in hex format.
         * @return Success if the account was created and stored, or an error.
         */
        outcome::result<void> AddAccountWithKey( const char *private_key ) const;

        /**
         * @brief Adds an account to local storage using a BIP39 mnemonic phrase.
         * @param[in] mnemonic BIP39 mnemonic phrase.
         * @return Success if the account was created and stored, or an error.
         */
        outcome::result<void> AddAccountWithMnemonic( const std::string &mnemonic ) const;

        /**
         * @brief Adds an account to local storage using a newly generated random BIP39 mnemonic.
         * @return The generated mnemonic phrase on success, or an error.
         */
        outcome::result<std::string> AddAccountWithRandomMnemonic() const;

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
         * @brief Merges the active account into another stored account.
         * @param[in] public_address Stored account address to receive the configured-token balance and become active.
         * @return Success when transfer, selection, and deletion of the previous active account complete.
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
         * @brief       Returns the task IDs of jobs submitted by the active account.
         * @param[in]   limit  Maximum number of task IDs to return (default: 50).
         * @param[in]   offset Number of task IDs to skip from the end of the list (default: 0).
         * @return      Vector of task IDs from the in-memory set, newest last.
         * @note        The on-disk file retains full history; only the most recent
         *              entries are kept in memory for polling.
         */
        std::vector<std::string> GetMyTaskIds( size_t limit = 50, size_t offset = 0 ) const;

        /**
         * @brief       Retrieves the completed result for a specific job by its task ID.
         * @param[in]   taskId The task ID (ipfs_block_id) of the job.
         * @return      The TaskResult if the task has completed, or an error if not found/incomplete.
         */
        outcome::result<SGProcessing::TaskResult> GetTaskResult( const std::string &taskId );

        /**
         * @brief Estimates the GNUS cost of a processing request manager.
         * @param[in] procmgr Processing manager containing parsed request data.
         * @return Estimated cost in minions, or 0 when the request size, price, or cost calculation fails.
         */
        uint64_t GetProcessCost( const sgns::sgprocessing::ProcessingManager &procmgr );

        /**
         * @brief Basis points of an escrow payout burned to the zero address during release.
         * @return Burn fraction in basis points (1/10000ths), e.g. 100 == 1%.
         */
        static constexpr uint64_t GetBurnBasisPoints()
        {
            return TransactionManager::BURN_BASIS_POINTS_DEFAULT;
        }

        /**
         * @brief Total basis points denominator used with @ref GetBurnBasisPoints.
         * @return Basis points total (10000).
         */
        static constexpr uint64_t GetBasisPointsTotal()
        {
            return TransactionManager::BASIS_POINTS_TOTAL;
        }

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

        /** Reloads log level overrides from log_config.json at runtime. */
        void LoadLogConfig();

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
         * @brief Adds a peer to PubSub and starts connecting to it.
         * @param[in] peer Peer multiaddress to connect to.
         */
        void AddPeer( const std::string &peer );

        /**
         * @brief Adds peers to PubSub and starts connecting to them.
         * @param[in] peers Peer multiaddresses to connect to.
         */
        void AddPeers( const std::vector<std::string> &peers );

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
         * @brief Counts known transactions filtered by optional status.
         * @param[in] tx_status Optional transaction status filter.
         * @return Number of matching transactions, or zero when transactions are not ready.
         */
        [[nodiscard]] size_t CountTransactions(
            std::optional<TransactionManager::TransactionStatus> tx_status = std::nullopt ) const;

        /**
         * @brief Returns the active account public address.
         * @return Public address of the active account.
         */
        std::string GetAddress() const;

        /**
         * @brief Retrieves the BIP39 mnemonic of the active account from secure storage.
         * @return The mnemonic phrase if found, or std::nullopt.
         */
        std::optional<std::string> GetMnemonicOfActiveAccount() const;

        /**
         * @brief Returns the configured child token identifier.
         * @return Token identifier from the node runtime configuration.
         */
        [[nodiscard]] TokenID GetTokenID() const
        {
            return dev_config_.TokenID;
        }

        /**
         * @brief Returns the current node initialization progress as a percentage and description.
         *        The percentage ranges from 0.0 (CREATING) to 1.0 (READY), with sub-progress
         *        reported during database migration and transaction manager initialization.
         * @return Pair of progress fraction and a human-readable status description.
         */
        [[nodiscard]] std::pair<float, std::string> GetInitializationStatus() const;

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
         * @brief Returns the shared GraphSync network used by the node's GlobalDBs.
         * @return Shared graphsync Network instance; inbound graphsync for this
         *         host is dispatched through its registered protocol handler.
         */
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network> GetGraphsyncNetwork()
        {
            return graphsyncnetwork_;
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
         * @brief Waits until an escrow hold output is consumed.
         * @param[in] originalEscrowId Hash of the original escrow hold transaction.
         * @param[in] timeout Maximum time to wait.
         * @return CONFIRMED when consumed, or INVALID when transactions are not ready or the wait times out.
         */
        TransactionManager::TransactionStatus WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                                    std::chrono::milliseconds timeout );

        /**
         * @brief Returns the current transaction manager lifecycle state.
         * @return Transaction manager state, or CREATING when the manager is not available.
         */
        TransactionManager::State GetTransactionManagerState() const;

        /**
         * @brief Returns the transaction manager when initialized.
         * @return Shared transaction manager, or Error::TRANSACTIONS_NOT_READY.
         */
        outcome::result<std::shared_ptr<TransactionManager>> GetTransactionManager() const;

        /**
         * @brief Configures RPC endpoints for a specific EVM chain on the public-chain input validator.
         *
         * Allows callers (including E2E tests) to register RPC endpoints for chains
         * that are not in the default mainnet set (e.g. Sepolia testnet).
         * The transaction manager must be in READY state.
         *
         * @param[in] chain_id  Numeric EVM chain ID as a string (e.g. "11155111" for Sepolia).
         * @param[in] endpoints  Vector of weighted RPC endpoints for the chain.
         * @return True when the endpoints were configured; false when the transaction
         *         manager is absent or not READY.
         */
        bool ConfigureRpcEndpoint( const std::string &chain_id, std::vector<WeightedRpcEndpoint> endpoints );

        /**
         * @brief Injects a custom chainlist fetcher for RPC endpoint discovery (test injection point).
         * @param[in] fetcher Callable returning the chainlist JSON string, or std::nullopt on failure.
         */
        void SetChainlistFetcher( std::function<std::optional<std::string>()> fetcher );

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
        std::string GetAuthorizedFullNodeAddress() const;

        /**
         * @brief Returns the current GeniusNode lifecycle state.
         * @return Current node state.
         */
        NodeState GetState() const
        {
            return state_.load();
        }

        [[nodiscard]] bool                     IsTrustEconomicallyReady() const;
        [[nodiscard]] bool                     CanApproveTrustSuccessors() const;
        [[nodiscard]] std::vector<std::string> GetCurrentTrustedPeers() const;

    protected:
        friend class TransactionSyncTest;
        friend class MultiAccountTestAccess;
        friend class GeniusNodeTestAccess;

        /**
         * @brief Enqueues a transaction and its proof directly through the transaction manager.
         * @param[in] tx Transaction to enqueue.
         * @param[in] proof Serialized proof bytes associated with @p tx.
         */
        void SendTransactionAndProof( std::shared_ptr<GeniusTransaction> tx, std::vector<uint8_t> proof );

        std::string write_base_path_; ///< Base path for node databases, logs, and account storage.

    private:
        // ─────────────────────────────────────────────────────────────────────────────
        // Runtime object graph — OWNERSHIP ORDER.
        //
        // The chain, provider first:
        //   io_context -> PubSub -> GeniusAccount (its AccountMessenger owns PubSub
        //   subscriptions) -> scheduler/generator/GraphSync -> Bitswap -> GlobalDB
        //   -> Blockchain -> bridge -> quorum -> TransactionManager -> processing
        //   -> timers and observers.
        // ─────────────────────────────────────────────────────────────────────────────

        std::shared_ptr<soralog::LoggingSystem>
            logging_system_; ///< libp2p logging system; outlives everything that logs.

        std::shared_ptr<boost::asio::io_context> io_; ///< Shared IO context for async services.
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
            io_work_guard_; ///< Keeps @ref io_ alive.

        /// PubSub's own io_context, retained across teardown.
        /// It must outlive @ref graphsyncnetwork_, hence the declaration here, above it.
        std::shared_ptr<boost::asio::io_context>   pubsub_context_keepalive_;
        std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub_; ///< PubSub networking service.

    protected:
        /// Active account used by node services. Declared after @ref pubsub_ because
        /// GeniusAccount owns an AccountMessenger holding PubSub subscriptions.
        std::shared_ptr<GeniusAccount> account_;

    private:
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler_; ///< libp2p scheduler.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_; ///< GraphSync request ID generator.
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork_; ///< GraphSync network.

        std::shared_ptr<libp2p::event::Bus>          bitswap_event_bus_; ///< Event bus for bitswap.
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap_; ///< IPFS bitswap; borrows the PubSub host and the bus.

        std::shared_ptr<crdt::GlobalDB>   tx_globaldb_;       ///< Transaction/global state CRDT DB.
        std::shared_ptr<MigrationManager> migration_manager_; ///< Migration engine (valid during MIGRATING_DATABASE).
        mutable std::mutex                migration_mutex_;   ///< Guards migration_manager_ reads from const methods.

        /// Declared before @ref transaction_manager_, which borrows it.
        std::shared_ptr<Blockchain>           blockchain_;        ///< Blockchain service.
        std::shared_ptr<eth::EthWatchService> eth_watch_service_; ///< Shared EVM event watcher.
        std::shared_ptr<BridgeRelayer>        bridge_relayer_;    ///< Bridge burn→mint relayer.

        /// Quorum trio. ~BurnConfig and ~TrustedPeerRegistry each call Unregister(),
        /// which needs SecureCrdt alive — hence SecureCrdt is declared first and so
        /// destroyed last. On the teardown path this is the only thing that
        /// unregisters them; ShutdownAccountBoundServices(_, release_members=false)
        /// deliberately does not.
        std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt_; ///< BURN-02: quorum-signing wrapper.
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry_; ///< BURN-02: signer-set source.
        std::shared_ptr<sgns::account::BurnConfig> burn_config_; ///< BURN-02/BURN-03: live burn-rate source.

        std::shared_ptr<TransactionManager> transaction_manager_; ///< Transaction service.

        std::shared_ptr<processing::ProcessingTaskQueue>      task_queue_;          ///< Processing task queue.
        std::shared_ptr<processing::ProcessingCoreImpl>       processing_core_;     ///< Processing engine core.
        std::shared_ptr<processing::SubTaskResultStorageImpl> task_result_storage_; ///< Subtask result store.
        std::shared_ptr<processing::ProcessingServiceImpl>    processing_service_;  ///< Processing network service.

        std::shared_ptr<ChainRpcEndpointProvider>
            rpc_endpoint_provider_; ///< Shared so the posted Initialize() job can hold it across an account switch.
        std::unique_ptr<evmwatcher::BridgeCatchupWatcher>
            catchup_watcher_; ///< Polling watcher that scans historical blocks for bridge burns.
        std::unique_ptr<boost::asio::steady_timer> gc_timer_; ///< Periodic GC timer for result cache cleanup.

        // ───────────────────────────── end ownership order ────────────────────────────

        std::vector<std::string> my_task_ids_; ///< Recent task IDs submitted by this node (capped in memory).
        static constexpr size_t  kMyTasksMemoryLimit = 50;       ///< Max task IDs kept in @ref my_task_ids_.
        bool                     autodht_;                       ///< Whether DHT discovery is enabled.
        bool                     isprocessor_;                   ///< Whether processing service should run.
        NodeType                 node_type_ = NodeType::Light;   ///< Role from sgns_config.json (default Light).
        base::Logger             node_logger_;                   ///< Main node logger.
        GeniusNodeConfig         dev_config_;                    ///< Runtime node configuration.
        std::string              ipfs_cache_dir_ = "ipfs_cache"; ///< Directory for IPFS block flat-file cache.
        bool                     mirror_results_ = false; ///< Whether to mirror processing results from other nodes.
        int result_retention_hours_              = 168;   ///< Hours to retain results before GC (0 = keep forever).
        int result_retention_max_mb_             = 0;     ///< Max MB for result cache (0 = no space cap).

        std::vector<ChainContractPair> catchup_chains_; ///< Populated by OnRpcEndpointsReady for catch-up scan (D-02).

        /// Serializes catchup_scan_done_, catchup_scan_in_progress_, and
        /// catchup_chains_ across the RPC catch-up state and OnRpcEndpointsReady,
        /// which both run on the multi-threaded io_ pool (DEFAULT_IO_THREADS = 4).
        mutable std::mutex catchup_mutex_;
        std::function<std::optional<std::string>()>
            chainlist_fetcher_; ///< Optional custom chainlist fetcher (test injection point via SetChainlistFetcher).
        /// Generation token for async bridge init. Incremented on account
        /// switch; the posted Initialize() job captures the value at post time
        /// and aborts if it is stale — so a reset transaction_manager_ /
        /// bridge_relayer_ is never dereferenced by an in-flight init.
        std::atomic<uint64_t> bridge_init_generation_{ 0 };
        std::string           gnus_network_full_path_;       ///< Versioned network DB path.
        std::string           processing_channel_topic_;     ///< Processing task channel topic.
        std::string           processing_grid_chanel_topic_; ///< Processing grid topic.
        uint16_t              subnet_id_ = 0;                ///< Subnet ID from sgns_config.json (reserved).
        /// Starts the catchup scan watcher at bridge init (default true). Set false in sgns_config.json to disable.
        bool rpc_catchup_ = true;

        std::vector<std::string> bootstrap_peers_;
        std::vector<std::string> bootstrap_fullnodes_;
        std::vector<std::string> trusted_peers_genesis_;     ///< Genesis trusted-peer list (BURN-02/BURN-03).
        std::string              bootstrapper_node_address_; ///< Genesis bootstrapper address (BURN-02/BURN-03).
        /// Quorum threshold for TrustedPeerRegistry membership changes; 0 = unset (defaulted to the
        /// majority floor for the parsed genesis peer count in LoadSgnsConfig()).
        uint64_t trusted_peer_quorum_threshold_ = 0;
        /// Quorum threshold for BurnConfig updates; 0 = unset (defaulted the same way).
        uint64_t                                 burn_config_quorum_threshold_ = 0;
        std::vector<libp2p::peer::PeerInfo>      bootstrap_fullnode_infos_;
        std::unordered_set<libp2p::peer::PeerId> bootstrap_fullnode_ids_;
        std::vector<libp2p::peer::PeerInfo>      bootstrap_peer_infos_;
        std::unordered_set<libp2p::peer::PeerId> bootstrap_peer_ids_;
        uint16_t                                 pubsubport_; ///< Active PubSub TCP port.
        std::shared_ptr<sgns::trustedpeer::TrustStateStore>
            trust_state_store_; ///< Durable network-scoped trust authority.
        std::shared_ptr<sgns::account::TrustStartupController>
            trust_startup_controller_; ///< Restricted boot state machine.

        /**
         * @brief Constructs a node, creating the account from @p source AFTER LoadSgnsConfig()
         *        resolves node_type_ (the init-order hinge fix, INTF-03).
         *
         * Account creation runs via std::visit over the AccountSource variant, with
         * node_type_ already resolved. Throws std::runtime_error on account-restore
         * failure; the public New(dev_config, AccountSource) catches and returns nullptr (D-04).
         * Old private constructor above is retained this phase (deleted in Phase 3).
         *
         * @param[in] dev_config Runtime configuration for paths, token settings, and payout data.
         * @param[in] source Account-creation source variant.
         */
        GeniusNode( const GeniusNodeConfig &dev_config, AccountSource source );

        /**
         * @brief Initializes OpenSSL library state used by networking dependencies.
         */
        void InitOpenSSL();

        /**
         * @brief Loads sgns_config.json which contains net_id, subnet_id, bootstrap_fullnodes,
         *        authorized_full_node, and is_processor. All fields are optional and default to
         *        safe values (DEV net, empty bootstrap, is_processor=true).
         */
        void LoadSgnsConfig();

        /**
         * @brief Starts periodic garbage collection of expired processing results from disk cache.
         */
        void StartResultGC();

        /**
         * @brief Runs one GC pass: evicts expired result files and enforces space cap.
         */
        void RunResultGC();

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
         * @param[in] port_seed Deterministic per-address port seed used to derive the node
         *            listening port when @c pubsub_port is not set; zero requests an
         *            OS-assigned ephemeral port. Fallback when the
         *            @c port_seed key is absent from @c network_config.json; overridable by
         *            that key when present (config wins, param is fallback).
         * @param[in] node_type Node role; drives the connection-limit water marks
         *            (replicating roles — Full and Archive — get the higher limits).
         * @return True when network initialization succeeds.
         *
         * @par Port resolution priority
         * The PubSub listening port is resolved in priority order:
         *   1. @c pubsub_port (string override read from @c network_config.json) takes
         *      priority when present and non-empty.
         *   2. Otherwise @c port_seed (the constructor param, or the @c port_seed key from
         *      @c network_config.json when present) derives the port via
         *      @c GenerateRandomPort(port_seed, account_address), except zero which first
         *      resolves an OS-selected ephemeral port.
         */
        bool InitNetwork( uint16_t port_seed, NodeType node_type );

        /**
         * @brief Network knobs resolved from @c network_config.json, passed between the
         *        InitNetwork helpers. Members the node owns outright (@c autodht_,
         *        @c bootstrap_peers_, @c reconnect_config_) are written directly instead.
         */
        struct NetworkSettings
        {
            std::string bind_address = "0.0.0.0"; ///< PubSub bind address ("pubsub_bind_address").
            bool        upnp_enabled = true;      ///< Whether UPnP/IGD mapping is attempted.
            int         high_water   = 0;         ///< Connection-manager high water mark.
            int         low_water    = 0;         ///< Connection-manager low water mark.
            uint16_t    config_port  = 0;         ///< "pubsub_port" override; zero when unset.
            uint16_t    port_seed    = 0;         ///< "port_seed", or the constructor param when the key is absent.
        };

        /**
         * @brief Reads @c network_config.json, applying every key that is present and well-typed.
         * @param[in] port_seed Fallback seed, returned in @c NetworkSettings::port_seed unless a
         *            valid @c port_seed key overrides it.
         * @param[in] node_type Node role; seeds the default water marks before any config override.
         * @return Settings with defaults for absent or ill-typed keys.
         *
         * Also repopulates @c bootstrap_peers_ and updates @c autodht_ / @c reconnect_config_.
         */
        NetworkSettings LoadNetworkConfig( uint16_t port_seed, NodeType node_type );

        /**
         * @brief A parsed bootstrap peer set: the PeerInfos to dial and their IDs for lookup.
         */
        struct BootstrapPeers
        {
            std::vector<libp2p::peer::PeerInfo>      infos; ///< Successfully parsed peers, in input order.
            std::unordered_set<libp2p::peer::PeerId> ids;   ///< The same peers' IDs, for membership tests.
        };

        /**
         * @brief Resolves multiaddr strings into the peer set used for reconnection tracking.
         * @param[in] addresses Multiaddr strings to parse; unparseable entries are warned and skipped.
         * @param[in] kind Role word used in log messages ("fullnode" or "peer").
         * @return The parsed peers; empty when @p addresses is empty or nothing parsed.
         */
        BootstrapPeers ParseBootstrapPeers( const std::vector<std::string> &addresses, std::string_view kind ) const;

        /**
         * @brief Derives @c base58key_, then creates and starts PubSub on @ref pubsubport_.
         * @param[in] settings Resolved network settings (bind address, water marks).
         * @return True on success; on failure PubSub is stopped and reset before returning false.
         */
        bool StartPubSub( const NetworkSettings &settings );

        /**
         * @brief Adopts the OS-assigned TCP port into @ref pubsubport_ after an ephemeral bind.
         * @param[in] interface_address Multiaddr reported by PubSub once listening.
         * @return True when a non-zero port was recovered.
         */
        bool AdoptEphemeralPort( const std::string &interface_address );

        /**
         * @brief Brings up Bitswap, the FileManager singletons, and the GraphSync network.
         * @note Requires a started PubSub; uses its libp2p host.
         */
        void InitContentExchange();

        /**
         * @brief Loads the CRDT configuration.
         */
        void LoadCrdtConfig();

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
         * @param[in] delay Delay before retrying blockchain initialization.
         */
        void ScheduleBlockchainRetry( std::chrono::seconds delay = std::chrono::seconds( 5 ) );

        /**
         * @brief Resolves the bridge_chains_config.json path using D-01 priority:
         *        BaseWritePath → binary-relative → CWD fallback.
         * @return Resolved filesystem path to bridge_chains_config.json.
         */
        std::filesystem::path ResolveBridgeChainsConfigPath() const;

        /**
         * @brief Async bridge initialization launched from INITIALIZING_TRANSACTIONS.
         *
         * Thin orchestrator: resolves the config path, constructs the provider,
         * subscribes BridgeRelayer + self as observers, posts Initialize().
         * The relayer and catch-up scan receive chains via observer callbacks.
         */
        void InitializeAndStartBridge();

        /**
         * @brief IBridgeInitObserver callback — stores chain list for catch-up scan.
         * @param[in] chains  Chain/contract pairs discovered during initialization.
         */
        void OnRpcEndpointsReady( std::vector<ChainContractPair> chains ) override;

        /**
         * @brief Shuts down node services: cancels health-check timer, unsubscribes disconnect events,
         *        and stops the transaction GlobalDB.
         */
        void ShutdownForDestruction();

        /**
         * @brief Releases the runtime object graph after all node I/O threads have stopped.
         *
         * Dependencies are destroyed explicitly so objects that own PubSub subscriptions,
         * GraphSync handlers, or Asio operations do not outlive PubSub or its I/O context.
         */
        void ReleaseRuntimeMembersAfterIoStopped();

        /**
         * @brief Stops account-bound runtime services in dependency order.
         * @param[in] deconfigure_account Whether to clear account database callbacks after stopping services.
         * @param[in] release_members Whether to release service owners immediately after stopping them.
         */
        outcome::result<void> ShutdownAccountBoundServices( bool deconfigure_account, bool release_members = true );

        /**
         * @brief Unregisters and releases node-scoped policy services during full shutdown only.
         */
        void ShutdownNodePolicyServices();

        outcome::result<std::shared_ptr<crdt::AtomicTransaction>> CreateEscrowInfoCRDTTransaction(
            std::string        path,
            sgns::base::Buffer value );

        /**
         * @brief Starts DHT provider discovery for the processing grid topic.
         */
        outcome::result<void> DHTInit();

        /**
         * @brief Parse a multiaddr string into a PeerInfo, replicating ipfs_pubsub::PeerInfoFromString
         */
        static boost::optional<libp2p::peer::PeerInfo> ParsePeerInfoFromString( const std::string &multiaddr_str );

        /**
         * @brief Connect to a peer on the PubSub I/O thread, retrying transient failures.
         */
        void ConnectPeer( std::string peer, libp2p::peer::PeerInfo peer_info, unsigned attempt );

        /**
         * @brief Subscribe to libp2p disconnect events for bootstrap fullnodes
         */
        void InitBootstrapReconnect();

        /**
         * @brief Start the periodic health-check polling for bootstrap fullnode connections
         */
        void StartBootstrapHealthCheck();

        /**
         * @brief Schedule a reconnection attempt with exponential backoff
         */
        void ScheduleBootstrapReconnect( const libp2p::peer::PeerId &peer_id, unsigned attempt );

        /**
         * @brief Perform the actual reconnection to a bootstrap peer
         */
        void DoReconnectToBootstrapPeer( const libp2p::peer::PeerId &peer_id );

        /**
         * @brief Schedule the next periodic health check
         */
        void ScheduleNextHealthCheck();

        /**
         * @brief Perform a health check on all bootstrap fullnode connections
         */
        void PerformHealthCheck();

        /**
         * @brief Queries libp2p connectedness for @p peer on the host's own io thread.
         *
         * libp2p's ConnectionManagerImpl keeps its connection table
         * (`connections_`) in a bare unordered_map with no synchronisation: it
         * assumes every access happens on the single io_context thread that owns
         * the host. GossipPubSub runs that context on its own thread, while
         * GeniusNode's scheduler runs on a separate io_context with several
         * threads. Calling Host::connectedness() directly therefore walks the
         * connection table while the pubsub thread erases from it during
         * connect/disconnect churn, which segfaults on a torn shared_ptr.
         *
         * This helper posts the query onto the pubsub io_context and waits for
         * the answer, so the table is only ever read on its owning thread. When
         * the context is unavailable, already stopped, or when we are already
         * running on it, the query runs inline (posting would deadlock).
         *
         * @param[in] peer Peer to query.
         * @return Connectedness for @p peer, or NOT_CONNECTED when the query
         *         could not be completed (shutdown in progress or timed out).
         */
        libp2p::Host::Connectedness HostConnectedness( const libp2p::peer::PeerInfo &peer ) const;

        struct PriceInfo
        {
            double                                             price;      ///< Cached USD token price.
            std::chrono::time_point<std::chrono::system_clock> lastUpdate; ///< Time when @ref price was fetched.
        };

        std::map<std::string, PriceInfo>                   m_tokenPriceCache; ///< Cached token price data by token id.
        const std::chrono::minutes                         m_cacheValidityDuration{ 1 }; ///< Price cache TTL.
        std::chrono::time_point<std::chrono::system_clock> m_lastApiCall{}; ///< Last external price API call time.
        static constexpr std::chrono::seconds              MIN_API_CALL_INTERVAL{ 5 }; ///< Minimum price API interval.

        static constexpr size_t  DEFAULT_IO_THREADS = 4;                 ///< Default IO thread count.
        size_t                   io_thread_count_{ DEFAULT_IO_THREADS }; ///< IO thread count.
        std::vector<std::thread> io_threads_;                            ///< Threads running @ref io_.
        std::thread              upnp_thread;                            ///< Background UPnP refresh thread.
        std::atomic<bool>        stop_upnp{ false };                     ///< UPnP thread stop flag.
        std::string              base58key_;                             ///< Base58 key suffix for DB paths.

        std::atomic<NodeState> state_{ NodeState::CREATING }; ///< Current node lifecycle state.
        std::atomic_bool       shutdown_started_{ false };    ///< Whether shutdown has been initiated.
        std::atomic_uint blockchain_retry_count_{ 0 }; ///< Number of blockchain retries scheduled (test observable).

        // ── Bootstrap fullnode reconnection ──
        struct BootstrapReconnectConfig
        {
            std::chrono::seconds base_delay{ 5 };
            std::chrono::seconds max_delay{ 300 };
            std::chrono::seconds health_check_interval{ 60 };
            std::chrono::seconds health_check_disconnected_interval{ 15 };
            double               background_multiplier{ 3.0 };
        };

        BootstrapReconnectConfig                           reconnect_config_;
        std::optional<libp2p::event::Handle>               bootstrap_disconnect_subscription_;
        std::optional<libp2p::basic::Scheduler::Handle>    health_check_handle_;
        std::unordered_map<libp2p::peer::PeerId, unsigned> reconnect_attempts_;
        std::mutex                                         reconnect_mutex_;

        crdt::GlobalDB::BackupOptions crdt_backup_config_{ true, 15, 12, true };

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
      # buffer_size must stay near capacity * message size: Sink::push only calls
      # async_flush() at 4/5 of it, so the 4Mb default made that threshold
      # unreachable and left the sink waiting on its latency timer.
      buffer_size: 131072
      latency: 100
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

        /**
         * @brief Returns the path to the local task-ID persistence file.
         * @return Absolute path to my_tasks.json.
         */
        std::string MyTasksFilePath() const;

        /**
         * @brief Loads previously-submitted task IDs from the local JSON file.
         */
        void LoadMyTaskIds();

        /**
         * @brief Writes the current task ID list to the local JSON file.
         */
        void PersistMyTaskIds();
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, GeniusNode::Error );

/// Lets a NodeState be passed straight to any spdlog/fmt call: `logger->debug( "state {}", state )`.
template <>
struct fmt::formatter<sgns::GeniusNode::NodeState> : formatter<std::string_view>
{
    format_context::iterator format( sgns::GeniusNode::NodeState state, format_context &ctx ) const;
};

#endif
