/**
 * @file       TransactionManager.cpp
 * @brief      
 * @date       2024-04-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/TransactionManager.hpp"

#include <utility>
#include <algorithm>

#include "account/TransferTransaction.hpp"
#include "account/MintTransaction.hpp"
#include "account/ProcessingTransaction.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/UTXOTxParameters.hpp"
#include "base/util.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "outcome/outcome.hpp"
#include "primitives/block.hpp"

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
        timer_m( std::make_shared<boost::asio::steady_timer>( *ctx_m, boost::asio::chrono::milliseconds( 300 ) ) ),
        hasher_m( std::move( hasher ) ),
        upnp_m( std::move( upnp ) ),
        base_port_m( base_port + 1 )

    {
        m_logger->set_level( spdlog::level::off );
        m_logger->info( "Initializing values by reading whole blockchain" );

        outgoing_db_m = std::make_shared<crdt::GlobalDB>(
            ctx_m,
            ( boost::format( base_path_m + "_out" ) ).str(),
            base_port_m,
            std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_m,
                                                              account_m->GetAddress<std::string>() + "out" ) );

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
            std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_m, account_m->GetAddress<std::string>() + "in" ) );

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

    bool TransactionManager::TransferFunds( double amount, const uint256_t &destination )
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

    void TransactionManager::MintFunds( double      amount,
                                        std::string transaction_hash,
                                        std::string chainid,
                                        std::string tokenid )
    {
        auto mint_transaction = std::make_shared<MintTransaction>( RoundTo5Digits( amount ),
                                                                   chainid,
                                                                   tokenid,
                                                                   FillDAGStruct( transaction_hash ) );
        this->EnqueueTransaction( std::move( mint_transaction ) );
    }

    outcome::result<std::string> TransactionManager::HoldEscrow( double             amount,
                                                                 const uint256_t   &dev_addr,
                                                                 float              peers_cut,
                                                                 const std::string &job_id )
    {
        bool ret       = false;
        auto hash_data = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );

        OUTCOME_TRY( ( auto &&, params ),
                     UTXOTxParameters::create( account_m->utxos,
                                               account_m->address.GetPublicKey(),
                                               amount,
                                               uint256_t{ "0x" + hash_data.toReadableString() } ) );

        account_m->utxos        = UTXOTxParameters::UpdateUTXOList( account_m->utxos, params );
        auto escrow_transaction = std::make_shared<EscrowTransaction>( params,
                                                                       RoundTo5Digits( amount ),
                                                                       dev_addr,
                                                                       peers_cut,
                                                                       FillDAGStruct() );
        this->EnqueueTransaction( escrow_transaction );
        m_logger->debug( "Holding escrow to 0x{} wih the amount {}",
                         hash_data.toReadableString(),
                         RoundTo5Digits( amount ) );

        return "0x" + hash_data.toReadableString();
    }

    outcome::result<void> TransactionManager::ProcessingDone( const std::string              &task_id,
                                                              const SGProcessing::TaskResult &taskresult )
    {
        if ( taskresult.subtask_results().size() == 0 )
        {
            m_logger->debug( "No result on " + task_id );
            return outcome::failure( boost::system::error_code{} );
        }
        std::string escrow_path;
        for ( auto &subtask : taskresult.subtask_results() )
        {
            if ( !subtask.escrow_path().empty() )
            {
                escrow_path = subtask.escrow_path();
                break;
            }
        }
        if ( escrow_path.empty() )
        {
            m_logger->debug( "Escrow NOT FOUND on " + task_id );
            return outcome::failure( boost::system::error_code{} );
        }
        m_logger->debug( "Fetching escrow from processing DB at " + escrow_path );
        OUTCOME_TRY( ( auto &&, transaction ), FetchTransaction( processing_db_m, escrow_path ) );

        std::shared_ptr<EscrowTransaction> escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( transaction );
        std::vector<std::string>           subtask_ids;
        std::vector<OutputDestInfo>        payout_peers;
        double peers_amount = RoundTo5Digits( ( escrow_tx->GetPeersCut() * escrow_tx->GetAmount() ) /
                                              static_cast<double>( taskresult.subtask_results().size() ) );
        auto   remainder    = escrow_tx->GetAmount();
        for ( auto &subtask : taskresult.subtask_results() )
        {
            std::cout << "Subtask Result " << subtask.subtaskid() << "from " << subtask.node_address() << std::endl;
            m_logger->debug( "Paying out {} ", peers_amount );
            subtask_ids.push_back( subtask.subtaskid() );
            payout_peers.push_back( { peers_amount, uint256_t{ subtask.node_address() } } );
            remainder -= peers_amount;
        }
        m_logger->debug( "Sending to dev {}", remainder );
        payout_peers.push_back( { RoundTo5Digits( remainder ), escrow_tx->GetDevAddress() } );
        InputUTXOInfo escrow_utxo_input;

        escrow_utxo_input.txid_hash_  = ( base::Hash256::fromReadableString( escrow_tx->dag_st.data_hash() ) ).value();
        escrow_utxo_input.output_idx_ = 0;
        escrow_utxo_input.signature_  = ""; //TODO - Signature

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            payout_peers,
            std::vector<InputUTXOInfo>{ escrow_utxo_input },
            FillDAGStruct() );
        this->EnqueueTransaction( transfer_transaction );

        return outcome::success();
    }

    double TransactionManager::GetBalance()
    {
        return account_m->GetBalance<double>();
    }

    void TransactionManager::Update()
    {
        SendTransaction();
        CheckIncoming();
        CheckOutgoing();
        static auto start_time = std::chrono::steady_clock::now();
        if ( std::chrono::steady_clock::now() - start_time > std::chrono::minutes( 60 ) )
        {
            start_time = std::chrono::steady_clock::now();
            RefreshPorts();
        }
    }

    void TransactionManager::EnqueueTransaction( std::shared_ptr<IGeniusTransactions> element )
    {
        std::lock_guard<std::mutex> lock( mutex_m );
        tx_queue_m.emplace_back( std::move( element ) );
    }

    //TODO - Fill hash stuff on DAGStruct
    SGTransaction::DAGStruct TransactionManager::FillDAGStruct( std::string transaction_hash )
    {
        SGTransaction::DAGStruct dag;
        auto                     timestamp = std::chrono::system_clock::now();

        dag.set_previous_hash( transaction_hash );
        dag.set_nonce( 0 );
        dag.set_source_addr( account_m->GetAddress<std::string>() );
        dag.set_timestamp( timestamp.time_since_epoch().count() );
        dag.set_uncle_hash( "" );
        dag.set_data_hash( "" ); //filled byt transaction class
        return dag;
    }

    outcome::result<void> TransactionManager::SendTransaction()
    {
        std::unique_lock lock( mutex_m );
        if ( tx_queue_m.empty() )
        {
            return std::errc::invalid_argument;
        }

        auto transaction = tx_queue_m.front();
        tx_queue_m.pop_front();
        account_m->nonce = account_m->nonce + 1;

        transaction->dag_st.set_nonce( account_m->nonce );

        auto transaction_path = GetTransactionPath( transaction );

        sgns::crdt::GlobalDB::Buffer data_transaction;
        data_transaction.put( transaction->SerializeByteVector() );

        BOOST_OUTCOME_TRYV2( auto &&, outgoing_db_m->Put( { transaction_path }, data_transaction ) );

        m_logger->debug( "Recording the transaction on " + transaction_path );
        if ( transaction->GetType() == "transfer" )
        {
            m_logger->debug( "Notifying receiving peers of transfers" );
            NotifyDestinationOfTransfer( transaction );
        }
        else if ( transaction->GetType() == "escrow" )
        {
            m_logger->debug( "Posting Escrow transaction into processing db" );
            PostEscrowOnProcessingDB( transaction );
        }

        return outcome::success();
    }

    std::string TransactionManager::GetTransactionPath( std::shared_ptr<IGeniusTransactions> element )
    {
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

        tx_key % TEST_NET_ID;

        auto transaction_path = tx_key.str() + element->GetTransactionFullPath();

        return transaction_path;
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
            m_logger->info( "Invalid transaction found. No Deserialization available" );
            return std::errc::invalid_argument;
        }
        return it->second( transaction_data_vector );
    }

    outcome::result<void> TransactionManager::CheckIncoming()
    {
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

        tx_key % TEST_NET_ID;

        auto transaction_paths = tx_key.str() + "in" + account_m->GetAddress<std::string>();
        m_logger->trace( "Probing incoming transactions on " + transaction_paths );
        OUTCOME_TRY( ( auto &&, transaction_list ), incoming_db_m->QueryKeyValues( transaction_paths ) );

        m_logger->trace( "Incoming transaction list grabbed from CRDT" );

        //m_logger->info( "Number of tasks in Queue: {}", queryTasks.size() );
        for ( auto element : transaction_list )
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

            auto maybe_transaction = FetchTransaction( incoming_db_m, { transaction_key.value() } );
            if ( !maybe_transaction.has_value() )
            {
                m_logger->debug( "Can't fetch transaction" );
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

        auto transaction_paths = tx_key.str() + account_m->GetAddress<std::string>();
        m_logger->trace( "Probing transactions on " + transaction_paths );
        OUTCOME_TRY( ( auto &&, transaction_list ), outgoing_db_m->QueryKeyValues( transaction_paths ) );

        m_logger->trace( "Transaction list grabbed from CRDT" );

        //m_logger->info( "Number of tasks in Queue: {}", queryTasks.size() );
        for ( auto element : transaction_list )
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
            //if ( maybe_transaction.value()->GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
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
            if ( dest_infos[i].dest_address == account_m->GetAddress<uint256_t>() )
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
        if ( mint_tx->GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
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

        if ( escrow_tx->GetSrcAddress<uint256_t>() == account_m->GetAddress<uint256_t>() )
        {
            auto dest_infos = escrow_tx->GetUTXOParameters();

            if ( !dest_infos.outputs_.empty() )
            {
                //The first is the escrow, second is the change (might not happen)
                auto hash = ( base::Hash256::fromReadableString( escrow_tx->dag_st.data_hash() ) ).value();
                if ( dest_infos.outputs_.size() > 1 )
                {
                    if ( dest_infos.outputs_[1].dest_address == account_m->GetAddress<uint256_t>() )
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
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx );
        auto dest_infos  = transfer_tx->GetDstInfos();

        for ( std::uint32_t i = 0; i < dest_infos.size(); ++i )
        {
            if ( dest_infos[i].dest_address != account_m->GetAddress<uint256_t>() )
            {
                std::string peer_address = Uint256ToString( dest_infos[i].dest_address );
                m_logger->debug( "Sending notification to " + peer_address );
                std::shared_ptr<crdt::GlobalDB> destination_db;
                auto                            destination_db_it = destination_dbs_m.find( peer_address );
                if ( destination_db_it == destination_dbs_m.end() )
                {
                    m_logger->debug( "Port to sync  " + std::to_string( base_port_m ) );

                    destination_db = std::make_shared<crdt::GlobalDB>(
                        ctx_m,
                        ( boost::format( base_path_m + "_out/" + peer_address ) ).str(),
                        base_port_m,
                        std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_m, peer_address + "in" ) );
                    if ( !destination_db->Init( crdt::CrdtOptions::DefaultOptions() ).has_value() )
                    {
                        throw std::runtime_error( "Could not start Destination GlobalDB" );
                    }
                    destination_dbs_m[peer_address] = destination_db;
                    used_ports_m.insert( base_port_m );
                    base_port_m++;
                    RefreshPorts();
                }
                else
                {
                    destination_db = destination_db_it->second;
                }

                boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

                tx_key % TEST_NET_ID;

                auto transaction_paths = tx_key.str() + "in" + peer_address + GetTransactionPath( tx );

                sgns::crdt::GlobalDB::Buffer data_transaction;
                data_transaction.put( tx->SerializeByteVector() );

                BOOST_OUTCOME_TRYV2( auto &&, destination_db->Put( { transaction_paths }, data_transaction ) );
            }
        }

        return outcome::success();
    }

    outcome::result<void> TransactionManager::PostEscrowOnProcessingDB( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );

        auto dest_infos = escrow_tx->GetUTXOParameters();

        if ( !dest_infos.outputs_.empty() )
        {
            std::string job_id_hash = Uint256ToString( dest_infos.outputs_[0].dest_address );

            sgns::crdt::GlobalDB::Buffer data_transaction;
            data_transaction.put( tx->SerializeByteVector() );

            m_logger->debug( "Escrow sent to processing db on path " + job_id_hash );

            BOOST_OUTCOME_TRYV2( auto &&, processing_db_m->Put( { job_id_hash }, data_transaction ) );
        }

        return outcome::success();
    }

    const std::vector<std::vector<uint8_t>> TransactionManager::GetOutTransactions() const
    {
        std::vector<std::vector<std::uint8_t>> result;
        result.reserve( outgoing_tx_processed_m.size() );
        for ( const auto &[key, value] : outgoing_tx_processed_m )
        {
            result.push_back( value );
        }
        return result;
    }

    const std::vector<std::vector<uint8_t>> TransactionManager::GetInTransactions() const
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
}
