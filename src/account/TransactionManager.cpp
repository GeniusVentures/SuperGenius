/**
 * @file       TransactionManager.cpp
 * @brief      
 * @date       2024-04-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <boost/range/concepts.hpp>

#include "account/TransactionManager.hpp"

#include <stdexcept>
#include <utility>
#include <algorithm>
#include <thread>

#include <ProofSystem/EthereumKeyPairParams.hpp>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "TransferTransaction.hpp"
#include "MintTransaction.hpp"
#include "EscrowTransaction.hpp"
#include "EscrowReleaseTransaction.hpp"
#include "UTXOTxParameters.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "base/fixed_point.hpp"
#include "crdt/impl/crdt_data_filter.hpp"
#include "crdt/proto/delta.pb.h"

#ifdef _PROOF_ENABLED
#include "proof/TransferProof.hpp"
#include "proof/ProcessingProof.hpp"
#endif

namespace sgns
{
    TransactionManager::TransactionManager( std::shared_ptr<crdt::GlobalDB>          processing_db,
                                            std::shared_ptr<boost::asio::io_context> ctx,
                                            std::shared_ptr<GeniusAccount>           account,
                                            std::shared_ptr<crypto::Hasher>          hasher ) :
        globaldb_m( std::move( processing_db ) ),
        ctx_m( std::move( ctx ) ),
        account_m( std::move( account ) ),
        hasher_m( std::move( hasher ) ),
        timer_m( std::make_shared<boost::asio::steady_timer>( *ctx_m, boost::asio::chrono::milliseconds( 300 ) ) )

    {
        m_logger->info( "Initializing values by reading whole blockchain" );
        globaldb_m->AddBroadcastTopic( account_m->GetAddress() + "in" );
        globaldb_m->AddBroadcastTopic( account_m->GetAddress() + "out" );

        bool crdt_tx_filter_initialized = crdt::CRDTDataFilter::RegisterElementFilter(
            "^/?" + GetBlockChainBase() + "[^/]*/tx/[^/]*/[0-9]+",
            [&]( const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                std::optional<std::vector<crdt::pb::Element>> maybe_tombstones;
                bool                                          valid_tx = false;
                std::shared_ptr<IGeniusTransactions>          tx;
                do
                {
                    //TODO - This verification is only needed because CRDT resyncs every boot up
                    // Remove once we remove the in memory processed_cids on crdt_datastore and use dagsyncher again
                    auto maybe_has_value = incoming_db_m->Get( element.key() );
                    if ( maybe_has_value.has_value() )
                    {
                        m_logger->debug( "Already have the transaction {}", element.key() );
                        valid_tx = true;
                        break;
                    }
                    auto maybe_tx = DeSerializeTransaction( element.value() );
                    if ( maybe_tx.has_error() )
                    {
                        break;
                    }
                    tx = maybe_tx.value();
                    if ( !IGeniusTransactions::CheckDAGStructSignature( tx->dag_st ) )
                    {
                        m_logger->error( "Could not validate signature of transaction {}", element.key() );
                        break;
                    }

                    m_logger->trace( "Valid signature of {}", element.key() );
                    valid_tx = true;

                } while ( 0 );
                if ( !valid_tx )
                {
                    std::vector<crdt::pb::Element> tombstones;
                    tombstones.push_back( element );
                    auto maybe_proof_key = GetExpectedProofKey( element.key(), tx );
                    if ( maybe_proof_key.has_value() )
                    {
                        crdt::pb::Element proof_tombstone;
                        proof_tombstone.set_key( maybe_proof_key.value() );
                        tombstones.push_back( proof_tombstone );
                    }

                    maybe_tombstones = tombstones;
                }

                return maybe_tombstones;
            } );

        bool crdt_proof_filter_initialized = crdt::CRDTDataFilter::RegisterElementFilter(
            "^/?" + GetBlockChainBase() + "[^/]*/proof/[^/]*/[0-9]+",
            [&]( const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                std::optional<std::vector<crdt::pb::Element>> maybe_tombstones;
                bool                                          valid_proof = false;
                do
                {
                    //TODO - This verification is only needed because CRDT resyncs every boot up
                    // Remove once we remove the in memory processed_cids on crdt_datastore and use dagsyncher again
                    auto maybe_has_value = incoming_db_m->Get( element.key() );
                    if ( maybe_has_value.has_value() )
                    {
                        m_logger->debug( "Already have the proof {}", element.key() );
                        valid_proof = true;
                        break;
                    }
                    std::vector<uint8_t> proof_data_vector( element.value().begin(), element.value().end() );
                    auto                 maybe_valid_proof = IBasicProof::VerifyFullProof( proof_data_vector );
                    if ( maybe_valid_proof.has_error() || ( !maybe_valid_proof.value() ) )
                    {
                        // TODO: kill reputation point of the node.
                        m_logger->error( "Could not verify proof {}", element.key() );
                        break;
                    }
                    m_logger->trace( "Valid proof of {}", element.key() );

                    valid_proof = true;
                } while ( 0 );

                if ( valid_proof == false )
                {
                    std::vector<crdt::pb::Element> tombstones;
                    tombstones.push_back( element );
                    auto maybe_tx_key = GetExpectedTxKey( element.key() );
                    if ( maybe_tx_key.has_value() )
                    {
                        crdt::pb::Element tx_tombstone;
                        tx_tombstone.set_key( maybe_tx_key.value() );
                        tombstones.push_back( tx_tombstone );
                    }
                    maybe_tombstones = tombstones;
                }

                return maybe_tombstones;
            } );
    }

    void TransactionManager::Start()
    {
        CheckIncoming( false );
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

    outcome::result<std::string> TransactionManager::TransferFunds( uint64_t amount, const std::string &destination )
    {
        auto maybe_params = UTXOTxParameters::create( account_m->utxos, account_m->GetAddress(), amount, destination );

        if ( !maybe_params )
        {
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
        }

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            TransferTransaction::New( maybe_params.value().outputs_,
                                      maybe_params.value().inputs_,
                                      FillDAGStruct(),
                                      account_m->eth_address ) );
        std::optional<std::vector<uint8_t>> maybe_proof;
#ifdef _PROOF_ENABLED
        TransferProof prover( static_cast<uint64_t>( account_m->GetBalance<uint64_t>() ),
                              static_cast<uint64_t>( amount ) );
        OUTCOME_TRY( ( auto &&, proof_result ), prover.GenerateFullProof() );
        maybe_proof = std::move( proof_result );
#endif

        account_m->utxos = UTXOTxParameters::UpdateUTXOList( account_m->utxos, maybe_params.value() );
        this->EnqueueTransaction( std::make_pair( transfer_transaction, maybe_proof ) );

        return transfer_transaction->dag_st.data_hash();
    }

    outcome::result<std::string> TransactionManager::MintFunds( uint64_t    amount,
                                                                std::string transaction_hash,
                                                                std::string chainid,
                                                                std::string tokenid )
    {
        auto mint_transaction = std::make_shared<MintTransaction>(
            MintTransaction::New( amount,
                                  std::move( chainid ),
                                  std::move( tokenid ),
                                  FillDAGStruct( std::move( transaction_hash ) ),
                                  account_m->eth_address ) );
        std::optional<std::vector<uint8_t>> maybe_proof;
#ifdef _PROOF_ENABLED
        TransferProof prover( 1000000000000000,
                              static_cast<uint64_t>( amount ) ); //Mint max 1000000 gnus per transaction
        OUTCOME_TRY( ( auto &&, proof_result ), prover.GenerateFullProof() );
        maybe_proof = std::move( proof_result );
#endif
        // Store the transaction ID before moving the transaction
        auto txId = mint_transaction->dag_st.data_hash();

        this->EnqueueTransaction( std::make_pair( std::move( mint_transaction ), maybe_proof ) );

        return txId;
    }

    outcome::result<std::pair<std::string, EscrowDataPair>> TransactionManager::HoldEscrow( uint64_t           amount,
                                                                                            const std::string &dev_addr,
                                                                                            uint64_t peers_cut,
                                                                                            const std::string &job_id )
    {
        auto hash_data = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );

        OUTCOME_TRY( ( auto &&, params ),
                     UTXOTxParameters::create( account_m->utxos,
                                               account_m->GetAddress(),
                                               amount,
                                               "0x" + hash_data.toReadableString() ) );

        account_m->utxos        = UTXOTxParameters::UpdateUTXOList( account_m->utxos, params );
        auto escrow_transaction = std::make_shared<EscrowTransaction>(
            EscrowTransaction::New( params, amount, dev_addr, peers_cut, FillDAGStruct(), account_m->eth_address ) );

        // Get the transaction ID for tracking
        auto txId = escrow_transaction->dag_st.data_hash();

        std::optional<std::vector<uint8_t>> maybe_proof;
