/**
 * @file       TransactionManager.hpp
 * @brief      Transaction coordination, CRDT sync, and lifecycle tracking for outgoing and incoming account activity.
 * @date       2024-03-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRANSACTION_MANAGER_HPP_
#define _TRANSACTION_MANAGER_HPP_

#include <memory>
#include <deque>
#include <cstdint>
#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <optional>

#include <boost/asio/steady_timer.hpp>
#include <boost/format.hpp>
#include <boost/system/error_code.hpp>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/atomic_transaction.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "account/GeniusTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "account/NodeType.hpp"
#include "account/GeniusInputValidator.hpp"
#include "account/InputValidators.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "base/logger.hpp"
#include "base/buffer.hpp"

#include "blockchain/Blockchain.hpp"
#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"

namespace sgns::account
{
    class BurnConfig;
} // namespace sgns::account

namespace sgns
{
    using namespace boost::multiprecision;
    using EscrowDataPair = std::pair<std::string, base::Buffer>;

    /**
     * @brief Coordinates transaction creation, CRDT propagation, verification, and status tracking.
     */
    class TransactionManager : public std::enable_shared_from_this<TransactionManager>
    {
    public:
        static constexpr std::string_view GNUS_FULL_NODES_TOPIC        = "SuperGNUSNode.TestNet.FullNode";
        static constexpr std::string_view GNUS_FULL_NODES_TOPIC_LEGACY = "SuperGNUSNode.TestNet.FullNode.963";
        static constexpr std::chrono::milliseconds NONCE_REQUEST_TIMEOUT        = std::chrono::seconds(
            5 ); ///< Unified timeout for all nonce requests

        /// Fraction of an escrow payout burned to the zero address during PayEscrow, in basis points.
        /// Pre-quorum/genesis-absent fallback only -- the live value is cached in burn_basis_points_
        /// and refreshed via BurnConfig's quorum-signed CRDT value (BURN-02, BURN-03).
        static constexpr uint64_t BURN_BASIS_POINTS_DEFAULT = 100; // 1%
        static constexpr uint64_t BASIS_POINTS_TOTAL        = 10000;

        /**
         * @brief State of the Transaction Manager
         */
        enum class State : uint8_t
        {
            CREATING = 0, ///< Creating the object
            INITIALIZING, ///< Initializing the object
            SYNCING,      ///< Syncing the transactions
            READY,        ///< Ready to process transactions
        };

        using TransactionPair  = std::pair<std::shared_ptr<GeniusTransaction>, std::optional<std::vector<uint8_t>>>;
        using TransactionBatch = std::vector<TransactionPair>;
        using TransactionItem  = std::pair<TransactionBatch, std::optional<std::shared_ptr<crdt::AtomicTransaction>>>;
        using StateChangeCallback = std::function<void( const State &previous, const State &current )>;

        /**
         * @brief Status of a transaction
         */
        enum class TransactionStatus : uint8_t
        {
            CREATED,     ///< Transaction created but not yet sent
            SENDING,     ///< Transaction is being sent
            CONFIRMED,   ///< Transaction confirmed
            VERIFYING,   ///< Transaction being verified
            UNCONFIRMED, ///< Local outgoing transaction expired inconclusively
            FAILED,      ///< Transaction failed
            INVALID      ///< Invalid transaction
        };

        /**
         * @brief Value delivered when an asynchronous outgoing-transaction wait completes.
         *
         * A terminal transaction status has an empty @ref error. Timeouts and manager
         * shutdown report `timed_out` and `operation_aborted`, respectively.
         */
        struct TransactionCompletion
        {
            std::string               transaction_id;
            TransactionStatus         status{ TransactionStatus::INVALID };
            std::chrono::milliseconds elapsed{};
            boost::system::error_code error;
        };

        using TransactionCompletionCallback = std::function<void( TransactionCompletion )>;

        /**
         * @brief Factory constructor of the TransactionManager
         *
         * @param[in] processing_db Database of the CRDT
         * @param[in] ctx The io context used to run its inner methods
         * @param[in] account Genius account to be used
         * @param[in] node_type Deployment role of this node (Full / Light / Archive)
         * @param[in] timestamp_tolerance Time to analyze a transaction with the same nonce/key
         * @param[in] mutability_window Window of time where a transaction can be modified
         * @return shared_ptr to the fully-wired TransactionManager instance
         * @note Default timestamp_tolerance is 5 minutes (300000 ms)
         * @note Default mutability_window is 10 minutes (600000 ms)
         * @note timestamp_tolerance must be smaller than mutability_window
         */
        static std::shared_ptr<TransactionManager> New(
            std::shared_ptr<crdt::GlobalDB>            processing_db,
            std::shared_ptr<boost::asio::io_context>   ctx,
            std::shared_ptr<GeniusAccount>             account,
            std::shared_ptr<Blockchain>                blockchain,
            NodeType                                   node_type                 = NodeType::Light,
            uint16_t                                   subnet_id                 = 0,
            std::chrono::milliseconds                  timestamp_tolerance       = std::chrono::milliseconds( 300000 ),
            std::chrono::milliseconds                  mutability_window         = std::chrono::milliseconds( 0 ),
            uint64_t                                   initial_burn_basis_points = BURN_BASIS_POINTS_DEFAULT,
            std::shared_ptr<sgns::account::BurnConfig> burn_config               = nullptr );

        ~TransactionManager();

        void Start();
        void RegisterTopicNames();
        void StartListeningTopics();
        void StartCore();

        std::vector<std::vector<uint8_t>> GetOutTransactions() const;
        std::vector<std::vector<uint8_t>> GetInTransactions() const;
        size_t CountTransactions( std::optional<TransactionStatus> tx_status = std::nullopt ) const;

        /**
         * @brief Creates and enqueues a transfer transaction.
         * @param[in] amount  Amount to transfer.
         * @param[in] destination  Recipient address.
         * @param[in] token_id  Token being transferred.
         * @return Transaction hash on success.
         */
        outcome::result<std::string> TransferFunds( uint64_t amount, std::string destination, TokenID token_id );

        /**
         * @brief Creates and enqueues a mint transaction.
         * @param[in] amount  Amount to mint.
         * @param[in] transaction_hash  Source-chain transaction hash used as the previous hash in the DAG.
         * @param[in] chainid  Originating chain identifier.
         * @param[in] tokenid  Token to mint.
         * @param[in] destination  Recipient address; defaults to the local account address when empty.
         * @return Transaction hash on success.
         */
        outcome::result<std::string> MintFunds( uint64_t    amount,
                                                std::string transaction_hash,
                                                std::string chainid,
                                                TokenID     tokenid,
                                                std::string destination );

        /**
         * @brief Creates and enqueues a one-time migration mint transaction.
         * @param[in] amount  Amount to migrate.
         * @param[in] from_version  Legacy version namespace for the migration source key.
         * @param[in] tokenid  Token to mint.
         * @param[in] destination  Recipient address; defaults to the local account address when empty.
         * @return Transaction hash on success.
         */
        outcome::result<std::string> MigrationFunds( uint64_t    amount,
                                                     std::string from_version,
                                                     TokenID     tokenid,
                                                     std::string destination = "" );

        /**
         * @brief Creates and enqueues an escrow-hold transaction.
         *
         * Hashes @p job_id with blake2b-256 to derive the escrow destination address,
         * selects UTXOs, reserves them, and signs the transaction.
         *
         * @param[in] amount  Total amount to lock in escrow.
         * @param[in] dev_addr  Developer address that receives the remainder after peer payouts.
         * @param[in] peers_cut  Multiplier (as a TokenAmount) applied to the escrow amount to calculate the per-peer share.
         * @param[in] job_id  Job identifier whose blake2b-256 hash becomes the escrow destination address.
         * @return Pair of (transaction hash, (escrow address, serialized transaction)) on success.
         */
        outcome::result<std::pair<std::string, EscrowDataPair>> HoldEscrow( uint64_t           amount,
                                                                            const std::string &dev_addr,
                                                                            uint64_t           peers_cut,
                                                                            const std::string &job_id );

        outcome::result<std::string> PayEscrow( const std::string                       &escrow_path,
                                                const SGProcessing::TaskResult          &task_result,
                                                std::shared_ptr<crdt::AtomicTransaction> crdt_transaction );

        /**
         * @brief Submits an escrow payout and observes it without blocking for confirmation.
         *
         * Transaction construction is performed during initiation; confirmation is event-driven
         * on the manager io_context. Pending observations are cancelled by @ref Stop. The callback
         * must not own the GeniusNode; capture immutable context or a weak observer instead.
         */
        void AsyncPayEscrow( std::string                              escrow_path,
                             SGProcessing::TaskResult                 task_result,
                             std::shared_ptr<crdt::AtomicTransaction> crdt_transaction,
                             std::chrono::milliseconds                timeout,
                             TransactionCompletionCallback            callback );

        /**
         * @brief Asynchronously observes an already-tracked outgoing transaction.
         */
        void AsyncWaitForTransactionOutgoing( std::string                   tx_id,
                                              std::chrono::milliseconds     timeout,
                                              TransactionCompletionCallback callback );

        // Wait for an incoming transaction to be processed with a timeout
        TransactionStatus WaitForTransactionIncoming( const std::string        &txId,
                                                      std::chrono::milliseconds timeout ) const;
        // Wait for an outgoing transaction to be processed with a timeout
        TransactionStatus WaitForTransactionOutgoing( const std::string        &txId,
                                                      std::chrono::milliseconds timeout ) const;

        /**
         * @brief Polls until an EscrowReleaseTransaction referencing @p originalEscrowId
         *        reaches a terminal state or @p timeout expires.
         * @return TransactionStatus of the release tx, or INVALID if not found within timeout.
         */
        TransactionStatus WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                std::chrono::milliseconds timeout ) const;

        static std::string GetTransactionPath( uint16_t base, const std::string &tx_hash );
        static std::string GetTransactionPath( const GeniusTransaction &element );
        static std::string GetTransactionPath( const std::string &tx_hash );
        static std::string GetTransactionProofPath( const GeniusTransaction &element );

        /**
         * @brief Fetches and deserializes a transaction from the CRDT by key.
         */
        static outcome::result<std::shared_ptr<GeniusTransaction>> FetchTransaction( crdt::GlobalDB  &db,
                                                                                     std::string_view transaction_key );
        static outcome::result<std::shared_ptr<GeniusTransaction>> DeSerializeTransaction(
            const base::Buffer &tx_data );

        State GetState() const
        {
            return state_m;
        }

        TransactionStatus GetTransactionStatusByTxId( const std::string &txId ) const;
        TransactionStatus GetOutgoingStatusByTxId( const std::string &txId ) const;

        /**
         * @brief Finds every tracked transaction in @p element's nonce slot except @p element itself.
         */
        std::vector<std::shared_ptr<GeniusTransaction>> GetConflictingTransactions(
            const GeniusTransaction &element ) const;

        /**
         * @brief Idempotent stop. Sets the stopped flag and wakes the tick loop.
         */
        void Stop();

        void RegisterStateChangeCallback( StateChangeCallback callback );
        void UnregisterStateChangeCallback();

        static std::string StateToString( State state )
        {
            switch ( state )
            {
                case State::CREATING:
                    return "CREATING";
                case State::INITIALIZING:
                    return "INITIALIZING";
                case State::SYNCING:
                    return "SYNCING";
                case State::READY:
                    return "READY";
                default:
                    return "UNKNOWN";
            }
        }

        /// @brief Builds the blockchain key prefix "/bc-<network_id>/" for the given network.
        static std::string GetBlockChainBase( uint16_t network_id );

        /// @brief Overload using the current network ID.
        static std::string GetBlockChainBase();

        /**
         * @brief Queries all transaction keys from the CRDT across monitored networks
         *        and processes each one via FetchAndProcessTransaction.
         */
        void QueryTransactions();

        /**
         * @brief Deserializes, parses, and adds a single transaction to the processed map.
         *
         * Skips transactions that are already tracked. When @p tx_data is provided it
         * is deserialized directly; otherwise the transaction is fetched from the CRDT
         * by @p tx_key. On success the peer nonce is updated and the transaction is
         * recorded as CONFIRMED.
         *
         * @param[in] tx_key  Full CRDT key of the transaction.
         * @param[in] tx_data  Optional pre-fetched serialized data (avoids a CRDT read).
         */
        outcome::result<void> FetchAndProcessTransaction( const std::string          &tx_key,
                                                          std::optional<base::Buffer> tx_data = std::nullopt );

        static outcome::result<std::shared_ptr<GeniusTransaction>> DeSerializeTransaction( std::string tx_data );

        /**
         * @brief Deserializes from EmbeddedTransaction proto oneof field.
         *        Dispatches on the oneof case instead of manual type string lookup.
         */
        static outcome::result<std::shared_ptr<GeniusTransaction>> DeSerializeEmbeddedTransaction(
            const EmbeddedTransaction &embedded );

    protected:
        friend class GeniusNode;
        friend class Migration3_6_0To3_7_0;
        friend class CertificateFallbackTestAccess;
        friend class TransactionManagerPendingLifecycleTestAccess;
        void EnqueueTransaction( TransactionPair element );
        void EnqueueTransaction( TransactionItem element );

        void SetTimeFrameToleranceMs( uint64_t timeframe_tolerance );
        void SetMutabilityWindowMs( uint64_t mutability_window );

    private:
        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "/bc-%hu/";

        struct PendingTransactionWait
        {
            PendingTransactionWait( boost::asio::io_context              &context,
                                    std::string                           id,
                                    TransactionCompletionCallback         completion_callback,
                                    std::chrono::steady_clock::time_point start_time ) :
                timer( context ),
                tx_id( std::move( id ) ),
                callback( std::move( completion_callback ) ),
                started_at( start_time )
            {
            }

            boost::asio::steady_timer             timer;
            std::string                           tx_id;
            TransactionCompletionCallback         callback;
            std::chrono::steady_clock::time_point started_at;
            std::atomic_bool                      completed{ false };
        };

        struct TrackedTx
        {
            std::shared_ptr<GeniusTransaction> tx;
            TransactionStatus                  status;
            uint64_t                           cached_nonce; // Cache nonce to avoid dereferencing tx
        };

        struct ReplayProtectionResult
        {
            ConsensusManager::ValidationResult validation = ConsensusManager::ValidationResult::Approve();
        };

        struct AccountUTXOState
        {
            uint64_t      version{ 0 };
            base::Hash256 root{};
            bool          initialized{ false };
        };

        TransactionManager( std::shared_ptr<crdt::GlobalDB>            processing_db,
                            std::shared_ptr<boost::asio::io_context>   ctx,
                            std::shared_ptr<GeniusAccount>             account,
                            std::shared_ptr<Blockchain>                blockchain,
                            NodeType                                   node_type,
                            uint16_t                                   subnet_id,
                            std::chrono::milliseconds                  timestamp_tolerance,
                            std::chrono::milliseconds                  mutability_window,
                            uint64_t                                   initial_burn_basis_points,
                            std::shared_ptr<sgns::account::BurnConfig> burn_config );

        // Parser function pointer alias: returns a set of topic strings or an error
        using TransactionParserFn =
            outcome::result<void> ( TransactionManager::* )( const std::shared_ptr<GeniusTransaction> & );

        SGTransaction::DAGStruct FillDAGStruct( std::optional<std::string> other_chain_hash = std::nullopt );
        std::string              GetOutgoingPreviousHash( uint64_t nonce ) const;
        std::string              GetTrackedOutgoingPreviousHash( uint64_t nonce ) const;
        std::string              GetPersistedOutgoingPreviousHash( uint64_t nonce ) const;
        std::string              QueryOutgoingPreviousHashFromCRDT( uint64_t nonce ) const;

        /**
         * @brief Commits a TransactionItem to the CRDT.
         *
         * Validates that each transaction in the batch carries the expected
         * sequential nonce relative to the confirmed nonce. On non-full-node
         * instances a network-unreachable error is forwarded as a timed_out
         * failure so the caller can keep the item for retry. On success the
         * transactions are parsed locally (UTXO updates, etc.), published to
         * the relevant topics, and their status is set to VERIFYING (or
         * CONFIRMED on a full node).
         *
         * @return Set of nonces that were successfully sent.
         */
        outcome::result<void> SendTransactionItem( TransactionItem &item );

        /**
         * @brief Rolls back a failed TransactionItem.
         *
         * Re-fetches the confirmed nonce (falling back to local state), marks
         * intermediate nonces as VERIFYING for re-check, sets the rolled-back
         * transactions to FAILED, reverts their UTXO side-effects, and releases
         * their reserved nonces.
         */
        outcome::result<void> RollbackTransactions( TransactionItem &item_to_rollback );

        /**
         * @brief Returns the set of network IDs to monitor.
         *        On DEV_NET (144), also includes TEST_NET (963) and MAIN_NET (369).
         */
        static std::vector<uint16_t> GetMonitoredNetworkIDs();

        /**
         * @brief Derives the proof key that corresponds to a transaction key by
         *        replacing "/tx/" with "/proof/".
         */
        static outcome::result<std::string> GetExpectedProofKey( const std::string                        &tx_key,
                                                                 const std::shared_ptr<GeniusTransaction> &tx );

        /**
         * @brief Inverse of GetExpectedProofKey — derives the tx key from a proof key.
         */
        static outcome::result<std::string> GetExpectedTxKey( const std::string &proof_key );

        /**
         * @brief Dispatches to the type-specific parser registered in transaction_parsers.
         */
        outcome::result<void> ParseTransaction( const std::shared_ptr<GeniusTransaction> &tx );

        /**
         * @brief Dispatches to the type-specific reverter registered in transaction_parsers.
         */
        outcome::result<void> RevertTransaction( const std::shared_ptr<GeniusTransaction> &tx );
        void UpdateAccountUTXOState( const std::shared_ptr<GeniusTransaction> &tx, bool increment_version );

        /**
         * @brief Loads UTXOs from local storage and/or the network, then processes
         *        the parent transactions of each UTXO. Any transactions that cannot
         *        be found are added to missing hashes for later resolution.
         *        Falls back to a full QueryTransactions when neither source has data.
         */
        void InitializeUTXOs();

        /**
         * @brief Attempts to resolve missing hashes by requesting them from
         *        the network. Transitions to READY when none remain and the nonce
         *        check passes.
         */
        void InitTransactions();

        /**
         * @brief Verifies that the local nonce is not behind the network nonce.
         *        Full nodes are allowed through even when the network is unreachable.
         * @return true if nonce is in sync (or we're a full node with no network).
         */
        bool CheckNonce() const;

        /**
         * @brief Compares the local proposed nonce with the network-confirmed nonce.
         *        Transitions back to READY when they match, checks validity when
         *        ahead, or requests heads when behind.
         */
        void SyncNonce();

        /**
         * @brief Request heads for relevant topics when we detect we're behind.
         */
        void RequestRelevantHeads();

        /**
         * @brief Validates signatures of outgoing transactions at the given nonces.
         *
         * Transactions with invalid signatures (checked current then legacy) are
         * removed from processed maps and deleted from the CRDT. Valid ones are
         * promoted to CONFIRMED.
         *
         * @param[in] nonces_to_check  Set of nonces to validate.
         * @return true if any transactions were invalidated.
         */
        outcome::result<bool> CheckTransactionValidity( const std::set<uint64_t> &nonces_to_check );

        /**
         * @brief Removes a transaction key from the CRDT within an atomic transaction,
         *        publishing to @p topics.
         */
        outcome::result<void> DeleteTransaction( std::string tx_key, const std::unordered_set<std::string> &topics );

        /// @brief Thread-safe lookup of an outgoing transaction by hash.
        std::shared_ptr<GeniusTransaction> GetTransactionByHash( const std::string &tx_hash ) const;

        /// @brief Same as GetTransactionByHash but assumes tx_mutex_m is already held.
        std::shared_ptr<GeniusTransaction> GetTransactionByHashNoLock( const std::string &tx_hash ) const;

        std::optional<TrackedTx> GetTrackedTxByNonceAndAddress( uint64_t nonce, const std::string &address ) const;
        std::optional<TrackedTx> GetTrackedTxByHash( const std::string &tx_hash ) const;

        TransactionStatus GetStatusByTxId( const std::string &txId, std::optional<bool> outgoing ) const;
        bool              SetOutgoingStatusByNonce( uint64_t nonce, TransactionStatus s );
        static bool       IsTerminalTransactionStatus( TransactionStatus status );
        void              NotifyTransactionStatusChanged( const std::string &tx_id );
        void              CompleteTransactionWait( const std::shared_ptr<PendingTransactionWait> &wait,
                                                   TransactionStatus                              status,
                                                   boost::system::error_code                      error = {} );
        void              CancelPendingTransactionWaits();

        /**
         * @brief Single iteration of the main processing loop.
         *
         * Drains the new-data and deleted-data queues, processes them, then
         * executes the state-specific action (INITIALIZING → InitTransactions,
         * SYNCING → SyncNonce, READY → send queued transactions).
         * Runs ConfirmTransactions and periodic sync regardless of state.
         */
        void TickOnce();

        outcome::result<ConsensusManager::Check> OnConsensusCertificate( const std::string          &tx_hash,
                                                                         const ConsensusCertificate &certificate );
        /**
         * @brief Handles proposal timeout cleanup for VERIFYING tracking entries.
         *        Called via ProposalCleanupHandler from ConsensusManager when a proposal slot is cleaned
         *        up due to timeout. Local outgoing entries become UNCONFIRMED; remote temporary entries are
         *        removed. CONFIRMED entries are left untouched. Missing entries are skipped silently.
         * @param[in] tx_hash Transaction hash identifying the tracking entry to clean up.
         */
        void OnProposalTimeoutCleanup( const std::string &tx_hash );

        std::shared_ptr<crdt::GlobalDB> globaldb_m;

        std::shared_ptr<boost::asio::io_context> ctx_m;
        std::shared_ptr<GeniusAccount>           account_m;
        std::shared_ptr<Blockchain>              blockchain_;
        NodeType                                 node_type_m;       ///< Deployment role driving replication behavior.
        uint16_t                                 subnet_id_ = 0;    ///< Subnet ID from config (reserved).
        std::string                              full_node_topic_m; ///< formatted full-node topic
        State                                    state_m;
        std::mutex                               state_change_callback_mutex_;
        StateChangeCallback                      state_change_callback_;

        // Head request rate limiting (for reactive requests due to nonce gaps)
        std::optional<std::chrono::steady_clock::time_point> last_head_request_time_;

        // Periodic sync - request heads every 10 minutes to stay in sync across devices/instances
        std::chrono::steady_clock::time_point last_periodic_sync_time_;
        std::atomic<bool>                     received_first_periodic_sync_response_{
            false }; // Track if we've gotten at least one response

        static constexpr std::chrono::minutes PERIODIC_SYNC_INTERVAL         = std::chrono::minutes( 10 );
        static constexpr std::chrono::seconds INITIAL_PERIODIC_SYNC_INTERVAL = std::chrono::seconds( 30 );

        // for the SendTransactionItem thread support
        mutable std::mutex          mutex_m;
        std::deque<TransactionItem> tx_queue_m;

        mutable std::shared_mutex                                   tx_mutex_m;
        std::unordered_map<std::string, TrackedTx>                  tx_processed_m;
        mutable std::shared_mutex                                   account_utxo_state_mutex_;
        mutable std::unordered_map<std::string, AccountUTXOState>   account_utxo_state_;
        std::atomic<uint32_t>                                       utxo_state_tracking_suppression_{ 0 };
        std::unordered_map<std::string, ConsensusManager::Proposal> pending_proposals_;
        std::function<void()>                                       task_m;
        std::atomic<bool>                                           stopped_{ false };
        std::mutex                                                  payout_submission_mutex_;
        std::mutex                                                  transaction_waits_mutex_;
        std::unordered_map<std::string, std::vector<std::shared_ptr<PendingTransactionWait>>> transaction_waits_;
        std::chrono::milliseconds                                                             timestamp_tolerance_m;
        std::chrono::milliseconds                                                             mutability_window_m;
        uint64_t nonce_window_m = DEFAULT_NONCE_WINDOW;

        // METRICS-01: Operational metrics counters
        // Atomic counters tracking vote rates, validation breakdown, and transaction lifecycle.
        // Flushed to log on TransactionManager destruction (per D-12/D-13/D-14).
        std::atomic<uint64_t> metrics_cert_fallback_success_{ 0 };
        std::atomic<uint64_t> metrics_cert_fallback_failure_{ 0 };
        std::atomic<uint64_t> metrics_validation_approve_{ 0 };
        std::atomic<uint64_t> metrics_validation_reject_{ 0 };
        std::atomic<uint64_t> metrics_tracking_insert_{ 0 };
        std::atomic<uint64_t> metrics_tracking_confirm_{ 0 };
        std::atomic<uint64_t> metrics_tracking_fail_{ 0 };

        /// @brief Live, cached burn-rate basis-points value (BURN-02, BURN-03).
        ///        Refreshed via BurnConfig::RegisterRefreshCallback; never a direct CRDT read.
        std::atomic<uint64_t> burn_basis_points_{ BURN_BASIS_POINTS_DEFAULT };

        static constexpr std::chrono::milliseconds TIMESTAMP_TOLERANCE  = std::chrono::seconds( 10 );
        static constexpr std::chrono::milliseconds MUTABILITY_WINDOW    = std::chrono::minutes( 15 );
        static constexpr uint64_t                  DEFAULT_NONCE_WINDOW = 5;

        std::mutex                                         cv_mutex_;
        std::condition_variable                            cv_;
        std::queue<crdt::CRDTCallbackManager::NewDataPair> new_data_queue_;
        std::queue<std::string>                            deleted_data_queue_;

        std::chrono::steady_clock::time_point last_loop_time_;
        std::atomic<bool>                     topic_names_registered_{ false };
        std::atomic<bool>                     listening_topics_started_{ false };
        std::atomic<bool>                     core_started_{ false };

        std::mutex                      missing_tx_mutex_;
        std::unordered_set<std::string> missing_tx_hashes_;

        std::chrono::steady_clock::time_point         last_init_tx_request_time_{};
        mutable std::chrono::steady_clock::time_point last_nonce_request_time_{};
        static constexpr std::chrono::milliseconds    k_init_tx_request_cooldown_ms{ 5000 };

        /// @brief Bridge mint reservation/persistence constants.
        static constexpr std::string_view kBridgeExecutedPrefix = "/bridge/executed/";
        static constexpr std::string_view kBridgeKeySeparator   = ":";

        outcome::result<void> ParseTransferTransaction( const std::shared_ptr<GeniusTransaction> &tx );
        outcome::result<void> ParseMintTransaction( const std::shared_ptr<GeniusTransaction> &tx );
        outcome::result<void> ParseEscrowTransaction( const std::shared_ptr<GeniusTransaction> &tx );
        outcome::result<void> RevertTransferTransaction( const std::shared_ptr<GeniusTransaction> &tx );
        outcome::result<void> RevertMintTransaction( const std::shared_ptr<GeniusTransaction> &tx );
        outcome::result<void> RevertEscrowTransaction( const std::shared_ptr<GeniusTransaction> &tx );
        outcome::result<void> PutProducedUTXOs( const GeniusTransaction &tx );
        outcome::result<void> DeleteProducedUTXOs( const GeniusTransaction &tx );

        static const std::unordered_map<std::string, std::pair<TransactionParserFn, TransactionParserFn>>
            transaction_parsers;

        base::Logger m_logger = base::createLogger( "TransactionManager" );

        /**
         * @brief CRDT element filter for incoming transactions.
         *
         * Deserializes the element, verifies its signature,
         * and checks for nonce conflicts. Rejected elements are returned as
         * tombstones together with their associated proof key.
         *
         * @return nullopt to accept, or a vector of tombstone elements to reject.
         */
        std::optional<std::vector<crdt::pb::Element>> FilterTransaction( const crdt::pb::Element &element );

        /**
         * @brief CRDT element filter for incoming proofs.
         *
         * Currently accepts all proofs that are already stored or newly arriving
         * (full verification path is present but short-circuited).
         * Invalid proofs are tombstoned together with their associated tx key.
         *
         * @return nullopt to accept, or a vector of tombstone elements to reject.
         */
        std::optional<std::vector<crdt::pb::Element>> FilterProof( const crdt::pb::Element &element );

        static uint64_t GetCurrentTimestamp();

        /**
         * @brief Computes @p current_timestamp − @p timestamp in milliseconds.
         *        Result may be negative when the timestamp is in the future.
         */
        int64_t GetElapsedTime( uint64_t timestamp, uint64_t current_timestamp ) const;

        /// @overload Uses the current wall-clock time.
        int64_t GetElapsedTime( uint64_t timestamp ) const;

        /**
         * @brief Returns true when the transaction's age exceeds mutability window.
         *        A window of zero means transactions are always mutable.
         *        Future-timestamped transactions are never considered immutable.
         */
        bool IsTransactionImmutable( const GeniusTransaction &tx ) const;

        /**
         * @brief Removes a transaction from map, reverts its UTXO
         *        side-effects, rolls back the peer nonce, and optionally deletes
         *        the key from the CRDT.
         */
        outcome::result<void> RemoveTransactionFromProcessedMaps( const std::string &transaction_key,
                                                                  bool               delete_from_crdt = false );

        /**
         * @brief Deserializes, conflict-checks, parses, and inserts a new
         *        transaction into tx_processed_m. Conflicting transactions are
         *        removed (with CRDT deletion) before the new one is applied.
         */
        outcome::result<void> AddTransactionToProcessedMaps( crdt::CRDTCallbackManager::NewDataPair new_data );

        /**
         * @brief Persists a tx-key → CID mapping in the RocksDB datastore so that
         *        the CID can be retrieved later via GetTransactionCID.
         */
        outcome::result<void> StoreTransactionCID( const std::string &key, const std::string &cid );

        void ProcessDeletion( std::string deleted_key );
        void ProcessNewData( crdt::CRDTCallbackManager::NewDataPair new_data );

        /**
         * @brief CRDT new-element callback. Stores the CID, pushes the data onto
         *        new_data_queue_, and wakes the tick loop.
         */
        void NewElementCallback( crdt::CRDTCallbackManager::NewDataPair new_data, std::string cid );

        /**
         * @brief CRDT deleted-element callback. Pushes the key onto
         *        deleted_data_queue_ and wakes the tick loop.
         */
        void DeleteElementCallback( std::string deleted_key );

        /**
         * @brief Updates state_m and fires the state change callback when the state actually changes.
         */
        void ChangeState( State new_state );

    public:
        enum class WitnessValidationResult : uint8_t
        {
            VALID,
            DRIFT,
            INVALID
        };

        /**
         * @brief Looks up the CID associated with a transaction hash in RocksDB,
         *        searching across all monitored networks.
         */
        outcome::result<std::string>                        GetTransactionCID( const std::string &tx_hash ) const;
        outcome::result<ConsensusManager::ValidationResult> HandleNonceConsensusSubject(
            const ConsensusManager::Subject &subject );
        ConsensusManager::ValidationResult ValidateTransactionForConsensus(
            const std::shared_ptr<GeniusTransaction> &tx ) const;
        bool                   CheckTransactionWellFormed( const GeniusTransaction &tx ) const;
        bool                   CheckTransactionAuthorization( const GeniusTransaction &tx ) const;
        bool                   CheckTransactionTimestamp( const GeniusTransaction &tx ) const;
        bool                   CheckTransactionReplayProtection( const GeniusTransaction &tx ) const;
        ReplayProtectionResult EvaluateTransactionReplayProtection( const GeniusTransaction &tx ) const;
        bool                   CheckTransactionTypeRules( const std::shared_ptr<GeniusTransaction> &tx ) const;
        std::optional<UTXOTransitionCommitment> BuildUTXOTransitionCommitment(
            const std::shared_ptr<GeniusTransaction> &tx ) const;
        std::optional<UTXOWitness> BuildUTXOWitness( const std::shared_ptr<GeniusTransaction> &tx ) const;
        bool                       ApplyTransactionToUTXOSnapshot( const std::shared_ptr<GeniusTransaction> &tx,
                                                                   std::vector<GeniusUTXO>                  &snapshot ) const;
        WitnessValidationResult    ValidateWitnessForConsensus( const ConsensusSubject                   &subject,
                                                                const std::shared_ptr<GeniusTransaction> &tx ) const;
        bool ValidateUTXOParametersForConsensus( const UTXOTxParameters &params, const std::string &address ) const;
        void SetNonceWindow( uint64_t window );
        outcome::result<void> ChangeTransactionState( const std::shared_ptr<GeniusTransaction> &tx,
                                                      TransactionStatus                         new_status );
        bool                  HasConfirmedInputConflict( const std::shared_ptr<GeniusTransaction> &candidate_tx ) const;

        bool KeyExistsInDB( const std::string &key ) const;

        /**
         * @brief Obtains the public-chain input validator for RPC endpoint wiring.
         * @return Mutable reference to the PublicChainInputValidator.
         */
        PublicChainInputValidator &GetPublicChainInputValidator() noexcept
        {
            return public_chain_input_validator_;
        }

        /**
         * @brief Obtains the public-chain input validator for RPC endpoint wiring (const).
         * @return Const reference to the PublicChainInputValidator.
         */
        const PublicChainInputValidator &GetPublicChainInputValidator() const noexcept
        {
            return public_chain_input_validator_;
        }

    private:
        static constexpr std::string_view GENIUS_CHAIN_ID = "supergenius";

        struct InputValidatorSelection
        {
            std::string            chain_id;
            const IInputValidator &validator;
        };

        InputValidatorSelection SelectInputValidator( const std::shared_ptr<GeniusTransaction> &tx ) const;

        GeniusInputValidator      genius_input_validator_;
        PublicChainInputValidator public_chain_input_validator_;
    };
}

template <>
struct fmt::formatter<sgns::TransactionManager::State> : formatter<std::string_view>
{
    format_context::iterator format( sgns::TransactionManager::State s, format_context &ctx ) const;
};

#endif
