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
#include <utility>
#include <map>
#include <unordered_map>

#include <boost/asio.hpp>
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
#include "crypto/hasher.hpp"
#include "proof/proto/SGProof.pb.h"
#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"
#include "primitives/common.hpp"
#include "upnp.hpp"

namespace sgns
{
    using namespace boost::multiprecision;

    class TransactionManager
    {
    public:
        using TransactionPair = std::pair<std::shared_ptr<IGeniusTransactions>, std::vector<uint8_t>>;

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

        const std::vector<std::vector<uint8_t>> GetOutTransactions() const;
        const std::vector<std::vector<uint8_t>> GetInTransactions() const;

        bool TransferFunds( double amount, const uint256_t &destination );
        void MintFunds( double amount, std::string transaction_hash, std::string chainid, std::string tokenid );
        outcome::result<std::string> HoldEscrow( double             amount,
                                                 const uint256_t   &dev_addr,
                                                 float              peers_cut,
                                                 const std::string &job_id );
        outcome::result<void> ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult );
        double                GetBalance();

    private:
        static constexpr std::uint16_t    MAIN_NET_ID             = 369;
        static constexpr std::uint16_t    TEST_NET_ID             = 963;
        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "bc-%hu/";
        using TransactionParserFn =
            outcome::result<void> ( TransactionManager::* )( const std::shared_ptr<IGeniusTransactions> & );

        void                     Update();
        void                     EnqueueTransaction( TransactionPair element );
        SGTransaction::DAGStruct FillDAGStruct( std::string transaction_hash = "" );
        outcome::result<void>    SendTransaction();
        std::string              GetTransactionPath( std::shared_ptr<IGeniusTransactions> element );
        std::string              GetTransactionProofPath( std::shared_ptr<IGeniusTransactions> element );

        outcome::result<std::shared_ptr<IGeniusTransactions>> FetchTransaction(
            const std::shared_ptr<crdt::GlobalDB> &db,
            std::string_view                       transaction_key );
        outcome::result<bool> CheckProof( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        outcome::result<void> CheckIncoming();

        outcome::result<void> CheckOutgoing();

        void RefreshPorts();

        std::shared_ptr<crdt::GlobalDB>                  processing_db_m;
        std::shared_ptr<boost::asio::io_context>         ctx_m;
        std::shared_ptr<GeniusAccount>                   account_m;
        std::shared_ptr<crypto::Hasher>                  hasher_m;
        std::string                                      base_path_m;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub_m;
        std::shared_ptr<upnp::UPNP>                      upnp_m;
        uint16_t                                         base_port_m;
        std::shared_ptr<boost::asio::steady_timer>       timer_m;
        std::shared_ptr<crdt::GlobalDB>                  outgoing_db_m;
        std::shared_ptr<crdt::GlobalDB>                  incoming_db_m;
        std::deque<TransactionPair>                      tx_queue_m;
        mutable std::mutex                               mutex_m;

        std::map<std::string, std::vector<std::uint8_t>>                 outgoing_tx_processed_m;
        std::map<std::string, std::vector<std::uint8_t>>                 incoming_tx_processed_m;
        std::unordered_map<std::string, std::shared_ptr<crdt::GlobalDB>> destination_dbs_m;
        std::set<uint16_t>                                               used_ports_m;

        outcome::result<void> ParseTransferTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        outcome::result<void> NotifyDestinationOfTransfer( const std::shared_ptr<IGeniusTransactions> &tx, std::optional<std::vector<uint8_t>> proof );
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
