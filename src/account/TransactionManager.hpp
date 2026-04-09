/**
 * @file       TransactionManager.hpp
 * @brief
 * @date       2024-03-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRANSACTION_MANAGER_HPP_
#define _TRANSACTION_MANAGER_HPP_

#include "UTXOManager.hpp"

#include <memory>
#include <deque>
#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <optional>

#include <boost/format.hpp>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/atomic_transaction.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "account/IGeniusTransactions.hpp"
#include "account/GeniusAccount.hpp"
#include "account/InputValidators.hpp"
#include "base/logger.hpp"
#include "base/buffer.hpp"
#include "crypto/hasher.hpp"

#include "blockchain/Blockchain.hpp"

#include "proof/proto/SGProof.pb.h"

#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"

namespace sgns
{
    using namespace boost::multiprecision;
    using EscrowDataPair = std::pair<std::string, base::Buffer>;

    class TransactionManager : public std::enable_shared_from_this<TransactionManager>
    {
    public:
        static constexpr std::string_view GNUS_FULL_NODES_TOPIC        = "SuperGNUSNode.TestNet.FullNode";
        static constexpr std::string_view GNUS_FULL_NODES_TOPIC_LEGACY = "SuperGNUSNode.TestNet.FullNode.963";
        static constexpr uint64_t         NONCE_REQUEST_TIMEOUT_MS =
            5000; ///< Unified timeout for all nonce requests (10 seconds)

        /**
         * @brief       State of the Transaction Manager
         */
        enum class State : uint8_t
        {
            CREATING = 0, ///< Creating the object
            INITIALIZING, ///< Initializing the object
            SYNCING,      ///< Syncing the transactions
            READY,        ///< Ready to process transactions
        };

        using TransactionPair  = std::pair<std::shared_ptr<IGeniusTransactions>, std::optional<std::vector<uint8_t>>>;
        using TransactionBatch = std::vector<TransactionPair>;
        using TransactionItem  = std::pair<TransactionBatch, std::optional<std::shared_ptr<crdt::AtomicTransaction>>>;
        using StateChangeCallback = std::function<void( const State &previous, const State &current )>;

        /**
         * @brief       Status of a transaction
         */
        enum class TransactionStatus : uint8_t
        {
            CREATED,   ///< Transaction created but not yet sent
            SENDING,   ///< Transaction is being sent
            CONFIRMED, ///< Transaction confirmed
            VERIFYING, ///< Transaction being verified
            FAILED,    ///< Transaction failed
            INVALID    ///< Invalid transaction
        };

        /**
         * @brief       Factory constructor of the TransactionManager
         * @param[in]   processing_db Database of the CRDT
         * @param[in]   ctx The io context used to run its inner methods
         * @param[in]   utxo_manager UTXO manager
         * @param[in]   account Genius account to be used
         * @param[in]   hasher Hasher to be used
         * @param[in]   full_node Parameter to indicate if the account is a full node
         * @param[in]   timestamp_tolerance Time to analyze a transaction with the same nonce/key
         * @param[in]   mutability_window Window of time where a transaction can be modified
         * @return      Instance of the TransactionManager initialized or Error
         * @note        Default timestamp_tolerance is 5 minutes (300000 ms)
         * @note        Default mutability_window is 10 minutes (600000 ms)
         * @note        timestamp_tolerance must be smaller than mutability_window
         */
        static std::shared_ptr<TransactionManager> New(
            std::shared_ptr<crdt::GlobalDB>          processing_db,
            std::shared_ptr<boost::asio::io_context> ctx,
            UTXOManager                             &utxo_manager,
            std::shared_ptr<GeniusAccount>           account,
            std::shared_ptr<crypto::Hasher>          hasher,
            std::shared_ptr<Blockchain>              blockchain,
            bool                                     full_node           = false,
            std::chrono::milliseconds                timestamp_tolerance = std::chrono::milliseconds( 0 ),
            std::chrono::milliseconds                mutability_window   = std::chrono::milliseconds( 0 ) );

        ~TransactionManager();

        void Start();
        void PrintAccountInfo() const;

        const GeniusAccount &GetAccount() const;

        std::vector<std::vector<uint8_t>> GetOutTransactions() const;
        std::vector<std::vector<uint8_t>> GetInTransactions() const;
        std::vector<std::vector<uint8_t>> GetTransactions(
            std::optional<TransactionStatus> tx_status = std::nullopt ) const;
        std::vector<std::vector<uint8_t>> GetTransactions() const;

        outcome::result<std::string> TransferFunds( uint64_t amount, const std::string &destination, TokenID token_id );
        outcome::result<std::string> MintFunds( uint64_t    amount,
                                                std::string transaction_hash,
                                                std::string chainid,
                                                TokenID     tokenid,
                                                std::string destination = "" );
        outcome::result<std::pair<std::string, EscrowDataPair>> HoldEscrow( uint64_t           amount,
                                                                            const std::string &dev_addr,
                                                                            uint64_t           peers_cut,
                                                                            const std::string &job_id );
        outcome::result<std::string>                            PayEscrow( const std::string                       &escrow_path,
                                                                           const SGProcessing::TaskResult          &task_result,
                                                                           std::shared_ptr<crdt::AtomicTransaction> crdt_transaction );

        // Wait for an incoming transaction to be processed with a timeout
        TransactionStatus WaitForTransactionIncoming( const std::string        &txId,
                                                      std::chrono::milliseconds timeout ) const;
        // Wait for an outgoing transaction to be processed with a timeout
        TransactionStatus WaitForTransactionOutgoing( const std::string        &txId,
                                                      std::chrono::milliseconds timeout ) const;
        TransactionStatus WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                std::chrono::milliseconds timeout ) const;

        static std::string GetTransactionPath( uint16_t base, const std::string &tx_hash );
        static std::string GetTransactionPath( const IGeniusTransactions &element );
        static std::string GetTransactionPath( const std::string &tx_hash );

        static std::string GetTransactionProofPath( const IGeniusTransactions &element );
        static outcome::result<std::shared_ptr<IGeniusTransactions>> FetchTransaction(
            const std::shared_ptr<crdt::GlobalDB> &db,
            std::string_view                       transaction_key );
        static outcome::result<std::shared_ptr<IGeniusTransactions>> DeSerializeTransaction(
            const base::Buffer &tx_data );

        State GetState() const
        {
            return state_m;
        }

        TransactionStatus GetOutgoingStatusByTxId( const std::string &txId ) const;
        TransactionStatus GetIncomingStatusByTxId( const std::string &txId ) const;

        outcome::result<std::shared_ptr<IGeniusTransactions>> GetConflictingTransaction(
            const IGeniusTransactions &element ) const;

        /**
         * @brief      Stops the TransactionManager processing
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

        static std::string GetBlockChainBase( uint16_t network_id );

        outcome::result<void> QueryTransactions();
        outcome::result<void> FetchAndProcessTransaction( const std::string          &tx_key,
                                                          std::optional<base::Buffer> tx_data = std::nullopt );

    protected:
        friend class GeniusNode;
        void EnqueueTransaction( TransactionPair element );
        void EnqueueTransaction( TransactionItem element );

        void SetTimeFrameToleranceMs( uint64_t timeframe_tolerance );
        void SetMutabilityWindowMs( uint64_t mutability_window );

    private:
        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "/bc-%hu/";

        struct TrackedTx
        {
            std::shared_ptr<IGeniusTransactions> tx;
            TransactionStatus                    status;
            uint64_t                             cached_nonce; // Cache nonce to avoid dereferencing tx
        };
        struct AccountUTXOState
        {
            uint64_t     version{ 0 };
            base::Hash256 root{};
            bool         initialized{ false };
        };

        TransactionManager( std::shared_ptr<crdt::GlobalDB>          processing_db,
                            std::shared_ptr<boost::asio::io_context> ctx,
                            UTXOManager                             &utxo_manager,
                            std::shared_ptr<GeniusAccount>           account,
                            std::shared_ptr<crypto::Hasher>          hasher,
                            std::shared_ptr<Blockchain>              blockchain,
                            bool                                     full_node,
                            std::chrono::milliseconds                timestamp_tolerance,
                            std::chrono::milliseconds                mutability_window );

        // Parser function pointer alias: returns a set of topic strings or an error
        using TransactionParserFn =
            outcome::result<void> ( TransactionManager::* )( const std::shared_ptr<IGeniusTransactions> & );

        SGTransaction::DAGStruct FillDAGStruct( std::optional<std::string> other_chain_hash = std::nullopt );
        std::string              GetOutgoingPreviousHash( uint64_t nonce ) const;
        outcome::result<void>    SendTransactionItem( TransactionItem &item );
        outcome::result<void>    RollbackTransactions( TransactionItem &item_to_rollback );

        static std::vector<uint16_t>                                 GetMonitoredNetworkIDs();
        static std::string                                           GetBlockChainBase();
        static outcome::result<std::shared_ptr<IGeniusTransactions>> DeSerializeTransaction( std::string tx_data );

        static outcome::result<std::string> GetExpectedProofKey( const std::string                          &tx_key,
                                                                 const std::shared_ptr<IGeniusTransactions> &tx );
        static outcome::result<std::string> GetExpectedTxKey( const std::string &proof_key );

        outcome::result<bool> CheckProof( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> RevertTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        bool                  DoesTransactionMutateUTXOState( const std::shared_ptr<IGeniusTransactions> &tx ) const;
        std::unordered_set<std::string> CollectTouchedAccounts( const std::shared_ptr<IGeniusTransactions> &tx ) const;
        AccountUTXOState                GetOrInitAccountUTXOState( const std::string &address ) const;
        void                            UpdateAccountUTXOState( const std::unordered_set<std::string> &addresses,
                                                                bool                                   increment_version );

        void InitializeUTXOs();
        void InitTransactions();
        bool CheckNonce() const;
        void SyncNonce();

        /**
         * @brief       Request heads for relevant topics when we detect we're behind
         */
        void RequestRelevantHeads();

        outcome::result<bool> CheckTransactionValidity( const std::set<uint64_t> &nonces_to_check );

        outcome::result<void> DeleteTransaction( std::string tx_key, const std::unordered_set<std::string> &topics );
        std::shared_ptr<IGeniusTransactions> GetTransactionByHash( const std::string &tx_hash ) const;
        std::shared_ptr<IGeniusTransactions> GetTransactionByHashNoLock( const std::string &tx_hash ) const;
        std::shared_ptr<IGeniusTransactions> GetTransactionByNonceAndAddress( uint64_t           nonce,
                                                                              const std::string &address ) const;
        std::optional<TrackedTx> GetTrackedTxByNonceAndAddress( uint64_t nonce, const std::string &address ) const;
        std::optional<TrackedTx> GetTrackedTxByHash( const std::string &tx_hash ) const;

        bool SetOutgoingStatusByNonce( uint64_t nonce, TransactionStatus s );

        void OnConsensusCertificate( const std::string &tx_hash );

        std::shared_ptr<crdt::GlobalDB> globaldb_m;

        std::shared_ptr<boost::asio::io_context> ctx_m;
        std::shared_ptr<GeniusAccount>           account_m;
        UTXOManager                             &utxo_manager_;
        std::shared_ptr<crypto::Hasher>          hasher_m;
        std::shared_ptr<Blockchain>              blockchain_;
        bool                                     full_node_m;
        std::string                              full_node_topic_m; ///< formatted full-node topic
        void                                     TickOnce();
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
        std::chrono::milliseconds                                   timestamp_tolerance_m;
        std::chrono::milliseconds                                   mutability_window_m;
        uint64_t                                                    nonce_window_m = DEFAULT_NONCE_WINDOW;

        static constexpr std::chrono::milliseconds TIMESTAMP_TOLERANCE  = std::chrono::seconds( 10 );
        static constexpr std::chrono::milliseconds MUTABILITY_WINDOW    = std::chrono::minutes( 15 );
        static constexpr uint64_t                  DEFAULT_NONCE_WINDOW = 5;

        std::mutex                                         cv_mutex_;
        std::condition_variable                            cv_;
        std::queue<crdt::CRDTCallbackManager::NewDataPair> new_data_queue_;
        std::queue<std::string>                            deleted_data_queue_;
        std::mutex                                         new_data_queue_mutex_;     // Separate mutex for the queue
        std::mutex                                         deleted_data_queue_mutex_; // Separate mutex for the queue

        std::chrono::steady_clock::time_point last_loop_time_;

        std::mutex                            missing_tx_mutex_;
        std::unordered_set<std::string>       missing_tx_hashes_;
        std::chrono::steady_clock::time_point last_init_tx_request_time_{};
        static constexpr uint64_t             k_init_tx_request_cooldown_ms = 5000;

        outcome::result<void> ParseTransferTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseEscrowReleaseTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        outcome::result<void> RevertTransferTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> RevertMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> RevertEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> RevertEscrowReleaseTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        static inline const std::unordered_map<std::string, std::pair<TransactionParserFn, TransactionParserFn>>
            transaction_parsers = {
                { "transfer",
                  { &TransactionManager::ParseTransferTransaction, &TransactionManager::RevertTransferTransaction } },
                { "mint", { &TransactionManager::ParseMintTransaction, &TransactionManager::RevertMintTransaction } },
                { "mint-v2",
                  { &TransactionManager::ParseMintTransaction, &TransactionManager::RevertMintTransaction } },
                { "escrow-hold",
                  { &TransactionManager::ParseEscrowTransaction, &TransactionManager::RevertEscrowTransaction } },
                { "escrow-release",
                  { &TransactionManager::ParseEscrowReleaseTransaction,
                    &TransactionManager::RevertEscrowReleaseTransaction } } };

        std::optional<std::vector<crdt::pb::Element>> FilterTransaction( const crdt::pb::Element &element );
        std::optional<std::vector<crdt::pb::Element>> FilterProof( const crdt::pb::Element &element );

        bool ShouldReplaceTransaction( const IGeniusTransactions &existing_tx,
                                       const IGeniusTransactions &new_tx ) const;

        static uint64_t GetCurrentTimestamp();
        int64_t         GetElapsedTime( uint64_t timestamp, uint64_t current_timestamp ) const;
        int64_t         GetElapsedTime( uint64_t timestamp ) const;

        bool IsTransactionImmutable( const IGeniusTransactions &tx ) const;

        outcome::result<void> RemoveTransactionFromProcessedMaps( const std::string &transaction_key,
                                                                  bool               delete_from_crdt = false );
        outcome::result<void> AddTransactionToProcessedMaps( crdt::CRDTCallbackManager::NewDataPair new_data );
        outcome::result<void> StoreTransactionCID( const std::string &key, const std::string &cid );

        void ProcessDeletion( std::string deleted_key );
        void ProcessNewData( crdt::CRDTCallbackManager::NewDataPair new_data );

        void NewElementCallback( crdt::CRDTCallbackManager::NewDataPair new_data, std::string cid );
        void DeleteElementCallback( std::string deleted_key );

        void ChangeState( State new_state );

    public:
        enum class WitnessValidationResult : uint8_t
        {
            VALID,
            DRIFT,
            INVALID
        };

        outcome::result<std::string>                    GetTransactionCID( const std::string &tx_hash ) const;
        outcome::result<ConsensusManager::SubjectCheck> HandleNonceConsensusSubject(
            const ConsensusManager::Subject &subject );
        bool ValidateTransactionForConsensus( const std::shared_ptr<IGeniusTransactions> &tx ) const;
        bool CheckTransactionWellFormed( const IGeniusTransactions &tx ) const;
        bool CheckTransactionAuthorization( const IGeniusTransactions &tx ) const;
        bool CheckTransactionTimestamp( const IGeniusTransactions &tx ) const;
        bool CheckTransactionReplayProtection( const IGeniusTransactions &tx ) const;
        bool CheckTransactionTypeRules( const std::shared_ptr<IGeniusTransactions> &tx ) const;
        std::optional<UTXOTransitionCommitment> BuildUTXOTransitionCommitment(
            const std::shared_ptr<IGeniusTransactions> &tx ) const;
        std::optional<UTXOWitness> BuildUTXOWitness( const std::shared_ptr<IGeniusTransactions> &tx ) const;
        bool ApplyTransactionToUTXOSnapshot( const std::shared_ptr<IGeniusTransactions> &tx,
                                             std::vector<GeniusUTXO>                     &snapshot ) const;
        WitnessValidationResult ValidateWitnessForConsensus( const ConsensusSubject                    &subject,
                                                             const std::shared_ptr<IGeniusTransactions> &tx ) const;
        bool ValidateUTXOParametersForConsensus( const UTXOTxParameters &params, const std::string &address ) const;
        void SetNonceWindow( uint64_t window );
        outcome::result<void> ChangeTransactionState( const std::shared_ptr<IGeniusTransactions> &tx,
                                                      TransactionStatus                           new_status );
        bool HasConfirmedInputConflict( const std::shared_ptr<IGeniusTransactions> &candidate_tx ) const;

        bool IsGoingToOverwrite( const std::string &key ) const;

    private:
        static constexpr std::string_view GENIUS_CHAIN_ID = "supergenius";

        std::string               GetValidationChainId( const std::shared_ptr<IGeniusTransactions> &tx ) const;
        const IInputValidator    &GetInputValidator( const std::string &chain_id ) const;
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