#ifdef _PROOF_ENABLED
        TransferProof prover( static_cast<uint64_t>( account_m->GetBalance<uint64_t>() ),
                              static_cast<uint64_t>( amount ) );
        OUTCOME_TRY( ( auto &&, proof_result ), prover.GenerateFullProof() );
        maybe_proof = std::move( proof_result );
#endif

        this->EnqueueTransaction( std::make_pair( escrow_transaction, maybe_proof ) );

        sgns::crdt::GlobalDB::Buffer data_transaction;
        data_transaction.put( escrow_transaction->SerializeByteVector() );

        // Return both the transaction ID and the original EscrowDataPair
        return std::make_pair( txId,
                               std::make_pair( "0x" + hash_data.toReadableString(), std::move( data_transaction ) ) );
    }

    outcome::result<std::string> TransactionManager::PayEscrow( const std::string              &escrow_path,
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
        OUTCOME_TRY( ( auto &&, transaction ), FetchTransaction( globaldb_m, escrow_path ) );

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
                                      FillDAGStruct(),
                                      account_m->eth_address ) );

        std::optional<std::vector<uint8_t>> transfer_proof;
#ifdef _PROOF_ENABLED
        //TODO - Create with the real balance and amount
        TransferProof transfer_prover( 1, 1 );
        OUTCOME_TRY( ( auto &&, transfer_proof_result ), transfer_prover.GenerateFullProof() );
        transfer_proof = std::move( transfer_proof_result );
