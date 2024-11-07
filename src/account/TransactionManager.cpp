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
#include "proof/TransferProof.hpp"
#include "proof/ProcessingProof.hpp"

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
        m_logger->set_level( spdlog::level::debug );
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
        bool ret = false;
        auto maybe_params =
            UTXOTxParameters::create( account_m->utxos, account_m->address.GetPublicKey(), amount, destination );

        if ( maybe_params )
        {
            auto transfer_transaction = std::make_shared<TransferTransaction>( maybe_params.value().outputs_,
                                                                               maybe_params.value().inputs_,
                                                                               FillDAGStruct() );

            TransferProof prover( account_m->GetBalance<uint64_t>(), uint64_t{ amount } );
            auto          proof_result = prover.GenerateFullProof();
            if ( proof_result.has_value() )
            {
                account_m->utxos = UTXOTxParameters::UpdateUTXOList( account_m->utxos, maybe_params.value() );
                this->EnqueueTransaction( std::make_pair( transfer_transaction, proof_result.value() ) );
                ret = true;
            }
        }

        return ret;
    }

    void TransactionManager::MintFunds( uint64_t amount )
    {
        auto          mint_transaction = std::make_shared<MintTransaction>( amount, FillDAGStruct() );
        TransferProof prover( 100000, amount );
        auto          proof_result = prover.GenerateFullProof();
        if ( proof_result.has_value() )
        {
            this->EnqueueTransaction( std::make_pair( std::move( mint_transaction ), proof_result.value() ) );
        }
    }

    bool TransactionManager::HoldEscrow( uint64_t           amount,
                                         uint64_t           num_chunks,
                                         const uint256_t   &dev_addr,
                                         float              dev_cut,
                                         const std::string &job_id )
    {
        bool ret          = false;
        auto hash_data    = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );
        auto maybe_params = UTXOTxParameters::create( account_m->utxos,
                                                      account_m->address.GetPublicKey(),
                                                      uint64_t{ amount },
                                                      uint256_t{ "0x" + hash_data.toReadableString() } );
        if ( maybe_params )
        {
            auto escrow_transaction = std::make_shared<EscrowTransaction>( maybe_params.value(),
                                                                           num_chunks,
                                                                           dev_addr,
                                                                           dev_cut,
                                                                           FillDAGStruct() );

            TransferProof prover( account_m->GetBalance<uint64_t>(), amount );
            auto          proof_result = prover.GenerateFullProof();
            if ( proof_result.has_value() )
            {
                account_m->utxos = UTXOTxParameters::UpdateUTXOList( account_m->utxos, maybe_params.value() );
                this->EnqueueTransaction( std::make_pair( escrow_transaction, proof_result.value() ) );
                ret = true;
            }
        }
        return ret;
    }

    bool TransactionManager::ReleaseEscrow( const std::string &job_id, const bool &pay )
    {
        if ( escrow_ctrl_m.empty() )
        {
            return false;
        }

        bool ret = false;

        //TODO - hash in string form in escrolcontrol
        auto hash_data = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );
        auto job_hash  = uint256_t{ "0x" + hash_data.toReadableString() };
        for ( auto it = escrow_ctrl_m.begin(); it != escrow_ctrl_m.end(); )
        {
            if ( job_hash == it->job_hash )
            {
                if ( pay )
                {
                    auto transfer_transaction =
                        std::make_shared<TransferTransaction>( it->payout_peers,
                                                               std::vector<InputUTXOInfo>{ it->original_input },
                                                               FillDAGStruct() );
                    //TODO - Create with the real balance and amount
                    TransferProof prover( uint64_t{ it->full_amount }, uint64_t{ it->full_amount } );
                    auto          proof_result = prover.GenerateFullProof();
                    if ( proof_result.has_value() )
                    {
                        this->EnqueueTransaction( std::make_pair( transfer_transaction, proof_result.value() ) );
                        ret = true;
                    }
                }
                it = escrow_ctrl_m.erase( it );
            }
            else
            {
                ++it;
            }
        }

        return ret;
    }

    void TransactionManager::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult )
    {
        for ( auto &subtask : taskresult.subtask_results() )
        {
            auto process_transaction =
                std::make_shared<ProcessingTransaction>( task_id, subtask.subtaskid(), FillDAGStruct() );

            ProcessingProof prover( task_id );
            auto            proof_result = prover.GenerateFullProof();
            if ( proof_result.has_value() )
            {
                //TODO - Check what we might have to prove here
                this->EnqueueTransaction( std::make_pair( process_transaction, std::vector<uint8_t>{} ) );
            }
        }
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

    void TransactionManager::EnqueueTransaction( TransactionPair element )
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
        if ( out_transactions.empty() )
        {
            return;
        }

        auto [transaction, proof] = out_transactions.front();
        out_transactions.pop_front();
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };
        tx_key % TEST_NET_ID;

        auto transaction_path = tx_key.str() + transaction->GetTransactionFullPath();
        auto proof_path       = tx_key.str() + transaction->GetProofFullPath();

        sgns::crdt::GlobalDB::Buffer data_transaction;
        data_transaction.put( transaction->SerializeByteVector() );

        db_m->Put( { transaction_path }, data_transaction );
        account_m->nonce++;

        sgns::crdt::GlobalDB::Buffer proof_transaction;

        //std::cout << " creating with proof with size  " <<  proof_vector.size() << std::endl;
        proof_transaction.put( proof );
        db_m->Put( { proof_path }, proof_transaction );

        auto maybe_last_hash   = block_storage_m->getLastFinalizedBlockHash();
        auto maybe_last_header = block_storage_m->getBlockHeader( maybe_last_hash.value() );

        primitives::BlockHeader header( maybe_last_header.value() );

        header.parent_hash = maybe_last_hash.value();

        header.number++;

        auto new_hash = block_storage_m->putBlockHeader( header );

        primitives::BlockBody body{ { base::Buffer{}.put( transaction_path ) } };

        primitives::BlockData block_data{ new_hash.value(), header, body };

        block_storage_m->putBlockData( header.number, block_data );

        m_logger->debug( "Putting on " + transaction_path +
                         " the data: " + std::string( data_transaction.toString() ) );
        m_logger->debug( "Recording Block with number " + std::to_string( header.number ) );
        block_storage_m->setLastFinalizedBlockHash( new_hash.value() );
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
                //TODO Verify the Snark
                const std::string &string_src_address = maybe_dag.value().source_addr();
                if ( !VerifyTransaction( string_src_address, maybe_dag.value().nonce() ) )
                {
                    m_logger->info( "Invalid PROOF" );
                    return std::errc::invalid_argument;
                }
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

    bool TransactionManager::VerifyTransaction( const std::string &string_src_address, const uint64_t nonce )
    {
        bool ret = false;

        boost::format proof_key{ std::string( TRANSACTION_BASE_FORMAT ) + string_src_address + "/tx/proof" + "/%llu" };
        proof_key % TEST_NET_ID;
        proof_key % nonce;

        std::cout << " proof_key.str() the proof in " << proof_key.str() << std::endl;

        auto maybe_proof_data = db_m->Get( { proof_key.str() } );

        if ( maybe_proof_data.has_value() )
        {
            auto value_vector = maybe_proof_data.value().toVector();
            std::cout << " it has value with size  " << value_vector.size() << std::endl;
            auto maybe_proof_validity = IBasicProof::VerifyFullProof( maybe_proof_data.value().toVector() );
            if ( maybe_proof_validity.has_value() )
            {
                ret = maybe_proof_validity.value();
            }
        }
        return ret;
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
                    else{
                        m_logger->debug( "Invalid transaction, hopefully just waiting for complete block.");
                        break;
                    }
                }
            }

        } while ( retval );
    }

    void TransactionManager::ParseTransferTransaction( const std::vector<std::uint8_t> &transaction_data )
    {
        TransferTransaction tx = TransferTransaction::DeSerializeByteVector( transaction_data );

        auto dest_infos = tx.GetDstInfos();

        for ( std::uint32_t i = 0; i < dest_infos.size(); ++i )
        {
            if ( dest_infos[i].dest_address == account_m->GetAddress<uint256_t>() )
            {
                auto       hash = ( base::Hash256::fromReadableString( tx.dag_st.data_hash() ) ).value();
                GeniusUTXO new_utxo( hash, i, uint64_t{ dest_infos[i].encrypted_amount } );
                account_m->PutUTXO( new_utxo );
            }
        }

        for ( auto &tx : tx.GetInputInfos() )
        {
            std::cout << "UTXO to be updated " << tx.txid_hash_.toReadableString() << std::endl;
            std::cout << "UTXO output" << tx.output_idx_ << std::endl;
        }

        account_m->RefreshUTXOs( tx.GetInputInfos() );
    }

    void TransactionManager::ParseMintTransaction( const std::vector<std::uint8_t> &transaction_data )
    {
        MintTransaction tx = MintTransaction::DeSerializeByteVector( transaction_data );

        if ( tx.GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
        {
            auto       hash = ( base::Hash256::fromReadableString( tx.dag_st.data_hash() ) ).value();
            GeniusUTXO new_utxo( hash, 0, tx.GetAmount() );
            account_m->PutUTXO( new_utxo );
            m_logger->info( "Created tokens, balance " + account_m->GetBalance<std::string>() );
        }
    }

    void TransactionManager::ParseEscrowTransaction( const std::vector<std::uint8_t> &transaction_data )
    {
        EscrowTransaction tx = EscrowTransaction::DeSerializeByteVector( transaction_data );

        if ( tx.GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
        {
            auto dest_infos = tx.GetUTXOParameters();

            if ( !dest_infos.outputs_.empty() )
            {
                //The first is the escrow, second is the change (might not happen)
                auto hash = ( base::Hash256::fromReadableString( tx.dag_st.data_hash() ) ).value();
                if ( dest_infos.outputs_.size() > 1 )
                {
                    if ( dest_infos.outputs_[1].dest_address == account_m->GetAddress<uint256_t>() )
                    {
                        GeniusUTXO new_utxo( hash, 1, uint64_t{ dest_infos.outputs_[1].encrypted_amount } );
                        account_m->PutUTXO( new_utxo );
                    }
                }
                account_m->RefreshUTXOs( tx.GetUTXOParameters().inputs_ );
                InputUTXOInfo escrow_utxo;

                escrow_utxo.txid_hash_  = hash;
                escrow_utxo.output_idx_ = 0;
                escrow_utxo.signature_  = ""; //TODO - Insert signature
                EscrowCtrl ctrl( tx.GetDestAddress(),
                                 tx.GetDestCut(),
                                 dest_infos.outputs_[0].dest_address,
                                 dest_infos.outputs_[0].encrypted_amount,
                                 tx.GetNumChunks(),
                                 escrow_utxo );
                escrow_ctrl_m.push_back( ctrl );
            }
        }
    }

    void TransactionManager::ParseProcessingTransaction( const std::vector<std::uint8_t> &transaction_data )
    {
        if ( escrow_ctrl_m.empty() )
        {
            return;
        }

        ProcessingTransaction tx = ProcessingTransaction::DeSerializeByteVector( transaction_data );

        for ( auto &ctrl : escrow_ctrl_m )
        {
            if ( ctrl.job_hash == tx.GetJobHash() )
            {
                ctrl.subtask_info[tx.GetSubtaskID()] = tx.GetSrcAddress<uint256_t>();

                if ( ctrl.subtask_info.size() == ctrl.num_subtasks )
                {
                    //Processing done
                    uint64_t peers_amount = ( ctrl.dev_cut * uint64_t{ ctrl.full_amount } ) / ctrl.subtask_info.size();
                    auto     remainder    = uint64_t{ ctrl.full_amount };

                    std::set<std::string> subtasks_ids;
                    for ( auto &pair : ctrl.subtask_info )
                    {
                        ctrl.payout_peers.push_back( { uint256_t{ peers_amount }, pair.second } );
                        remainder -= peers_amount;
                        subtasks_ids.insert( pair.first );
                    }
                    ctrl.payout_peers.push_back( { uint256_t{ remainder }, ctrl.dev_addr } );
                    if ( processing_finished_cb_m )
                    {
                        processing_finished_cb_m( tx.GetTaskID(), subtasks_ids);
                    }
                }
            }
        }
    }
}
