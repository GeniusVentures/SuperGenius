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
        TransactionManager( std::shared_ptr<crdt::GlobalDB>           db,
                            std::shared_ptr<boost::asio::io_context>  ctx,
                            std::shared_ptr<GeniusAccount>            account,
                            std::shared_ptr<crypto::Hasher>           hasher,
                            std::shared_ptr<blockchain::BlockStorage> block_storage );

        ~TransactionManager() = default;

        void Start();
        void PrintAccountInfo();

        const GeniusAccount &GetAccount() const;

        const std::vector<std::vector<uint8_t>> &GetTransactions() const
        {
            return transactions;
        }

        bool                         TransferFunds( uint64_t amount, const uint256_t &destination );
        void                         MintFunds( uint64_t amount );
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

        void                     Update();
        void                     EnqueueTransaction( std::shared_ptr<IGeniusTransactions> element );
        SGTransaction::DAGStruct FillDAGStruct();
        outcome::result<void>    SendTransaction();
        std::string              GetTransactionPath( std::shared_ptr<IGeniusTransactions> element );
        outcome::result<void>    RecordBlock( const std::vector<std::string> &transaction_keys );

        outcome::result<std::vector<std::vector<uint8_t>>> GetTransactionsFromBlock(
            const primitives::BlockId &block_number );

        outcome::result<std::shared_ptr<IGeniusTransactions>> FetchTransaction( std::string_view transaction_key );
        outcome::result<std::vector<uint8_t>>                 ParseTransaction( std::string_view transaction_key );
        void ParseTransferTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        void ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx );
        void ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx );

        /**
         * @brief      Checks the blockchain for any new blocks to sync current values
         */
        void CheckBlockchain();

        std::shared_ptr<crdt::GlobalDB>                  db_m;
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

        base::Logger m_logger = sgns::base::createLogger( "TransactionManager" );
    };
}

#endif
