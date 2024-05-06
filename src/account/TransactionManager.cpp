/**
 * @file       TransactionManager.cpp
 * @brief      
 * @date       2024-04-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "account/MintTransaction.hpp"
#include "account/ProcessingTransaction.hpp"

namespace sgns
{
    TransactionManager::TransactionManager( std::shared_ptr<crdt::GlobalDB>           db,
                                            std::shared_ptr<boost::asio::io_context>  ctx,
                                            std::shared_ptr<GeniusAccount>            account,
                                            std::shared_ptr<blockchain::BlockStorage> block_storage ) :
        db_m( std::move( db ) ),                                                                                    //
        ctx_m( std::move( ctx ) ),                                                                                  //
        account_m( std::move( account ) ),                                                                          //
        timer_m( std::make_shared<boost::asio::steady_timer>( *ctx_m, boost::asio::chrono::milliseconds( 300 ) ) ), //
        last_block_id_m( 0 ),                                                                                       //
        last_trans_on_block_id( 0 ),                                                                                //
        block_storage_m( std::move( block_storage ) )

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

            this->timer_m->async_wait( [this, task]( const boost::system::error_code & )
                                       { this->ctx_m->post( *task ); } );
        };
        ctx_m->post( *task );
    }

    void TransactionManager::PrintAccountInfo()
    {
        std::cout << "Account Address: " << account_m->GetAddress<std::string>() << std::endl;
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

    void TransactionManager::HoldEscrow( const uint64_t &amount )
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

    //TODO - Fill hash stuff on DAGStruct
    SGTransaction::DAGStruct TransactionManager::FillDAGStruct()
    {
        SGTransaction::DAGStruct dag;
        auto                     timestamp = std::chrono::system_clock::now();

        dag.set_previous_hash( "" );
        dag.set_nonce( account_m->nonce );
        dag.set_source_addr( account_m->GetAddress<std::string>() );
        dag.set_timestamp( timestamp.time_since_epoch().count() );
        dag.set_uncle_hash( "" );
        dag.set_data_hash( "" ); //filled byt transaction class
        return dag;
    }

    void TransactionManager::SendTransaction()
    {
        std::unique_lock<std::mutex> lock( mutex_m );
        if ( !out_transactions.empty() )
        {
            auto elem = out_transactions.front();
            out_transactions.pop_front();
            boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

            tx_key % TEST_NET_ID;

            auto transaction_path = tx_key.str() + elem->GetTransactionFullPath();

            sgns::crdt::GlobalDB::Buffer data_transaction;

            data_transaction.put( elem->SerializeByteVector() );
            db_m->Put( { transaction_path }, data_transaction );
            account_m->nonce++;

            auto maybe_last_hash   = block_storage_m->getLastFinalizedBlockHash();
            auto maybe_last_header = block_storage_m->getBlockHeader( maybe_last_hash.value() );

            primitives::BlockHeader header( maybe_last_header.value() );

            header.parent_hash = maybe_last_hash.value();

            header.number++;

            auto new_hash = block_storage_m->putBlockHeader( header );

            primitives::BlockData block_data;
            block_data.hash   = new_hash.value();
            block_data.header = header;
            primitives::BlockBody body{ { base::Buffer{}.put( transaction_path ) } };
            block_data.body = body;

            block_storage_m->putBlockData( header.number, block_data );

            m_logger->debug( "Putting on " + transaction_path +
                             " the data: " + std::string( data_transaction.toString() ) );
            m_logger->debug( "Recording Block with number " + std::to_string( header.number ) );
            block_storage_m->setLastFinalizedBlockHash( new_hash.value() );
        }
        lock.unlock(); // Manual unlock, no need to wait to run out of scope
    }

    bool TransactionManager::GetTransactionsFromBlock( const primitives::BlockNumber &block_number )
    {
        outcome::result<primitives::BlockBody> retval          = outcome::failure( boost::system::error_code{} );
        std::size_t                            transaction_num = 0;
        bool                                   ret             = false;
        //do
        {
            retval = block_storage_m->getBlockBody( block_number /*, transaction_num*/ );
            //m_logger->debug( "Trying to read transaction " + std::to_string( transaction_num ) );

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
                transaction_num++;
                ret = true;
            }
        }
        //while ( !retval );
        return ret;
    }

    void TransactionManager::ParseTransaction( std::string transaction_key )
    {
        auto maybe_transaction_data = db_m->Get( { transaction_key } );
        if ( maybe_transaction_data )
        {
            auto maybe_dag = IGeniusTransactions::DeSerializeDAGStruct( maybe_transaction_data.value().toVector() );
            m_logger->debug( "Found the data, deserializing into DAG " + transaction_key );
            if ( maybe_dag )
            {
                const std::string &string_src_address = maybe_dag.value().source_addr();
                if ( string_src_address == account_m->GetAddress<std::string>() )
                {
                    account_m->nonce = maybe_dag.value().nonce() + 1;
                }
                if ( maybe_dag.value().type() == "transfer" )
                {
                    m_logger->info( "Transfer transaction" );
                    TransferTransaction tx =
                        TransferTransaction::DeSerializeByteVector( maybe_transaction_data.value().toVector() );
                    if ( tx.GetDstAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
                    {
                        account_m->balance += static_cast<uint64_t>( tx.GetAmount<uint256_t>() );
                        m_logger->info( "Added tokens, balance " + std::to_string( account_m->balance ) );
                    }
                    if ( tx.GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
                    {
                        account_m->balance -= static_cast<uint64_t>( tx.GetAmount<uint256_t>() );
                        m_logger->info( "Subtracted tokens, balance " + std::to_string( account_m->balance ) );
                    }
                }
                else if ( maybe_dag.value().type() == "mint" )
                {
                    m_logger->info( "Mint transaction" );
                    MintTransaction tx =
                        MintTransaction::DeSerializeByteVector( maybe_transaction_data.value().toVector() );

                    //std::cout << tx.GetAddress<std::string>() << std::endl;
                    if ( tx.GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
                    {
                        account_m->balance += tx.GetAmount();
                        m_logger->info( "Created tokens, balance " + std::to_string( account_m->balance ) );
                    }
                }
                else if ( maybe_dag.value().type() == "escrow" )
                {
                }
                else if ( maybe_dag.value().type() == "process" )
                {
                    m_logger->info( "Process transaction" );
                    ProcessingTransaction tx =
                        ProcessingTransaction::DeSerializeByteVector( maybe_transaction_data.value().toVector() );

                    //std::cout << tx.GetAddress<std::string>() << std::endl;
                    /*if ( tx.GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
                    {
                        account_m->balance += tx.GetAmount();
                        m_logger->info( "Created tokens, balance " + std::to_string( account_m->balance ) );
                    }*/
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
                bool valid_transaction = true;

                auto DAGHeader = retval.value();
                //m_logger->debug( "Destination address of Header: " + std::string( DAGHeader.toString() ) );

                //validation that index is the same as number
                if ( DAGHeader.number == last_block_id_m )
                {
                    m_logger->info( "Checking transactions from block" );
                    valid_transaction = GetTransactionsFromBlock( DAGHeader.number );
                }
                if ( valid_transaction )
                {
                    last_block_id_m++;
                }
                else
                {
                    m_logger->debug( "Invalid transaction" );
                }
            }

        } while ( retval );
    }

}
