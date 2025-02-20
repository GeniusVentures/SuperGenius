/**
 * @file       TransactionManager.cpp
 * @brief      
 * @date       2024-04-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <boost/range/concepts.hpp>
#include <nil/crypto3/algebra/marshalling.hpp>
#include "account/TransactionManager.hpp"

#include <stdexcept>
#include <utility>
#include <algorithm>

#include <ProofSystem/EthereumKeyPairParams.hpp>

#include "TransferTransaction.hpp"
#include "MintTransaction.hpp"
#include "ProcessingTransaction.hpp"
#include "EscrowTransaction.hpp"
#include "UTXOTxParameters.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "base/util.hpp"
#include "base/fixed_point.hpp"

#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>
#ifdef _PROOF_ENABLED
#include "proof/TransferProof.hpp"
#include "proof/ProcessingProof.hpp"
#endif

namespace sgns
{
    TransactionManager::TransactionManager( std::shared_ptr<crdt::GlobalDB>                  processing_db,
                                            std::shared_ptr<boost::asio::io_context>         ctx,
                                            std::shared_ptr<GeniusAccount>                   account,
                                            std::shared_ptr<crypto::Hasher>                  hasher,
                                            std::string                                      base_path,
                                            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub,
                                            std::shared_ptr<upnp::UPNP>                      upnp,
                                            uint16_t                                         base_port ) :
        processing_db_m( std::move( processing_db ) ),
        pubsub_m( std::move( pubsub ) ),
        base_path_m( std::move( base_path ) ),
        ctx_m( std::move( ctx ) ),
        account_m( std::move( account ) ),
        hasher_m( std::move( hasher ) ),
        upnp_m( std::move( upnp ) ),
        base_port_m( base_port + 1 ),
        timer_m( std::make_shared<boost::asio::steady_timer>( *ctx_m, boost::asio::chrono::milliseconds( 300 ) ) )

    {
        m_logger->set_level( spdlog::level::debug );
        m_logger->info( "Initializing values by reading whole blockchain" );

        outgoing_db_m = std::make_shared<crdt::GlobalDB>(
            ctx_m,
            ( boost::format( base_path_m + "_out" ) ).str(),
            base_port_m,
            std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_m, account_m->GetAddress() + "out" ) );

        used_ports_m.insert( base_port_m );
        base_port_m++;
        if ( !outgoing_db_m->Init( crdt::CrdtOptions::DefaultOptions() ).has_value() )
        {
            throw std::runtime_error( "Could not start Outgoing GlobalDB" );
        }

        incoming_db_m = std::make_shared<crdt::GlobalDB>(
            ctx_m,
            ( boost::format( base_path_m + "_in" ) ).str(),
            base_port_m,
            std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_m, account_m->GetAddress() + "in" ) );

        if ( !incoming_db_m->Init( crdt::CrdtOptions::DefaultOptions() ).has_value() )
        {
            throw std::runtime_error( "Could not start Incoming GlobalDB" );
        }
        used_ports_m.insert( base_port_m );
        base_port_m++;

        RefreshPorts();
    }

    void TransactionManager::Start()
    {
        CheckIncoming();
        CheckOutgoing();

        task_m = [this]()
        {
            this->Update();
            this->timer_m->expires_after( boost::asio::chrono::milliseconds( 300 ) );

            this->timer_m->async_wait(
                [weak_instance = weak_from_this()]( const boost::system::error_code & )
                {
                    if ( auto instance = weak_instance.lock() ) // Ensure instance is still alive
                    {
                        instance->ctx_m->post( instance->task_m );
                    }
                } );
        };

        ctx_m->post( task_m );
    }

    void TransactionManager::PrintAccountInfo()
    {
        std::cout << "Account Address: " << account_m->GetAddress() << std::endl;
        std::cout << "Balance: " << account_m->GetBalance<std::string>() << std::endl;
        std::cout << "Token Type: " << account_m->GetToken() << std::endl;
        std::cout << "Nonce: " << account_m->GetNonce() << std::endl;
    }

    const GeniusAccount &TransactionManager::GetAccount() const
    {
        return *account_m;
    }

    bool TransactionManager::TransferFunds( uint64_t amount, const std::string &destination )
    {
        bool ret          = false;
        auto maybe_params = UTXOTxParameters::create( account_m->utxos, account_m->GetAddress(), amount, destination );

        if ( maybe_params )
        {
            auto transfer_transaction = std::make_shared<TransferTransaction>(
                TransferTransaction::New( maybe_params.value().outputs_,
                                          maybe_params.value().inputs_,
                                          FillDAGStruct() ) );
            std::optional<std::vector<uint8_t>> maybe_proof;
#ifdef _PROOF_ENABLED
            TransferProof prover( static_cast<uint64_t>( account_m->GetBalance<uint64_t>() ),
                                  static_cast<uint64_t>( amount ) );
            auto          proof_result = prover.GenerateFullProof();
            if ( proof_result.has_value() )
            {
                maybe_proof = proof_result.value();
                ret         = true;
            }
#else
            ret = true;
#endif
            if ( ret == true )
            {
                account_m->utxos = UTXOTxParameters::UpdateUTXOList( account_m->utxos, maybe_params.value() );
                this->EnqueueTransaction( std::make_pair( transfer_transaction, maybe_proof ) );
            }
        }

        return ret;
    }

    void TransactionManager::MintFunds( uint64_t    amount,
                                        std::string transaction_hash,
                                        std::string chainid,
                                        std::string tokenid )
    {
        auto mint_transaction = std::make_shared<MintTransaction>(
            MintTransaction::New( amount,
                                  std::move( chainid ),
                                  std::move( tokenid ),
                                  FillDAGStruct( std::move( transaction_hash ) ) ) );
        std::optional<std::vector<uint8_t>> maybe_proof;
#ifdef _PROOF_ENABLED
        TransferProof prover( 1000000000000000,
                              static_cast<uint64_t>( amount ) ); //Mint max 1000000 gnus per transaction
        auto          proof_result = prover.GenerateFullProof();
        if ( proof_result.has_value() )
        {
            maybe_proof = proof_result.value();
        }
#endif
        this->EnqueueTransaction( std::make_pair( std::move( mint_transaction ), maybe_proof ) );
    }

    outcome::result<EscrowDataPair> TransactionManager::HoldEscrow( uint64_t           amount,
                                                                    const std::string &dev_addr,
                                                                    uint64_t           peers_cut,
                                                                    const std::string &job_id )
    {
        bool ret       = false;
        auto hash_data = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );

        OUTCOME_TRY( ( auto &&, params ),
                     UTXOTxParameters::create( account_m->utxos,
                                               account_m->GetAddress(),
                                               amount,
                                               "0x" + hash_data.toReadableString() ) );

        account_m->utxos        = UTXOTxParameters::UpdateUTXOList( account_m->utxos, params );
        auto escrow_transaction = std::make_shared<EscrowTransaction>(
            EscrowTransaction::New( params, amount, dev_addr, peers_cut, FillDAGStruct() ) );

        std::optional<std::vector<uint8_t>> maybe_proof;
#ifdef _PROOF_ENABLED

        TransferProof prover( static_cast<uint64_t>( account_m->GetBalance<uint64_t>() ),
                              static_cast<uint64_t>( amount ) );
        OUTCOME_TRY( ( auto &&, proof_result ), prover.GenerateFullProof() );
        maybe_proof = proof_result;

#endif
        account_m->utxos = UTXOTxParameters::UpdateUTXOList( account_m->utxos, params );
        this->EnqueueTransaction( std::make_pair( escrow_transaction, maybe_proof ) );
        m_logger->debug( "Holding escrow to 0x{} wih the amount {}", hash_data.toReadableString(), amount );

        sgns::crdt::GlobalDB::Buffer data_transaction;
        data_transaction.put( escrow_transaction->SerializeByteVector() );
        return std::make_pair( "0x" + hash_data.toReadableString(), std::move( data_transaction ) );
    }

    outcome::result<void> TransactionManager::PayEscrow( const std::string              &escrow_path,
                                                         const SGProcessing::TaskResult &taskresult )
    {
        if ( taskresult.subtask_results().size() == 0 )
        {
            m_logger->debug( "No result found on escrow " + escrow_path );
            return outcome::failure( boost::system::error_code{} );
        }
        if ( escrow_path.empty() )
        {
            m_logger->debug( "Escrow path empty " );
            return outcome::failure( boost::system::error_code{} );
        }
        m_logger->debug( "Fetching escrow from processing DB at " + escrow_path );
        OUTCOME_TRY( ( auto &&, transaction ), FetchTransaction( processing_db_m, escrow_path ) );

        std::shared_ptr<EscrowTransaction> escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( transaction );
        std::vector<std::string>           subtask_ids;
        std::vector<OutputDestInfo>        payout_peers;

        auto mult_result = sgns::fixed_point::multiply( escrow_tx->GetAmount(), escrow_tx->GetPeersCut() );
        //TODO: check fail here, maybe if peer cut is greater than one to...
        uint64_t peers_amount = mult_result.value() / static_cast<uint64_t>( taskresult.subtask_results().size() );
        auto     remainder    = escrow_tx->GetAmount();
        for ( auto &subtask : taskresult.subtask_results() )
        {
            std::cout << "Subtask Result " << subtask.subtaskid() << "from " << subtask.node_address() << std::endl;
            m_logger->debug( "Paying out {} ", peers_amount );
            subtask_ids.push_back( subtask.subtaskid() );
            payout_peers.push_back( { peers_amount, subtask.node_address() } );
            remainder -= peers_amount;
        }
        m_logger->debug( "Sending to dev {}", remainder );
        payout_peers.push_back( { remainder, escrow_tx->GetDevAddress() } );
        InputUTXOInfo escrow_utxo_input;

        escrow_utxo_input.txid_hash_  = ( base::Hash256::fromReadableString( escrow_tx->dag_st.data_hash() ) ).value();
        escrow_utxo_input.output_idx_ = 0;
        escrow_utxo_input.signature_  = ""; //TODO - Signature

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            TransferTransaction::New( payout_peers,
                                      std::vector<InputUTXOInfo>{ escrow_utxo_input },
                                      FillDAGStruct() ) );

        std::optional<std::vector<uint8_t>> maybe_proof;
#ifdef _PROOF_ENABLED
        //TODO - Create with the real balance and amount
        TransferProof prover( 1, 1 );

        OUTCOME_TRY( ( auto &&, proof_result ), prover.GenerateFullProof() );
        maybe_proof = proof_result;
#endif

        this->EnqueueTransaction( std::make_pair( transfer_transaction, maybe_proof ) );

        return outcome::success();
    }

    uint64_t TransactionManager::GetBalance()
    {
        return account_m->GetBalance<uint64_t>();
    }

    void TransactionManager::Update()
    {
        SendTransaction();
        CheckIncoming();
        //CheckOutgoing();
        static auto start_time = std::chrono::steady_clock::now();
        if ( std::chrono::steady_clock::now() - start_time > std::chrono::minutes( 60 ) )
        {
            start_time = std::chrono::steady_clock::now();
            RefreshPorts();
        }
    }

    void TransactionManager::EnqueueTransaction( TransactionPair element )
    {
        std::lock_guard<std::mutex> lock( mutex_m );
        tx_queue_m.emplace_back( std::move( element ) );
    }

    //TODO - Fill hash stuff on DAGStruct
    SGTransaction::DAGStruct TransactionManager::FillDAGStruct( std::string transaction_hash ) const
    {
        SGTransaction::DAGStruct dag;
        auto                     timestamp = std::chrono::system_clock::now();

        dag.set_previous_hash( transaction_hash );
        dag.set_nonce( ++account_m->nonce );
        dag.set_source_addr( account_m->GetAddress() );
        dag.set_timestamp( timestamp.time_since_epoch().count() );
        dag.set_uncle_hash( "" );
        dag.set_data_hash( "" ); //filled by transaction class

        return dag;
    }

    outcome::result<void> TransactionManager::SendTransaction()
    {
        std::unique_lock lock( mutex_m );
        if ( tx_queue_m.empty() )
        {
            return std::errc::invalid_argument;
        }

        auto [transaction, maybe_proof] = tx_queue_m.front();

        transaction->dag_st.set_nonce( account_m->nonce );
        transaction->dag_st.clear_signature();

        auto signature = MakeSignature( transaction->dag_st );

        transaction->dag_st.set_signature( signature.data(), signature.size() );

        sgns::crdt::HierarchicalKey  tx_key( GetTransactionPath( *transaction ) );
        sgns::crdt::GlobalDB::Buffer data_transaction;

        m_logger->debug( "Recording the transaction on " + tx_key.GetKey() );

        auto crdt_transaction = outgoing_db_m->BeginTransaction();

        data_transaction.put( transaction->SerializeByteVector() );
        BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

        if ( maybe_proof )
        {
            sgns::crdt::HierarchicalKey  proof_key( GetTransactionProofPath( *transaction ) );
            sgns::crdt::GlobalDB::Buffer proof_transaction;

            auto proof = maybe_proof.value();
            m_logger->debug( "Recording the proof on " + proof_key.GetKey() );

            proof_transaction.put( proof );
            BOOST_OUTCOME_TRYV2( auto &&,
                                 crdt_transaction->Put( std::move( proof_key ), std::move( proof_transaction ) ) );
        }

        if ( transaction->GetType() == "transfer" )
        {
            m_logger->debug( "Notifying receiving peers of transfers" );
            BOOST_OUTCOME_TRYV2( auto &&, NotifyDestinationOfTransfer( transaction, maybe_proof ) );
        }
        BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Commit() );

        tx_queue_m.pop_front();

        BOOST_OUTCOME_TRYV2( auto &&, ParseTransaction( transaction ) );

        return outcome::success();
    }

    std::string TransactionManager::GetTransactionPath( IGeniusTransactions &element )
    {
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

        tx_key % TEST_NET_ID;

        auto transaction_path = tx_key.str() + element.GetTransactionFullPath();

        return transaction_path;
    }

    std::string TransactionManager::GetTransactionProofPath( IGeniusTransactions &element )
    {
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

        tx_key % TEST_NET_ID;

        auto proof_path = tx_key.str() + element.GetProofFullPath();

        return proof_path;
    }

    std::string TransactionManager::GetNotificationPath( const std::string &destination )
    {
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

        tx_key % TEST_NET_ID;

        auto notification_path = tx_key.str() + destination + "/notify/";

        return notification_path;
    }

    outcome::result<void> TransactionManager::ParseTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto it = transaction_parsers.find( tx->GetType() );
        if ( it == transaction_parsers.end() )
        {
            m_logger->info( "No Parser Available" );
            return std::errc::invalid_argument;
        }

        return ( this->*( it->second ) )( tx );
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::FetchTransaction(
        const std::shared_ptr<crdt::GlobalDB> &db,
        std::string_view                       transaction_key )
    {
        OUTCOME_TRY( ( auto &&, transaction_data ), db->Get( { std::string( transaction_key ) } ) );

        auto transaction_data_vector = transaction_data.toVector();

        OUTCOME_TRY( ( auto &&, dag ), IGeniusTransactions::DeSerializeDAGStruct( transaction_data_vector ) );

        m_logger->debug( "Found the data, deserializing into DAG {}", transaction_key );

        auto it = IGeniusTransactions::GetDeSerializers().find( dag.type() );
        if ( it == IGeniusTransactions::GetDeSerializers().end() )
        {
            m_logger->info( "Invalid transaction found. No Deserialization available for type {}", dag.type() );
            return std::errc::invalid_argument;
        }
        return it->second( transaction_data_vector );
    }

    outcome::result<bool> TransactionManager::CheckProof( const std::shared_ptr<IGeniusTransactions> &tx )
    {
#ifdef _PROOF_ENABLED
        m_logger->debug( "Checking the proof in {}",
                         GetNotificationPath( account_m->GetAddress() ) + "proof/" + GetTransactionProofPath( *tx ) );
        OUTCOME_TRY( ( auto &&, proof_data ),
                     incoming_db_m->Get( { GetNotificationPath( account_m->GetAddress() ) + "proof/" +
                                           GetTransactionProofPath( *tx ) } ) );

        auto proof_data_vector = proof_data.toVector();

        m_logger->debug( "Proof data acquired. Verifying..." );
        //std::cout << " it has value with size  " << proof_data.size() << std::endl;
        return IBasicProof::VerifyFullProof( proof_data_vector );
#else
        return true;
#endif
    }

    outcome::result<void> TransactionManager::CheckIncoming()
    {
        auto transaction_paths = GetNotificationPath( account_m->GetAddress() ) + "tx/";
        m_logger->trace( "Probing incoming transactions on " + transaction_paths );
        OUTCOME_TRY( ( auto &&, transaction_list ), incoming_db_m->QueryKeyValues( transaction_paths ) );

        m_logger->trace( "Incoming transaction list grabbed from CRDT" );

        //m_logger->info( "Number of tasks in Queue: {}", queryTasks.size() );
        for ( const auto &element : transaction_list )
        {
            auto transaction_key = incoming_db_m->KeyToString( element.first );
            if ( !transaction_key.has_value() )
            {
                m_logger->debug( "Unable to convert a key to string" );
                continue;
            }
            if ( incoming_tx_processed_m.find( { transaction_key.value() } ) != incoming_tx_processed_m.end() )
            {
                m_logger->trace( "Transaction already processed: " + transaction_key.value() );
                continue;
            }

            m_logger->debug( "Finding incoming transaction: " + transaction_key.value() );
            auto maybe_transaction = FetchTransaction( incoming_db_m, { transaction_key.value() } );
            if ( !maybe_transaction.has_value() )
            {
                m_logger->debug( "Can't fetch transaction" );
                continue;
            }
#ifdef _PROOF_ENABLED
            auto maybe_proof = CheckProof( maybe_transaction.value() );
            if ( !maybe_proof.has_value() )
            {
                m_logger->info( "Invalid PROOF" );
                continue;
            }
#endif
            if ( !CheckDAGStructSignature( maybe_transaction.value()->dag_st ) )
            {
                m_logger->error( "Could not validate signature of transaction from {}",
                                 maybe_transaction.value()->dag_st.source_addr() );
                continue;
            }
            auto maybe_parsed = ParseTransaction( maybe_transaction.value() );
            if ( maybe_parsed.has_error() )
            {
                m_logger->debug( "Can't parse the transaction" );
                continue;
            }
            incoming_tx_processed_m[{ transaction_key.value() }] = maybe_transaction.value()->SerializeByteVector();
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::CheckOutgoing()
    {
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

        tx_key % TEST_NET_ID;

        auto transaction_paths = tx_key.str() + account_m->GetAddress() + "/tx";
        m_logger->trace( "Probing transactions on " + transaction_paths );
        OUTCOME_TRY( ( auto &&, transaction_list ), outgoing_db_m->QueryKeyValues( transaction_paths ) );

        m_logger->trace( "Transaction list grabbed from CRDT" );

        //m_logger->info( "Number of tasks in Queue: {}", queryTasks.size() );
        for ( const auto &element : transaction_list )
        {
            auto transaction_key = outgoing_db_m->KeyToString( element.first );
            if ( !transaction_key.has_value() )
            {
                m_logger->debug( "Unable to convert a key to string" );
                continue;
            }
            if ( outgoing_tx_processed_m.find( { transaction_key.value() } ) != outgoing_tx_processed_m.end() )
            {
                m_logger->trace( "Transaction already processed: " + transaction_key.value() );
                continue;
            }

            auto maybe_transaction = FetchTransaction( outgoing_db_m, { transaction_key.value() } );
            if ( !maybe_transaction.has_value() )
            {
                m_logger->debug( "Can't fetch transaction" );
                continue;
            }
            m_logger->debug( "Transaction fetched on " + transaction_key.value() );
            auto maybe_parsed = ParseTransaction( maybe_transaction.value() );
            if ( maybe_parsed.has_error() )
            {
                m_logger->debug( "Can't parse the transaction" );
                continue;
            }
            m_logger->debug( "Transaction parsed " + transaction_key.value() );

            account_m->nonce = std::max( account_m->nonce, maybe_transaction.value()->dag_st.nonce() );

            outgoing_tx_processed_m[{ transaction_key.value() }] = maybe_transaction.value()->SerializeByteVector();
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseTransferTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx );
        auto dest_infos  = transfer_tx->GetDstInfos();

        for ( std::uint32_t i = 0; i < dest_infos.size(); ++i )
        {
            if ( dest_infos[i].dest_address == account_m->GetAddress() )
            {
                auto       hash = ( base::Hash256::fromReadableString( transfer_tx->dag_st.data_hash() ) ).value();
                GeniusUTXO new_utxo( hash, i, dest_infos[i].encrypted_amount );
                account_m->PutUTXO( new_utxo );
            }
        }

        for ( auto &input : transfer_tx->GetInputInfos() )
        {
            m_logger->trace( "UTXO to be updated {}", input.txid_hash_.toReadableString() );
            m_logger->trace( "UTXO output {}", input.output_idx_ );
        }

        account_m->RefreshUTXOs( transfer_tx->GetInputInfos() );
        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );
        if ( mint_tx->GetSrcAddress() == account_m->GetAddress() )
        {
            auto       hash = ( base::Hash256::fromReadableString( mint_tx->dag_st.data_hash() ) ).value();
            GeniusUTXO new_utxo( hash, 0, mint_tx->GetAmount() );
            account_m->PutUTXO( new_utxo );
            m_logger->info( "Created tokens, balance " + account_m->GetBalance<std::string>() );
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );

        if ( escrow_tx->GetSrcAddress() == account_m->GetAddress() )
        {
            auto dest_infos = escrow_tx->GetUTXOParameters();

            if ( !dest_infos.outputs_.empty() )
            {
                //The first is the escrow, second is the change (might not happen)
                auto hash = ( base::Hash256::fromReadableString( escrow_tx->dag_st.data_hash() ) ).value();
                if ( dest_infos.outputs_.size() > 1 )
                {
                    if ( dest_infos.outputs_[1].dest_address == account_m->GetAddress() )
                    {
                        GeniusUTXO new_utxo( hash, 1, dest_infos.outputs_[1].encrypted_amount );
                        account_m->PutUTXO( new_utxo );
                    }
                }
                account_m->RefreshUTXOs( escrow_tx->GetUTXOParameters().inputs_ );
            }
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::NotifyDestinationOfTransfer(
        const std::shared_ptr<IGeniusTransactions> &tx,
        std::optional<std::vector<uint8_t>>         proof )
    {
        auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx );
        auto dest_infos  = transfer_tx->GetDstInfos();

        for ( const auto &dest_info : dest_infos )
        {
            if ( dest_info.dest_address == account_m->GetAddress() )
            {
                continue;
            }

            m_logger->debug( "Sending notification to " + dest_info.dest_address );
            std::shared_ptr<crdt::GlobalDB> destination_db;
            auto                            destination_db_it = destination_dbs_m.find( dest_info.dest_address );
            if ( destination_db_it == destination_dbs_m.end() )
            {
                m_logger->debug( "Port to sync  " + std::to_string( base_port_m ) );

                destination_db = std::make_shared<crdt::GlobalDB>(
                    ctx_m,
                    ( boost::format( base_path_m + "_out/" + dest_info.dest_address ) ).str(),
                    base_port_m,
                    std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_m, dest_info.dest_address + "in" ) );
                if ( !destination_db->Init( crdt::CrdtOptions::DefaultOptions() ).has_value() )
                {
                    throw std::runtime_error( "Could not start Destination GlobalDB" );
                }
                destination_dbs_m[dest_info.dest_address] = destination_db;
                used_ports_m.insert( base_port_m );
                base_port_m++;
                //RefreshPorts();
            }
            else
            {
                destination_db = destination_db_it->second;
            }

            auto                         crdt_transaction = destination_db->BeginTransaction();
            sgns::crdt::GlobalDB::Buffer data_transaction;
            sgns::crdt::HierarchicalKey  tx_key( GetNotificationPath( dest_info.dest_address ) + "tx/" +
                                                GetTransactionPath( *tx ) );

            data_transaction.put( tx->SerializeByteVector() );

            m_logger->debug( "Putting replicate transaction in {}", tx_key.GetKey() );
            BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );
            if ( proof )
            {
                sgns::crdt::HierarchicalKey  proof_key( GetNotificationPath( dest_info.dest_address ) + "proof/" +
                                                       GetTransactionProofPath( *tx ) );
                sgns::crdt::GlobalDB::Buffer proof_data;

                proof_data.put( proof.value() );
                m_logger->debug( "Putting replicate PROOF in {}", proof_key.GetKey() );
                BOOST_OUTCOME_TRYV2( auto &&,
                                     crdt_transaction->Put( std::move( proof_key ), std::move( proof_data ) ) );
            }
            BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Commit() );
        }

        return outcome::success();
    }

    std::vector<std::vector<uint8_t>> TransactionManager::GetOutTransactions() const
    {
        std::vector<std::vector<std::uint8_t>> result;
        result.reserve( outgoing_tx_processed_m.size() );
        for ( const auto &[key, value] : outgoing_tx_processed_m )
        {
            result.push_back( value );
        }
        return result;
    }

    std::vector<std::vector<uint8_t>> TransactionManager::GetInTransactions() const
    {
        std::vector<std::vector<std::uint8_t>> result;
        result.reserve( incoming_tx_processed_m.size() );
        for ( const auto &[key, value] : incoming_tx_processed_m )
        {
            result.push_back( value );
        }
        return result;
    }

    void TransactionManager::RefreshPorts()
    {
        if ( upnp_m->GetIGD() )
        {
            for ( auto &port : used_ports_m )
            {
                upnp_m->OpenPort( port, port, "TCP", 3600 );
            }
        }
    }

    std::vector<uint8_t> TransactionManager::MakeSignature( SGTransaction::DAGStruct dag_st ) const
    {
        dag_st.clear_signature();
        auto                 size = dag_st.ByteSizeLong();
        std::vector<uint8_t> serialized( size );
        dag_st.SerializeToArray( serialized.data(), size );

        std::array<uint8_t, 32> hashed = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( serialized );

        ethereum::signature_type  signature = nil::crypto3::sign( hashed,
                                                                 this->account_m->eth_address->get_private_key() );
        std::vector<std::uint8_t> signed_vector( 64 );

        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<0>( signature ),
                                                  signed_vector.begin(),
                                                  signed_vector.begin() + 32 );
        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<1>( signature ),
                                                  signed_vector.begin() + 32,
                                                  signed_vector.end() );

        nil::crypto3::multiprecision::cpp_int r;
        nil::crypto3::multiprecision::cpp_int s;

        import_bits( r, signed_vector.cbegin(), signed_vector.cbegin() + 32 );
        import_bits( s, signed_vector.cbegin() + 32, signed_vector.cbegin() + 64 );

        return signed_vector;
    }

    bool TransactionManager::CheckDAGStructSignature( SGTransaction::DAGStruct dag_st ) const
    {
        auto                 str_signature = dag_st.signature();
        std::vector<uint8_t> vec_sig( str_signature.cbegin(), str_signature.cend() );

        dag_st.clear_signature();
        auto                 size = dag_st.ByteSizeLong();
        std::vector<uint8_t> serialized( size );
        dag_st.SerializeToArray( serialized.data(), size );

        std::array<uint8_t, 32> hashed = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( serialized );

        auto [r_success, r] = nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
            vec_sig.cbegin(),
            vec_sig.cbegin() + 32 );

        if ( !r_success )
        {
            return false;
        }

        auto [s_success, s] = nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
            vec_sig.cbegin() + 32,
            vec_sig.cbegin() + 64 );

        if ( !s_success )
        {
            return false;
        }

        ethereum::signature_type sig( r, s );

        auto eth_pubkey = ethereum::EthereumKeyGenerator::BuildPublicKey( dag_st.source_addr() );

        return nil::crypto3::verify( hashed, sig, eth_pubkey );
    }
}
