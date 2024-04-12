/**
 * @file       TransactionManager.cpp
 * @brief      
 * @date       2024-04-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "account/MintTransaction.hpp"

namespace sgns
{
    const boost::format TransactionManager::transfer_fmt_template( std::string( TransactionManager::TRANSACTION_BASE_FORMAT ) +
                                                                   std::string( TransactionManager::TRANSFER_FORMAT ) );
    const boost::format TransactionManager::process_fmt_template( std::string( TransactionManager::TRANSACTION_BASE_FORMAT ) +
                                                                  std::string( TransactionManager::PROCESSING_FORMAT ) );
    const boost::format TransactionManager::mint_fmt_template( std::string( TransactionManager::TRANSACTION_BASE_FORMAT ) +
                                                               std::string( TransactionManager::MINT_FORMAT ) );
    const boost::format TransactionManager::escrow_fmt_template( std::string( TransactionManager::TRANSACTION_BASE_FORMAT ) +
                                                                 std::string( TransactionManager::ESCROW_FORMAT ) );

    TransactionManager::TransactionManager( std::shared_ptr<crdt::GlobalDB> db, std::shared_ptr<boost::asio::io_context> ctx,
                                            std::shared_ptr<GeniusAccount>            account,
                                            std::shared_ptr<blockchain::BlockStorage> block_storage ) :
        db_m( std::move( db ) ),                                                                                    //
        ctx_m( std::move( ctx ) ),                                                                                  //
        account_m( std::move( account ) ),                                                                          //
        block_storage_m( std::move( block_storage ) ),                                                              //
        timer_m( std::make_shared<boost::asio::steady_timer>( *ctx_m, boost::asio::chrono::milliseconds( 300 ) ) ), //
        last_block_id_m( 0 ),                                                                                       //
        last_trans_on_block_id( 0 )

    {
        m_logger->set_level( spdlog::level::debug );
        m_logger->info( "Initializing values by reading whole blockchain" );

        CheckBlockchain();
        m_logger->info( "Last valid block ID" + std::to_string( last_block_id_m ) );
    }

    void TransactionManager::Start()
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

    void TransactionManager::PrintAccountInfo()
    {
        std::cout << "Account Address: " << account_m->GetAddress() << std::endl;
        std::cout << "Balance: " << account_m->GetBalance() << std::endl;
        std::cout << "Token Type: " << account_m->GetToken() << std::endl;
        std::cout << "Nonce: " << account_m->GetNonce() << std::endl;
    }

    const GeniusAccount &TransactionManager::GetAccount() const
    {
        return *account_m;
    }
    void TransactionManager::TransferFunds( const uint256_t &amount, const uint256_t &destination )
    {
        auto transfer_transaction = std::make_shared<sgns::TransferTransaction>( amount, destination, FillDAGStruct() );
        this->EnqueueTransaction( transfer_transaction );
    }
    void TransactionManager::MintFunds( const uint64_t &amount )
    {
        auto mint_transaction = std::make_shared<sgns::MintTransaction>( amount, FillDAGStruct() );
        this->EnqueueTransaction( mint_transaction );
    }

    void TransactionManager::Update()
    {
        SendTransaction();
        CheckBlockchain();
    }
    void TransactionManager::EnqueueTransaction( std::shared_ptr<IGeniusTransactions> element )
    {
        std::lock_guard<std::mutex> lock( mutex_m );
        out_transactions.emplace_back( std::move( element ) );
    }
    SGTransaction::DAGStruct TransactionManager::FillDAGStruct()
    {
        return SGTransaction::DAGStruct{};
    }

    void TransactionManager::SendTransaction()
    {
        std::unique_lock<std::mutex> lock( mutex_m );
        if ( !out_transactions.empty() )
        {
            boost::format transfer_tx_key( transfer_fmt_template );
            transfer_tx_key % TEST_NET_ID % account_m->GetAddress() % account_m->nonce;
            //std::string transaction_key =
            //    "bc-" + std::to_string( TEST_NET_ID ) + "/" + account_m->GetAddress() + "/tx/transfer/" + std::to_string( account_m->nonce );

            //std::string dagheader_key = "bc-" + std::to_string( TEST_NET_ID ) + "/blockchain/" + std::to_string( last_block_id_m );

            //std::string blockchainkey = dagheader_key + "/tx/" + std::to_string( last_trans_on_block_id );
            auto elem = out_transactions.front();
            out_transactions.pop_front();

            sgns::crdt::GlobalDB::Buffer data_transaction;

            data_transaction.put( elem->SerializeByteVector() );
            db_m->Put( { transfer_tx_key.str() }, data_transaction );

            auto maybe_last_hash   = block_storage_m->getLastFinalizedBlockHash();
            auto maybe_last_header = block_storage_m->getBlockHeader( maybe_last_hash.value() );

            primitives::BlockHeader header( maybe_last_header.value() );

            header.parent_hash = maybe_last_hash.value();

            header.number++;

            auto new_hash = block_storage_m->putBlockHeader( header );

            primitives::BlockData block_data;
            block_data.hash   = new_hash.value();
            block_data.header = header;
            primitives::BlockBody body{ { base::Buffer{}.put( transfer_tx_key.str() ) } };
            block_data.body = body;

            block_storage_m->putBlockData( header.number, block_data );

            //sgns::crdt::GlobalDB::Buffer data_key;
            //sgns::crdt::GlobalDB::Buffer data_dagheader;

            m_logger->debug( "Putting on " + transfer_tx_key.str() + " " + std::string( data_transaction.toString() ) );

            //data_key.put( transfer_tx_key.str() );
            //db_m->Put( { blockchainkey }, data_key );
            m_logger->debug( "Recording BlockHeader with number " + std::to_string( header.number ) );
            m_logger->debug( "Recording BlockData with number " + std::to_string( header.number ) );
            block_storage_m->setLastFinalizedBlockHash( new_hash.value() );
            //TODO - Fix this. Now I'm putting the sender's address. so it updates its values and nonce
            //data_dagheader.put( account_m->GetAddress() );
            //db_m->Put( { dagheader_key }, data_dagheader );
            //m_logger->debug( "Putting on " + dagheader_key + " " + std::string( data_dagheader.toString() ) );
        }
        lock.unlock(); // Manual unlock, no need to wait to run out of scope
    }
    void TransactionManager::GetTransactionsFromBlock( const primitives::BlockNumber &block_number )
    {
        outcome::result<primitives::BlockBody> retval          = outcome::failure( boost::system::error_code{} );
        std::size_t                            transaction_num = 0;
        //do
        {
            retval = block_storage_m->getBlockBody( block_number /*, transaction_num*/ );
            m_logger->debug( "Trying to read transaction " + std::to_string( transaction_num ) );

            if ( retval )
            {
                //any block will need checking
                m_logger->debug( "Getting transaction " + std::to_string( transaction_num ) );

                //this is a vector<extrinsics>, which is a vector<buffer>
                auto block_body = retval.value();
                //m_logger->debug( "Destination address of Header: " + std::string( DAGHeader.toString() ) );

                //Just one buffer for now
                if ( block_body.size() == 1 )
                {
                    m_logger->info( "The block data is " + std::string( block_body[0].data.toString() ) );

                    ParseTransaction( std::string( block_body[0].data.toString() ) );
                }
                else
                {
                    m_logger->info( "The block size is " + std::to_string( block_body.size() ) );
                }
            }
            transaction_num++;
        }
    }
    void TransactionManager::ParseTransaction( std::string transaction_key )
    {
        auto maybe_transaction_data = db_m->Get( { transaction_key } );
        m_logger->debug( "Fetching transaction on " + transaction_key );
        if ( maybe_transaction_data )
        {
            auto maybe_dag = IGeniusTransactions::DeSerializeDAGStruct( maybe_transaction_data.value().toVector() );
            m_logger->debug( "Found the data, deserializing into DAG " + transaction_key );
            if ( maybe_dag )
            {
                m_logger->debug( "DAG DESERIALIZED! " + maybe_dag.value().type() );
                if ( maybe_dag.value().type() == "transfer" )
                {
                    m_logger->info( "Transfer transaction" );
                    TransferTransaction tx = TransferTransaction::DeSerializeByteVector( maybe_transaction_data.value().toVector() );

                    std::cout << tx.GetAddress<std::string>() << std::endl;
                    if ( tx.GetAddress<uint256_t>() == account_m->address )
                    {
                        m_logger->info( "NEW TRANSACTION TO ME " + tx.GetAddress<std::string>() );
                        account_m->balance += static_cast<uint64_t>( tx.GetAmount<uint256_t>() );
                        m_logger->info( "Updated balance " + std::to_string( account_m->balance ) );
                    }
                }
            }
        }
    }

    /**
         * @brief      Checks the blockchain for any new blocks to sync current values
         */
    void TransactionManager::CheckBlockchain()
    {
        outcome::result<primitives::BlockHeader> retval = outcome::failure( boost::system::error_code{} );
        do
        {
            retval = block_storage_m->getBlockHeader( last_block_id_m );
            if ( retval )
            {
                //any block will need checking
                m_logger->debug( "Found new blockchain entry for block " + std::to_string( last_block_id_m ) );
                m_logger->debug( "Getting DAGHeader value" );
                bool incoming = true;

                auto DAGHeader = retval.value();
                //m_logger->debug( "Destination address of Header: " + std::string( DAGHeader.toString() ) );

                //validation that index is the same as number
                if ( DAGHeader.number == last_block_id_m )
                {
                    m_logger->info( "Checking transactions from block" );
                    GetTransactionsFromBlock( DAGHeader.number );
                }
            }
            last_block_id_m++;
        } while ( retval );
    }

    void TransactionManager::GetOutgoingTransactionData( std::string key )
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

            if ( maybe_transfer.size() == 8 )
            {
                MintTransaction received = MintTransaction::DeSerializeByteVector( maybe_transfer.toVector() );

                m_logger->info( "Minting new Tokens " + std::to_string( received.GetAmount() ) );
                account_m->balance += received.GetAmount();
                m_logger->info( "Updated balance " + std::to_string( account_m->balance ) );
            }
        }
    }
}