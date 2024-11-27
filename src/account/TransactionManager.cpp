/**
 * @file       TransactionManager.cpp
 * @brief      
 * @date       2024-04-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/TransactionManager.hpp"

#include <utility>

#include "account/TransferTransaction.hpp"
#include "account/MintTransaction.hpp"
#include "account/ProcessingTransaction.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/UTXOTxParameters.hpp"
#include "outcome/outcome.hpp"
#include "primitives/block.hpp"

namespace sgns
{
    TransactionManager::TransactionManager( std::shared_ptr<crdt::GlobalDB>           db,
                                            std::shared_ptr<boost::asio::io_context>  ctx,
                                            std::shared_ptr<GeniusAccount>            account,
                                            std::shared_ptr<crypto::Hasher>           hasher,
                                            std::shared_ptr<blockchain::BlockStorage> block_storage ) :
        TransactionManager( std::move( db ),
                            std::move( ctx ),
                            std::move( account ),
                            std::move( hasher ),
                            std::move( block_storage ),
                            nullptr )

    {
    }

    TransactionManager::TransactionManager( std::shared_ptr<crdt::GlobalDB>           db,
                                            std::shared_ptr<boost::asio::io_context>  ctx,
                                            std::shared_ptr<GeniusAccount>            account,
                                            std::shared_ptr<crypto::Hasher>           hasher,
                                            std::shared_ptr<blockchain::BlockStorage> block_storage,
                                            ProcessFinishCbType                       processing_finished_cb ) :
        db_m( std::move( db ) ),
        ctx_m( std::move( ctx ) ),
        account_m( std::move( account ) ),
        timer_m( std::make_shared<boost::asio::steady_timer>( *ctx_m, boost::asio::chrono::milliseconds( 300 ) ) ),
        last_block_id_m( 0 ),
        last_trans_on_block_id( 0 ),
        block_storage_m( std::move( block_storage ) ),
        hasher_m( std::move( hasher ) ),
        processing_finished_cb_m( std::move( processing_finished_cb ) )

    {
        m_logger->set_level( spdlog::level::off );
        m_logger->info( "Initializing values by reading whole blockchain" );
    }

    void TransactionManager::Start()
    {
        CheckBlockchain();

        m_logger->info( "Last valid block ID" + std::to_string( last_block_id_m ) );
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
        std::cout << "Balance: " << account_m->GetBalance<std::string>() << std::endl;
        std::cout << "Token Type: " << account_m->GetToken() << std::endl;
        std::cout << "Nonce: " << account_m->GetNonce() << std::endl;
    }

    const GeniusAccount &TransactionManager::GetAccount() const
    {
        return *account_m;
    }

    bool TransactionManager::TransferFunds( uint64_t amount, const uint256_t &destination )
    {
        bool ret          = false;
        auto maybe_params = UTXOTxParameters::create( account_m->utxos,
                                                      account_m->address.GetPublicKey(),
                                                      amount,
                                                      destination );

        if ( maybe_params )
        {
            account_m->utxos          = UTXOTxParameters::UpdateUTXOList( account_m->utxos, maybe_params.value() );
            auto transfer_transaction = std::make_shared<TransferTransaction>( maybe_params.value().outputs_,
                                                                               maybe_params.value().inputs_,
                                                                               FillDAGStruct() );
            this->EnqueueTransaction( transfer_transaction );
            ret = true;
        }

        return ret;
    }

    void TransactionManager::MintFunds( uint64_t amount )
    {
        auto mint_transaction = std::make_shared<MintTransaction>( amount, FillDAGStruct() );
        this->EnqueueTransaction( std::move( mint_transaction ) );
    }

    outcome::result<std::string> TransactionManager::HoldEscrow( uint64_t           amount,
                                                                 const uint256_t   &dev_addr,
                                                                 float              peers_cut,
                                                                 const std::string &job_id )
    {
        bool ret       = false;
        auto hash_data = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );

        OUTCOME_TRY( ( auto &&, params ),
                     UTXOTxParameters::create( account_m->utxos,
                                               account_m->address.GetPublicKey(),
                                               uint64_t{ amount },
                                               uint256_t{ "0x" + hash_data.toReadableString() } ) );

        account_m->utxos        = UTXOTxParameters::UpdateUTXOList( account_m->utxos, params );
        auto escrow_transaction = std::make_shared<EscrowTransaction>( params,
                                                                       amount,
                                                                       dev_addr,
                                                                       peers_cut,
                                                                       FillDAGStruct() );
        this->EnqueueTransaction( escrow_transaction );

        return GetTransactionPath( escrow_transaction );
    }

    bool TransactionManager::ReleaseEscrow( const std::string                 &job_id,
                                            const bool                        &pay,
                                            const std::vector<OutputDestInfo> &destinations )
    {
        //if ( escrow_ctrl_m.empty() )
        //{
        //    return false;
        //}
        //
        //bool ret = false;
        //
        ////TODO - hash in string form in escrolcontrol
        //auto hash_data = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );
        //auto job_hash  = uint256_t{ "0x" + hash_data.toReadableString() };
        //for ( auto it = escrow_ctrl_m.begin(); it != escrow_ctrl_m.end(); )
        //{
        //    if ( job_hash == it->job_hash )
        //    {
        //        if ( pay )
        //        {
        //            auto transfer_transaction = std::make_shared<TransferTransaction>(
        //                destinations,
        //                std::vector<InputUTXOInfo>{ it->original_input },
        //                FillDAGStruct() );
        //            this->EnqueueTransaction( transfer_transaction );
        //            ret = true;
        //        }
        //        it = escrow_ctrl_m.erase( it );
        //    }
        //    else
        //    {
        //        ++it;
        //    }
        //}

        return true;
    }

    void TransactionManager::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult )
    {
        //Fetch Escrow

        auto maybe_tx = FetchTransaction( taskresult.subtask_results( 0 ).escrow_path() );
        if ( maybe_tx.has_error() )
        {
            std::cout << "ERROR IN FETCHING TRANSACTION" << std::endl;
        }
        std::shared_ptr<EscrowTransaction> escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( maybe_tx.value() );
        std::vector<std::string>           subtask_ids;
        std::vector<OutputDestInfo>        payout_peers;
        uint64_t peers_amount = ( escrow_tx->GetPeersCut() * uint64_t{ escrow_tx->GetAmount() } ) /
                                taskresult.subtask_results().size();
        auto remainder = escrow_tx->GetAmount();
        for ( auto &subtask : taskresult.subtask_results() )
        {
            std::cout << "Subtask Result " << subtask.subtaskid()  << "from " << subtask.node_address() << std::endl;
            subtask_ids.push_back( subtask.subtaskid() );
            payout_peers.push_back( { uint256_t{ peers_amount }, uint256_t{ subtask.node_address() } } );
            remainder -= peers_amount;
        }
        payout_peers.push_back( { uint256_t{ remainder }, escrow_tx->GetDevAddress() } );
        InputUTXOInfo escrow_utxo_input;

        escrow_utxo_input.txid_hash_  = ( base::Hash256::fromReadableString( escrow_tx->dag_st.data_hash() ) ).value();
        escrow_utxo_input.output_idx_ = 0;
        escrow_utxo_input.signature_  = "";

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            payout_peers,
            std::vector<InputUTXOInfo>{ escrow_utxo_input },
            FillDAGStruct() );
        this->EnqueueTransaction( transfer_transaction );

        //task_queue_->CompleteTask( task_id, taskresult );
    }

    uint64_t TransactionManager::GetBalance()
    {
        return account_m->GetBalance<uint64_t>();
    }

    void TransactionManager::Update()
    {
        SendTransaction();
        CheckBlockchain();
    }

    void TransactionManager::EnqueueTransaction( std::shared_ptr<IGeniusTransactions> element )
    {
        std::lock_guard<std::mutex> lock( mutex_m );
        this->account_m->nonce += 1;
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

    SGTransaction::DAGStruct TransactionManager::FillDAGStructProc()
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

    outcome::result<void> TransactionManager::SendTransaction()
    {
        std::unique_lock<std::mutex> lock( mutex_m );
        if ( out_transactions.empty() )
        {
            return std::errc::invalid_argument;
        }

        auto transaction = out_transactions.front();
        out_transactions.pop_front();

        auto transaction_path = GetTransactionPath( transaction );

        sgns::crdt::GlobalDB::Buffer data_transaction;
        data_transaction.put( transaction->SerializeByteVector() );

        BOOST_OUTCOME_TRYV2( auto &&, db_m->Put( { transaction_path }, data_transaction ) );

        m_logger->debug( "Putting on " + transaction_path +
                         " the data: " + std::string( data_transaction.toString() ) );

        BOOST_OUTCOME_TRYV2( auto &&, RecordBlock( { transaction_path } ) );
        return outcome::success();
    }

    std::string TransactionManager::GetTransactionPath( std::shared_ptr<IGeniusTransactions> element )
    {
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

        tx_key % TEST_NET_ID;

        auto transaction_path = tx_key.str() + element->GetTransactionFullPath();

        return transaction_path;
    }

    outcome::result<void> TransactionManager::RecordBlock( const std::vector<std::string> &transaction_keys )
    {
        OUTCOME_TRY( ( auto &&, last_hash ), block_storage_m->getLastFinalizedBlockHash() );
        OUTCOME_TRY( ( auto &&, last_header ), block_storage_m->getBlockHeader( last_hash ) );

        primitives::BlockHeader header( last_header );

        header.parent_hash = last_hash;

        header.number++;

        OUTCOME_TRY( ( auto &&, new_hash ), block_storage_m->putBlockHeader( header ) );

        primitives::BlockBody body;

        for ( auto &transaction : transaction_keys )
        {
            body.push_back( { base::Buffer{}.put( transaction ) } );
        }

        primitives::BlockData block_data{ new_hash, header, body };

        BOOST_OUTCOME_TRYV2( auto &&, block_storage_m->putBlockData( header.number, block_data ) );

        m_logger->debug( "Recording Block with number " + std::to_string( header.number ) );

        BOOST_OUTCOME_TRYV2( auto &&, block_storage_m->setLastFinalizedBlockHash( new_hash ) );
        return outcome::success();
    }

    outcome::result<std::vector<std::vector<uint8_t>>> TransactionManager::GetTransactionsFromBlock(
        const primitives::BlockId &block_number )
    {
        if ( auto block_body = block_storage_m->getBlockBody( block_number ); block_body )
        {
            std::vector<std::vector<uint8_t>> ret;

            for ( auto &key : block_body.value() )
            {
                if ( auto transaction = ParseTransaction( key.data.toString() ); transaction )
                {
                    ret.push_back( std::move( transaction.value() ) );
                }
            }

            return ret;
        }

        return std::errc::invalid_argument;
    }

    outcome::result<std::vector<uint8_t>> TransactionManager::ParseTransaction( std::string_view transaction_key )
    {
        if ( auto maybe_transaction_data = db_m->Get( { std::string( transaction_key ) } ); maybe_transaction_data )
        {
            auto transaction_data = maybe_transaction_data.value().toVector();
            auto maybe_dag        = IGeniusTransactions::DeSerializeDAGStruct( transaction_data );
            m_logger->debug( "Found the data, deserializing into DAG {}", transaction_key );
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
                    ParseTransferTransaction( transaction_data );
                }
                else if ( maybe_dag.value().type() == "mint" )
                {
                    m_logger->info( "Mint transaction" );
                    ParseMintTransaction( transaction_data );
                }
                else if ( maybe_dag.value().type() == "escrow" )
                {
                    m_logger->info( "Escrow transaction" );
                    ParseEscrowTransaction( transaction_data );
                }
                else if ( maybe_dag.value().type() == "process" )
                {
                    m_logger->info( "Process transaction" );
                    ParseProcessingTransaction( transaction_data );
                }
            }
            return transaction_data;
        }

        return std::errc::invalid_argument;
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::FetchTransaction(
        std::string_view transaction_key )
    {
        if ( auto maybe_transaction_data = db_m->Get( { std::string( transaction_key ) } ); maybe_transaction_data )
        {
            auto transaction_data = maybe_transaction_data.value().toVector();
            auto maybe_dag        = IGeniusTransactions::DeSerializeDAGStruct( transaction_data );
            m_logger->debug( "Found the data, deserializing into DAG {}", transaction_key );

            auto it = IGeniusTransactions::GetDeSerializers().find( "escrow" );
            if ( it == IGeniusTransactions::GetDeSerializers().end() )
            {
                m_logger->info( "Invalid transaction found. No Deserialization available" );
                return std::errc::invalid_argument;
            }
            return it->second( transaction_data );
        }
        else
        {
            return std::errc::invalid_argument;
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
            if ( retval = block_storage_m->getBlockHeader( last_block_id_m ); retval )
            {
                if ( auto DAGHeader = retval.value(); DAGHeader.number == last_block_id_m )
                {
                    if ( auto transactions = GetTransactionsFromBlock( DAGHeader.number ); transactions )
                    {
                        this->transactions.insert( this->transactions.end(),
                                                   transactions.value().begin(),
                                                   transactions.value().end() );
                        last_block_id_m++;
                    }
                    else
                    {
                        m_logger->debug( "Invalid transaction, hopefully just waiting for complete block." );
                        break;
                    }
                }
            }

        } while ( retval );
    }

    void TransactionManager::ParseTransferTransaction( const std::vector<std::uint8_t> &transaction_data )
    {
        auto tx = TransferTransaction::DeSerializeByteVector( transaction_data );

        auto dest_infos = tx->GetDstInfos();

        for ( std::uint32_t i = 0; i < dest_infos.size(); ++i )
        {
            if ( dest_infos[i].dest_address == account_m->GetAddress<uint256_t>() )
            {
                auto       hash = ( base::Hash256::fromReadableString( tx->dag_st.data_hash() ) ).value();
                GeniusUTXO new_utxo( hash, i, uint64_t{ dest_infos[i].encrypted_amount } );
                account_m->PutUTXO( new_utxo );
            }
        }

        for ( auto &input : tx->GetInputInfos() )
        {
            std::cout << "UTXO to be updated " << input.txid_hash_.toReadableString() << std::endl;
            std::cout << "UTXO output" << input.output_idx_ << std::endl;
        }

        account_m->RefreshUTXOs( tx->GetInputInfos() );
    }

    void TransactionManager::ParseMintTransaction( const std::vector<std::uint8_t> &transaction_data )
    {
        auto tx = MintTransaction::DeSerializeByteVector( transaction_data );

        if ( tx->GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
        {
            auto       hash = ( base::Hash256::fromReadableString( tx->dag_st.data_hash() ) ).value();
            GeniusUTXO new_utxo( hash, 0, tx->GetAmount() );
            account_m->PutUTXO( new_utxo );
            m_logger->info( "Created tokens, balance " + account_m->GetBalance<std::string>() );
        }
    }

    void TransactionManager::ParseEscrowTransaction( const std::vector<std::uint8_t> &transaction_data )
    {
        auto tx = EscrowTransaction::DeSerializeByteVector( transaction_data );

        if ( tx->GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
        {
            auto dest_infos = tx->GetUTXOParameters();

            if ( !dest_infos.outputs_.empty() )
            {
                //The first is the escrow, second is the change (might not happen)
                auto hash = ( base::Hash256::fromReadableString( tx->dag_st.data_hash() ) ).value();
                if ( dest_infos.outputs_.size() > 1 )
                {
                    if ( dest_infos.outputs_[1].dest_address == account_m->GetAddress<uint256_t>() )
                    {
                        GeniusUTXO new_utxo( hash, 1, uint64_t{ dest_infos.outputs_[1].encrypted_amount } );
                        account_m->PutUTXO( new_utxo );
                    }
                }
                account_m->RefreshUTXOs( tx->GetUTXOParameters().inputs_ );

            }
        }
    }

    void TransactionManager::ParseProcessingTransaction( const std::vector<std::uint8_t> &transaction_data )
    {
        //if ( escrow_ctrl_m.empty() )
        //{
        //    return;
        //}

        auto tx = ProcessingTransaction::DeSerializeByteVector( transaction_data );

        //for ( auto &ctrl : escrow_ctrl_m )
        //{
        //    if ( ctrl.job_hash == tx->GetJobHash() )
        //    {
        //        //Processing done
        //        uint64_t peers_amount = ( ctrl.peers_cut * uint64_t{ ctrl.full_amount } ) /
        //                                tx->GetNodeAddresses().size();
        //        auto remainder = uint64_t{ ctrl.full_amount };
        //
        //        std::set<std::string>       subtasks_ids;
        //        std::vector<OutputDestInfo> payout_peers;
        //        for ( auto &subtask_id : tx->GetSubtaskIDs() )
        //        {
        //            subtasks_ids.insert( subtask_id );
        //        }
        //        for ( auto &node_address : tx->GetNodeAddresses() )
        //        {
        //            payout_peers.push_back( { uint256_t{ peers_amount }, uint256_t{ node_address } } );
        //            remainder -= peers_amount;
        //        }
        //        payout_peers.push_back( { uint256_t{ remainder }, ctrl.dev_addr } );
        //        if ( processing_finished_cb_m )
        //        {
        //            processing_finished_cb_m( tx->GetTaskID(), subtasks_ids, payout_peers );
        //        }
        //    }
        //}
    }
}
