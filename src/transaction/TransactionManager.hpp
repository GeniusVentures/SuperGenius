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
#include "transaction/TransactionConsensusHandler.hpp"
#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"

namespace sgns::account
{
    class BurnConfig;
    class ConfirmedBurnValueProvider;
} // namespace sgns::account

namespace sgns
{
    class MintTransactionV2;
    class EscrowTransaction;

    using namespace boost::multiprecision;
    using EscrowDataPair = std::pair<std::string, base::Buffer>;

    /**
     * @brief Discovery entry returned by GetRegistrationsForMain.
     *
     * Each entry identifies a child wallet registered to the queried main wallet.
     */
    struct RegistrationDiscoveryEntry
    {
        std::string                         child_addr;  ///< Child wallet public address (128-hex)
        std::string                         main_addr;   ///< Main wallet public address (128-hex)
        uint64_t                            sequence;    ///< Registration sequence number
        SGTransaction::RegistrationMetadata metadata;    ///< Registration metadata
    };

    /**
     * @brief Coordinates transaction creation, CRDT propagation, verification, and status tracking.
     */
    class TransactionManager : public std::enable_shared_from_this<TransactionManager>
    {
    public:
        static constexpr std::string_view          GNUS_FULL_NODES_TOPIC        = "SuperGNUSNode.TestNet.FullNode";
        static constexpr std::string_view          GNUS_FULL_NODES_TOPIC_LEGACY = "SuperGNUSNode.TestNet.FullNode.963";
        static constexpr std::chrono::milliseconds NONCE_REQUEST_TIMEOUT        = std::chrono::seconds(
            5 ); ///< Unified timeout for all nonce requests

        /// Fraction of an escrow payout burned to the zero address during PayEscrow, in basis points.
        /// Pre-quorum/genesis-absent fallback only -- the live value is cached in burn_basis_points_
        /// and refreshed via BurnConfig's quorum-signed CRDT value (BURN-02, BURN-03).
        static constexpr uint64_t BURN_BASIS_POINTS_DEFAULT = 100; // 1%
        static constexpr uint64_t BASIS_POINTS_TOTAL        = 10000;

        enum class Error : uint8_t
        {
            TRUST_POLICY_NOT_READY = 1,
        };

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
            std::shared_ptr<crdt::GlobalDB>          processing_db,
            std::shared_ptr<boost::asio::io_context> ctx,
            std::shared_ptr<GeniusAccount>           account,
            std::shared_ptr<Blockchain>              blockchain,
            NodeType                                 node_type                 = NodeType::Light,
            uint16_t                                 subnet_id                 = 0,
            std::chrono::milliseconds                timestamp_tolerance       = std::chrono::milliseconds( 300000 ),
            std::chrono::milliseconds                mutability_window         = std::chrono::milliseconds( 0 ),
            uint64_t                                 initial_burn_basis_points = BURN_BASIS_POINTS_DEFAULT,
            std::shared_ptr<const sgns::account::ConfirmedBurnValueProvider> confirmed_burn_provider = nullptr );

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
         * @brief Creates and enqueues a child-wallet registration transaction (caller-supplied sequence).
         */
        outcome::result<std::string> RegisterChild( std::string                         main_address,
                                                    SGTransaction::RegistrationMetadata metadata,
                                                    uint64_t                            sequence );
        /**
         * @brief Creates and enqueues a child-wallet registration transaction with auto-derived sequence.
         */
        outcome::result<std::string> RegisterChild( std::string                         main_address,
                                                    SGTransaction::RegistrationMetadata metadata );

        /**
         * @brief Recovers funds from a registered child wallet back to this account (D-60/D-62/CONS-02).
         */
        outcome::result<std::string> RecoverFromChild( std::string child_address,
                                                       uint64_t    amount,
                                                       TokenID     token_id );

        /**
         * @brief Creates and enqueues a child-initiated Detach transaction (D-35, caller-supplied sequence).
         */
        outcome::result<std::string> DetachChild( SGTransaction::RegistrationMetadata metadata,
                                                  uint64_t                            sequence,
                                                  uint64_t                            supersedes_sequence );
        /**
         * @brief Creates and enqueues a child-initiated Detach transaction with auto-derived sequence.
         */
        outcome::result<std::string> DetachChild( SGTransaction::RegistrationMetadata metadata );

