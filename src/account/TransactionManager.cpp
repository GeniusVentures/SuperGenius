/**
 * @file       TransactionManager.cpp
 * @brief
 * @date       2024-04-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/TransactionManager.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <unordered_set>
#include <utility>
#include <thread>
#include <system_error>

#include <boost/asio/post.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <openssl/err.h>

#include <ProofSystem/EthereumKeyPairParams.hpp>
#include "TransferTransaction.hpp"
#include "MintTransaction.hpp"
#include "MintTransactionV2.hpp"
#include "MigrationTransaction.hpp"
#include "MigrationInputValidator.hpp"
#include "MigrationAllowList.hpp"
#include "EscrowTransaction.hpp"
#include "UTXOMerkle.hpp"
#include "account/BurnConfig.hpp"
#include "account/TokenAmount.hpp"
#include "account/AccountMessenger.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crdt/proto/delta.pb.h"
#include "base/hexutil.hpp"
#include "base/sgns_version.hpp"
#include "crypto/hasher.hpp"

#include "outcome/outcome.hpp"
#include "proof/ProcessingProof.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, TransactionManager::Error, e )
{
    switch ( e )
    {
        case sgns::TransactionManager::Error::TRUST_POLICY_NOT_READY:
            return "TRUST_POLICY_NOT_READY";
    }
    return "Unknown TransactionManager error";
}

namespace sgns
{
    namespace
    {
        using input_validator_constants::HASH256_BYTES;
        using input_validator_constants::SERIALIZED_UINT32_BYTES;
        using utxo_merkle::HashLeaf;
        using utxo_merkle::HashNode;
        using utxo_merkle::OutPointKey;
        using utxo_merkle::ReadUInt32BE;
        using utxo_merkle::ReadUInt64BE;
        using utxo_merkle::SerializeUTXOLeafPayload;

        bool ExtractProducedUTXOs( const GeniusTransaction &tx, std::vector<GeniusUTXO> &outputs )
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

        std::string TransferInputOwner( const TransferTransaction &transaction )
        {
            return utxo_address::IsEscrowLockAddress( transaction.GetUncleHash() ) ? transaction.GetUncleHash()
                                                                                   : transaction.GetSrcAddress();
        }

        base::Logger TransactionManagerLogger()
        {
            // Always call base::createLogger to get the current logger
            // This will return existing logger or create new one as needed
            return base::createLogger( "TransactionManager" );
        }

        std::string TransactionManagerLoggerName( const std::string &address, NodeType node_type )
        {
            return "TransactionManager:" + address.substr( 0, 8 ) +
                   ":role=" + std::string( NodeTypeToString( node_type ) );
        }

        base::Logger MakeTransactionManagerLogger( const std::string &address, NodeType node_type )
        {
            return TransactionManagerLogger()->clone( TransactionManagerLoggerName( address, node_type ) );
        }
    }

    // SIZE-01: Pre-publish size enforcement — reject transactions exceeding PubSub
    // message size limit before they enter the consensus pipeline. Matches the
    // handler-level MAX_EMBEDDED_TX_BYTES for defense-in-depth (per D-02).
    static constexpr size_t MAX_PUBSUB_TX_BYTES = 64 * 1024; // 65536 bytes

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

    std::shared_ptr<TransactionManager> TransactionManager::New(
        std::shared_ptr<crdt::GlobalDB>          processing_db,
        std::shared_ptr<boost::asio::io_context> ctx,
        std::shared_ptr<GeniusAccount>           account,
        std::shared_ptr<Blockchain>              blockchain,
        NodeType                                 node_type,
        uint16_t                                 subnet_id,
        std::chrono::milliseconds                timestamp_tolerance,
        std::chrono::milliseconds                mutability_window,
        uint64_t                                 initial_burn_basis_points,
        std::shared_ptr<const sgns::account::ConfirmedBurnValueProvider> confirmed_burn_provider )
    {
        auto instance = std::shared_ptr<TransactionManager>( new TransactionManager( std::move( processing_db ),
                                                                                     std::move( ctx ),
                                                                                     std::move( account ),
                                                                                     std::move( blockchain ),
                                                                                     node_type,
                                                                                     subnet_id,
                                                                                     timestamp_tolerance,
                                                                                     mutability_window,
                                                                                     initial_burn_basis_points,
                                                                                     std::move( confirmed_burn_provider ) ) );

        instance->blockchain_->RegisterCertificateHandler(
            NONCE_SUBJECT_TYPE,
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                const std::string          &subject_hash,
                const ConsensusCertificate &certificate ) -> outcome::result<ConsensusManager::Check>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    auto process_result = strong->OnConsensusCertificate( subject_hash, certificate );
                    if ( process_result.has_error() )
                    {
                        strong->m_logger->error( "Failed to process certificate proposal_id={} error={}",
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
                const ConsensusManager::Subject &subject ) -> outcome::result<ConsensusManager::ValidationResult>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->HandleNonceConsensusSubject( subject );
                }
                return outcome::failure( std::errc::owner_dead );
            } );
        instance->blockchain_->RegisterProposalCleanupHandler(
            NONCE_SUBJECT_TYPE,
            [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )]( const std::string &tx_hash )
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    strong->OnProposalTimeoutCleanup( tx_hash );
                }
            } );

        instance->blockchain_->RegisterSlotKeyHandler(
            NONCE_SUBJECT_TYPE,
            []( const ConsensusManager::Subject &subject ) -> std::string
            {
                auto nonce = ConsensusManager::DecodeNonceSubject( subject );
                if ( nonce.has_value() &&
                     nonce.value().transaction().transaction_case() != EmbeddedTransaction::TRANSACTION_NOT_SET )
                {
                    auto tx = TransactionManager::DeSerializeEmbeddedTransaction( nonce.value().transaction() );
                    if ( tx.has_value() )
                    {
                        return tx.value()->GetSlotID();
                    }
                }
                return subject.account_id() + ":" + std::to_string( nonce.has_value() ? nonce.value().nonce() : 0ULL );
            } );

        auto monitored_networks = GetMonitoredNetworkIDs();
        for ( auto network_id : monitored_networks )
        {
            std::string       blockchain_base = GetBlockChainBase( network_id );
            const std::string tx_pattern      = "^/?" + blockchain_base + "tx/[^/]+";
            const std::string proof_pattern   = "^/?" + blockchain_base + "proof/[^/]+";

            const bool tx_filter_registered = instance->globaldb_m->RegisterElementFilter(
                tx_pattern,
                [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                    const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
                {
                    if ( auto strong = weak_ptr.lock() )
                    {
                        return strong->FilterTransaction( element );
                    }
                    return std::nullopt;
                } );
            if ( !tx_filter_registered )
            {
                instance->m_logger->error( "Failed to register transaction element filter for pattern {}", tx_pattern );
            }

            const bool proof_filter_registered = instance->globaldb_m->RegisterElementFilter(
                proof_pattern,
                [weak_ptr( std::weak_ptr<TransactionManager>( instance ) )](
                    const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
                {
                    if ( auto strong = weak_ptr.lock() )
                    {
                        return strong->FilterProof( element );
                    }
                    return std::nullopt;
                } );
            if ( !proof_filter_registered )
            {
                instance->m_logger->error( "Failed to register proof element filter for pattern {}", proof_pattern );
            }

            instance->globaldb_m->RegisterNewElementCallback(
                tx_pattern,
                [weak_ptr( std::weak_ptr<TransactionManager>(
                    instance ) )]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
                {
                    if ( auto strong = weak_ptr.lock() )
                    {
                        strong->NewElementCallback( std::move( new_data ), cid );
                    }
                } );
            instance->globaldb_m->RegisterDeletedElementCallback(
                tx_pattern,
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
                                            std::shared_ptr<Blockchain>              blockchain,
                                            NodeType                                 node_type,
                                            uint16_t                                 subnet_id,
                                            std::chrono::milliseconds                timestamp_tolerance,
                                            std::chrono::milliseconds                mutability_window,
                                            uint64_t initial_burn_basis_points,
                                            std::shared_ptr<const sgns::account::ConfirmedBurnValueProvider>
                                                confirmed_burn_provider ) :
        globaldb_m( std::move( processing_db ) ),
        ctx_m( std::move( ctx ) ),
        account_m( std::move( account ) ),
        blockchain_( std::move( blockchain ) ),
        node_type_m( node_type ),
        subnet_id_( subnet_id ),
        state_m( State::CREATING ),
        last_periodic_sync_time_( std::chrono::steady_clock::now() ),
        timestamp_tolerance_m( timestamp_tolerance ),
        mutability_window_m( mutability_window ),
        burn_basis_points_( initial_burn_basis_points ),
        confirmed_burn_provider_( std::move( confirmed_burn_provider ) ),
        last_loop_time_( std::chrono::steady_clock::now() ),
        m_logger( MakeTransactionManagerLogger( account_m->GetAddress(), node_type_m ) )
    {
    }

    TransactionManager::~TransactionManager()
    {
        m_logger->debug( "~TransactionManager CALLED" );

        Stop();

        // METRICS-01: Flush all operational metrics counters on destruction (per D-14)
        m_logger->debug( "~TransactionManager: Metrics — cert_fallback(success={} failure={}) "
                         "validation(approve={} reject={}) tracking(insert={} confirm={} fail={})",
                         metrics_cert_fallback_success_.load(),
                         metrics_cert_fallback_failure_.load(),
                         metrics_validation_approve_.load(),
                         metrics_validation_reject_.load(),
                         metrics_tracking_insert_.load(),
                         metrics_tracking_confirm_.load(),
                         metrics_tracking_fail_.load() );
    }

    void TransactionManager::Stop()
    {
        if ( stopped_.exchange( true ) )
        {
            return; // idempotent — also the one-shot guard for the deregistration below
        }

        // Let an escrow transaction that already entered its short submission section
        // finish before account/CRDT dependencies are torn down. This must stay ahead
        // of deregistration so the in-flight submission still sees a wired manager.
        {
            std::lock_guard submission_lock( payout_submission_mutex_ );
        }

        // Detach from GlobalDB. Deregistering here rather than in the destructor makes
        // teardown deterministic instead of refcount-timed: GlobalDB filters are keyed
        // by pattern and registration REPLACES by pattern, so a manager destroyed late
        // (e.g. one kept alive by a queued handler across an account switch) would
        // otherwise tear down its successor's registrations. The stopped_ exchange
        // above guarantees this runs exactly once.
        //
        // The patterns are recomputed rather than stored: they derive only from
        // version::GetNetworkID(), which is set once in GeniusNode's constructor
        // before any TransactionManager exists, so this reproduces exactly what
        // New() registered.
        if ( globaldb_m )
        {
            for ( auto network_id : GetMonitoredNetworkIDs() )
            {
                const std::string blockchain_base = GetBlockChainBase( network_id );
                const std::string tx_pattern      = "^/?" + blockchain_base + "tx/[^/]+";
                const std::string proof_pattern   = "^/?" + blockchain_base + "proof/[^/]+";

                globaldb_m->UnregisterNewElementCallback( tx_pattern );
                globaldb_m->UnregisterDeletedElementCallback( tx_pattern );
                globaldb_m->UnregisterElementFilter( tx_pattern );
                globaldb_m->UnregisterElementFilter( proof_pattern );
            }
        }

        // Detach from consensus. All four are keyed on NONCE_SUBJECT_TYPE.
        if ( blockchain_ )
        {
            blockchain_->UnregisterCertificateHandler( NONCE_SUBJECT_TYPE );
            blockchain_->UnregisterSubjectHandler( NONCE_SUBJECT_TYPE );
            blockchain_->UnregisterProposalCleanupHandler( NONCE_SUBJECT_TYPE );
            blockchain_->UnregisterSlotKeyHandler( NONCE_SUBJECT_TYPE );
        }

        // Detach from the account while it is still guaranteed alive: GeniusNode calls
        // Stop() before DeconfigureDatabaseDependencies() and before releasing account_.
        if ( account_m )
        {
            account_m->ClearGetTransactionCIDMethod();
        }

        CancelPendingTransactionWaits();
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
        if ( ReplicatesAllAccounts( node_type_m ) )
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

        globaldb_m->AddListenTopic( account_m->GetAddress() );
        m_logger->info( "Adding broadcast to full node on {}", full_node_topic_m );
        if ( ReplicatesAllAccounts( node_type_m ) )
        {
            m_logger->debug( "Listening full node on {}", full_node_topic_m );
            globaldb_m->AddListenTopic( full_node_topic_m );
        }
    }

    void TransactionManager::StartCore()
    {
        if ( GetState() != State::CREATING || stopped_.load() || core_started_.exchange( true ) )
        {
            return;
        }

        m_logger->info( "Starting Transaction Manager" );

        ChangeState( State::INITIALIZING );

        if ( stopped_.load() )
        {
            return;
        }

        InitializeUTXOs();

        // First kick. Capture weakly, like the recurring post in TickOnce(): a strong
        // self-capture lets the manager outlive GeniusNode's reset during an account
        // switch, which tears down the account and Blockchain this handler dereferences.
        boost::asio::post( *ctx_m,
                           [weak_instance = weak_from_this()]
                           {
                               if ( auto instance = weak_instance.lock(); instance && !instance->stopped_.load() )
                               {
                                   instance->TickOnce();
                               }
                           } );
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
        last_loop_time_           = now;

        std::vector<std::string>                            elements_to_delete;
        std::vector<crdt::CRDTCallbackManager::NewDataPair> elements_to_process;
        elements_to_delete.reserve( deleted_data_queue_.size() );
        elements_to_process.reserve( new_data_queue_.size() );
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
            m_logger->debug( "Deleting key: {} ", deletion_key );
            ProcessDeletion( deletion_key );
        }
        for ( auto &new_data : elements_to_process )
        {
            m_logger->debug( "Adding key: {} ", new_data.first );
            ProcessNewData( new_data );
        }

        m_logger->trace( "Loop iteration - time since last: {}ms", time_since_last_loop );

        switch ( GetState() )
        {
            case State::INITIALIZING:
                InitTransactions();
                if ( GetState() == State::READY )
                {
                    m_logger->debug( "Transaction Manager is now READY - starting regular updates" );
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
                        m_logger->info( "Send deferred/retryable ({}). Keeping transaction in queue", err.message() );
                        break;
                    }

                    ChangeState( State::SYNCING );

                    m_logger->error( "Error in SendTransactionItem: {}", err.message() );

                    auto rollback_result = RollbackTransactions( tx_queue_m.front() );
                    if ( rollback_result.has_error() )
                    {
                        m_logger->error( "{} error, couldn't fetch nonce", __func__ );
                        break;
                    }
                    tx_queue_m.pop_front();
                    break;
                }
                tx_queue_m.pop_front();
            }
            break;
        }

        auto time_since_last_sync = std::chrono::duration_cast<std::chrono::seconds>( now - last_periodic_sync_time_ );
        bool should_sync          = received_first_periodic_sync_response_.load()
                                        ? time_since_last_sync >= PERIODIC_SYNC_INTERVAL
                                        : time_since_last_sync >= INITIAL_PERIODIC_SYNC_INTERVAL;

        if ( should_sync )
        {
            auto interval_desc = received_first_periodic_sync_response_.load() ? "10 minutes" : "30 seconds";
            m_logger->debug( "Periodic sync - requesting heads (interval: {})", interval_desc );
            auto topics_result = globaldb_m->GetMonitoredTopics();
            if ( topics_result.has_value() )
            {
                if ( account_m->RequestHeads( topics_result.value() ) )
                {
                    last_periodic_sync_time_ = now;
                    m_logger->debug( "Periodic sync head request sent for {} topics", topics_result.value().size() );
                }
                else
                {
                    m_logger->warn( "Periodic sync head request failed" );
                }
            }
            else
            {
                m_logger->warn( "Could not get monitored topics for head request" );
            }
        }

        std::unique_lock lock( cv_mutex_ );
        cv_.wait_for( lock,
                      std::chrono::milliseconds( 300 ),
                      [this] { return stopped_.load() || !new_data_queue_.empty() || !deleted_data_queue_.empty(); } );
        lock.unlock();

        // Schedule next tick if not stopped
        if ( stopped_.load() )
        {
            return;
        }

        boost::asio::post( *ctx_m,
                           [weak_instance = weak_from_this()]
                           {
                               if ( auto instance = weak_instance.lock(); instance && !instance->stopped_.load() )
                               {
                                   instance->TickOnce();
                               }
                           } );
    }

    outcome::result<std::string> TransactionManager::TransferFunds( uint64_t    amount,
                                                                    std::string destination,
                                                                    TokenID     token_id )
    {
        // stopped_ is checked separately from the state: Stop() detaches from GlobalDB,
        // Blockchain and the account without moving state_m out of READY.
        if ( stopped_.load() || GetState() != State::READY )
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
        if ( stopped_.load() || GetState() != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        if ( chainid.empty() )
        {
            // Canonicalize default MintV2 source-chain metadata for newly created public-chain mints.
            chainid = "public";
        }

        // Strip "0x" hex prefix if present — Hash256::fromReadableString expects raw hex.
        if ( transaction_hash.size() >= 2 && transaction_hash[0] == '0' && transaction_hash[1] == 'x' )
        {
            transaction_hash = transaction_hash.substr( 2 );
        }

        // UTXO reservation check — prevent duplicate mint creation for the same burn
        // Uses UTXO_RESERVED state (D-18) instead of in-memory bridge_mint_reservations_
        base::Hash256 burn_tx_hash;
        if ( auto parsed = base::Hash256::fromReadableString( transaction_hash ); parsed.has_value() )
        {
            burn_tx_hash   = parsed.value();
            auto &utxo_mgr = account_m->GetUTXOManager();
            if ( utxo_mgr.IsOutPointReserved( burn_tx_hash, 0 ) || utxo_mgr.IsOutPointConsumed( burn_tx_hash, 0 ) )
            {
                m_logger->warn( "{}: Bridge mint already processed (UTXO) for chain={} tx_hash={}",
                                __func__,
                                chainid,
                                transaction_hash );
                return outcome::failure( std::errc::already_connected );
            }
        }

        // Persistence check — reject if this burn was already executed (survives restart)
        const std::string persistence_key = chainid + std::string( kBridgeKeySeparator ) + transaction_hash;
        {
            auto datastore = globaldb_m ? globaldb_m->GetDataStore() : nullptr;
            if ( datastore )
            {
                crdt::GlobalDB::Buffer key_buffer;
                key_buffer.put( std::string( kBridgeExecutedPrefix ) + persistence_key );
                auto existing = datastore->get( key_buffer );
                if ( existing.has_value() )
                {
                    m_logger->warn( "{}: Bridge mint already executed (persisted) for chain={} tx_hash={}",
                                    __func__,
                                    chainid,
                                    transaction_hash );
                    return outcome::failure( std::errc::already_connected );
                }
            }
        }

        auto          source_hash = base::Hash256::fromReadableString( transaction_hash );
        base::Hash256 source_input_hash;
        if ( source_hash.has_error() )
        {
            m_logger->warn(
                "{}: Source hash parse inconsistency for mint tx_ref={}, using empty input hash and uncle_hash fallback",
                __func__,
                transaction_hash );
        }
        else
        {
            source_input_hash = source_hash.value();
        }

        // D-18/D-19: Insert burn UTXO, then reserve via ReserveUTXOs (sets RESERVED state)
        if ( !source_hash.has_error() )
        {
            GeniusUTXO burn_utxo( source_hash.value(), 0, amount, tokenid, account_m->GetAddress() );
            account_m->GetUTXOManager().PutUTXO( burn_utxo,
                                                 account_m->GetAddress(),
                                                 sgns::UTXOManager::UTXOType::UTXO_BRIDGE );
        }

        std::vector<GeniusUTXO> source_utxos;
        source_utxos.emplace_back( source_input_hash, 0, amount, tokenid, account_m->GetAddress() );
        auto mint_inputs = account_m->CreateInputsFromUTXOs( source_utxos );

        // Reserve the burn UTXO — transitions READY → RESERVED (D-18)
        account_m->GetUTXOManager().ReserveUTXOs( mint_inputs,
                                                  transaction_hash,
                                                  sgns::UTXOManager::UTXOType::UTXO_BRIDGE );

        // Capture input info for potential rollback (mint_inputs may be moved below)
        auto rollback_inputs = mint_inputs;

        auto txId = std::string{};
        try
        {
            auto mint_transaction = std::make_shared<MintTransactionV2>(
                MintTransactionV2::New( amount,
                                        std::move( chainid ),
                                        tokenid,
                                        FillDAGStruct( std::move( transaction_hash ) ),
                                        std::move( mint_inputs ),
                                        destination ) );

            mint_transaction->MakeSignature( *account_m );
            txId = mint_transaction->GetHash();
            EnqueueTransaction( std::make_pair( std::move( mint_transaction ), std::nullopt ) );
        }
        catch ( const std::exception &e )
        {
            account_m->GetUTXOManager().RollbackUTXOs( rollback_inputs,
                                                       transaction_hash,
                                                       sgns::UTXOManager::UTXOType::UTXO_BRIDGE );
            m_logger->error( "{}: MintFunds failed — rolled back reservation for tx_hash={}: {}",
                             __func__,
                             transaction_hash,
                             e.what() );
            return outcome::failure( std::errc::operation_canceled );
        }

        return txId;
    }

    outcome::result<std::string> TransactionManager::MigrationFunds( uint64_t    amount,
                                                                     std::string from_version,
                                                                     TokenID     tokenid,
                                                                     std::string destination )
    {
        if ( stopped_.load() || GetState() != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto migration_transaction = std::make_shared<MigrationTransaction>(
            MigrationTransaction::New( amount, std::move( from_version ), tokenid, FillDAGStruct(), destination ) );

        migration_transaction->MakeSignature( *account_m );

        auto txId = migration_transaction->GetHash();

        EnqueueTransaction( std::make_pair( std::move( migration_transaction ), std::nullopt ) );

        return txId;
    }

    outcome::result<std::pair<std::string, EscrowDataPair>> TransactionManager::HoldEscrow( uint64_t           amount,
                                                                                            const std::string &job_id )
    {
        if ( stopped_.load() || GetState() != State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto              hash_data = crypto::blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );
        const std::string lock_id   = "0x" + hash_data.toReadableString();

        BOOST_OUTCOME_TRY(
            auto params,
            account_m->GetUTXOManager().CreateTxParameter( amount, lock_id, TokenID::FromBytes( { 0x00 } ) ) );
        auto [inputs, outputs]  = params;
        auto escrow_transaction = std::make_shared<EscrowTransaction>(
            EscrowTransaction::New( params, amount, FillDAGStruct( lock_id ) ) );

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

    outcome::result<std::vector<OutputDestInfo>> TransactionManager::BuildPayoutOutputs(
        const SGProcessing::TaskResult &task_result,
        uint64_t                        escrow_amount,
        const TokenID                  &escrow_token_id,
        uint64_t                        burn_basis_points )
    {
        using boost::multiprecision::uint128_t;

        // Static: this function is static but still logs; createLogger returns the process-wide
        // "TransactionManager" logger.
        static const base::Logger logger = base::createLogger( "TransactionManager" );

        if ( burn_basis_points > BASIS_POINTS_TOTAL )
        {
            return std::errc::invalid_argument;
        }

        const auto burn      = ( static_cast<uint128_t>( escrow_amount ) * burn_basis_points ) / BASIS_POINTS_TOTAL;
        const auto available = static_cast<uint128_t>( escrow_amount ) - burn;

        // One malformed entry must never block the payout: honest peers get paid, the bad entry
        // gets nothing.
        std::unordered_set<std::string>                   seen_subtask_ids;
        std::vector<const SGProcessing::SubTaskResult *> valid_results;
        for ( const auto &result : task_result.subtask_results() )
        {
            const bool valid = !result.subtaskid().empty() && !result.developer_address().empty() &&
                               base::IsHexAddress( result.node_address() ) &&
                               result.token_id().size() == std::tuple_size_v<TokenID::ByteArray> &&
                               result.developer_cut() <= DEVELOPER_CUT_SCALE &&
                               seen_subtask_ids.insert( result.subtaskid() ).second;
            if ( valid )
            {
                valid_results.push_back( &result );
            }
            else
            {
                logger->warn( "Ignoring invalid subtask result in escrow payout: subtaskid=\"{}\" peer=\"{}\" "
                              "developer=\"{}\" cut={}",
                              result.subtaskid(),
                              result.node_address(),
                              result.developer_address(),
                              result.developer_cut() );
            }
        }

        if ( valid_results.empty() )
        {
            logger->error( "No valid subtask results in escrow payout" );
            return std::errc::invalid_argument;
        }

        // Even split of what is left after the burn; the split remainder is burned too, so every
        // minion is accounted for without an apportionment pass. Each result's developer cut is
        // floored and the floor residue stays with that result's peer, so a result's peer and
        // developer outputs always sum to its per-result share.
        const auto per_result = available / valid_results.size();
        const auto dust       = available % valid_results.size();

        std::vector<OutputDestInfo> outputs;
        outputs.reserve( valid_results.size() * 2 + 1 );
        // Developer credits from several results collapse into one output per (address, token).
        std::map<std::pair<std::string, std::string>, uint64_t> developer_amounts;
        for ( const auto *result : valid_results )
        {
            const auto dev_amount = static_cast<uint64_t>(
                static_cast<uint128_t>( per_result ) * result->developer_cut() / DEVELOPER_CUT_SCALE );
            developer_amounts[{ result->developer_address(), result->token_id() }] += dev_amount;

            const auto peer_amount = static_cast<uint64_t>( per_result ) - dev_amount;
            if ( peer_amount != 0 )
            {
                outputs.push_back( { peer_amount,
                                     result->node_address(),
                                     TokenID::FromBytes( result->token_id().data(), result->token_id().size() ) } );
            }
        }
        for ( const auto &[key, amount] : developer_amounts )
        {
            if ( amount != 0 )
            {
                const auto &[address, token_bytes] = key;
                outputs.push_back( { amount, address, TokenID::FromBytes( token_bytes.data(), token_bytes.size() ) } );
            }
        }
        // Always emitted, even at zero, so the release has a fixed shape for observers.
        outputs.push_back( { static_cast<uint64_t>( burn + dust ), std::string( BURN_ADDRESS ), escrow_token_id } );

        const auto total = std::accumulate( outputs.cbegin(),
                                            outputs.cend(),
                                            uint128_t{ 0 },
                                            []( const uint128_t sum, const OutputDestInfo &output )
                                            { return sum + output.encrypted_amount; } );
        if ( total != escrow_amount )
        {
            return std::errc::result_out_of_range;
        }
        return outputs;
    }

    outcome::result<std::string> TransactionManager::PayEscrow(
        const std::string                       &escrow_path,
        const SGProcessing::TaskResult          &task_result,
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction )
    {
        // Dereferences globaldb_m and account_m below; Stop() has already detached from both.
        if ( stopped_.load() )
        {
            return std::errc::operation_canceled;
        }

        uint64_t burn_basis_points = burn_basis_points_.load( std::memory_order_relaxed );
        if ( confirmed_burn_provider_ )
        {
            if ( !confirmed_burn_provider_->IsReady() )
            {
                return outcome::failure( Error::TRUST_POLICY_NOT_READY );
            }
            burn_basis_points = confirmed_burn_provider_->GetBasisPoints();
        }
        if ( burn_basis_points > BASIS_POINTS_TOTAL )
        {
            m_logger->error( "Burn basis points {} exceed maximum {}", burn_basis_points, BASIS_POINTS_TOTAL );
            return std::errc::invalid_argument;
        }

        const auto &subtask_results = task_result.subtask_results();
        if ( subtask_results.empty() )
        {
            m_logger->error( "No result found on escrow {}", escrow_path );
            return std::errc::invalid_argument;
        }
        if ( escrow_path.empty() )
        {
            m_logger->error( "Escrow path empty" );
            return std::errc::invalid_argument;
        }
        m_logger->debug( "Fetching escrow from processing DB at {}", escrow_path );
        BOOST_OUTCOME_TRY( auto transaction, FetchTransaction( *globaldb_m, escrow_path ) );

        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( transaction );
        if ( !escrow_tx )
        {
            m_logger->error( "Transaction at escrow path {} is not an escrow transaction", escrow_path );
            return std::errc::invalid_argument;
        }

        const auto escrow_params = escrow_tx->GetUTXOParameters();
        if ( escrow_params.second.empty() )
        {
            m_logger->error( "Escrow transaction {} has no payout output", escrow_tx->GetHash() );
            return std::errc::invalid_argument;
        }

        if ( crdt_transaction && !escrow_tx->GetSrcAddress().empty() )
        {
            BOOST_OUTCOME_TRY( crdt_transaction->AddTopic( escrow_tx->GetSrcAddress() ) );
        }

        BOOST_OUTCOME_TRY( auto payout_peers,
                           BuildPayoutOutputs( task_result,
                                               escrow_tx->GetAmount(),
                                               escrow_params.second.front().token_id,
                                               burn_basis_points ) );

        InputUTXOInfo escrow_utxo_input;
        escrow_utxo_input.txid_hash_  = base::Hash256::fromReadableString( escrow_tx->GetHash() ).value();
        escrow_utxo_input.output_idx_ = 0;
        escrow_utxo_input.signature_  = account_m->Sign( escrow_utxo_input.SerializeForSigning() );

        std::string lock_id = escrow_tx->GetUncleHash();
        if ( lock_id.empty() )
        {
            lock_id = escrow_params.second.front().dest_address;
            m_logger->warn(
                "Escrow transaction {} has empty lock_id but has UTXO parameters - using dest_address as fallback lock_id: {}",
                escrow_tx->GetHash(),
                lock_id );
        }

        auto transfer_transaction = std::make_shared<TransferTransaction>(
            TransferTransaction::New( std::vector{ escrow_utxo_input }, payout_peers, FillDAGStruct( lock_id ) ) );

        transfer_transaction->MakeSignature( *account_m );

        EnqueueTransaction( TransactionItem{ TransactionBatch{ { transfer_transaction, std::nullopt } },
                                             std::move( crdt_transaction ) } );
        return transfer_transaction->GetHash();
    }

    void TransactionManager::AsyncPayEscrow( std::string                              escrow_path,
                                             SGProcessing::TaskResult                 task_result,
                                             std::shared_ptr<crdt::AtomicTransaction> crdt_transaction,
                                             std::chrono::milliseconds                timeout,
                                             TransactionCompletionCallback            callback )
    {
        if ( !callback )
        {
            return;
        }

        const auto       started_at = std::chrono::steady_clock::now();
        std::unique_lock submission_lock( payout_submission_mutex_ );
        if ( stopped_.load() )
        {
            submission_lock.unlock();
            callback(
                TransactionCompletion{ {}, TransactionStatus::INVALID, {}, boost::asio::error::operation_aborted } );
            return;
        }

        auto payout = PayEscrow( escrow_path, task_result, std::move( crdt_transaction ) );
        submission_lock.unlock();
        if ( payout.has_error() )
        {
            callback( TransactionCompletion{
                {},
                TransactionStatus::INVALID,
                std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - started_at ),
                payout.error() } );
            return;
        }

        AsyncWaitForTransactionOutgoing( std::move( payout.value() ), timeout, std::move( callback ) );
    }

    void TransactionManager::AsyncWaitForTransactionOutgoing( std::string                   tx_id,
                                                              std::chrono::milliseconds     timeout,
                                                              TransactionCompletionCallback callback )
    {
        if ( !callback )
        {
            return;
        }

        auto wait = std::make_shared<PendingTransactionWait>( *ctx_m,
                                                              std::move( tx_id ),
                                                              std::move( callback ),
                                                              std::chrono::steady_clock::now() );
        wait->timer.expires_after( timeout );

        {
            std::lock_guard lock( transaction_waits_mutex_ );
            if ( stopped_.load() )
            {
                wait->completed.store( true );
            }
            else
            {
                transaction_waits_[wait->tx_id].push_back( wait );
            }
        }

        if ( wait->completed.load() )
        {
            auto completion_callback = std::move( wait->callback );
            completion_callback( TransactionCompletion{ wait->tx_id,
                                                        TransactionStatus::INVALID,
                                                        {},
                                                        boost::asio::error::operation_aborted } );
            return;
        }

        wait->timer.async_wait(
            [weak_self = weak_from_this(),
             weak_wait = std::weak_ptr<PendingTransactionWait>( wait )]( const boost::system::error_code &error )
            {
                if ( error == boost::asio::error::operation_aborted )
                {
                    return;
                }
                if ( auto manager = weak_self.lock() )
                {
                    if ( auto pending_wait = weak_wait.lock() )
                    {
                        manager->CompleteTransactionWait(
                            pending_wait,
                            manager->GetOutgoingStatusByTxId( pending_wait->tx_id ),
                            boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
                    }
                }
            } );

        const auto status = GetOutgoingStatusByTxId( wait->tx_id );
        if ( IsTerminalTransactionStatus( status ) )
        {
            CompleteTransactionWait( wait, status );
        }
    }

    bool TransactionManager::IsTerminalTransactionStatus( TransactionStatus status )
    {
        return status == TransactionStatus::CONFIRMED || status == TransactionStatus::UNCONFIRMED ||
               status == TransactionStatus::FAILED;
    }

    void TransactionManager::NotifyTransactionStatusChanged( const std::string &tx_id )
    {
        std::vector<std::shared_ptr<PendingTransactionWait>> waits;
        {
            std::lock_guard lock( transaction_waits_mutex_ );
            if ( auto it = transaction_waits_.find( tx_id ); it != transaction_waits_.end() )
            {
                waits = it->second;
            }
        }
        if ( waits.empty() )
        {
            return;
        }

        const auto status = GetOutgoingStatusByTxId( tx_id );
        if ( !IsTerminalTransactionStatus( status ) )
        {
            return;
        }

        for ( const auto &wait : waits )
        {
            CompleteTransactionWait( wait, status );
        }
    }

    void TransactionManager::CompleteTransactionWait( const std::shared_ptr<PendingTransactionWait> &wait,
                                                      TransactionStatus                              status,
                                                      boost::system::error_code                      error )
    {
        if ( wait->completed.exchange( true ) )
        {
            return;
        }

        boost::system::error_code ignored;
        wait->timer.cancel( ignored );
        {
            std::lock_guard lock( transaction_waits_mutex_ );
            auto            it = transaction_waits_.find( wait->tx_id );
            if ( it != transaction_waits_.end() )
            {
                auto &waits = it->second;
                waits.erase( std::remove( waits.begin(), waits.end(), wait ), waits.end() );
                if ( waits.empty() )
                {
                    transaction_waits_.erase( it );
                }
            }
        }

        auto callback = std::move( wait->callback );
        if ( !callback )
        {
            return;
        }

        TransactionCompletion completion{ wait->tx_id,
                                          status,
                                          std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - wait->started_at ),
                                          error };
        boost::asio::post( *ctx_m,
                           [callback = std::move( callback ), completion = std::move( completion )]() mutable
                           { callback( std::move( completion ) ); } );
    }

    void TransactionManager::CancelPendingTransactionWaits()
    {
        std::unordered_map<std::string, std::vector<std::shared_ptr<PendingTransactionWait>>> waits;
        {
            std::lock_guard lock( transaction_waits_mutex_ );
            waits.swap( transaction_waits_ );
        }

        for ( auto &[_, transaction_waits] : waits )
        {
            for ( auto &wait : transaction_waits )
            {
                if ( wait->completed.exchange( true ) )
                {
                    continue;
                }

                boost::system::error_code ignored;
                wait->timer.cancel( ignored );
                auto callback = std::move( wait->callback );
                if ( callback )
                {
                    callback( TransactionCompletion{ wait->tx_id,
                                                     TransactionStatus::INVALID,
                                                     std::chrono::duration_cast<std::chrono::milliseconds>(
                                                         std::chrono::steady_clock::now() - wait->started_at ),
                                                     boost::asio::error::operation_aborted } );
                }
            }
        }
    }

    void TransactionManager::EnqueueTransaction( TransactionItem element )
    {
        m_logger->debug( "Transaction enqueuing" );
        if ( element.first.empty() )
        {
            m_logger->error( "Ignoring empty transaction batch" );
            return;
        }

        for ( auto &&[tx, _] : element.first )
        {
            auto result = ChangeTransactionState( tx, TransactionStatus::CREATED );
            if ( !result )
            {
                m_logger->error( "Failed to change transaction state for {}", tx->GetHash() );
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
        auto                     timestamp     = std::chrono::system_clock::now();
        const auto               previous_hash = GetOutgoingPreviousHash( nonce );

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

        m_logger->debug( "Recovered previous hash {} for nonce {} from persisted head", persisted_hash, nonce );
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

                const auto candidate_hash = candidate->GetHash();
                if ( selected_hash.empty() ||
                     Blockchain::BestHash( selected_hash, candidate->GetHash() ) == candidate->GetHash() )
                {
                    selected_hash = candidate_hash;
                }
            }
        }

        if ( !selected_hash.empty() )
        {
            m_logger->debug( "Recovered previous hash {} for nonce {} from persisted transactions",
                             selected_hash,
                             nonce );
            return selected_hash;
        }
        return "";
    }

    TransactionManager::InputValidatorSelection TransactionManager::SelectInputValidator(
        const std::shared_ptr<GeniusTransaction> &tx ) const
    {
        std::string chain_id( GENIUS_CHAIN_ID );

        if ( tx )
        {
            if ( auto tx_chain_id = tx->GetChainId(); !tx_chain_id.empty() )
            {
                chain_id = std::move( tx_chain_id );
            }
            else if ( tx->GetType() == "mint-v2" )
            {
                chain_id = "public";
            }
        }

        if ( const auto *registered_validator = IInputValidator::Get( chain_id ) )
        {
            return { std::move( chain_id ), *registered_validator };
        }

        if ( chain_id == GENIUS_CHAIN_ID || chain_id == GeniusTransaction::GENIUS_CHAIN_ID )
        {
            return { std::move( chain_id ), genius_input_validator_ };
        }

        return { std::move( chain_id ), public_chain_input_validator_ };
    }

    outcome::result<void> TransactionManager::SendTransactionItem( TransactionItem &item )
    {
        auto &[transaction_batch, maybe_crdt_transaction] = item;

        m_logger->trace( "{} called", __func__ );

        auto crdt_transaction = maybe_crdt_transaction.value_or( nullptr );
        if ( !crdt_transaction )
        {
            crdt_transaction = globaldb_m->BeginTransaction();
        }

        if ( transaction_batch.empty() )
        {
            return outcome::success();
        }

        uint64_t expected_next_nonce;
        if ( auto local_confirmed = account_m->GetLocalConfirmedNonce(); local_confirmed.has_value() )
        {
            expected_next_nonce = local_confirmed.value() + 1;
            m_logger->debug( "Using local confirmed nonce {} as send baseline", local_confirmed.value() );
        }
        else
        {
            // If confirmed nonce is not available yet, preserve local enqueue order.
            expected_next_nonce = transaction_batch.front().first->GetNonce();
            m_logger->debug( "Local confirmed nonce unavailable, using first queued nonce {} as send baseline",
                             expected_next_nonce );
        }
        std::unordered_set<std::string> topicSet{ full_node_topic_m, account_m->GetAddress() };

        std::vector<std::shared_ptr<GeniusTransaction>> transactions_sent;
        transactions_sent.reserve( transaction_batch.size() );
        for ( auto &[transaction, maybe_proof] : transaction_batch )
        {
            if ( transaction->GetNonce() != expected_next_nonce )
            {
                if ( transaction->GetNonce() > expected_next_nonce )
                {
                    m_logger->debug( "Deferring transaction send due to nonce gap - Expected: {}, Tried to send: {}",
                                     expected_next_nonce,
                                     transaction->GetNonce() );
                    return outcome::failure(
                        boost::system::errc::make_error_code( boost::system::errc::resource_unavailable_try_again ) );
                }

                m_logger->error( "Transaction with unexpected nonce - Expected: {}, Tried to send: {}",
                                 expected_next_nonce,
                                 transaction->GetNonce() );
                return outcome::failure(
                    boost::system::errc::make_error_code( boost::system::errc::invalid_argument ) );
            }

            auto                   transaction_path = GetTransactionPath( *transaction );
            crdt::HierarchicalKey  tx_key( transaction_path );
            crdt::GlobalDB::Buffer data_transaction;

            m_logger->debug( "Recording the transaction on {}", tx_key.GetKey() );

            data_transaction.put( transaction->SerializeByteVector() );
            BOOST_OUTCOME_TRY( crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

            if ( maybe_proof )
            {
                crdt::HierarchicalKey  proof_key( GetTransactionProofPath( *transaction ) );
                crdt::GlobalDB::Buffer proof_transaction;

                auto &proof = maybe_proof.value();
                m_logger->debug( "Recording the proof on {}", proof_key.GetKey() );

                proof_transaction.put( proof );
                BOOST_OUTCOME_TRY( crdt_transaction->Put( std::move( proof_key ), std::move( proof_transaction ) ) );
            }
            m_logger->debug( "Creating Consensus Proposal for tx {}", transaction_path );

            topicSet.merge( transaction->GetTopics() );
            transactions_sent.push_back( transaction );

            expected_next_nonce++;
        }

        BOOST_OUTCOME_TRY( crdt_transaction->Commit( topicSet ) );

        for ( auto &transaction : transactions_sent )
        {
            const auto &[_, validator]    = SelectInputValidator( transaction );
            const bool utxo_data_required = validator.RequiresConsensusUTXOData();

            std::optional<UTXOTransitionCommitment> utxo_commitment;
            std::optional<UTXOWitness>              utxo_witness;

            if ( transaction->HasUTXOParameters() )
            {
                utxo_commitment = BuildUTXOTransitionCommitment( transaction );
                if ( !utxo_commitment.has_value() )
                {
                    m_logger->error( "{}: Missing required UTXO commitment for tx={} type={}",
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
                        m_logger->error( "{}: Missing required UTXO witness for tx={} type={}",
                                         __func__,
                                         transaction->GetHash(),
                                         transaction->GetType() );
                        return outcome::failure( std::errc::invalid_argument );
                    }
                }
            }

            // SIZE-01: Pre-publish size enforcement gate
            // Reject oversized transactions (>64KB) before they enter the consensus
            // pipeline to prevent silent PubSub message drops. Defense-in-depth with
            // the handler-level MAX_EMBEDDED_TX_BYTES check (per D-02).
            // Serialize tx into EmbeddedTransaction proto with typed oneof field
            auto embedded_tx = transaction->SerializeToEmbeddedTransaction();
            if ( embedded_tx.ByteSizeLong() > MAX_PUBSUB_TX_BYTES )
            {
                m_logger->error( "{}: Transaction exceeds PubSub size limit tx={} size={} max={}",
                                 __func__,
                                 transaction->GetHash(),
                                 embedded_tx.ByteSizeLong(),
                                 MAX_PUBSUB_TX_BYTES );
                return outcome::failure( std::errc::message_size );
            }

            BOOST_OUTCOME_TRY( auto &&proposal,
                               blockchain_->CreateConsensusProposal( transaction->GetSrcAddress(),
                                                                     transaction->GetNonce(),
                                                                     transaction->GetHash(),
                                                                     embedded_tx,
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
        return GetBlockChainBase( base ) + GeniusTransaction::GetTransactionFullPath( tx_hash );
    }

    std::string TransactionManager::GetTransactionPath( const GeniusTransaction &element )
    {
        return GetBlockChainBase() + element.GetTransactionFullPath();
    }

    std::string TransactionManager::GetTransactionPath( const std::string &tx_hash )
    {
        return GetBlockChainBase() + GeniusTransaction::GetTransactionFullPath( tx_hash );
    }

    std::string TransactionManager::GetTransactionProofPath( const GeniusTransaction &element )
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

    outcome::result<std::string> TransactionManager::GetExpectedProofKey( const std::string &tx_key,
                                                                          const std::shared_ptr<GeniusTransaction> &tx )
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

    outcome::result<std::shared_ptr<GeniusTransaction>> TransactionManager::DeSerializeTransaction(
        std::string tx_data )
    {
        BOOST_OUTCOME_TRY( auto dag, GeniusTransaction::DeSerializeDAGStruct( tx_data ) );

        auto it = GeniusTransaction::GetDeSerializers().find( dag.type() );
        if ( it == GeniusTransaction::GetDeSerializers().end() )
        {
            return std::errc::invalid_argument;
        }
        return it->second( std::vector<uint8_t>( tx_data.begin(), tx_data.end() ) );
    }

    outcome::result<std::shared_ptr<GeniusTransaction>> TransactionManager::DeSerializeEmbeddedTransaction(
        const EmbeddedTransaction &embedded )
    {
        // Ensure all deserializers are registered in the map
        // (also needed by DeSerializeTransaction for DAG-type lookups).
        static const bool registered = []
        {
            GeniusTransaction::RegisterDeserializer( "transfer", &TransferTransaction::DeSerializeByteVector );
            GeniusTransaction::RegisterDeserializer( "mint-v2", &MintTransactionV2::DeSerializeByteVector );
            GeniusTransaction::RegisterDeserializer( "mint", &MintTransaction::DeSerializeByteVector );
            GeniusTransaction::RegisterDeserializer( "migration", &MigrationTransaction::DeSerializeByteVector );
            GeniusTransaction::RegisterDeserializer( "escrow-hold", &EscrowTransaction::DeSerializeByteVector );
            GeniusTransaction::RegisterDeserializer( "escrow-release", &EscrowTransaction::DeSerializeByteVector );
            return true;
        }();
        (void) registered;

        // Dispatch on the oneof case — each branch calls the deserializer directly.
        switch ( embedded.transaction_case() )
        {
            case EmbeddedTransaction::kTransfer:
            {
                std::string bytes;
                embedded.transfer().SerializeToString( &bytes );
                return GeniusTransaction::GetDeSerializers().at( "transfer" )(
                    std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            }
            case EmbeddedTransaction::kMintV2:
            {
                std::string bytes;
                embedded.mint_v2().SerializeToString( &bytes );
                return GeniusTransaction::GetDeSerializers().at( "mint-v2" )(
                    std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            }
            case EmbeddedTransaction::kMint:
            {
                std::string bytes;
                embedded.mint().SerializeToString( &bytes );
                return GeniusTransaction::GetDeSerializers().at( "mint" )(
                    std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            }
            case EmbeddedTransaction::kMigration:
            {
                std::string bytes;
                embedded.migration().SerializeToString( &bytes );
                return GeniusTransaction::GetDeSerializers().at( "migration" )(
                    std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            }
            case EmbeddedTransaction::kEscrow:
            {
                std::string bytes;
                embedded.escrow().SerializeToString( &bytes );
                return GeniusTransaction::GetDeSerializers().at( "escrow-hold" )(
                    std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            }
            case EmbeddedTransaction::kEscrowRelease:
            {
                std::string bytes;
                embedded.escrow_release().SerializeToString( &bytes );
                return GeniusTransaction::GetDeSerializers().at( "escrow-release" )(
                    std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            }
            case EmbeddedTransaction::TRANSACTION_NOT_SET:
            default:
                return std::errc::invalid_argument;
        }
    }

    outcome::result<void> TransactionManager::ParseTransaction( const std::shared_ptr<GeniusTransaction> &tx )
    {
        auto it = transaction_parsers.find( tx->GetType() );
        if ( it == transaction_parsers.end() )
        {
            m_logger->info( "No Parser Available" );
            return std::errc::invalid_argument;
        }

        BOOST_OUTCOME_TRY( ( this->*it->second.first )( tx ) );
        UpdateAccountUTXOState( tx, true );
        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertTransaction( const std::shared_ptr<GeniusTransaction> &tx )
    {
        auto it = transaction_parsers.find( tx->GetType() );
        if ( it == transaction_parsers.end() )
        {
            m_logger->info( "No Reverter Available" );
            return std::errc::invalid_argument;
        }

        utxo_state_tracking_suppression_.fetch_add( 1 );
        auto revert_result = ( this->*( it->second.second ) )( tx );
        utxo_state_tracking_suppression_.fetch_sub( 1 );
        BOOST_OUTCOME_TRY( revert_result );
        UpdateAccountUTXOState( tx, false );
        return outcome::success();
    }

    void TransactionManager::UpdateAccountUTXOState( const std::shared_ptr<GeniusTransaction> &tx,
                                                     bool                                      increment_version )
    {
        if ( !tx || utxo_state_tracking_suppression_.load() != 0 )
        {
            return;
        }

        std::unordered_map<std::string, base::Hash256> roots;
        auto                                           add_address = [this, &roots]( const std::string &address )
        {
            if ( !ReplicatesAllAccounts( node_type_m ) && address != account_m->GetAddress() )
            {
                return;
            }
            if ( roots.find( address ) == roots.end() )
            {
                roots.emplace( address, account_m->GetUTXOManager().ComputeUTXOMerkleRoot( address ) );
            }
        };

        if ( tx->HasUTXOParameters() )
        {
            auto params_opt = tx->GetUTXOParametersOpt();
            if ( !params_opt.has_value() )
            {
                return;
            }

            const auto &[inputs, outputs] = params_opt.value();
            if ( !inputs.empty() )
            {
                add_address( tx->GetSrcAddress() );
            }
            for ( const auto &output : outputs )
            {
                if ( !output.dest_address.empty() )
                {
                    add_address( output.dest_address );
                }
            }
        }
        else if ( tx->GetType() == "mint" &&
                  !tx->GetSrcAddress().empty() ) // Legacy mint transactions still create UTXOs for the source account.
        {
            add_address( tx->GetSrcAddress() );
        }

        if ( roots.empty() )
        {
            return;
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

    outcome::result<std::shared_ptr<GeniusTransaction>> TransactionManager::FetchTransaction(
        crdt::GlobalDB  &db,
        std::string_view transaction_key )
    {
        BOOST_OUTCOME_TRY( auto transaction_data, db.Get( { std::string( transaction_key ) } ) );
        return DeSerializeTransaction( transaction_data );
    }

    outcome::result<std::shared_ptr<GeniusTransaction>> TransactionManager::DeSerializeTransaction(
        const base::Buffer &tx_data )
    {
        const auto &transaction_data_vector = tx_data.toVector();

        BOOST_OUTCOME_TRY( auto dag, GeniusTransaction::DeSerializeDAGStruct( transaction_data_vector ) );

        auto it = GeniusTransaction::GetDeSerializers().find( dag.type() );
        if ( it == GeniusTransaction::GetDeSerializers().end() )
        {
            return std::errc::invalid_argument;
        }
        return it->second( transaction_data_vector );
    }

    void TransactionManager::QueryTransactions()
    {
        for ( auto network_id : GetMonitoredNetworkIDs() )
        {
            const std::string query_path = GetBlockChainBase( network_id ) + "tx";
            m_logger->trace( "Probing transactions on {}", query_path );
            auto transaction_list = globaldb_m->QueryKeyValues( query_path );
            if ( transaction_list.has_error() )
            {
                m_logger->error( "Unable to query transactions on {}", query_path );
                continue;
            }

            m_logger->trace( "Transaction list grabbed from CRDT with Size {}", transaction_list.value().size() );

            for ( const auto &[key, value] : transaction_list.value() )
            {
                auto transaction_key = globaldb_m->KeyToString( key );
                if ( !transaction_key.has_value() )
                {
                    m_logger->error( "Unable to convert a key to string" );
                    continue;
                }
                auto process_result = FetchAndProcessTransaction( transaction_key.value(), value );
                if ( process_result.has_error() )
                {
                    m_logger->error( "Unable to fetch and process transaction {}", transaction_key.value() );
                }
            }
        }
    }

    outcome::result<void> TransactionManager::FetchAndProcessTransaction( const std::string          &tx_key,
                                                                          std::optional<base::Buffer> tx_data )
    {
        {
            std::shared_lock tx_lock( tx_mutex_m );
            auto             tracked = tx_processed_m.find( tx_key );
            if ( tracked != tx_processed_m.end() )
            {
                m_logger->trace( "Transaction already processed: {}", tx_key );
                return outcome::success();
            }
        }

        auto transaction_result = [&]()
        {
            if ( tx_data.has_value() )
            {
                m_logger->debug( "Deserializing transaction: {}", tx_key );
                return DeSerializeTransaction( tx_data.value() );
            }

            m_logger->debug( "Finding transaction: {}", tx_key );
            return FetchTransaction( *globaldb_m, tx_key );
        }();
        if ( transaction_result.has_error() )
        {
            m_logger->debug( "Can't fetch transaction {}", tx_key );
            return outcome::failure( transaction_result.error() );
        }

        auto &transaction = transaction_result.value();
        if ( transaction->GetHash().empty() )
        {
            m_logger->error( "Error, received transaction without hash: {}", tx_key );
            return outcome::failure( std::errc::invalid_argument );
        }

        m_logger->debug( "Checking if the transaction has a valid certificate to be confirmed {}", tx_key );

        auto next_tx_state = TransactionStatus::VERIFYING;

        if ( blockchain_->CheckCertificate( transaction->GetHash() ) )
        {
            m_logger->debug( "Transaction has a valid certificate, marking as CONFIRMED {}", tx_key );
            next_tx_state = TransactionStatus::CONFIRMED;
        }
        BOOST_OUTCOME_TRY( ChangeTransactionState( transaction, next_tx_state ) );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::PutProducedUTXOs( const GeniusTransaction &tx )
    {
        std::vector<GeniusUTXO> outputs;
        if ( !ExtractProducedUTXOs( tx, outputs ) )
        {
            return std::errc::invalid_argument;
        }

        for ( const auto &output : outputs )
        {
            BOOST_OUTCOME_TRY( account_m->GetUTXOManager().PutUTXO( output, output.GetOwnerAddress() ) );
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::DeleteProducedUTXOs( const GeniusTransaction &tx )
    {
        std::vector<GeniusUTXO> outputs;
        if ( !ExtractProducedUTXOs( tx, outputs ) )
        {
            return std::errc::invalid_argument;
        }

        for ( const auto &output : outputs )
        {
            BOOST_OUTCOME_TRY( account_m->GetUTXOManager().DeleteUTXO( output.GetTxID(),
                                                                       output.GetOutputIdx(),
                                                                       output.GetOwnerAddress() ) );
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseTransferTransaction( const std::shared_ptr<GeniusTransaction> &tx )
    {
        auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx );
        if ( !transfer_tx )
        {
            return std::errc::invalid_argument;
        }

        auto dest_infos = transfer_tx->GetDstInfos();

        BOOST_OUTCOME_TRY( PutProducedUTXOs( *transfer_tx ) );
        for ( const auto &dest_info : dest_infos )
        {
            m_logger->debug( "Notify {} of transfer of {} to it", dest_info.dest_address, dest_info.encrypted_amount );
        }

        for ( auto &input : transfer_tx->GetInputInfos() )
        {
            m_logger->trace( "UTXO to be updated {}", input.txid_hash_.toReadableString() );
            m_logger->trace( "UTXO output {}", input.output_idx_ );
        }
        BOOST_OUTCOME_TRY( account_m->GetUTXOManager().ConsumeUTXOs( transfer_tx->GetInputInfos(),
                                                                     TransferInputOwner( *transfer_tx ) ) );
        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseMintTransaction( const std::shared_ptr<GeniusTransaction> &tx )
    {
        if ( auto migration_tx = std::dynamic_pointer_cast<MigrationTransaction>( tx ) )
        {
            auto params = migration_tx->GetUTXOParameters();
            BOOST_OUTCOME_TRY( PutProducedUTXOs( *migration_tx ) );

            if ( !params.first.empty() )
            {
                BOOST_OUTCOME_TRY(
                    account_m->GetUTXOManager().ConsumeUTXOs( params.first, migration_tx->GetSrcAddress() ) );
            }

            m_logger->info( "Created tokens (migration), amount {} balance {}",
                            migration_tx->GetAmount(),
                            account_m->GetUTXOManager().GetBalance() );
            return outcome::success();
        }

        if ( auto mint_tx_v2 = std::dynamic_pointer_cast<MintTransactionV2>( tx ) )
        {
            auto params = mint_tx_v2->GetUTXOParameters();
            BOOST_OUTCOME_TRY( PutProducedUTXOs( *mint_tx_v2 ) );

            if ( !params.first.empty() )
            {
                BOOST_OUTCOME_TRY(
                    account_m->GetUTXOManager().ConsumeUTXOs( params.first,
                                                              mint_tx_v2->GetSrcAddress(),
                                                              sgns::UTXOManager::UTXOType::UTXO_BRIDGE ) );
            }

            m_logger->info( "Created tokens (mint-v2), amount {} balance {}",
                            mint_tx_v2->GetAmount(),
                            account_m->GetUTXOManager().GetBalance() );
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
        m_logger->info( "Created tokens, amount {} balance {}",
                        mint_tx->GetAmount(),
                        account_m->GetUTXOManager().GetBalance() );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::ParseEscrowTransaction( const std::shared_ptr<GeniusTransaction> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );
        if ( !escrow_tx )
        {
            return std::errc::invalid_argument;
        }

        auto params = escrow_tx->GetUTXOParameters();
        BOOST_OUTCOME_TRY( PutProducedUTXOs( *escrow_tx ) );

        if ( !params.first.empty() )
        {
            BOOST_OUTCOME_TRY( account_m->GetUTXOManager().ConsumeUTXOs( params.first, escrow_tx->GetSrcAddress() ) );
        }

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertTransferTransaction( const std::shared_ptr<GeniusTransaction> &tx )
    {
        auto transfer_tx = std::dynamic_pointer_cast<TransferTransaction>( tx );
        if ( !transfer_tx )
        {
            return std::errc::invalid_argument;
        }

        auto dest_infos = transfer_tx->GetDstInfos();

        BOOST_OUTCOME_TRY( DeleteProducedUTXOs( *transfer_tx ) );
        for ( const auto &dest_info : dest_infos )
        {
            m_logger->debug( "Notify {} of deletion of {} to it", dest_info.dest_address, dest_info.encrypted_amount );
        }

        BOOST_OUTCOME_TRY( account_m->GetUTXOManager().RestoreConsumedUTXOs( transfer_tx->GetInputInfos(),
                                                                             TransferInputOwner( *transfer_tx ) ) );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertMintTransaction( const std::shared_ptr<GeniusTransaction> &tx )
    {
        auto revert_utxo_mint = [this]( const auto &mint_tx, const char *label ) -> outcome::result<void>
        {
            auto params = mint_tx->GetUTXOParameters();

            BOOST_OUTCOME_TRY( DeleteProducedUTXOs( *mint_tx ) );
            if ( !params.first.empty() )
            {
                account_m->GetUTXOManager().RollbackUTXOs( params.first, mint_tx->GetHash() );
            }

            m_logger->info( "Deleted {} tokens ({}), from tx {}, final balance {}",
                            mint_tx->GetAmount(),
                            label,
                            mint_tx->GetHash(),
                            account_m->GetUTXOManager().GetBalance() );
            return outcome::success();
        };

        if ( auto migration_tx = std::dynamic_pointer_cast<MigrationTransaction>( tx ) )
        {
            BOOST_OUTCOME_TRY( revert_utxo_mint( migration_tx, "migration" ) );
            return outcome::success();
        }

        if ( auto mint_tx_v2 = std::dynamic_pointer_cast<MintTransactionV2>( tx ) )
        {
            BOOST_OUTCOME_TRY( revert_utxo_mint( mint_tx_v2, "mint-v2" ) );
            return outcome::success();
        }

        auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx );
        if ( !mint_tx )
        {
            return std::errc::invalid_argument;
        }

        auto hash = ( base::Hash256::fromReadableString( mint_tx->GetHash() ) ).value();
        BOOST_OUTCOME_TRY( account_m->GetUTXOManager().DeleteUTXO( hash, 0, mint_tx->GetSrcAddress() ) );
        m_logger->info( "Deleted {} tokens, from tx {}, final balance {}",
                        mint_tx->GetAmount(),
                        mint_tx->GetHash(),
                        account_m->GetUTXOManager().GetBalance() );

        return outcome::success();
    }

    outcome::result<void> TransactionManager::RevertEscrowTransaction( const std::shared_ptr<GeniusTransaction> &tx )
    {
        auto escrow_tx = std::dynamic_pointer_cast<EscrowTransaction>( tx );
        if ( !escrow_tx )
        {
            return std::errc::invalid_argument;
        }

        if ( auto params = escrow_tx->GetUTXOParameters(); !params.second.empty() )
        {
            BOOST_OUTCOME_TRY( DeleteProducedUTXOs( *escrow_tx ) );
            BOOST_OUTCOME_TRY(
                account_m->GetUTXOManager().RestoreConsumedUTXOs( params.first, escrow_tx->GetSrcAddress() ) );
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

    size_t TransactionManager::CountTransactions( std::optional<TransactionStatus> tx_status ) const
    {
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        return std::count_if( tx_processed_m.cbegin(),
                              tx_processed_m.cend(),
                              [&tx_status]( const auto &entry )
                              { return !tx_status || entry.second.status == tx_status.value(); } );
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
                m_logger->debug( "Transaction is FINALIZED" );
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
                m_logger->trace( "Searching for transaction {}", txId );
                bool found = false;
                for ( const auto &[_, tracked] : tx_processed_m )
                {
                    if ( tracked.tx && tracked.tx->GetHash() == txId &&
                         tracked.tx->GetSrcAddress() == account_m->GetAddress() )
                    {
                        retval = tracked.status;
                        m_logger->trace( "Transaction status is {}", static_cast<int>( retval ) );
                        found = true;
                        break;
                    }
                }
                if ( !found )
                {
                    m_logger->trace( "Transaction untracked" );
                    retval = TransactionStatus::FAILED;
                }
            }

            if ( retval == TransactionStatus::INVALID || retval == TransactionStatus::CONFIRMED ||
                 retval == TransactionStatus::UNCONFIRMED || retval == TransactionStatus::FAILED )
            {
                m_logger->trace( "Transaction has finalized state {}", static_cast<int>( retval ) );
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
            m_logger->warn( "Invalid original escrow tx id while waiting release: {}", originalEscrowId );
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
                m_logger->debug( "Escrow hold ({},0) is consumed", originalEscrowId );
                return TransactionStatus::CONFIRMED;
            }

            if ( is_escrow_spent_by_confirmed_transfer() )
            {
                m_logger->debug( "Escrow release confirmed via tracked transfer spend for {}", originalEscrowId );
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
        m_logger->debug( "Initializing UTXOs" );

        auto utxo_result = account_m->GetUTXOManager().LoadUTXOs( globaldb_m->GetDataStore() );
        if ( utxo_result.has_error() )
        {
            m_logger->error( "Failed to load UTXOs from storage" );
        }

        bool has_local_utxos    = utxo_result.has_value() && utxo_result.value();
        auto monitored_networks = GetMonitoredNetworkIDs();

        if ( has_local_utxos )
        {
            auto checkpoint_result = account_m->GetUTXOManager().LoadLatestCheckpoint( account_m->GetAddress() );
            if ( checkpoint_result.has_error() )
            {
                m_logger->warn( "Failed to load local UTXO checkpoint during init: {}",
                                checkpoint_result.error().message() );
            }
            else if ( checkpoint_result.value().has_value() )
            {
                const auto local_root = account_m->GetUTXOManager().ComputeUTXOMerkleRoot( account_m->GetAddress() );
                if ( local_root != checkpoint_result.value()->utxo_merkle_root )
                {
                    m_logger->warn(
                        "Local UTXO root mismatch with checkpoint during init. Clearing local UTXOs and rebuilding" );

                    auto clear_result = account_m->GetUTXOManager().SetUTXOs( std::vector<GeniusUTXO>{},
                                                                              account_m->GetAddress() );
                    if ( clear_result.has_error() )
                    {
                        m_logger->error( "Failed to clear local UTXOs after checkpoint mismatch: {}",
                                         clear_result.error().message() );
                    }
                    else
                    {
                        has_local_utxos = false;
                    }
                }
            }
        }

        if ( !has_local_utxos )
        {
            m_logger->info( "No local or network UTXOs found, querying transactions to mount UTXOs" );
            QueryTransactions();
            return;
        }

        auto utxo_map = account_m->GetUTXOManager().GetAllUTXOs();

        if ( has_local_utxos )
        {
            for ( const auto &[address, utxo_data_vector] : utxo_map )
            {
                m_logger->debug( "Loaded {} UTXOs for address {}", utxo_data_vector.size(), address.substr( 0, 8 ) );
                for ( auto &utxo_data : utxo_data_vector )
                {
                    auto &[utxo_state, utxo] = utxo_data;
                    const auto tx_hash       = utxo.GetTxID().toReadableString();
                    m_logger->debug( "UTXO - state: {}, tx_hash: {}, index: {}, amount: {}",
                                     static_cast<uint8_t>( utxo_state ),
                                     tx_hash,
                                     utxo.GetOutputIdx(),
                                     utxo.GetAmount() );

                    if ( utxo_state != UTXOManager::UTXOState::UTXO_READY )
                    {
                        m_logger->debug( "Skipping UTXO in state {} for tx {}",
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
                            m_logger->debug( "Processed transaction in {}", tx_path );
                            processed = true;
                            break;
                        }
                    }

                    // Incomplete history for another account must not prevent this account from starting.
                    if ( !processed && address == account_m->GetAddress() )
                    {
                        std::lock_guard missing_lock( missing_tx_mutex_ );
                        missing_tx_hashes_.insert( tx_hash );
                    }
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
            else
            {
                RequestRelevantHeads();
            }
            return;
        }

        // TODO - Remove this once we remove the passive heads processing or we want transactions we are not subscribed here
        return;

        m_logger->info( "Missing {} transactions during init", missing_count );

        auto now = std::chrono::steady_clock::now();
        if ( last_init_tx_request_time_ != std::chrono::steady_clock::time_point{} &&
             now - last_init_tx_request_time_ < k_init_tx_request_cooldown_ms )
        {
            m_logger->debug( "Skipping tx requests (init cooldown)" );
            return;
        }
        last_init_tx_request_time_ = now;

        const auto request_timeout = k_init_tx_request_cooldown_ms;
        for ( const auto &tx_hash : missing_tx_hashes_copy )
        {
            m_logger->debug( "Requesting transaction with hash {} (this: {})",
                             tx_hash,
                             reinterpret_cast<uint64_t>( this ) );
            auto request_result = account_m->RequestTransaction( request_timeout, tx_hash );
            if ( request_result.has_error() )
            {
                m_logger->error( "Failed to request transaction with hash {}", tx_hash );
            }
            else
            {
                m_logger->debug( "Successfully requested transaction with hash {}", tx_hash );
            }
        }
    }

    bool TransactionManager::CheckNonce() const
    {
        // Genesis-creating full node — no peers, no prior UTXOs, nonce is trivially zero.
        // The PubSub broadcast would just time out (pre-consensus legacy path).
        if ( ReplicatesAllAccounts( node_type_m ) &&
             account_m->GetAddress() == Blockchain::GetAuthorizedFullNodeAddress() )
        {
            m_logger->debug( "Genesis full node — skipping network nonce check" );
            return true;
        }

        m_logger->debug( "Checking if my local confirmed nonce is in sync with the network" );

        const auto now                               = std::chrono::steady_clock::now();
        const bool regular_node_retry_is_on_cooldown = !ReplicatesAllAccounts( node_type_m ) &&
                                                       last_nonce_request_time_ !=
                                                           std::chrono::steady_clock::time_point{} &&
                                                       now < last_nonce_request_time_ + NONCE_REQUEST_TIMEOUT;
        if ( regular_node_retry_is_on_cooldown )
        {
            return false;
        }
        last_nonce_request_time_ = now;

        auto nonce_from_network_result = account_m->FetchNetworkNonce( NONCE_REQUEST_TIMEOUT );
        if ( nonce_from_network_result.has_error() )
        {
            m_logger->error( "Failed to fetch network nonce: {}", nonce_from_network_result.error().message() );
            if ( ReplicatesAllAccounts( node_type_m ) )
            {
                m_logger->debug(
                    "Network nonce fetch failed, but we have a replicating node configured. Allowing it to boot" );
                return true;
            }
            return false;
        }
        auto maybe_nonce = nonce_from_network_result.value();
        if ( !maybe_nonce.has_value() )
        {
            m_logger->error( "Network doesn't have nonce info, trusting local nonce" );
            return true;
        }

        auto network_nonce      = maybe_nonce.value();
        auto local_nonce_result = account_m->GetLocalConfirmedNonce();
        if ( local_nonce_result.has_error() )
        {
            m_logger->debug( "No local nonce found. Network nonce exists: {}", network_nonce );
            return false;
        }
        auto local_nonce = local_nonce_result.value();

        if ( network_nonce > local_nonce )
        {
            m_logger->error( "Nonce mismatch - Network: {}, Local: {}", network_nonce, local_nonce );

            return false;
        }
        m_logger->debug( "Nonce is in sync with the network - Network: {}, Local: {}", network_nonce, local_nonce );
        return true;
    }

    void TransactionManager::SyncNonce()
    {
        m_logger->debug( "Checking if my nonce is updated" );

        auto     nonce_result    = account_m->GetConfirmedNonce( NONCE_REQUEST_TIMEOUT );
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
            m_logger->debug( "Network nonce updated: {}", expected_next_nonce );
            ChangeState( State::READY );
        }
        else if ( proposed_nonce > expected_next_nonce )
        {
            m_logger->error( "Local nonce ahead - Local: {}, Expected: {}. Checking for invalid tx",
                             proposed_nonce,
                             expected_next_nonce );
            std::set<uint64_t> nonces_to_check;
            for ( auto i = expected_next_nonce; i < proposed_nonce; ++i )
            {
                nonces_to_check.insert( i );
                m_logger->debug( "Inserting nonce to check: {}", i );
            }

            (void) CheckTransactionValidity( nonces_to_check );
        }
        else if ( proposed_nonce < expected_next_nonce )
        {
            uint64_t nonce_gap = expected_next_nonce - proposed_nonce;
            m_logger->error( "Local nonce behind - Local: {}, Expected: {}. Gap: {}. Waiting to sync",
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
                m_logger->trace( "Skipping head request - too soon since last request ({}s ago)", elapsed.count() );
                return;
            }
        }

        auto topics_result = globaldb_m->GetMonitoredTopics();
        if ( !topics_result.has_value() )
        {
            m_logger->warn( "Could not get monitored topics for head request" );
            return;
        }
        m_logger->info( "Requesting heads for {} topics", topics_result.value().size() );

        if ( account_m->RequestHeads( topics_result.value() ) )
        {
            last_head_request_time_ = now;
            m_logger->debug( "Periodic sync head request sent for {} topics", topics_result.value().size() );
        }
        else
        {
            m_logger->warn( "Failed to request heads" );
        }
    }

    outcome::result<bool> TransactionManager::CheckTransactionValidity( const std::set<uint64_t> &nonces_to_check )
    {
        bool                     changed = false;
        std::vector<std::string> invalid_transaction_keys;
        {
            std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m );
            m_logger->debug( "{}: Checking transactions", __func__ );

            for ( auto &nonce : nonces_to_check )
            {
                for ( auto &[key, tracked] : tx_processed_m )
                {
                    if ( !tracked.tx || tracked.tx->GetSrcAddress() != account_m->GetAddress() )
                    {
                        continue;
                    }

                    m_logger->debug( "{}: Seeing if transaction {} is valid {}",
                                     __func__,
                                     tracked.cached_nonce,
                                     nonce );

                    if ( tracked.cached_nonce == nonce )
                    {
                        bool valid_tx = true;
                        if ( !CheckTransactionAuthorization( *tracked.tx ) )
                        {
                            m_logger->error( "Could not validate signature of transaction with nonce {}", nonce );
                            valid_tx = false;
                        }
                        else
                        {
                            m_logger->debug( "{}: Transaction is valid with {}", __func__, nonce );
                        }
                        if ( !valid_tx )
                        {
                            // Collect the key for later removal
                            invalid_transaction_keys.push_back( key );
                            changed = true;
                            m_logger->debug( "{}: INVALID TX {}", __func__, nonce );
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

        m_logger->debug( "Deleting transaction on {}", tx_key );

        BOOST_OUTCOME_TRY( crdt_transaction->Remove( { std::move( tx_key ) } ) );

        m_logger->debug( "Removed key transaction on {}", tx_key );

        BOOST_OUTCOME_TRY( crdt_transaction->Commit( topics ) );

        m_logger->debug( "Commited tx on {}", tx_key );

        return outcome::success();
    }

    std::shared_ptr<GeniusTransaction> TransactionManager::GetTransactionByHash( const std::string &tx_hash ) const
    {
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        return GetTransactionByHashNoLock( tx_hash );
    }

    std::shared_ptr<GeniusTransaction> TransactionManager::GetTransactionByHashNoLock(
        const std::string &tx_hash ) const
    {
        for ( const auto &[_, tracked] : tx_processed_m )
        {
            m_logger->debug( "Searching for hash {}", tx_hash );
            if ( tracked.tx && tracked.tx->GetHash() == tx_hash )
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

    TransactionManager::TransactionStatus TransactionManager::GetTransactionStatusByTxId(
        const std::string &txId ) const
    {
        return GetStatusByTxId( txId, std::nullopt );
    }

    TransactionManager::TransactionStatus TransactionManager::GetOutgoingStatusByTxId( const std::string &txId ) const
    {
        return GetStatusByTxId( txId, true );
    }

    TransactionManager::TransactionStatus TransactionManager::GetStatusByTxId( const std::string  &txId,
                                                                               std::optional<bool> outgoing ) const
    {
        auto                                incoming_status = TransactionStatus::INVALID;
        std::shared_lock<std::shared_mutex> tx_lock( tx_mutex_m );
        for ( const auto &[_, tracked] : tx_processed_m )
        {
            if ( !tracked.tx || tracked.tx->GetHash() != txId )
            {
                continue;
            }

            const auto is_outgoing = tracked.tx->GetSrcAddress() == account_m->GetAddress();
            if ( outgoing.has_value() )
            {
                if ( is_outgoing == outgoing.value() )
                {
                    return tracked.status;
                }
                continue;
            }

            if ( is_outgoing )
            {
                return tracked.status;
            }
            incoming_status = tracked.status;
        }
        return incoming_status;
    }

    bool TransactionManager::SetOutgoingStatusByNonce( uint64_t nonce, TransactionStatus s )
    {
        bool                                ret = false;
        std::shared_ptr<GeniusTransaction>  tx;
        std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m );
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
            m_logger->debug( "No outgoing tx found with nonce {}", nonce );
        }
        return ret;
    }

    std::optional<std::vector<crdt::pb::Element>> TransactionManager::FilterTransaction(
        const crdt::pb::Element &element )
    {
        std::optional<std::vector<crdt::pb::Element>> maybe_tombstones;
        bool                                          should_delete = true;
        std::shared_ptr<GeniusTransaction>            new_tx;
        do
        {
            auto maybe_new_tx = DeSerializeTransaction( element.value() );
            if ( maybe_new_tx.has_error() )
            {
                m_logger->error( "Failed to deserialize incoming transaction {}", element.key() );
                break;
            }
            new_tx = maybe_new_tx.value();

            if ( !CheckTransactionAuthorization( *new_tx ) )
            {
                m_logger->error( "Could not validate signature of transaction {}", element.key() );
                break;
            }
            if ( KeyExistsInDB( GetTransactionPath( *new_tx ) ) )
            {
                m_logger->debug( "New transaction {} would overwrite an existing one. Preventing that",
                                 new_tx->GetHash() );
                break;
            }
            should_delete = false;

        } while ( 0 );

        if ( should_delete )
        {
            std::vector<crdt::pb::Element> additional_elements_to_delete;
            std::optional<std::string>     proof_key;
            if ( new_tx )
            {
                proof_key = GetTransactionProofPath( *new_tx );
            }
            else if ( const auto tx_pos = element.key().find( "/tx/" ); tx_pos != std::string::npos )
            {
                proof_key = element.key();
                proof_key->replace( tx_pos, 4, "/proof/" );
                if ( proof_key->size() <= tx_pos + 7 )
                {
                    proof_key.reset();
                }
            }

            if ( proof_key.has_value() )
            {
                crdt::pb::Element proof_element;
                proof_element.set_key( proof_key.value() );
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
                m_logger->error( "Could not verify proof {}", element.key() );
                break;
            }
            m_logger->trace( "Valid proof of {}", element.key() );

            valid_proof = true;
        } while ( 0 );

        if ( !valid_proof )
        {
            std::vector<crdt::pb::Element> tombstones;
            tombstones.push_back( element );
            if ( const auto proof_pos = element.key().find( "/proof/" ); proof_pos != std::string::npos )
            {
                std::string tx_key = element.key();
                tx_key.replace( proof_pos, 7, "/tx/" );
                if ( tx_key.size() > proof_pos + 4 )
                {
                    crdt::pb::Element tx_tombstone;
                    tx_tombstone.set_key( std::move( tx_key ) );
                    tombstones.push_back( tx_tombstone );
                }
            }
            maybe_tombstones = tombstones;
        }

        return maybe_tombstones;
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
            m_logger->debug( "Transaction timestamp {} is in the future (current: {}), elapsed: {} ms",
                             timestamp,
                             current_timestamp,
                             elapsed );
        }
        else
        {
            m_logger->trace( "Transaction timestamp {} elapsed: {} ms", timestamp, elapsed );
        }

        return elapsed;
    }

    int64_t TransactionManager::GetElapsedTime( uint64_t timestamp ) const
    {
        return GetElapsedTime( timestamp, GetCurrentTimestamp() );
    }

    bool TransactionManager::IsTransactionImmutable( const GeniusTransaction &tx ) const
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
            m_logger->debug( "Transaction from future is not immutable (elapsed: {} ms)", elapsed );
            return false;
        }

        bool is_immutable = elapsed > mutability_window_m.count();

        if ( is_immutable )
        {
            m_logger->debug( "Transaction is immutable (elapsed: {} ms, window: {} ms)",
                             elapsed,
                             mutability_window_m.count() );
        }
        else
        {
            m_logger->trace( "Transaction is still mutable (elapsed: {} ms, window: {} ms)",
                             elapsed,
                             mutability_window_m.count() );
        }

        return is_immutable;
    }

    void TransactionManager::SetTimeFrameToleranceMs( uint64_t timeframe_tolerance )
    {
        timestamp_tolerance_m = std::chrono::milliseconds( timeframe_tolerance );

        m_logger->info( "Updated timeframe tolerance to {} ms", timeframe_tolerance );
    }

    void TransactionManager::SetMutabilityWindowMs( uint64_t mutability_window )
    {
        mutability_window_m = std::chrono::milliseconds( mutability_window );

        m_logger->info( "Updated mutability window to {} ms", mutability_window );
    }

    outcome::result<void> TransactionManager::RemoveTransactionFromProcessedMaps( const std::string &transaction_key,
                                                                                  bool               delete_from_crdt )
    {
        m_logger->debug( "Removing transaction from processed maps: {}", transaction_key );
        bool found = false;
        {
            std::unique_lock tx_lock( tx_mutex_m );
            auto             it = tx_processed_m.find( transaction_key );
            if ( it != tx_processed_m.end() )
            {
                m_logger->debug( "Removing from processed: {}", transaction_key );

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
            m_logger->debug( "Transaction not found in processed maps: {}", transaction_key );
        }
        return outcome::success();
    }

    outcome::result<void> TransactionManager::AddTransactionToProcessedMaps(
        crdt::CRDTCallbackManager::NewDataPair new_data )
    {
        auto [key, value] = new_data;

        m_logger->debug( "Trying to deserialize {}", key );

        BOOST_OUTCOME_TRY( auto new_tx, DeSerializeTransaction( value ) );

        m_logger->debug( "Deserialized transaction {}", key );

        if ( new_tx->GetHash().empty() )
        {
            m_logger->error( "Empty hash on {}", key );
            return outcome::failure( boost::system::error_code{} );
        }

        m_logger->debug( "Verifying if we have a conflicting transaction {}", key );

        auto conflicting_txs = GetConflictingTransactions( *new_tx );

        if ( !conflicting_txs.empty() )
        {
            bool has_confirmed_conflict = false;
            {
                std::shared_lock tx_lock( tx_mutex_m );
                has_confirmed_conflict = std::any_of(
                    conflicting_txs.begin(),
                    conflicting_txs.end(),
                    [&]( const auto &conflict )
                    {
                        const auto it = tx_processed_m.find( GetTransactionPath( conflict->GetHash() ) );
                        return it != tx_processed_m.end() && it->second.status == TransactionStatus::CONFIRMED;
                    } );
            }
            if ( has_confirmed_conflict )
            {
                m_logger->debug( "A conflicting transaction is already CONFIRMED, not adding incoming transaction {}",
                                 key );
                BOOST_OUTCOME_TRY( ChangeTransactionState( new_tx, TransactionStatus::FAILED ) );
                return outcome::failure( boost::system::error_code{} );
            }
            for ( const auto &conflict : conflicting_txs )
            {
                const auto tracked = GetTrackedTxByHash( conflict->GetHash() );
                if ( !tracked.has_value() || tracked->status == TransactionStatus::FAILED ||
                     tracked->status == TransactionStatus::VERIFYING )
                {
                    continue;
                }
                m_logger->warn( "Setting conflicting transaction to VERIFYING since it's not confirmed: {}",
                                conflict->GetHash() );
                BOOST_OUTCOME_TRY( ChangeTransactionState( conflict, TransactionStatus::VERIFYING ) );
            }
        }

        m_logger->debug( "Checking if the transaction has a valid certificate to be confirmed {}", key );

        auto next_tx_state = TransactionStatus::VERIFYING;
        auto has_cert      = blockchain_->CheckCertificate( new_tx->GetHash() );

        if ( has_cert )
        {
            m_logger->debug( "Transaction has a valid certificate, marking as CONFIRMED {}", key );
            next_tx_state = TransactionStatus::CONFIRMED;
            for ( const auto &conflict : conflicting_txs )
            {
                const auto tracked = GetTrackedTxByHash( conflict->GetHash() );
                if ( !tracked.has_value() || tracked->status == TransactionStatus::FAILED )
                {
                    continue;
                }
                m_logger->warn(
                    "Setting conflicting transaction to FAILED because the new has a certificate and it doesn't: {}",
                    conflict->GetHash() );
                BOOST_OUTCOME_TRY( ChangeTransactionState( conflict, TransactionStatus::FAILED ) );
            }
        }

        auto maybe_existing = GetTrackedTxByHash( new_tx->GetHash() );
        if ( maybe_existing.has_value() && next_tx_state == TransactionStatus::VERIFYING )
        {
            const auto current_status = maybe_existing->status;
            if ( current_status == TransactionStatus::FAILED || current_status == TransactionStatus::CONFIRMED )
            {
                m_logger->debug( "Keeping terminal status {} for tx {}, skipping downgrade to VERIFYING (has_cert={})",
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
        m_logger->debug( "Processing deletion of {}", key );

        auto remove_res = RemoveTransactionFromProcessedMaps( key );

        if ( remove_res.has_error() )
        {
            m_logger->error( "Error removing transaction {}: {}", key, remove_res.error().message() );
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
            m_logger->error( "RocksDB datastore unavailable, cannot store CID for tx {}", key );
            return outcome::failure( std::errc::bad_file_descriptor );
        }

        crdt::GlobalDB::Buffer key_buffer;
        key_buffer.put( key );

        crdt::GlobalDB::Buffer value_buffer;
        value_buffer.put( cid );

        BOOST_OUTCOME_TRY( datastore->put( key_buffer, value_buffer ) );

        return outcome::success();
    }

    void TransactionManager::ProcessNewData( crdt::CRDTCallbackManager::NewDataPair new_data )
    {
        m_logger->debug( "Processing new data with key {}", new_data.first );

        auto add_res = AddTransactionToProcessedMaps( new_data );

        if ( add_res.has_error() )
        {
            m_logger->error( "Error adding transaction {}: {}", new_data.first, add_res.error().message() );
        }
        else
        {
            // Successfully received and processed new transaction data
            // Mark that we've received data (for periodic sync interval adjustment)
            if ( !received_first_periodic_sync_response_.load() )
            {
                received_first_periodic_sync_response_.store( true );
                m_logger->info(
                    "First transaction data received from network, switching to 10-minute periodic sync interval" );
            }
        }
    }

    void TransactionManager::NewElementCallback( crdt::CRDTCallbackManager::NewDataPair new_data, std::string cid )
    {
        auto store_cid_res = StoreTransactionCID( new_data.first, cid );
        if ( store_cid_res.has_error() )
        {
            m_logger->error( "Failed to store CID for key {}: {}", new_data.first, store_cid_res.error().message() );
        }

        auto key = new_data.first;

        std::size_t queue_size = 0;
        {
            std::lock_guard lock( cv_mutex_ );
            new_data_queue_.push( std::move( new_data ) );
            queue_size = new_data_queue_.size();
        }

        cv_.notify_one();

        m_logger->debug( "CRDT new data queued, {} - (queue size: {})", key, queue_size );
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

        m_logger->debug( "CRDT deleted key queued, {} - (queue size: {})", deleted_key, queue_size );
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
        std::lock_guard lock( state_change_callback_mutex_ );
        if ( state_m == new_state )
        {
            return;
        }
        m_logger->info( "State changed from {} to {}", state_m, new_state );
        auto old_state = state_m;
        state_m        = new_state;
        if ( state_change_callback_ )
        {
            state_change_callback_( old_state, new_state );
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
            m_logger->debug( "Looking for CID of tx {} in network {}", tx_hash, network_id );
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

    std::vector<std::shared_ptr<GeniusTransaction>> TransactionManager::GetConflictingTransactions(
        const GeniusTransaction &element ) const
    {
        std::vector<std::shared_ptr<GeniusTransaction>> conflicts;
        std::shared_lock                                tx_lock( tx_mutex_m );
        for ( const auto &[_, tracked] : tx_processed_m )
        {
            if ( tracked.tx && tracked.cached_nonce == element.GetNonce() &&
                 tracked.tx->GetSrcAddress() == element.GetSrcAddress() && tracked.tx->GetHash() != element.GetHash() )
            {
                conflicts.push_back( tracked.tx );
            }
        }
        return conflicts;
    }

    bool TransactionManager::HasConfirmedInputConflict( const std::shared_ptr<GeniusTransaction> &candidate_tx ) const
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

    void TransactionManager::OnProposalTimeoutCleanup( const std::string &tx_hash )
    {
        auto tx = GetTransactionByHash( tx_hash );
        if ( !tx )
        {
            // D-10: Entry not found — silently return, nothing to clean up.
            return;
        }

        std::unique_lock tx_lock( tx_mutex_m );
        const auto       key = GetTransactionPath( *tx );
        auto             it  = tx_processed_m.find( key );
        if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::VERIFYING )
        {
            if ( tx->GetSrcAddress() == account_m->GetAddress() )
            {
                tx_lock.unlock(); // ChangeTransactionState acquires its own lock
                m_logger->info( "{}: Proposal timeout — transitioning local tx to UNCONFIRMED tx={}",
                                __func__,
                                tx_hash );
                (void) ChangeTransactionState( tx, TransactionStatus::UNCONFIRMED );
                return;
            }

            m_logger->info( "{}: Proposal timeout — removing remote temp entry tx={}", __func__, tx_hash );
            tx_processed_m.erase( it );
        }
        // D-10: Entry not in map OR entry status is not VERIFYING → silently skip.
    }

    outcome::result<ConsensusManager::Check> TransactionManager::OnConsensusCertificate(
        const std::string          &tx_hash,
        const ConsensusCertificate &certificate )
    {
        m_logger->debug( "{}: Consensus certificate arrived for transaction {}", __func__, tx_hash );
        auto tx                             = GetTransactionByHash( tx_hash );
        bool reconstructed_from_certificate = false;
        if ( !tx )
        {
            // CONFLICT-01 / NONCE-01: Standalone validator without local transaction state.
            // Deserialize from the certificate's embedded proposal (Phase 1 transaction).
            auto nonce_subject_result = ConsensusManager::DecodeNonceSubject( certificate.proposal().subject() );
            if ( nonce_subject_result.has_error() )
            {
                m_logger->warn( "{}: Certificate for hash {} has no decodable NonceSubject, "
                                "accepting",
                                __func__,
                                tx_hash );
                // METRICS-01: Certificate fallback deserialization failure
                metrics_cert_fallback_failure_.fetch_add( 1, std::memory_order_relaxed );
                return ConsensusManager::Check::Approve;
            }
            const auto &nonce_subject = nonce_subject_result.value();

            if ( nonce_subject.transaction().transaction_case() == EmbeddedTransaction::TRANSACTION_NOT_SET )
            {
                m_logger->warn( "{}: Certificate for hash {} has no embedded transaction "
                                "(pre-Phase-1 certificate), accepting",
                                __func__,
                                tx_hash );
                return ConsensusManager::Check::Approve;
            }

            auto tx_result = DeSerializeEmbeddedTransaction( nonce_subject.transaction() );
            if ( tx_result.has_error() )
            {
                m_logger->warn( "{}: Failed to deserialize tx from certificate for hash {}, "
                                "accepting certificate",
                                __func__,
                                tx_hash );
                metrics_cert_fallback_failure_.fetch_add( 1, std::memory_order_relaxed );
                return ConsensusManager::Check::Approve;
            }
            tx = tx_result.value();

            // Verify hash binding — deserialized tx must match certificate's tx_hash
            if ( tx->GetHash() != tx_hash || !tx->CheckHash() )
            {
                m_logger->warn( "{}: Certificate-embedded tx hash mismatch for {}, "
                                "accepting certificate without processing embedded data",
                                __func__,
                                tx_hash );
                metrics_cert_fallback_failure_.fetch_add( 1, std::memory_order_relaxed );
                return ConsensusManager::Check::Approve;
            }
            reconstructed_from_certificate = true;
        }

        auto conflicting_txs = GetConflictingTransactions( *tx );
        for ( const auto &conflict : conflicting_txs )
        {
            auto tracked = GetTrackedTxByHash( conflict->GetHash() );
            if ( tracked.has_value() && tracked->status == TransactionStatus::CONFIRMED )
            {
                m_logger->critical( "{}: Conflicting transaction {} is already CONFIRMED while processing "
                                    "certificate winner {}; refusing contradictory finality",
                                    __func__,
                                    conflict->GetHash(),
                                    tx_hash );
                return ConsensusManager::Check::Stalled;
            }
        }

        for ( const auto &conflict : conflicting_txs )
        {
            auto tracked = GetTrackedTxByHash( conflict->GetHash() );
            if ( tracked.has_value() && tracked->status == TransactionStatus::FAILED )
            {
                continue;
            }
            m_logger->warn( "{}: Failing transaction {} superseded by certified transaction {}",
                            __func__,
                            conflict->GetHash(),
                            tx_hash );
            if ( auto result = ChangeTransactionState( conflict, TransactionStatus::FAILED ); result.has_error() )
            {
                m_logger->error( "{}: Failed to mark superseded transaction {} as FAILED: {}",
                                 __func__,
                                 conflict->GetHash(),
                                 result.error().message() );
                return outcome::failure( result.error() );
            }
        }

        if ( auto result = ChangeTransactionState( tx, TransactionStatus::CONFIRMED ); result.has_error() )
        {
            m_logger->error( "{}: Failed to confirm certified transaction {}: {}",
                             __func__,
                             tx_hash,
                             result.error().message() );
            if ( reconstructed_from_certificate )
            {
                metrics_cert_fallback_failure_.fetch_add( 1, std::memory_order_relaxed );
            }
            return outcome::failure( result.error() );
        }

        if ( reconstructed_from_certificate )
        {
            metrics_cert_fallback_success_.fetch_add( 1, std::memory_order_relaxed );
            m_logger->info( "{}: Standalone validator confirmed tx {} from certificate proposal_id={}",
                            __func__,
                            tx_hash,
                            certificate.proposal_id() );
        }
        else
        {
            m_logger->debug( "{}: Transaction {} confirmed by consensus", __func__, tx_hash );
        }

        auto tx_hash_bin = base::Hash256::fromReadableString( tx_hash );
        if ( tx_hash_bin.has_error() )
        {
            m_logger->error( "{}: Could not parse tx hash for checkpoint tx={}", __func__, tx_hash );
            return outcome::failure( tx_hash_bin.error() );
        }

        auto validator_registry = blockchain_->GetValidatorRegistry();
        if ( !validator_registry )
        {
            m_logger->error( "{}: No validator registry, skipping checkpoint", __func__ );
            return outcome::failure( std::errc::no_such_device );
        }

        const uint64_t registry_epoch = validator_registry->GetRegistryEpoch();
        const auto     registry_cid   = validator_registry->GetRegistryCid();
        auto           registry_hash  = crypto::sha2_256( registry_cid.data(), registry_cid.size() );

        if ( auto checkpoint_res = account_m->GetUTXOManager().CreateCheckpoint( registry_epoch,
                                                                                 tx_hash_bin.value(),
                                                                                 registry_hash );
             checkpoint_res.has_error() )
        {
            m_logger->error( "{}: Failed to create UTXO checkpoint tx={} epoch={} err={}",
                             __func__,
                             tx_hash,
                             registry_epoch,
                             checkpoint_res.error().message() );
        }
        m_logger->debug( "{}: Transaction approved: {:.8}", __func__, tx_hash );
        return ConsensusManager::Check::Approve;
    }

    outcome::result<ConsensusManager::ValidationResult> TransactionManager::HandleNonceConsensusSubject(
        const ConsensusManager::Subject &subject )
    {
        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        if ( nonce_subject.has_error() )
        {
            m_logger->error( "{}: Received unexpected subject payload", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        const std::string tx_hash = nonce_subject.value().tx_hash();
        const auto        key     = GetTransactionPath( tx_hash );

        // DESER-01: Deserialize from EmbeddedTransaction oneof field
        if ( nonce_subject.value().transaction().transaction_case() == EmbeddedTransaction::TRANSACTION_NOT_SET )
        {
            m_logger->error( "{}: No embedded transaction set, rejecting", __func__ );
            return ConsensusManager::ValidationResult::Reject();
        }

        auto tx_result = DeSerializeEmbeddedTransaction( nonce_subject.value().transaction() );
        if ( tx_result.has_error() )
        {
            m_logger->error( "{}: Failed to deserialize embedded tx for hash {}", __func__, tx_hash );
            return ConsensusManager::ValidationResult::Reject();
        }
        auto tx = tx_result.value();

        // Hash binding verification — cryptographic integrity gate (defense-in-depth)
        if ( tx->GetHash() != tx_hash )
        {
            m_logger->error( "{}: Hash binding mismatch, tx->GetHash() != subject.tx_hash for {}", __func__, tx_hash );
            return ConsensusManager::ValidationResult::Reject();
        }

        // BIND-01: Commitment-tx binding cross-check
        if ( nonce_subject.value().has_utxo_commitment() )
        {
            if ( !tx->HasUTXOParameters() )
            {
                m_logger->error( "{}: Subject has UTXO commitment but deserialized tx lacks "
                                 "UTXO parameters — possible malicious embedding, rejecting tx={}",
                                 __func__,
                                 tx_hash );
                return ConsensusManager::ValidationResult::Reject();
            }

            auto reconstructed = BuildUTXOTransitionCommitment( tx );
            if ( !reconstructed.has_value() ||
                 reconstructed->consumed_outpoints_root() !=
                     nonce_subject.value().utxo_commitment().consumed_outpoints_root() ||
                 reconstructed->produced_outputs_root() !=
                     nonce_subject.value().utxo_commitment().produced_outputs_root() )
            {
                m_logger->error( "{}: Commitment-tx binding mismatch — "
                                 "reconstructed commitment differs from subject claim for tx={}",
                                 __func__,
                                 tx_hash );
                return ConsensusManager::ValidationResult::Reject();
            }
        }

        // TRACK-01: Insert temporary tracking entry via ChangeTransactionState lifecycle
        uint64_t          tracked_nonce  = tx->GetNonce();
        TransactionStatus tracked_status = TransactionStatus::VERIFYING;
        {
            std::unique_lock tx_lock( tx_mutex_m );
            auto             it = tx_processed_m.find( key );
            if ( it == tx_processed_m.end() )
            {
                tx_lock.unlock();
                // Proper state machine: CREATED → VERIFYING (no direct tx_processed_m manipulation)
                auto create_result = ChangeTransactionState( tx, TransactionStatus::CREATED );
                if ( create_result.has_error() )
                {
                    m_logger->warn( "{}: CREATE failed for embedded tx {}, entry may exist via race: {}",
                                    __func__,
                                    tx_hash,
                                    create_result.error().message() );
                    // Re-read in case another thread inserted it
                    std::unique_lock tx_lock2( tx_mutex_m );
                    auto             it2 = tx_processed_m.find( key );
                    if ( it2 != tx_processed_m.end() )
                    {
                        if ( it2->second.status == TransactionStatus::FAILED )
                        {
                            return ConsensusManager::ValidationResult::Reject();
                        }
                        tracked_status = it2->second.status;
                        tracked_nonce  = it2->second.cached_nonce;
                    }
                }
                else
                {
                    ChangeTransactionState( tx, TransactionStatus::VERIFYING );
                }
            }
            else if ( it->second.status == TransactionStatus::FAILED )
            {
                m_logger->debug( "{}: Transaction {} previously FAILED, rejecting", __func__, tx_hash );
                return ConsensusManager::ValidationResult::Reject();
            }
            else
            {
                // Entry already exists with higher-status — use its values for downstream checks
                tracked_status = it->second.status;
                tracked_nonce  = it->second.cached_nonce;
            }
        }

        if ( !tx )
        {
            m_logger->error( "{}: Tracked transaction missing for hash {}", __func__, tx_hash );
            return outcome::failure( std::errc::invalid_argument );
        }

        auto reject_and_maybe_fail_local = [&]( const char *reason ) -> ConsensusManager::ValidationResult
        {
            // METRICS-01: Validation reject counter with reason logged at info level
            metrics_validation_reject_.fetch_add( 1, std::memory_order_relaxed );
            m_logger->info( "{}: Proposal rejected for hash {}: {}", __func__, tx_hash, reason );

            m_logger->error( "{}: Rejecting nonce subject for hash {}: {}", __func__, tx_hash, reason );

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
                        m_logger->error( "{}: Failed to mark rejected local tx as FAILED for hash {}: {}",
                                         __func__,
                                         tx_hash,
                                         fail_result.error().message() );
                    }
                }
            }
            else
            {
                // TRACK-01 per D-02: Mark remote embedded temp entry as FAILED via ChangeTransactionState
                {
                    std::unique_lock tx_lock( tx_mutex_m );
                    auto             it = tx_processed_m.find( GetTransactionPath( tx_hash ) );
                    if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::VERIFYING )
                    {
                        tx_lock.unlock();
                        ChangeTransactionState( tx, TransactionStatus::FAILED );
                        m_logger->debug( "{}: Marked rejected embedded tx as FAILED for {}", __func__, tx_hash );
                    }
                }
            }

            return ConsensusManager::ValidationResult::Reject();
        };

        if ( tracked_nonce != nonce_subject.value().nonce() )
        {
            m_logger->error( "{}: Nonce mismatch for hash {}", __func__, tx_hash );
            return reject_and_maybe_fail_local( "nonce mismatch" );
        }

        if ( !subject.account_id().empty() && tx->GetSrcAddress() != subject.account_id() )
        {
            m_logger->error( "{}: Account mismatch for hash {}", __func__, tx_hash );
            return reject_and_maybe_fail_local( "account mismatch" );
        }

        if ( tracked_status == TransactionStatus::FAILED )
        {
            m_logger->error( "{}: Transaction status invalid for hash {}", __func__, tx_hash );
            return reject_and_maybe_fail_local( "transaction already failed" );
        }

        if ( HasConfirmedInputConflict( tx ) )
        {
            m_logger->error( "{}: Outpoint conflict against finalized transaction for hash {}", __func__, tx_hash );
            return reject_and_maybe_fail_local( "input outpoint already finalized by another transaction" );
        }

        const auto witness_validation = ValidateWitnessForConsensus( subject, tx );
        if ( witness_validation == WitnessValidationResult::INVALID )
        {
            m_logger->error( "{}: Witness validation failed for hash {}", __func__, tx_hash );
            return reject_and_maybe_fail_local( "witness validation failed" );
        }

        if ( auto migration_tx = std::dynamic_pointer_cast<MigrationTransaction>( tx ) )
        {
            MigrationAllowList allow_list( globaldb_m->GetDataStore(), migration_tx->GetFromVersion() );
            auto eligibility_result = allow_list.IsEligible( migration_tx->GetSrcAddress(), migration_tx->GetAmount() );
            if ( eligibility_result.has_error() )
            {
                m_logger->warn( "{}: Failed to evaluate local migration allowlist tx={} src={} err={}, pending",
                                __func__,
                                tx_hash,
                                migration_tx->GetSrcAddress(),
                                eligibility_result.error().message() );
                return ConsensusManager::ValidationResult::Pending();
            }
            if ( !eligibility_result.value() )
            {
                return reject_and_maybe_fail_local( "migration source address not locally eligible" );
            }
        }

        auto validate_result = ValidateTransactionForConsensus( tx );

        if ( validate_result.check == ConsensusManager::Check::Pending )
        {
            return validate_result;
        }
        if ( validate_result.check != ConsensusManager::Check::Approve )
        {
            return reject_and_maybe_fail_local( "transaction validation failed" );
        }

        // METRICS-01: Validation approve counter
        metrics_validation_approve_.fetch_add( 1, std::memory_order_relaxed );
        return ConsensusManager::ValidationResult::Approve();
    }

    bool TransactionManager::ValidateUTXOParametersForConsensus( const UTXOTxParameters &params,
                                                                 const std::string      &address ) const
    {
        m_logger->debug( "{}: Validating UTXO params for address {}", __func__, address );
        if ( params.first.empty() || params.second.empty() )
        {
            m_logger->error( "{}: Empty inputs or outputs", __func__ );
            return false;
        }

        if ( !account_m->GetUTXOManager().VerifyParameters( params, address ) )
        {
            m_logger->error( "{}: VerifyParameters failed for address {}", __func__, address );
            return false;
        }

        m_logger->debug( "{}: UTXO params valid for address {}", __func__, address );
        return true;
    }

    ConsensusManager::ValidationResult TransactionManager::ValidateTransactionForConsensus(
        const std::shared_ptr<GeniusTransaction> &tx ) const
    {
        m_logger->debug( "{}: Validating transaction", __func__ );
        if ( !tx )
        {
            m_logger->error( "{}: Null transaction", __func__ );
            return ConsensusManager::ValidationResult::Reject();
        }

        if ( !CheckTransactionWellFormed( *tx ) )
        {
            m_logger->error( "{}: Well-formed check failed tx={}", __func__, tx->GetHash() );
            return ConsensusManager::ValidationResult::Reject();
        }
        if ( !CheckTransactionAuthorization( *tx ) )
        {
            m_logger->error( "{}: Authorization check failed tx={}", __func__, tx->GetHash() );
            return ConsensusManager::ValidationResult::Reject();
        }
        if ( !CheckTransactionTimestamp( *tx ) )
        {
            m_logger->error( "{}: Timestamp check failed tx={}", __func__, tx->GetHash() );
            return ConsensusManager::ValidationResult::Reject();
        }
        auto replay_result = EvaluateTransactionReplayProtection( *tx );
        if ( replay_result.validation.check != ConsensusManager::Check::Approve )
        {
            m_logger->error( "{}: Replay protection failed tx={}", __func__, tx->GetHash() );
            return replay_result.validation;
        }
        //TODO - Deal with checking the Mint
        if ( !CheckTransactionTypeRules( tx ) )
        {
            m_logger->error( "{}: Type rules failed tx={}", __func__, tx->GetHash() );
            return ConsensusManager::ValidationResult::Reject();
        }

        m_logger->debug( "{}: Transaction valid tx={}", __func__, tx->GetHash() );
        return ConsensusManager::ValidationResult::Approve();
    }

    bool TransactionManager::CheckTransactionWellFormed( const GeniusTransaction &tx ) const
    {
        m_logger->debug( "{}: Checking well-formed tx={}", __func__, tx.GetHash() );
        if ( tx.GetHash().empty() || !tx.CheckHash() )
        {
            m_logger->error( "{}: Hash invalid tx={}", __func__, tx.GetHash() );
            return false;
        }

        if ( tx.GetSrcAddress().empty() )
        {
            m_logger->error( "{}: Empty source address tx={}", __func__, tx.GetHash() );
            return false;
        }

        if ( tx.GetTimestamp() == 0 )
        {
            m_logger->error( "{}: Missing timestamp tx={}", __func__, tx.GetHash() );
            return false;
        }

        if ( transaction_parsers.find( tx.GetType() ) == transaction_parsers.end() )
        {
            m_logger->error( "{}: Unknown tx type {}", __func__, tx.GetType() );
            return false;
        }

        m_logger->debug( "{}: Well-formed ok tx={}", __func__, tx.GetHash() );
        return true;
    }

    bool TransactionManager::CheckTransactionAuthorization( const GeniusTransaction &tx ) const
    {
        m_logger->debug( "{}: Checking authorization tx={}", __func__, tx.GetHash() );
        if ( tx.CheckSignature() || tx.CheckDAGSignatureLegacy() )
        {
            m_logger->debug( "{}: Authorization ok tx={}", __func__, tx.GetHash() );
            return true;
        }
        m_logger->error( "{}: Authorization failed tx={}", __func__, tx.GetHash() );
        return false;
    }

    bool TransactionManager::CheckTransactionTimestamp( const GeniusTransaction &tx ) const
    {
        m_logger->debug( "{}: Checking timestamp tx={}", __func__, tx.GetHash() );
        const auto ts = tx.GetTimestamp();
        if ( ts == 0 )
        {
            m_logger->error( "{}: Missing timestamp tx={}", __func__, tx.GetHash() );
            return false;
        }

        const auto elapsed      = GetElapsedTime( ts );
        const auto tolerance_ms = static_cast<int64_t>( timestamp_tolerance_m.count() );
        const auto drift_ms     = elapsed >= 0 ? elapsed : -elapsed;

        if ( tolerance_ms > 0 && drift_ms > tolerance_ms )
        {
            m_logger->error( "{}: Timestamp out of tolerance tx={} (elapsed: {} ms, tolerance: {} ms)",
                             __func__,
                             tx.GetHash(),
                             elapsed,
                             tolerance_ms );
            return false;
        }

        m_logger->debug( "{}: Timestamp ok tx={}", __func__, tx.GetHash() );
        return true;
    }

    bool TransactionManager::CheckTransactionReplayProtection( const GeniusTransaction &tx ) const
    {
        return EvaluateTransactionReplayProtection( tx ).validation.check == ConsensusManager::Check::Approve;
    }

    TransactionManager::ReplayProtectionResult TransactionManager::EvaluateTransactionReplayProtection(
        const GeniusTransaction &tx ) const
    {
        m_logger->debug( "{}: Checking replay protection tx={}", __func__, tx.GetHash() );

        if ( tx.GetNonce() > 0 )
        {
            const auto previous_hash = tx.GetPreviousHash();
            if ( previous_hash.empty() )
            {
                m_logger->error( "{}: Missing previous hash tx={}", __func__, tx.GetHash() );
                return { ConsensusManager::ValidationResult::Reject() };
            }
            if ( tx.GetSrcAddress() == account_m->GetAddress() )
            {
                const auto expected_previous_hash = GetOutgoingPreviousHash( tx.GetNonce() );
                if ( !expected_previous_hash.empty() && previous_hash != expected_previous_hash )
                {
                    m_logger->error( "{}: Previous hash does not match local account head tx={}",
                                     __func__,
                                     tx.GetHash() );
                    return { ConsensusManager::ValidationResult::Reject() };
                }
            }
            auto previous_cert_result = blockchain_->GetCertificateBySubjectHash( previous_hash );
            if ( previous_cert_result.has_error() )
            {
                m_logger->error( "{}: Missing previous certificate for hash {}", __func__, previous_hash );
                return { ConsensusManager::ValidationResult::Pending(
                    { ConsensusManager::PendingDependencyKey::Certificate( previous_hash ) } ) };
            }
            const auto &previous_subject = previous_cert_result.value().proposal().subject();
            auto        previous_nonce   = ConsensusManager::DecodeNonceSubject( previous_subject );
            if ( previous_nonce.has_error() )
            {
                return { ConsensusManager::ValidationResult::Reject() };
            }
            if ( previous_subject.account_id() != tx.GetSrcAddress() )
            {
                return { ConsensusManager::ValidationResult::Reject() };
            }
            if ( ( previous_nonce.value().nonce() + 1 ) != tx.GetNonce() )
            {
                return { ConsensusManager::ValidationResult::Reject() };
            }
        }

        auto nonce_result = account_m->GetPeerNonce( tx.GetSrcAddress() );
        if ( nonce_result.has_error() )
        {
            m_logger->debug( "{}: No confirmed nonce for address {}", __func__, tx.GetSrcAddress() );
            return { ConsensusManager::ValidationResult::Approve() };
        }

        const auto confirmed_nonce = nonce_result.value();
        const auto tx_nonce        = tx.GetNonce();

        if ( tx_nonce <= confirmed_nonce )
        {
            m_logger->error( "{}: Nonce too low tx={} nonce={} confirmed={}",
                             __func__,
                             tx.GetHash(),
                             tx_nonce,
                             confirmed_nonce );
            return { ConsensusManager::ValidationResult::Reject() };
        }

        if ( tx_nonce > confirmed_nonce + nonce_window_m )
        {
            m_logger->error( "{}: Nonce too high tx={} nonce={} confirmed={} window={}",
                             __func__,
                             tx.GetHash(),
                             tx_nonce,
                             confirmed_nonce,
                             nonce_window_m );
            return { ConsensusManager::ValidationResult::Reject() };
        }

        if ( tx_nonce > confirmed_nonce + 1 )
        {
            for ( uint64_t n = confirmed_nonce + 1; n < tx_nonce; ++n )
            {
                auto tracked = GetTrackedTxByNonceAndAddress( n, tx.GetSrcAddress() );
                if ( !tracked.has_value() )
                {
                    m_logger->error( "{}: Missing intermediate nonce {} for address {}",
                                     __func__,
                                     n,
                                     tx.GetSrcAddress() );
                    return { ConsensusManager::ValidationResult::Reject() };
                }
                if ( tracked->status == TransactionStatus::FAILED )
                {
                    m_logger->error( "{}: Intermediate nonce {} invalid for address {}",
                                     __func__,
                                     n,
                                     tx.GetSrcAddress() );
                    return { ConsensusManager::ValidationResult::Reject() };
                }
            }
        }
        m_logger->debug( "{}: Replay protection ok tx={}", __func__, tx.GetHash() );
        return { ConsensusManager::ValidationResult::Approve() };
    }

    bool TransactionManager::CheckTransactionTypeRules( const std::shared_ptr<GeniusTransaction> &tx ) const
    {
        m_logger->debug( "{}: Checking type rules", __func__ );
        if ( !tx )
        {
            m_logger->error( "{}: Null transaction", __func__ );
            return false;
        }

        if ( tx->HasUTXOParameters() )
        {
            auto params_opt = tx->GetUTXOParametersOpt();
            if ( !params_opt.has_value() )
            {
                m_logger->error( "{}: Missing UTXO parameters for tx={}", __func__, tx->GetHash() );
                return false;
            }
            const auto &[_, validator] = SelectInputValidator( tx );
            return validator.ValidateUTXOParameters( params_opt.value(),
                                                     tx->GetSrcAddress(),
                                                     account_m->GetUTXOManager() );
        }

        return true;
    }

    TransactionManager::WitnessValidationResult TransactionManager::ValidateWitnessForConsensus(
        const ConsensusSubject                   &subject,
        const std::shared_ptr<GeniusTransaction> &tx ) const
    {
        if ( !tx )
        {
            m_logger->error( "{}: Null transaction", __func__ );
            return WitnessValidationResult::INVALID;
        }

        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        m_logger->debug( "{}: Start tx={} src={} nonce={} subject_nonce={} has_nonce={} "
                         "has_utxo_params={} has_commitment={} has_witness={}",
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
            m_logger->debug( "{}: Subject has no nonce payload, accepting tx={}", __func__, tx->GetHash() );
            return WitnessValidationResult::VALID;
        }

        const auto [chain_id, validator] = SelectInputValidator( tx );

        if ( !tx->HasUTXOParameters() )
        {
            // BIND-01: Hardened early-return — if subject claims UTXO commitment
            // but tx lacks UTXO params, this is Pitfall 5 bypass → reject as INVALID
            if ( nonce_subject.has_value() && nonce_subject.value().has_utxo_commitment() )
            {
                m_logger->error( "{}: Subject has UTXO commitment "
                                 "but tx has no UTXO params — rejecting tx={}",
                                 __func__,
                                 tx->GetHash() );
                return WitnessValidationResult::INVALID;
            }
            m_logger->debug( "{}: Tx has no UTXO params, accepting tx={}", __func__, tx->GetHash() );
            return WitnessValidationResult::VALID;
        }

        if ( !nonce_subject.value().has_utxo_commitment() )
        {
            m_logger->error( "{}: Missing UTXO commitment tx={}", __func__, tx->GetHash() );
            return WitnessValidationResult::INVALID;
        }

        const auto &commitment = nonce_subject.value().utxo_commitment();
        if ( commitment.consumed_outpoints_root().size() != base::Hash256::size() ||
             commitment.produced_outputs_root().size() != base::Hash256::size() )
        {
            m_logger->error( "{}: Invalid commitment root sizes tx={} consumed_size={} "
                             "produced_size={} expected={}",
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
            m_logger->error( "{}: Failed to parse commitment consumed root tx={}", __func__, tx->GetHash() );
            return WitnessValidationResult::INVALID;
        }

        if ( validator.RequiresConsensusUTXOData() && !nonce_subject.value().has_utxo_witness() )
        {
            m_logger->error( "{}: Missing required UTXO witness tx={} chain_id={} validator_requires_witness={}",
                             __func__,
                             tx->GetHash(),
                             chain_id,
                             validator.RequiresConsensusUTXOData() );
            return WitnessValidationResult::INVALID;
        }

        auto params_opt = tx->GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            m_logger->error( "{}: Missing UTXO params payload tx={}", __func__, tx->GetHash() );
            return WitnessValidationResult::INVALID;
        }
        (void) consumed_root_result;
        const bool witness_ok = validator.ValidateWitness( subject, tx, params_opt.value(), blockchain_ );
        m_logger->debug( "{}: Validator witness result tx={} chain_id={} result={}",
                         __func__,
                         tx->GetHash(),
                         chain_id,
                         witness_ok );
        return witness_ok ? WitnessValidationResult::VALID : WitnessValidationResult::INVALID;
    }

    std::optional<UTXOTransitionCommitment> TransactionManager::BuildUTXOTransitionCommitment(
        const std::shared_ptr<GeniusTransaction> &tx ) const
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
            m_logger->warn( "{}: Could not extract produced outputs for tx={}", __func__, tx->GetHash() );
            return std::nullopt;
        }
        for ( size_t i = 0; i < produced_outputs.size(); ++i )
        {
            const auto &produced_output  = produced_outputs[i];
            const auto  produced_tx_hash = produced_output.GetTxID();
            auto       *committed_output = commitment.add_produced_outputs();
            committed_output->set_tx_id_hash( produced_tx_hash.data(), produced_tx_hash.size() );
            committed_output->set_output_index( produced_output.GetOutputIdx() );
            committed_output->set_owner_address( produced_output.GetOwnerAddress() );
            const auto token_bytes = produced_output.GetTokenID().bytes();
            committed_output->set_token_id( token_bytes.data(), token_bytes.size() );
            committed_output->set_amount( produced_output.GetAmount() );
        }

        const auto produced_outputs_root = utxo_merkle::ComputeMerkleRootFromUTXOs( produced_outputs );
        commitment.set_consumed_outpoints_root( consumed_outpoints_root.data(), consumed_outpoints_root.size() );
        commitment.set_produced_outputs_root( produced_outputs_root.data(), produced_outputs_root.size() );
        return commitment;
    }

    std::optional<UTXOWitness> TransactionManager::BuildUTXOWitness(
        const std::shared_ptr<GeniusTransaction> &tx ) const
    {
        if ( !tx )
        {
            m_logger->error( "{}: Missing transaction", __func__ );
            return std::nullopt;
        }

        if ( !tx->HasUTXOParameters() )
        {
            m_logger->error( "{}: No UTXO parameters for transaction {}", __func__, tx->GetHash() );
            return std::nullopt;
        }

        auto params_opt = tx->GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            m_logger->error( "{}: Unexpected missing UTXO parameters for transaction {}", __func__, tx->GetHash() );
            return std::nullopt;
        }
        const auto &inputs = params_opt->first;

        struct SnapshotLeaf
        {
            std::string          outpoint_key;
            std::vector<uint8_t> payload;
        };

        std::vector<SnapshotLeaf> leaves;
        leaves.reserve( inputs.size() );
        for ( const auto &input : inputs )
        {
            auto utxo = account_m->GetUTXOManager().GetUnconsumedUTXO( input.txid_hash_, input.output_idx_ );
            if ( !utxo.has_value() )
            {
                m_logger->error( "{}: Missing input UTXO for transaction {} and key {}",
                                 __func__,
                                 tx->GetHash(),
                                 OutPointKey( input.txid_hash_, input.output_idx_ ) );
                return std::nullopt;
            }
            leaves.push_back(
                { OutPointKey( utxo->GetTxID(), utxo->GetOutputIdx() ), SerializeUTXOLeafPayload( utxo.value() ) } );
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
                m_logger->error( "{}: Missing outpoint for transaction {} and key {}", __func__, tx->GetHash(), key );
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
                m_logger->error( "{}: Missing producer transaction for input {}",
                                 __func__,
                                 input.txid_hash_.toReadableString() );
                return std::nullopt;
            }
            std::vector<GeniusUTXO> produced_outputs;
            if ( !ExtractProducedUTXOs( *producer_tx, produced_outputs ) )
            {
                m_logger->error( "{}: Could not extract produced outputs for producer transaction {}",
                                 __func__,
                                 producer_tx->GetHash() );
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
                m_logger->error( "{}: Missing produced UTXO for transaction {} and key {}",
                                 __func__,
                                 tx->GetHash(),
                                 key );
                return std::nullopt;
            }
            if ( produced_leaves[produced_it->second].payload != leaves[leaf_index].payload )
            {
                m_logger->error( "{}: Payload mismatch for produced UTXO for transaction {} and key {}",
                                 __func__,
                                 tx->GetHash(),
                                 key );
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

    bool TransactionManager::ApplyTransactionToUTXOSnapshot( const std::shared_ptr<GeniusTransaction> &tx,
                                                             std::vector<GeniusUTXO>                  &snapshot ) const
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
        if ( !tx->HasUTXOParameters() )
        {
            return false;
        }

        auto params_opt = tx->GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            return false;
        }
        const auto &inputs = params_opt->first;

        std::vector<GeniusUTXO> produced_outputs;
        if ( !ExtractProducedUTXOs( *tx, produced_outputs ) )
        {
            return false;
        }
        remove_inputs( inputs );
        for ( const auto &output : produced_outputs )
        {
            if ( output.GetOwnerAddress() == tx->GetSrcAddress() )
            {
                snapshot.push_back( output );
            }
        }
        return true;
    }

    void TransactionManager::SetNonceWindow( uint64_t window )
    {
        if ( window == 0 )
        {
            m_logger->warn( "{}: Nonce window 0, using default {}", __func__, DEFAULT_NONCE_WINDOW );
            nonce_window_m = DEFAULT_NONCE_WINDOW;
            return;
        }
        m_logger->info( "{}: Setting nonce window to {}", __func__, window );
        nonce_window_m = window;
    }

    outcome::result<void> TransactionManager::ChangeTransactionState( const std::shared_ptr<GeniusTransaction> &tx,
                                                                      TransactionStatus new_status )
    {
        static constexpr std::string_view FUNC = __func__;
        m_logger->debug( "{}: Changing transaction state to {} for transaction {}",
                         FUNC,
                         static_cast<int>( new_status ),
                         tx->GetHash() );
        const auto key = GetTransactionPath( *tx );
        switch ( new_status )
        {
            case TransactionStatus::CREATED:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                auto             it = tx_processed_m.find( key );
                if ( it != tx_processed_m.end() )
                {
                    m_logger->error( "{}: Trying to CREATE a transaction that already exists {}", FUNC, tx->GetHash() );
                    return outcome::failure( std::errc::file_exists );
                }
                m_logger->debug( "{}: Set status of CREATE to transaction {}", FUNC, tx->GetHash() );
                tx_processed_m.emplace( key, TrackedTx{ tx, TransactionStatus::CREATED, tx->GetNonce() } );
                // METRICS-01: Tracking insert — temp entry created in tx_processed_m
                metrics_tracking_insert_.fetch_add( 1, std::memory_order_relaxed );
                m_logger->info( "{}: Temp tracking entry created tx={}", FUNC, tx->GetHash() );
            }
            break;
            case TransactionStatus::SENDING:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                auto             it = tx_processed_m.find( key );
                if ( it == tx_processed_m.end() )
                {
                    m_logger->error( "{}: Trying to SEND a transaction that doesn't exist {}", FUNC, tx->GetHash() );
                    return outcome::failure( std::errc::no_such_file_or_directory );
                }
                if ( it->second.status != TransactionStatus::CREATED )
                {
                    m_logger->error( "{}: Trying to SEND a transaction that is not in CREATED status {}",
                                     FUNC,
                                     tx->GetHash() );
                    return outcome::failure( std::errc::invalid_argument );
                }
                it->second.status = TransactionStatus::SENDING;
                m_logger->debug( "{}: Set status of SENDING to transaction {}", FUNC, tx->GetHash() );
            }
            break;
            case TransactionStatus::VERIFYING:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                auto             it = tx_processed_m.find( key );

                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::VERIFYING )
                {
                    m_logger->error( "{}: Trying to VERIFY a transaction that is already in VERIFY {}",
                                     FUNC,
                                     tx->GetHash() );
                    break;
                }
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::CONFIRMED )
                {
                    m_logger->warn( "{}: Unconfirming transaction {} and verifying it again", FUNC, tx->GetHash() );
                    BOOST_OUTCOME_TRY( RevertTransaction( tx ) );

                    BOOST_OUTCOME_TRY( DeleteTransaction( key, tx->GetTopics() ) );

                    account_m->RollBackPeerConfirmedNonce( it->second.cached_nonce, tx->GetSrcAddress() );
                }
                tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::VERIFYING, tx->GetNonce() };
                m_logger->debug( "{}: Set status of VERIFYING to transaction {}", FUNC, tx->GetHash() );
                m_logger->debug( "{}: Attempting to resume the proposal handling to transaction {}",
                                 FUNC,
                                 tx->GetHash() );
                tx_lock.unlock();
                BOOST_OUTCOME_TRY( blockchain_->TryResumeProposal( tx->GetHash() ) );
                m_logger->debug( "{}: Resumed the proposal handling to transaction {}", FUNC, tx->GetHash() );
            }

            break;
            case TransactionStatus::CONFIRMED:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                auto             it = tx_processed_m.find( key );
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::CONFIRMED )
                {
                    m_logger->error( "{}: Trying to CONFIRM a transaction that is already CONFIRMED {}",
                                     FUNC,
                                     tx->GetHash() );
                    break;
                }
                tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::CONFIRMED, tx->GetNonce() };

                // Clear bridge mint reservation and persist executed state
                if ( tx->GetType() == "mint-v2" )
                {
                    auto mint_tx = std::dynamic_pointer_cast<MintTransactionV2>( tx );
                    if ( mint_tx )
                    {
                        const std::string reservation_key = mint_tx->GetChainId() + std::string( kBridgeKeySeparator ) +
                                                            tx->dag_st.uncle_hash();
                        // Persist executed state to RocksDB — survives restart
                        auto datastore = globaldb_m ? globaldb_m->GetDataStore() : nullptr;
                        if ( datastore )
                        {
                            crdt::GlobalDB::Buffer key_buffer;
                            key_buffer.put( std::string( kBridgeExecutedPrefix ) + reservation_key );
                            crdt::GlobalDB::Buffer value_buffer;
                            value_buffer.put( "1" );
                            auto put_result = datastore->put( key_buffer, value_buffer );
                            if ( put_result.has_error() )
                            {
                                m_logger->error( "{}: Failed to persist executed bridge mint for {}",
                                                 FUNC,
                                                 reservation_key );
                            }
                        }
                    }
                }

                // METRICS-01: Tracking confirm — entry promoted to CONFIRMED
                metrics_tracking_confirm_.fetch_add( 1, std::memory_order_relaxed );
                m_logger->info( "{}: Tracking entry confirmed tx={}", FUNC, tx->GetHash() );

                m_logger->debug( "{}: Set status of CONFIRMED to transaction {}", FUNC, tx->GetHash() );
                auto parse_result = ParseTransaction( tx );
                if ( parse_result.has_error() )
                {
                    // The tracked state was already promoted. Wake observers even when
                    // applying its account-side effects fails.
                    tx_lock.unlock();
                    NotifyTransactionStatusChanged( tx->GetHash() );
                    return outcome::failure( parse_result.error() );
                }
                account_m->SetPeerConfirmedNonce( tx->GetNonce(), tx->GetSrcAddress(), tx->GetHash() );
                {
                    std::lock_guard missing_lock( missing_tx_mutex_ );
                    missing_tx_hashes_.erase( tx->GetHash() );
                }
            }

            break;
            case TransactionStatus::UNCONFIRMED:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                const auto       key = GetTransactionPath( *tx );
                auto             it  = tx_processed_m.find( key );
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::CONFIRMED )
                {
                    m_logger->debug( "{}: Keeping CONFIRMED transaction from becoming UNCONFIRMED {}",
                                     FUNC,
                                     tx->GetHash() );
                    break;
                }
                tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::UNCONFIRMED, tx->GetNonce() };
                if ( tx->GetSrcAddress() == account_m->GetAddress() )
                {
                    account_m->ReleaseNonce( tx->GetNonce() );
                }
                m_logger->info( "{}: Tracking entry unconfirmed after inconclusive expiry tx={}", FUNC, tx->GetHash() );
            }

            break;
            case TransactionStatus::INVALID:
            case TransactionStatus::FAILED:
            {
                std::unique_lock tx_lock( tx_mutex_m );
                auto             it = tx_processed_m.find( key );
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::FAILED )
                {
                    m_logger->error( "{}: Trying to FAIL a transaction that is already FAILED {}",
                                     FUNC,
                                     tx->GetHash() );
                    break;
                }
                if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::CONFIRMED )
                {
                    m_logger->debug( "{}: Unconfirming transaction {}", FUNC, tx->GetHash() );
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
                        if ( tx->GetType() == "mint-v2" )
                        {
                            account_m->GetUTXOManager().RollbackUTXOs( params_opt->first,
                                                                       tx->dag_st.uncle_hash(),
                                                                       UTXOManager::UTXOType::UTXO_BRIDGE );
                        }
                        else
                        {
                            account_m->GetUTXOManager().RollbackUTXOs( params_opt->first, tx->GetHash() );
                        }
                    }
                }
                tx_processed_m[key] = TrackedTx{ tx, TransactionStatus::FAILED, tx->GetNonce() };

                // Clear bridge mint reservation on failure
                if ( tx->GetType() == "mint-v2" )
                {
                    auto mint_tx = std::dynamic_pointer_cast<MintTransactionV2>( tx );
                    // UTXO consumed automatically via ParseMintTransactionV2's ConsumeUTXOs
                    (void) mint_tx;
                }

                // METRICS-01: Tracking fail — entry transitioned to FAILED
                metrics_tracking_fail_.fetch_add( 1, std::memory_order_relaxed );
                m_logger->info( "{}: Tracking entry failed tx={}", FUNC, tx->GetHash() );

                account_m->ReleaseNonce( tx->GetNonce() );

                m_logger->debug( "{}: Set status of FAILED to transaction {}", FUNC, tx->GetHash() );
                {
                    std::lock_guard missing_lock( missing_tx_mutex_ );
                    missing_tx_hashes_.erase( tx->GetHash() );
                }
            }

            break;
            default:
                m_logger->error( "{}: Invalid transaction status {} for transaction {}",
                                 FUNC,
                                 static_cast<int>( new_status ),
                                 tx->GetHash() );
                return outcome::failure( std::errc::invalid_argument );
        }

        // Notify after every lock local to the transition has been released. Querying
        // the tracked value also avoids reporting a requested transition that was rejected.
        m_logger->debug( "{}: Transaction {} state changed to {}",
                         FUNC,
                         tx->GetHash(),
                         static_cast<int>( new_status ) );
        NotifyTransactionStatusChanged( tx->GetHash() );
        return outcome::success();
    }

    bool TransactionManager::KeyExistsInDB( const std::string &key ) const
    {
        auto existing_data_result = globaldb_m->Get( key );
        if ( !existing_data_result.has_value() )
        {
            return false;
        }
        auto result = DeSerializeTransaction( existing_data_result.value() );
        return !result.has_error();
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
