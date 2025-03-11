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
#include "upnp.hpp"

namespace sgns
{
    using namespace boost::multiprecision;
    using EscrowDataPair = std::pair<std::string, base::Buffer>;

    class TransactionManager : public std::enable_shared_from_this<TransactionManager>
    {
    public:
        using TransactionPair = std::pair<std::shared_ptr<IGeniusTransactions>, std::optional<std::vector<uint8_t>>>;

        TransactionManager( std::shared_ptr<crdt::GlobalDB>                  processing_db,
                            std::shared_ptr<boost::asio::io_context>         ctx,
                            std::shared_ptr<GeniusAccount>                   account,
                            std::shared_ptr<crypto::Hasher>                  hasher,
                            std::string                                      base_path,
                            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub,
                            std::shared_ptr<upnp::UPNP>                      upnp,
                            uint16_t                                         base_port );

        ~TransactionManager() = default;

        void Start();
        void PrintAccountInfo();

        const GeniusAccount &GetAccount() const;

        std::vector<std::vector<uint8_t>> GetOutTransactions() const;
        std::vector<std::vector<uint8_t>> GetInTransactions() const;

        outcome::result<std::string> TransferFunds( uint64_t amount, const std::string &destination );
        outcome::result<std::string> MintFunds( uint64_t    amount,
                                                std::string transaction_hash,
                                                std::string chainid,
                                                std::string tokenid );
        outcome::result<std::pair<std::string, EscrowDataPair>> HoldEscrow( uint64_t           amount,
                                                                            const std::string &dev_addr,
                                                                            uint64_t           peers_cut,
                                                                            const std::string &job_id );
        outcome::result<void> PayEscrow( const std::string &escrow_path, const SGProcessing::TaskResult &taskresult );
        uint64_t              GetBalance();

        // Wait for an incoming transaction to be processed with a timeout
        bool WaitForTransactionIncoming( const std::string &txId, std::chrono::milliseconds timeout ) const;
        // Wait for an outgoing transaction to be processed with a timeout
        bool WaitForTransactionOutgoing( const std::string &txId, std::chrono::milliseconds timeout ) const;

    private:
        static constexpr std::uint16_t    MAIN_NET_ID             = 369;
        static constexpr std::uint16_t    TEST_NET_ID             = 963;
        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "bc-%hu/";
        using TransactionParserFn =
            outcome::result<void> ( TransactionManager::* )( const std::shared_ptr<IGeniusTransactions> & );

        void                     Update();
        void                     EnqueueTransaction( TransactionPair element );
        SGTransaction::DAGStruct FillDAGStruct( std::string transaction_hash = "" ) const;
        outcome::result<void>    SendTransaction();
        std::string              GetTransactionPath( IGeniusTransactions &element );
        std::string              GetTransactionProofPath( IGeniusTransactions &element );
        static std::string       GetNotificationPath( const std::string &destination );
        static std::string       GetTransactionBasePath( const std::string &address );

        outcome::result<std::shared_ptr<IGeniusTransactions>> FetchTransaction(
            const std::shared_ptr<crdt::GlobalDB> &db,
            std::string_view                       transaction_key );
        outcome::result<bool> CheckProof( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        outcome::result<void> CheckIncoming();

        outcome::result<void> CheckOutgoing();

        void RefreshPorts();

        std::shared_ptr<crdt::GlobalDB> processing_db_m; //TODO - remove this as it's only needed on the PayEscrow
        std::shared_ptr<crdt::GlobalDB> outgoing_db_m;
        std::shared_ptr<crdt::GlobalDB> incoming_db_m;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub_m;
        std::string                                      base_path_m;

        std::vector<uint8_t> MakeSignature( SGTransaction::DAGStruct dag_st ) const;
        bool                 CheckDAGStructSignature( SGTransaction::DAGStruct dag_st ) const;

        std::shared_ptr<boost::asio::io_context>   ctx_m;
        std::shared_ptr<GeniusAccount>             account_m;
        std::shared_ptr<crypto::Hasher>            hasher_m;
        std::shared_ptr<upnp::UPNP>                upnp_m;
        uint16_t                                   base_port_m;
        std::shared_ptr<boost::asio::steady_timer> timer_m;
        // for the SendTransaction thread support
        mutable std::mutex          mutex_m;
        std::deque<TransactionPair> tx_queue_m;

        mutable std::shared_mutex                                        outgoing_tx_mutex_m;
        std::map<std::string, std::shared_ptr<IGeniusTransactions>>      outgoing_tx_processed_m;
        mutable std::shared_mutex                                        incoming_tx_mutex_m;
        std::map<std::string, std::shared_ptr<IGeniusTransactions>>      incoming_tx_processed_m;
        std::unordered_map<std::string, std::shared_ptr<crdt::GlobalDB>> destination_dbs_m;
        std::set<uint16_t>                                               used_ports_m;
        std::function<void()>                                            task_m;

        outcome::result<void> ParseTransferTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        outcome::result<void> NotifyDestinationOfTransfer( const std::shared_ptr<IGeniusTransactions> &tx,
                                                            const std::optional<std::vector<uint8_t>>& proof);
        outcome::result<void> PostEscrowOnProcessingDB( const std::shared_ptr<IGeniusTransactions> &tx );

        static inline const std::unordered_map<std::string, TransactionParserFn> transaction_parsers = {
            { "transfer", &TransactionManager::ParseTransferTransaction },
            { "mint", &TransactionManager::ParseMintTransaction },
            { "escrow", &TransactionManager::ParseEscrowTransaction },
        };

        base::Logger m_logger = sgns::base::createLogger( "TransactionManager" );
    };
}

#endif
