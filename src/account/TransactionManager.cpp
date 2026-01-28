/**
 * @file       TransactionManager.cpp
 * @brief
 * @date       2024-04-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <boost/asio/post.hpp>

#include "account/TransactionManager.hpp"

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
#include "account/TokenAmount.hpp"
#include "account/AccountMessenger.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crdt/proto/delta.pb.h"
#include "base/sgns_version.hpp"

#include "proof/TransferProof.hpp"
#include "proof/ProcessingProof.hpp"

namespace sgns
{
    std::shared_ptr<TransactionManager> TransactionManager::New( std::shared_ptr<crdt::GlobalDB>          processing_db,
                                                                 std::shared_ptr<boost::asio::io_context> ctx,
                                                                 UTXOManager                             &utxo_manager,
                                                                 std::shared_ptr<GeniusAccount>           account,
                                                                 std::shared_ptr<crypto::Hasher>          hasher,
                                                                 bool                                     full_node,
                                                                 std::chrono::milliseconds timestamp_tolerance,
                                                                 std::chrono::milliseconds mutability_window )
    {
        auto instance = std::shared_ptr<TransactionManager>( new TransactionManager( std::move( processing_db ),
                                                                                     std::move( ctx ),
                                                                                     utxo_manager,
                                                                                     std::move( account ),
                                                                                     std::move( hasher ),
                                                                                     full_node,
                                                                                     std::move( timestamp_tolerance ),
                                                                                     std::move( mutability_window ) ) );

        auto monitored_networks = GetMonitoredNetworkIDs();
        for ( auto network_id : monitored_networks )
        {
            std::string blockchain_base            = GetBlockChainBase( network_id );
            bool        crdt_tx_filter_initialized = instance->globaldb_m->RegisterElementFilter(
                "^/?" + blockchain_base + "tx/[^/]+",
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
                "^/?" + blockchain_base + "proof/[^/]+",
                [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                    const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
                {
                    if ( auto strong = weak_ptr.lock() )
                    {
                        return strong->FilterProof( element );
                    }
                    return std::nullopt;
                } );

            (void)instance->globaldb_m->RegisterNewElementCallback(
                "^/?" + blockchain_base + "tx/[^/]+",
                [weak_ptr( std::weak_ptr<TransactionManager>(
                    instance ) )]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
                {
                    if ( auto strong = weak_ptr.lock() )
                    {
                        strong->NewElementCallback( std::move( new_data ) );
                    }
                } );
            (void)instance->globaldb_m->RegisterDeletedElementCallback(
                "^/?" + blockchain_base + "tx/[^/]+",
                [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )]( std::string        deleted_key,
                                                                             const std::string &cid )
                {
                    if ( auto strong = weak_ptr.lock() )
                    {
                        strong->DeleteElementCallback( std::move( deleted_key ) );
                    }
                } );
        }

        return instance;
    }

    TransactionManager::TransactionManager( std::shared_ptr<crdt::GlobalDB>          processing_db,
                                            std::shared_ptr<boost::asio::io_context> ctx,
                                            UTXOManager                             &utxo_manager,
                                            std::shared_ptr<GeniusAccount>           account,
                                            std::shared_ptr<crypto::Hasher>          hasher,
                                            bool                                     full_node,
                                            std::chrono::milliseconds                timestamp_tolerance,
                                            std::chrono::milliseconds                mutability_window ) :
        globaldb_m( std::move( processing_db ) ),
        ctx_m( std::move( ctx ) ),
        account_m( std::move( account ) ),
        utxo_manager_( utxo_manager ),
        hasher_m( std::move( hasher ) ),
        full_node_m( std::move( full_node ) ),
        state_m( State::CREATING ),
        last_periodic_sync_time_( std::chrono::steady_clock::now() ),
        timestamp_tolerance_m( std::move( timestamp_tolerance ) ),
        mutability_window_m( std::move( mutability_window ) ),
        last_loop_time_( std::chrono::steady_clock::now() )

    {
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
        // Notify condition variable to wake up waiting thread
        cv_.notify_all();
    }

    void TransactionManager::Start()
    {
        if ( GetState() != State::CREATING || stopped_.load() )
        {
            return;
        }

        m_logger->info( "[{} - full: {}] Initializing values by reading whole blockchain",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m );

        full_node_topic_m = std::string( GNUS_FULL_NODES_TOPIC );

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

        ChangeState( State::INITIALIZING );

        if ( !stopped_.load() )
        {
            CheckTransactions();
        }
        // First kick: keep self alive during the first dispatch only
        if ( !stopped_.load() )
        {
            boost::asio::post( *ctx_m, [self = shared_from_this()]() { self->TickOnce(); } );
        }
    }

    // One “tick”: do work, then schedule the next tick via weak capture
    void TransactionManager::TickOnce()
    {
        if ( stopped_.load() )
        {
            return;
        }

        auto now                  = std::chrono::steady_clock::now();
        auto time_since_last_loop = std::chrono::duration_cast<std::chrono::milliseconds>( now - last_loop_time_ )
                                        .count();
        last_loop_time_ = now;

        std::vector<std::string> elements_to_delete;
        {
            std::lock_guard<std::mutex> queue_lock( deleted_data_queue_mutex_ );
            while ( !deleted_data_queue_.empty() )
            {
                elements_to_delete.push_back( std::move( deleted_data_queue_.front() ) );
                deleted_data_queue_.pop();
            }
        }
        std::vector<crdt::CRDTCallbackManager::NewDataPair> elements_to_process;
        {
            std::lock_guard<std::mutex> queue_lock( new_data_queue_mutex_ );
            while ( !new_data_queue_.empty() )
            {
                elements_to_process.push_back( std::move( new_data_queue_.front() ) );
                new_data_queue_.pop();
            }
        }

        for ( auto &deletion_key : elements_to_delete )
        {
            m_logger->debug( "[{} - full: {}] Deleting key: {} ",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             deletion_key );
            ProcessDeletion( deletion_key );
        }
        for ( auto &new_data : elements_to_process )
        {
            m_logger->debug( "[{} - full: {}] Adding key: {} ",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             new_data.first );
            ProcessNewData( new_data );
        }

        m_logger->trace( "[{} - full: {}] Loop iteration - time since last: {}ms",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         time_since_last_loop );

        switch ( GetState() )
        {
            case State::INITIALIZING:
                InitNonce( 8000 );
                if ( GetState() == State::READY )
                {
                    m_logger->debug( "[{} - full: {}] Transaction Manager is now READY - starting regular updates",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m );
                }
                break;

            case State::CREATING: // Should not happen, but handle gracefully
                break;

            case State::SYNCING:
                this->SyncNonce();
                break;

            case State::READY:
            {
                std::unique_lock lock( mutex_m );
                if ( tx_queue_m.empty() )
                {
                    break;
                }

                auto send_result = SendTransactionItem( tx_queue_m.front() );
                if ( send_result.has_error() )
                {
                    // Immediately switch to SYNCING so no new transactions are created while we roll back.
                    ChangeState( State::SYNCING );

                    m_logger->error( "[{} - full: {}] Error in SendTransactionItem: {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     send_result.error().message(),
                                     full_node_m );

                    auto rollback_result = RollbackTransactions( tx_queue_m.front() );
                    if ( rollback_result.has_error() )
                    {
                        m_logger->error( "[{} - full: {}] RollbackTransactions error, couldn't fetch nonce",
                                         account_m->GetAddress().substr( 0, 8 ),
                                         full_node_m );
                        break;
                    }

                    // Check if error was due to network timeout - if so, keep transaction in queue for retry
                    // when full node becomes available
                    if ( send_result.error() == boost::system::errc::make_error_code( boost::system::errc::timed_out ) )
                    {
                        m_logger->info( "[{} - full: {}] Network timeout - keeping transaction in queue for retry",
                                        account_m->GetAddress().substr( 0, 8 ),
                                        full_node_m );
                        // Don't pop - transaction stays in queue for retry when we return to READY
                    }
                    else
                    {
                        // Other errors (like invalid_argument from nonce mismatch) - remove from queue
                        tx_queue_m.pop_front();
                    }
                    break;
                }
                auto nonces_sent = send_result.value();
                for ( auto nonce : nonces_sent )
                {
                    m_logger->debug( "[{} - full: {}] Confirming local nonce to {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     nonce );
                    account_m->SetLocalConfirmedNonce( nonce );
                }
                tx_queue_m.pop_front();
                lock.unlock();
            }
            break;
        }

        auto confirm_result = ConfirmTransactions();
        if ( confirm_result.has_error() )
        {
            m_logger->trace( "[{} - full: {}] Unknown ConfirmTransactions error",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
        }

        // Periodic sync - request heads every 10 minutes to stay synchronized across devices/instances
        // Use 30 second interval until we get first response, then switch to 10 minutes
        bool should_sync = false;
        if ( !received_first_periodic_sync_response_.load() )
        {
            auto time_since_last_sync = std::chrono::duration_cast<std::chrono::seconds>( now -
                                                                                          last_periodic_sync_time_ );
            should_sync               = time_since_last_sync >= INITIAL_PERIODIC_SYNC_INTERVAL;
        }
        else
        {
            auto time_since_last_sync = std::chrono::duration_cast<std::chrono::minutes>( now -
                                                                                          last_periodic_sync_time_ );
            should_sync               = time_since_last_sync >= PERIODIC_SYNC_INTERVAL;
        }

        if ( should_sync )
        {
            auto interval_desc = received_first_periodic_sync_response_.load() ? "10 minutes" : "30 seconds";
            m_logger->debug( "[{} - full: {}] Periodic sync - requesting heads (interval: {})",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             interval_desc );
            auto topics_result = globaldb_m->GetMonitoredTopics();
            if ( topics_result.has_value() )
            {
                if ( account_m->RequestHeads( topics_result.value() ) )
                {
                    last_periodic_sync_time_ = now;
                    m_logger->debug( "[{} - full: {}] Periodic sync head request sent for {} topics",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     topics_result.value().size() );
                }
                else
                {
                    m_logger->warn( "[{} - full: {}] Periodic sync head request failed",
                                    account_m->GetAddress().substr( 0, 8 ),
                                    full_node_m );
                }
            }
            else
            {
                m_logger->warn( "[{} - full: {}] Could not get monitored topics for head request",
                                account_m->GetAddress().substr( 0, 8 ),
                                full_node_m );
            }
        }

        // Wait with condition variable instead of timer
        // Wait with condition variable - wake up on notification OR timeout
        std::unique_lock<std::mutex> lock( cv_mutex_ );
        cv_.wait_for( lock,
                      std::chrono::milliseconds( 300 ),
                      [this]()
                      {
                          bool new_data     = false;
                          bool deleted_data = false;
                          {
                              std::lock_guard new_data_queue_lock( new_data_queue_mutex_ );
                              new_data = !new_data_queue_.empty();
                          }
                          {
                              std::lock_guard delete_data_queue_lock( deleted_data_queue_mutex_ );
                              deleted_data = !deleted_data_queue_.empty();
                          }
                          return stopped_.load() || new_data || deleted_data;
                      } );

        // Schedule next tick if not stopped
        if ( !stopped_.load() )
        {
            boost::asio::post( *ctx_m,
                               [weak_instance = weak_from_this()]()
                               {
                                   if ( auto instance = weak_instance.lock() )
                                   {
                                       if ( !instance->stopped_.load() )
                                       {
                                           instance->TickOnce();
                                       }
                                   }
                               } );
        }
    }

    void TransactionManager::PrintAccountInfo()
    {
        std::cout << "Account Address: " << account_m->GetAddress() << std::endl;
        std::cout << "Balance: " << std::to_string( utxo_manager_.GetBalance() ) << std::endl;
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
        if ( GetState() != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        OUTCOME_TRY( auto &&params, utxo_manager_.CreateTxParameter( amount, destination, token_id ) );
        auto [inputs, outputs] = params;

        //TODO params.SignParameters( account_m );

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            TransferTransaction::New( inputs, outputs, FillDAGStruct() ) );

        transfer_transaction->MakeSignature( *account_m );

        utxo_manager_.ReserveUTXOs( inputs );

        EnqueueTransaction( std::make_pair( transfer_transaction, std::nullopt ) );

        return transfer_transaction->GetHash();
    }

    outcome::result<std::string> TransactionManager::MintFunds( uint64_t    amount,
                                                                std::string transaction_hash,
                                                                std::string chainid,
                                                                TokenID     tokenid )
    {
        if ( GetState() != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto mint_transaction = std::make_shared<MintTransaction>(
            MintTransaction::New( amount,
                                  std::move( chainid ),
                                  std::move( tokenid ),
                                  FillDAGStruct( std::move( transaction_hash ) ) ) );

        mint_transaction->MakeSignature( *account_m );

        // Store the transaction ID before moving the transaction
        auto txId = mint_transaction->GetHash();

        EnqueueTransaction( std::make_pair( std::move( mint_transaction ), std::nullopt ) );

        return txId;
    }

    outcome::result<std::pair<std::string, EscrowDataPair>> TransactionManager::HoldEscrow( uint64_t           amount,
                                                                                            const std::string &dev_addr,
                                                                                            uint64_t peers_cut,
                                                                                            const std::string &job_id )
    {
        if ( GetState() != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto hash_data = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );

        OUTCOME_TRY( ( auto &&, params ),
                     utxo_manager_.CreateTxParameter( amount,
                                                      "0x" + hash_data.toReadableString(),
                                                      TokenID::FromBytes( { 0x00 } ) ) );
        auto [inputs, outputs] = params;
        //TODO params.SignParameters( account_m );
        utxo_manager_.ReserveUTXOs( inputs );

        auto escrow_transaction = std::make_shared<EscrowTransaction>(
            EscrowTransaction::New( params, amount, dev_addr, peers_cut, FillDAGStruct() ) );

        escrow_transaction->MakeSignature( *account_m );

        // Get the transaction ID for tracking
        auto txId = escrow_transaction->GetHash();

        EnqueueTransaction( std::make_pair( escrow_transaction, std::nullopt ) );

        sgns::crdt::GlobalDB::Buffer data_transaction;
        data_transaction.put( escrow_transaction->SerializeByteVector() );

        // Return both the transaction ID and the original EscrowDataPair
        return std::make_pair( txId,
                               std::make_pair( "0x" + hash_data.toReadableString(), std::move( data_transaction ) ) );
    }

    outcome::result<std::string> TransactionManager::PayEscrow(
        const std::string                       &escrow_path,
        const SGProcessing::TaskResult          &task_result,
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction )
    {
        if ( task_result.subtask_results().size() == 0 )
        {
            m_logger->error( "[{} - full: {}] No result found on escrow {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             escrow_path );
            return outcome::failure( boost::system::error_code{} );
        }
        if ( escrow_path.empty() )
        {
            m_logger->error( "[{} - full: {}] Escrow path empty", account_m->GetAddress().substr( 0, 8 ), full_node_m );
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

        const auto escrowTokenId = escrow_tx->GetUTXOParameters().second[0].token_id;

        uint64_t peers_amount = peer_total.Value() / static_cast<uint64_t>( task_result.subtask_results().size() );
        auto     remainder    = escrow_tx->GetAmount();

        for ( auto &subtask : task_result.subtask_results() )
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

        escrow_utxo_input.txid_hash_  = ( base::Hash256::fromReadableString( escrow_tx->GetHash() ) ).value();
        escrow_utxo_input.output_idx_ = 0;
        escrow_utxo_input.signature_  = ""; //TODO - Signature

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            TransferTransaction::New( std::vector{ escrow_utxo_input }, payout_peers, FillDAGStruct() ) );

        auto escrow_release_tx = std::make_shared<EscrowReleaseTransaction>(
            EscrowReleaseTransaction::New( escrow_tx->GetUTXOParameters(),
                                           escrow_tx->GetAmount(),
                                           escrow_tx->GetDevAddress(),
                                           escrow_tx->dag_st.source_addr(),
                                           escrow_tx->GetHash(),
                                           FillDAGStruct() ) );

        TransactionBatch tx_batch;

        transfer_transaction->MakeSignature( *account_m );
        escrow_release_tx->MakeSignature( *account_m );

        tx_batch.push_back( std::make_pair( transfer_transaction, std::nullopt ) );
        tx_batch.push_back( std::make_pair( escrow_release_tx, std::nullopt ) );

        EnqueueTransaction( std::make_pair( tx_batch, std::move( crdt_transaction ) ) );
        return transfer_transaction->GetHash();
    }

    void TransactionManager::Update()
    {
        auto check_result = CheckTransactions();
        if ( check_result.has_error() )
        {
            m_logger->error( "[{} - full: {}] Unknown CheckTransactions error in Update()",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
        }
        auto confirm_result = ConfirmTransactions();
        if ( confirm_result.has_error() )
        {
            m_logger->trace( "[{} - full: {}] Unknown ConfirmTransactions error in Update()",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
        }
    }

    void TransactionManager::EnqueueTransaction( TransactionItem element )
    {
        m_logger->debug( "[{} - full: {}] Transaction enqueuing", account_m->GetAddress().substr( 0, 8 ), full_node_m );
        {
            std::unique_lock tx_lock( tx_mutex_m );
            for ( auto &tx_pair : element.first )
            {
                const auto &tx    = tx_pair.first;
                const auto  key   = GetTransactionPath( *tx );
                const auto  nonce = tx->dag_st.nonce();
                // tx visible to status queries immediately
                tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::CREATED, nonce };
                m_logger->debug( "[{} - full: {}] Setting {} to CREATED",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 tx->GetHash() );
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
        dag.set_nonce( account_m->ReserveNextNonce() );
        dag.set_source_addr( account_m->GetAddress() );
        dag.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );
        dag.set_uncle_hash( "" );
        dag.set_data_hash( "" ); //filled by transaction class

        return dag;
    }

    outcome::result<std::set<uint64_t>> TransactionManager::SendTransactionItem( TransactionItem &item )
    {
        std::set<uint64_t> nonces_set;
        auto [transaction_batch, maybe_crdt_transaction]          = item;
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction = nullptr;

        m_logger->info( "SendTransactionItem Happening" );

        if ( maybe_crdt_transaction.has_value() && maybe_crdt_transaction.value() )
        {
            crdt_transaction = std::move( maybe_crdt_transaction.value() );
        }
        else
        {
            crdt_transaction = globaldb_m->BeginTransaction();
        }
        auto     nonce_result        = account_m->GetConfirmedNonce( NONCE_REQUEST_TIMEOUT_MS );
        uint64_t expected_next_nonce = 0;
        int64_t  confirmed_nonce     = -1;

        if ( nonce_result.has_value() )
        {
            confirmed_nonce = static_cast<int64_t>( nonce_result.value() );
            m_logger->debug( "[{} - full: {}] Set nonce to {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             confirmed_nonce );
            expected_next_nonce = static_cast<uint64_t>( confirmed_nonce ) + 1;
        }
        else if ( ( !nonce_result.has_value() ) &&
                  ( nonce_result.error() == AccountMessenger::Error::NO_RESPONSE_RECEIVED ) && ( !full_node_m ) )
        {
            m_logger->error( "[{} - full: {}] {}: Network unreachable when fetching nonce",
                             __func__,
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
        }
        else if ( ( !nonce_result.has_value() ) &&
                  ( nonce_result.error() == AccountMessenger::Error::NO_RESPONSE_RECEIVED ) && ( full_node_m ) )
        {
            m_logger->warn( "[{} - full: {}] Could not fetch nonce, but proceeding since full node",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m );
            auto local_confirmed = account_m->GetLocalConfirmedNonce();
            if ( local_confirmed.has_value() )
            {
                confirmed_nonce = static_cast<int64_t>( local_confirmed.value() );

                m_logger->debug( "[{} - full: {}] Using local confirmed nonce {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 local_confirmed.value() );
                expected_next_nonce = static_cast<uint64_t>( confirmed_nonce ) + 1;
            }
        }

        bool valid_batch = true;

        for ( auto &transaction_pair : transaction_batch )
        {
            auto [transaction, maybe_proof] = transaction_pair;

            if ( transaction->dag_st.nonce() == expected_next_nonce )
            {
                auto                         transaction_path = GetTransactionPath( *transaction );
                sgns::crdt::HierarchicalKey  tx_key( transaction_path );
                sgns::crdt::GlobalDB::Buffer data_transaction;

                m_logger->debug( "[{} - full: {}] Recording the transaction on {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 tx_key.GetKey() );

                data_transaction.put( transaction->SerializeByteVector() );
                BOOST_OUTCOME_TRYV2( auto &&,
                                     crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

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
                    BOOST_OUTCOME_TRYV2(
                        auto &&,
                        crdt_transaction->Put( std::move( proof_key ), std::move( proof_transaction ) ) );
                }
                nonces_set.insert( transaction->dag_st.nonce() );
            }
            else
            {
                m_logger->error( "[{} - full: {}] Transaction with unexpected nonce - Expected: {}, Tried to send: {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 expected_next_nonce,
                                 transaction->dag_st.nonce() );

                return outcome::failure(
                    boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
            }
            expected_next_nonce++;
        }

        std::set<std::string> topicSet;
        for ( auto &transaction_pair : transaction_batch )
        {
            OUTCOME_TRY( auto &&parsedTopics, ParseTransaction( transaction_pair.first ) );
            topicSet.insert( parsedTopics.begin(), parsedTopics.end() );
            {
                std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m );
                const auto                          key      = GetTransactionPath( *transaction_pair.first );
                const auto                          nonce    = transaction_pair.first->dag_st.nonce();
                auto                                it       = tx_processed_m.find( key );
                auto                                tx_state = TransactionStatus::VERIFYING;
                if ( full_node_m )
                {
                    tx_state = TransactionStatus::CONFIRMED;
                }
                if ( it != tx_processed_m.end() )
                {
                    if ( it->second.status != tx_state && tx_state == TransactionStatus::VERIFYING )
                    {
                        verifying_count_.fetch_add( 1, std::memory_order_relaxed );
                    }
                    it->second.status = tx_state;
                }
                else
                {
                    tx_processed_m[key] = TrackedTx{ transaction_pair.first, tx_state, nonce };
                    if ( tx_state == TransactionStatus::VERIFYING )
                    {
                        verifying_count_.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            }
        }

        BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Commit( topicSet ) );

        return nonces_set;
    }

    outcome::result<void> TransactionManager::RollbackTransactions( TransactionItem &item_to_rollback )
    {
        int64_t confirmed_nonce = -1;

        if ( auto nonce_result = account_m->GetConfirmedNonce( NONCE_REQUEST_TIMEOUT_MS ); nonce_result.has_value() )
        {
            confirmed_nonce = static_cast<int64_t>( nonce_result.value() );
            m_logger->debug( "[{} - full: {}] Set nonce to {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             confirmed_nonce );
        }
        else
        {
            m_logger->error( "[{} - full: {}] {}: Could not fetch confirmed nonce ({}). Attempting rollback with "
                             "local state",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             __func__,
                             nonce_result.error().message() );
            auto local_nonce_result = account_m->GetLocalConfirmedNonce();
            if ( local_nonce_result.has_value() )
            {
                confirmed_nonce = static_cast<int64_t>( local_nonce_result.value() );
                m_logger->debug( "[{} - full: {}] Falling back to local confirmed nonce {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 confirmed_nonce );
            }
            else
            {
                m_logger->error( "[{} - full: {}] No local confirmed nonce available, rolling back assuming none",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m );
                confirmed_nonce = -1;
            }
        }

        auto [transaction_batch, _dontcare] = item_to_rollback;

        for ( auto &transaction_pair : transaction_batch )
        {
            auto [transaction, __dontcare] = transaction_pair;

            auto signed_previous_nonce = static_cast<int64_t>( transaction->dag_st.nonce() ) - 1;

            for ( auto tx_nonce = signed_previous_nonce; tx_nonce > confirmed_nonce; --tx_nonce )
            {
                //let's verify if we didn't mistakenly confirm any bad transactions.
                m_logger->debug( "[{} - full: {}] Setting \"VERIFYING\" status to transaction with nonce {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 tx_nonce );
                (void)SetOutgoingStatusByNonce( static_cast<uint64_t>( tx_nonce ), TransactionStatus::VERIFYING );
            }
            {
                std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m );
                const auto                          key   = GetTransactionPath( *transaction );
                const auto                          nonce = transaction->dag_st.nonce();

                if ( auto it = tx_processed_m.find( key ); it != tx_processed_m.end() )
                {
                    // Update verifying_count if status is changing from VERIFYING
                    if ( it->second.status == TransactionStatus::VERIFYING )
                    {
                        verifying_count_.fetch_sub( 1, std::memory_order_relaxed );
                    }
                    it->second.tx           = transaction;
                    it->second.status       = TransactionStatus::FAILED;
                    it->second.cached_nonce = nonce;
                }
                else
                {
                    // New entry rolled back: start directly as FAILED
                    tx_processed_m.emplace( key, TrackedTx{ transaction, TransactionStatus::FAILED, nonce } );
                }
            }
            RemoveTransactionFromProcessedMaps( GetTransactionPath( *transaction ) );
            account_m->ReleaseNonce( transaction->dag_st.nonce() );
        }
        return outcome::success();
    }

    std::string TransactionManager::GetTransactionPath( IGeniusTransactions &element )
    {
        auto transaction_path = GetBlockChainBase() + element.GetTransactionFullPath();

        return transaction_path;
    }

    std::string TransactionManager::GetTransactionPath( const std::string &tx_hash )
    {
        return GetBlockChainBase() + IGeniusTransactions::GetTransactionFullPath( tx_hash );
    }

    std::string TransactionManager::GetTransactionProofPath( IGeniusTransactions &element )
    {
        auto proof_path = GetBlockChainBase() + element.GetProofFullPath();

        return proof_path;
    }

    std::string TransactionManager::GetTransactionBasePath( const std::string &address )
    {
        (void)address;
        auto tx_base_path = GetBlockChainBase() + "tx";

        return tx_base_path;
    }

    std::vector<uint16_t> TransactionManager::GetMonitoredNetworkIDs()
    {
        std::vector<uint16_t> monitored_networks{ version::GetNetworkID() };
        if ( version::GetNetworkID() == version::DEV_NET_ID ) // DEV network
        {
            monitored_networks.push_back( version::TEST_NET_ID );
            monitored_networks.push_back( version::MAIN_NET_ID );
        }
        return monitored_networks;
    }

    std::string TransactionManager::GetBlockChainBase( uint16_t network_id )
    {
        boost::format tx_key{ std::string( TRANSACTION_BASE_FORMAT ) };

        tx_key % network_id;

        return tx_key.str();
    }

    std::string TransactionManager::GetBlockChainBase()
    {
        return GetBlockChainBase( version::GetNetworkID() );
    }

    outcome::result<std::string> TransactionManager::GetExpectedProofKey(
        const std::string                          &tx_key,
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        if ( tx )
        {
            return GetTransactionProofPath( *tx );
        }

        const auto tx_pos = tx_key.find( "/tx/" );
        if ( tx_pos == std::string::npos )
        {
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
        }

        std::string proof_key = tx_key;
        proof_key.replace( tx_pos, 4, "/proof/" );

        if ( proof_key.size() <= tx_pos + 7 )
        {
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
        }

        return proof_key;
    }

    outcome::result<std::string> TransactionManager::GetExpectedTxKey( const std::string &proof_key )
    {
        const auto proof_pos = proof_key.find( "/proof/" );
        if ( proof_pos == std::string::npos )
        {
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
        }

        std::string tx_key = proof_key;
        tx_key.replace( proof_pos, 7, "/tx/" );

        if ( tx_key.size() <= proof_pos + 4 )
        {
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
        }

        return tx_key;
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

        auto it = IGeniusTransactions::GetDeSerializers().find( dag.type() );
        if ( it == IGeniusTransactions::GetDeSerializers().end() )
        {
            return std::errc::invalid_argument;
        }
        return it->second( transaction_data_vector );
    }

    outcome::result<bool> TransactionManager::CheckProof( const std::shared_ptr<IGeniusTransactions> &tx )
    {
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
        return IBasicProof::VerifyFullProof( proof_data_vector );
    }

    outcome::result<void> TransactionManager::CheckTransactions()
    {
        auto monitored_networks = GetMonitoredNetworkIDs();

        for ( auto network_id : monitored_networks )
        {
            std::string blockchain_base = GetBlockChainBase( network_id );
            std::string query_path      = blockchain_base + "tx";
            m_logger->trace( "[{} - full: {}] Probing transactions on {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             query_path );
            OUTCOME_TRY( ( auto &&, transaction_list ), globaldb_m->QueryKeyValues( query_path ) );

            m_logger->trace( "[{} - full: {}] Transaction list grabbed from CRDT with Size {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             transaction_list.size() );

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
                {
                    std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
                    if ( tx_processed_m.find( transaction_key.value() ) != tx_processed_m.end() )
                    {
                        m_logger->trace( "[{} - full: {}] Transaction already processed: {}",
                                         account_m->GetAddress().substr( 0, 8 ),
                                         full_node_m,
                                         transaction_key.value() );
                        continue;
                    }
                }
                m_logger->debug( "[{} - full: {}] Finding transaction: {}",
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

                if ( maybe_transaction.value()->GetHash().empty() )
                {
                    m_logger->error( "[{} - full: {}] Error, received transaction without hash: {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     transaction_key.value() );
                    continue;
                    //maybe_transaction.value()->FillHash();
                }

                auto maybe_parsed = ParseTransaction( maybe_transaction.value() );
                if ( maybe_parsed.has_error() )
                {
                    m_logger->debug( "[{} - full: {}] Can't parse the transaction {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     transaction_key.value() );
                    continue;
                }

                const auto nonce = maybe_transaction.value()->dag_st.nonce();

                account_m->SetPeerConfirmedNonce( nonce, maybe_transaction.value()->dag_st.source_addr() );

                {
                    std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m );
                    tx_processed_m[transaction_key.value()] = TrackedTx{ maybe_transaction.value(),
                                                                         TransactionStatus::CONFIRMED,
                                                                         nonce };
                }
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
            auto       hash = ( base::Hash256::fromReadableString( transfer_tx->GetHash() ) ).value();
            GeniusUTXO new_utxo( hash, i, dest_infos[i].encrypted_amount, dest_infos[i].token_id );
            utxo_manager_.PutUTXO( new_utxo, dest_infos[i].dest_address );

            m_logger->debug( "[{} - full: {}] Notify {} of transfer of {} to it",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             dest_infos[i].dest_address,
                             dest_infos[i].encrypted_amount );
            topics.emplace( dest_infos[i].dest_address );
        }

        m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         transfer_tx->GetSrcAddress() );
        topics.emplace( transfer_tx->GetSrcAddress() );

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

        utxo_manager_.ConsumeUTXOs( transfer_tx->GetInputInfos(), transfer_tx->GetSrcAddress() );
        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::ParseMintTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };

        auto       hash = ( base::Hash256::fromReadableString( mint_tx->GetHash() ) ).value();
        GeniusUTXO new_utxo( hash, 0, mint_tx->GetAmount(), mint_tx->GetTokenID() );
        utxo_manager_.PutUTXO( new_utxo, mint_tx->GetSrcAddress() );
        m_logger->info( "[{} - full: {}] Created tokens, amount {} balance {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        std::to_string( mint_tx->GetAmount() ),
                        std::to_string( utxo_manager_.GetBalance() ) );

        m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         mint_tx->GetSrcAddress() );
        topics.emplace( mint_tx->GetSrcAddress() );

        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::ParseEscrowTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };

        if ( escrow_tx->GetSrcAddress() == account_m->GetAddress() )
        {
            auto [_, outputs] = escrow_tx->GetUTXOParameters();

            if ( !outputs.empty() )
            {
                //The first is the escrow, second is the change (might not happen)
                auto hash = ( base::Hash256::fromReadableString( escrow_tx->GetHash() ) ).value();
                if ( outputs.size() > 1 )
                {
                    GeniusUTXO new_utxo( hash, 1, outputs[1].encrypted_amount, outputs[1].token_id );
                    utxo_manager_.PutUTXO( new_utxo, outputs[1].dest_address );
                }
                utxo_manager_.ConsumeUTXOs( escrow_tx->GetUTXOParameters().first, escrow_tx->GetSrcAddress() );
            }
        }

        m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         escrow_tx->GetSrcAddress() );
        topics.emplace( escrow_tx->GetSrcAddress() );

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

        m_logger->debug( "[{} - full: {}] Adding Escrow source address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         escrowReleaseTx->GetEscrowSource() );
        topics.emplace( escrowReleaseTx->GetEscrowSource() );

        m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         escrowReleaseTx->GetSrcAddress() );
        topics.emplace( escrowReleaseTx->GetSrcAddress() );

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

        for ( const auto &dest_info : dest_infos )
        {
            auto hash = ( base::Hash256::fromReadableString( transfer_tx->GetHash() ) ).value();
            utxo_manager_.DeleteUTXO( hash, dest_info.dest_address );

            m_logger->debug( "[{} - full: {}] Notify {} of deletion of {} to it",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             dest_info.dest_address,
                             dest_info.encrypted_amount );
            topics.emplace( dest_info.dest_address );
        }

        m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         transfer_tx->GetSrcAddress() );
        topics.emplace( transfer_tx->GetSrcAddress() );

        m_logger->debug( "[{} - full: {}] Re-parsing inputs to be added as UTXOs",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m );
        for ( const auto &input : transfer_tx->GetInputInfos() )
        {
            m_logger->debug( "[{} - full: {}] Fetching transaction {} ",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             input.txid_hash_.toReadableString() );
            auto tx = GetTransactionByHashNoLock( input.txid_hash_.toReadableString() );
            if ( tx )
            {
                m_logger->debug( "[{} - full: {}] Re-parsing {} transaction",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 tx->GetType() );
                OUTCOME_TRY( ParseTransaction( tx ) );
            }
        }
        utxo_manager_.RollbackUTXOs( transfer_tx->GetInputInfos() );

        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::RevertMintTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };

        auto hash = ( base::Hash256::fromReadableString( mint_tx->GetHash() ) ).value();
        utxo_manager_.DeleteUTXO( hash, mint_tx->GetSrcAddress() );
        m_logger->info( "[{} - full: {}] Deleted {} tokens, from tx {}, final balance {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        mint_tx->GetAmount(),
                        mint_tx->GetHash(),
                        std::to_string( utxo_manager_.GetBalance() ) );

        m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         mint_tx->GetSrcAddress() );
        topics.emplace( mint_tx->GetSrcAddress() );

        return topics;
    }

    outcome::result<std::set<std::string>> TransactionManager::RevertEscrowTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );

        std::set<std::string> topics{ full_node_topic_m, account_m->GetAddress() };

        if ( escrow_tx->GetSrcAddress() == account_m->GetAddress() )
        {
            if ( auto [inputs, outputs] = escrow_tx->GetUTXOParameters(); !outputs.empty() )
            {
                //The first is the escrow, second is the change (might not happen)
                auto hash = ( base::Hash256::fromReadableString( escrow_tx->GetHash() ) ).value();
                if ( outputs.size() > 1 )
                {
                    utxo_manager_.DeleteUTXO( hash, outputs[1].dest_address );
                }
                for ( auto &input : inputs )
                {
                    auto tx = GetTransactionByHashNoLock( input.txid_hash_.toReadableString() );
                    if ( tx )
                    {
                        m_logger->debug( "[{} - full: {}] Re-parsing {} transaction",
                                         account_m->GetAddress().substr( 0, 8 ),
                                         full_node_m,
                                         tx->GetType() );
                        OUTCOME_TRY( ParseTransaction( tx ) );
                    }
                }
                utxo_manager_.RollbackUTXOs( inputs );
            }
        }

        m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         escrow_tx->GetSrcAddress() );
        topics.emplace( escrow_tx->GetSrcAddress() );

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

        m_logger->debug( "[{} - full: {}] Adding Escrow source address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         escrowReleaseTx->GetEscrowSource() );
        topics.emplace( escrowReleaseTx->GetEscrowSource() );

        m_logger->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         escrowReleaseTx->GetSrcAddress() );
        topics.emplace( escrowReleaseTx->GetSrcAddress() );

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
            std::shared_lock tx_lock( tx_mutex_m );
            result.reserve( tx_processed_m.size() );
            for ( const auto &[key, value] : tx_processed_m )
            {
                if ( value.tx && value.tx->GetSrcAddress() == account_m->GetAddress() )
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
            std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            result.reserve( tx_processed_m.size() );
            for ( const auto &[key, value] : tx_processed_m )
            {
                if ( value.tx && value.tx->GetSrcAddress() != account_m->GetAddress() )
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
        TransactionManager::TransactionStatus retval = TransactionStatus::FAILED;

        do
        {
            std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            bool                                found = false;
            for ( const auto &kv : tx_processed_m )
            {
                const auto &tracked = kv.second;
                if ( tracked.tx && tracked.tx->GetHash() == txId &&
                     tracked.tx->GetSrcAddress() != account_m->GetAddress() )
                {
                    retval = tracked.status;
                    found  = true;
                    break;
                }
            }

            if ( retval == TransactionStatus::CONFIRMED )
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
            std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            m_logger->trace( "[{} - full: {}] Searching for transaction {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             txId );
            bool found = false;
            for ( const auto &kv : tx_processed_m )
            {
                const auto &tracked = kv.second;
                if ( tracked.tx && tracked.tx->GetHash() == txId &&
                     tracked.tx->GetSrcAddress() == account_m->GetAddress() )
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
                retval = TransactionStatus::FAILED;
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
            std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );

            for ( const auto &kv : tx_processed_m )
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
                                         tracked.tx->GetHash() );

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

        auto nonce_result = account_m->GetConfirmedNonce( NONCE_REQUEST_TIMEOUT_MS );
        if ( nonce_result.has_value() )
        {
            auto network_confirmed_nonce = nonce_result.value();
            m_logger->debug( "[{} - full: {}] Nonce from the network received {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             network_confirmed_nonce );
            bool synched = true;
            for ( uint64_t i = 1; i <= network_confirmed_nonce; ++i )
            {
                auto tx = GetTransactionByNonceAndAddress( i, account_m->GetAddress() );
                if ( !tx )
                {
                    synched = false;
                    m_logger->debug( "[{} - full: {}] Missing transaction with nonce {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     i );
                    break;
                }
            }
            if ( synched )
            {
                ChangeState( State::READY );
            }
            else
            {
                // We're missing transactions - request heads to help sync faster
                uint64_t gap = network_confirmed_nonce - account_m->GetProposedNonce() + 1;
                m_logger->info( "[{} - full: {}] Missing transactions during init, gap: {}",
                                account_m->GetAddress().substr( 0, 8 ),
                                full_node_m,
                                gap );
                RequestRelevantHeads();
            }
        }
        else
        {
            //TODO - Have the full node respond it doesn't have it to check for connectivity
            m_logger->debug( "[{} - full: {}] No node from the network, assume we are updated",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
            ChangeState( State::READY );
        }
    }

    void TransactionManager::SyncNonce()
    {
        m_logger->debug( "[{} - full: {}] Checking if my nonce is updated",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m );

        auto     nonce_result    = account_m->GetConfirmedNonce( NONCE_REQUEST_TIMEOUT_MS );
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
            ChangeState( State::READY );
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

            (void)CheckTransactionValidity( nonces_to_check );
        }
        else if ( proposed_nonce < expected_next_nonce )
        {
            uint64_t nonce_gap = expected_next_nonce - proposed_nonce;
            m_logger->error( "[{} - full: {}] Local nonce behind - Local: {}, Expected: {}. Gap: {}. Waiting to sync",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             proposed_nonce,
                             expected_next_nonce,
                             nonce_gap );

            // If we're behind at all, we need to catch up - even a gap of 1 means
            // there's transaction data in CRDT that we don't have, and we cannot
            // safely propose new transactions until we're caught up
            constexpr uint64_t SIGNIFICANT_GAP_THRESHOLD = 1;
            if ( nonce_gap >= SIGNIFICANT_GAP_THRESHOLD )
            {
                RequestRelevantHeads();
            }
        }
    }

    void TransactionManager::RequestRelevantHeads()
    {
        // Rate limiting: don't request more than once per 30 seconds
        auto now = std::chrono::steady_clock::now();
        if ( last_head_request_time_.has_value() )
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>( now - last_head_request_time_.value() );
            if ( elapsed.count() < 30 )
            {
                m_logger->trace( "[{} - full: {}] Skipping head request - too soon since last request ({}s ago)",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 elapsed.count() );
                return;
            }
        }

        auto topics_result = globaldb_m->GetMonitoredTopics();
        if ( !topics_result.has_value() )
        {
            m_logger->warn( "[{} - full: {}] Could not get monitored topics for head request",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m );
            return;
        }
        m_logger->info( "[{} - full: {}] Requesting heads for {} topics",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        topics_result.value().size() );

        if ( account_m->RequestHeads( topics_result.value() ) )
        {
            last_head_request_time_ = now;
            m_logger->debug( "[{} - full: {}] Periodic sync head request sent for {} topics",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             topics_result.value().size() );
        }
        else
        {
            m_logger->warn( "[{} - full: {}] Failed to request heads",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m );
        }
    }

    outcome::result<bool> TransactionManager::CheckTransactionValidity( const std::set<uint64_t> &nonces_to_check )
    {
        bool                     changed = false;
        std::vector<std::string> invalid_transaction_keys;
        {
            std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            m_logger->debug( "[{} - full: {}] {}: Checking transactions",
                             __func__,
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );

            for ( auto &nonce : nonces_to_check )
            {
                for ( auto it : tx_processed_m )
                {
                    if ( !it.second.tx || it.second.tx->GetSrcAddress() != account_m->GetAddress() )
                    {
                        continue;
                    }

                    m_logger->debug( "[{} - full: {}] {}: Seeing if transaction {} is valid {}",
                                     __func__,
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     it.second.tx->dag_st.nonce(),
                                     nonce );

                    if ( it.second.tx->dag_st.nonce() == nonce )
                    {
                        bool valid_tx = true;
                        if ( !it.second.tx->CheckSignature() )
                        {
                            if ( !it.second.tx->CheckDAGSignatureLegacy() )
                            {
                                m_logger->error(
                                    "[{} - full: {}] Could not validate signature of transaction with nonce {}",
                                    account_m->GetAddress().substr( 0, 8 ),
                                    full_node_m,
                                    nonce );
                                valid_tx = false;
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
                            m_logger->debug( "[{} - full: {}] {}: Transaction is valid with {}",
                                             __func__,
                                             account_m->GetAddress().substr( 0, 8 ),
                                             full_node_m,
                                             nonce );
                        }
                        if ( !valid_tx )
                        {
                            // Collect the key for later removal
                            invalid_transaction_keys.push_back( it.first );
                            changed = true;
                            m_logger->debug( "[{} - full: {}] {}: INVALID TX {}",
                                             __func__,
                                             account_m->GetAddress().substr( 0, 8 ),
                                             full_node_m,
                                             nonce );
                        }
                        else
                        {
                            it.second.status = TransactionStatus::CONFIRMED;
                        }
                    }
                }
            }
        }

        for ( auto it = invalid_transaction_keys.rbegin(); it != invalid_transaction_keys.rend(); ++it )
        {
            RemoveTransactionFromProcessedMaps( *it, true );
        }
        return changed;
    }

    outcome::result<void> TransactionManager::DeleteTransaction( std::string                  tx_key,
                                                                 const std::set<std::string> &topics )
    {
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction = globaldb_m->BeginTransaction();

        m_logger->debug( "[{} - full: {}] Deleting transaction on {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         tx_key );

        OUTCOME_TRY( crdt_transaction->Remove( { std::move( tx_key ) } ) );

        m_logger->debug( "[{} - full: {}] Removed key transaction on {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         tx_key );

        OUTCOME_TRY( crdt_transaction->Commit( topics ) );

        m_logger->debug( "[{} - full: {}] Commited tx on {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         tx_key );

        return outcome::success();
    }

    std::shared_ptr<IGeniusTransactions> TransactionManager::GetTransactionByHash( const std::string &tx_hash ) const
    {
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        return GetTransactionByHashNoLock( tx_hash );
    }

    std::shared_ptr<IGeniusTransactions> TransactionManager::GetTransactionByHashNoLock(
        const std::string &tx_hash ) const
    {
        for ( const auto &kv : tx_processed_m )
        {
            m_logger->debug( "[{} - full: {}] Searching for hash {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             tx_hash );
            if ( kv.second.tx && kv.second.tx->GetHash() == tx_hash &&
                 kv.second.tx->GetSrcAddress() == account_m->GetAddress() )
            {
                return kv.second.tx;
            }
        }
        return nullptr;
    }

    std::shared_ptr<IGeniusTransactions> TransactionManager::GetTransactionByNonceAndAddress(
        uint64_t           nonce,
        const std::string &address ) const
    {
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( const auto &kv : tx_processed_m )
        {
            if ( kv.second.tx && ( kv.second.tx->dag_st.nonce() == nonce ) &&
                 ( kv.second.tx->GetSrcAddress() == address ) )
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
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( const auto &kv : tx_processed_m )
        {
            const auto &t = kv.second.tx;
            if ( t && t->GetHash() == txId && t->GetSrcAddress() == account_m->GetAddress() )
            {
                return kv.second.status;
            }
        }
        return TransactionStatus::INVALID;
    }

    TransactionManager::TransactionStatus TransactionManager::GetIncomingStatusByTxId( const std::string &txId ) const
    {
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( const auto &kv : tx_processed_m )
        {
            const auto &t = kv.second.tx;
            if ( t && t->GetHash() == txId && t->GetSrcAddress() != account_m->GetAddress() )
            {
                return kv.second.status;
            }
        }
        return TransactionStatus::INVALID;
    }

    bool TransactionManager::SetOutgoingStatusByNonce( uint64_t nonce, TransactionStatus s )
    {
        std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( auto &kv : tx_processed_m )
        {
            if ( !kv.second.tx )
            {
                continue;
            }
            if ( kv.second.tx->GetSrcAddress() != account_m->GetAddress() )
            {
                continue;
            }
            if ( kv.second.tx->dag_st.nonce() != nonce )
            {
                continue;
            }

            auto old_status  = kv.second.status;
            kv.second.status = s;

            // Update verifying_count
            if ( old_status == TransactionStatus::VERIFYING && s != TransactionStatus::VERIFYING )
            {
                verifying_count_.fetch_sub( 1, std::memory_order_relaxed );
            }
            else if ( old_status != TransactionStatus::VERIFYING && s == TransactionStatus::VERIFYING )
            {
                verifying_count_.fetch_add( 1, std::memory_order_relaxed );
            }

            m_logger->debug( "[{} - full: {}] Set tx {} (nonce {}) to {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             kv.second.tx->GetHash(),
                             nonce,
                             static_cast<int>( s ) );
            return true;
        }
        m_logger->debug( "[{} - full: {}] No outgoing tx found with nonce {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         nonce );
        return false;
    }

    outcome::result<void> TransactionManager::ConfirmTransactions()
    {
        // Fast path: check if there are any VERIFYING transactions
        if ( verifying_count_.load( std::memory_order_relaxed ) == 0 )
        {
            m_logger->trace( "[{} - full: {}] No VERIFYING transactions, skipping nonce check in ConfirmTransactions",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
            return outcome::success();
        }

        // Collect nonces of VERIFYING transactions using index
        std::vector<uint64_t> verifying_nonces;
        {
            std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            for ( const auto &kv : tx_processed_m )
            {
                if ( !kv.second.tx )
                {
                    continue;
                }
                if ( kv.second.tx->GetSrcAddress() != account_m->GetAddress() )
                {
                    continue;
                }
                if ( kv.second.status == TransactionStatus::VERIFYING )
                {
                    verifying_nonces.push_back( kv.second.tx->dag_st.nonce() );
                }
            }
        }

        // If nothing to confirm after lock, skip
        if ( verifying_nonces.empty() )
        {
            m_logger->trace( "[{} - full: {}] No VERIFYING transactions after lock check",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m );
            return outcome::success();
        }

        // Fetch confirmed nonce only if we have VERIFYING transactions
        auto nonce_result = account_m->GetConfirmedNonce( NONCE_REQUEST_TIMEOUT_MS );
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

        // Use nonce index for O(1) lookup and update
        {
            std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            for ( uint64_t nonce : verifying_nonces )
            {
                if ( nonce <= confirmed_nonce )
                {
                    for ( auto &kv : tx_processed_m )
                    {
                        if ( !kv.second.tx )
                        {
                            continue;
                        }
                        if ( kv.second.tx->GetSrcAddress() != account_m->GetAddress() )
                        {
                            continue;
                        }
                        if ( kv.second.tx->dag_st.nonce() != nonce )
                        {
                            continue;
                        }
                        if ( kv.second.status == TransactionStatus::VERIFYING )
                        {
                            kv.second.status = TransactionStatus::CONFIRMED;
                            verifying_count_.fetch_sub( 1, std::memory_order_relaxed );
                            m_logger->debug( "[{} - full: {}] Transaction {} (nonce {}) set to CONFIRMED",
                                             account_m->GetAddress().substr( 0, 8 ),
                                             full_node_m,
                                             kv.first,
                                             nonce );
                        }
                    }
                }
            }
        }

        return outcome::success();
    }

    std::optional<std::vector<crdt::pb::Element>> TransactionManager::FilterTransaction(
        const crdt::pb::Element &element )
    {
        std::optional<std::vector<crdt::pb::Element>> maybe_tombstones;
        bool                                          should_delete = false;
        std::shared_ptr<IGeniusTransactions>          new_tx;
        do
        {
            auto maybe_new_tx = DeSerializeTransaction( element.value() );
            if ( maybe_new_tx.has_error() )
            {
                m_logger->error( "[{} - full: {}] Failed to deserialize incoming transaction {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 element.key() );
                should_delete = true;
                break;
            }
            new_tx = maybe_new_tx.value();

            if ( !new_tx->CheckSignature() )
            {
                if ( !new_tx->CheckDAGSignatureLegacy() )
                {
                    m_logger->error( "[{} - full: {}] Could not validate signature of transaction {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     element.key() );
                    should_delete = true;
                    break;
                }
                m_logger->debug( "[{} - full: {}] Legacy transaction validated: {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 element.key() );
            }
            std::shared_ptr<IGeniusTransactions> conflicting_tx;

            auto conflicting_tx_res = GetConflictingTransaction( *new_tx );
            if ( !conflicting_tx_res.has_value() )
            {
                //maybe it's not been processed yet, but it's on CRDT
                auto maybe_existing_value = globaldb_m->Get( element.key() );
                if ( !maybe_existing_value.has_value() )
                {
                    m_logger->trace( "[{} - full: {}] No existing transaction, accepting new transaction {}",
                                     account_m->GetAddress().substr( 0, 8 ),
                                     full_node_m,
                                     element.key() );

                    break;
                }
                m_logger->debug(
                    "[{} - full: {}] Found transaction with the same key {}, checking mutability window and timestamps",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    element.key() );

                conflicting_tx_res = DeSerializeTransaction( maybe_existing_value.value() );
                if ( conflicting_tx_res.has_error() )
                {
                    m_logger->warn( "[{} - full: {}] Failed to deserialize existing transaction {}, accepting new one",
                                    account_m->GetAddress().substr( 0, 8 ),
                                    full_node_m,
                                    element.key() );
                    break;
                }
            }
            conflicting_tx = std::move( conflicting_tx_res.value() );

            m_logger->debug( "[{} - full: {}] Checking if new tx {} is the correct one",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             new_tx->GetHash() );

            should_delete = !ShouldReplaceTransaction( *conflicting_tx, *new_tx );

        } while ( 0 );

        if ( should_delete )
        {
            std::vector<crdt::pb::Element> additional_elements_to_delete;
            auto                           maybe_proof_key = GetExpectedProofKey( element.key(), new_tx );
            if ( maybe_proof_key.has_value() )
            {
                crdt::pb::Element proof_element;
                proof_element.set_key( maybe_proof_key.value() );
                additional_elements_to_delete.push_back( proof_element );
            }

            maybe_tombstones = additional_elements_to_delete;
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

        if ( !valid_proof )
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

    bool TransactionManager::ShouldReplaceTransaction( const IGeniusTransactions &existing_tx,
                                                       const IGeniusTransactions &new_tx ) const
    {
        // First check if the existing transaction is immutable
        if ( existing_tx.GetHash() == new_tx.GetHash() )
        {
            m_logger->info( "[{} - full: {}] Already have the same transaction, rejecting replacement attempt",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m );
            return false;
        }
        if ( IsTransactionImmutable( existing_tx ) )
        {
            m_logger->info( "[{} - full: {}] Existing transaction is immutable, rejecting replacement attempt",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m );
            return false;
        }

        // Get timestamps and elapsed times
        auto existing_timestamp = existing_tx.GetTimestamp();
        auto new_timestamp      = new_tx.GetTimestamp();
        auto time_diff          = GetElapsedTime( new_timestamp, existing_timestamp ); // preserve original semantics

        // If new tx is earlier than existing (time_diff > 0) allow replacement.
        // If timestamp_tolerance_m > 0 enforce the tolerance window; otherwise only the sign of time_diff is considered.
        if ( time_diff > 0 )
        {
            if ( timestamp_tolerance_m.count() == 0 )
            {
                m_logger->debug(
                    "[{} - full: {}] Timestamp tolerance disabled — new tx earlier (diff {} ms): allowing replacement",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    time_diff );
                return true;
            }

            if ( time_diff < timestamp_tolerance_m.count() )
            {
                m_logger->debug(
                    "[{} - full: {}] Timestamps within tolerance ({} ms). Existing: {} , New: {} , Diff: {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    timestamp_tolerance_m.count(),
                    existing_timestamp,
                    new_timestamp,
                    time_diff );

                m_logger->info( "[{} - full: {}] New transaction is earlier (ts: {} vs {}), will replace existing",
                                account_m->GetAddress().substr( 0, 8 ),
                                full_node_m,
                                new_timestamp,
                                existing_timestamp );
                return true;
            }
        }

        m_logger->warn(
            "[{} - full: {}] New transaction not eligible for replacement. Existing: {} , New: {} , Diff: {} ms, Tolerance: {} ms",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            existing_timestamp,
            new_timestamp,
            time_diff,
            timestamp_tolerance_m.count() );
        return false;
    }

    uint64_t TransactionManager::GetCurrentTimestamp() const
    {
        // Get current time in milliseconds since epoch
        auto now      = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>( duration ).count();
    }

    int64_t TransactionManager::GetElapsedTime( uint64_t timestamp, uint64_t current_timestamp ) const
    {
        // Calculate elapsed time (can be negative if timestamp is in the future)
        int64_t elapsed = static_cast<int64_t>( current_timestamp ) - static_cast<int64_t>( timestamp );

        if ( elapsed < 0 )
        {
            m_logger->debug( "[{} - full: {}] Transaction timestamp {} is in the future (current: {}), elapsed: {} ms",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             timestamp,
                             current_timestamp,
                             elapsed );
        }
        else
        {
            m_logger->trace( "[{} - full: {}] Transaction timestamp {} elapsed: {} ms",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             timestamp,
                             elapsed );
        }

        return elapsed;
    }

    int64_t TransactionManager::GetElapsedTime( uint64_t timestamp ) const
    {
        return GetElapsedTime( timestamp, GetCurrentTimestamp() );
    }

    bool TransactionManager::IsTransactionImmutable( const IGeniusTransactions &tx ) const
    {
        // mutability window of zero => always mutable
        if ( mutability_window_m.count() == 0 )
        {
            return false;
        }

        auto tx_timestamp = tx.GetTimestamp();
        auto elapsed      = GetElapsedTime( tx_timestamp );

        // If elapsed is negative, the transaction is from the future - not immutable
        if ( elapsed < 0 )
        {
            m_logger->debug( "[{} - full: {}] Transaction from future is not immutable (elapsed: {} ms)",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             elapsed );
            return false;
        }

        bool is_immutable = elapsed > mutability_window_m.count();

        if ( is_immutable )
        {
            m_logger->debug( "[{} - full: {}] Transaction is immutable (elapsed: {} ms, window: {} ms)",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             elapsed,
                             mutability_window_m.count() );
        }
        else
        {
            m_logger->trace( "[{} - full: {}] Transaction is still mutable (elapsed: {} ms, window: {} ms)",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             elapsed,
                             mutability_window_m.count() );
        }

        return is_immutable;
    }

    void TransactionManager::SetTimeFrameToleranceMs( uint64_t timeframe_tolerance )
    {
        timestamp_tolerance_m = std::chrono::milliseconds( timeframe_tolerance );

        m_logger->info( "[{} - full: {}] Updated timeframe tolerance to {} ms",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        timeframe_tolerance );
    }

    void TransactionManager::SetMutabilityWindowMs( uint64_t mutability_window )
    {
        mutability_window_m = std::chrono::milliseconds( mutability_window );

        m_logger->info( "[{} - full: {}] Updated mutability window to {} ms",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        mutability_window );
    }

    outcome::result<void> TransactionManager::RemoveTransactionFromProcessedMaps( const std::string &transaction_key,
                                                                                  bool               delete_from_crdt )
    {
        m_logger->debug( "[{} - full: {}] Removing transaction from processed maps: {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         transaction_key );
        bool found = false;
        {
            std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            auto                                it = tx_processed_m.find( transaction_key );
            if ( it != tx_processed_m.end() )
            {
                m_logger->debug( "[{} - full: {}] Removing from processed: {}",
                                 account_m->GetAddress().substr( 0, 8 ),
                                 full_node_m,
                                 transaction_key );

                if ( it->second.tx )
                {
                    OUTCOME_TRY( auto &&topics, RevertTransaction( it->second.tx ) );
                    if ( delete_from_crdt )
                    {
                        OUTCOME_TRY( DeleteTransaction( transaction_key, topics ) );
                    }
                    account_m->RollBackPeerConfirmedNonce( it->second.tx->dag_st.nonce(),
                                                           it->second.tx->dag_st.source_addr() );
                    if ( it->second.status == TransactionStatus::VERIFYING )
                    {
                        verifying_count_.fetch_sub( 1, std::memory_order_relaxed );
                    }
                }
                tx_processed_m.erase( it );
                found = true;
            }
        }

        if ( !found )
        {
            m_logger->debug( "[{} - full: {}] Transaction not found in processed maps: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             transaction_key );
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::AddTransactionToProcessedMaps(
        crdt::CRDTCallbackManager::NewDataPair new_data )
    {
        auto [key, value] = new_data;

        m_logger->debug( "[{} - full: {}] Trying to deserialize {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         key );

        OUTCOME_TRY( auto &&new_tx, DeSerializeTransaction( value ) );

        m_logger->debug( "[{} - full: {}] Deserialized transaction {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         key );
        bool should_add_transaction = false;
        auto tx_hash                = new_tx->GetHash();
        if ( tx_hash.empty() )
        {
            //new_tx->FillHash();
            //tx_hash = new_tx->GetHash();
            m_logger->error( "[{} - full: {}] Empty hash on {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             key );
            return outcome::failure( boost::system::error_code{} );
        }
        m_logger->debug( "[{} - full: {}] Checking if we already have this transaction {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         key );

        std::unique_lock tx_lock( tx_mutex_m );
        auto             it = tx_processed_m.find( key );

        if ( it != tx_processed_m.end() )
        {
            m_logger->debug( "[{} - full: {}] Already have the transaction {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             key );
            return outcome::success();
        }
        m_logger->debug( "[{} - full: {}] Verifying if we have a conflicting transaction {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         key );
        tx_lock.unlock();
        auto conflicting_tx = GetConflictingTransaction( *new_tx );
        tx_lock.lock();

        if ( conflicting_tx.has_value() )
        {
            m_logger->debug( "[{} - full: {}] Found conflicting transaction with hash: {}, removing it",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             conflicting_tx.value()->GetHash() );

            const auto conflict_hash = conflicting_tx.value()->GetHash();
            tx_lock.unlock();
            OUTCOME_TRY( RemoveTransactionFromProcessedMaps( GetTransactionPath( conflict_hash ), true ) );
            tx_lock.lock();
        }

        m_logger->debug( "[{} - full: {}] Parsing new transaction {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         key );
        OUTCOME_TRY( ParseTransaction( new_tx ) );

        const auto nonce = new_tx->dag_st.nonce();

        account_m->SetPeerConfirmedNonce( nonce, new_tx->dag_st.source_addr() );

        tx_processed_m[key] = TrackedTx{ new_tx, TransactionStatus::CONFIRMED, nonce };

        return outcome::success();
    }

    void TransactionManager::ProcessDeletion( std::string key )
    {
        m_logger->debug( "[{} - full: {}] Processing deletion of {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         key );

        auto remove_res = RemoveTransactionFromProcessedMaps( key );

        if ( remove_res.has_error() )
        {
            m_logger->error( "[{} - full: {}] Error removing transaction {}: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             key,
                             remove_res.error().message() );
        }
    }

    void TransactionManager::ProcessNewData( crdt::CRDTCallbackManager::NewDataPair new_data )
    {
        m_logger->debug( "[{} - full: {}] Processing new data with key {}",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         new_data.first );

        auto add_res = AddTransactionToProcessedMaps( new_data );

        if ( add_res.has_error() )
        {
            m_logger->error( "[{} - full: {}] Error adding transaction {}: {}",
                             account_m->GetAddress().substr( 0, 8 ),
                             full_node_m,
                             new_data.first,
                             add_res.error().message() );
        }
        else
        {
            // Successfully received and processed new transaction data
            // Mark that we've received data (for periodic sync interval adjustment)
            if ( !received_first_periodic_sync_response_.load() )
            {
                received_first_periodic_sync_response_.store( true );
                m_logger->info(
                    "[{} - full: {}] First transaction data received from network, switching to 10-minute periodic sync interval",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m );
            }
        }
    }

    void TransactionManager::NewElementCallback( crdt::CRDTCallbackManager::NewDataPair new_data )
    {
        std::shared_ptr<IGeniusTransactions> new_tx;

        {
            std::lock_guard queue_lock( new_data_queue_mutex_ );
            new_data_queue_.push( new_data );
        }

        m_logger->debug( "[{} - full: {}] CRDT new data queued, {} - (queue size: {})",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         new_data.first,
                         new_data_queue_.size() );

        // Notify the condition variable to wake up the main loop
        cv_.notify_one();
    }

    void TransactionManager::DeleteElementCallback( std::string deleted_key )
    {
        std::shared_ptr<IGeniusTransactions> new_tx;

        {
            std::lock_guard queue_lock( deleted_data_queue_mutex_ );
            deleted_data_queue_.push( deleted_key );
        }

        m_logger->debug( "[{} - full: {}] CRDT deleted key queued, {} - (queue size: {})",
                         account_m->GetAddress().substr( 0, 8 ),
                         full_node_m,
                         deleted_key,
                         deleted_data_queue_.size() );

        // Notify the condition variable to wake up the main loop
        cv_.notify_one();
    }

    void TransactionManager::RegisterStateChangeCallback( StateChangeCallback callback )
    {
        std::lock_guard lock( state_change_callback_mutex_ );
        state_change_callback_ = callback;
    }

    void TransactionManager::UnregisterStateChangeCallback()
    {
        std::lock_guard lock( state_change_callback_mutex_ );
        state_change_callback_ = nullptr;
    }

    void TransactionManager::ChangeState( State new_state )
    {
        {
            std::lock_guard lock( state_change_callback_mutex_ );
            if ( state_m != new_state )
            {
                m_logger->info( "[{} - full: {}] State changed from {} to {}",
                                account_m->GetAddress().substr( 0, 8 ),
                                full_node_m,
                                static_cast<int>( state_m ),
                                static_cast<int>( new_state ) );
                auto old_state = state_m;
                state_m        = new_state;
                if ( state_change_callback_ )
                {
                    state_change_callback_( old_state, new_state );
                }
            }
        }
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::GetConflictingTransaction(
        IGeniusTransactions &element )
    {
        auto tx = GetTransactionByNonceAndAddress( element.dag_st.nonce(), element.GetSrcAddress() );
        if ( tx )
        {
            return tx;
        }

        return outcome::failure( boost::system::error_code{} );
    }
}
