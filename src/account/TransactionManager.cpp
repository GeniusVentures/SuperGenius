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
#include "account/TokenAmount.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crdt/proto/delta.pb.h"
#include "base/sgns_version.hpp"

#ifdef _PROOF_ENABLED
#include "proof/TransferProof.hpp"
#include "proof/ProcessingProof.hpp"
#endif

namespace sgns
{

    std::shared_ptr<TransactionManager> TransactionManager::New( std::shared_ptr<crdt::GlobalDB>          processing_db,
                                                                 std::shared_ptr<boost::asio::io_context> ctx,
                                                                 std::shared_ptr<GeniusAccount>           account,
                                                                 std::shared_ptr<crypto::Hasher>          hasher,
                                                                 bool                                     full_node )
    {
        auto instance = std::shared_ptr<TransactionManager>( new TransactionManager( std::move( processing_db ),
                                                                                     std::move( ctx ),
                                                                                     std::move( account ),
                                                                                     std::move( hasher ),
                                                                                     full_node ) );

        bool crdt_tx_filter_initialized = instance->globaldb_m->RegisterElementFilter(
            "^/?" + GetBlockChainBase() + "[^/]*/tx/[^/]*/[0-9]+",
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->FilterTransaction( element );
                }
                return std::nullopt;
            } );

        bool crdt_proof_filter_initialized = instance->globaldb_m->RegisterElementFilter(
            "^/?" + GetBlockChainBase() + "[^/]*/proof/[^/]*/[0-9]+",
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->FilterProof( element );
                }
                return std::nullopt;
            } );
        instance->globaldb_m->Start();

        return instance;
    }

    TransactionManager::TransactionManager( std::shared_ptr<crdt::GlobalDB>          processing_db,
                                            std::shared_ptr<boost::asio::io_context> ctx,
                                            std::shared_ptr<GeniusAccount>           account,
                                            std::shared_ptr<crypto::Hasher>          hasher,
                                            bool                                     full_node ) :
        globaldb_m( std::move( processing_db ) ),
        ctx_m( std::move( ctx ) ),
        account_m( std::move( account ) ),
        hasher_m( std::move( hasher ) ),
        timer_m( std::make_shared<boost::asio::steady_timer>( *ctx_m, boost::asio::chrono::milliseconds( 300 ) ) ),
        full_node_m( std::move( full_node ) ),
        state_m( State::CREATING )

    {
        m_logger->info( "[{} - full: {}] Initializing values by reading whole blockchain",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m );

        boost::format full_node_topic{ std::string( GNUS_FULL_NODES_TOPIC ) };
#ifdef DEV_NET
        full_node_topic % DEV_NET_ID;
#else
        full_node_topic % TEST_NET_ID;
#endif
        full_node_topic_m = full_node_topic.str();

        globaldb_m->AddListenTopic( account_m->GetAddress() );
        m_logger->info( "[{} - full: {}] Adding broadcast to full node on {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        full_node_topic_m );
        if ( full_node_m )
        {
            m_logger->debug( "[{} - full: {}] Listening full node on {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             full_node_topic_m );
            globaldb_m->AddListenTopic( full_node_topic_m );
        }
        globaldb_m->AddTopicName( full_node_topic_m );
        globaldb_m->AddTopicName( account_m->GetAddress() );
    }

    TransactionManager::~TransactionManager()
    {
        m_logger->debug( "[{} - full: {}] ~TransactionManager CALLED",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m );
        Stop();
    }

    void TransactionManager::Stop()
    {
        if ( stopped_.exchange( true ) )
        {
            return; // idempotent
        }
        if ( timer_m )
        {
            boost::system::error_code ig;
            timer_m->cancel( ig ); // unblock async_wait with ec
        }
    }

    void TransactionManager::Start()
    {
        if ( state_m != State::CREATING || stopped_.load() )
        {
            return;
        }

        state_m = State::INITIALIZING;

        if ( !stopped_.load() )
        {
            CheckIncoming();
            CheckOutgoing();
        }

        task_m = [this]()
        {
            if ( stopped_.load() )
            {
                return;
            }

            switch ( state_m )
            {
                case State::INITIALIZING:
                    this->InitNonce( 5000 );
                    if ( state_m == State::READY )
                    {
                        m_logger->debug( "[{} - full: {}] Transaction Manager is now READY - starting regular updates",
                                         account_m->GetAddress().substr( 0, 8 ),
                                         full_node_m );
                    }
                    break;

                case State::CREATING: // Should not happen, but handle gracefully
                    break;

                case State::SYNCHING:
                    this->SyncNonce();
                    break;

                case State::READY:
                {
                    auto send_result = SendTransaction();
                    if ( send_result.has_error() )
                    {
                        m_logger->error( "[{} - full: {}] Unknown SendTranscation error in SendTransaction::Update()",
                                         account_m->GetAddress().substr( 0, 8 ),
                                         full_node_m );
                    }
                    break;
                }
            }

            if ( !stopped_.load() )
            {
                Update();
            }

            timer_m->expires_after( boost::asio::chrono::milliseconds( 300 ) );
            timer_m->async_wait(
                [weak_instance = weak_from_this()]( const boost::system::error_code &ec )
                {
                    if ( ec )
                    {
                        return; // canceled or other error
                    }
                    if ( auto instance = weak_instance.lock() )
                    {
                        if ( !instance->stopped_.load() )
                        {
                            instance->ctx_m->post( instance->task_m );
                        }
                    }
                } );
        };

        if ( !stopped_.load() )
        {
            ctx_m->post( task_m );
        }
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

    outcome::result<std::string> TransactionManager::TransferFunds( uint64_t           amount,
                                                                    const std::string &destination,
                                                                    TokenID            token_id )
    {
        if ( state_m != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        OUTCOME_TRY(
            auto &&params,
            UTXOTxParameters::create( account_m->utxos, account_m->GetAddress(), amount, destination, token_id ) );

        params.SignParameters( account_m );

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            TransferTransaction::New( params.outputs_, params.inputs_, FillDAGStruct() ) );

        std::optional<std::vector<uint8_t>> maybe_proof;
#ifdef _PROOF_ENABLED
        TransferProof prover( static_cast<uint64_t>( account_m->GetBalance<uint64_t>() ),
                              static_cast<uint64_t>( amount ) );
        OUTCOME_TRY( ( auto &&, proof_result ), prover.GenerateFullProof() );
        maybe_proof = std::move( proof_result );
#endif

        account_m->utxos = UTXOTxParameters::ReserveUTXOs( account_m->utxos, params );
        this->EnqueueTransaction( std::make_pair( transfer_transaction, maybe_proof ) );

        return transfer_transaction->dag_st.data_hash();
    }

    outcome::result<std::string> TransactionManager::MintFunds( uint64_t    amount,
                                                                std::string transaction_hash,
                                                                std::string chainid,
                                                                TokenID     tokenid )
    {
        if ( state_m != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto mint_transaction = std::make_shared<MintTransaction>(
            MintTransaction::New( amount,
                                  std::move( chainid ),
                                  std::move( tokenid ),
                                  FillDAGStruct( std::move( transaction_hash ) ) ) );
        std::optional<std::vector<uint8_t>> maybe_proof;
#ifdef _PROOF_ENABLED
        TransferProof prover( 1000000000000,
                              static_cast<uint64_t>( amount ) ); // Mint max 1000000 gnus per transaction
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
        if ( state_m != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto hash_data = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );

        OUTCOME_TRY( ( auto &&, params ),
                     UTXOTxParameters::create( account_m->utxos,
                                               account_m->GetAddress(),
                                               amount,
                                               "0x" + hash_data.toReadableString(),
                                               TokenID::FromBytes( { 0x00 } ) ) );

        params.SignParameters( account_m );

        account_m->utxos        = UTXOTxParameters::ReserveUTXOs( account_m->utxos, params );
        auto escrow_transaction = std::make_shared<EscrowTransaction>(
            EscrowTransaction::New( params, amount, dev_addr, peers_cut, FillDAGStruct() ) );

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

    outcome::result<std::string> TransactionManager::PayEscrow(
        const std::string                       &escrow_path,
        const SGProcessing::TaskResult          &taskresult,
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction )
    {
        if ( taskresult.subtask_results().size() == 0 )
        {
            m_logger->debug( "[{} - full: {}] No result found on escrow {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             escrow_path );
            return outcome::failure( boost::system::error_code{} );
        }
        if ( escrow_path.empty() )
        {
            m_logger->debug( "[{} - full: {}] Escrow path empty", account_m->GetAddress().substr( 0, 8 ), full_node_m );
            return outcome::failure( boost::system::error_code{} );
        }
        m_logger->debug( "[{} - full: {}] Fetching escrow from processing DB at {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         escrow_path );
        OUTCOME_TRY( ( auto &&, transaction ), FetchTransaction( globaldb_m, escrow_path ) );

        std::shared_ptr<EscrowTransaction> escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( transaction );
        std::vector<std::string>           subtask_ids;
        std::vector<OutputDestInfo>        payout_peers;

        OUTCOME_TRY( ( auto &&, escrow_amount_ptr ), TokenAmount::New( escrow_tx->GetAmount() ) );

        OUTCOME_TRY( ( auto &&, peers_cut_ptr ), TokenAmount::New( escrow_tx->GetPeersCut() ) );

        OUTCOME_TRY( ( auto &&, peer_total ), escrow_amount_ptr->Multiply( *peers_cut_ptr ) );

        const auto escrowTokenId = escrow_tx->GetUTXOParameters().outputs_[0].token_id;

        uint64_t peers_amount = peer_total.Value() / static_cast<uint64_t>( taskresult.subtask_results().size() );
        auto     remainder    = escrow_tx->GetAmount();

        for ( auto &subtask : taskresult.subtask_results() )
        {
            std::cout << "Subtask Result " << subtask.subtaskid() << "from " << subtask.node_address() << std::endl;
            m_logger->debug( "[{} - full: {}] Paying out {} in {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             peers_amount,
                             subtask.token_id() );
            subtask_ids.push_back( subtask.subtaskid() );
            payout_peers.push_back( { peers_amount,
                                      subtask.node_address(),
                                      TokenID::FromBytes( subtask.token_id().data(), subtask.token_id().size() ) } );
            remainder -= peers_amount;
        }
        //TODO: see what do with token_id here
        m_logger->debug( "[{} - full: {}] Sending to dev {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         remainder );
        payout_peers.push_back( { remainder, escrow_tx->GetDevAddress(), escrowTokenId } );
        InputUTXOInfo escrow_utxo_input;

        escrow_utxo_input.txid_hash_  = ( base::Hash256::fromReadableString( escrow_tx->dag_st.data_hash() ) ).value();
        escrow_utxo_input.output_idx_ = 0;
        escrow_utxo_input.signature_  = ""; //TODO - Signature

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            TransferTransaction::New( payout_peers,
                                      std::vector<InputUTXOInfo>{ escrow_utxo_input },
                                      FillDAGStruct() ) );

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
                                           FillDAGStruct() ) );

        std::optional<std::vector<uint8_t>> escrow_release_proof;
#ifdef _PROOF_ENABLED
        //TODO - Create with the real balance and amount
        TransferProof escrow_release_prover( 1, 1 );
        OUTCOME_TRY( ( auto &&, escrow_release_proof_result ), escrow_release_prover.GenerateFullProof() );
        escrow_release_proof = std::move( escrow_release_proof_result );
#endif

        TransactionBatch tx_batch;

        tx_batch.push_back( std::make_pair( transfer_transaction, transfer_proof ) );
        tx_batch.push_back( std::make_pair( escrow_release_tx, escrow_release_proof ) );

        EnqueueTransaction( std::make_pair( tx_batch, std::move( crdt_transaction ) ) );
        return transfer_transaction->dag_st.data_hash();
    }

    uint64_t TransactionManager::GetBalance()
    {
        return account_m->GetBalance<uint64_t>();
    }

    void TransactionManager::Update()
    {
        auto check_out_result = CheckOutgoing();
        if ( check_out_result.has_error() )
        {
            m_logger->error( "[{} - full: {}] Unknown CheckOutgoing error in SendTransaction::Update()",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
        }
        auto check_result = CheckIncoming();
        if ( check_result.has_error() )
        {
            m_logger->error( "[{} - full: {}] Unknown CheckIncoming error in SendTransaction::Update()",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
        }
        auto confirm_result = ConfirmTransactions();
        if ( confirm_result.has_error() )
        {
            m_logger->trace( "[{} - full: {}] Unknown ConfirmTransactions error in SendTransaction::Update()",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
        }
    }

    void TransactionManager::EnqueueTransaction( TransactionItem element )
    {
        m_logger->debug( "[{} - full: {}] Transaction enqueuing", account_m->GetAddress().substr( 0, 8 ), full_node_m );
        {
            std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
            for ( auto &tx_pair : element.first )
            {
                const auto &tx  = tx_pair.first;
                const auto  key = GetTransactionPath( *tx );
                // tx visible to status queries immediately
                outgoing_tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::CREATED };
                m_logger->debug( "[{} - full: {}] Setting {} to CREATED",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 tx->dag_st.data_hash() );
            }
        }
        std::lock_guard<std::mutex> lock( mutex_m );
        tx_queue_m.emplace_back( std::move( element ) );
    }

    void TransactionManager::EnqueueTransaction( TransactionPair element )
    {
        EnqueueTransaction( { { std::move( element ) }, std::nullopt } );
    }

    //TODO - Fill hash stuff on DAGStruct
    SGTransaction::DAGStruct TransactionManager::FillDAGStruct( std::string transaction_hash ) const
    {
        SGTransaction::DAGStruct dag;
        auto                     timestamp = std::chrono::system_clock::now();

        dag.set_previous_hash( transaction_hash );
        dag.set_nonce( account_m->GetProposedNonce() );
        dag.set_source_addr( account_m->GetAddress() );
        dag.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );
        dag.set_uncle_hash( "" );
        dag.set_data_hash( "" ); //filled by transaction class

        account_m->IncProposedNonce();

        return dag;
    }

    outcome::result<bool> TransactionManager::SendTransaction()
    {
        std::unique_lock lock( mutex_m );
        if ( tx_queue_m.empty() )
        {
            return outcome::success();
        }

        auto [transaction_batch, maybe_crdt_transaction] = tx_queue_m.front();
        //attempt here to insert the correct nonce
        tx_queue_m.pop_front();
        lock.unlock();
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction = nullptr;

        if ( maybe_crdt_transaction.has_value() && maybe_crdt_transaction.value() )
        {
            crdt_transaction = std::move( maybe_crdt_transaction.value() );
        }
        else
        {
            crdt_transaction = globaldb_m->BeginTransaction();
        }
        auto     nonce_result        = account_m->GetConfirmedNonce( 3000 );
        uint64_t expected_next_nonce = 0;
        uint64_t confirmed_nonce     = 0;
        if ( !nonce_result.has_value() )
        {
            m_logger->debug( "[{} - full: {}] Can't fetch nonce from the network, getting local",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
            nonce_result = account_m->GetLocalConfirmedNonce();
        }

        if ( nonce_result.has_value() )
        {
            confirmed_nonce = nonce_result.value();
            m_logger->debug( "[{} - full: {}] Set nonce to {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             confirmed_nonce );
            expected_next_nonce = confirmed_nonce + 1;
        }

        for ( auto &transaction_pair : transaction_batch )
        {
            auto [transaction, maybe_proof] = transaction_pair;

            if ( transaction->dag_st.nonce() > expected_next_nonce )
            {
                //Either my old txs are outdated or
                //The responder has not updated yet
                m_logger->error( "[{} - full: {}] Network outdated nonce - Expected: {}, Tried to send: {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 expected_next_nonce,
                                 transaction->dag_st.nonce() );
                state_m          = State::SYNCHING;
                auto local_nonce = transaction->dag_st.nonce();
                while ( --local_nonce > confirmed_nonce )
                {
                    m_logger->debug( "[{} - full: {}] Setting status of transactions {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     local_nonce );
                    (void)SetOutgoingStatusByNonce( local_nonce, TransactionStatus::VERIFYING );
                }
                {
                    std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
                    const auto                          key = GetTransactionPath( *transaction );

                    auto &t  = outgoing_tx_processed_m[key]; // create if missing
                    t.tx     = transaction;
                    t.status = TransactionStatus::FAILED;
                }

                RevertTransaction( transaction );
                return outcome::failure( boost::system::error_code{} );
            }
            else if ( transaction->dag_st.nonce() < expected_next_nonce )
            {
                m_logger->error( "[{} - full: {}] Local nonce outdated - Expected: {}, Tried to send: {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 expected_next_nonce,
                                 transaction->dag_st.nonce() );
                state_m = State::SYNCHING;

                {
                    std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
                    const auto                          key = GetTransactionPath( *transaction );

                    auto &t  = outgoing_tx_processed_m[key];
                    t.tx     = transaction;
                    t.status = TransactionStatus::FAILED;
                }
                RevertTransaction( transaction );

                return outcome::failure( boost::system::error_code{} );
            }

            (void)transaction->MakeSignature( account_m );

            auto                         transaction_path = GetTransactionPath( *transaction );
            sgns::crdt::HierarchicalKey  tx_key( transaction_path );
            sgns::crdt::GlobalDB::Buffer data_transaction;

            m_logger->debug( "[{} - full: {}] Recording the transaction on {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             tx_key.GetKey() );

            data_transaction.put( transaction->SerializeByteVector() );
            BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

            if ( maybe_proof )
            {
                sgns::crdt::HierarchicalKey  proof_key( GetTransactionProofPath( *transaction ) );
                sgns::crdt::GlobalDB::Buffer proof_transaction;

                auto &proof = maybe_proof.value();
                m_logger->debug( "[{} - full: {}] Recording the proof on {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 proof_key.GetKey() );

                proof_transaction.put( proof );
                BOOST_OUTCOME_TRYV2( auto &&,
                                     crdt_transaction->Put( std::move( proof_key ), std::move( proof_transaction ) ) );
            }
            expected_next_nonce++;
        }
        expected_next_nonce--;

        std::set<std::string> topicSet;
        for ( auto &transaction_pair : transaction_batch )
        {
            OUTCOME_TRY( auto &&parsedTopics, ParseTransaction( transaction_pair.first ) );
            topicSet.insert( parsedTopics.begin(), parsedTopics.end() );
            {
                std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
                const auto                          key      = GetTransactionPath( *transaction_pair.first );
                auto                                it       = outgoing_tx_processed_m.find( key );
                auto                                tx_state = TransactionStatus::VERIFYING;
                if ( full_node_m )
                {
                    tx_state = TransactionStatus::CONFIRMED;
                }
                if ( it != outgoing_tx_processed_m.end() )
                {
                    it->second.status = tx_state;
                }
                else
                {
                    outgoing_tx_processed_m[key] = TrackedTx{ transaction_pair.first, tx_state };
                }
            }
        }

        BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Commit( topicSet ) );
        account_m->SetLocalConfirmedNonce( expected_next_nonce );

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
#ifdef DEV_NET
        tx_key % DEV_NET_ID;
#else
        tx_key % TEST_NET_ID;
#endif
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

    outcome::result<std::set<std::string>> TransactionManager::ParseTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto it = transaction_parsers.find( tx->GetType() );
        if ( it == transaction_parsers.end() )
        {
            m_logger->info( "[{} - full: {}] No Parser Available",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m );
            return std::errc::invalid_argument;
        }

        return ( this->*( it->second.first ) )( tx );
    }

    outcome::result<std::set<std::string>> TransactionManager::RevertTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto it = transaction_parsers.find( tx->GetType() );
        if ( it == transaction_parsers.end() )
        {
            m_logger->info( "[{} - full: {}] No Reverter Available",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m );
            return std::errc::invalid_argument;
        }

        return ( this->*( it->second.second ) )( tx );
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::FetchTransaction(
        const std::shared_ptr<crdt::GlobalDB> &db,
        std::string_view                       transaction_key )
    {
        OUTCOME_TRY( ( auto &&, transaction_data ), db->Get( { std::string( transaction_key ) } ) );

        return DeSerializeTransaction( transaction_data );
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::DeSerializeTransaction(
        const base::Buffer &tx_data )
    {
        auto transaction_data_vector = tx_data.toVector();

        OUTCOME_TRY( ( auto &&, dag ), IGeniusTransactions::DeSerializeDAGStruct( transaction_data_vector ) );

        //m_logger->debug( "Found the data, deserializing into DAG {}", transaction_key );

        auto it = IGeniusTransactions::GetDeSerializers().find( dag.type() );
        if ( it == IGeniusTransactions::GetDeSerializers().end() )
        {
            //m_logger->info( "Invalid transaction found. No Deserialization available for type {}", dag.type() );
            return std::errc::invalid_argument;
        }
        return it->second( transaction_data_vector );
    }

    outcome::result<bool> TransactionManager::CheckProof( const std::shared_ptr<IGeniusTransactions> &tx )
    {
#ifdef _PROOF_ENABLED
        auto proof_path = GetTransactionProofPath( *tx );
        m_logger->debug( "[{} - full: {}] Checking the proof in {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         proof_path );
        OUTCOME_TRY( ( auto &&, proof_data ), globaldb_m->Get( { proof_path } ) );

        auto proof_data_vector = proof_data.toVector();

        m_logger->debug( "[{} - full: {}] Proof data acquired. Verifying...",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m );
        //std::cout << " it has value with size  " << proof_data.size() << std::endl;
        return IBasicProof::VerifyFullProof( proof_data_vector );
#else
        return true;
#endif
    }

    outcome::result<void> TransactionManager::CheckIncoming()
    {
        m_logger->trace( "[{} - full: {}] Probing incoming transactions on {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         GetBlockChainBase() + "!" + account_m->GetAddress() + "/tx" );
        OUTCOME_TRY( ( auto &&, transaction_list ),
                     globaldb_m->QueryKeyValues( GetBlockChainBase(), "!" + account_m->GetAddress(), "/tx" ) );

        m_logger->trace( "[{} - full: {}] Incoming transaction list grabbed from CRDT with Size {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         transaction_list.size() );

        //m_logger->info( "Number of tasks in Queue: {}", queryTasks.size() );
        for ( const auto &element : transaction_list )
        {
            auto transaction_key = globaldb_m->KeyToString( element.first );
            if ( !transaction_key.has_value() )
            {
                m_logger->debug( "[{} - full: {}] Unable to convert a key to string",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m );
                continue;
            }
            if ( incoming_tx_processed_m.find( transaction_key.value() ) != incoming_tx_processed_m.end() )
            {
                m_logger->trace( "[{} - full: {}] Transaction already processed: {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 transaction_key.value() );
                continue;
            }

            m_logger->debug( "[{} - full: {}] Finding incoming transaction: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             transaction_key.value() );
            auto maybe_transaction = FetchTransaction( globaldb_m, transaction_key.value() );
            if ( !maybe_transaction.has_value() )
            {
                m_logger->debug( "[{} - full: {}] Can't fetch transaction {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 transaction_key.value() );
                continue;
            }

            m_logger->debug( "[{} - full: {}] Transaction fetched successfully: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             transaction_key.value() );

            auto maybe_parsed = ParseTransaction( maybe_transaction.value() );
            if ( maybe_parsed.has_error() )
            {
                m_logger->debug( "[{} - full: {}] Can't parse the transaction {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 transaction_key.value() );
                continue;
            }
            m_logger->debug( "[{} - full: {}] Transaction parsed successfully: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             transaction_key.value() );

            if ( full_node_m )
            {
                account_m->SetPeerConfirmedNonce( maybe_transaction.value()->dag_st.nonce(),
                                                  maybe_transaction.value()->dag_st.source_addr() );
                m_logger->debug( "[{} - full: {}] Updated peer nonce for {} to {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 maybe_transaction.value()->dag_st.source_addr().substr( 0, 8 ),
                                 maybe_transaction.value()->dag_st.nonce() );
            }
            {
                m_logger->trace( "[{} - full: {}] Inserting into incoming {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 transaction_key.value() );
                std::unique_lock<std::shared_mutex> out_lock( incoming_tx_mutex_m );
                incoming_tx_processed_m[transaction_key.value()] = TrackedTx{ maybe_transaction.value(),
                                                                              TransactionStatus::CONFIRMED };
            }
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::CheckOutgoing()
    {
        m_logger->trace( "[{} - full: {}] Probing outgoing transactions on {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         GetBlockChainBase() );
        OUTCOME_TRY( ( auto &&, transaction_list ),
                     globaldb_m->QueryKeyValues( GetBlockChainBase(), account_m->GetAddress(), "/tx" ) );

        m_logger->trace( "[{} - full: {}] Transaction list grabbed from CRDT",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m );

        //m_logger->info( "Number of tasks in Queue: {}", queryTasks.size() );
        for ( const auto &element : transaction_list )
        {
            auto transaction_key = globaldb_m->KeyToString( element.first );
            if ( !transaction_key.has_value() )
            {
                m_logger->debug( "[{} - full: {}] Unable to convert a key to string",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m );
                continue;
            }
            if ( outgoing_tx_processed_m.find( transaction_key.value() ) != outgoing_tx_processed_m.end() )
            {
                m_logger->trace( "[{} - full: {}] Transaction already processed: {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 transaction_key.value() );
                continue;
            }

            auto maybe_transaction = FetchTransaction( globaldb_m, transaction_key.value() );
            if ( !maybe_transaction.has_value() )
            {
                m_logger->debug( "[{} - full: {}] Can't fetch transaction",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m );
                continue;
            }
            m_logger->debug( "[{} - full: {}] Transaction fetched on {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             transaction_key.value() );
            auto maybe_parsed = ParseTransaction( maybe_transaction.value() );
            if ( maybe_parsed.has_error() )
            {
                m_logger->debug( "[{} - full: {}] Can't parse the transaction",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m );
                continue;
            }
            m_logger->debug( "[{} - full: {}] Transaction parsed {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             transaction_key.value() );

            account_m->SetLocalConfirmedNonce( maybe_transaction.value()->dag_st.nonce() );

            {
                m_logger->trace( "[{} - full: {}] Inserting into outgoing {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 transaction_key.value() );
                std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
                outgoing_tx_processed_m[transaction_key.value()] = TrackedTx{ maybe_transaction.value(),
                                                                              TransactionStatus::CONFIRMED };
            }
        }
        return outcome::success();
    }

    outcome::result<std::set<std::string>> TransactionManager::ParseTransferTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto transfer_tx         = std::dynamic_pointer_cast<TransferTransaction>( tx );
        auto dest_infos          = transfer_tx->GetDstInfos();
        bool notify_destinations = false;

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };
        if ( ( transfer_tx->GetSrcAddress() == account_m->GetAddress() ) || ( full_node_m ) )
        {
            notify_destinations = true;
        }

        for ( std::uint32_t i = 0; i < dest_infos.size(); ++i )
        {
            if ( dest_infos[i].dest_address == account_m->GetAddress() )
            {
                auto       hash = ( base::Hash256::fromReadableString( transfer_tx->dag_st.data_hash() ) ).value();
                GeniusUTXO new_utxo( hash, i, dest_infos[i].encrypted_amount, dest_infos[i].token_id );
                account_m->PutUTXO( new_utxo );
            }
            if ( notify_destinations )
            {
                m_logger->debug( "[{} - full: {}] Notify {} of transfer of {} to it",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 dest_infos[i].dest_address,
                                 dest_infos[i].encrypted_amount );
                topics.emplace( dest_infos[i].dest_address );
            }
        }
        if ( full_node_m )
        {
            m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             transfer_tx->GetSrcAddress() );
            topics.emplace( transfer_tx->GetSrcAddress() );
        }

        for ( auto &input : transfer_tx->GetInputInfos() )
        {
            m_logger->trace( "[{} - full: {}] UTXO to be updated {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             input.txid_hash_.toReadableString() );
            m_logger->trace( "[{} - full: {}] UTXO output {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             input.output_idx_ );
        }

        account_m->ConsumeUTXOs( transfer_tx->GetInputInfos() );
        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::ParseMintTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };

        if ( mint_tx->GetSrcAddress() == account_m->GetAddress() )
        {
            auto       hash = ( base::Hash256::fromReadableString( mint_tx->dag_st.data_hash() ) ).value();
            GeniusUTXO new_utxo( hash, 0, mint_tx->GetAmount(), mint_tx->GetTokenID() );
            account_m->PutUTXO( new_utxo );
            m_logger->info( "[{} - full: {}] Created tokens, balance {}",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m,
                            account_m->GetBalance<std::string>() );
        }
        if ( full_node_m )
        {
            m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             mint_tx->GetSrcAddress() );
            topics.emplace( mint_tx->GetSrcAddress() );
        }

        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::ParseEscrowTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };

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
                        GeniusUTXO new_utxo( hash,
                                             1,
                                             dest_infos.outputs_[1].encrypted_amount,
                                             dest_infos.outputs_[1].token_id );
                        account_m->PutUTXO( new_utxo );
                    }
                }
                account_m->ConsumeUTXOs( escrow_tx->GetUTXOParameters().inputs_ );
            }
        }
        if ( full_node_m )
        {
            m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             escrow_tx->GetSrcAddress() );
            topics.emplace( escrow_tx->GetSrcAddress() );
        }

        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::ParseEscrowReleaseTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrowReleaseTx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };
        if ( !escrowReleaseTx )
        {
            m_logger->error( "[{} - full: {}] Failed to cast transaction to EscrowReleaseTransaction",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
            return std::errc::invalid_argument;
        }
        if ( ( escrowReleaseTx->GetSrcAddress() == account_m->GetAddress() ) || ( full_node_m ) )
        {
            m_logger->debug( "[{} - full: {}] Adding Escrow source address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             escrowReleaseTx->GetEscrowSource() );
            topics.emplace( escrowReleaseTx->GetEscrowSource() );
        }

        if ( full_node_m )
        {
            m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             escrowReleaseTx->GetSrcAddress() );
            topics.emplace( escrowReleaseTx->GetSrcAddress() );
        }

        std::string originalEscrowHash = escrowReleaseTx->GetOriginalEscrowHash();
        m_logger->debug( "[{} - full: {}] Successfully fetched release for escrow: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         originalEscrowHash );

        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::RevertTransferTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto transfer_tx         = std::dynamic_pointer_cast<TransferTransaction>( tx );
        auto dest_infos          = transfer_tx->GetDstInfos();
        bool notify_destinations = false;

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };
        if ( ( transfer_tx->GetSrcAddress() == account_m->GetAddress() ) || ( full_node_m ) )
        {
            notify_destinations = true;
        }

        for ( std::uint32_t i = 0; i < dest_infos.size(); ++i )
        {
            if ( dest_infos[i].dest_address == account_m->GetAddress() )
            {
                auto hash = ( base::Hash256::fromReadableString( transfer_tx->dag_st.data_hash() ) ).value();
                account_m->DeleteUTXO( hash );
            }
            if ( notify_destinations )
            {
                m_logger->debug( "[{} - full: {}] Notify {} of deletion of {} to it",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 dest_infos[i].dest_address,
                                 dest_infos[i].encrypted_amount );
                topics.emplace( dest_infos[i].dest_address );
            }
        }
        if ( full_node_m )
        {
            m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             transfer_tx->GetSrcAddress() );
            topics.emplace( transfer_tx->GetSrcAddress() );
        }

        m_logger->debug( "[{} - full: {}] Re-parsing inputs to be added as UTXOs",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m );
        for ( auto &input : transfer_tx->GetInputInfos() )
        {
            m_logger->debug( "[{} - full: {}] Fetchin transaction {} ",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             input.txid_hash_.toReadableString() );
            auto tx = GetOutTransaction( input.txid_hash_.toReadableString() );
            if ( tx )
            {
                m_logger->debug( "[{} - full: {}] Re-parsing {} transaction",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 tx->GetType() );
                OUTCOME_TRY( ParseTransaction( tx ) );
            }
        }
        UTXOTxParameters params( transfer_tx->GetInputInfos(), transfer_tx->GetDstInfos() );
        account_m->utxos = UTXOTxParameters::RollbackUTXOs( account_m->utxos, params );

        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::RevertMintTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };

        if ( mint_tx->GetSrcAddress() == account_m->GetAddress() )
        {
            auto hash = ( base::Hash256::fromReadableString( mint_tx->dag_st.data_hash() ) ).value();
            account_m->DeleteUTXO( hash );
            m_logger->info( "[{} - full: {}] Deleted tokens, balance {}",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m,
                            account_m->GetBalance<std::string>() );
        }
        if ( full_node_m )
        {
            m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             mint_tx->GetSrcAddress() );
            topics.emplace( mint_tx->GetSrcAddress() );
        }

        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::RevertEscrowTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };

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
                        account_m->DeleteUTXO( hash );
                    }
                }
                for ( auto &input : escrow_tx->GetUTXOParameters().inputs_ )
                {
                    auto tx = GetOutTransaction( input.txid_hash_.toReadableString() );
                    if ( tx )
                    {
                        m_logger->debug( "[{} - full: {}] Re-parsing {} transaction",
                                         account_m->GetAddress().substr( 0, 8 ),
                                         full_node_m,
                                         tx->GetType() );
                        OUTCOME_TRY( ParseTransaction( tx ) );
                    }
                }
                account_m->utxos = UTXOTxParameters::RollbackUTXOs( account_m->utxos, dest_infos );
            }
        }
        if ( full_node_m )
        {
            m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             escrow_tx->GetSrcAddress() );
            topics.emplace( escrow_tx->GetSrcAddress() );
        }

        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::RevertEscrowReleaseTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrowReleaseTx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };
        if ( !escrowReleaseTx )
        {
            m_logger->error( "[{} - full: {}] Failed to cast transaction to EscrowReleaseTransaction",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
            return std::errc::invalid_argument;
        }
        if ( ( escrowReleaseTx->GetSrcAddress() == account_m->GetAddress() ) || ( full_node_m ) )
        {
            m_logger->debug( "[{} - full: {}] Adding Escrow source address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             escrowReleaseTx->GetEscrowSource() );
            topics.emplace( escrowReleaseTx->GetEscrowSource() );
        }

        if ( full_node_m )
        {
            m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             escrowReleaseTx->GetSrcAddress() );
            topics.emplace( escrowReleaseTx->GetSrcAddress() );
        }

        std::string originalEscrowHash = escrowReleaseTx->GetOriginalEscrowHash();
        m_logger->debug( "[{} - full: {}] Successfully fetched release for escrow: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         originalEscrowHash );

        return topics;
    }

    std::vector<std::vector<uint8_t>> TransactionManager::GetOutTransactions() const
    {
        std::vector<std::vector<std::uint8_t>> result;
        {
            std::shared_lock out_lock( outgoing_tx_mutex_m );
            result.reserve( outgoing_tx_processed_m.size() );
            for ( const auto &[key, value] : outgoing_tx_processed_m )
            {
                if ( value.tx )
                {
                    result.push_back( value.tx->SerializeByteVector() );
                }
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
                if ( value.tx )
                {
                    result.push_back( value.tx->SerializeByteVector() );
                }
            }
        }
        return result;
    }

    TransactionManager::TransactionStatus TransactionManager::WaitForTransactionIncoming(
        const std::string        &txId,
        std::chrono::milliseconds timeout ) const
    {
        auto                                  start  = std::chrono::steady_clock::now();
        TransactionManager::TransactionStatus retval = TransactionStatus::CREATED;

        do
        {
            std::shared_lock<std::shared_mutex> in_lock( incoming_tx_mutex_m );
            bool                                found = false;
            for ( const auto &kv : incoming_tx_processed_m )
            {
                const auto &tracked = kv.second;
                if ( tracked.tx && tracked.tx->dag_st.data_hash() == txId )
                {
                    retval = tracked.status;
                    found  = true;
                    break;
                }
            }
            if ( !found )
            {
                m_logger->debug( "[{} - full: {}] Transaction IS INVALID",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m );
                retval = TransactionStatus::INVALID;
            }

            if ( retval == TransactionStatus::INVALID || retval == TransactionStatus::CONFIRMED ||
                 retval == TransactionStatus::FAILED )
            {
                m_logger->debug( "[{} - full: {}] Transaction is FINALIZED",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m );
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        } while ( std::chrono::steady_clock::now() - start < timeout );

        return retval;
    }

    TransactionManager::TransactionStatus TransactionManager::WaitForTransactionOutgoing(
        const std::string        &txId,
        std::chrono::milliseconds timeout ) const
    {
        auto                                  start  = std::chrono::steady_clock::now();
        TransactionManager::TransactionStatus retval = TransactionStatus::CREATED;

        do
        {
            std::shared_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
            m_logger->trace( "[{} - full: {}] Searching for transaction {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             txId );
            bool found = false;
            for ( const auto &kv : outgoing_tx_processed_m )
            {
                const auto &tracked = kv.second;
                if ( tracked.tx && tracked.tx->dag_st.data_hash() == txId )
                {
                    retval = tracked.status;
                    m_logger->trace( "[{} - full: {}] Transaction status is {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     static_cast<int>( retval ) );
                    found = true;
                    break;
                }
            }
            if ( !found )
            {
                m_logger->trace( "[{} - full: {}] Transaction untracked",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m );
                retval = TransactionStatus::INVALID;
            }

            if ( retval == TransactionStatus::INVALID || retval == TransactionStatus::CONFIRMED ||
                 retval == TransactionStatus::FAILED )
            {
                m_logger->trace( "[{} - full: {}] Transaction has finalized state {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 static_cast<int>( retval ) );
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        } while ( std::chrono::steady_clock::now() - start < timeout );

        return retval;
    }

    TransactionManager::TransactionStatus TransactionManager::WaitForEscrowRelease(
        const std::string        &originalEscrowId,
        std::chrono::milliseconds timeout ) const
    {
        auto                                  start  = std::chrono::steady_clock::now();
        TransactionManager::TransactionStatus retval = TransactionStatus::INVALID;

        while ( std::chrono::steady_clock::now() - start < timeout )
        {
            std::shared_lock<std::shared_mutex> in_lock( incoming_tx_mutex_m );

            for ( const auto &kv : incoming_tx_processed_m )
            {
                const auto &tracked = kv.second;
                if ( !tracked.tx )
                {
                    continue;
                }

                if ( tracked.tx->GetType() == "escrow-release" )
                {
                    auto escrowReleaseTx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tracked.tx );
                    if ( escrowReleaseTx && escrowReleaseTx->GetOriginalEscrowHash() == originalEscrowId )
                    {
                        m_logger->debug( "[{} - full: {}] Found matching escrow release transaction with tx id: {}",
                                         account_m->GetAddress().substr( 0, 8 ),
                                         full_node_m,
                                         tracked.tx->dag_st.data_hash() );

                        retval = tracked.status;

                        // If finalized, return immediately; otherwise keep waiting.
                        if ( retval == TransactionStatus::CONFIRMED || retval == TransactionStatus::FAILED ||
                             retval == TransactionStatus::INVALID )
                        {
                            return retval;
                        }
                    }
                }
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }

        return retval; // Will be INVALID if not seen within timeout
    }

    void TransactionManager::InitNonce( uint64_t timeout_ms )
    {
        m_logger->debug( "[{} - full: {}] Trying to get confirmed nonce",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m );

        auto nonce_result = account_m->GetConfirmedNonce( timeout_ms );
        if ( nonce_result.has_value() )
        {
            auto network_confirmed_nonce = nonce_result.value();
            m_logger->debug( "[{} - full: {}] Nonce from the network received {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             network_confirmed_nonce );
            auto local_nonce_result = account_m->GetLocalConfirmedNonce();
            if ( local_nonce_result.has_value() )
            {
                m_logger->debug( "[{} - full: {}] Local nonce {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 local_nonce_result.value() );
                if ( local_nonce_result.value() >= network_confirmed_nonce )
                {
                    state_m = State::READY;
                }
            }
            else
            {
                m_logger->debug( "[{} - full: {}] Local nonce with no value",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m );
            }
        }
        else
        {
            m_logger->debug( "[{} - full: {}] No node from the network, assume we are updated",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );

            state_m = State::READY;
        }
    }

    void TransactionManager::SyncNonce()
    {
        m_logger->debug( "[{} - full: {}] Checking if my nonce is updated",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m );

        auto     nonce_result    = account_m->GetConfirmedNonce( 3000 );
        uint64_t confirmed_nonce = 0;
        if ( nonce_result.has_value() )
        {
            confirmed_nonce = nonce_result.value();
        }
        else
        {
            auto local_nonce_result = account_m->GetLocalConfirmedNonce();
            if ( local_nonce_result.has_value() )
            {
                confirmed_nonce = local_nonce_result.value();
            }
            else
            {
                return;
            }
        }
        uint64_t expected_next_nonce = confirmed_nonce + 1;
        uint64_t proposed_nonce      = account_m->GetProposedNonce();

        if ( proposed_nonce == expected_next_nonce )
        {
            //Either my old txs are outdated or
            //The responder has not updated yet
            m_logger->debug( "[{} - full: {}] Network nonce updated: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             expected_next_nonce );
            state_m = State::READY;
        }
        else if ( proposed_nonce > expected_next_nonce )
        {
            m_logger->error( "[{} - full: {}] Local nonce ahead - Local: {}, Expected: {}. Checking for invalid tx",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             proposed_nonce,
                             expected_next_nonce );
            std::set<uint64_t> nonces_to_check;
            for ( auto i = expected_next_nonce; i < proposed_nonce; ++i )
            {
                nonces_to_check.insert( i );
                m_logger->debug( "[{} - full: {}] Inserting nonce to check: {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 i );
            }

            CheckTransactionValidity( nonces_to_check );
        }
        else if ( proposed_nonce < expected_next_nonce )
        {
            m_logger->error( "[{} - full: {}] Local nonce behind - Local: {}, Expected: {}. Waiting to sync",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             proposed_nonce,
                             expected_next_nonce );
        }
    }

    bool TransactionManager::CheckTransactionValidity( std::set<uint64_t> nonces_to_check )
    {
        bool                                changed = false;
        std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
        m_logger->debug( "[{} - full: {}] CheckTransactionValidity: Checking transactions",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m );

        for ( auto &nonce : nonces_to_check )
        {
            for ( auto it = outgoing_tx_processed_m.begin(); it != outgoing_tx_processed_m.end(); )
            {
                if ( !it->second.tx )
                {
                    ++it;
                    continue;
                }

                m_logger->debug( "[{} - full: {}] CheckTransactionValidity: Seeing if transaction {} is valid {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 it->second.tx->dag_st.nonce(),
                                 nonce );

                if ( it->second.tx->dag_st.nonce() == nonce )
                {
                    bool invalid_tx = false;
                    if ( !it->second.tx->CheckSignature() )
                    {
                        if ( !it->second.tx->CheckDAGSignatureLegacy() )
                        {
                            m_logger->error(
                                "[{} - full: {}] Could not validate signature of transaction with nonce {}",
                                account_m->GetAddress().substr( 0, 8 ),
                                full_node_m,
                                nonce );
                            invalid_tx = true;
                        }
                        else
                        {
                            m_logger->debug( "[{} - full: {}] Legacy transaction validated with nonce: {}",
                                             account_m->GetAddress().substr( 0, 8 ),
                                             full_node_m,
                                             nonce );
                        }
                    }
                    else
                    {
                        m_logger->debug( "[{} - full: {}] CheckTransactionValidity: Transaction is valid with {}",
                                         account_m->GetAddress().substr( 0, 8 ),
                                         full_node_m,
                                         nonce );
                    }
                    if ( invalid_tx )
                    {
                        DeleteTransaction( it->second.tx );
                        auto previous_valid_nonce = nonce ? nonce - 1 : 0;
                        account_m->RollBackConfirmedNonce( previous_valid_nonce );
                        it->second.status = TransactionStatus::FAILED; // keep entry so waiters can see FAILED
                        ++it;
                        changed = true;
                        m_logger->debug( "[{} - full: {}] CheckTransactionValidity: INVALID TX {}",
                                         account_m->GetAddress().substr( 0, 8 ),
                                         full_node_m,
                                         nonce );
                        continue; // check possible duplicates with same nonce
                    }
                    else
                    {
                        it->second.status = TransactionStatus::CONFIRMED;
                    }
                }
                ++it;
            }
        }
        return changed;
    }

    bool TransactionManager::DeleteTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        bool ret = false;

        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction = globaldb_m->BeginTransaction();

        auto                        transaction_path = GetTransactionPath( *tx );
        sgns::crdt::HierarchicalKey tx_key( transaction_path );

        m_logger->debug( "[{} - full: {}] Deleting transaction on {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         tx_key.GetKey() );

        auto remove_result = crdt_transaction->Remove( std::move( tx_key ) );

        if ( !remove_result.has_error() )
        {
            m_logger->debug( "[{} - full: {}] Removed key transaction on {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             tx_key.GetKey() );
            auto get_topics_result = RevertTransaction( tx );
            if ( get_topics_result.has_value() )
            {
                m_logger->debug( "[{} - full: {}] Reverted tx on {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 tx_key.GetKey() );

                crdt_transaction->Commit( get_topics_result.value() );
                ret = true;
                m_logger->debug( "[{} - full: {}] Commited tx on {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 tx_key.GetKey() );
            }
        }

        return ret;
    }

    std::shared_ptr<IGeniusTransactions> TransactionManager::GetOutTransaction( const std::string &tx_hash ) const
    {
        for ( const auto &kv : outgoing_tx_processed_m )
        {
            m_logger->debug( "[{} - full: {}] Searching for hash {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             tx_hash );
            if ( kv.second.tx && kv.second.tx->dag_st.data_hash() == tx_hash )
            {
                return kv.second.tx;
            }
        }
        return nullptr;
    }

    std::shared_ptr<IGeniusTransactions> TransactionManager::GetOutTransaction( uint64_t nonce ) const
    {
        std::shared_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
        for ( const auto &kv : outgoing_tx_processed_m )
        {
            if ( kv.second.tx && kv.second.tx->dag_st.nonce() == nonce )
            {
                return kv.second.tx;
            }
        }
        return nullptr;
    }

    TransactionManager::State TransactionManager::GetState() const
    {
        return state_m;
    }

    TransactionManager::TransactionStatus TransactionManager::GetOutgoingStatusByTxId( const std::string &txId ) const
    {
        std::shared_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
        for ( const auto &kv : outgoing_tx_processed_m )
        {
            const auto &t = kv.second.tx;
            if ( t && t->dag_st.data_hash() == txId )
            {
                return kv.second.status;
            }
        }
        return TransactionStatus::INVALID;
    }

    TransactionManager::TransactionStatus TransactionManager::GetIncomingStatusByTxId( const std::string &txId ) const
    {
        std::shared_lock<std::shared_mutex> in_lock( incoming_tx_mutex_m );
        for ( const auto &kv : incoming_tx_processed_m )
        {
            const auto &t = kv.second.tx;
            if ( t && t->dag_st.data_hash() == txId )
            {
                return kv.second.status;
            }
        }
        return TransactionStatus::INVALID;
    }

    bool TransactionManager::SetOutgoingStatusByNonce( uint64_t nonce, TransactionStatus s )
    {
        std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
        for ( auto &kv : outgoing_tx_processed_m )
        {
            auto &tracked = kv.second;
            if ( tracked.tx && tracked.tx->dag_st.nonce() == nonce )
            {
                tracked.status = s;
                m_logger->debug( "[{} - full: {}] Set tx {} (nonce {}) to {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 tracked.tx->dag_st.data_hash(),
                                 nonce,
                                 static_cast<int>( s ) );
                return true;
            }
        }
        m_logger->debug( "[{} - full: {}] No outgoing tx found with nonce {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         nonce );
        return false;
    }

    outcome::result<void> TransactionManager::ConfirmTransactions()
    {
        std::vector<std::string> to_confirm;

        // First pass: find VERIFYING transactions and store their keys
        {
            std::shared_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
            for ( const auto &pair : outgoing_tx_processed_m )
            {
                if ( pair.second.status == TransactionStatus::VERIFYING )
                {
                    to_confirm.push_back( pair.first );
                }
            }
        }

        // If nothing to confirm, skip
        if ( to_confirm.empty() )
        {
            m_logger->trace( "[{} - full: {}] No VERIFYING transactions, skipping nonce check in ConfirmTransactions",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
            return outcome::success();
        }

        // Fetch confirmed nonce only if we have VERIFYING transactions
        auto nonce_result = account_m->GetConfirmedNonce( 4000 );
        if ( !nonce_result.has_value() )
        {
            m_logger->debug( "[{} - full: {}] Can't fetch nonce from the network in ConfirmTransactions",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
            return outcome::failure( boost::system::error_code{} );
        }

        uint64_t confirmed_nonce = nonce_result.value();
        m_logger->debug( "[{} - full: {}] Confirmed nonce from network: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         confirmed_nonce );

        // Second pass: update only the ones we collected
        {
            std::unique_lock<std::shared_mutex> out_lock( outgoing_tx_mutex_m );
            for ( const auto &key : to_confirm )
            {
                auto it = outgoing_tx_processed_m.find( key );
                if ( it != outgoing_tx_processed_m.end() && it->second.tx->dag_st.nonce() <= confirmed_nonce )
                {
                    it->second.status = TransactionStatus::CONFIRMED;
                    m_logger->debug( "[{} - full: {}] Transaction {} set to CONFIRMED",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     key );
                }
            }
        }

        return outcome::success();
    }

    std::optional<std::vector<crdt::pb::Element>> TransactionManager::FilterTransaction(
        const crdt::pb::Element &element )
    {
        std::optional<std::vector<crdt::pb::Element>> maybe_tombstones;
        bool                                          valid_tx = false;
        std::shared_ptr<IGeniusTransactions>          tx;
        do
        {
            //TODO - This verification is only needed because CRDT resyncs every boot up
            // Remove once we remove the in memory processed_cids on crdt_datastore and use dagsyncher again
            auto maybe_has_value = globaldb_m->Get( element.key() );
            if ( maybe_has_value.has_value() )
            {
                m_logger->debug( "[{} - full: {}] Already have the transaction {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 element.key() );
                valid_tx = true;
                break;
            }
            auto maybe_tx = DeSerializeTransaction( element.value() );
            if ( maybe_tx.has_error() )
            {
                break;
            }
            tx = maybe_tx.value();
            if ( !tx->CheckSignature() )
            {
                if ( !tx->CheckDAGSignatureLegacy() )
                {
                    m_logger->error( "[{} - full: {}] Could not validate signature of transaction {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     element.key() );
                    break;
                }
                else
                {
                    m_logger->debug( "[{} - full: {}] Legacy transaction validated: {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     element.key() );
                }
            }

            m_logger->trace( "[{} - full: {}] Valid signature of {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             element.key() );
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
    }

    std::optional<std::vector<crdt::pb::Element>> TransactionManager::FilterProof( const crdt::pb::Element &element )
    {
        std::optional<std::vector<crdt::pb::Element>> maybe_tombstones;
        bool                                          valid_proof = false;
        do
        {
            //TODO - This verification is only needed because CRDT resyncs every boot up
            // Remove once we remove the in memory processed_cids on crdt_datastore and use dagsyncher again
            auto maybe_has_value = globaldb_m->Get( element.key() );
            if ( maybe_has_value.has_value() )
            {
                m_logger->debug( "[{} - full: {}] Already have the proof {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 element.key() );
                valid_proof = true;
                break;
            }
            valid_proof = true;
            break;
            std::vector<uint8_t> proof_data_vector( element.value().begin(), element.value().end() );
            auto                 maybe_valid_proof = IBasicProof::VerifyFullProof( proof_data_vector );
            if ( maybe_valid_proof.has_error() || ( !maybe_valid_proof.value() ) )
            {
                // TODO: kill reputation point of the node.
                m_logger->error( "[{} - full: {}] Could not verify proof {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 element.key() );
                break;
            }
            m_logger->trace( "[{} - full: {}] Valid proof of {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             element.key() );

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
    }

}