#endif
        auto escrow_release_tx = std::make_shared<EscrowReleaseTransaction>(
            EscrowReleaseTransaction::New( escrow_tx->GetUTXOParameters(),
                                           escrow_tx->GetAmount(),
                                           escrow_tx->GetDevAddress(),
                                           escrow_tx->dag_st.source_addr(),
                                           escrow_tx->dag_st.data_hash(),
                                           FillDAGStruct(),
                                           account_m->eth_address ) );

        std::optional<std::vector<uint8_t>> escrow_release_proof;
#ifdef _PROOF_ENABLED
        //TODO - Create with the real balance and amount
        TransferProof escrow_release_prover( 1, 1 );
        OUTCOME_TRY( ( auto &&, escrow_release_proof_result ), escrow_release_prover.GenerateFullProof() );
        escrow_release_proof = std::move( escrow_release_proof_result );
#endif

        this->EnqueueTransaction( std::make_pair( escrow_release_tx, escrow_release_proof ) );
        this->EnqueueTransaction( std::make_pair( transfer_transaction, transfer_proof ) );
        return transfer_transaction->dag_st.data_hash();
    }

    uint64_t TransactionManager::GetBalance()
    {
        return account_m->GetBalance<uint64_t>();
    }

    void TransactionManager::Update()
    {
        auto send_result = SendTransaction();
        if ( send_result.has_error() )
        {
            m_logger->error( "Unknown SendTranscation error in SendTransaction::Update()" );
        }
        auto check_result = CheckIncoming();
        if ( check_result.has_error() )
        {
            m_logger->error( "Unknown CheckIncoming error in SendTransaction::Update()" );
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
            return outcome::success();
        }

        auto [transaction, maybe_proof] = tx_queue_m.front();
        tx_queue_m.pop_front();
        lock.unlock();

        // this was set prior and needed for the proof to match when the proof was generated

        auto                         transaction_path = GetTransactionPath( *transaction );
        sgns::crdt::HierarchicalKey  tx_key( transaction_path );
        sgns::crdt::GlobalDB::Buffer data_transaction;

        m_logger->debug( "Recording the transaction on " + tx_key.GetKey() );

        auto crdt_transaction = globaldb_m->BeginTransaction();

        data_transaction.put( transaction->SerializeByteVector() );
        BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

        if ( maybe_proof )
        {
            sgns::crdt::HierarchicalKey  proof_key( GetTransactionProofPath( *transaction ) );
            sgns::crdt::GlobalDB::Buffer proof_transaction;

            auto &proof = maybe_proof.value();
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
        else if ( transaction->GetType() == "escrow-release" )
        {
            m_logger->debug( "Notifying escrow source of escrow release" );
            BOOST_OUTCOME_TRYV2( auto &&, NotifyEscrowRelease( transaction, maybe_proof ) );
        }
        BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Commit( account_m->GetAddress() + "out" ) );

        BOOST_OUTCOME_TRYV2( auto &&, ParseTransaction( transaction ) );

        {
            std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
            outgoing_tx_processed_m[transaction_path] = transaction;
        }


        return outcome::success();
    }

    std::string TransactionManager::GetTransactionPath( IGeniusTransactions &element )
    {
        auto transaction_path = GetBlockChainBase() + element.GetTransactionFullPath();

        return transaction_path;
    }

    std::string TransactionManager::GetTransactionProofPath( IGeniusTransactions &element )
    {
        auto proof_path = GetBlockChainBase() + element.GetProofFullPath();

        return proof_path;
    }

    std::string TransactionManager::GetTransactionBasePath( const std::string &address )
    {
        auto tx_base_path = GetBlockChainBase() + address;

        return tx_base_path;
    }

    std::string TransactionManager::GetBlockChainBase()
    {
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

        tx_key % TEST_NET_ID;
        return tx_key.str();
    }

    outcome::result<std::string> TransactionManager::GetExpectedProofKey(
        const std::string                          &tx_key,
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        std::string ret;
        do
        {
            if ( tx )
            {
                ret = GetTransactionProofPath( *tx );
                break;
            }

            static const std::regex txRegex( "^/?" + GetBlockChainBase() + "/[^/]*/tx/([^/]*)/([0-9]+)$" );
            std::smatch             matches;

            if ( !std::regex_match( tx_key, matches, txRegex ) || matches.size() < 2 )
            {
                // Not a valid transaction key
                return outcome::failure(
                    boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
            }
            std::string txType = matches[1]; // The transaction type
            std::string txId   = matches[2]; // The ID part

            // Find the position of "/tx/" to extract the address part
            size_t txPos = tx_key.find( "/tx/" );
            if ( txPos == std::string::npos )
            {
                return outcome::failure(
                    boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
            }

            // Extract the address part (everything before "/tx/")
            std::string addressPart = tx_key.substr( 0, txPos );

            // Construct the proof key
            ret = addressPart + "/proof/" + txType + txId;

        } while ( 0 );

        return ret;
    }

    outcome::result<std::string> TransactionManager::GetExpectedTxKey( const std::string &proof_key )
    {
        std::string ret;
        do
        {
            static const std::regex proofRegex( "^/?" + GetBlockChainBase() + "/[^/]*/proof/([^/]*)/([0-9]+)$" );
            std::smatch             matches;

            if ( !std::regex_match( proof_key, matches, proofRegex ) || matches.size() < 2 )
            {
                // Not a valid transaction key
                return outcome::failure(
                    boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
            }
            std::string proofType = matches[1]; // The proof type (e.g., "transfer")
            std::string proofId   = matches[2]; // The ID part

            // Find the position of "/tx/" to extract the address part
            size_t proofPos = proof_key.find( "/proof/" );
            if ( proofPos == std::string::npos )
            {
                return outcome::failure(
                    boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
            }

            std::string addressPart = proof_key.substr( 0, proofPos );

            // Construct the proof key
            ret = addressPart + "/tx/" + proofType + "/" + proofId;

        } while ( 0 );

        return ret;
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::DeSerializeTransaction(
        std::string tx_data )
    {
        OUTCOME_TRY( ( auto &&, dag ), IGeniusTransactions::DeSerializeDAGStruct( tx_data ) );

        auto it = IGeniusTransactions::GetDeSerializers().find( dag.type() );
        if ( it == IGeniusTransactions::GetDeSerializers().end() )
        {
            return std::errc::invalid_argument;
        }
        return it->second( std::vector<uint8_t>( tx_data.begin(), tx_data.end() ) );
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
        auto proof_path = GetTransactionProofPath( *tx );
        m_logger->debug( "Checking the proof in {}", proof_path );
        OUTCOME_TRY( ( auto &&, proof_data ), globaldb_m->Get( { proof_path } ) );

        auto proof_data_vector = proof_data.toVector();

        m_logger->debug( "Proof data acquired. Verifying..." );
        //std::cout << " it has value with size  " << proof_data.size() << std::endl;
        return IBasicProof::VerifyFullProof( proof_data_vector );
#else
        return true;
#endif
    }

    outcome::result<void> TransactionManager::CheckIncoming( bool checkProofs )
    {
        m_logger->trace( "Probing incoming transactions on " + GetBlockChainBase() + "!" + account_m->GetAddress() +
                         "/tx" );
        OUTCOME_TRY( ( auto &&, transaction_list ),
                     globaldb_m->QueryKeyValues( GetBlockChainBase(), "!" + account_m->GetAddress(), "/tx" ) );

        m_logger->trace( "Incoming transaction list grabbed from CRDT with Size {}", transaction_paths.size() );

        //m_logger->info( "Number of tasks in Queue: {}", queryTasks.size() );
        for ( const auto &element : transaction_list )
        {
            auto transaction_key = globaldb_m->KeyToString( element.first );
            if ( !transaction_key.has_value() )
            {
                m_logger->debug( "Unable to convert a key to string" );
                continue;
            }
            if ( incoming_tx_processed_m.find( transaction_key.value() ) != incoming_tx_processed_m.end() )
            {
                m_logger->trace( "Transaction already processed: " + transaction_key.value() );
                continue;
            }

            m_logger->debug( "Finding incoming transaction: " + transaction_key.value() );
            auto maybe_transaction = FetchTransaction( globaldb_m, transaction_key.value() );
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

            {
                m_logger->trace( "Inserting into incoming {}", transaction_key.value() );
                std::unique_lock<std::shared_mutex> out_lock( incoming_tx_mutex_m );
                incoming_tx_processed_m[transaction_key.value()] = maybe_transaction.value();
            }
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::CheckOutgoing()
    {
        m_logger->trace( "Probing outgoing transactions on " + GetBlockChainBase() );
        OUTCOME_TRY( ( auto &&, transaction_list ),
                     globaldb_m->QueryKeyValues( GetBlockChainBase(), account_m->GetAddress(), "/tx" ) );

        m_logger->trace( "Transaction list grabbed from CRDT" );

        //m_logger->info( "Number of tasks in Queue: {}", queryTasks.size() );
        for ( const auto &element : transaction_list )
        {
            auto transaction_key = globaldb_m->KeyToString( element.first );
            if ( !transaction_key.has_value() )
            {
                m_logger->debug( "Unable to convert a key to string" );
                continue;
            }
            if ( outgoing_tx_processed_m.find( transaction_key.value() ) != outgoing_tx_processed_m.end() )
            {
                m_logger->trace( "Transaction already processed: " + transaction_key.value() );
                continue;
            }

            auto maybe_transaction = FetchTransaction( globaldb_m, transaction_key.value() );
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
            {
                m_logger->trace( "Inserting into outgoing {}", transaction_key.value() );
                std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
                outgoing_tx_processed_m[transaction_key.value()] = maybe_transaction.value();
            }
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

    outcome::result<void> TransactionManager::ParseEscrowReleaseTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrowReleaseTx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tx );
        if ( !escrowReleaseTx )
        {
            m_logger->error( "Failed to cast transaction to EscrowReleaseTransaction" );
            return std::errc::invalid_argument;
        }

        std::string originalEscrowHash = escrowReleaseTx->GetOriginalEscrowHash();
        m_logger->debug( "Successfully fetched release for escrow: " + originalEscrowHash );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::NotifyDestinationOfTransfer(
        const std::shared_ptr<IGeniusTransactions> &tx,
        const std::optional<std::vector<uint8_t>>  &proof )
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

            const std::string topic            = dest_info.dest_address + "in";
            const std::string txPath           = GetTransactionPath( *tx );
            const std::string proofPath        = GetTransactionProofPath( *tx ) ;

            globaldb_m->AddBroadcastTopic( topic );

            auto crdt_transaction = globaldb_m->BeginTransaction();

            sgns::crdt::HierarchicalKey  tx_key( txPath );
            sgns::crdt::GlobalDB::Buffer data_transaction;
            data_transaction.put( tx->SerializeByteVector() );

            m_logger->debug( "Putting replicate transaction in " + tx_key.GetKey() );
            BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

            if ( proof )
            {
                sgns::crdt::HierarchicalKey  proof_key( proofPath );
                sgns::crdt::GlobalDB::Buffer proof_data;
                proof_data.put( proof.value() );
                m_logger->debug( "Putting replicate PROOF in " + proof_key.GetKey() );
                BOOST_OUTCOME_TRYV2( auto &&,
                                     crdt_transaction->Put( std::move( proof_key ), std::move( proof_data ) ) );
            }

            BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Commit( topic ) );
        }

        return outcome::success();
    }

    outcome::result<void> TransactionManager::NotifyEscrowRelease( const std::shared_ptr<IGeniusTransactions> &tx,
                                                                   const std::optional<std::vector<uint8_t>>  &proof )
    {
        auto escrow_release_tx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tx );
        if ( !escrow_release_tx )
        {
            m_logger->error( "Failed to cast transaction to EscrowReleaseTransaction" );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
        }

        std::string escrow_source = escrow_release_tx->GetEscrowSource();
        m_logger->debug( "Notifying escrow source: " + escrow_source );

        const std::string topic            = escrow_source + "in";
        const std::string txPath           = GetTransactionPath( *tx );
        const std::string proofPath        = GetTransactionProofPath( *tx );

        globaldb_m->AddBroadcastTopic( topic );

        auto crdt_transaction = globaldb_m->BeginTransaction();

        sgns::crdt::HierarchicalKey  tx_key( txPath );
        sgns::crdt::GlobalDB::Buffer data_transaction;
        data_transaction.put( tx->SerializeByteVector() );

        m_logger->debug( "Putting replicate escrow release transaction in " + tx_key.GetKey() );
        BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

        if ( proof )
        {
            sgns::crdt::HierarchicalKey  proof_key( proofPath );
            sgns::crdt::GlobalDB::Buffer proof_data;
            proof_data.put( proof.value() );
            m_logger->debug( "Putting replicate escrow release PROOF in " + proof_key.GetKey() );
            BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Put( std::move( proof_key ), std::move( proof_data ) ) );
        }

        BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Commit( topic ) );
        return outcome::success();
    }

    std::vector<std::vector<uint8_t>> TransactionManager::GetOutTransactions() const
    {
        std::vector<std::vector<std::uint8_t>> result;
        {
            std::shared_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
            result.reserve( outgoing_tx_processed_m.size() );
            for ( const auto &[key, value] : outgoing_tx_processed_m )
            {
                result.push_back( value->SerializeByteVector() );
            }
        }
        return result;
    }

    std::vector<std::vector<uint8_t>> TransactionManager::GetInTransactions() const
    {
        std::vector<std::vector<std::uint8_t>> result;
        {
            std::shared_lock<std::shared_mutex> in_lock( incoming_tx_mutex_m );
            result.reserve( incoming_tx_processed_m.size() );
            for ( const auto &[key, value] : incoming_tx_processed_m )
            {
                result.push_back( value->SerializeByteVector() );
            }
        }
        return result;
    }

    bool TransactionManager::WaitForTransactionIncoming( const std::string        &txId,
                                                         std::chrono::milliseconds timeout ) const
    {
        auto start = std::chrono::steady_clock::now();

        // Construct incoming notification path
        std::string incoming_path = GetBlockChainBase();

        do
        {
            // Check in incoming transactions with the exact path
            {
                std::shared_lock<std::shared_mutex> in_lock( incoming_tx_mutex_m );
                for ( auto tx : incoming_tx_processed_m )
                {
                    if ( tx.second->dag_st.data_hash() == txId )
                    {
                        return true;
                    }
                }
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        } while ( std::chrono::steady_clock::now() - start < timeout );

        return false;
    }

    bool TransactionManager::WaitForTransactionOutgoing( const std::string        &txId,
                                                         std::chrono::milliseconds timeout ) const
    {
        auto start = std::chrono::steady_clock::now();

        do
        {
            // Check in outgoing transactions with the exact path
            {
                std::shared_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
                for ( auto tx : outgoing_tx_processed_m )
                {
                    if ( tx.second->dag_st.data_hash() == txId )
                    {
                        return true;
                    }
                }
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        } while ( std::chrono::steady_clock::now() - start < timeout );

        return false;
    }

    bool TransactionManager::WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                   std::chrono::milliseconds timeout ) const
    {
        auto start = std::chrono::steady_clock::now();
        while ( std::chrono::steady_clock::now() - start < timeout )
        {
            {
                std::shared_lock<std::shared_mutex> lock( incoming_tx_mutex_m );
                for ( const auto &entry : incoming_tx_processed_m )
                {
                    auto tx = entry.second;
                    if ( tx->GetType() == "escrow-release" )
                    {
                        auto escrowReleaseTx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tx );
                        if ( escrowReleaseTx && escrowReleaseTx->GetOriginalEscrowHash() == originalEscrowId )
                        {
                            m_logger->debug( "Found matching escrow release transaction with tx id: " +
                                             tx->dag_st.data_hash() );
                            return true;
                        }
                    }
                }
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }
        m_logger->debug( "Timed out waiting for escrow release transaction for escrow id: " + originalEscrowId );
        return false;
    }
}
