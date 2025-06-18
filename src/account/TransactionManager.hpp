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
#include <map>
#include <unordered_map>

#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/atomic_transaction.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "account/IGeniusTransactions.hpp"
#include "account/TransferTransaction.hpp"
#include "account/MintTransaction.hpp"
#include "account/EscrowTransaction.hpp"
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
        using TransactionPair  = std::pair<std::shared_ptr<IGeniusTransactions>, std::optional<std::vector<uint8_t>>>;
        using TransactionBatch = std::vector<TransactionPair>;
        using TransactionItem  = std::pair<TransactionBatch, std::optional<std::shared_ptr<crdt::AtomicTransaction>>>;

        TransactionManager( std::shared_ptr<crdt::GlobalDB>          processing_db,
                            std::shared_ptr<boost::asio::io_context> ctx,
                            std::shared_ptr<GeniusAccount>           account,
                            std::shared_ptr<crypto::Hasher>          hasher,
                            bool                                     full_node = false );

        ~TransactionManager();

        void Start();
        void PrintAccountInfo();

        const GeniusAccount &GetAccount() const;

        std::vector<std::vector<uint8_t>> GetOutTransactions() const;
        std::vector<std::vector<uint8_t>> GetInTransactions() const;

        outcome::result<std::string>                            TransferFunds( uint64_t           amount,
                                                                               const std::string &destination,
                                                                               std::string        token_id = "" );
        outcome::result<std::string>                            MintFunds( uint64_t    amount,
                                                                           std::string transaction_hash,
                                                                           std::string chainid,
                                                                           std::string tokenid );
        outcome::result<std::pair<std::string, EscrowDataPair>> HoldEscrow( uint64_t           amount,
                                                                            const std::string &dev_addr,
                                                                            uint64_t           peers_cut,
                                                                            const std::string &job_id );
        outcome::result<std::string>                            PayEscrow( const std::string                       &escrow_path,
                                                                           const SGProcessing::TaskResult          &taskresult,
                                                                           std::shared_ptr<crdt::AtomicTransaction> crdt_transaction );
        uint64_t                                                GetBalance();

        // Wait for an incoming transaction to be processed with a timeout
        bool WaitForTransactionIncoming( const std::string &txId, std::chrono::milliseconds timeout ) const;
        // Wait for an outgoing transaction to be processed with a timeout
        bool WaitForTransactionOutgoing( const std::string &txId, std::chrono::milliseconds timeout ) const;
        bool WaitForEscrowRelease( const std::string &originalEscrowId, std::chrono::milliseconds timeout ) const;

        static std::string GetTransactionPath( IGeniusTransactions &element );

        static std::string GetTransactionProofPath( IGeniusTransactions &element );
        static outcome::result<std::shared_ptr<IGeniusTransactions>> FetchTransaction(
            const std::shared_ptr<crdt::GlobalDB> &db,
            std::string_view                       transaction_key );

    protected:
        friend class GeniusNode;
        void EnqueueTransaction( TransactionPair element );
        void EnqueueTransaction( TransactionItem element );

    private:
        static constexpr std::uint16_t    MAIN_NET_ID             = 369;
        static constexpr std::uint16_t    TEST_NET_ID             = 963;
        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "/bc-%hu/";
        static constexpr std::string_view GNUS_FULL_NODES_TOPIC   = "SuperGNUSNode.TestNet.FullNode.%hu";
        using TransactionParserFn =
            outcome::result<void> ( TransactionManager::* )( const std::shared_ptr<IGeniusTransactions> & );

        void                     Update();
        SGTransaction::DAGStruct FillDAGStruct( std::string transaction_hash = "" ) const;
        outcome::result<void>    SendTransaction();

        static std::string GetTransactionBasePath( const std::string &address );
        static std::string GetBlockChainBase();
        static outcome::result<std::shared_ptr<IGeniusTransactions>> DeSerializeTransaction( std::string tx_data );
        static outcome::result<std::string>                          GetExpectedProofKey( const std::string                          &tx_key,
                                                                                          const std::shared_ptr<IGeniusTransactions> &tx );
        static outcome::result<std::string>                          GetExpectedTxKey( const std::string &proof_key );

        outcome::result<bool> CheckProof( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        outcome::result<void> CheckIncoming( bool checkProofs = true );

        outcome::result<void> CheckOutgoing();

        std::shared_ptr<crdt::GlobalDB> globaldb_m;

        std::shared_ptr<boost::asio::io_context>   ctx_m;
        std::shared_ptr<GeniusAccount>             account_m;
        std::shared_ptr<crypto::Hasher>            hasher_m;
        std::shared_ptr<boost::asio::steady_timer> timer_m;
        bool                                       full_node_m;
        // for the SendTransaction thread support
        mutable std::mutex          mutex_m;
        std::deque<TransactionItem> tx_queue_m;

        mutable std::shared_mutex                                   outgoing_tx_mutex_m;
        std::map<std::string, std::shared_ptr<IGeniusTransactions>> outgoing_tx_processed_m;
        mutable std::shared_mutex                                   incoming_tx_mutex_m;
        std::map<std::string, std::shared_ptr<IGeniusTransactions>> incoming_tx_processed_m;
        std::function<void()>                                       task_m;

        outcome::result<void> ParseTransferTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseEscrowReleaseTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        static inline const std::unordered_map<std::string, TransactionParserFn> transaction_parsers = {
            { "transfer", &TransactionManager::ParseTransferTransaction },
            { "mint", &TransactionManager::ParseMintTransaction },
            { "escrow-hold", &TransactionManager::ParseEscrowTransaction },
            { "escrow-release", &TransactionManager::ParseEscrowReleaseTransaction } };

        base::Logger m_logger = sgns::base::createLogger( "TransactionManager" );
    };
}

#endif
