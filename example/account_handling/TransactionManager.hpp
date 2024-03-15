/**
 * @file       TransactionManager.hpp
 * @brief      
 * @date       2024-03-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRANSACTION_MANAGER_HPP_
#define _TRANSACTION_MANAGER_HPP_
#include <memory>
#include <boost/asio.hpp>
#include <deque>
#include <sstream>
#include <crdt/globaldb/globaldb.hpp>
#include "account/IGeniusTransactions.hpp"
#include "account/TransferTransaction.hpp"
#include "account/GeniusAccount.hpp"

#include "base/logger.hpp"

namespace sgns
{
    class TransactionManager
    {
    public:
        TransactionManager( std::shared_ptr<crdt::GlobalDB> db, std::shared_ptr<boost::asio::io_context> ctx,
                            std::shared_ptr<GeniusAccount> account ) :
            db_m( std::move( db ) ),                                                                                    //
            ctx_m( std::move( ctx ) ),                                                                                  //
            account_m( std::move( account ) ),                                                                          //
            timer_m( std::make_shared<boost::asio::steady_timer>( *ctx_m, boost::asio::chrono::milliseconds( 300 ) ) ), //
            last_block_id( 0 ),                                                                                         //
            last_trans_on_block_id( 0 )

        {
            m_logger->info( "Initializing values by reading whole blockchain" );

            CheckBlockchain();
            m_logger->info( "Last valid block ID" + std::to_string( last_block_id ) );
        }
        void Start()
        {
            auto task = std::make_shared<std::function<void()>>();

            *task = [this, task]()
            {
                this->Update();
                this->timer_m->expires_after( boost::asio::chrono::milliseconds( 300 ) );

                this->timer_m->async_wait( [this, task]( const boost::system::error_code & ) { this->ctx_m->post( *task ); } );
            };
            ctx_m->post( *task );
        }

        void Update()
        {
            SendTransaction();
            CheckBlockchain();
        }

        void EnqueueTransaction( std::shared_ptr<IGeniusTransactions> element )
        {
            std::lock_guard<std::mutex> lock( mutex_m );
            out_transactions.emplace_back( std::move( element ) );
        }
        void PrintAccountInfo()
        {
            std::cout << "Account Address: " << account_m->GetAddress() << std::endl;
            std::cout << "Balance: " << account_m->GetBalance() << std::endl;
            std::cout << "Token Type: " << account_m->GetToken() << std::endl;
            std::cout << "Nonce: " << account_m->GetNonce() << std::endl;
        }
        const GeniusAccount &GetAccount() const
        {
            return *account_m;
        }

        ~TransactionManager() = default;

    private:
        std::shared_ptr<crdt::GlobalDB>                  db_m;
        std::shared_ptr<boost::asio::io_context>         ctx_m;
        std::shared_ptr<GeniusAccount>                   account_m;
        std::deque<std::shared_ptr<IGeniusTransactions>> out_transactions;
        std::shared_ptr<boost::asio::steady_timer>       timer_m;
        mutable std::mutex                               mutex_m;
        std::uint64_t                                    last_block_id;
        std::uint64_t                                    last_trans_on_block_id;

        static constexpr std::string_view MAIN_NET = "369";
        static constexpr std::string_view TEST_NET = "963";

        base::Logger m_logger = sgns::base::createLogger( "TransactionManager" );

        // static constexpr const char formatTransfer[]   = "bc-{}/{}/tx/transfer/{}";
        // static constexpr const char formatProcessing[] = "bc-{}/{}/tx/processing/{}/{}/{}";
        // static constexpr const char formatProof[]      = "bc-{}/{}/tx/processing/proof{}";
        // static constexpr const char formatBlock[]      = "bc-{}/blockchain/{}";

        void SendTransaction()
        {
            std::unique_lock<std::mutex> lock( mutex_m );
            if ( !out_transactions.empty() )
            {

                std::string transaction_key =
                    "bc-" + std::string( TEST_NET ) + "/" + account_m->GetAddress() + "/tx/transfer/" + std::to_string( account_m->nonce );

                std::string dagheader_key = "bc-" + std::string( TEST_NET ) + "/blockchain/" + std::to_string( last_block_id );

                std::string blockchainkey = dagheader_key + "/tx/" + std::to_string( last_trans_on_block_id );

                sgns::crdt::GlobalDB::Buffer data_transaction;
                sgns::crdt::GlobalDB::Buffer data_key;
                sgns::crdt::GlobalDB::Buffer data_dagheader;

                auto elem = out_transactions.front();
                out_transactions.pop_front();

                data_transaction.put( elem->SerializeByteVector() );

                db_m->Put( { transaction_key }, data_transaction );
                m_logger->debug( "Putting on " + transaction_key + " " + std::string( data_transaction.toString() ) );

                data_key.put( transaction_key );
                db_m->Put( { blockchainkey }, data_key );
                m_logger->debug( "Putting on " + blockchainkey + " " + std::string( data_key.toString() ) );

                //TODO - Fix this. Now I'm putting the sender's address. so it updates its values and nonce
                data_dagheader.put( account_m->GetAddress() );
                db_m->Put( { dagheader_key }, data_dagheader );
                m_logger->debug( "Putting on " + dagheader_key + " " + std::string( data_dagheader.toString() ) );
            }
            lock.unlock(); // Manual unlock, no need to wait to run out of scope
        }

        void GetTransactionsFromBlock( bool incoming, std::string block_path )
        {
            outcome::result<crdt::GlobalDB::Buffer> retval           = outcome::failure( boost::system::error_code{} );
            uint32_t                                num_transactions = 0;

            do
            {
                std::string next_transaction = block_path + "/tx/" + std::to_string( num_transactions );
                m_logger->debug( "Getting transaction list from: " + next_transaction );
                //search for the next transaction
                retval = db_m->Get( { next_transaction } );
                if ( retval )
                {
                    //there is a transaction on the block.
                    //the retval.value() will be the key for the transaction payload
                    auto transaction_key = retval.value();

                    m_logger->info( "Transaction found on " + std::string( transaction_key.toString() ) );
                    if ( incoming )
                    {
                        GetIncomingTransactionData( std::string( transaction_key.toString() ) );
                    }
                    else
                    {
                        GetOutgoingTransactionData( std::string( transaction_key.toString() ) );
                    }

                    num_transactions++;
                }
            } while ( retval );
        }
        /**
         * @brief      Checks the blockchain for any new blocks to sync current values
         */
        void CheckBlockchain()
        {
            outcome::result<crdt::GlobalDB::Buffer> retval = outcome::failure( boost::system::error_code{} );

            do
            {
                std::string blockchainkey = "bc-" + std::string( TEST_NET ) + "/blockchain/" + std::to_string( last_block_id );

                //search for the latest blockchain entry
                retval = db_m->Get( { blockchainkey } );
                if ( retval )
                {
                    m_logger->debug( "Found new blockchain entry on " + blockchainkey );
                    m_logger->debug( "Getting DAGHeader value" );
                    bool incoming = true;

                    auto DAGHeader = retval.value();
                    m_logger->debug( "Destination address of Header: " + std::string( DAGHeader.toString() ) );

                    if ( DAGHeader.toString() == account_m->GetAddress() )
                    {
                        m_logger->info( "The transaction is from me, incrementing nonce" );
                        incoming = false;
                        account_m->nonce++;
                    }

                    GetTransactionsFromBlock( incoming, blockchainkey );

                    last_block_id++;
                }
            } while ( retval );
        }

        void GetIncomingTransactionData( std::string key )
        {
            outcome::result<crdt::GlobalDB::Buffer> retval           = outcome::failure( boost::system::error_code{} );
            uint32_t                                num_transactions = 0;

            retval = db_m->Get( { key } );
            if ( retval )
            {
                //Found transaction. See DAGStruct for transaction type.
                //If transfer, update funds
                auto maybe_transfer = retval.value();
                m_logger->debug( "Transfer transaction data " + std::string( maybe_transfer.toHex() ) );

                if ( maybe_transfer.size() == 64 )
                {
                    TransferTransaction received = TransferTransaction::DeSerializeByteVector( maybe_transfer.toVector() );

                    m_logger->debug( "RECEIVER ADDRESS " + received.GetAddress<std::string>() );
                    m_logger->debug( "MY ADDRESS " + account_m->GetAddress() );

                    if ( received.GetAddress<uint256_t>() == account_m->address )
                    {
                        m_logger->info( "NEW TRANSACTION TO ME " + received.GetAddress<std::string>() );
                        account_m->balance += static_cast<uint64_t>( received.GetAmount<uint256_t>() );
                        m_logger->info( "Updated balance " + std::to_string( account_m->balance ) );
                    }
                }
            }
        }
        void GetOutgoingTransactionData( std::string key )
        {
            outcome::result<crdt::GlobalDB::Buffer> retval           = outcome::failure( boost::system::error_code{} );
            uint32_t                                num_transactions = 0;

            retval = db_m->Get( { key } );
            if ( retval )
            {
                //Found transaction. See DAGStruct for transaction type.
                //If transfer, update funds
                auto maybe_transfer = retval.value();
                m_logger->debug( "Transfer transaction data " + std::string( maybe_transfer.toHex() ) );

                if ( maybe_transfer.size() == 64 )
                {
                    TransferTransaction received = TransferTransaction::DeSerializeByteVector( maybe_transfer.toVector() );

                    m_logger->info( "Transaction from me " + received.GetAddress<std::string>() );
                    account_m->balance -= static_cast<uint64_t>( received.GetAmount<uint256_t>() );
                    m_logger->info( "Updated balance " + std::to_string( account_m->balance ) );
                }
            }
        }
    };
}

#endif
