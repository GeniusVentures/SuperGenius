/**
 * @file       TransactionManager.hpp
 * @brief
 * @date       2024-03-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRANSACTION_MANAGER_HPP_
#define _TRANSACTION_MANAGER_HPP_

#include <memory>
#include <deque>
#include <cstdint>
#include <unordered_map>
#include <set>
#include <optional>

#include <boost/format.hpp>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/atomic_transaction.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "account/IGeniusTransactions.hpp"
#include "account/TransferTransaction.hpp"
#include "account/MintTransaction.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/EscrowReleaseTransaction.hpp"
#include "account/ProcessingTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "base/logger.hpp"
#include "base/buffer.hpp"
#include "crypto/hasher.hpp"
#ifdef _PROOF_ENABLED
#include "proof/proto/SGProof.pb.h"
#endif
#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"

namespace sgns
{
    using namespace boost::multiprecision;
    using EscrowDataPair = std::pair<std::string, base::Buffer>;

    class TransactionManager : public std::enable_shared_from_this<TransactionManager>
    {
    public:
        static constexpr std::uint16_t MAIN_NET_ID = 369;
        static constexpr std::uint16_t TEST_NET_ID = 963;
        static constexpr std::uint16_t DEV_NET_ID  = 144;
#ifdef DEV_NET
        static constexpr std::string_view GNUS_FULL_NODES_TOPIC = "SuperGNUSNode.TestNet.FullNode.%hu.dev";
#else
        static constexpr std::string_view GNUS_FULL_NODES_TOPIC = "SuperGNUSNode.TestNet.FullNode.%hu";
#endif
        using TransactionPair  = std::pair<std::shared_ptr<IGeniusTransactions>, std::optional<std::vector<uint8_t>>>;
        using TransactionBatch = std::vector<TransactionPair>;
        using TransactionItem  = std::pair<TransactionBatch, std::optional<std::shared_ptr<crdt::AtomicTransaction>>>;

        enum class State
        {
            CREATING = 0,
            INITIALIZING,
            SYNCHING,
            READY,
        };

        enum class TransactionStatus
        {
            CREATED,
            SENDING,
            CONFIRMED,
            VERIFYING,
            FAILED,
            INVALID
        };
        static std::shared_ptr<TransactionManager> New(
            std::shared_ptr<crdt::GlobalDB>          processing_db,
            std::shared_ptr<boost::asio::io_context> ctx,
            std::shared_ptr<GeniusAccount>           account,
            std::shared_ptr<crypto::Hasher>          hasher,
            bool                                     full_node           = false,
            std::chrono::milliseconds                timestamp_tolerance = TIMESTAMP_TOLERANCE,
            std::chrono::milliseconds                immutability_window = IMMUTABILITY_WINDOW );

        ~TransactionManager();

        void Start();
        void PrintAccountInfo();

        const GeniusAccount &GetAccount() const;

        std::vector<std::vector<uint8_t>> GetOutTransactions() const;
        std::vector<std::vector<uint8_t>> GetInTransactions() const;

        outcome::result<std::string> TransferFunds( uint64_t amount, const std::string &destination, TokenID token_id );
        outcome::result<std::string> MintFunds( uint64_t    amount,
                                                std::string transaction_hash,
                                                std::string chainid,
                                                TokenID     tokenid );
        outcome::result<std::pair<std::string, EscrowDataPair>> HoldEscrow( uint64_t           amount,
                                                                            const std::string &dev_addr,
                                                                            uint64_t           peers_cut,
                                                                            const std::string &job_id );
        outcome::result<std::string>                            PayEscrow( const std::string                       &escrow_path,
                                                                           const SGProcessing::TaskResult          &taskresult,
                                                                           std::shared_ptr<crdt::AtomicTransaction> crdt_transaction );
        uint64_t                                                GetBalance();

        // Wait for an incoming transaction to be processed with a timeout
        TransactionStatus WaitForTransactionIncoming( const std::string        &txId,
                                                      std::chrono::milliseconds timeout ) const;
        // Wait for an outgoing transaction to be processed with a timeout
        TransactionStatus WaitForTransactionOutgoing( const std::string        &txId,
                                                      std::chrono::milliseconds timeout ) const;
        TransactionStatus WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                std::chrono::milliseconds timeout ) const;

        static std::string GetTransactionPath( IGeniusTransactions &element );

        static std::string GetTransactionProofPath( IGeniusTransactions &element );
        static outcome::result<std::shared_ptr<IGeniusTransactions>> FetchTransaction(
            const std::shared_ptr<crdt::GlobalDB> &db,
            std::string_view                       transaction_key );
        static outcome::result<std::shared_ptr<IGeniusTransactions>> DeSerializeTransaction(
            const base::Buffer &tx_data );

        State             GetState() const;
        TransactionStatus GetOutgoingStatusByTxId( const std::string &txId ) const;
        TransactionStatus GetIncomingStatusByTxId( const std::string &txId ) const;

        // Stop the periodic Update() loop and prevent re-posting.
        void Stop();

    protected:
        friend class GeniusNode;
        void EnqueueTransaction( TransactionPair element );
        void EnqueueTransaction( TransactionItem element );

        void SetTimeFrameToleranceMs( uint64_t timeframe_tolerance );
        void SetImmutabilityWindowMs( uint64_t immutability_window );

    private:
        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "/bc-%hu/";

        TransactionManager( std::shared_ptr<crdt::GlobalDB>          processing_db,
                            std::shared_ptr<boost::asio::io_context> ctx,
                            std::shared_ptr<GeniusAccount>           account,
                            std::shared_ptr<crypto::Hasher>          hasher,
                            bool                                     full_node,
                            std::chrono::milliseconds                timestamp_tolerance,
                            std::chrono::milliseconds                immutability_window );

        // Parser function pointer alias: returns a set of topic strings or an error
        using TransactionParserFn = outcome::result<std::set<std::string>> ( TransactionManager::* )(
            const std::shared_ptr<IGeniusTransactions> & );

        void                     Update();
        SGTransaction::DAGStruct FillDAGStruct( std::string transaction_hash = "" ) const;
        outcome::result<bool>    SendTransaction();
        outcome::result<void>    ConfirmTransactions();

        static std::string GetTransactionBasePath( const std::string &address );
        static std::string GetBlockChainBase();
        static outcome::result<std::shared_ptr<IGeniusTransactions>> DeSerializeTransaction( std::string tx_data );

        static outcome::result<std::string> GetExpectedProofKey( const std::string                          &tx_key,
                                                                 const std::shared_ptr<IGeniusTransactions> &tx );
        static outcome::result<std::string> GetExpectedTxKey( const std::string &proof_key );

        outcome::result<bool>                  CheckProof( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<std::set<std::string>> ParseTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<std::set<std::string>> RevertTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        outcome::result<void> CheckIncoming();

        outcome::result<void> CheckOutgoing();

        void InitNonce( uint64_t timeout_ms );
        void SyncNonce();

        outcome::result<bool> CheckTransactionValidity( std::set<uint64_t> nonces_to_check );

        outcome::result<void> DeleteTransaction( std::string tx_key, const std::set<std::string> &topics );
        std::shared_ptr<IGeniusTransactions> GetOutTransaction( const std::string &tx_hash ) const;
        std::shared_ptr<IGeniusTransactions> GetOutTransaction( uint64_t nonce ) const;

        bool SetOutgoingStatusByNonce( uint64_t nonce, TransactionStatus s );

        std::shared_ptr<crdt::GlobalDB> globaldb_m;

        std::shared_ptr<boost::asio::io_context> ctx_m;
        std::shared_ptr<GeniusAccount>           account_m;
        std::shared_ptr<crypto::Hasher>          hasher_m;
        bool                                     full_node_m;
        std::string                              full_node_topic_m; ///< formatted full-node topic
        void                                     TickOnce();
        State                                    state_m;

        // for the SendTransaction thread support
        mutable std::mutex          mutex_m;
        std::deque<TransactionItem> tx_queue_m;

        struct TrackedTx
        {
            std::shared_ptr<IGeniusTransactions> tx;
            TransactionStatus                    status;
        };

        mutable std::shared_mutex                  outgoing_tx_mutex_m;
        std::unordered_map<std::string, TrackedTx> outgoing_tx_processed_m;
        mutable std::shared_mutex                  incoming_tx_mutex_m;
        std::unordered_map<std::string, TrackedTx> incoming_tx_processed_m;
        std::function<void()>                      task_m;
        std::atomic<bool>                          stopped_{ false };
        std::chrono::milliseconds                  timestamp_tolerance_m;
        std::chrono::milliseconds                  immutability_window_m;

        static constexpr std::chrono::milliseconds TIMESTAMP_TOLERANCE = std::chrono::seconds( 10 );
        static constexpr std::chrono::milliseconds IMMUTABILITY_WINDOW = std::chrono::minutes( 15 );

        std::mutex                         cv_mutex_;
        std::condition_variable            cv_;
        std::queue<crdt::NotificationData> notification_queue_; // Buffer for multiple notifications
        std::mutex                         queue_mutex_;        // Separate mutex for the queue

        std::chrono::steady_clock::time_point last_loop_time_;

        outcome::result<std::set<std::string>> ParseTransferTransaction(
            const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<std::set<std::string>> ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<std::set<std::string>> ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<std::set<std::string>> ParseEscrowReleaseTransaction(
            const std::shared_ptr<IGeniusTransactions> &tx );

        outcome::result<std::set<std::string>> RevertTransferTransaction(
            const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<std::set<std::string>> RevertMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<std::set<std::string>> RevertEscrowTransaction(
            const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<std::set<std::string>> RevertEscrowReleaseTransaction(
            const std::shared_ptr<IGeniusTransactions> &tx );

        static inline const std::unordered_map<std::string, std::pair<TransactionParserFn, TransactionParserFn>>
            transaction_parsers = {
                { "transfer",
                  { &TransactionManager::ParseTransferTransaction, &TransactionManager::RevertTransferTransaction } },
                { "mint", { &TransactionManager::ParseMintTransaction, &TransactionManager::RevertMintTransaction } },
                { "escrow-hold",
                  { &TransactionManager::ParseEscrowTransaction, &TransactionManager::RevertEscrowTransaction } },
                { "escrow-release",
                  { &TransactionManager::ParseEscrowReleaseTransaction,
                    &TransactionManager::RevertEscrowReleaseTransaction } } };

        base::Logger m_logger = base::createLogger( "TransactionManager" );

        std::optional<std::vector<crdt::pb::Element>> FilterTransaction( const crdt::pb::Element &element );
        std::optional<std::vector<crdt::pb::Element>> FilterProof( const crdt::pb::Element &element );
        void                                          NotificationCallback( const crdt::NotificationData &keys );

        bool ShouldReplaceTransaction( const std::shared_ptr<IGeniusTransactions> &existing_tx,
                                       const std::shared_ptr<IGeniusTransactions> &new_tx ) const;

        uint64_t GetCurrentTimestamp() const;
        int64_t  GetElapsedTime( uint64_t timestamp, uint64_t current_timestamp ) const;
        int64_t  GetElapsedTime( uint64_t timestamp ) const;

        bool IsTransactionImmutable( const std::shared_ptr<IGeniusTransactions> &tx ) const;

        outcome::result<void> RemoveTransactionFromProcessedMaps( const std::string &transaction_key,
                                                                  bool               delete_from_crdt = false );

        void ProcessTombstones( const std::vector<std::string> &tombstones );
        void ProcessElements( const std::vector<std::string> &elements );

        void NewElementCallback( const std::string &key, const base::Buffer &value );
    };
}

#endif
