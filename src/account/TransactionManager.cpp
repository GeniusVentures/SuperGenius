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

#include "account/BridgeConsensusAdapter.hpp"
#include <ProofSystem/EthereumKeyPairParams.hpp>
#include "TransferTransaction.hpp"
#include "MintTransaction.hpp"
#include "MintTransactionV2.hpp"
#include "MigrationTransaction.hpp"
#include "MigrationAllowList.hpp"
#include "EscrowTransaction.hpp"
#include "UTXOMerkle.hpp"
#include "account/TokenAmount.hpp"
#include "account/AccountMessenger.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crdt/proto/delta.pb.h"
#include "base/sgns_version.hpp"

#include "outcome/outcome.hpp"
#include "proof/ProcessingProof.hpp"

namespace sgns
{
    namespace
    {
        using utxo_merkle::HashLeaf;
        using utxo_merkle::HashNode;
        using utxo_merkle::OutPointKey;
        using utxo_merkle::ReadUInt32BE;
        using utxo_merkle::ReadUInt64BE;
        using utxo_merkle::SerializeUTXOLeafPayload;
        using input_validator_constants::HASH256_BYTES;
        using input_validator_constants::SERIALIZED_UINT32_BYTES;

        bool ExtractProducedUTXOs( const IGeniusTransactions &tx, std::vector<GeniusUTXO> &outputs )
        {
            auto tx_hash = base::Hash256::fromReadableString( tx.GetHash() );
            if ( tx_hash.has_error() )
            {
                return false;
            }

            outputs.clear();
            if ( !tx.HasUTXOParameters() )
            {
                return false;
            }

            auto params_opt = tx.GetUTXOParametersOpt();
            if ( !params_opt.has_value() )
            {
                return false;
            }

            const auto &dst_infos = params_opt->second;
            outputs.reserve( dst_infos.size() );
            for ( std::uint32_t i = 0; i < dst_infos.size(); ++i )
            {
                outputs.emplace_back( tx_hash.value(),
                                      i,
                                      dst_infos[i].encrypted_amount,
                                      dst_infos[i].token_id,
                                      dst_infos[i].dest_address );
            }
            return true;
        }

    } // namespace