        /**
         * @brief Creates and enqueues a child-initiated Replace-Main transaction (D-37, caller-supplied sequence).
         */
        outcome::result<std::string> ReplaceMain( std::string                         new_main_address,
                                                  SGTransaction::RegistrationMetadata metadata,
                                                  uint64_t                            sequence,
                                                  uint64_t                            supersedes_sequence );
        /**
         * @brief Creates and enqueues a child-initiated Replace-Main transaction with auto-derived sequence.
         */
        outcome::result<std::string> ReplaceMain( std::string                         new_main_address,
                                                  SGTransaction::RegistrationMetadata metadata );

        /**
         * @brief Creates and enqueues a main-initiated Revoke transaction (D-36).
         */
        outcome::result<std::string> RevokeChild( std::string child_address );

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
         * @param[in] job_id  Job identifier whose blake2b-256 hash becomes the escrow destination address.
         * @param[in] network_scope  Private-network identity scoping the escrow chain id
         *             (empty = public scope; the chain id stays at the genius default).
         * @return Pair of (transaction hash, (escrow address, serialized transaction)) on success.
         *
         * @note The escrow hold carries no payout metadata. The developer address and cut are
         *       reported per subtask by the processing peer that ran the work, since peers of a
         *       single job may be running apps from different developers.
         */
        outcome::result<std::pair<std::string, EscrowDataPair>> HoldEscrow( uint64_t           amount,
                                                                            const std::string &job_id,
                                                                            std::string        network_scope = "" );

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
         * @brief Finds one tracked transaction in @p element's nonce slot (first by map
         *        iteration) other than @p element itself.
         * @return The conflicting transaction, or an error when none is tracked.
         */
        outcome::result<std::shared_ptr<GeniusTransaction>> GetConflictingTransaction(
            const GeniusTransaction &element ) const;

        /** @brief BestHash tie-break: true when @p new_tx outranks @p existing_tx. */
        bool ShouldReplaceTransaction( const GeniusTransaction &existing_tx,
                                       const GeniusTransaction &new_tx ) const;

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
        friend class RegistrationE2ETestAccess;
        friend class RegTestAccess;
        friend class MultiNodeFinalityFaultTestAccess;
        friend class MultiAccountTestAccess;
        friend class TransactionConsensusHandler;
        void EnqueueTransaction( TransactionPair element );
        void EnqueueTransaction( TransactionItem element );

        void SetTimeFrameToleranceMs( uint64_t timeframe_tolerance );
        void SetMutabilityWindowMs( uint64_t mutability_window );

    private:
        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "/bc-%hu/";

        /// Destination of the burned fraction of an escrow payout.
        static constexpr std::string_view BURN_ADDRESS = "0x0000000000000000000000000000000000000000";

        /// Scale of SubTaskResult::developer_cut; 1'000'000 == 100%.
        static constexpr uint64_t DEVELOPER_CUT_SCALE = 1000000;

        /**
         * @brief Splits an escrow amount across contributing peers, their developers and the burn.
         *
         * Each subtask result names the peer that did the work plus the developer of the app that
         * ran it, so a single job whose subtasks were processed by different apps pays each
         * developer its own cut. The results share an even split of the amount left after the
         * burn (the split remainder is burned too); each result's share is divided by its
         * @c developer_cut, floored in the developer's disfavor, and minted in that result's
         * token. Results with malformed metadata are skipped, they neither block the payout nor
         * earn from it.
         *
         * @param[in] task_result Collected subtask results carrying peer and developer payout metadata.
         * @param[in] escrow_amount Total amount locked by the escrow hold.
         * @param[in] escrow_token_id Token of the escrow lock output, used for the burn output.
         * @param[in] burn_basis_points Fraction of the escrow burned before the peer/developer split.
         * @return Outputs in result order, then developers by (address, token), burn last; the
         *         burn output is always present, zero-valued peer and developer credits are omitted.
         */
        static outcome::result<std::vector<OutputDestInfo>> BuildPayoutOutputs(
            const SGProcessing::TaskResult &task_result,
            uint64_t                        escrow_amount,
            const TokenID                  &escrow_token_id,
            uint64_t                        burn_basis_points );

        friend class PayoutOutputsTestAccess;

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
            bool                               effects_applied = false; ///< Parse effects durably applied (TRACK-01 ordering)
        };

        struct AccountUTXOState
        {
            uint64_t      version{ 0 };
            base::Hash256 root{};
            bool          initialized{ false };
        };

        TransactionManager( std::shared_ptr<crdt::GlobalDB>          processing_db,
                            std::shared_ptr<boost::asio::io_context> ctx,
                            std::shared_ptr<GeniusAccount>           account,
                            std::shared_ptr<Blockchain>              blockchain,
                            NodeType                                 node_type,
                            uint16_t                                 subnet_id,
                            std::chrono::milliseconds                timestamp_tolerance,
                            std::chrono::milliseconds                mutability_window,
                            uint64_t                                 initial_burn_basis_points,
                            std::shared_ptr<const sgns::account::ConfirmedBurnValueProvider> confirmed_burn_provider );

        // Parser function pointer alias: returns a set of topic strings or an error
        using TransactionParserFn =
            outcome::result<void> ( TransactionManager::* )( const GeniusTransaction & );

        SGTransaction::DAGStruct FillDAGStruct( std::optional<std::string> other_chain_hash = std::nullopt );
        /** @brief FillDAGStruct variant scoped to @p source_address (child-recovery chains). */
        SGTransaction::DAGStruct FillDAGStructForAddress( const std::string &source_address );
        std::string              GetOutgoingPreviousHash( uint64_t nonce ) const;
        std::string              GetTrackedOutgoingPreviousHash( uint64_t nonce ) const;
        std::string              GetPersistedOutgoingPreviousHash( uint64_t nonce ) const;
        std::string              GetRegisteredOutgoingPreviousHash( uint64_t nonce ) const;
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
         * @brief Dispatches to the type-specific parser registered in transaction_parsers.
         */
        outcome::result<void> ParseTransaction( const GeniusTransaction &tx );

        /**
         * @brief Dispatches to the type-specific reverter registered in transaction_parsers.
         */
        outcome::result<void> RevertTransaction( const GeniusTransaction &tx );
        void UpdateAccountUTXOState( const GeniusTransaction &tx, bool increment_version );

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

        /**
         * @brief Finds an exact-hash transaction at its normal CRDT path across
         *        every monitored network.
         *
         * A decoded value is a candidate only when its intrinsic hash matches
         * @p tx_hash; mismatched or unavailable CRDT values are not authority.
         */
        outcome::result<std::optional<std::shared_ptr<GeniusTransaction>>> FetchExactTransactionFromCRDT(
            const std::string &tx_hash ) const;

        std::optional<TrackedTx> GetTrackedTxByNonceAndAddress( uint64_t nonce, const std::string &address ) const;
        std::optional<TrackedTx> GetTrackedTxByHash( const std::string &tx_hash ) const;

        /**
         * @brief Erases the tracking entry for @p tx_hash iff it is still VERIFYING.
         * @return true when an entry was erased.
         */
        bool RemoveTrackedIfVerifying( const std::string &tx_hash );

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

        // METRICS-01: Operational metrics counters
        // Atomic counters tracking vote rates, validation breakdown, and transaction lifecycle.
        // Flushed to log on TransactionManager destruction (per D-12/D-13/D-14).
        std::atomic<uint64_t> metrics_tracking_insert_{ 0 };
        std::atomic<uint64_t> metrics_tracking_confirm_{ 0 };
        std::atomic<uint64_t> metrics_tracking_fail_{ 0 };

        /// @brief Compatibility burn rate for managers constructed without a trust provider.
        std::atomic<uint64_t> burn_basis_points_{ BURN_BASIS_POINTS_DEFAULT };
        /// @brief Node-scoped durable-ready burn state shared by every replacement manager.
        std::shared_ptr<const sgns::account::ConfirmedBurnValueProvider> confirmed_burn_provider_;

        static constexpr std::chrono::milliseconds TIMESTAMP_TOLERANCE  = std::chrono::seconds( 10 );
        static constexpr std::chrono::milliseconds MUTABILITY_WINDOW    = std::chrono::minutes( 15 );

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

        outcome::result<void> ParseTransferTransaction( const GeniusTransaction &tx );
        outcome::result<void> ParseMintTransaction( const GeniusTransaction &tx );
        outcome::result<void> ParseEscrowTransaction( const GeniusTransaction &tx );
        outcome::result<void> RevertTransferTransaction( const GeniusTransaction &tx );
        outcome::result<void> RevertMintTransaction( const GeniusTransaction &tx );
        outcome::result<void> RevertEscrowTransaction( const GeniusTransaction &tx );
        outcome::result<void> PutProducedUTXOs( const GeniusTransaction &tx );
        outcome::result<void> DeleteProducedUTXOs( const GeniusTransaction &tx );

        /**
         * @brief No-op parser for "registration" tx type.
         *
         * Registration transactions carry no UTXO parameters and are already fully
         * handled (signature/sequence/monotonicity validation, CRDT persistence) by
         * FilterRegistration/RegElementCallback. This entry exists solely to satisfy
         * transaction_parsers' membership check.
         */
        outcome::result<void> ParseRegistrationTransaction( const GeniusTransaction &tx );
        /// @brief No-op reverter for "registration" tx type — see ParseRegistrationTransaction.
        outcome::result<void> RevertRegistrationTransaction( const GeniusTransaction &tx );
        /**
         * @brief Applies a confirmed RevokeTx: rewrites the child's reg/ record with
         *        detach_flag=true under the revoke's hash.
         */
        outcome::result<void> ParseRevokeTransaction( const GeniusTransaction &tx );
        /// @brief No-op reverter for "revoke" tx type — leaving the target Detached is fail-safe.
        outcome::result<void> RevertRevokeTransaction( const GeniusTransaction &tx );

        /**
         * @brief reg/ new-element callback: discovers registrations naming this node as
         *        main and follows the child's channel (D-49).
         */
        void RegElementCallback( crdt::CRDTCallbackManager::NewDataPair new_data, std::string cid );

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

        /**
         * @brief CRDT element filter for incoming child-wallet registrations.
         *
         * Gates reg/ elements on deserialization, child signature, main-address shape,
         * sequence monotonicity (D-46) and supersedes_sequence fork prevention (D-38).
         *
         * @return nullopt to accept, or a vector of tombstone elements to reject.
         */
        std::optional<std::vector<crdt::pb::Element>> FilterRegistration( const crdt::pb::Element &element );

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
        /**
         * @brief Looks up the CID associated with a transaction hash in RocksDB,
         *        searching across all monitored networks.
         */
        outcome::result<std::string>                        GetTransactionCID( const std::string &tx_hash ) const;
        outcome::result<void> ChangeTransactionState( const std::shared_ptr<GeniusTransaction> &tx,
                                                      TransactionStatus new_status );
        bool                  HasConfirmedInputConflict( const GeniusTransaction &candidate_tx ) const;

        bool KeyExistsInDB( const std::string &key ) const;

        /**
         * @brief Verifies that an already validated certificate certifies @p transaction itself.
         *
         * Callers must locate the record through the transaction-derived canonical
         * slot first. This supplies the second, exact-subject check required for
         * shared slots, so a competing transaction cannot inherit finality.
         */
        static bool CertificateMatchesTransaction( const ConsensusCertificate &certificate,
                                                   const GeniusTransaction    &transaction );
        /**
         * @brief Loads the transaction's validated certificate from the canonical-slot record.
         *
         * Callers must still pass the result through CertificateMatchesTransaction.
         */
        outcome::result<ConsensusCertificate> GetTransactionCertificate( const GeniusTransaction &transaction ) const;

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

        /**
         * @brief Enumerates the child-wallet registrations recorded under a main wallet.
         *
         * Scans the per-network `reg` subtrees of the monitored networks and collects
         * the registration transactions whose main address matches @p main_address.
         *
         * @param main_address Main wallet public address (128-hex).
         * @return Vector of RegistrationDiscoveryEntry on success.
         */
        outcome::result<std::vector<RegistrationDiscoveryEntry>> GetRegistrationsForMain(
            const std::string &main_address );

        /// Consensus witness verdicts, evaluated by TransactionConsensusHandler.
        using WitnessValidationResult = TransactionConsensusHandler::WitnessValidationResult;
        /// Replay-protection evaluation outcome, produced by TransactionConsensusHandler.
        using ReplayProtectionResult  = TransactionConsensusHandler::ReplayProtectionResult;

        /// @brief Builds the consumed/produced Merkle commitment a nonce subject carries.
        std::optional<UTXOTransitionCommitment> BuildUTXOTransitionCommitment( const GeniusTransaction &tx ) const;

        /// @brief Validates the UTXO witness carried by a nonce consensus subject.
        WitnessValidationResult ValidateWitnessForConsensus( const ConsensusSubject  &subject,
                                                             const GeniusTransaction &tx ) const;

        /// @brief Evaluates previous-hash/nonce replay protection for a transaction.
        ReplayProtectionResult EvaluateTransactionReplayProtection( const GeniusTransaction &tx ) const;

        /**
         * @brief Outcome of the escrow price gate (D-08-01/D-08-02).
         *
         * Layer-neutral by design: the reason field carries the numeric value of
         * sgns::PriceValidationReason (0 = Accepted) so this header stays free of
         * any coinprices include — the transaction layer only consumes the verdict.
         */
        struct EscrowPriceGateOutcome
        {
            enum class Check : uint8_t
            {
                Approve, ///< Price claim validated (or no gate wired)
                Reject,  ///< Price claim failed validation — typed reason below
                Pending  ///< Claiming task not synced yet — retry, never reject (D-08-02)
            };

            Check       check   = Check::Approve;
            int         reason  = 0; ///< static_cast<sgns::PriceValidationReason>; 0 = Accepted
            std::string task_id;    ///< Matched claiming task id (empty when Pending)
        };

        /// @brief Node-supplied gate evaluating an escrow-hold tx's price claim (D-08-01).
        using EscrowPriceGateFn = std::function<EscrowPriceGateOutcome( const GeniusTransaction & )>;

        /// @brief Optional observer invoked when the gate rejects an escrow (wired by a later phase).
        using PriceRejectNotifierFn =
            std::function<void( const GeniusTransaction &, const EscrowPriceGateOutcome & )>;

        /**
         * @brief Installs the escrow price gate (D-08-01). Not setting one keeps
         *        every escrow-hold transaction approving — null-safe default, so
         *        unwired callers and existing test suites behave unchanged.
         */
        void SetEscrowPriceGate( EscrowPriceGateFn gate );

        /// @brief Installs the price-reject observer (null clears it).
        void SetPriceRejectNotifier( PriceRejectNotifierFn notifier );

        /**
         * @brief Runs the installed escrow price gate for @p tx.
         * @return The gate verdict; Approve when no gate is set.
         */
        EscrowPriceGateOutcome EvaluateEscrowPriceGate( const GeniusTransaction &tx ) const;

        /// @brief Invokes the price-reject observer when one is set (null-safe).
        void NotifyPriceReject( const GeniusTransaction &tx, const EscrowPriceGateOutcome &outcome ) const;

        /**
         * @brief First-rejector trigger (D-08-05): proposes the rejection subject.
         *
         * Builds the TaskRejectionSubject for a gate-Rejected escrow and submits
         * it through the consensus proposal path, so quorum can certify the
         * rejection and the poster's refund becomes network-authoritative.
         * Duplicates are harmless: the subject is slot-keyed on the rejected
         * escrow, an existing certificate short-circuits the submission, and
         * every honest rejector proposes the identical subject anyway.
         *
         * All failures are logged and swallowed — the gate verdict is already
         * decided and returned; this telemetry must never change it.
         *
         * @param[in] escrow_tx Escrow-hold transaction the gate rejected.
         * @param[in] outcome   Rejection verdict (typed reason + matched task id).
         */
        void SubmitTaskRejectionSubject( const GeniusTransaction &escrow_tx, const EscrowPriceGateOutcome &outcome );

        /** @brief Whole-transaction signature / authorization check. */
        bool CheckTransactionAuthorization( const GeniusTransaction &tx ) const;
        /** @brief Parent-child registration authority check (transfers from certified children, revokes). */
        bool CheckParentChildAuthority( const GeniusTransaction &tx ) const;
        /** @brief Replay-protection check expressed as a plain boolean. */
        bool CheckTransactionReplayProtection( const GeniusTransaction &tx ) const;

        void SetBridgeExecutedMarkerWriteFailureForTest( bool fail );
        void SetFetchAndProcessBeforeStateChangeHookForTest( std::function<void()> hook );

    private:
        static constexpr std::string_view GENIUS_CHAIN_ID = "supergenius";

        /**
         * @brief Derives the escrow chain id for a private-network job scope.
         * @param[in] private_network_id Private-network identity; empty = public scope.
         * @return "supergenius" when public (byte-identical to GENIUS_CHAIN_ID),
         *         "supergenius/<private_network_id>" when scoped.
         */
        static std::string ScopedChainId( const std::string &private_network_id );

        struct InputValidatorSelection
        {
            std::string            chain_id;
            const IInputValidator &validator;
        };

        InputValidatorSelection SelectInputValidator( const GeniusTransaction &tx ) const;

        GeniusInputValidator      genius_input_validator_;
        PublicChainInputValidator public_chain_input_validator_;

        /// @brief Consensus-facing half of this manager; see TransactionConsensusHandler.
        std::unique_ptr<TransactionConsensusHandler> consensus_m_;

        /// @brief Node-supplied escrow price gate; empty = gate disabled (D-08-01).
        std::optional<EscrowPriceGateFn> escrow_price_gate_;

        /// @brief Optional escrow price-reject observer (D-08-01; empty = none).
        PriceRejectNotifierFn price_reject_notifier_;

        outcome::result<void> PersistBridgeExecutedMarker( const MintTransactionV2 &mint_tx );
        void                  ReleaseBridgeMintReservation( const GeniusTransaction &tx );
        bool                  EnterFinalityFaultBarrier();

        /**
         * @brief Vote-time verification of a TaskRejectionSubject (D-08-06).
         *
         * Independently re-runs the escrow price gate on the referenced
         * task+escrow and approves only when the recomputed verdict is Reject
         * AND its typed reason equals the subject's reject_reason. A forged or
         * stale rejection (mismatched reason, unresolvable refs, or a subject
         * whose refs recompute to Accept) can never certify (CONS-03).
         *
         * @param[in] subject Raw consensus subject to decode and verify.
         * @return Approve on recomputed-reason match, otherwise Reject.
         */
        ConsensusManager::ValidationResult HandleTaskRejectionSubject(
            const ConsensusManager::Subject &subject ) const;

        /**
         * @brief Applies a certified TaskRejectionSubject (D-08-05/D-08-07).
         *
         * Restores the poster's funds in whichever refund regime the tracked
         * escrow is in: a PENDING/UNCONFIRMED (non-CONFIRMED, non-terminal)
         * tracked escrow is driven to FAILED so the existing FAILED machinery
         * performs RollbackUTXOs (regime 1); a CONFIRMED escrow routes to the
         * regime-2 full-refund release spend. A locally-unknown escrow hash is
         * ignored without an error state (nothing to restore on this node).
         *
         * @param[in] subject_hash Subject hash the certificate was keyed on.
         * @param[in] certificate  Certified rejection carrier.
         * @return Approve once the certificate's effects are settled.
         */
        outcome::result<ConsensusManager::Check> HandleTaskRejectionCertificate(
            const std::string          &subject_hash,
            const ConsensusCertificate &certificate );

        /**
         * @brief Builds and submits the regime-2 rejection release (D-08-05/D-08-07).
         *
         * Follows the PayEscrow construction verbatim (fetch record, escrow
         * InputUTXOInfo signed by the local account, FillDAGStruct lock id with
         * the empty-fallback, MakeSignature, EnqueueTransaction) but emits
         * exactly ONE output paying the FULL escrowed amount to the escrow's
         * source address (the poster). BuildPayoutOutputs is deliberately NOT
         * used — it always emits a burn output, which D-08-07 forbids for
         * rejection refunds. Only reachable from the rejection-certificate
         * handler's CONFIRMED branch: an uncertified escrow has no outpoint to
         * spend and honest validators would reject the release (08-RESEARCH
         * Pitfall 1). A second release of the same escrow fails structurally
         * through UTXO double-spend rules.
         *
         * @param[in] escrow_tx CONFIRMED escrow whose UTXO is refunded.
         * @return Release transaction hash on success, otherwise an error.
         */
        outcome::result<std::string> BuildRejectionReleaseTransaction( const EscrowTransaction &escrow_tx );

        /**
         * @brief Canonical slot key for a rejection subject's escrow (D-08-05).
         *
         * Shared by the registered slot-key handler and the first-rejector
         * trigger's certificate dedupe so the two can never drift: every
         * rejection proposal and certificate for one escrow lives in one slot.
         *
         * @param[in] original_escrow_hash Hash of the rejected escrow transaction.
         * @return Namespaced slot key.
         */
        static std::string TaskRejectionSlotKey( const std::string &original_escrow_hash );

        bool                  fail_bridge_executed_marker_write_for_test_ = false;
        std::function<void()> fetch_and_process_before_state_change_hook_for_test_;

        struct FinalityFaultBarrier
        {
            bool armed    = false;
            bool entered  = false;
            bool released = false;
        };
        mutable std::mutex      fault_test_mutex_;
        std::condition_variable fault_test_cv_;
        uint64_t                mint_effects_for_test_ = 0;
        FinalityFaultBarrier    mint_effects_barrier_;
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, TransactionManager::Error );

template <>
struct fmt::formatter<sgns::TransactionManager::State> : formatter<std::string_view>
{
    format_context::iterator format( sgns::TransactionManager::State s, format_context &ctx ) const;
};

#endif
