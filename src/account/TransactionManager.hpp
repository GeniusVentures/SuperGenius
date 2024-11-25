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
        using ProcessFinishCbType = std::function<
            void( const std::string &, const std::set<std::string> &, const std::vector<OutputDestInfo> & )>;

        TransactionManager( std::shared_ptr<crdt::GlobalDB>           db,
                            std::shared_ptr<boost::asio::io_context>  ctx,
                            std::shared_ptr<GeniusAccount>            account,
                            std::shared_ptr<crypto::Hasher>           hasher,
                            std::shared_ptr<blockchain::BlockStorage> block_storage );

        TransactionManager( std::shared_ptr<crdt::GlobalDB>           db,
                            std::shared_ptr<boost::asio::io_context>  ctx,
                            std::shared_ptr<GeniusAccount>            account,
                            std::shared_ptr<crypto::Hasher>           hasher,
                            std::shared_ptr<blockchain::BlockStorage> block_storage,
                            ProcessFinishCbType                       processing_finished_cb );

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
                                                 uint64_t           num_chunks,
                                                 const uint256_t   &dev_addr,
                                                 float              dev_cut,
                                                 const std::string &job_id );
        bool                         ReleaseEscrow( const std::string                 &job_id,
                                                    const bool                        &pay,
                                                    const std::vector<OutputDestInfo> &destinations );
        void     ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult );
        uint64_t GetBalance();

    private:
        struct EscrowCtrl
        {
            EscrowCtrl( uint256_t     addr,
                        float         cut,
                        uint256_t     hash,
                        uint256_t     amount,
                        uint64_t      subtasks_num,
                        InputUTXOInfo input ) :
                dev_addr( std::move( addr ) ),
                dev_cut( cut ),
                job_hash( std::move( hash ) ),
                full_amount( std::move( amount ) ),
                num_subtasks( subtasks_num ),
                original_input( std::move( input ) )
            {
            }

            uint256_t     dev_addr;
            float         dev_cut;
            uint256_t     job_hash;
            uint256_t     full_amount;
            uint64_t      num_subtasks;
            InputUTXOInfo original_input;
        };

        static constexpr std::uint16_t    MAIN_NET_ID             = 369;
        static constexpr std::uint16_t    TEST_NET_ID             = 963;
        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "bc-%hu/";

        void                     Update();
        void                     EnqueueTransaction( std::shared_ptr<IGeniusTransactions> element );
        SGTransaction::DAGStruct FillDAGStruct();
        SGTransaction::DAGStruct FillDAGStructProc();
        outcome::result<void>    SendTransaction();
        std::string              GetTransactionPath( std::shared_ptr<IGeniusTransactions> element );
        outcome::result<void>    RecordBlock( const std::vector<std::string> &transaction_keys );

        outcome::result<std::vector<std::vector<uint8_t>>> GetTransactionsFromBlock(
            const primitives::BlockId &block_number );

        outcome::result<std::vector<uint8_t>> ParseTransaction( std::string_view transaction_key );
        void ParseTransferTransaction( const std::vector<std::uint8_t> &transaction_data );
        void ParseMintTransaction( const std::vector<std::uint8_t> &transaction_data );
        void ParseEscrowTransaction( const std::vector<std::uint8_t> &transaction_data );
        void ParseProcessingTransaction( const std::vector<std::uint8_t> &transaction_data );

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
        ProcessFinishCbType                              processing_finished_cb_m;

        //TODO - Replace with std::unordered_map<std::string, EscrowCtrl> for better performance, maybe?
        std::vector<EscrowCtrl> escrow_ctrl_m;

        base::Logger m_logger = sgns::base::createLogger( "TransactionManager" );
    };
}

#endif