    base::Logger TransactionManagerLogger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "TransactionManager" );
    }

    const std::unordered_map<
        std::string,
        std::pair<TransactionManager::TransactionParserFn, TransactionManager::TransactionParserFn>>
        TransactionManager::transaction_parsers = {
            { "transfer",
              { &TransactionManager::ParseTransferTransaction, &TransactionManager::RevertTransferTransaction } },
            { "mint", { &TransactionManager::ParseMintTransaction, &TransactionManager::RevertMintTransaction } },
            { "mint-v2", { &TransactionManager::ParseMintTransaction, &TransactionManager::RevertMintTransaction } },
            { "migration", { &TransactionManager::ParseMintTransaction, &TransactionManager::RevertMintTransaction } },
            { "escrow-hold",
              { &TransactionManager::ParseEscrowTransaction, &TransactionManager::RevertEscrowTransaction } } };

    std::shared_ptr<TransactionManager> TransactionManager::New( std::shared_ptr<crdt::GlobalDB>          processing_db,
                                                                 std::shared_ptr<boost::asio::io_context> ctx,
                                                                 std::shared_ptr<GeniusAccount>           account,
                                                                 std::shared_ptr<crypto::Hasher>          hasher,
                                                                 std::shared_ptr<Blockchain>              blockchain,
                                                                 bool                                     full_node,
                                                                 std::chrono::milliseconds timestamp_tolerance,
                                                                 std::chrono::milliseconds mutability_window )
    {
        auto instance = std::shared_ptr<TransactionManager>( new TransactionManager( std::move( processing_db ),
                                                                                     std::move( ctx ),
                                                                                     std::move( account ),
                                                                                     std::move( hasher ),
                                                                                     std::move( blockchain ),
                                                                                     full_node,
                                                                                     timestamp_tolerance,
                                                                                     mutability_window ) );

        instance->blockchain_->RegisterCertificateHandler(
            NONCE_SUBJECT_TYPE,
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const std::string          &subject_hash,
                const ConsensusCertificate &certificate ) -> outcome::result<ConsensusManager::Check>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    auto process_result = strong->OnConsensusCertificate( subject_hash );
                    if ( process_result.has_error() )
                    {
                        TransactionManagerLogger()->error(
                            "[{} - full: {}] Failed to process certificate proposal_id={} error={}",
                            strong->account_m->GetAddress().substr( 0, 8 ),
                            strong->full_node_m,
                            certificate.proposal_id(),
                            process_result.error().message() );
                    }
                    return process_result;
                }
                return outcome::failure( std::errc::owner_dead );
            } );
        instance->blockchain_->RegisterSubjectHandler(
            NONCE_SUBJECT_TYPE,
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const ConsensusManager::Subject &subject ) -> outcome::result<ConsensusManager::Check>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->HandleNonceConsensusSubject( subject );
                }
                return outcome::failure( std::errc::owner_dead );
            } );
        RegisterBridgeEventConsensusHandler(
            instance->blockchain_,
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const eth::BridgeEventClaim       &claim,
                const ConsensusManager::Subject   &subject ) -> outcome::result<ConsensusManager::Check>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->HandleBridgeEventConsensusSubject( claim, subject );
                }
                return outcome::failure( std::errc::owner_dead );
            } );
        RegisterBridgeEventConsensusCertificateHandler(
            instance->blockchain_,
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const eth::BridgeEventClaim          &claim,
                const std::string                    &subject_hash,
                const ConsensusManager::Certificate  &certificate ) -> outcome::result<ConsensusManager::Check>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->OnBridgeEventConsensusCertificate( claim, subject_hash, certificate );
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
                                            std::shared_ptr<GeniusAccount>           account,
                                            std::shared_ptr<crypto::Hasher>          hasher,
                                            std::shared_ptr<Blockchain>              blockchain,
                                            bool                                     full_node,
                                            std::chrono::milliseconds                timestamp_tolerance,
                                            std::chrono::milliseconds                mutability_window ) :
        globaldb_m( std::move( processing_db ) ),
        ctx_m( std::move( ctx ) ),
        account_m( std::move( account ) ),
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
        account_m->ClearGetTransactionCIDMethod();
        Stop();
    }

    void TransactionManager::Stop()
    {
        if ( stopped_.exchange( true ) )
        {
            return; // idempotent
        }

        cv_.notify_all();
    }

    void TransactionManager::Start()
    {
        RegisterTopicNames();
        StartListeningTopics();
        StartCore();
    }

    void TransactionManager::RegisterTopicNames()
    {
        if ( stopped_.load() || topic_names_registered_.exchange( true ) )
        {
            return;
        }

        full_node_topic_m = std::string( GNUS_FULL_NODES_TOPIC );

        globaldb_m->AddTopicName( account_m->GetAddress() );
        if ( full_node_m )
        {
            globaldb_m->AddTopicName( full_node_topic_m );
        }
    }

    void TransactionManager::StartListeningTopics()
    {
        if ( stopped_.load() || listening_topics_started_.exchange( true ) )
        {
            return;
        }

        RegisterTopicNames();

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
    }

    void TransactionManager::StartCore()
    {
        if ( GetState() != State::CREATING || stopped_.load() || core_started_.exchange( true ) )
        {
            return;
        }

        TransactionManagerLogger()->info( "[{} - full: {}] Starting Transaction Manager",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m );

        ChangeState( State::INITIALIZING );

        if ( stopped_.load() )
        {
            return;
        }

        InitializeUTXOs();

        // First kick: keep self alive during the first dispatch only
        boost::asio::post( *ctx_m, [self = shared_from_this()]() { self->TickOnce(); } );
    }

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

        std::vector<std::string>                            elements_to_delete;
        std::vector<crdt::CRDTCallbackManager::NewDataPair> elements_to_process;
        {
            std::lock_guard lock( cv_mutex_ );
            while ( !deleted_data_queue_.empty() )
            {
                elements_to_delete.push_back( std::move( deleted_data_queue_.front() ) );
                deleted_data_queue_.pop();
            }
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
                    const auto err             = send_result.error();
                    const bool retryable_error = ( err == boost::system::errc::make_error_code(
                                                              boost::system::errc::timed_out ) ) ||
                                                 ( err == boost::system::errc::make_error_code(
                                                              boost::system::errc::resource_unavailable_try_again ) );

                    if ( retryable_error )
                    {
                        TransactionManagerLogger()->info(
                            "[{} - full: {}] Send deferred/retryable ({}). Keeping transaction in queue",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m,
                            err.message() );
                        break;
                    }

                    ChangeState( State::SYNCING );

                    TransactionManagerLogger()->error( "[{} - full: {}] Error in SendTransactionItem: {}",
                                                       account_m->GetAddress().substr( 0, 8 ),
                                                       full_node_m,
                                                       err.message() );

                    auto rollback_result = RollbackTransactions( tx_queue_m.front() );
                    if ( rollback_result.has_error() )
                    {
                        TransactionManagerLogger()->error( "[{} - full: {}] {} error, couldn't fetch nonce",
                                                           account_m->GetAddress().substr( 0, 8 ),
                                                           full_node_m,
                                                           __func__ );
                        break;
                    }
                    tx_queue_m.pop_front();
                    break;
                }
                tx_queue_m.pop_front();
            }
            break;
        }

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

        std::unique_lock lock( cv_mutex_ );
        cv_.wait_for( lock,
                      std::chrono::milliseconds( 300 ),
                      [this] { return stopped_.load() || !new_data_queue_.empty() || !deleted_data_queue_.empty(); } );
        lock.unlock();

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
        std::cout << "Account Address: " << account_m->GetAddress() << '\n'
                  << "Balance: " << std::to_string( account_m->GetUTXOManager().GetBalance() ) << '\n'
                  << "Token Type: " << account_m->GetToken() << '\n'
                  << "Nonce: " << account_m->GetNonce() << '\n';
    }

    outcome::result<std::string> TransactionManager::TransferFunds( uint64_t    amount,
                                                                    std::string destination,
                                                                    TokenID     token_id )
    {
        if ( GetState() != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        BOOST_OUTCOME_TRY(
            auto params,

            account_m->GetUTXOManager().CreateTxParameter( amount, std::move( destination ), token_id ) );
        auto [inputs, outputs] = params;

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            TransferTransaction::New( inputs, outputs, FillDAGStruct() ) );

        transfer_transaction->MakeSignature( *account_m );

        account_m->GetUTXOManager().ReserveUTXOs( inputs, transfer_transaction->GetHash() );

        EnqueueTransaction( std::make_pair( transfer_transaction, std::nullopt ) );

        return transfer_transaction->GetHash();
    }

    outcome::result<std::string> TransactionManager::MintFunds( uint64_t    amount,
                                                                std::string transaction_hash,
                                                                std::string chainid,
                                                                TokenID     tokenid,
                                                                std::string destination )
    {
        if ( GetState() != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        if ( destination.empty() )
        {
            destination = account_m->GetAddress();
        }
        if ( chainid.empty() )
        {
            // MintV2 represents bridge/public-chain input. Empty chain id must not fall back to Genius validation.
            chainid = "public";
        }

        auto          source_hash = base::Hash256::fromReadableString( transaction_hash );
        base::Hash256 source_input_hash;
        if ( source_hash.has_error() )
        {
            TransactionManagerLogger()->warn(
                "[{} - full: {}] {}: Source hash parse inconsistency for mint tx_ref={}, using empty input hash and uncle_hash fallback",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                __func__,
                transaction_hash );
        }
        else
        {
            source_input_hash = source_hash.value();
        }

        std::vector<GeniusUTXO> source_utxos;
        source_utxos.emplace_back( source_input_hash, 0, amount, tokenid, account_m->GetAddress() );
        auto mint_inputs = account_m->CreateInputsFromUTXOs( source_utxos );

        auto mint_transaction = std::make_shared<MintTransactionV2>(
            MintTransactionV2::New( amount,
                                    std::move( chainid ),
                                    tokenid,
                                    FillDAGStruct( std::move( transaction_hash ) ),
                                    std::move( mint_inputs ),
                                    destination ) );

        mint_transaction->MakeSignature( *account_m );

        // Store the transaction ID before moving the transaction
        auto txId = mint_transaction->GetHash();

        EnqueueTransaction( std::make_pair( std::move( mint_transaction ), std::nullopt ) );

        return txId;
    }

    outcome::result<std::string> TransactionManager::MigrationFunds( uint64_t    amount,
                                                                     std::string from_version,
                                                                     TokenID     tokenid,
                                                                     std::string destination )
    {
        if ( GetState() != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        if ( destination.empty() )
        {
            destination = account_m->GetAddress();
        }

        auto migration_transaction = std::make_shared<MigrationTransaction>(
            MigrationTransaction::New( amount, std::move( from_version ), tokenid, FillDAGStruct(), destination ) );

        migration_transaction->MakeSignature( *account_m );

        auto txId = migration_transaction->GetHash();

        EnqueueTransaction( std::make_pair( std::move( migration_transaction ), std::nullopt ) );

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
        auto              hash_data = hasher_m->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );
        const std::string lock_id   = "0x" + hash_data.toReadableString();

        BOOST_OUTCOME_TRY(
            auto params,
            account_m->GetUTXOManager().CreateTxParameter( amount, lock_id, TokenID::FromBytes( { 0x00 } ) ) );
        auto [inputs, outputs]  = params;
        auto escrow_transaction = std::make_shared<EscrowTransaction>(
            EscrowTransaction::New( params, amount, dev_addr, peers_cut, FillDAGStruct( lock_id ) ) );

        escrow_transaction->MakeSignature( *account_m );
        account_m->GetUTXOManager().ReserveUTXOs( inputs, escrow_transaction->GetHash() );

        // Get the transaction ID for tracking
        auto txId = escrow_transaction->GetHash();

        EnqueueTransaction( std::make_pair( escrow_transaction, std::nullopt ) );

        crdt::GlobalDB::Buffer data_transaction;
        data_transaction.put( escrow_transaction->SerializeByteVector() );

        // Return both the transaction ID and the original EscrowDataPair
        return std::make_pair( txId, std::make_pair( lock_id, std::move( data_transaction ) ) );
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
            return std::errc::invalid_argument;
        }
        if ( escrow_path.empty() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Escrow path empty",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );
            return std::errc::invalid_argument;
        }
        TransactionManagerLogger()->debug( "[{} - full: {}] Fetching escrow from processing DB at {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           escrow_path );
        BOOST_OUTCOME_TRY( auto transaction, FetchTransaction( globaldb_m, escrow_path ) );

        std::shared_ptr<EscrowTransaction> escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( transaction );
        if ( crdt_transaction && escrow_tx && !escrow_tx->GetSrcAddress().empty() )
        {
            BOOST_OUTCOME_TRY( crdt_transaction->AddTopic( escrow_tx->GetSrcAddress() ) );
        }
        std::vector<std::string>    subtask_ids;
        std::vector<OutputDestInfo> payout_peers;

        BOOST_OUTCOME_TRY( auto escrow_amount_ptr, TokenAmount::New( escrow_tx->GetAmount() ) );

        BOOST_OUTCOME_TRY( auto peers_cut_ptr, TokenAmount::New( escrow_tx->GetPeersCut() ) );

        BOOST_OUTCOME_TRY( auto peer_total, escrow_amount_ptr->Multiply( *peers_cut_ptr ) );

        const auto escrowTokenId = escrow_tx->GetUTXOParameters().second[0].token_id;

        uint64_t peers_amount = peer_total.Value() / static_cast<uint64_t>( task_result.subtask_results().size() );
        auto     remainder    = escrow_tx->GetAmount();

        for ( auto &subtask : task_result.subtask_results() )
        {
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

        std::string lock_id = escrow_tx->GetUncleHash();
        if ( lock_id.empty() && !escrow_tx->GetUTXOParameters().second.empty() )
        {
            lock_id = escrow_tx->GetUTXOParameters().second[0].dest_address;
            TransactionManagerLogger()->warn(
                "[{} - full: {}] Escrow transaction {} has empty lock_id but has UTXO parameters - using dest_address as fallback lock_id: {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                escrow_tx->GetHash(),
                lock_id );
        }

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            TransferTransaction::New( std::vector{ escrow_utxo_input }, payout_peers, FillDAGStruct( lock_id ) ) );

        transfer_transaction->MakeSignature( *account_m );

        TransactionBatch tx_batch;
        tx_batch.push_back( std::make_pair( transfer_transaction, std::nullopt ) );
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
    SGTransaction::DAGStruct TransactionManager::FillDAGStruct( std::optional<std::string> other_chain_hash )
    {
        SGTransaction::DAGStruct dag;
        std::string              chain_hash;
        const auto               nonce         = account_m->ReserveNextNonce();
        auto                     previous_hash = GetOutgoingPreviousHash( nonce );
        auto                     timestamp     = std::chrono::system_clock::now();

        if ( other_chain_hash.has_value() )
        {
            chain_hash = std::move( other_chain_hash.value() );
        }

        dag.set_previous_hash( previous_hash );
        dag.set_nonce( nonce );
        dag.set_source_addr( account_m->GetAddress() );
        dag.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );
        dag.set_uncle_hash( chain_hash );

        return dag;
    }

    std::string TransactionManager::GetOutgoingPreviousHash( uint64_t nonce ) const
    {
        if ( nonce == 0 )
        {
            return "";
        }

        auto tracked_hash = GetTrackedOutgoingPreviousHash( nonce );
        if ( !tracked_hash.empty() )
        {
            return tracked_hash;
        }

        auto persisted_hash = GetPersistedOutgoingPreviousHash( nonce );
        if ( !persisted_hash.empty() )
        {
            return persisted_hash;
        }

        return QueryOutgoingPreviousHashFromCRDT( nonce );
    }

    std::string TransactionManager::GetTrackedOutgoingPreviousHash( uint64_t nonce ) const
    {
        {
            std::shared_lock tx_lock( tx_mutex_m );
            for ( const auto &[_, tracked] : tx_processed_m )
            {
                if ( !tracked.tx )
                {
                    continue;
                }
                if ( tracked.tx->GetSrcAddress() != account_m->GetAddress() )
                {
                    continue;
                }
                if ( tracked.cached_nonce != ( nonce - 1 ) )
                {
                    continue;
                }
                if ( tracked.status == TransactionStatus::FAILED || tracked.status == TransactionStatus::INVALID )
                {
                    continue;
                }
                return tracked.tx->GetHash();
            }
        }
        return "";
    }

    std::string TransactionManager::GetPersistedOutgoingPreviousHash( uint64_t nonce ) const
    {
        if ( nonce == 0 )
        {
            return "";
        }

        auto persisted_hash_result = account_m->GetLocalConfirmedTxHash( nonce - 1 );
        if ( persisted_hash_result.has_error() )
        {
            return "";
        }

        const auto &persisted_hash = persisted_hash_result.value();
        if ( persisted_hash.empty() || !blockchain_->CheckCertificate( persisted_hash ) )
        {
            return "";
        }

        TransactionManagerLogger()->debug(
            "[{} - full: {}] Recovered previous hash {} for nonce {} from persisted head",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            persisted_hash,
            nonce );
        return persisted_hash;
    }

    std::string TransactionManager::QueryOutgoingPreviousHashFromCRDT( uint64_t nonce ) const
    {
        if ( nonce == 0 )
        {
            return "";
        }

        const uint64_t expected_previous_nonce = nonce - 1;
        std::string    selected_hash;
        auto           monitored_networks = GetMonitoredNetworkIDs();
        for ( auto network_id : monitored_networks )
        {
            const std::string query_path = GetBlockChainBase( network_id ) + "tx";
            auto              tx_list    = globaldb_m->QueryKeyValues( query_path );
            if ( !tx_list.has_value() )
            {
                continue;
            }

            for ( const auto &[_, value] : tx_list.value() )
            {
                auto tx_result = DeSerializeTransaction( value );
                if ( !tx_result.has_value() || !tx_result.value() )
                {
                    continue;
                }

                const auto &candidate = tx_result.value();
                if ( candidate->GetSrcAddress() != account_m->GetAddress() ||
                     candidate->GetNonce() != expected_previous_nonce )
                {
                    continue;
                }

                if ( !blockchain_->CheckCertificate( candidate->GetHash() ) )
                {
                    continue;
                }

                if ( selected_hash.empty() ||
                     blockchain_->BestHash( selected_hash, candidate->GetHash() ) == candidate->GetHash() )
                {
                    selected_hash = candidate->GetHash();
                }
            }
        }

        if ( !selected_hash.empty() )
        {
            TransactionManagerLogger()->debug(
                "[{} - full: {}] Recovered previous hash {} for nonce {} from persisted transactions",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                selected_hash,
                nonce );
            return selected_hash;
        }
        return "";
    }

    std::string TransactionManager::GetValidationChainId( const std::shared_ptr<IGeniusTransactions> &tx ) const
    {
        if ( !tx )
        {
            return std::string( GENIUS_CHAIN_ID );
        }
        const auto chain_id = tx->GetChainId();
        if ( chain_id.empty() )
        {
            if ( tx->GetType() == "mint-v2" )
            {
                return "public";
            }
            return std::string( GENIUS_CHAIN_ID );
        }
        return chain_id;
    }

    const IInputValidator &TransactionManager::GetInputValidator( const std::string &chain_id ) const
    {
        if ( chain_id.empty() || chain_id == GENIUS_CHAIN_ID )
        {
            return genius_input_validator_;
        }

        return public_chain_input_validator_;
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
        std::optional<uint64_t> expected_next_nonce;
        if ( auto local_confirmed = account_m->GetLocalConfirmedNonce(); local_confirmed.has_value() )
        {
            expected_next_nonce = local_confirmed.value() + 1;
            TransactionManagerLogger()->debug( "[{} - full: {}] Using local confirmed nonce {} as send baseline",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               local_confirmed.value() );
        }
        else if ( !transaction_batch.empty() )
        {
            // If confirmed nonce is not available yet, preserve local enqueue order.
            expected_next_nonce = transaction_batch.front().first->GetNonce();
            TransactionManagerLogger()->debug( "[{} - full: {}] Local confirmed nonce unavailable, using first "
                                               "queued nonce {} as send baseline",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               expected_next_nonce.value() );
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
            if ( !expected_next_nonce.has_value() )
            {
                expected_next_nonce = transaction->GetNonce();
            }

            if ( transaction->GetNonce() != expected_next_nonce.value() )
            {
                if ( transaction->GetNonce() > expected_next_nonce.value() )
                {
                    TransactionManagerLogger()->debug(
                        "[{} - full: {}] Deferring transaction send due to nonce gap - Expected: {}, Tried to send: {}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        expected_next_nonce.value(),
                        transaction->GetNonce() );
                    return outcome::failure(
                        boost::system::errc::make_error_code( boost::system::errc::resource_unavailable_try_again ) );
                }

                TransactionManagerLogger()->error(
                    "[{} - full: {}] Transaction with unexpected nonce - Expected: {}, Tried to send: {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    expected_next_nonce.value(),
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
            BOOST_OUTCOME_TRY( crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

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
                BOOST_OUTCOME_TRY( crdt_transaction->Put( std::move( proof_key ), std::move( proof_transaction ) ) );
            }
            TransactionManagerLogger()->debug( "[{} - full: {}] Creating Consensus Proposal for tx {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               transaction_path );

            topicSet.merge( transaction->GetTopics() );
            transactions_sent.insert( transaction );

            expected_next_nonce = expected_next_nonce.value() + 1;
        }

        BOOST_OUTCOME_TRY( crdt_transaction->Commit( topicSet ) );

        for ( auto &transaction : transactions_sent )
        {
            const auto  chain_id           = GetValidationChainId( transaction );
            const auto &validator          = GetInputValidator( chain_id );
            const bool  utxo_data_required = validator.RequiresConsensusUTXOData();

            std::optional<UTXOTransitionCommitment> utxo_commitment;
            std::optional<UTXOWitness>              utxo_witness;

            if ( transaction->HasUTXOParameters() )
            {
                utxo_commitment = BuildUTXOTransitionCommitment( transaction );
                if ( !utxo_commitment.has_value() )
                {
                    TransactionManagerLogger()->error(
                        "[{} - full: {}] {}: Missing required UTXO commitment for tx={} type={}",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m,
                        __func__,
                        transaction->GetHash(),
                        transaction->GetType() );
                    return outcome::failure( std::errc::invalid_argument );
                }

                if ( utxo_data_required )
                {
                    utxo_witness = BuildUTXOWitness( transaction );
                    if ( !utxo_witness.has_value() )
                    {
                        TransactionManagerLogger()->error(
                            "[{} - full: {}] {}: Missing required UTXO witness for tx={} type={}",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m,
                            __func__,
                            transaction->GetHash(),
                            transaction->GetType() );
                        return outcome::failure( std::errc::invalid_argument );
                    }
                }
            }

            // Serialize tx for embedding in NonceSubject (per D-04, PROTO-01/SER-01)
            auto serialized_tx = transaction->SerializeByteVector();
            auto tx_type       = transaction->GetType();

            BOOST_OUTCOME_TRY( auto &&proposal,
                               blockchain_->CreateConsensusProposal( transaction->GetSrcAddress(),
                                                                     transaction->GetNonce(),
                                                                     transaction->GetHash(),
                                                                     tx_type,
                                                                     serialized_tx,
                                                                     utxo_commitment,
                                                                     utxo_witness ) );
            BOOST_OUTCOME_TRY( ChangeTransactionState( transaction, TransactionStatus::SENDING ) );
            BOOST_OUTCOME_TRY( blockchain_->SubmitProposal( proposal ) );
        }

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RollbackTransactions( TransactionItem &item_to_rollback )
    {
        auto [transaction_batch, _] = item_to_rollback;
        for ( auto &[transaction, maybe_proof] : transaction_batch )
        {
            BOOST_OUTCOME_TRY( ChangeTransactionState( transaction, TransactionStatus::FAILED ) );
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
        BOOST_OUTCOME_TRY( auto dag, IGeniusTransactions::DeSerializeDAGStruct( tx_data ) );

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

        BOOST_OUTCOME_TRY( ( this->*it->second.first )( tx ) );
        if ( DoesTransactionMutateUTXOState( tx ) && utxo_state_tracking_suppression_.load() == 0 )
        {
            UpdateAccountUTXOState( CollectTouchedAccounts( tx ), true );
        }
        return outcome::success();
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

        utxo_state_tracking_suppression_.fetch_add( 1 );
        auto revert_result = ( this->*( it->second.second ) )( tx );
        utxo_state_tracking_suppression_.fetch_sub( 1 );
        BOOST_OUTCOME_TRY( revert_result );
        if ( DoesTransactionMutateUTXOState( tx ) && utxo_state_tracking_suppression_.load() == 0 )
        {
            UpdateAccountUTXOState( CollectTouchedAccounts( tx ), false );
        }
        return outcome::success();
    }

    bool TransactionManager::DoesTransactionMutateUTXOState( const std::shared_ptr<IGeniusTransactions> &tx ) const
    {
        if ( !tx )
        {
            return false;
        }

        if ( tx->HasUTXOParameters() )
        {
            return true;
        }

        // Legacy mint transactions still create UTXOs for the source account.
        return tx->GetType() == "mint";
    }

    std::unordered_set<std::string> TransactionManager::CollectTouchedAccounts(
        const std::shared_ptr<IGeniusTransactions> &tx ) const
    {
        std::unordered_set<std::string> addresses;
        if ( !tx )
        {
            return addresses;
        }

        if ( tx->HasUTXOParameters() )
        {
            auto params_opt = tx->GetUTXOParametersOpt();
            if ( params_opt.has_value() )
            {
                const auto &[inputs, outputs] = params_opt.value();
                if ( !inputs.empty() )
                {
                    if ( full_node_m || tx->GetSrcAddress() == account_m->GetAddress() )
                    {
                        addresses.insert( tx->GetSrcAddress() );
                    }
                }
                for ( const auto &output : outputs )
                {
                    if ( !output.dest_address.empty() &&
                         ( full_node_m || output.dest_address == account_m->GetAddress() ) )
                    {
                        addresses.insert( output.dest_address );
                    }
                }
            }
        }
        else if ( tx->GetType() == "mint" && !tx->GetSrcAddress().empty() &&
                  ( full_node_m || tx->GetSrcAddress() == account_m->GetAddress() ) )
        {
            addresses.insert( tx->GetSrcAddress() );
        }

        return addresses;
    }

    TransactionManager::AccountUTXOState TransactionManager::GetOrInitAccountUTXOState(
        const std::string &address ) const
    {
        const auto current_root = account_m->GetUTXOManager().ComputeUTXOMerkleRoot( address );

        std::unique_lock state_lock( account_utxo_state_mutex_ );
        auto            &state = account_utxo_state_[address];
        if ( !state.initialized )
        {
            state.version     = 0;
            state.initialized = true;
        }
        state.root = current_root;
        return state;
    }

    void TransactionManager::UpdateAccountUTXOState( const std::unordered_set<std::string> &addresses,
                                                     bool                                   increment_version )
    {
        if ( addresses.empty() )
        {
            return;
        }

        std::unordered_map<std::string, base::Hash256> roots;
        roots.reserve( addresses.size() );
        for ( const auto &address : addresses )
        {
            if ( !full_node_m && address != account_m->GetAddress() )
            {
                continue;
            }
            roots.emplace( address, account_m->GetUTXOManager().ComputeUTXOMerkleRoot( address ) );
        }

        std::unique_lock state_lock( account_utxo_state_mutex_ );
        for ( const auto &[address, root] : roots )
        {
            auto &state = account_utxo_state_[address];
            if ( !state.initialized )
            {
                state.version     = 0;
                state.initialized = true;
            }
            if ( increment_version )
            {
                state.version++;
            }
            else if ( state.version > 0 )
            {
                state.version--;
            }
            state.root = root;
        }
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::FetchTransaction(
        const std::shared_ptr<crdt::GlobalDB> &db,
        std::string_view                       transaction_key )
    {
        BOOST_OUTCOME_TRY( auto transaction_data, db->Get( { std::string( transaction_key ) } ) );

        return DeSerializeTransaction( transaction_data );
    }

    outcome::result<std::shared_ptr<IGeniusTransactions>> TransactionManager::DeSerializeTransaction(
        const base::Buffer &tx_data )
    {
        const auto &transaction_data_vector = tx_data.toVector();

        BOOST_OUTCOME_TRY( auto dag, IGeniusTransactions::DeSerializeDAGStruct( transaction_data_vector ) );

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
        BOOST_OUTCOME_TRY( auto proof_data, globaldb_m->Get( { proof_path } ) );

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
            BOOST_OUTCOME_TRY( auto transaction_list, globaldb_m->QueryKeyValues( query_path ) );

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
        BOOST_OUTCOME_TRY( ChangeTransactionState( transaction, next_tx_state ) );

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
            BOOST_OUTCOME_TRY( account_m->GetUTXOManager().PutUTXO( new_utxo, dest_infos[i].dest_address ) );

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
        BOOST_OUTCOME_TRY(
            account_m->GetUTXOManager().ConsumeUTXOs( transfer_tx->GetInputInfos(), transfer_tx->GetSrcAddress() ) );
        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        if ( auto migration_tx = std::dynamic_pointer_cast<MigrationTransaction>( tx ) )
        {
            auto [inputs, outputs] = migration_tx->GetUTXOParameters();
            auto hash              = ( base::Hash256::fromReadableString( migration_tx->GetHash() ) ).value();
            for ( std::uint32_t i = 0; i < outputs.size(); ++i )
            {
                GeniusUTXO new_utxo( hash, i, outputs[i].encrypted_amount, outputs[i].token_id );
                account_m->GetUTXOManager().PutUTXO( new_utxo, outputs[i].dest_address );
            }

            if ( !inputs.empty() )
            {
                account_m->GetUTXOManager().ConsumeUTXOs( inputs, migration_tx->GetSrcAddress() );
            }

            TransactionManagerLogger()->info( "[{} - full: {}] Created tokens (migration), amount {} balance {}",
                                              account_m->GetAddress().substr( 0, 8 ),
                                              full_node_m,
                                              std::to_string( migration_tx->GetAmount() ),
                                              std::to_string( account_m->GetUTXOManager().GetBalance() ) );
            return outcome::success();
        }

        if ( auto mint_tx_v2 = std::dynamic_pointer_cast<MintTransactionV2>( tx ) )
        {
            auto [inputs, outputs] = mint_tx_v2->GetUTXOParameters();
            auto hash              = ( base::Hash256::fromReadableString( mint_tx_v2->GetHash() ) ).value();
            for ( std::uint32_t i = 0; i < outputs.size(); ++i )
            {
                GeniusUTXO new_utxo( hash, i, outputs[i].encrypted_amount, outputs[i].token_id );
                account_m->GetUTXOManager().PutUTXO( new_utxo, outputs[i].dest_address );
            }

            if ( !inputs.empty() )
            {
                account_m->GetUTXOManager().ConsumeUTXOs( inputs, mint_tx_v2->GetSrcAddress() );
            }

            TransactionManagerLogger()->info( "[{} - full: {}] Created tokens (mint-v2), amount {} balance {}",
                                              account_m->GetAddress().substr( 0, 8 ),
                                              full_node_m,
                                              std::to_string( mint_tx_v2->GetAmount() ),
                                              std::to_string( account_m->GetUTXOManager().GetBalance() ) );
            return outcome::success();
        }

        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );
        if ( !mint_tx )
        {
            return std::errc::invalid_argument;
        }

        auto hash = ( base::Hash256::fromReadableString( mint_tx->GetHash() ) ).value();
        BOOST_OUTCOME_TRY(
            account_m->GetUTXOManager().PutUTXO( GeniusUTXO( hash, 0, mint_tx->GetAmount(), mint_tx->GetTokenID() ),
                                                 mint_tx->GetSrcAddress() ) );
        TransactionManagerLogger()->info( "[{} - full: {}] Created tokens, amount {} balance {}",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          std::to_string( mint_tx->GetAmount() ),
                                          std::to_string( account_m->GetUTXOManager().GetBalance() ) );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );
        if ( !escrow_tx )
        {
            return std::errc::invalid_argument;
        }

        auto [inputs, outputs] = escrow_tx->GetUTXOParameters();
        auto hash              = ( base::Hash256::fromReadableString( escrow_tx->GetHash() ) ).value();

        for ( std::uint32_t i = 0; i < outputs.size(); ++i )
        {
            // output[0] is escrow hold, optional output[1] is change.
            GeniusUTXO new_utxo( hash, i, outputs[i].encrypted_amount, outputs[i].token_id );
            BOOST_OUTCOME_TRY( account_m->GetUTXOManager().PutUTXO( new_utxo, outputs[i].dest_address ) );
        }

        if ( !inputs.empty() )
        {
            BOOST_OUTCOME_TRY( account_m->GetUTXOManager().ConsumeUTXOs( inputs, escrow_tx->GetSrcAddress() ) );
        }

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertTransferTransaction(
        const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx );
        auto dest_infos  = transfer_tx->GetDstInfos();

        for ( std::uint32_t i = 0; i < dest_infos.size(); ++i )
        {
            const auto &dest_info = dest_infos[i];
            auto        hash      = ( base::Hash256::fromReadableString( transfer_tx->GetHash() ) ).value();
            BOOST_OUTCOME_TRY( account_m->GetUTXOManager().DeleteUTXO( hash, i, dest_info.dest_address ) );

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
                BOOST_OUTCOME_TRY( ParseTransaction( tx ) );
            }
        }
        account_m->GetUTXOManager().RollbackUTXOs( transfer_tx->GetInputInfos(), transfer_tx->GetHash() );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertMintTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        if ( auto migration_tx = std::dynamic_pointer_cast<MigrationTransaction>( tx ) )
        {
            auto [inputs, outputs] = migration_tx->GetUTXOParameters();
            auto hash              = ( base::Hash256::fromReadableString( migration_tx->GetHash() ) ).value();

            for ( std::uint32_t i = 0; i < outputs.size(); ++i )
            {
                const auto &dest_info = outputs[i];
                BOOST_OUTCOME_TRY( account_m->GetUTXOManager().DeleteUTXO( hash, i, dest_info.dest_address ) )
            }
            if ( !inputs.empty() )
            {
                account_m->GetUTXOManager().RollbackUTXOs( inputs, tx->GetHash() );
            }

            TransactionManagerLogger()->info(
                "[{} - full: {}] Deleted {} tokens (migration), from tx {}, final balance {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                migration_tx->GetAmount(),
                migration_tx->GetHash(),
                std::to_string( account_m->GetUTXOManager().GetBalance() ) );
            return outcome::success();
        }

        if ( auto mint_tx_v2 = std::dynamic_pointer_cast<MintTransactionV2>( tx ) )
        {
            auto [inputs, outputs] = mint_tx_v2->GetUTXOParameters();
            auto hash              = ( base::Hash256::fromReadableString( mint_tx_v2->GetHash() ) ).value();

            for ( std::uint32_t i = 0; i < outputs.size(); ++i )
            {
                const auto &dest_info = outputs[i];
                BOOST_OUTCOME_TRY( account_m->GetUTXOManager().DeleteUTXO( hash, i, dest_info.dest_address ) )
            }
            if ( !inputs.empty() )
            {
                account_m->GetUTXOManager().RollbackUTXOs( inputs, tx->GetHash() );
            }

            TransactionManagerLogger()->info(
                "[{} - full: {}] Deleted {} tokens (mint-v2), from tx {}, final balance {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                mint_tx_v2->GetAmount(),
                mint_tx_v2->GetHash(),
                std::to_string( account_m->GetUTXOManager().GetBalance() ) );
            return outcome::success();
        }

        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );
        if ( !mint_tx )
        {
            return std::errc::invalid_argument;
        }

        auto hash = ( base::Hash256::fromReadableString( mint_tx->GetHash() ) ).value();
        BOOST_OUTCOME_TRY( account_m->GetUTXOManager().DeleteUTXO( hash, 0, mint_tx->GetSrcAddress() ) );
        TransactionManagerLogger()->info( "[{} - full: {}] Deleted {} tokens, from tx {}, final balance {}",
                                          account_m->GetAddress().substr( 0, 8 ),
                                          full_node_m,
                                          mint_tx->GetAmount(),
                                          mint_tx->GetHash(),
                                          std::to_string( account_m->GetUTXOManager().GetBalance() ) );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertEscrowTransaction( const std::shared_ptr<IGeniusTransactions> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );
        if ( !escrow_tx )
        {
            return std::errc::invalid_argument;
        }

        if ( auto [inputs, outputs] = escrow_tx->GetUTXOParameters(); !outputs.empty() )
        {
            auto hash = ( base::Hash256::fromReadableString( escrow_tx->GetHash() ) ).value();
            for ( std::uint32_t i = 0; i < outputs.size(); ++i )
            {
                BOOST_OUTCOME_TRY( account_m->GetUTXOManager().DeleteUTXO( hash, i, outputs[i].dest_address ) );
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
                    BOOST_OUTCOME_TRY( ParseTransaction( tx ) );
                }
            }
            account_m->GetUTXOManager().RollbackUTXOs( inputs, escrow_tx->GetHash() );
        }

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
        auto start              = std::chrono::steady_clock::now();
        auto escrow_hash_result = base::Hash256::fromReadableString( originalEscrowId );
        if ( escrow_hash_result.has_error() )
        {
            TransactionManagerLogger()->warn( "[{} - full: {}] Invalid original escrow tx id while waiting release: {}",
                                              account_m->GetAddress().substr( 0, 8 ),
                                              full_node_m,
                                              originalEscrowId );
            return TransactionStatus::INVALID;
        }
        const auto escrow_hash = escrow_hash_result.value();

        auto is_escrow_spent_by_confirmed_transfer = [this, &escrow_hash]() -> bool
        {
            std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            for ( const auto &[_, tracked] : tx_processed_m )
            {
                if ( tracked.status != TransactionStatus::CONFIRMED || !tracked.tx || !tracked.tx->HasUTXOParameters() )
                {
                    continue;
                }

                const auto params_opt = tracked.tx->GetUTXOParametersOpt();
                if ( !params_opt.has_value() )
                {
                    continue;
                }

                const auto &inputs                 = params_opt->first;
                const bool  spends_original_escrow = std::any_of(
                    inputs.begin(),
                    inputs.end(),
                    [&escrow_hash]( const InputUTXOInfo &input )
                    { return input.txid_hash_ == escrow_hash && input.output_idx_ == 0; } );

                if ( spends_original_escrow )
                {
                    return true;
                }
            }
            return false;
        };

        while ( std::chrono::steady_clock::now() - start < timeout )
        {
            if ( account_m->GetUTXOManager().IsOutPointConsumed( escrow_hash, 0 ) )
            {
                TransactionManagerLogger()->debug( "[{} - full: {}] Escrow hold ({},0) is consumed",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   originalEscrowId );
                return TransactionStatus::CONFIRMED;
            }

            if ( is_escrow_spent_by_confirmed_transfer() )
            {
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] Escrow release confirmed via tracked transfer spend for {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    originalEscrowId );
                return TransactionStatus::CONFIRMED;
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }

        return TransactionStatus::INVALID;
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

        auto utxo_result = account_m->GetUTXOManager().LoadUTXOs( globaldb_m->GetDataStore() );
        if ( utxo_result.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] Failed to load UTXOs from storage",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m );
        }

        bool has_local_utxos    = utxo_result.has_value() && utxo_result.value();
        auto monitored_networks = GetMonitoredNetworkIDs();

        if ( has_local_utxos )
        {
            auto checkpoint_result = account_m->GetUTXOManager().LoadLatestCheckpoint( account_m->GetAddress() );
            if ( checkpoint_result.has_error() )
            {
                TransactionManagerLogger()->warn(
                    "[{} - full: {}] Failed to load local UTXO checkpoint during init: {}",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    checkpoint_result.error().message() );
            }
            else if ( checkpoint_result.value().has_value() )
            {
                const auto local_root = account_m->GetUTXOManager().ComputeUTXOMerkleRoot( account_m->GetAddress() );
                if ( local_root != checkpoint_result.value()->utxo_merkle_root )
                {
                    TransactionManagerLogger()->warn(
                        "[{} - full: {}] Local UTXO root mismatch with checkpoint during init. Clearing local UTXOs and rebuilding",
                        account_m->GetAddress().substr( 0, 8 ),
                        full_node_m );

                    auto clear_result = account_m->GetUTXOManager().SetUTXOs( std::vector<GeniusUTXO>{},
                                                                              account_m->GetAddress() );
                    if ( clear_result.has_error() )
                    {
                        TransactionManagerLogger()->error(
                            "[{} - full: {}] Failed to clear local UTXOs after checkpoint mismatch: {}",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m,
                            clear_result.error().message() );
                    }
                    else
                    {
                        has_local_utxos = false;
                    }
                }
            }
        }

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

        auto utxo_map = account_m->GetUTXOManager().GetAllUTXOs();

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
        auto local_nonce_result = account_m->GetLocalConfirmedNonce();
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
            BOOST_OUTCOME_TRY( RemoveTransactionFromProcessedMaps( *it, true ) );
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

        BOOST_OUTCOME_TRY( crdt_transaction->Remove( { std::move( tx_key ) } ) );

        TransactionManagerLogger()->debug( "[{} - full: {}] Removed key transaction on {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           tx_key );

        BOOST_OUTCOME_TRY( crdt_transaction->Commit( topics ) );

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
                    BOOST_OUTCOME_TRY( RevertTransaction( it->second.tx ) );
                    if ( delete_from_crdt )
                    {
                        auto topics = it->second.tx->GetTopics();
                        BOOST_OUTCOME_TRY( DeleteTransaction( transaction_key, topics ) );
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

        BOOST_OUTCOME_TRY( auto new_tx, DeSerializeTransaction( value ) );

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
                BOOST_OUTCOME_TRY( ChangeTransactionState( new_tx, TransactionStatus::FAILED ) );
                tx_lock.lock();
                return outcome::failure( boost::system::error_code{} );
            }
            TransactionManagerLogger()->warn(
                "[{} - full: {}] Setting conflicting transaction to VERIFYING since it's not confirmed: {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                conflicting_tx.value()->GetHash() );
            tx_lock.unlock();
            BOOST_OUTCOME_TRY( ChangeTransactionState( conflicting_tx.value(), TransactionStatus::VERIFYING ) );
        }

        TransactionManagerLogger()->debug(
            "[{} - full: {}] Checking if the transaction has a valid certificate to be confirmed {}",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            key );

        auto next_tx_state = TransactionStatus::VERIFYING;
        auto has_cert      = blockchain_->CheckCertificate( new_tx->GetHash() );

        if ( has_cert )
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
                BOOST_OUTCOME_TRY( ChangeTransactionState( conflicting_tx.value(), TransactionStatus::FAILED ) );
            }
        }

        auto maybe_existing = GetTrackedTxByHash( new_tx->GetHash() );
        if ( maybe_existing.has_value() && next_tx_state == TransactionStatus::VERIFYING )
        {
            const auto current_status = maybe_existing->status;
            if ( current_status == TransactionStatus::FAILED || current_status == TransactionStatus::CONFIRMED )
            {
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] Keeping terminal status {} for tx {}, skipping downgrade to VERIFYING (has_cert={})",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    static_cast<int>( current_status ),
                    new_tx->GetHash(),
                    has_cert );
                return outcome::success();
            }
        }

        return ChangeTransactionState( new_tx, next_tx_state );
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

        std::size_t queue_size = 0;
        {
            std::lock_guard lock( cv_mutex_ );
            new_data_queue_.push( std::move( new_data ) );
            queue_size = new_data_queue_.size();
        }

        cv_.notify_one();

        TransactionManagerLogger()->debug( "[{} - full: {}] CRDT new data queued, {} - (queue size: {})",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           key,
                                           queue_size );
    }

    void TransactionManager::DeleteElementCallback( std::string deleted_key )
    {
        std::size_t queue_size = 0;
        {
            std::lock_guard lock( cv_mutex_ );
            deleted_data_queue_.push( deleted_key );
            queue_size = deleted_data_queue_.size();
        }
        cv_.notify_one();

        TransactionManagerLogger()->debug( "[{} - full: {}] CRDT deleted key queued, {} - (queue size: {})",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           deleted_key,
                                           queue_size );
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

    bool TransactionManager::HasConfirmedInputConflict( const std::shared_ptr<IGeniusTransactions> &candidate_tx ) const
    {
        if ( !candidate_tx || !candidate_tx->HasUTXOParameters() )
        {
            return false;
        }

        auto candidate_params = candidate_tx->GetUTXOParametersOpt();
        if ( !candidate_params.has_value() )
        {
            return false;
        }

        std::unordered_set<std::string> candidate_inputs;
        candidate_inputs.reserve( candidate_params->first.size() );
        for ( const auto &input : candidate_params->first )
        {
            candidate_inputs.insert( OutPointKey( input.txid_hash_, input.output_idx_ ) );
        }

        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( const auto &[_, tracked] : tx_processed_m )
        {
            if ( !tracked.tx || tracked.status != TransactionStatus::CONFIRMED ||
                 tracked.tx->GetHash() == candidate_tx->GetHash() || !tracked.tx->HasUTXOParameters() )
            {
                continue;
            }

            auto other_params = tracked.tx->GetUTXOParametersOpt();
            if ( !other_params.has_value() )
            {
                continue;
            }

            for ( const auto &other_input : other_params->first )
            {
                if ( candidate_inputs.find( OutPointKey( other_input.txid_hash_, other_input.output_idx_ ) ) !=
                     candidate_inputs.end() )
                {
                    return true;
                }
            }
        }
        return false;
    }

    outcome::result<ConsensusManager::Check> TransactionManager::OnConsensusCertificate( const std::string &tx_hash )
    {
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Consensus certificate arrived for transaction {}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx_hash );
        auto tx = GetTransactionByHash( tx_hash );
        if ( !tx )
        {
            TransactionManagerLogger()->warn(
                "[{} - full: {}] {}: Transaction not found for hash {}. Accepting certificate callback and waiting for tx ingestion path",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                __func__,
                tx_hash );
            return ConsensusManager::Check::Approve;
        }
        // TRACK-01 per D-02: Promote temp embedded-tx entry from VERIFYING to CONFIRMED
        {
            std::unique_lock tx_lock( tx_mutex_m );
            auto it = tx_processed_m.find( GetTransactionPath( tx_hash ) );
            if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::VERIFYING )
            {
                it->second.status = TransactionStatus::CONFIRMED;
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] {}: Promoted temp embedded-tx entry to CONFIRMED for tx {}",
                    account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__, tx_hash );
            }
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
                    return outcome::failure( result.error() );
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
            return outcome::failure( result.error() );
        }
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Transaction {} confirmed by consensus",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx_hash );

        auto tx_hash_bin = base::Hash256::fromReadableString( tx_hash );
        if ( tx_hash_bin.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Could not parse tx hash for checkpoint tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return outcome::failure( tx_hash_bin.error() );
        }

        auto validator_registry = blockchain_->GetValidatorRegistry();
        if ( !validator_registry )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: No validator registry, skipping checkpoint",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__ );
            return outcome::failure( std::errc::no_such_device );
        }

        const uint64_t registry_epoch = validator_registry->GetRegistryEpoch();
        const auto     registry_cid   = validator_registry->GetRegistryCid();
        auto           registry_hash  = hasher_m->sha2_256( registry_cid.data(), registry_cid.size() );

        if ( auto checkpoint_res = account_m->GetUTXOManager().CreateCheckpoint( registry_epoch,
                                                                                 tx_hash_bin.value(),
                                                                                 registry_hash );
             checkpoint_res.has_error() )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Failed to create UTXO checkpoint tx={} epoch={} err={}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                __func__,
                tx_hash,
                registry_epoch,
                checkpoint_res.error().message() );
        }
        return ConsensusManager::Check::Approve;
    }

    outcome::result<ConsensusManager::Check> TransactionManager::HandleBridgeEventConsensusSubject(
        const eth::BridgeEventClaim       &claim,
        const ConsensusManager::Subject   &subject )
    {
        (void)subject;
        if ( claim.src_chain_id == 0 || claim.dest_chain_id == 0 )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Rejecting bridge event with empty chain id",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__ );
            return ConsensusManager::Check::Reject;
        }
        if ( CreateBridgeEventMintRequest( claim ).has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Rejecting bridge event that cannot map to mint input",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__ );
            return ConsensusManager::Check::Reject;
        }
        return ConsensusManager::Check::Approve;
    }

    outcome::result<ConsensusManager::Check> TransactionManager::OnBridgeEventConsensusCertificate(
        const eth::BridgeEventClaim          &claim,
        const std::string                    &subject_hash,
        const ConsensusManager::Certificate  &certificate )
    {
        (void)certificate;
        if ( GetState() != State::READY )
        {
            TransactionManagerLogger()->warn(
                "[{} - full: {}] {}: Bridge event certificate {} arrived before transaction manager is ready",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                __func__,
                subject_hash.substr( 0, 8 ) );
            return ConsensusManager::Check::Stalled;
        }

        BOOST_OUTCOME_TRY( auto mint_request, CreateBridgeEventMintRequest( claim ) );
        auto tx_result = MintFunds( mint_request.amount,
                                    mint_request.transaction_hash,
                                    mint_request.chain_id,
                                    mint_request.token_id,
                                    mint_request.destination );
        if ( tx_result.has_error() )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Failed to route bridge event certificate {} to mint path: {}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                __func__,
                subject_hash.substr( 0, 8 ),
                tx_result.error().message() );
            return outcome::failure( tx_result.error() );
        }

        TransactionManagerLogger()->info(
            "[{} - full: {}] {}: Routed bridge event certificate {} to mint transaction {}",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            __func__,
            subject_hash.substr( 0, 8 ),
            tx_result.value() );
        return ConsensusManager::Check::Approve;
    }

    outcome::result<ConsensusManager::Check> TransactionManager::HandleNonceConsensusSubject(
        const ConsensusManager::Subject &subject )
    {
        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        if ( nonce_subject.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Received unexpected subject payload",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        const std::string tx_hash = nonce_subject.value().tx_hash();
        const auto        key     = GetTransactionPath( tx_hash );

        // SANTZ-01: Sanitization sandwich — validate before deserializing untrusted PubSub bytes
        // DESER-01: Deserialize from embedded bytes instead of CRDT lookup
        static constexpr size_t MAX_EMBEDDED_TX_BYTES = 64 * 1024;

        const auto &tx_data = nonce_subject.value().transaction_data();
        if ( tx_data.empty() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Empty embedded tx data, rejecting",
                                               account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__ );
            return ConsensusManager::Check::Reject;
        }

        // SANTZ-01 Stage 1: Hard size cap before any protobuf parse
        if ( tx_data.size() > MAX_EMBEDDED_TX_BYTES )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Embedded tx data exceeds max size {} > {}, rejecting",
                account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__,
                tx_data.size(), MAX_EMBEDDED_TX_BYTES );
            return ConsensusManager::Check::Reject;
        }

        // SANTZ-01 Stage 2: Hash integrity gate before deserialization
        {
            auto computed_hash = hasher_m->blake2b_256(
                gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( tx_data.data() ),
                                          tx_data.size() ) );
            if ( computed_hash.toReadableString() != tx_hash )
            {
                TransactionManagerLogger()->error(
                    "[{} - full: {}] {}: Embedded tx hash mismatch, rejecting tx_hash={}",
                    account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__, tx_hash );
                return ConsensusManager::Check::Reject;
            }
        }

        // SANTZ-01 Stage 3: Bounded protobuf parse
        auto tx_result = DeSerializeTransaction( std::string( tx_data.begin(), tx_data.end() ) );
        if ( tx_result.has_error() )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Failed to deserialize embedded tx for hash {}",
                account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__, tx_hash );
            return ConsensusManager::Check::Reject;
        }
        auto tx = tx_result.value();

        // Hash binding verification — cryptographic integrity gate (defense-in-depth)
        if ( tx->GetHash() != tx_hash )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Hash binding mismatch, tx->GetHash() != subject.tx_hash for {}",
                account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__, tx_hash );
            return ConsensusManager::Check::Reject;
        }

        // BIND-01: Commitment-tx binding cross-check
        if ( nonce_subject.value().has_utxo_commitment() )
        {
            if ( !tx->HasUTXOParameters() )
            {
                TransactionManagerLogger()->error(
                    "[{} - full: {}] {}: Subject has UTXO commitment but deserialized tx lacks "
                    "UTXO parameters — possible malicious embedding, rejecting tx={}",
                    account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__, tx_hash );
                return ConsensusManager::Check::Reject;
            }

            auto reconstructed = BuildUTXOTransitionCommitment( tx );
            if ( !reconstructed.has_value() ||
                 reconstructed->consumed_outpoints_root() !=
                     nonce_subject.value().utxo_commitment().consumed_outpoints_root() ||
                 reconstructed->produced_outputs_root() !=
                     nonce_subject.value().utxo_commitment().produced_outputs_root() )
            {
                TransactionManagerLogger()->error(
                    "[{} - full: {}] {}: Commitment-tx binding mismatch — "
                    "reconstructed commitment differs from subject claim for tx={}",
                    account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__, tx_hash );
                return ConsensusManager::Check::Reject;
            }
        }

        // TRACK-01: Insert temporary tracking entry (per D-01: on proposal arrival after deserialization)
        uint64_t          tracked_nonce  = tx->GetNonce();
        TransactionStatus tracked_status = TransactionStatus::VERIFYING;
        {
            std::unique_lock tx_lock( tx_mutex_m );
            auto it = tx_processed_m.find( key );
            if ( it == tx_processed_m.end() )
            {
                tx_processed_m.emplace( key, TrackedTx{ tx, TransactionStatus::VERIFYING, tracked_nonce } );
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] {}: Inserted temp tracking entry for embedded tx {}",
                    account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__, tx_hash );
            }
            else if ( it->second.status == TransactionStatus::FAILED )
            {
                TransactionManagerLogger()->debug(
                    "[{} - full: {}] {}: Transaction {} previously FAILED, rejecting",
                    account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__, tx_hash );
                return ConsensusManager::Check::Reject;
            }
            else
            {
                // Entry already exists with higher-status — use its values for downstream checks
                tracked_status = it->second.status;
                tracked_nonce = it->second.cached_nonce;
            }
        }

        if ( !tx )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Tracked transaction missing for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return outcome::failure( std::errc::invalid_argument );
        }

        auto reject_and_maybe_fail_local = [&]( const char *reason ) -> ConsensusManager::Check
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Rejecting nonce subject for hash {}: {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash,
                                               reason );

            // Ensure local outgoing invalid transactions don't stay in VERIFYING forever.
            if ( tx->GetSrcAddress() == account_m->GetAddress() )
            {
                auto current_out_status = GetOutgoingStatusByTxId( tx->GetHash() );
                if ( current_out_status != TransactionStatus::FAILED &&
                     current_out_status != TransactionStatus::CONFIRMED )
                {
                    if ( auto fail_result = ChangeTransactionState( tx, TransactionStatus::FAILED );
                         fail_result.has_error() )
                    {
                        TransactionManagerLogger()->error(
                            "[{} - full: {}] {}: Failed to mark rejected local tx as FAILED for hash {}: {}",
                            account_m->GetAddress().substr( 0, 8 ),
                            full_node_m,
                            __func__,
                            tx_hash,
                            fail_result.error().message() );
                    }
                }
            }

            // TRACK-01 per D-02: Cleanup temp tracking entry on reject
            {
                std::unique_lock tx_lock( tx_mutex_m );
                auto it = tx_processed_m.find( GetTransactionPath( tx_hash ) );
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::VERIFYING )
                {
                    tx_processed_m.erase( it );
                    TransactionManagerLogger()->debug(
                        "[{} - full: {}] {}: Removed temp tracking entry for rejected embedded tx {}",
                        account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__, tx_hash );
                }
            }

            return ConsensusManager::Check::Reject;
        };

        if ( tracked_nonce != nonce_subject.value().nonce() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Nonce mismatch for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return reject_and_maybe_fail_local( "nonce mismatch" );
        }

        if ( !subject.account_id().empty() && tx->GetSrcAddress() != subject.account_id() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Account mismatch for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return reject_and_maybe_fail_local( "account mismatch" );
        }

        if ( tracked_status == TransactionStatus::FAILED )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Transaction status invalid for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return reject_and_maybe_fail_local( "transaction already failed" );
        }

        if ( HasConfirmedInputConflict( tx ) )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Outpoint conflict against finalized transaction "
                                               "for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return reject_and_maybe_fail_local( "input outpoint already finalized by another transaction" );
        }

        const auto witness_validation = ValidateWitnessForConsensus( subject, tx );
        if ( witness_validation == WitnessValidationResult::INVALID )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Witness validation failed for hash {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx_hash );
            return reject_and_maybe_fail_local( "witness validation failed" );
        }

        if ( auto migration_tx = std::dynamic_pointer_cast<MigrationTransaction>( tx ) )
        {
            MigrationAllowList allow_list( globaldb_m->GetDataStore(), migration_tx->GetFromVersion() );
            auto eligibility_result = allow_list.IsEligible( migration_tx->GetSrcAddress(), migration_tx->GetAmount() );
            if ( eligibility_result.has_error() )
            {
                TransactionManagerLogger()->warn(
                    "[{} - full: {}] {}: Failed to evaluate local migration allowlist tx={} src={} err={}, pending",
                    account_m->GetAddress().substr( 0, 8 ),
                    full_node_m,
                    __func__,
                    tx_hash,
                    migration_tx->GetSrcAddress(),
                    eligibility_result.error().message() );
                return ConsensusManager::Check::Pending;
            }
            if ( !eligibility_result.value() )
            {
                return reject_and_maybe_fail_local( "migration source address not locally eligible" );
            }
        }

        auto validate_result = ValidateTransactionForConsensus( tx );

        if ( !validate_result )
        {
            return reject_and_maybe_fail_local( "transaction validation failed" );
        }

        return ConsensusManager::Check::Approve;
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

        if ( !account_m->GetUTXOManager().VerifyParameters( params, address ) )
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
        //TODO - Deal with checking the Mint
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

        const auto elapsed      = GetElapsedTime( ts );
        const auto tolerance_ms = static_cast<int64_t>( timestamp_tolerance_m.count() );
        const auto drift_ms     = elapsed >= 0 ? elapsed : -elapsed;

        if ( tolerance_ms > 0 && drift_ms > tolerance_ms )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Timestamp out of tolerance tx={} (elapsed: {} ms, tolerance: {} ms)",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetHash(),
                                               elapsed,
                                               tolerance_ms );
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

        if ( tx.GetNonce() > 0 )
        {
            const auto previous_hash = tx.GetPreviousHash();
            if ( previous_hash.empty() )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] {}: Missing previous hash tx={}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__,
                                                   tx.GetHash() );
                return false;
            }
            auto previous_cert_result = blockchain_->GetCertificateBySubjectHash( previous_hash );
            if ( previous_cert_result.has_error() )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] {}: Missing previous certificate for hash {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__,
                                                   previous_hash );
                return false;
            }
            const auto &previous_subject = previous_cert_result.value().proposal().subject();
            auto        previous_nonce   = ConsensusManager::DecodeNonceSubject( previous_subject );
            if ( previous_nonce.has_error() )
            {
                return false;
            }
            if ( previous_subject.account_id() != tx.GetSrcAddress() )
            {
                return false;
            }
            if ( ( previous_nonce.value().nonce() + 1 ) != tx.GetNonce() )
            {
                return false;
            }
        }

        auto nonce_result = account_m->GetPeerNonce( tx.GetSrcAddress() );
        if ( nonce_result.has_error() )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] {}: No confirmed nonce for address {}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx.GetSrcAddress() );
            return true;
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

        if ( tx->HasUTXOParameters() )
        {
            auto params_opt = tx->GetUTXOParametersOpt();
            if ( !params_opt.has_value() )
            {
                TransactionManagerLogger()->error( "[{} - full: {}] {}: Missing UTXO parameters for tx={}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__,
                                                   tx->GetHash() );
                return false;
            }
            const auto  chain_id  = GetValidationChainId( tx );
            const auto &validator = GetInputValidator( chain_id );
            return validator.ValidateUTXOParameters( params_opt.value(),
                                                     tx->GetSrcAddress(),
                                                     account_m->GetUTXOManager() );
        }

        return true;
    }

    TransactionManager::WitnessValidationResult TransactionManager::ValidateWitnessForConsensus(
        const ConsensusSubject                     &subject,
        const std::shared_ptr<IGeniusTransactions> &tx ) const
    {
        if ( !tx )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Null transaction",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__ );
            return WitnessValidationResult::INVALID;
        }

        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        TransactionManagerLogger()->debug(
            "[{} - full: {}] {}: Start tx={} src={} nonce={} subject_nonce={} has_nonce={} "
            "has_utxo_params={} has_commitment={} has_witness={}",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            __func__,
            tx->GetHash(),
            tx->GetSrcAddress(),
            tx->GetNonce(),
            nonce_subject.has_value() ? nonce_subject.value().nonce() : 0,
            nonce_subject.has_value(),
            tx->HasUTXOParameters(),
            nonce_subject.has_value() && nonce_subject.value().has_utxo_commitment(),
            nonce_subject.has_value() && nonce_subject.value().has_utxo_witness() );

        if ( nonce_subject.has_error() )
        {
            TransactionManagerLogger()->debug( "[{} - full: {}] {}: Subject has no nonce payload, accepting tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return WitnessValidationResult::VALID;
        }

        const auto  chain_id  = GetValidationChainId( tx );
        const auto &validator = GetInputValidator( chain_id );

        if ( !tx->HasUTXOParameters() )
        {
            // BIND-01: Hardened early-return — if subject claims UTXO commitment
            // but tx lacks UTXO params, this is Pitfall 5 bypass → reject as INVALID
            if ( nonce_subject.has_value() && nonce_subject.value().has_utxo_commitment() )
            {
                TransactionManagerLogger()->error(
                    "[{} - full: {}] {}: Subject has UTXO commitment "
                    "but tx has no UTXO params — rejecting tx={}",
                    account_m->GetAddress().substr( 0, 8 ), full_node_m, __func__,
                    tx->GetHash() );
                return WitnessValidationResult::INVALID;
            }
            TransactionManagerLogger()->debug( "[{} - full: {}] {}: Tx has no UTXO params, accepting tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return WitnessValidationResult::VALID;
        }

        if ( !nonce_subject.value().has_utxo_commitment() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Missing UTXO commitment tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return WitnessValidationResult::INVALID;
        }

        const auto &commitment = nonce_subject.value().utxo_commitment();
        if ( commitment.consumed_outpoints_root().size() != base::Hash256::size() ||
             commitment.produced_outputs_root().size() != base::Hash256::size() )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Invalid commitment root sizes tx={} consumed_size={} "
                "produced_size={} expected={}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                __func__,
                tx->GetHash(),
                commitment.consumed_outpoints_root().size(),
                commitment.produced_outputs_root().size(),
                base::Hash256::size() );
            return WitnessValidationResult::INVALID;
        }
        auto consumed_root_result = base::Hash256::fromSpan(
            gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( commitment.consumed_outpoints_root().data() ) ),
                       commitment.consumed_outpoints_root().size() ) );
        if ( consumed_root_result.has_error() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Failed to parse commitment consumed root tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return WitnessValidationResult::INVALID;
        }

        if ( validator.RequiresConsensusUTXOData() && !nonce_subject.value().has_utxo_witness() )
        {
            TransactionManagerLogger()->error(
                "[{} - full: {}] {}: Missing required UTXO witness tx={} chain_id={} validator_requires_witness={}",
                account_m->GetAddress().substr( 0, 8 ),
                full_node_m,
                __func__,
                tx->GetHash(),
                chain_id,
                validator.RequiresConsensusUTXOData() );
            return WitnessValidationResult::INVALID;
        }

        auto params_opt = tx->GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            TransactionManagerLogger()->error( "[{} - full: {}] {}: Missing UTXO params payload tx={}",
                                               account_m->GetAddress().substr( 0, 8 ),
                                               full_node_m,
                                               __func__,
                                               tx->GetHash() );
            return WitnessValidationResult::INVALID;
        }
        (void)consumed_root_result;
        const bool witness_ok = validator.ValidateWitness( subject, tx, params_opt.value(), blockchain_ );
        TransactionManagerLogger()->debug( "[{} - full: {}] {}: Validator witness result tx={} chain_id={} result={}",
                                           account_m->GetAddress().substr( 0, 8 ),
                                           full_node_m,
                                           __func__,
                                           tx->GetHash(),
                                           chain_id,
                                           witness_ok );
        return witness_ok ? WitnessValidationResult::VALID : WitnessValidationResult::INVALID;
    }

    std::optional<UTXOTransitionCommitment> TransactionManager::BuildUTXOTransitionCommitment(
        const std::shared_ptr<IGeniusTransactions> &tx ) const
    {
        if ( !tx )
        {
            return std::nullopt;
        }
        if ( !tx->HasUTXOParameters() )
        {
            return std::nullopt;
        }
        auto params_opt = tx->GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            return std::nullopt;
        }
        const auto &inputs = params_opt->first;
        if ( inputs.empty() )
        {
            return std::nullopt;
        }
        auto tx_hash = base::Hash256::fromReadableString( tx->GetHash() );
        if ( tx_hash.has_error() )
        {
            return std::nullopt;
        }

        UTXOTransitionCommitment          commitment;
        std::vector<std::vector<uint8_t>> consumed_payloads;
        consumed_payloads.reserve( inputs.size() );
        for ( const auto &input : inputs )
        {
            auto *committed_input = commitment.add_consumed_outpoints();
            committed_input->set_tx_id_hash( input.txid_hash_.data(), input.txid_hash_.size() );
            committed_input->set_output_index( input.output_idx_ );

            std::vector<uint8_t> leaf_payload;
            leaf_payload.reserve( HASH256_BYTES + SERIALIZED_UINT32_BYTES );
            leaf_payload.insert( leaf_payload.end(), input.txid_hash_.begin(), input.txid_hash_.end() );
            utxo_merkle::AppendUInt32BE( leaf_payload, input.output_idx_ );
            consumed_payloads.push_back( std::move( leaf_payload ) );
        }
        const auto consumed_outpoints_root = utxo_merkle::ComputeMerkleRootFromPayloads(
            std::move( consumed_payloads ) );

        std::vector<GeniusUTXO> produced_outputs;
        if ( !ExtractProducedUTXOs( *tx, produced_outputs ) )
        {
            TransactionManagerLogger()->warn( "[{} - full: {}] {}: Could not extract produced outputs for tx={}",
                                              account_m->GetAddress().substr( 0, 8 ),
                                              full_node_m,
                                              __func__,
                                              tx->GetHash() );
            return std::nullopt;
        }
        std::vector<std::vector<uint8_t>> produced_payloads;
        produced_payloads.reserve( produced_outputs.size() );
        for ( size_t i = 0; i < produced_outputs.size(); ++i )
        {
            const auto &produced_output  = produced_outputs[i];
            auto       *committed_output = commitment.add_produced_outputs();
            committed_output->set_tx_id_hash( tx_hash.value().data(), tx_hash.value().size() );
            committed_output->set_output_index( static_cast<uint32_t>( i ) );
            committed_output->set_owner_address( produced_output.GetOwnerAddress() );
            const auto token_bytes = produced_output.GetTokenID().bytes();
            committed_output->set_token_id( token_bytes.data(), token_bytes.size() );
            committed_output->set_amount( produced_output.GetAmount() );

            produced_payloads.push_back( SerializeUTXOLeafPayload( produced_output ) );
        }
        const auto produced_outputs_root = account_m->GetUTXOManager().ComputeUTXOMerkleRootFromSnapshot(
            produced_outputs );
        const auto produced_outputs_root_from_payloads = utxo_merkle::ComputeMerkleRootFromPayloads(
            std::move( produced_payloads ) );
        if ( produced_outputs_root != produced_outputs_root_from_payloads )
        {
            return std::nullopt;
        }

        commitment.set_consumed_outpoints_root( consumed_outpoints_root.data(), consumed_outpoints_root.size() );
        commitment.set_produced_outputs_root( produced_outputs_root.data(), produced_outputs_root.size() );
        return commitment;
    }

    std::optional<UTXOWitness> TransactionManager::BuildUTXOWitness(
        const std::shared_ptr<IGeniusTransactions> &tx ) const
    {
        if ( !tx )
        {
            return std::nullopt;
        }

        if ( !tx->HasUTXOParameters() )
        {
            return std::nullopt;
        }

        auto params_opt = tx->GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            return std::nullopt;
        }
        const auto &inputs = params_opt->first;

        struct SnapshotLeaf
        {
            std::string          outpoint_key;
            std::vector<uint8_t> payload;
        };

        std::vector<SnapshotLeaf> leaves;
        auto utxos = account_m->GetUTXOManager().GetUTXOsForReservation( tx->GetSrcAddress(), tx->GetHash() );
        leaves.reserve( utxos.size() );
        for ( const auto &utxo : utxos )
        {
            leaves.push_back(
                { OutPointKey( utxo.GetTxID(), utxo.GetOutputIdx() ), SerializeUTXOLeafPayload( utxo ) } );
        }

        std::sort( leaves.begin(),
                   leaves.end(),
                   []( const SnapshotLeaf &a, const SnapshotLeaf &b ) { return a.payload < b.payload; } );

        std::unordered_map<std::string, size_t> outpoint_to_index;
        outpoint_to_index.reserve( leaves.size() );
        std::vector<base::Hash256> level_hashes;
        level_hashes.reserve( leaves.size() );
        for ( size_t i = 0; i < leaves.size(); ++i )
        {
            outpoint_to_index.emplace( leaves[i].outpoint_key, i );
            level_hashes.push_back( HashLeaf( leaves[i].payload ) );
        }

        UTXOWitness witness;
        for ( const auto &input : inputs )
        {
            const auto key = OutPointKey( input.txid_hash_, input.output_idx_ );
            auto       it  = outpoint_to_index.find( key );
            if ( it == outpoint_to_index.end() )
            {
                return std::nullopt;
            }

            const size_t leaf_index = it->second;
            auto        *proof      = witness.add_consumed_inputs();
            proof->set_tx_id_hash( input.txid_hash_.data(), input.txid_hash_.size() );
            proof->set_output_index( input.output_idx_ );
            proof->set_leaf_payload( leaves[leaf_index].payload.data(), leaves[leaf_index].payload.size() );

            size_t                     current_index = leaf_index;
            std::vector<base::Hash256> current_level = level_hashes;
            while ( current_level.size() > 1 )
            {
                if ( ( current_level.size() % 2 ) != 0 )
                {
                    current_level.push_back( current_level.back() );
                }

                const size_t sibling_index = current_index ^ 1U;
                auto        *step          = proof->add_branch();
                step->set_sibling_hash( current_level[sibling_index].data(), current_level[sibling_index].size() );
                step->set_is_left_sibling( sibling_index < current_index );

                std::vector<base::Hash256> next_level;
                next_level.reserve( current_level.size() / 2 );
                for ( size_t i = 0; i < current_level.size(); i += 2 )
                {
                    next_level.push_back( HashNode( current_level[i], current_level[i + 1] ) );
                }

                current_index = current_index / 2;
                current_level = std::move( next_level );
            }

            auto producer_tx = GetTransactionByHash( input.txid_hash_.toReadableString() );
            if ( !producer_tx )
            {
                return std::nullopt;
            }
            std::vector<GeniusUTXO> produced_outputs;
            if ( !ExtractProducedUTXOs( *producer_tx, produced_outputs ) )
            {
                return std::nullopt;
            }

            std::vector<SnapshotLeaf> produced_leaves;
            produced_leaves.reserve( produced_outputs.size() );
            for ( const auto &output_utxo : produced_outputs )
            {
                produced_leaves.push_back( { OutPointKey( output_utxo.GetTxID(), output_utxo.GetOutputIdx() ),
                                             SerializeUTXOLeafPayload( output_utxo ) } );
            }
            std::sort( produced_leaves.begin(),
                       produced_leaves.end(),
                       []( const SnapshotLeaf &a, const SnapshotLeaf &b ) { return a.payload < b.payload; } );

            std::unordered_map<std::string, size_t> produced_outpoint_to_index;
            produced_outpoint_to_index.reserve( produced_leaves.size() );
            std::vector<base::Hash256> produced_level_hashes;
            produced_level_hashes.reserve( produced_leaves.size() );
            for ( size_t i = 0; i < produced_leaves.size(); ++i )
            {
                produced_outpoint_to_index.emplace( produced_leaves[i].outpoint_key, i );
                produced_level_hashes.push_back( HashLeaf( produced_leaves[i].payload ) );
            }

            auto produced_it = produced_outpoint_to_index.find( key );
            if ( produced_it == produced_outpoint_to_index.end() )
            {
                return std::nullopt;
            }
            if ( produced_leaves[produced_it->second].payload != leaves[leaf_index].payload )
            {
                return std::nullopt;
            }

            size_t                     produced_index = produced_it->second;
            std::vector<base::Hash256> produced_level = produced_level_hashes;
            while ( produced_level.size() > 1 )
            {
                if ( ( produced_level.size() % 2 ) != 0 )
                {
                    produced_level.push_back( produced_level.back() );
                }

                const size_t sibling_index = produced_index ^ 1U;
                auto        *step          = proof->add_produced_branch();
                step->set_sibling_hash( produced_level[sibling_index].data(), produced_level[sibling_index].size() );
                step->set_is_left_sibling( sibling_index < produced_index );

                std::vector<base::Hash256> next_level;
                next_level.reserve( produced_level.size() / 2 );
                for ( size_t i = 0; i < produced_level.size(); i += 2 )
                {
                    next_level.push_back( HashNode( produced_level[i], produced_level[i + 1] ) );
                }

                produced_index = produced_index / 2;
                produced_level = std::move( next_level );
            }
        }

        return witness;
    }

    bool TransactionManager::ApplyTransactionToUTXOSnapshot( const std::shared_ptr<IGeniusTransactions> &tx,
                                                             std::vector<GeniusUTXO> &snapshot ) const
    {
        if ( !tx )
        {
            return false;
        }
        const auto remove_inputs = [&]( const std::vector<InputUTXOInfo> &inputs )
        {
            for ( const auto &input : inputs )
            {
                auto it = std::find_if(
                    snapshot.begin(),
                    snapshot.end(),
                    [&]( const GeniusUTXO &u )
                    { return u.GetTxID() == input.txid_hash_ && u.GetOutputIdx() == input.output_idx_; } );
                if ( it != snapshot.end() )
                {
                    snapshot.erase( it );
                }
            }
        };
        const auto tx_hash = base::Hash256::fromReadableString( tx->GetHash() );
        if ( tx_hash.has_error() )
        {
            return false;
        }

        if ( !tx->HasUTXOParameters() )
        {
            return false;
        }

        auto params_opt = tx->GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            return false;
        }
        const auto &[inputs, outputs] = params_opt.value();
        remove_inputs( inputs );
        for ( std::uint32_t i = 0; i < outputs.size(); ++i )
        {
            if ( outputs[i].dest_address == tx->GetSrcAddress() )
            {
                snapshot.emplace_back( tx_hash.value(),
                                       i,
                                       outputs[i].encrypted_amount,
                                       outputs[i].token_id,
                                       tx->GetSrcAddress() );
            }
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
                    BOOST_OUTCOME_TRY( RevertTransaction( tx ) );

                    BOOST_OUTCOME_TRY( DeleteTransaction( key, tx->GetTopics() ) );

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
                BOOST_OUTCOME_TRY( blockchain_->TryResumeProposal( tx->GetHash() ) );
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
                BOOST_OUTCOME_TRY( ParseTransaction( tx ) );
                account_m->SetPeerConfirmedNonce( tx->GetNonce(), tx->GetSrcAddress(), tx->GetHash() );
                {
                    std::lock_guard missing_lock( missing_tx_mutex_ );
                    missing_tx_hashes_.erase( tx->GetHash() );
                }
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
                    BOOST_OUTCOME_TRY( RevertTransaction( tx ) );

                    BOOST_OUTCOME_TRY( DeleteTransaction( key, tx->GetTopics() ) );

                    account_m->RollBackPeerConfirmedNonce( it->second.cached_nonce, tx->GetSrcAddress() );
                }
                else if ( tx->GetSrcAddress() == account_m->GetAddress() && tx->HasUTXOParameters() )
                {
                    // Local outgoing tx failed before confirmation: release locally reserved inputs.
                    auto params_opt = tx->GetUTXOParametersOpt();
                    if ( params_opt.has_value() )
                    {
                        account_m->GetUTXOManager().RollbackUTXOs( params_opt->first, tx->GetHash() );
                    }
                }
                tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::FAILED, tx->GetNonce() };
                account_m->ReleaseNonce( tx->GetNonce() );

                TransactionManagerLogger()->debug( "[{} - full: {}] {}: Set status of FAILED to transaction {}",
                                                   account_m->GetAddress().substr( 0, 8 ),
                                                   full_node_m,
                                                   __func__,
                                                   tx->GetHash() );
                {
                    std::lock_guard missing_lock( missing_tx_mutex_ );
                    missing_tx_hashes_.erase( tx->GetHash() );
                }
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
