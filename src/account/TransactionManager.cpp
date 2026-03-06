/**
 * @file       TransactionManager.cpp
 * @brief
 * @date       2024-04-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/TransactionManager.hpp"

#include <utility>
#include <thread>
#include <system_error>

#include <boost/asio/post.hpp>
#include <openssl/err.h>

#include <ProofSystem/EthereumKeyPairParams.hpp>
#include "TransferTransaction.hpp"
#include "MintTransaction.hpp"
#include "EscrowTransaction.hpp"
#include "EscrowReleaseTransaction.hpp"
#include "account/TokenAmount.hpp"
#include "account/AccountMessenger.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crdt/proto/delta.pb.h"
#include "base/sgns_version.hpp"

#include "proof/ProcessingProof.hpp"

namespace sgns
{
    base::Logger TransactionManagerLogger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "TransactionManager" );
    }

    std::shared_ptr<TransactionManager> TransactionManager::New( std::shared_ptr<crdt::GlobalDB>          processing_db,
                                                                 std::shared_ptr<boost::asio::io_context> ctx,
                                                                 UTXOManager                             &utxo_manager,
                                                                 std::shared_ptr<GeniusAccount>           account,
                                                                 std::shared_ptr<crypto::Hasher>          hasher,
                                                                 std::shared_ptr<Blockchain>              blockchain,
                                                                 bool                                     full_node,
                                                                 std::chrono::milliseconds timestamp_tolerance,
                                                                 std::chrono::milliseconds mutability_window )
    {
        auto instance = std::shared_ptr<TransactionManager>( new TransactionManager( std::move( processing_db ),
                                                                                     std::move( ctx ),
                                                                                     utxo_manager,
                                                                                     std::move( account ),
                                                                                     std::move( hasher ),
                                                                                     std::move( blockchain ),
                                                                                     full_node,
                                                                                     timestamp_tolerance,
                                                                                     mutability_window ) );

        instance->blockchain_->RegisterCertificateHandler(
            SubjectType::SUBJECT_NONCE,
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )]( const std::string          &subject_hash,
                                                                         const ConsensusCertificate &certificate )
            {
                (void)certificate;
                if ( auto strong = weak_ptr.lock() )
                {
                    strong->OnConsensusCertificate( subject_hash );
                }
            } );
        instance->blockchain_->RegisterSubjectHandler(
            SubjectType::SUBJECT_NONCE,
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const ConsensusManager::Subject &subject ) -> outcome::result<ConsensusManager::SubjectCheck>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->HandleNonceConsensusSubject( subject );
                }
                return outcome::failure( std::errc::owner_dead );
            } );

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
                        strong->NewElementCallback( std::move( new_data ), cid );
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

        instance->account_m->SetGetUTXOsMethod(
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const std::string &address ) -> outcome::result<std::vector<std::string>>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    std::vector<std::string> results;
                    auto                     utxos = strong->utxo_manager_.GetUTXOs( address );
                    results.reserve( utxos.size() );

                    for ( const auto &utxo : utxos )
                    {
                        results.push_back( utxo.GetTxID().toReadableString() );
                    }
                    return results;
                }
                return outcome::failure( std::errc::owner_dead );
            } );
        instance->account_m->SetGetTransactionCIDMethod(
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const std::string &tx_hash ) -> outcome::result<std::string>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->GetTransactionCID( tx_hash );
                }
                return outcome::failure( std::errc::owner_dead );
            } );

        return instance;
    }

    TransactionManager::TransactionManager( std::shared_ptr<crdt::GlobalDB>          processing_db,
                                            std::shared_ptr<boost::asio::io_context> ctx,
                                            UTXOManager                             &utxo_manager,
                                            std::shared_ptr<GeniusAccount>           account,
                                            std::shared_ptr<crypto::Hasher>          hasher,
                                            std::shared_ptr<Blockchain>              blockchain,
                                            bool                                     full_node,
                                            std::chrono::milliseconds                timestamp_tolerance,
                                            std::chrono::milliseconds                mutability_window ) :
        globaldb_m( std::move( processing_db ) ),
        ctx_m( std::move( ctx ) ),
        account_m( std::move( account ) ),
        utxo_manager_( utxo_manager ),
        hasher_m( std::move( hasher ) ),
        blockchain_( std::move( blockchain ) ),
        full_node_m( full_node ),
        state_m( State::CREATING ),
        last_periodic_sync_time_( std::chrono::steady_clock::now() ),
        timestamp_tolerance_m( timestamp_tolerance ),
        mutability_window_m( mutability_window ),
        last_loop_time_( std::chrono::steady_clock::now() )

    {
    }

    TransactionManager::~TransactionManager()
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] ~TransactionManager CALLED",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m );
        if ( globaldb_m )
        {
            auto monitored_networks = GetMonitoredNetworkIDs();
            for ( auto network_id : monitored_networks )
            {
                std::string       blockchain_base = GetBlockChainBase( network_id );
                const std::string tx_pattern      = "^/?" + blockchain_base + "tx/[^/]+";
                const std::string proof_pattern   = "^/?" + blockchain_base + "proof/[^/]+";

                globaldb_m->UnregisterNewElementCallback( tx_pattern );
                globaldb_m->UnregisterDeletedElementCallback( tx_pattern );
                globaldb_m->UnregisterElementFilter( tx_pattern );
                globaldb_m->UnregisterElementFilter( proof_pattern );
            }
        }
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

        TransactionManagerLogger()->info( "[{} - full: {}] Starting Transaction Manager",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m );

        full_node_topic_m = std::string( GNUS_FULL_NODES_TOPIC );

        globaldb_m->AddListenTopic( account_m->GetAddress() );
        TransactionManagerLogger()->info( "[{} - full: {}] Adding broadcast to full node on {}",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          full_node_topic_m );
        if ( full_node_m )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Listening full node on {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               full_node_topic_m );
            globaldb_m->AddListenTopic( full_node_topic_m );
        }

        ChangeState( State::INITIALIZING );

        if ( stopped_.load() )
        {
            return;
        }

        InitializeUTXOs();

        // First kick: keep self alive during the first dispatch only
        boost::asio::post( *ctx_m, [self = shared_from_this()]() { self->TickOnce(); } );
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
            std::lock_guard queue_lock( deleted_data_queue_mutex_ );
            while ( !deleted_data_queue_.empty() )
            {
                elements_to_delete.push_back( std::move( deleted_data_queue_.front() ) );
                deleted_data_queue_.pop();
            }
        }
        std::vector<crdt::CRDTCallbackManager::NewDataPair> elements_to_process;
        {
            std::lock_guard queue_lock( new_data_queue_mutex_ );
            while ( !new_data_queue_.empty() )
            {
                elements_to_process.push_back( std::move( new_data_queue_.front() ) );
                new_data_queue_.pop();
            }
        }

        for ( auto &deletion_key : elements_to_delete )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Deleting key: {} ",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               deletion_key );
            ProcessDeletion( deletion_key );
        }
        for ( auto &new_data : elements_to_process )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Adding key: {} ",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               new_data.first );
            ProcessNewData( new_data );
        }

        TransactionManagerLogger()->trace( "[{} - full: {}] Loop iteration - time since last: {}ms",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           time_since_last_loop );

        switch ( GetState() )
        {
            case State::INITIALIZING:
                InitTransactions();
                if ( GetState() == State::READY )
                {
                    TransactionManagerLogger()->debug(
                        "[{} - full: {}] Transaction Manager is now READY - starting regular updates",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m );
                }
                break;

            case State::CREATING: // Should not happen, but handle gracefully
                break;

            case State::SYNCING:
                SyncNonce();
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

                    TransactionManagerLogger()->error( "[{} - full: {}] Error in SendTransactionItem: {}",
                                                       account_m->GetAddress().substr( 0, 8 ),
                                                       full_node_m,
                                                       send_result.error().message() );

                    auto rollback_result = RollbackTransactions( tx_queue_m.front() );
                    if ( rollback_result.has_error() )
                    {
                        TransactionManagerLogger()->error( "[{} - full: {}] {} error, couldn't fetch nonce",
                                                           account_m->GetAddress().substr( 0, 8 ),
                                                           full_node_m,
                                                           __func__ );
                        break;
                    }

                    // Check if error was due to network timeout - if so, keep transaction in queue for retry
                    // when full node becomes available
                    if ( send_result.error() == boost::system::errc::make_error_code( boost::system::errc::timed_out ) )
                    {
                        TransactionManagerLogger()->info(
                            "[{} - full: {}] Network timeout - keeping transaction in queue for retry",
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
                tx_queue_m.pop_front();
            }
            break;
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
            TransactionManagerLogger()->debug( "[{} - full: {}] Periodic sync - requesting heads (interval: {})",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               interval_desc );
            auto topics_result = globaldb_m->GetMonitoredTopics();
            if ( topics_result.has_value() )
            {
                if ( account_m->RequestHeads( topics_result.value() ) )
                {
                    last_periodic_sync_time_ = now;
                    TransactionManagerLogger()->debug( "[{} - full: {}] Periodic sync head request sent for {} topics",
                                                       account_m->GetAddress().substr( 0, 8 ),
                                                       full_node_m,
                                                       topics_result.value().size() );
                }
                else
                {
                    TransactionManagerLogger()->warn( "[{} - full: {}] Periodic sync head request failed",
                                                      account_m->GetAddress().substr( 0, 8 ),
                                                      full_node_m );
                }
            }
            else
            {
                TransactionManagerLogger()->warn( "[{} - full: {}] Could not get monitored topics for head request",
                                                  account_m->GetAddress().substr( 0, 8 ),
                                                  full_node_m );
            }
        }

        // Wait with condition variable instead of timer
        // Wait with condition variable - wake up on notification OR timeout
        std::unique_lock lock( cv_mutex_ );
        cv_.wait_for( lock,
                      std::chrono::milliseconds( 300 ),
                      [this]
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

    void TransactionManager::PrintAccountInfo() const
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
        utxo_manager_.ReserveUTXOs( inputs );

        auto escrow_transaction = std::make_shared<EscrowTransaction>(
            EscrowTransaction::New( params, amount, dev_addr, peers_cut, FillDAGStruct() ) );

        escrow_transaction->MakeSignature( *account_m );

        // Get the transaction ID for tracking
        auto txId = escrow_transaction->GetHash();

        EnqueueTransaction( std::make_pair( escrow_transaction, std::nullopt ) );

        crdt::GlobalDB::Buffer data_transaction;
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
            TransactionManagerLogger()->error( "[{} - full: {}] No result found on escrow {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               escrow_path );
            return outcome::failure( boost::system::error_code{} );
        }
        if ( escrow_path.empty() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Escrow path empty",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );
            return outcome::failure( boost::system::error_code{} );
        }
        TransactionManagerLogger()->debug( "[{} - full: {}] Fetching escrow from processing DB at {}",
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
            TransactionManagerLogger()->debug( "[{} - full: {}] Paying out {} in {}",
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
        TransactionManagerLogger()->debug( "[{} - full: {}] Sending to dev {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           remainder );
        payout_peers.push_back( { remainder, escrow_tx->GetDevAddress(), escrowTokenId } );

        InputUTXOInfo escrow_utxo_input;
        escrow_utxo_input.txid_hash_  = base::Hash256::fromReadableString( escrow_tx->GetHash() ).value();
        escrow_utxo_input.output_idx_ = 0;
        escrow_utxo_input.signature_  = account_m->Sign( escrow_utxo_input.SerializeForSigning() );

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

    void TransactionManager::EnqueueTransaction( TransactionItem element )
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] Transaction enqueuing",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m );
        {
            for ( auto &&[tx, _] : element.first )
            {
                auto result = ChangeTransactionState( tx, TransactionStatus::CREATED );
                if ( !result )
                {
                    TransactionManagerLogger()->error( "[{} - full: {}] Failed to change transaction state for {}",
                                                       account_m->GetAddress().substr( 0, 8 ),
                                                       full_node_m,
                                                       tx->GetHash() );
                }
            }
        }
        std::lock_guard lock( mutex_m );
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

    outcome::result<void> TransactionManager::SendTransactionItem( TransactionItem &item )
    {
        auto [transaction_batch, maybe_crdt_transaction]          = item;
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction = nullptr;

        TransactionManagerLogger()->trace( "{} called", __func__ );

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
            TransactionManagerLogger()->debug( "[{} - full: {}] Set nonce to {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               confirmed_nonce );
            expected_next_nonce = static_cast<uint64_t>( confirmed_nonce ) + 1;
        }
        else if ( nonce_result.has_error() && nonce_result.error() == AccountMessenger::Error::NO_RESPONSE_RECEIVED )
        {
            if ( !full_node_m )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] {}: Network unreachable when fetching nonce",
                                                   __func__,
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m );
                return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
            }

            TransactionManagerLogger()->warn( "[{} - full: {}] Could not fetch nonce, but proceeding since full node",
                                              account_m->GetAddress().substr( 0, 8 ),
                                              full_node_m );
            if ( auto local_confirmed = account_m->GetLocalConfirmedNonce(); local_confirmed.has_value() )
            {
                confirmed_nonce = static_cast<int64_t>( local_confirmed.value() );

                TransactionManagerLogger()->debug( "[{} - full: {}] Using local confirmed nonce {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   local_confirmed.value() );
                expected_next_nonce = static_cast<uint64_t>( confirmed_nonce ) + 1;
            }
        }
        std::unordered_set<std::string>                topicSet;
        std::set<std::shared_ptr<IGeniusTransactions>> transactions_sent;
        if ( !transaction_batch.empty() )
        {
            topicSet.emplace( full_node_topic_m );
            topicSet.emplace( account_m->GetAddress() );
        }

        for ( auto &[transaction, maybe_proof] : transaction_batch )
        {
            if ( transaction->GetNonce() != expected_next_nonce )
            {
                TransactionManagerLogger()->error(
                    "[{} - full: {}] Transaction with unexpected nonce - Expected: {}, Tried to send: {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    expected_next_nonce,
                    transaction->GetNonce() );

                return outcome::failure(
                    boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
            }

            auto                   transaction_path = GetTransactionPath( *transaction );
            crdt::HierarchicalKey  tx_key( transaction_path );
            crdt::GlobalDB::Buffer data_transaction;

            TransactionManagerLogger()->debug( "[{} - full: {}] Recording the transaction on {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               tx_key.GetKey() );

            data_transaction.put( transaction->SerializeByteVector() );
            BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

            if ( maybe_proof )
            {
                crdt::HierarchicalKey  proof_key( GetTransactionProofPath( *transaction ) );
                crdt::GlobalDB::Buffer proof_transaction;

                auto &proof = maybe_proof.value();
                TransactionManagerLogger()->debug( "[{} - full: {}] Recording the proof on {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   proof_key.GetKey() );

                proof_transaction.put( proof );
                BOOST_OUTCOME_TRYV2( auto &&,
                                     crdt_transaction->Put( std::move( proof_key ), std::move( proof_transaction ) ) );
            }
            TransactionManagerLogger()->debug( "[{} - full: {}] Creating Consensus Proposal for tx {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               transaction_path );

            topicSet.merge( transaction->GetTopics() );
            transactions_sent.insert( transaction );

            expected_next_nonce++;
        }

        OUTCOME_TRY( crdt_transaction->Commit( topicSet ) );

        for ( auto &transaction : transactions_sent )
        {
            OUTCOME_TRY( auto &&proposal,
                         blockchain_->CreateConsensusProposal( transaction->GetSrcAddress(),
                                                               transaction->GetNonce(),
                                                               transaction->GetHash() ) );
            OUTCOME_TRY( blockchain_->SubmitProposal( proposal ) );

            OUTCOME_TRY( ChangeTransactionState( transaction, TransactionStatus::SENDING ) );
        }

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RollbackTransactions( TransactionItem &item_to_rollback )
    {
        auto [transaction_batch, _] = item_to_rollback;
        for ( auto &[transaction, maybe_proof] : transaction_batch )
        {
            OUTCOME_TRY( ChangeTransactionState( transaction, TransactionStatus::FAILED ) );
        }
        return outcome::success();
    }

    std::string TransactionManager::GetTransactionPath( uint16_t base, const std::string &tx_hash )
    {
        return GetBlockChainBase( base ) + IGeniusTransactions::GetTransactionFullPath( tx_hash );
    }

    std::string TransactionManager::GetTransactionPath( const IGeniusTransactions &element )
    {
        return GetBlockChainBase() + element.GetTransactionFullPath();
    }

    std::string TransactionManager::GetTransactionPath( const std::string &tx_hash )
    {
        return GetBlockChainBase() + IGeniusTransactions::GetTransactionFullPath( tx_hash );
    }

    std::string TransactionManager::GetTransactionProofPath( const IGeniusTransactions &element )
    {
        auto proof_path = GetBlockChainBase() + element.GetProofFullPath();

        return proof_path;
    }

    std::vector<uint16_t> TransactionManager::GetMonitoredNetworkIDs()
    {
        std::vector monitored_networks{ version::GetNetworkID() };
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

    outcome::result<void> TransactionManager::ParseTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto it = transaction_parsers.find( tx->GetType() );
        if ( it == transaction_parsers.end() )
        {
            TransactionManagerLogger()->info( "[{} - full: {}] No Parser Available",
                                              account_m->GetAddress().substr( 0, 8 ),
                                              full_node_m );
            return std::errc::invalid_argument;
        }

        return ( this->*it->second.first )( tx );
    }

    outcome::result<void> TransactionManager::RevertTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto it = transaction_parsers.find( tx->GetType() );
        if ( it == transaction_parsers.end() )
        {
            TransactionManagerLogger()->info( "[{} - full: {}] No Reverter Available",
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
        OUTCOME_TRY( auto transaction_data, db->Get( { std::string( transaction_key ) } ) );

        return DeSerializeTransaction( transaction_data );
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::DeSerializeTransaction(
        const base::Buffer &tx_data )
    {
        const auto &transaction_data_vector = tx_data.toVector();

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
        TransactionManagerLogger()->debug( "[{} - full: {}] Checking the proof in {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           proof_path );
        OUTCOME_TRY( ( auto &&, proof_data ), globaldb_m->Get( { proof_path } ) );

        auto proof_data_vector = proof_data.toVector();

        TransactionManagerLogger()->debug( "[{} - full: {}] Proof data acquired. Verifying...",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m );
        return IBasicProof::VerifyFullProof( proof_data_vector );
    }

    outcome::result<void> TransactionManager::QueryTransactions()
    {
        auto monitored_networks = GetMonitoredNetworkIDs();

        for ( auto network_id : monitored_networks )
        {
            std::string blockchain_base = GetBlockChainBase( network_id );
            std::string query_path      = blockchain_base + "tx";
            TransactionManagerLogger()->trace( "[{} - full: {}] Probing transactions on {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               query_path );
            OUTCOME_TRY( auto transaction_list, globaldb_m->QueryKeyValues( query_path ) );

            TransactionManagerLogger()->trace( "[{} - full: {}] Transaction list grabbed from CRDT with Size {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               transaction_list.size() );

            for ( const auto &[key, value] : transaction_list )
            {
                auto transaction_key = globaldb_m->KeyToString( key );
                if ( !transaction_key.has_value() )
                {
                    TransactionManagerLogger()->error( "[{} - full: {}] Unable to convert a key to string",
                                                       account_m->GetAddress().substr( 0, 8 ),
                                                       full_node_m );
                    continue;
                }
                auto process_result = FetchAndProcessTransaction( transaction_key.value(), value );
                if ( !transaction_key.has_value() )
                {
                    TransactionManagerLogger()->error( "[{} - full: {}] Unable to fetch and process transaction {}",
                                                       account_m->GetAddress().substr( 0, 8 ),
                                                       full_node_m,
                                                       transaction_key.value() );
                }
            }
        }

        return outcome::success();
    }

    outcome::result<void> TransactionManager::FetchAndProcessTransaction( const std::string          &tx_key,
                                                                          std::optional<base::Buffer> tx_data )
    {
        {
            std::shared_lock tx_lock( tx_mutex_m );
            auto             tracked = tx_processed_m.find( tx_key );
            if ( tracked != tx_processed_m.end() )
            {
                if ( tracked->second.tx )
                {
                    std::lock_guard missing_lock( missing_tx_mutex_ );
                    missing_tx_hashes_.erase( tracked->second.tx->GetHash() );
                }
                TransactionManagerLogger()->trace( "[{} - full: {}] Transaction already processed: {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   tx_key );
                return outcome::success();
            }
        }

        auto transaction_result = [&]()
        {
            if ( tx_data.has_value() )
            {
                TransactionManagerLogger()->debug( "[{} - full: {}] Deserializing transaction: {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   tx_key );
                return DeSerializeTransaction( tx_data.value() );
            }

            TransactionManagerLogger()->debug( "[{} - full: {}] Finding transaction: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               tx_key );
            return FetchTransaction( globaldb_m, tx_key );
        }();

        if ( transaction_result.has_error() )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Can't fetch transaction {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               tx_key );
            return outcome::failure( transaction_result.error() );
        }
        auto &transaction = transaction_result.value();

        if ( transaction->GetHash().empty() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Error, received transaction without hash: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               tx_key );
            return outcome::failure( std::errc::invalid_argument );
        }

        TransactionManagerLogger()->debug(
            "[{} - full: {}] Checking if the transaction has a valid certificate to be confirmed {}",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            tx_key );

        auto next_tx_state = TransactionStatus::VERIFYING;

        if ( blockchain_->CheckCertificate( transaction->GetHash() ) )
        {
            TransactionManagerLogger()->debug(
                "[{} - full: {}] Transaction has a valid certificate, marking as CONFIRMED {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                tx_key );
            next_tx_state = TransactionStatus::CONFIRMED;
        }
        OUTCOME_TRY( ChangeTransactionState( transaction, next_tx_state ) );

        {
            std::lock_guard missing_lock( missing_tx_mutex_ );
            missing_tx_hashes_.erase( transaction->GetHash() );
        }

        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseTransferTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx );
        auto dest_infos  = transfer_tx->GetDstInfos();

        for ( std::uint32_t i = 0; i < dest_infos.size(); ++i )
        {
            auto       hash = ( base::Hash256::fromReadableString( transfer_tx->GetHash() ) ).value();
            GeniusUTXO new_utxo( hash, i, dest_infos[i].encrypted_amount, dest_infos[i].token_id );
            utxo_manager_.PutUTXO( new_utxo, dest_infos[i].dest_address );

            TransactionManagerLogger()->debug( "[{} - full: {}] Notify {} of transfer of {} to it",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               dest_infos[i].dest_address,
                                               dest_infos[i].encrypted_amount );
        }

        for ( auto &input : transfer_tx->GetInputInfos() )
        {
            TransactionManagerLogger()->trace( "[{} - full: {}] UTXO to be updated {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               input.txid_hash_.toReadableString() );
            TransactionManagerLogger()->trace( "[{} - full: {}] UTXO output {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               input.output_idx_ );
        }
        utxo_manager_.ConsumeUTXOs( transfer_tx->GetInputInfos(), transfer_tx->GetSrcAddress() );
        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );

        auto       hash = ( base::Hash256::fromReadableString( mint_tx->GetHash() ) ).value();
        GeniusUTXO new_utxo( hash, 0, mint_tx->GetAmount(), mint_tx->GetTokenID() );
        utxo_manager_.PutUTXO( new_utxo, mint_tx->GetSrcAddress() );
        TransactionManagerLogger()->info( "[{} - full: {}] Created tokens, amount {} balance {}",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          std::to_string( mint_tx->GetAmount() ),
                                          std::to_string( utxo_manager_.GetBalance() ) );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );

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

        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseEscrowReleaseTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrowReleaseTx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tx );

        if ( !escrowReleaseTx )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Failed to cast transaction to EscrowReleaseTransaction",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );
            return std::errc::invalid_argument;
        }

        std::string originalEscrowHash = escrowReleaseTx->GetOriginalEscrowHash();
        TransactionManagerLogger()->debug( "[{} - full: {}] Successfully fetched release for escrow: {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           originalEscrowHash );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertTransferTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx );
        auto dest_infos  = transfer_tx->GetDstInfos();

        for ( const auto &dest_info : dest_infos )
        {
            auto hash = ( base::Hash256::fromReadableString( transfer_tx->GetHash() ) ).value();
            utxo_manager_.DeleteUTXO( hash, dest_info.dest_address );

            TransactionManagerLogger()->debug( "[{} - full: {}] Notify {} of deletion of {} to it",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               dest_info.dest_address,
                                               dest_info.encrypted_amount );
        }

        TransactionManagerLogger()->debug( "[{} - full: {}] Adding origin address to Broadcast: {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           transfer_tx->GetSrcAddress() );

        TransactionManagerLogger()->debug( "[{} - full: {}] Re-parsing inputs to be added as UTXOs",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m );
        for ( const auto &input : transfer_tx->GetInputInfos() )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Fetching transaction {} ",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               input.txid_hash_.toReadableString() );
            auto tx = GetTransactionByHashNoLock( input.txid_hash_.toReadableString() );
            if ( tx )
            {
                TransactionManagerLogger()->debug( "[{} - full: {}] Re-parsing {} transaction",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   tx->GetType() );
                OUTCOME_TRY( ParseTransaction( tx ) );
            }
        }
        utxo_manager_.RollbackUTXOs( transfer_tx->GetInputInfos() );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );

        auto hash = ( base::Hash256::fromReadableString( mint_tx->GetHash() ) ).value();
        utxo_manager_.DeleteUTXO( hash, mint_tx->GetSrcAddress() );
        TransactionManagerLogger()->info( "[{} - full: {}] Deleted {} tokens, from tx {}, final balance {}",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          mint_tx->GetAmount(),
                                          mint_tx->GetHash(),
                                          std::to_string( utxo_manager_.GetBalance() ) );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );

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
                        TransactionManagerLogger()->debug( "[{} - full: {}] Re-parsing {} transaction",
                                                           account_m->GetAddress().substr( 0, 8 ),
                                                           full_node_m,
                                                           tx->GetType() );
                        OUTCOME_TRY( ParseTransaction( tx ) );
                    }
                }
                utxo_manager_.RollbackUTXOs( inputs );
            }
        }

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertEscrowReleaseTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrowReleaseTx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tx );

        if ( !escrowReleaseTx )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Failed to cast transaction to EscrowReleaseTransaction",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );
            return std::errc::invalid_argument;
        }

        std::string originalEscrowHash = escrowReleaseTx->GetOriginalEscrowHash();
        TransactionManagerLogger()->debug( "[{} - full: {}] Successfully fetched release for escrow: {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           originalEscrowHash );

        return outcome::success();
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

    std::vector<std::vector<uint8_t>> TransactionManager::GetTransactions(
        std::optional<TransactionStatus> tx_status ) const
    {
        std::vector<std::vector<std::uint8_t>> result;
        {
            std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            result.reserve( tx_processed_m.size() );
            for ( const auto &[_, value] : tx_processed_m )
            {
                if ( !tx_status || value.status == tx_status.value() )
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
        auto start  = std::chrono::steady_clock::now();
        auto retval = TransactionStatus::FAILED;

        do
        {
            std::shared_lock tx_lock( tx_mutex_m );
            for ( const auto &[_, tracked] : tx_processed_m )
            {
                if ( tracked.tx && tracked.tx->GetHash() == txId &&
                     tracked.tx->GetSrcAddress() != account_m->GetAddress() )
                {
                    retval = tracked.status;
                    break;
                }
            }

            if ( retval == TransactionStatus::CONFIRMED )
            {
                TransactionManagerLogger()->debug( "[{} - full: {}] Transaction is FINALIZED",
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
        auto start  = std::chrono::steady_clock::now();
        auto retval = TransactionStatus::CREATED;

        do
        {
            std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            TransactionManagerLogger()->trace( "[{} - full: {}] Searching for transaction {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               txId );
            bool found = false;
            for ( const auto &[_, tracked] : tx_processed_m )
            {
                if ( tracked.tx && tracked.tx->GetHash() == txId &&
                     tracked.tx->GetSrcAddress() == account_m->GetAddress() )
                {
                    retval = tracked.status;
                    TransactionManagerLogger()->trace( "[{} - full: {}] Transaction status is {}",
                                                       account_m->GetAddress().substr( 0, 8 ),
                                                       full_node_m,
                                                       static_cast<int>( retval ) );
                    found = true;
                    break;
                }
            }
            if ( !found )
            {
                TransactionManagerLogger()->trace( "[{} - full: {}] Transaction untracked",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m );
                retval = TransactionStatus::FAILED;
            }

            if ( retval == TransactionStatus::INVALID || retval == TransactionStatus::CONFIRMED ||
                 retval == TransactionStatus::FAILED )
            {
                TransactionManagerLogger()->trace( "[{} - full: {}] Transaction has finalized state {}",
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
        auto start  = std::chrono::steady_clock::now();
        auto retval = TransactionStatus::INVALID;

        while ( std::chrono::steady_clock::now() - start < timeout )
        {
            std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );

            for ( const auto &[_, tracked] : tx_processed_m )
            {
                if ( !tracked.tx )
                {
                    continue;
                }

                if ( tracked.tx->GetType() == "escrow-release" )
                {
                    auto escrowReleaseTx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tracked.tx );
                    if ( escrowReleaseTx && escrowReleaseTx->GetOriginalEscrowHash() == originalEscrowId )
                    {
                        TransactionManagerLogger()->debug(
                            "[{} - full: {}] Found matching escrow release transaction with tx id: {}",
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

    void TransactionManager::InitializeUTXOs()
    {
        {
            std::lock_guard missing_lock( missing_tx_mutex_ );
            missing_tx_hashes_.clear();
        }
        TransactionManagerLogger()->debug( "[{} - full: {}] Initializing UTXOs",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m );

        auto utxo_result = utxo_manager_.LoadUTXOs( globaldb_m->GetDataStore() );
        if ( utxo_result.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Failed to load UTXOs from storage",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );
        }

        const bool has_local_utxos    = utxo_result.has_value() && utxo_result.value();
        auto       monitored_networks = GetMonitoredNetworkIDs();

        std::unordered_set<std::string> network_hashes;
        bool                            has_network_utxos = false;

        TransactionManagerLogger()->debug( "[{} - full: {}] Requesting UTXOs from network during init",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m );
        auto network_utxos = account_m->RequestUTXOs( 8000, account_m->GetAddress() );
        if ( network_utxos.has_value() && !network_utxos.value().empty() )
        {
            network_hashes    = network_utxos.value();
            has_network_utxos = true;
            TransactionManagerLogger()->debug( "[{} - full: {}] Received {} UTXOs from network",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               network_hashes.size() );
        }
        else
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] No UTXO response received from network during init",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );
        }

        if ( !has_local_utxos && !has_network_utxos )
        {
            TransactionManagerLogger()->info(
                "[{} - full: {}] No local or network UTXOs found, querying transactions to mount UTXOs",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m );
            QueryTransactions();
            return;
        }

        auto utxo_map = utxo_manager_.GetAllUTXOs();

        if ( has_local_utxos )
        {
            for ( const auto &[address, utxo_data_vector] : utxo_map )
            {
                TransactionManagerLogger()->debug( "[{} - full: {}] Loaded {} UTXOs for address {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   utxo_data_vector.size(),
                                                   address.substr( 0, 8 ) );
                for ( auto &utxo_data : utxo_data_vector )
                {
                    auto &[utxo_state, utxo] = utxo_data;
                    const auto tx_hash       = utxo.GetTxID().toReadableString();
                    TransactionManagerLogger()->debug(
                        "[{} - full: {}] UTXO - state: {}, tx_hash: {}, index: {}, amount: {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        static_cast<uint8_t>( utxo_state ),
                        tx_hash,
                        utxo.GetOutputIdx(),
                        utxo.GetAmount() );

                    if ( utxo_state != UTXOManager::UTXOState::UTXO_READY )
                    {
                        TransactionManagerLogger()->debug( "[{} - full: {}] Skipping UTXO in state {} for tx {}",
                                                           account_m->GetAddress().substr( 0, 8 ),
                                                           full_node_m,
                                                           static_cast<uint8_t>( utxo_state ),
                                                           tx_hash );
                        continue;
                    }

                    bool processed = false;
                    for ( auto network_id : monitored_networks )
                    {
                        auto tx_path        = GetTransactionPath( network_id, tx_hash );
                        auto process_result = FetchAndProcessTransaction( tx_path );
                        if ( !process_result.has_error() )
                        {
                            TransactionManagerLogger()->debug( "[{} - full: {}] Processed transaction in {}",
                                                               account_m->GetAddress().substr( 0, 8 ),
                                                               full_node_m,
                                                               tx_path );
                            processed = true;
                            break;
                        }
                    }

                    if ( !processed )
                    {
                        std::lock_guard missing_lock( missing_tx_mutex_ );
                        missing_tx_hashes_.insert( tx_hash );
                    }
                }
            }
        }

        if ( has_network_utxos )
        {
            for ( const auto &tx_hash : network_hashes )
            {
                bool processed = false;
                for ( auto network_id : monitored_networks )
                {
                    auto tx_path        = GetTransactionPath( network_id, tx_hash );
                    auto process_result = FetchAndProcessTransaction( tx_path );
                    if ( !process_result.has_error() )
                    {
                        TransactionManagerLogger()->debug( "[{} - full: {}] Processed transaction in {}",
                                                           account_m->GetAddress().substr( 0, 8 ),
                                                           full_node_m,
                                                           tx_path );
                        processed = true;
                        break;
                    }
                }

                if ( !processed )
                {
                    std::lock_guard missing_lock( missing_tx_mutex_ );
                    missing_tx_hashes_.insert( tx_hash );
                }
            }
        }
    }

    void TransactionManager::InitTransactions()
    {
        size_t                          missing_count = 0;
        std::unordered_set<std::string> missing_tx_hashes_copy;
        {
            std::lock_guard missing_lock( missing_tx_mutex_ );
            missing_tx_hashes_copy = missing_tx_hashes_;
            missing_count          = missing_tx_hashes_.size();
        }

        if ( missing_count == 0 )
        {
            if ( CheckNonce() )
            {
                ChangeState( State::READY );
            }
            return;
        }
        // TODO - Remove this once we remove the passive heads processing or we want transactions we are not subscribed here
        return;

        TransactionManagerLogger()->info( "[{} - full: {}] Missing {} transactions during init",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          missing_count );

        auto now = std::chrono::steady_clock::now();
        if ( last_init_tx_request_time_ != std::chrono::steady_clock::time_point{} &&
             now - last_init_tx_request_time_ < std::chrono::milliseconds( k_init_tx_request_cooldown_ms ) )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Skipping tx requests (init cooldown)",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );
            return;
        }
        last_init_tx_request_time_ = now;

        const auto request_timeout = std::chrono::milliseconds( k_init_tx_request_cooldown_ms );
        for ( const auto &tx_hash : missing_tx_hashes_copy )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Requesting transaction with hash {} (this: {})",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               tx_hash,
                                               reinterpret_cast<uint64_t>( this ) );
            auto request_result = account_m->RequestTransaction( request_timeout.count(), tx_hash );
            if ( request_result.has_error() )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] Failed to request transaction with hash {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   tx_hash );
            }
            else
            {
                TransactionManagerLogger()->debug( "[{} - full: {}] Successfully requested transaction with hash {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   tx_hash );
            }
        }
    }

    bool TransactionManager::CheckNonce() const
    {
        TransactionManagerLogger()->debug(
            "[{} - full: {}] Checking if my local confirmed nonce is in sync with the network",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m );

        auto nonce_from_network_result = account_m->FetchNetworkNonce( NONCE_REQUEST_TIMEOUT_MS );
        if ( nonce_from_network_result.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Failed to fetch network nonce: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               nonce_from_network_result.error().message() );
            if ( full_node_m )
            {
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] Network nonce fetch failed, but we have a full node configured. Allowing for it to boot",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m );
                return true;
            }
            return false;
        }
        auto maybe_nonce = nonce_from_network_result.value();
        if ( !maybe_nonce.has_value() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Network doesn't have nonce info, trusting local nonce",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );
            return true;
        }

        auto network_nonce      = maybe_nonce.value();
        auto local_nonce_result = account_m->GetPeerNonce( account_m->GetAddress() );
        if ( local_nonce_result.has_error() )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] No local nonce found. Network nonce exists: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               network_nonce );
            return false;
        }
        auto local_nonce = local_nonce_result.value();

        if ( network_nonce > local_nonce )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Nonce mismatch - Network: {}, Local: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               network_nonce,
                                               local_nonce );

            return false;
        }
        TransactionManagerLogger()->debug( "[{} - full: {}] Nonce is in sync with the network - Network: {}, Local: {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           network_nonce,
                                           local_nonce );
        return true;
    }

    void TransactionManager::SyncNonce()
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] Checking if my nonce is updated",
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
            TransactionManagerLogger()->debug( "[{} - full: {}] Network nonce updated: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               expected_next_nonce );
            ChangeState( State::READY );
        }
        else if ( proposed_nonce > expected_next_nonce )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] Local nonce ahead - Local: {}, Expected: {}. Checking for invalid tx",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                proposed_nonce,
                expected_next_nonce );
            std::set<uint64_t> nonces_to_check;
            for ( auto i = expected_next_nonce; i < proposed_nonce; ++i )
            {
                nonces_to_check.insert( i );
                TransactionManagerLogger()->debug( "[{} - full: {}] Inserting nonce to check: {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   i );
            }

            (void)CheckTransactionValidity( nonces_to_check );
        }
        else if ( proposed_nonce < expected_next_nonce )
        {
            uint64_t nonce_gap = expected_next_nonce - proposed_nonce;
            TransactionManagerLogger()->error(
                "[{} - full: {}] Local nonce behind - Local: {}, Expected: {}. Gap: {}. Waiting to sync",
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
                TransactionManagerLogger()->trace(
                    "[{} - full: {}] Skipping head request - too soon since last request ({}s ago)",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    elapsed.count() );
                return;
            }
        }

        auto topics_result = globaldb_m->GetMonitoredTopics();
        if ( !topics_result.has_value() )
        {
            TransactionManagerLogger()->warn( "[{} - full: {}] Could not get monitored topics for head request",
                                              account_m->GetAddress().substr( 0, 8 ),
                                              full_node_m );
            return;
        }
        TransactionManagerLogger()->info( "[{} - full: {}] Requesting heads for {} topics",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          topics_result.value().size() );

        if ( account_m->RequestHeads( topics_result.value() ) )
        {
            last_head_request_time_ = now;
            TransactionManagerLogger()->debug( "[{} - full: {}] Periodic sync head request sent for {} topics",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               topics_result.value().size() );
        }
        else
        {
            TransactionManagerLogger()->warn( "[{} - full: {}] Failed to request heads",
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
            TransactionManagerLogger()->debug( "[{} - full: {}] {}: Checking transactions",
                                               __func__,
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );

            for ( auto &nonce : nonces_to_check )
            {
                for ( auto [key, tracked] : tx_processed_m )
                {
                    if ( !tracked.tx || tracked.tx->GetSrcAddress() != account_m->GetAddress() )
                    {
                        continue;
                    }

                    TransactionManagerLogger()->debug( "[{} - full: {}] {}: Seeing if transaction {} is valid {}",
                                                       __func__,
                                                       account_m->GetAddress().substr( 0, 8 ),
                                                       full_node_m,
                                                       tracked.cached_nonce,
                                                       nonce );

                    if ( tracked.cached_nonce == nonce )
                    {
                        bool valid_tx = true;
                        if ( !CheckTransactionAuthorization( *tracked.tx ) )
                        {
                            TransactionManagerLogger()->error(
                                "[{} - full: {}] Could not validate signature of transaction with nonce {}",
                                account_m->GetAddress().substr( 0, 8 ),
                                full_node_m,
                                nonce );
                            valid_tx = false;
                        }
                        else
                        {
                            TransactionManagerLogger()->debug( "[{} - full: {}] {}: Transaction is valid with {}",
                                                               __func__,
                                                               account_m->GetAddress().substr( 0, 8 ),
                                                               full_node_m,
                                                               nonce );
                        }
                        if ( !valid_tx )
                        {
                            // Collect the key for later removal
                            invalid_transaction_keys.push_back( key );
                            changed = true;
                            TransactionManagerLogger()->debug( "[{} - full: {}] {}: INVALID TX {}",
                                                               __func__,
                                                               account_m->GetAddress().substr( 0, 8 ),
                                                               full_node_m,
                                                               nonce );
                        }
                        else
                        {
                            tracked.status = TransactionStatus::CONFIRMED;
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

    outcome::result<void> TransactionManager::DeleteTransaction( std::string                            tx_key,
                                                                 const std::unordered_set<std::string> &topics )
    {
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction = globaldb_m->BeginTransaction();

        TransactionManagerLogger()->debug( "[{} - full: {}] Deleting transaction on {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           tx_key );

        OUTCOME_TRY( crdt_transaction->Remove( { std::move( tx_key ) } ) );

        TransactionManagerLogger()->debug( "[{} - full: {}] Removed key transaction on {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           tx_key );

        OUTCOME_TRY( crdt_transaction->Commit( topics ) );

        TransactionManagerLogger()->debug( "[{} - full: {}] Commited tx on {}",
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
        for ( const auto &[_, tracked] : tx_processed_m )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Searching for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               tx_hash );
            if ( tracked.tx && tracked.tx->GetHash() == tx_hash )
            {
                return tracked.tx;
            }
        }
        return nullptr;
    }

    std::shared_ptr<IGeniusTransactions> TransactionManager::GetTransactionByNonceAndAddress(
        uint64_t           nonce,
        const std::string &address ) const
    {
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( const auto &[_, tracked] : tx_processed_m )
        {
            if ( tracked.tx && ( tracked.cached_nonce == nonce ) && ( tracked.tx->GetSrcAddress() == address ) )
            {
                return tracked.tx;
            }
        }
        return nullptr;
    }

    std::optional<TransactionManager::TrackedTx> TransactionManager::GetTrackedTxByNonceAndAddress(
        uint64_t           nonce,
        const std::string &address ) const
    {
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( const auto &[_, tracked] : tx_processed_m )
        {
            if ( tracked.tx && ( tracked.cached_nonce == nonce ) && ( tracked.tx->GetSrcAddress() == address ) )
            {
                return tracked;
            }
        }
        return std::nullopt;
    }

    std::optional<TransactionManager::TrackedTx> TransactionManager::GetTrackedTxByHash(
        const std::string &tx_hash ) const
    {
        //TODO - Check for all monitored networks
        auto tx_path = GetTransactionPath( tx_hash );

        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        auto                                maybe_tracked = tx_processed_m.find( tx_path );
        if ( maybe_tracked != tx_processed_m.end() )
        {
            return maybe_tracked->second;
        }
        return std::nullopt;
    }

    TransactionManager::TransactionStatus TransactionManager::GetOutgoingStatusByTxId( const std::string &txId ) const
    {
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( const auto &[_, tracked] : tx_processed_m )
        {
            if ( tracked.tx && tracked.tx->GetHash() == txId && tracked.tx->GetSrcAddress() == account_m->GetAddress() )
            {
                return tracked.status;
            }
        }
        return TransactionStatus::INVALID;
    }

    TransactionManager::TransactionStatus TransactionManager::GetIncomingStatusByTxId( const std::string &txId ) const
    {
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( const auto &[_, tracked] : tx_processed_m )
        {
            if ( tracked.tx && tracked.tx->GetHash() == txId && tracked.tx->GetSrcAddress() != account_m->GetAddress() )
            {
                return tracked.status;
            }
        }
        return TransactionStatus::INVALID;
    }

    bool TransactionManager::SetOutgoingStatusByNonce( uint64_t nonce, TransactionStatus s )
    {
        bool                                 ret = false;
        std::shared_ptr<IGeniusTransactions> tx;
        std::unique_lock<std::shared_mutex>  tx_lock( tx_mutex_m );
        for ( auto &[_, tracked] : tx_processed_m )
        {
            if ( !tracked.tx )
            {
                continue;
            }
            if ( tracked.tx->GetSrcAddress() != account_m->GetAddress() )
            {
                continue;
            }
            if ( tracked.cached_nonce != nonce )
            {
                continue;
            }
            tx = tracked.tx;
            break;
        }
        tx_lock.unlock();
        if ( tx )
        {
            auto result = ChangeTransactionState( std::move( tx ), s );
            if ( !result.has_error() )
            {
                ret = true;
            }
        }
        else
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] No outgoing tx found with nonce {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               nonce );
        }
        return ret;
    }

    std::optional<std::vector<crdt::pb::Element>> TransactionManager::FilterTransaction(
        const crdt::pb::Element &element )
    {
        std::optional<std::vector<crdt::pb::Element>> maybe_tombstones;
        bool                                          should_delete = true;
        std::shared_ptr<IGeniusTransactions>          new_tx;
        do
        {
            auto maybe_new_tx = DeSerializeTransaction( element.value() );
            if ( maybe_new_tx.has_error() )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] Failed to deserialize incoming transaction {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   element.key() );
                break;
            }
            new_tx = maybe_new_tx.value();

            if ( !CheckTransactionAuthorization( *new_tx ) )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] Could not validate signature of transaction {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   element.key() );
                break;
            }
            if ( IsGoingToOverwrite( GetTransactionPath( *new_tx ) ) )
            {
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] New transaction {} would overwrite an existing one. Preventing that",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    new_tx->GetHash() );
                break;
            }
            should_delete = false;

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
            // Remove once we remove the in memory processed_cids on crdt_datastore and use dagsyncer again
            auto maybe_has_value = globaldb_m->Get( element.key() );
            if ( maybe_has_value.has_value() )
            {
                TransactionManagerLogger()->debug( "[{} - full: {}] Already have the proof {}",
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
                TransactionManagerLogger()->error( "[{} - full: {}] Could not verify proof {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   element.key() );
                break;
            }
            TransactionManagerLogger()->trace( "[{} - full: {}] Valid proof of {}",
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
        TransactionManagerLogger()->debug(
            "[{} - full: {}] {}: Checking if new transaction {} should replace existing one {}",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            __func__,
            new_tx.GetHash(),
            existing_tx.GetHash() );

        return blockchain_->BestHash( existing_tx.GetHash(), new_tx.GetHash() ) == new_tx.GetHash();
    }

    uint64_t TransactionManager::GetCurrentTimestamp()
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
            TransactionManagerLogger()->debug(
                "[{} - full: {}] Transaction timestamp {} is in the future (current: {}), elapsed: {} ms",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                timestamp,
                current_timestamp,
                elapsed );
        }
        else
        {
            TransactionManagerLogger()->trace( "[{} - full: {}] Transaction timestamp {} elapsed: {} ms",
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
            TransactionManagerLogger()->debug(
                "[{} - full: {}] Transaction from future is not immutable (elapsed: {} ms)",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                elapsed );
            return false;
        }

        bool is_immutable = elapsed > mutability_window_m.count();

        if ( is_immutable )
        {
            TransactionManagerLogger()->debug(
                "[{} - full: {}] Transaction is immutable (elapsed: {} ms, window: {} ms)",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                elapsed,
                mutability_window_m.count() );
        }
        else
        {
            TransactionManagerLogger()->trace(
                "[{} - full: {}] Transaction is still mutable (elapsed: {} ms, window: {} ms)",
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

        TransactionManagerLogger()->info( "[{} - full: {}] Updated timeframe tolerance to {} ms",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          timeframe_tolerance );
    }

    void TransactionManager::SetMutabilityWindowMs( uint64_t mutability_window )
    {
        mutability_window_m = std::chrono::milliseconds( mutability_window );

        TransactionManagerLogger()->info( "[{} - full: {}] Updated mutability window to {} ms",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          mutability_window );
    }

    outcome::result<void> TransactionManager::RemoveTransactionFromProcessedMaps( const std::string &transaction_key,
                                                                                  bool               delete_from_crdt )
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] Removing transaction from processed maps: {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           transaction_key );
        bool found = false;
        {
            std::unique_lock tx_lock( tx_mutex_m );
            auto             it = tx_processed_m.find( transaction_key );
            if ( it != tx_processed_m.end() )
            {
                TransactionManagerLogger()->debug( "[{} - full: {}] Removing from processed: {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   transaction_key );

                if ( it->second.tx )
                {
                    OUTCOME_TRY( RevertTransaction( it->second.tx ) );
                    if ( delete_from_crdt )
                    {
                        auto topics = it->second.tx->GetTopics();
                        OUTCOME_TRY( DeleteTransaction( transaction_key, topics ) );
                    }
                    account_m->RollBackPeerConfirmedNonce( it->second.cached_nonce,
                                                           it->second.tx->dag_st.source_addr() );
                }
                tx_processed_m.erase( it );
                found = true;
            }
        }

        if ( !found )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Transaction not found in processed maps: {}",
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

        TransactionManagerLogger()->debug( "[{} - full: {}] Trying to deserialize {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           key );

        OUTCOME_TRY( auto &&new_tx, DeSerializeTransaction( value ) );

        TransactionManagerLogger()->debug( "[{} - full: {}] Deserialized transaction {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           key );

        if ( new_tx->GetHash().empty() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Empty hash on {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               key );
            return outcome::failure( boost::system::error_code{} );
        }

        TransactionManagerLogger()->debug( "[{} - full: {}] Verifying if we have a conflicting transaction {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           key );

        auto conflicting_tx = GetConflictingTransaction( *new_tx );

        if ( conflicting_tx.has_value() )
        {
            TransactionManagerLogger()->warn(
                "[{} - full: {}] Found conflicting transaction that passed the FILTER with hash: {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                conflicting_tx.value()->GetHash() );
            std::unique_lock tx_lock( tx_mutex_m );
            auto             it = tx_processed_m.find( GetTransactionPath( conflicting_tx.value()->GetHash() ) );

            // No need to check if not found because we already found it on GetConflictingTransaction

            if ( it->second.status == TransactionStatus::CONFIRMED )
            {
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] Conflicting transaction is already CONFIRMED, not adding incoming transaction{}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    key );
                tx_lock.unlock();
                OUTCOME_TRY( ChangeTransactionState( new_tx, TransactionStatus::FAILED ) );
                tx_lock.lock();
                return outcome::failure( boost::system::error_code{} );
            }
            TransactionManagerLogger()->warn(
                "[{} - full: {}] Setting conflicting transaction to VERIFYING since it's not confirmed: {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                conflicting_tx.value()->GetHash() );
            tx_lock.unlock();
            OUTCOME_TRY( ChangeTransactionState( conflicting_tx.value(), TransactionStatus::VERIFYING ) );
        }

        TransactionManagerLogger()->debug(
            "[{} - full: {}] Checking if the transaction has a valid certificate to be confirmed {}",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            key );

        auto next_tx_state = TransactionStatus::VERIFYING;

        if ( blockchain_->CheckCertificate( new_tx->GetHash() ) )
        {
            TransactionManagerLogger()->debug(
                "[{} - full: {}] Transaction has a valid certificate, marking as CONFIRMED {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                key );
            next_tx_state = TransactionStatus::CONFIRMED;
            if ( conflicting_tx.has_value() )
            {
                TransactionManagerLogger()->warn(
                    "[{} - full: {}] Setting conflicting transaction to FAILED because the new has a certificate and it doesn't: {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    conflicting_tx.value()->GetHash() );
                OUTCOME_TRY( ChangeTransactionState( conflicting_tx.value(), TransactionStatus::FAILED ) );
            }
        }
        OUTCOME_TRY( ChangeTransactionState( new_tx, next_tx_state ) );

        return outcome::success();
    }

    void TransactionManager::ProcessDeletion( std::string key )
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] Processing deletion of {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           key );

        auto remove_res = RemoveTransactionFromProcessedMaps( key );

        if ( remove_res.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Error removing transaction {}: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               key,
                                               remove_res.error().message() );
        }
    }

    outcome::result<void> TransactionManager::StoreTransactionCID( const std::string &key, const std::string &cid )
    {
        if ( cid.empty() )
        {
            return outcome::success();
        }

        auto datastore = globaldb_m ? globaldb_m->GetDataStore() : nullptr;
        if ( !datastore )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] RocksDB datastore unavailable, cannot store CID for tx {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                key );
            return outcome::failure( std::errc::bad_file_descriptor );
        }

        crdt::GlobalDB::Buffer key_buffer;
        key_buffer.put( key );

        crdt::GlobalDB::Buffer value_buffer;
        value_buffer.put( cid );

        auto put_result = datastore->put( key_buffer, value_buffer );
        if ( put_result.has_error() )
        {
            return outcome::failure( put_result.error() );
        }

        return outcome::success();
    }

    void TransactionManager::ProcessNewData( crdt::CRDTCallbackManager::NewDataPair new_data )
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] Processing new data with key {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           new_data.first );

        auto add_res = AddTransactionToProcessedMaps( new_data );

        if ( add_res.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Error adding transaction {}: {}",
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
                TransactionManagerLogger()->info(
                    "[{} - full: {}] First transaction data received from network, switching to 10-minute periodic sync interval",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m );
            }
        }
    }

    void TransactionManager::NewElementCallback( crdt::CRDTCallbackManager::NewDataPair new_data, std::string cid )
    {
        auto store_cid_res = StoreTransactionCID( new_data.first, cid );
        if ( store_cid_res.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Failed to store CID for key {}: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               new_data.first,
                                               store_cid_res.error().message() );
        }

        auto key = new_data.first;
        {
            std::lock_guard queue_lock( new_data_queue_mutex_ );
            new_data_queue_.push( std::move( new_data ) );
        }

        TransactionManagerLogger()->debug( "[{} - full: {}] CRDT new data queued, {} - (queue size: {})",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           key,
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

        TransactionManagerLogger()->debug( "[{} - full: {}] CRDT deleted key queued, {} - (queue size: {})",
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
        state_change_callback_ = std::move( callback );
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
                TransactionManagerLogger()->info( "[{} - full: {}] State changed from {} to {}",
                                                  account_m->GetAddress().substr( 0, 8 ),
                                                  full_node_m,
                                                  state_m,
                                                  new_state );
                auto old_state = state_m;
                state_m        = new_state;
                if ( state_change_callback_ )
                {
                    state_change_callback_( old_state, new_state );
                }
            }
        }
    }

    outcome::result<std::string> TransactionManager::GetTransactionCID( const std::string &tx_hash ) const
    {
        auto datastore = globaldb_m->GetDataStore();
        if ( !datastore )
        {
            return outcome::failure( std::errc::bad_file_descriptor );
        }

        auto monitored_networks = GetMonitoredNetworkIDs();
        for ( auto network_id : monitored_networks )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] Looking for CID of tx {} in network {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               tx_hash,
                                               network_id );
            auto                   key = GetTransactionPath( network_id, tx_hash );
            crdt::GlobalDB::Buffer key_buffer;

            key_buffer.put( key );

            auto value_res = datastore->get( key_buffer );
            if ( value_res.has_value() )
            {
                return std::string( value_res.value().toString() );
            }
        }

        return outcome::failure( std::errc::no_such_file_or_directory );
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::GetConflictingTransaction(
        const IGeniusTransactions &element ) const
    {
        auto tx = GetTransactionByNonceAndAddress( element.GetNonce(), element.GetSrcAddress() );
        if ( tx && tx->GetHash() != element.GetHash() )
        {
            return tx;
        }

        return outcome::failure( std::errc::no_such_file_or_directory );
    }

    void TransactionManager::OnConsensusCertificate( const std::string &tx_hash )
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Consensus certificate arrived for transaction {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx_hash );
        auto tx = GetTransactionByHash( tx_hash );
        if ( !tx )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Transaction not found for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return;
        }
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Checking for conflicting transaction with {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx_hash );

        auto conflicting_tx = GetConflictingTransaction( *tx );

        if ( conflicting_tx.has_value() )
        {
            TransactionManagerLogger()->warn( "[{} - full: {}] Found conflicting transaction: {}",
                                              account_m->GetAddress().substr( 0, 8 ),
                                              full_node_m,
                                              conflicting_tx.value()->GetHash() );
            std::unique_lock tx_lock( tx_mutex_m );
            auto             it = tx_processed_m.find( GetTransactionPath( conflicting_tx.value()->GetHash() ) );

            // No need to check if not found because we already found it on GetConflictingTransaction

            if ( it->second.status == TransactionStatus::CONFIRMED )
            {
                TransactionManagerLogger()->error(
                    "[{} - full: {}] Conflicting transaction {} is CONFIRMED as well as incoming {}, not sure what to do {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    conflicting_tx.value()->GetHash(),
                    tx_hash );
                tx_lock.unlock();
                if ( ShouldReplaceTransaction( *conflicting_tx.value(), *tx ) )
                {
                    auto result = ChangeTransactionState( conflicting_tx.value(), TransactionStatus::FAILED );
                    if ( result.has_error() )
                    {
                        TransactionManagerLogger()->error(
                            "[{} - full: {}] {}: Failed to change conflicting transaction state to FAILED for current tx {}: {}",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m,
                            __func__,
                            conflicting_tx.value()->GetHash(),
                            result.error().message() );
                    }
                }
                else
                {
                    auto result = ChangeTransactionState( tx, TransactionStatus::FAILED );
                    if ( result.has_error() )
                    {
                        TransactionManagerLogger()->error(
                            "[{} - full: {}] {}: Failed to change transaction state to FAILED for new tx {}: {}",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m,
                            __func__,
                            tx_hash,
                            result.error().message() );
                    }
                    return;
                }
            }
            else
            {
                TransactionManagerLogger()->warn(
                    "[{} - full: {}] Setting conflicting transaction {} to FAILED since the new one {} is confirmed: ",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    conflicting_tx.value()->GetHash(),
                    tx_hash );
                tx_lock.unlock();
                auto result = ChangeTransactionState( conflicting_tx.value(), TransactionStatus::FAILED );
                if ( result.has_error() )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Failed to change transaction state to FAILED for hash {}: {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        tx_hash,
                        result.error().message() );
                }
            }
        }

        auto result = ChangeTransactionState( tx, TransactionStatus::CONFIRMED );
        if ( result.has_error() )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Failed to change transaction state to CONFIRMED for hash {}: {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                __func__,
                tx_hash,
                result.error().message() );
            return;
        }
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Transaction {} confirmed by consensus",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx_hash );
    }

    outcome::result<ConsensusManager::SubjectCheck> TransactionManager::HandleNonceConsensusSubject(
        const ConsensusManager::Subject &subject )
    {
        if ( subject.type() != SubjectType::SUBJECT_NONCE )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Received unexpected subject type: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               static_cast<int>( subject.type() ) );
            return outcome::failure( std::errc::invalid_argument );
        }

        const std::string tx_hash = subject.nonce().tx_hash();
        const auto        key     = GetTransactionPath( tx_hash );

        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        auto                                it = tx_processed_m.find( key );
        if ( it == tx_processed_m.end() )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] {}: Transaction not found for hash {}, pending",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return ConsensusManager::SubjectCheck::Pending;
        }

        auto &tracked = it->second;
        if ( !tracked.tx )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Tracked transaction missing for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return outcome::failure( std::errc::invalid_argument );
        }

        if ( tracked.cached_nonce != subject.nonce().nonce() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Nonce mismatch for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return ConsensusManager::SubjectCheck::Reject;
        }

        if ( !subject.account_id().empty() && tracked.tx->GetSrcAddress() != subject.account_id() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Account mismatch for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return ConsensusManager::SubjectCheck::Reject;
        }

        if ( tracked.status == TransactionStatus::FAILED )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Transaction status invalid for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return ConsensusManager::SubjectCheck::Reject;
        }

        auto validate_result = ValidateTransactionForConsensus( tracked.tx );

        return validate_result ? ConsensusManager::SubjectCheck::Approve : ConsensusManager::SubjectCheck::Reject;
    }

    bool TransactionManager::ValidateUTXOParametersForConsensus( const UTXOTxParameters &params,
                                                                 const std::string      &address ) const
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Validating UTXO params for address {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           address );
        if ( params.first.empty() || params.second.empty() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Empty inputs or outputs",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__ );
            return false;
        }

        if ( !full_node_m && address != account_m->GetAddress() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Non-full node cannot verify address {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               address );
            return false;
        }

        if ( !utxo_manager_.VerifyParameters( params, address ) )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: VerifyParameters failed for address {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               address );
            return false;
        }

        TransactionManagerLogger()->debug( "[{} - full: {}] {}: UTXO params valid for address {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           address );
        return true;
    }

    bool TransactionManager::ValidateTransactionForConsensus( const std::shared_ptr<IGeniusTransactions> &tx ) const
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Validating transaction",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__ );
        if ( !tx )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Null transaction",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__ );
            return false;
        }

        if ( !CheckTransactionWellFormed( *tx ) )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Well-formed check failed tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return false;
        }
        if ( !CheckTransactionAuthorization( *tx ) )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Authorization check failed tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return false;
        }
        if ( !CheckTransactionTimestamp( *tx ) )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Timestamp check failed tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return false;
        }
        if ( !CheckTransactionReplayProtection( *tx ) )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Replay protection failed tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return false;
        }
        if ( !CheckTransactionTypeRules( tx ) )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Type rules failed tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return false;
        }

        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Transaction valid tx={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx->GetHash() );
        return true;
    }

    bool TransactionManager::CheckTransactionWellFormed( const IGeniusTransactions &tx ) const
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Checking well-formed tx={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx.GetHash() );
        if ( tx.GetHash().empty() || !tx.CheckHash() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Hash invalid tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetHash() );
            return false;
        }

        if ( tx.GetSrcAddress().empty() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Empty source address tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetHash() );
            return false;
        }

        if ( tx.GetTimestamp() == 0 )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Missing timestamp tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetHash() );
            return false;
        }

        if ( transaction_parsers.find( tx.GetType() ) == transaction_parsers.end() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Unknown tx type {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetType() );
            return false;
        }

        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Well-formed ok tx={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx.GetHash() );
        return true;
    }

    bool TransactionManager::CheckTransactionAuthorization( const IGeniusTransactions &tx ) const
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Checking authorization tx={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx.GetHash() );
        if ( tx.CheckSignature() || tx.CheckDAGSignatureLegacy() )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] {}: Authorization ok tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetHash() );
            return true;
        }
        TransactionManagerLogger()->error( "[{} - full: {}] {}: Authorization failed tx={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx.GetHash() );
        return false;
    }

    bool TransactionManager::CheckTransactionTimestamp( const IGeniusTransactions &tx ) const
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Checking timestamp tx={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx.GetHash() );
        const auto ts = tx.GetTimestamp();
        if ( ts == 0 )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Missing timestamp tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetHash() );
            return false;
        }

        const auto elapsed = GetElapsedTime( ts );
        if ( elapsed < 0 && timestamp_tolerance_m.count() > 0 &&
             ( -elapsed ) > static_cast<int64_t>( timestamp_tolerance_m.count() ) )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Timestamp out of tolerance tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetHash() );
            return false;
        }

        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Timestamp ok tx={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx.GetHash() );
        return true;
    }

    bool TransactionManager::CheckTransactionReplayProtection( const IGeniusTransactions &tx ) const
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Checking replay protection tx={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx.GetHash() );
        auto nonce_result = account_m->GetPeerNonce( tx.GetSrcAddress() );
        if ( nonce_result.has_error() )
        {
            if ( tx.GetNonce() == 0 )
            {
                TransactionManagerLogger()->debug( "[{} - full: {}] {}: No peer nonce required for tx with nonce=0",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__ );
                return true;
            }
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Missing peer nonce for address {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetSrcAddress() );

            return false;
        }

        const auto confirmed_nonce = nonce_result.value();
        const auto tx_nonce        = tx.GetNonce();

        if ( tx_nonce <= confirmed_nonce )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Nonce too low tx={} nonce={} confirmed={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetHash(),
                                               tx_nonce,
                                               confirmed_nonce );
            return false;
        }

        if ( tx_nonce > confirmed_nonce + nonce_window_m )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Nonce too high tx={} nonce={} confirmed={} window={}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                __func__,
                tx.GetHash(),
                tx_nonce,
                confirmed_nonce,
                nonce_window_m );
            return false;
        }

        if ( tx_nonce > confirmed_nonce + 1 )
        {
            for ( uint64_t n = confirmed_nonce + 1; n < tx_nonce; ++n )
            {
                auto tracked = GetTrackedTxByNonceAndAddress( n, tx.GetSrcAddress() );
                if ( !tracked.has_value() )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Missing intermediate nonce {} for address {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        n,
                        tx.GetSrcAddress() );
                    return false;
                }
                if ( tracked->status == TransactionStatus::FAILED )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Intermediate nonce {} invalid for address {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        n,
                        tx.GetSrcAddress() );
                    return false;
                }
            }
        }

        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Replay protection ok tx={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx.GetHash() );
        return true;
    }

    bool TransactionManager::CheckTransactionTypeRules( const std::shared_ptr<IGeniusTransactions> &tx ) const
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Checking type rules",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__ );
        if ( !tx )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Null transaction",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__ );
            return false;
        }

        if ( tx->GetType() == "transfer" )
        {
            auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx );
            if ( !transfer_tx )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] {}: Failed to cast transfer tx",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__ );
                return false;
            }
            return ValidateUTXOParametersForConsensus(
                UTXOTxParameters{ transfer_tx->GetInputInfos(), transfer_tx->GetDstInfos() },
                transfer_tx->GetSrcAddress() );
        }

        if ( tx->GetType() == "escrow-hold" )
        {
            auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );
            if ( !escrow_tx )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] {}: Failed to cast escrow-hold tx",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__ );
                return false;
            }
            return ValidateUTXOParametersForConsensus( escrow_tx->GetUTXOParameters(), escrow_tx->GetSrcAddress() );
        }

        if ( tx->GetType() == "escrow-release" )
        {
            auto escrow_release_tx = std::dynamic_pointer_cast<EscrowReleaseTransaction>( tx );
            if ( !escrow_release_tx )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] {}: Failed to cast escrow-release tx",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__ );
                return false;
            }
            return ValidateUTXOParametersForConsensus( escrow_release_tx->GetUTXOParameters(),
                                                       escrow_release_tx->GetSrcAddress() );
        }

        if ( tx->GetType() == "mint" )
        {
            auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );
            if ( !mint_tx )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] {}: Failed to cast mint tx",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__ );
                return false;
            }
            if ( mint_tx->GetAmount() == 0 )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] {}: Mint amount is zero",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__ );
                return false;
            }
            return true;
        }

        return true;
    }

    void TransactionManager::SetNonceWindow( uint64_t window )
    {
        if ( window == 0 )
        {
            TransactionManagerLogger()->warn( "[{} - full: {}] {}: Nonce window 0, using default {}",
                                              account_m->GetAddress().substr( 0, 8 ),
                                              full_node_m,
                                              __func__,
                                              DEFAULT_NONCE_WINDOW );
            nonce_window_m = DEFAULT_NONCE_WINDOW;
            return;
        }
        TransactionManagerLogger()->info( "[{} - full: {}] {}: Setting nonce window to {}",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          __func__,
                                          window );
        nonce_window_m = window;
    }

    outcome::result<void> TransactionManager::ChangeTransactionState( const std::shared_ptr<IGeniusTransactions> &tx,
                                                                      TransactionStatus new_status )
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Changing transaction state to {} for transaction {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           static_cast<int>( new_status ),
                                           tx->GetHash() );
        switch ( new_status )
        {
            case TransactionStatus::CREATED:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                const auto       key = GetTransactionPath( *tx );
                auto             it  = tx_processed_m.find( key );
                if ( it != tx_processed_m.end() )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Trying to CREATE a transaction that already exists {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        tx->GetHash() );
                    return outcome::failure( std::errc::file_exists );
                }
                TransactionManagerLogger()->debug( "[{} - full: {}] {}: Set status of CREATE to transaction {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__,
                                                   tx->GetHash() );
                tx_processed_m.emplace( key, TrackedTx{ tx, TransactionStatus::CREATED, tx->GetNonce() } );
            }
            break;
            case TransactionStatus::SENDING:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                const auto       key = GetTransactionPath( *tx );
                auto             it  = tx_processed_m.find( key );
                if ( it == tx_processed_m.end() )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Trying to SEND a transaction that doesn't exist {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        tx->GetHash() );
                    return outcome::failure( std::errc::no_such_file_or_directory );
                }
                if ( it->second.status != TransactionStatus::CREATED )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Trying to SEND a transaction that is not in CREATED status {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        tx->GetHash() );
                    return outcome::failure( std::errc::invalid_argument );
                }
                it->second.status = TransactionStatus::SENDING;
                TransactionManagerLogger()->debug( "[{} - full: {}] {}: Set status of SENDING to transaction {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__,
                                                   tx->GetHash() );
            }
            break;
            case TransactionStatus::VERIFYING:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                const auto       key = GetTransactionPath( *tx );
                auto             it  = tx_processed_m.find( key );

                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::VERIFYING )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Trying to VERIFY a transaction that is already in VERIFY {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        tx->GetHash() );
                    break;
                }
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::CONFIRMED )
                {
                    TransactionManagerLogger()->warn(
                        "[{} - full: {}] {}: Unconfirming transaction {} and verifying it again",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        tx->GetHash() );
                    OUTCOME_TRY( RevertTransaction( tx ) );

                    OUTCOME_TRY( DeleteTransaction( key, tx->GetTopics() ) );

                    account_m->RollBackPeerConfirmedNonce( it->second.cached_nonce, tx->GetSrcAddress() );
                }
                tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::VERIFYING, tx->GetNonce() };
                TransactionManagerLogger()->debug( "[{} - full: {}] {}: Set status of VERIFYING to transaction {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__,
                                                   tx->GetHash() );
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] {}: Attempting to resume the proposal handling to transaction {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    __func__,
                    tx->GetHash() );
                tx_lock.unlock();
                OUTCOME_TRY( blockchain_->TryResumeProposal( tx->GetHash() ) );
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] {}: Resumed the proposal handling to transaction {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    __func__,
                    tx->GetHash() );
            }

            break;
            case TransactionStatus::CONFIRMED:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                const auto       key = GetTransactionPath( *tx );
                auto             it  = tx_processed_m.find( key );
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::CONFIRMED )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Trying to CONFIRM a transaction that is already CONFIRMED {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        tx->GetHash() );
                    break;
                }
                tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::CONFIRMED, tx->GetNonce() };

                TransactionManagerLogger()->debug( "[{} - full: {}] {}: Set status of CONFIRMED to transaction {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__,
                                                   tx->GetHash() );
                OUTCOME_TRY( ParseTransaction( tx ) );
                account_m->SetPeerConfirmedNonce( tx->GetNonce(), tx->GetSrcAddress() );
            }

            break;
            case TransactionStatus::INVALID:
            case TransactionStatus::FAILED:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                const auto       key = GetTransactionPath( *tx );
                auto             it  = tx_processed_m.find( key );
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::FAILED )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Trying to FAIL a transaction that is already FAILED {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        tx->GetHash() );
                    break;
                }
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::CONFIRMED )
                {
                    TransactionManagerLogger()->debug( "[{} - full: {}] {}: Unconfirming transaction {}",
                                                       account_m->GetAddress().substr( 0, 8 ),
                                                       full_node_m,
                                                       __func__,
                                                       tx->GetHash() );
                    OUTCOME_TRY( RevertTransaction( tx ) );

                    OUTCOME_TRY( DeleteTransaction( key, tx->GetTopics() ) );

                    account_m->RollBackPeerConfirmedNonce( it->second.cached_nonce, tx->GetSrcAddress() );
                }
                tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::FAILED, tx->GetNonce() };
                account_m->ReleaseNonce( tx->GetNonce() );

                TransactionManagerLogger()->debug( "[{} - full: {}] {}: Set status of FAILED to transaction {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__,
                                                   tx->GetHash() );
            }

            break;
            default:
                TransactionManagerLogger()->error(
                    "[{} - full: {}] {}: Invalid transaction status {} for transaction {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    __func__,
                    static_cast<int>( new_status ),
                    tx->GetHash() );
                return outcome::failure( std::errc::invalid_argument );
        }

        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Transaction {} state changed to {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx->GetHash(),
                                           static_cast<int>( new_status ) );
        return outcome::success();
    }

    bool TransactionManager::IsGoingToOverwrite( const std::string &key ) const
    {
        auto existing_data_result = globaldb_m->Get( key );
        if ( existing_data_result.has_value() )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] {}: Key {} already exists in global DB, will overwrite",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               key );
            auto maybe_old_tx = DeSerializeTransaction( existing_data_result.value() );
            if ( maybe_old_tx.has_error() )
            {
                TransactionManagerLogger()->error(
                    "[{} - full: {}] Failed to deserialize existing transaction, allow to replace it {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    key );
                return false;
            }
            return true;
        }
        return false;
    }

}

fmt::format_context::iterator fmt::formatter<sgns::TransactionManager::State>::format(
    sgns::TransactionManager::State s,
    format_context                 &ctx ) const
{
    using State = sgns::TransactionManager::State;

    string_view name = "UNKNOWN";

    switch ( s )
    {
        case State::CREATING:
            name = "CREATING";
            break;
        case State::INITIALIZING:
            name = "INITIALIZING";
            break;
        case State::SYNCING:
            name = "SYNCING";
            break;
        case State::READY:
            name = "READY";
            break;
    }

    return formatter<string_view>::format( name, ctx );
}
