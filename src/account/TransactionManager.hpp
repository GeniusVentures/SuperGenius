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
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include "crdt/globaldb/globaldb.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "account/IGeniusTransactions.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/block_storage.hpp"
#include "base/logger.hpp"

using namespace boost::multiprecision;
namespace sgns
{
    class TransactionManager
    {
    public:
        TransactionManager( std::shared_ptr<crdt::GlobalDB>           db,      //
                            std::shared_ptr<boost::asio::io_context>  ctx,     //
                            std::shared_ptr<GeniusAccount>            account, //
                            std::shared_ptr<blockchain::BlockStorage> block_storage );
        ~TransactionManager() = default;
        void Start();
        void PrintAccountInfo();

        const GeniusAccount &GetAccount() const;

        void TransferFunds( const uint256_t &amount, const uint256_t &destination );
        void MintFunds( const uint64_t &amount );
        void HoldEscrow( const uint64_t &amount, const std::string &job_id );
        void ReleaseEscrow( const std::string &job_id );

    private:
        std::shared_ptr<crdt::GlobalDB>                  db_m;
        std::shared_ptr<boost::asio::io_context>         ctx_m;
        std::shared_ptr<GeniusAccount>                   account_m;
        std::deque<std::shared_ptr<IGeniusTransactions>> out_transactions;
        std::shared_ptr<boost::asio::steady_timer>       timer_m;
        mutable std::mutex                               mutex_m;
        primitives::BlockNumber                          last_block_id_m;
        std::uint64_t                                    last_trans_on_block_id;
        std::shared_ptr<blockchain::BlockStorage>        block_storage_m;
        //Hash256                                          last_transaction_hash;

        static constexpr std::uint16_t MAIN_NET_ID = 369;
        static constexpr std::uint16_t TEST_NET_ID = 963;

        base::Logger m_logger = sgns::base::createLogger( "TransactionManager" );

        static constexpr std::string_view TRANSACTION_BASE_FORMAT = "bc-%hu/";

        void                     Update();
        void                     EnqueueTransaction( std::shared_ptr<IGeniusTransactions> element );
        SGTransaction::DAGStruct FillDAGStruct();
        void                     SendTransaction();
        bool                     GetTransactionsFromBlock( const primitives::BlockNumber &block_number );

        void ParseTransaction( std::string transaction_key );

        void ParseTransferTransaction( const std::vector<std::uint8_t> &transaction_data );
        void ParseMintTransaction( const std::vector<std::uint8_t> &transaction_data );

            /**
         * @brief      Checks the blockchain for any new blocks to sync current values
         */
            void CheckBlockchain();
    };
}

#endif
