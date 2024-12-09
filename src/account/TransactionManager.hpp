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
#include "blockchain/block_storage.hpp"
#include "base/logger.hpp"
#include "crypto/hasher.hpp"
#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"
#include "primitives/common.hpp"

namespace sgns
{
    using namespace boost::multiprecision;

    class TransactionManager
    {
    public:
        TransactionManager( std::shared_ptr<boost::asio::io_context>         ctx,
                            std::shared_ptr<GeniusAccount>                   account,
                            std::shared_ptr<crypto::Hasher>                  hasher,
                            std::shared_ptr<blockchain::BlockStorage>        block_storage,
                            std::string                                      base_path,
                            uint16_t                                         port,
                            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub );

        ~TransactionManager() = default;

        void Start();
        void PrintAccountInfo();

        const GeniusAccount &GetAccount() const;

        const std::vector<std::vector<uint8_t>> &GetTransactions() const
        {
            return transactions;
        }

        bool TransferFunds( uint64_t amount, const uint256_t &destination );
        void MintFunds( uint64_t amount, std::string transaction_hash, std::string chainid, std::string tokenid );
        outcome::result<std::string> HoldEscrow( uint64_t           amount,
                                                 const uint256_t   &dev_addr,
                                                 float              peers_cut,
                                                 const std::string &job_id );
        void     ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult );
        uint64_t GetBalance();

    private:
        static constexpr std::uint16_t    MAIN_NET_ID             = 369;
        static constexpr std::uint16_t    TEST_NET_ID             = 963;
        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "bc-%hu/";
        using TransactionParserFn =
            outcome::result<void> ( TransactionManager::* )( const std::shared_ptr<IGeniusTransactions> & );

        void                     Update();
        void                     EnqueueTransaction( std::shared_ptr<IGeniusTransactions> element );
        SGTransaction::DAGStruct FillDAGStruct( std::string transaction_hash = "" );
        outcome::result<void>    SendTransaction();
        std::string              GetTransactionPath( std::shared_ptr<IGeniusTransactions> element );
        outcome::result<void>    RecordBlock( const std::vector<std::string> &transaction_keys );

        outcome::result<std::vector<std::vector<uint8_t>>> GetTransactionsFromBlock(
            const primitives::BlockId &block_number );

        outcome::result<std::shared_ptr<IGeniusTransactions>> FetchTransaction(
            const std::shared_ptr<crdt::GlobalDB> &db,
            std::string_view                       transaction_key );
        outcome::result<void> ParseTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        /**
         * @brief      Checks the blockchain for any new blocks to sync current values
         */
        void CheckBlockchain();

        outcome::result<void> CheckIncoming();

        outcome::result<void> CheckOutgoing();

        std::shared_ptr<crdt::GlobalDB>                  outgoing_db_m;
        std::shared_ptr<crdt::GlobalDB>                  incoming_db_m;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub_m;
        std::string                                      base_path_m;
        uint16_t                                         port_m;
        std::shared_ptr<boost::asio::io_context>         ctx_m;
        std::shared_ptr<GeniusAccount>                   account_m;
        std::vector<std::vector<std::uint8_t>>           transactions;
        std::deque<std::shared_ptr<IGeniusTransactions>> out_transactions;
        std::shared_ptr<boost::asio::steady_timer>       timer_m;
        mutable std::mutex                               mutex_m;
        primitives::BlockNumber                          last_block_id_m;
        std::uint64_t                                    last_trans_on_block_id;
        std::shared_ptr<blockchain::BlockStorage>        block_storage_m;
        std::shared_ptr<crypto::Hasher>                  hasher_m;

        std::unordered_map<std::string, std::vector<std::uint8_t>>       outgoing_tx_processed_m;
        std::unordered_map<std::string, std::vector<std::uint8_t>>       incoming_tx_processed_m;
        std::unordered_map<std::string, std::shared_ptr<crdt::GlobalDB>> destination_dbs_m;

        outcome::result<void> ParseTransferTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        outcome::result<void> ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        outcome::result<void> NotifyDestinationOfTransfer( const std::shared_ptr<IGeniusTransactions> &tx );
        static inline const std::unordered_map<std::string, TransactionParserFn> transaction_parsers = {
            { "transfer", &TransactionManager::ParseTransferTransaction },
            { "mint", &TransactionManager::ParseMintTransaction },
            { "escrow", &TransactionManager::ParseEscrowTransaction },

        };

        base::Logger m_logger = sgns::base::createLogger( "TransactionManager" );
    };
}

#endif
