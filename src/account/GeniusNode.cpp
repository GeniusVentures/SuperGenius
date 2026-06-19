/**
 * @file       GeniusNode.cpp
 * @brief
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include <chrono>
#include <stdexcept>
#include <thread>
#include <memory>
#include <exception>
#include <random>
#include <filesystem>
#include <set>

#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/uuid/uuid.hpp>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/dll.hpp>
#include <boost/json.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <WalletCore/HDWallet.h>
#include <WalletCore/Coin.h>

#include "account/GeniusAccount.hpp"
#include "base/sgns_version.hpp"
#include "account/TokenAmount.hpp"
#include "account/GeniusNode.hpp"
#include "account/ChainRpcEndpointProvider.hpp"
#include "account/MigrationManager.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "upnp.hpp"
#include "processing/processing_tasksplit.hpp"
#include <eth/abi_decoder.hpp>
#include <base/parse_utility.hpp>
#include <eth/rpc_http_transport.hpp>
#include <eth/json_rpc.hpp>
#include <eth/event_filter.hpp>
#include <eth/eth_watch_cli.hpp>     // event_registry().params_for() — Bridge V2 ABI decode (D-13)
#include <eth/secp256k1_utility.hpp> // DecompressXOnlyPubkey() — X-only key decompression (D-13)
#include "processing/processing_subtask_enqueuer_impl.hpp"
#include "processing/impl/TaskQueueImpl.hpp"
#include "outcome/outcome.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include <Generators.hpp>

namespace
{
    uint16_t GenerateRandomPort( uint16_t base, const std::string &seed )
    {
        uint32_t seed_hash = 0;
        for ( char c : seed )
        {
            seed_hash = seed_hash * 31 + static_cast<uint8_t>( c );
        }

        std::mt19937                            rng( seed_hash );
        std::uniform_int_distribution<uint16_t> dist( 0, 300 );

        return base + dist( rng );
    }

    const char *NodeStateToString( sgns::GeniusNode::NodeState state )
    {
        using State = sgns::GeniusNode::NodeState;
        switch ( state )
        {
            case State::CREATING:
                return "CREATING";
            case State::MIGRATING_DATABASE:
                return "MIGRATING_DATABASE";
            case State::INITIALIZING_DATABASE:
                return "INITIALIZING_DATABASE";
            case State::INITIALIZING_PROCESSING:
                return "INITIALIZING_PROCESSING";
            case State::INITIALIZING_BLOCKCHAIN:
                return "INITIALIZING_BLOCKCHAIN";
            case State::INITIALIZING_TRANSACTIONS:
                return "INITIALIZING_TRANSACTIONS";
            case State::READY:
                return "READY";
        }
        return "UNKNOWN";
    }
}

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, GeniusNode::Error, e )
{
    switch ( e )
    {
        case sgns::GeniusNode::Error::INSUFFICIENT_FUNDS:
            return "Insufficient funds for the transaction";
        case sgns::GeniusNode::Error::DATABASE_WRITE_ERROR:
            return "Error writing data into the database";
        case sgns::GeniusNode::Error::INVALID_TRANSACTION_HASH:
            return "Input transaction hash is invalid";
        case sgns::GeniusNode::Error::INVALID_CHAIN_ID:
            return "Chain ID is invalid";
        case sgns::GeniusNode::Error::INVALID_TOKEN_ID:
            return "Token ID is invalid";
        case sgns::GeniusNode::Error::TOKEN_ID_MISMATCH:
            return "Informed Token ID doesn't match initialized ID";
        case sgns::GeniusNode::Error::PROCESS_COST_ERROR:
            return "The calculated Processing cost was negative";
        case sgns::GeniusNode::Error::PROCESS_INFO_MISSING:
            return "Processing information missing on JSON file";
        case sgns::GeniusNode::Error::INVALID_JSON:
            return "Json cannot be parsed";
        case sgns::GeniusNode::Error::INVALID_BLOCK_PARAMETERS:
            return "Json missing block params";
        case sgns::GeniusNode::Error::NO_PROCESSOR:
            return "Json missing processor";
        case sgns::GeniusNode::Error::NO_PRICE:
            return "Could not get a price";
        case sgns::GeniusNode::Error::TRANSACTIONS_NOT_READY:
            return "Transaction manager is not ready";
        case sgns::GeniusNode::Error::TRANSACTION_NOT_FINALIZED:
            return "Requested transaction not finalized within timeout";
        case sgns::GeniusNode::Error::TRANSACTION_FAILED:
            return "Requested transaction failed";
    }
    return "Unknown error";
}

using namespace boost::multiprecision;

namespace sgns
{
    base::Logger GeniusNodeLogger()
    {
        return base::createLogger( "GeniusNode" );
    }

    std::shared_ptr<GeniusNode> GeniusNode::New( const DevConfig_st &dev_config,
                                                 bool                autodht,
                                                 bool                isprocessor,
                                                 uint16_t            base_port,
                                                 bool                is_full_node,
                                                 bool                use_upnp )
    {
        auto account = GeniusAccount::New( dev_config.TokenID, dev_config.BaseWritePath, is_full_node );
        if ( account == nullptr )
        {
            return nullptr;
        }
        auto instance = std::shared_ptr<GeniusNode>( new GeniusNode( dev_config,
                                                                     std::move( account ),
                                                                     autodht,
                                                                     isprocessor,
                                                                     base_port,
                                                                     is_full_node,
                                                                     use_upnp ) );

        if ( instance )
        {
            instance->BeginDBInitialization();
        }

        return instance;
    }

    std::shared_ptr<GeniusNode> GeniusNode::New( const DevConfig_st &dev_config,
                                                 const char         *eth_private_key,
                                                 bool                autodht,
                                                 bool                isprocessor,
                                                 uint16_t            base_port,
                                                 bool                is_full_node,
                                                 bool                use_upnp )
    {
        auto account = GeniusAccount::New( dev_config.TokenID,
                                           eth_private_key,
                                           dev_config.BaseWritePath,
                                           is_full_node );
        if ( account == nullptr )
        {
            return nullptr;
        }

        auto instance = std::shared_ptr<GeniusNode>( new GeniusNode( dev_config,
                                                                     std::move( account ),
                                                                     autodht,
                                                                     isprocessor,
                                                                     base_port,
                                                                     is_full_node,
                                                                     use_upnp ) );

        if ( instance )
        {
            instance->BeginDBInitialization();
        }

        return instance;
    }

    std::shared_ptr<GeniusNode> GeniusNode::NewFromMnemonic( const DevConfig_st &dev_config,
                                                             const std::string  &mnemonic,
                                                             bool                autodht,
                                                             bool                isprocessor,
                                                             uint16_t            base_port,
                                                             bool                is_full_node,
                                                             bool                use_upnp )
    {
        try
        {
            auto account = GeniusAccount::NewFromMnemonic( dev_config.TokenID,
                                                           mnemonic,
                                                           dev_config.BaseWritePath,
                                                           is_full_node );

            if ( account == nullptr )
            {
                return nullptr;
            }

            auto instance = std::shared_ptr<GeniusNode>( new GeniusNode( dev_config,
                                                                         std::move( account ),
                                                                         autodht,
                                                                         isprocessor,
                                                                         base_port,
                                                                         is_full_node,
                                                                         use_upnp ) );

            if ( instance )
            {
                instance->BeginDBInitialization();
            }

            return instance;
        }
        catch ( const std::invalid_argument &err )
        {
            std::cerr << "Failed to generate address from mnemonic: " << err.what() << '\n';
        }
        return nullptr;
    }

    GeniusNode::GeniusNode( const DevConfig_st            &dev_config,
                            std::shared_ptr<GeniusAccount> account,
                            bool                           autodht,
                            bool                           isprocessor,
                            uint16_t                       base_port,
                            bool                           is_full_node,
                            bool                           use_upnp ) :
        write_base_path_( dev_config.BaseWritePath ),
        account_( std::move( account ) ),
        io_( std::make_shared<boost::asio::io_context>() ),
        io_work_guard_( boost::asio::make_work_guard( *io_ ) ),
        autodht_( autodht ),
        isprocessor_( isprocessor ),
        is_full_node_( is_full_node ),
        dev_config_( dev_config ),
        processing_channel_topic_( std::string( PROCESSING_CHANNEL ) ),
        processing_grid_chanel_topic_( std::string( PROCESSING_GRID_CHANNEL ) ),
        m_lastApiCall( std::chrono::system_clock::now() - MIN_API_CALL_INTERVAL ),
        scheduler_( std::make_shared<libp2p::basic::SchedulerImpl>(
            std::make_shared<libp2p::basic::AsioSchedulerBackend>( io_ ),
            libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } ) ),
        generator_( std::make_shared<ipfs_lite::ipfs::graphsync::RequestIdGenerator>() ),
        processing_callback_pool_( std::make_unique<boost::asio::thread_pool>( 1 ) ),
        use_upnp_( use_upnp )
    {
        // Rotate log files before initializing logging system
        RotateLogFiles( write_base_path_ );
        InitOpenSSL();

        if ( !InitLoggers( write_base_path_ ) )
        {
            throw std::runtime_error( "Could not configure loggers" );
        }

        node_logger_->info( sgns::version::SuperGeniusVersionText() );

        if ( !InitNetwork( base_port, is_full_node_ ) )
        {
            throw std::runtime_error( "Network initialization error" );
        }
        node_logger_->debug( "Account Address {}", account_->GetAddress() );

        // Initializes the thread pool for IO context
        io_threads_.reserve( io_thread_count_ );
        for ( unsigned i = 0; i < io_thread_count_; ++i )
        {
            io_threads_.emplace_back( [ctx = io_] { ctx->run(); } );
        }

        if ( use_upnp_ )
        {
            RefreshUPNP( pubsubport_ );
        }
    }

    void GeniusNode::BeginDBInitialization()
    {
        StateTransition( NodeState::MIGRATING_DATABASE );
    }

    void GeniusNode::StateTransition( NodeState next_state )
    {
        state_.store( next_state );
        node_logger_->debug( "Transitioning to state {}", NodeStateToString( next_state ) );

        switch ( next_state )
        {
            case NodeState::MIGRATING_DATABASE:
            {
                account_->InitMessenger( pubsub_ );
                MigrateDatabase(
                    [weak_self( weak_from_this() )]( outcome::result<void> result )
                    {
                        if ( auto strong = weak_self.lock() )
                        {
                            if ( result.has_error() )
                            {
                                strong->node_logger_->error( "Database migration error: {}", result.error().message() );
                                if ( result.error() == MigrationManager::Error::BLOCKCHAIN_INIT_FAILED )
                                {
                                    strong->node_logger_->info( "Scheduling blockchain retry after failure" );
                                    strong->ScheduleMigrationRetry();
                                }
                                return;
                            }
                            strong->StateTransition( NodeState::INITIALIZING_DATABASE );
                        }
                    } );
                break;
            }
            case NodeState::INITIALIZING_DATABASE:
            {
                if ( !InitDatabase() )
                {
                    node_logger_->error( "GlobalDB initialization error" );
                    return;
                }
                account_->ConfigureDatabaseDependencies( tx_globaldb_ );
                tx_globaldb_->AddListenTopic( processing_channel_topic_ );
                StateTransition( NodeState::INITIALIZING_BLOCKCHAIN );
                break;
            }
            case NodeState::INITIALIZING_BLOCKCHAIN:
            {
                if ( !blockchain_ )
                {
                    auto weak_self = weak_from_this();
                    blockchain_    = Blockchain::New(
                        tx_globaldb_,
                        account_,
                        pubsub_,
                        [weak_self]( outcome::result<void> result )
                        {
                            if ( auto strong = weak_self.lock() )
                            {
                                if ( result.has_error() )
                                {
                                    strong->node_logger_->error( "Error starting blockchain: {}",
                                                                 result.error().message() );
                                    strong->node_logger_->info( "Scheduling blockchain retry after failure" );
                                    strong->ScheduleBlockchainRetry();
                                    return;
                                }
                                auto current_state = strong->state_.load();
                                if ( current_state != NodeState::INITIALIZING_BLOCKCHAIN )
                                {
                                    strong->node_logger_->debug(
                                        "Skipping transaction initialization, unexpected state: {}",
                                        NodeStateToString( current_state ) );
                                    return;
                                }
                                strong->node_logger_->debug(
                                    "Blockchain started successfully, starting transaction manager" );
                                if ( strong->is_full_node_ )
                                {
                                    strong->node_logger_->debug(
                                        "Full node: Setting blockchain to grab other account creation blocks" );
                                    strong->blockchain_->SetFullNodeMode();
                                }

                                // Move transaction initialization off the AccountMessenger worker thread.
                                boost::asio::post(
                                    *strong->io_,
                                    [weak_self]
                                    {
                                        if ( auto strong = weak_self.lock() )
                                        {
                                            auto current_state = strong->state_.load();
                                            if ( current_state != NodeState::INITIALIZING_BLOCKCHAIN )
                                            {
                                                strong->node_logger_->debug(
                                                    "Skipping transaction initialization, unexpected state: {}",
                                                    NodeStateToString( current_state ) );
                                                return;
                                            }
                                            strong->StateTransition( NodeState::INITIALIZING_TRANSACTIONS );
                                        }
                                    } );
                            }
                        } );
                }
                blockchain_->Start();

                break;
            }

            case NodeState::INITIALIZING_TRANSACTIONS:
            {
                transaction_manager_ = TransactionManager::New( tx_globaldb_,
                                                                io_,
                                                                account_,
                                                                std::make_shared<crypto::HasherImpl>(),
                                                                blockchain_,
                                                                is_full_node_ );

                transaction_manager_->RegisterStateChangeCallback(
                    [weak_self = weak_from_this()]( TransactionManager::State old_state,
                                                    TransactionManager::State new_state )
                    {
                        if ( auto strong = weak_self.lock() )
                        {
                            strong->TransactionStateChanged( old_state, new_state );
                        }
                    } );
                transaction_manager_->Start();
                // TS-01: Wire configurable timestamp tolerance from DevConfig_st
                // to TransactionManager's CheckTransactionTimestamp via SetTimeFrameToleranceMs.
                // Default: 300000ms (±5 minutes), overridable via DevConfig_st aggregate init.
                transaction_manager_->SetTimeFrameToleranceMs( kDefaultTimestampToleranceMs );

                // Phase 6 (D-01..D-10): Wire slot-hash populator bridging
                // PublicChainInputValidator -> ConsensusManager::CreateVote, so
                // each signed vote commits to its RPC endpoint slot hashes.
                // Single-chain resolution: use the first configured chain id.
                // Multi-chain (resolve chain_id from the vote's proposal subject)
                // is a future enhancement.
                if ( blockchain_ )
                {
                    blockchain_->SetSlotHashPopulator(
                        [this]( sgns::ConsensusVote &vote )
                        {
                            if ( !transaction_manager_ )
                            {
                                return;
                            }
                            auto &validator = transaction_manager_->GetPublicChainInputValidator();
                            const auto chain_id = validator.GetFirstConfiguredChainId();
                            if ( !chain_id.has_value() )
                            {
                                // No RPC endpoints configured: leave slots empty (abstain, D-05).
                                node_logger_->debug( "SlotHashPopulator: no configured chain; abstaining" );
                                return;
                            }
                            const auto slot0 = validator.GetSlotHash( 0, chain_id.value() );
                            const auto slot1 = validator.GetSlotHash( 1, chain_id.value() );
                            const auto slot2 = validator.GetSlotHash( 2, chain_id.value() );
                            if ( !slot0.empty() )
                            {
                                vote.set_slot_0_hash( slot0.data(), slot0.size() );
                            }
                            if ( !slot1.empty() )
                            {
                                vote.set_slot_1_hash( slot1.data(), slot1.size() );
                            }
                            if ( !slot2.empty() )
                            {
                                vote.set_slot_2_hash( slot2.data(), slot2.size() );
                            }
                            node_logger_->debug( "SlotHashPopulator: populated chain_id={} slot0={} slot1={} slot2={}",
                                                 chain_id.value(),
                                                 !slot0.empty(),
                                                 !slot1.empty(),
                                                 !slot2.empty() );
                        } );
                }

                // Initialize shared EthWatchService for EVM event detection
                eth_watch_service_ = std::make_shared<eth::EthWatchService>();

                // Initialize bridge relayer — wires evmrelay burn events → MintFunds
                bridge_relayer_ = BridgeRelayer::Create( std::weak_ptr<TransactionManager>( transaction_manager_ ),
                                                         eth_watch_service_ );

                // D-04: Launch async bridge initialization as NON-BLOCKING post.
                // ChainRpcEndpointProvider::Initialize() runs on the io_context
                // independently; observers (BridgeRelayer, catch-up scan) are
                // notified synchronously within Initialize. The node state
                // machine proceeds through INITIALIZING_PROCESSING → READY without waiting.
                boost::asio::post( *io_,
                                   [weak_self = weak_from_this()]
                                   {
                                       if ( auto strong = weak_self.lock() )
                                       {
                                           strong->InitializeAndStartBridge();
                                       }
                                   } );

                break;
            }
            case NodeState::INITIALIZING_PROCESSING:
            {
                ResetProcessingMembers();

                if ( !InitProcessingModules() )
                {
                    node_logger_->error( "Processing modules initialization error" );
                    return;
                }

                auto payout_address = account_->GetAddress();

                if ( auto address = account_->LoadFromSecureStorage( "payout_address" ); address.has_value() )
                {
                    payout_address = std::move( address.value() );
                    node_logger_->debug( "Using address {:.8} for payout", payout_address );
                }

                processing_service_ = std::make_shared<processing::ProcessingServiceImpl>(
                    pubsub_,
                    MAX_NODES_COUNT,
                    std::make_shared<processing::SubTaskEnqueuerImpl>( task_queue_ ),
                    task_result_storage_,
                    processing_core_,
                    [weak_self = weak_from_this()]( const std::string &var, const SGProcessing::TaskResult &taskresult )
                    {
                        if ( auto strong = weak_self.lock() )
                        {
                            strong->ProcessingDone( var, taskresult );
                        }
                    },
                    [weak_self = weak_from_this()]( const std::string &var )
                    {
                        if ( auto strong = weak_self.lock() )
                        {
                            strong->ProcessingError( var );
                        }
                    },
                    payout_address );

                processing_service_->SetChannelListRequestTimeout( boost::posix_time::milliseconds( 3000 ) );
                if ( isprocessor_ )
                {
                    StartProcessing();
                }
                StateTransition( NodeState::READY );
                break;
            }

            case NodeState::READY:
            {
                node_logger_->info( "GeniusNode READY" );
                break;
            }
            case NodeState::CREATING:
            default:
                break;
        }
    }

    void GeniusNode::InitOpenSSL()
    {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
    }

    bool GeniusNode::InitLoggers( const std::string &base_path )
    {
        logging_system_ = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            GetLoggingSystem( base_path ) ) );
        auto result     = logging_system_->configure();
        if ( result.has_error )
        {
            std::cerr << "Logger init error: " << result.message;
            return false;
        }

        libp2p::log::setLoggingSystem( logging_system_ );
        libp2p::log::setLevelOfGroup( "SuperGeniusDemo", soralog::Level::ERROR_ );

        std::string logdir = "";
#ifndef SGNS_DEBUGLOGS
        logdir = base_path + "/sgnslog2.log";
#endif
#ifdef SGNS_DEBUGLOGS
        // Debug mode
        node_logger_              = ConfigureLogger( "SuperGeniusNode", logdir, spdlog::level::debug );
        auto loggerGeniusNode     = ConfigureLogger( "GeniusNode", logdir, spdlog::level::debug );
        auto loggerGlobalDB       = ConfigureLogger( "GlobalDB", logdir, spdlog::level::err );
        auto loggerDAGSyncer      = ConfigureLogger( "GraphsyncDAGSyncer", logdir, spdlog::level::err );
        auto loggerGraphsync      = ConfigureLogger( "graphsync", logdir, spdlog::level::err );
        auto loggerBroadcaster    = ConfigureLogger( "PubSubBroadcasterExt", logdir, spdlog::level::err );
        auto loggerDataStore      = ConfigureLogger( "CrdtDatastore", logdir, spdlog::level::err );
        auto loggerCRDTHeads      = ConfigureLogger( "CrdtHeads", logdir, spdlog::level::err );
        auto loggerTransactions   = ConfigureLogger( "TransactionManager", logdir, spdlog::level::debug );
        auto loggerMigration      = ConfigureLogger( "MigrationManager", logdir, spdlog::level::err );
        auto loggerMigrationStep  = ConfigureLogger( "MigrationStep", logdir, spdlog::level::err );
        auto loggerQueue          = ConfigureLogger( "TaskQueueImpl", logdir, spdlog::level::trace );
        auto loggerRocksDB        = ConfigureLogger( "rocksdb", logdir, spdlog::level::err );
        auto logkad               = ConfigureLogger( "Kademlia", logdir, spdlog::level::err );
        auto logNoise             = ConfigureLogger( "Noise", logdir, spdlog::level::err );
        auto logProcessingEngine  = ConfigureLogger( "ProcessingEngine", logdir, spdlog::level::err );
        auto loggerSubQueue       = ConfigureLogger( "ProcessingSubTaskQueueAccessorImpl", logdir, spdlog::level::err );
        auto loggerProcServ       = ConfigureLogger( "ProcessingService", logdir, spdlog::level::err );
        auto loggerProcqm         = ConfigureLogger( "ProcessingSubTaskQueueManager", logdir, spdlog::level::err );
        auto loggerUPNP           = ConfigureLogger( "UPNP", logdir, spdlog::level::err );
        auto loggerProcessingNode = ConfigureLogger( "ProcessingNode", logdir, spdlog::level::err );
        auto loggerGossipPubsub   = ConfigureLogger( "GossipPubSub", logdir, spdlog::level::err );
        auto loggerAccountMessenger = ConfigureLogger( "AccountMessenger", logdir, spdlog::level::err );
        auto loggerGeniusAccount    = ConfigureLogger( "GeniusAccount", logdir, spdlog::level::err );
        auto loggerKeyPair          = ConfigureLogger( "KeyPairFileStorage", logdir, spdlog::level::err );
        auto loggerBlockchain       = ConfigureLogger( "Blockchain", logdir, spdlog::level::err );
        auto loggerValidator        = ConfigureLogger( "ValidatorRegistry", logdir, spdlog::level::debug );
        auto loggerProcMgr          = ConfigureLogger( "SGProcessingManager", logdir, spdlog::level::err );
        auto loggerProcessor        = ConfigureLogger( "SGProcessor", logdir, spdlog::level::err );
        auto loggerCrdtCallback     = ConfigureLogger( "CRDTCallbackManager", logdir, spdlog::level::err );
        auto loggerCoinPrices       = ConfigureLogger( "CoinPrices", logdir, spdlog::level::err );
        auto loggerUTXOManager      = ConfigureLogger( "UTXOManager", logdir, spdlog::level::err );
        auto loggerConsensusManager = ConfigureLogger( "ConsensusManager", logdir, spdlog::level::debug );
        auto loggerCRDTSet          = ConfigureLogger( "CRDTSet", logdir, spdlog::level::err );
        auto loggerInputValidator   = ConfigureLogger( "InputValidator", logdir, spdlog::level::trace );
        // AsyncIOManager loggers
        auto asioFileCommon  = ConfigureLogger( "FILECommon", logdir, spdlog::level::err );
        auto asioFileManager = ConfigureLogger( "FileManager", logdir, spdlog::level::err );
        auto asioHttpCommon  = ConfigureLogger( "HTTPCommon", logdir, spdlog::level::err );
        auto asioIpfsCommon  = ConfigureLogger( "IPFSCommon", logdir, spdlog::level::err );
        auto asioIpfsLoader  = ConfigureLogger( "IPFSLoader", logdir, spdlog::level::err );
        auto asioFileLoader  = ConfigureLogger( "MNNLoader", logdir, spdlog::level::err );
        auto asioWSCommon    = ConfigureLogger( "WSCommon", logdir, spdlog::level::err );
        // libp2p loggers
        libp2p::log::setLevelOfGroup( "*", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "Gossip", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "crypto", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "identify", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "kademlia", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "libp2p", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "mplex", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "muxer", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "plaintext", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "protocols", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "secio", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "security", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "yamux", soralog::Level::DEBUG );
#else
        // Release mode
        node_logger_              = ConfigureLogger( "SuperGeniusNode", logdir, spdlog::level::err );
        auto loggerGeniusNode     = ConfigureLogger( "GeniusNode", logdir, spdlog::level::err );
        auto loggerGlobalDB       = ConfigureLogger( "GlobalDB", logdir, spdlog::level::err );
        auto loggerDAGSyncer      = ConfigureLogger( "GraphsyncDAGSyncer", logdir, spdlog::level::err );
        auto loggerGraphsync      = ConfigureLogger( "graphsync", logdir, spdlog::level::err );
        auto loggerBroadcaster    = ConfigureLogger( "PubSubBroadcasterExt", logdir, spdlog::level::err );
        auto loggerDataStore      = ConfigureLogger( "CrdtDatastore", logdir, spdlog::level::err );
        auto loggerCRDTHeads      = ConfigureLogger( "CrdtHeads", logdir, spdlog::level::err );
        auto loggerTransactions   = ConfigureLogger( "TransactionManager", logdir, spdlog::level::err );
        auto loggerMigration      = ConfigureLogger( "MigrationManager", logdir, spdlog::level::err );
        auto loggerMigrationStep  = ConfigureLogger( "MigrationStep", logdir, spdlog::level::err );
        auto loggerQueue          = ConfigureLogger( "TaskQueueImpl", logdir, spdlog::level::err );
        auto loggerRocksDB        = ConfigureLogger( "rocksdb", logdir, spdlog::level::err );
        auto logkad               = ConfigureLogger( "Kademlia", logdir, spdlog::level::err );
        auto logNoise             = ConfigureLogger( "Noise", logdir, spdlog::level::err );
        auto logProcessingEngine  = ConfigureLogger( "ProcessingEngine", logdir, spdlog::level::err );
        auto loggerSubQueue       = ConfigureLogger( "ProcessingSubTaskQueueAccessorImpl", logdir, spdlog::level::err );
        auto loggerProcServ       = ConfigureLogger( "ProcessingService", logdir, spdlog::level::err );
        auto loggerProcqm         = ConfigureLogger( "ProcessingSubTaskQueueManager", logdir, spdlog::level::err );
        auto loggerUPNP           = ConfigureLogger( "UPNP", logdir, spdlog::level::err );
        auto loggerProcessingNode = ConfigureLogger( "ProcessingNode", logdir, spdlog::level::err );
        auto loggerGossipPubsub   = ConfigureLogger( "GossipPubSub", logdir, spdlog::level::err );
        auto loggerAccountMessenger = ConfigureLogger( "AccountMessenger", logdir, spdlog::level::err );
        auto loggerGeniusAccount    = ConfigureLogger( "GeniusAccount", logdir, spdlog::level::err );
        auto loggerKeyPair          = ConfigureLogger( "KeyPairFileStorage", logdir, spdlog::level::err );
        auto loggerBlockchain       = ConfigureLogger( "Blockchain", logdir, spdlog::level::err );
        auto loggerValidator        = ConfigureLogger( "ValidatorRegistry", logdir, spdlog::level::err );
        auto loggerProcMgr          = ConfigureLogger( "SGProcessingManager", logdir, spdlog::level::err );
        auto loggerProcessor        = ConfigureLogger( "SGProcessor", logdir, spdlog::level::err );
        auto loggerCrdtCallback     = ConfigureLogger( "CRDTCallbackManager", logdir, spdlog::level::err );
        auto loggerCoinPrices       = ConfigureLogger( "CoinPrices", logdir, spdlog::level::err );
        auto loggerUTXOManager      = ConfigureLogger( "UTXOManager", logdir, spdlog::level::err );
        auto loggerConsensusManager = ConfigureLogger( "ConsensusManager", logdir, spdlog::level::err );
        auto loggerCRDTSet          = ConfigureLogger( "CRDTSet", logdir, spdlog::level::err );
        auto loggerInputValidator   = ConfigureLogger( "InputValidator", logdir, spdlog::level::err );

        //AsyncIOManager Loggers
        auto asioFileCommon  = ConfigureLogger( "FILECommon", logdir, spdlog::level::err );
        auto asioFileManager = ConfigureLogger( "FileManager", logdir, spdlog::level::err );
        auto asioHttpCommon  = ConfigureLogger( "HTTPCommon", logdir, spdlog::level::err );
        auto asioIpfsCommon  = ConfigureLogger( "IPFSCommon", logdir, spdlog::level::err );
        auto asioIpfsLoader  = ConfigureLogger( "IPFSLoader", logdir, spdlog::level::err );
        auto asioFileLoader  = ConfigureLogger( "MNNLoader", logdir, spdlog::level::err );
        auto asioWSCommon    = ConfigureLogger( "WSCommon", logdir, spdlog::level::err );
#endif

        // Logger initialization complete
        node_logger_->info( "Logger initialized successfully" );

        return true;
    }

    bool GeniusNode::InitNetwork( uint16_t base_port, bool is_full_node )
    {
        bool ret    = true;
        pubsubport_ = GenerateRandomPort( base_port, account_->GetAddress() );

        std::string old_lanip;
        do
        {
            if ( use_upnp_ )
            {
                //ret = InitUPNP();
                (void) InitUPNP(); // Ignore UPNP init result for now
            }

            // Make a base58 out of our address
            std::string                tempaddress = account_->GetAddress();
            std::vector<unsigned char> inputBytes( tempaddress.begin(), tempaddress.end() );
            std::vector<unsigned char> hash( SHA256_DIGEST_LENGTH );
            SHA256( inputBytes.data(), inputBytes.size(), hash.data() );

            auto key          = libp2p::multi::ContentIdentifierCodec::encodeCIDV0( hash.data(), hash.size() );
            auto acc_cid      = libp2p::multi::ContentIdentifierCodec::decode( key );
            auto maybe_base58 = libp2p::multi::ContentIdentifierCodec::toString( acc_cid.value() );
            if ( !maybe_base58 )
            {
                ret = false;
                node_logger_->error( "We couldn't convert the account {} to base58", account_->GetAddress() );
                break;
            }
            base58key_ = maybe_base58.value();

            gnus_network_full_path_ = std::string( GNUS_NETWORK_PATH ) + version::GetNetAndVersionAppendix() +
                                      base58key_;

            auto pubsubKeyPath = gnus_network_full_path_ + "/pubs_processor";

            //Set a pubsub config, use no signing because we can verify with proof and dag structure
            libp2p::protocol::gossip::Config config;
            config.echo_forward_mode       = false;
            config.sign_messages           = false;
            config.seen_cache_limit        = 10;
            config.heartbeat_interval_msec = std::chrono::milliseconds{ 500 };
            config.rw_timeout_msec         = std::chrono::seconds{ 30 };

            pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>(
                crdt::KeyPairFileStorage( write_base_path_ + pubsubKeyPath ).GetKeyPair().value(),
                config );
            auto pubs = pubsub_->Start( pubsubport_, {}, old_lanip, {} );
            pubs.wait();
            node_logger_->info( "PubSub started at address: {}", pubsub_->GetLocalAddress() );

            if ( !is_full_node )
            {
                pubsub_->GetHost()->getConnectionManagerConfig().high_water = 300;
                pubsub_->GetHost()->getConnectionManagerConfig().low_water  = 150;
            }
            else
            {
                pubsub_->GetHost()->getConnectionManagerConfig().high_water = 400;
                pubsub_->GetHost()->getConnectionManagerConfig().low_water  = 200;
            }
            graphsyncnetwork_ = std::make_shared<ipfs_lite::ipfs::graphsync::Network>( pubsub_->GetHost(), scheduler_ );

            // Initialize DHT early so peer discovery works during database migration
            if ( autodht_ )
            {
                DHTInit();
            }
        } while ( 0 );
        return ret;
    }

    bool GeniusNode::InitUPNP()
    {
        auto upnp = std::make_shared<upnp::UPNP>();
        if ( !upnp->GetIGD() )
        {
            return true;
        }

        bool ret = false;
        do
        {
            std::string wanip = upnp->GetWanIP();
            std::string lanip = upnp->GetLocalIP();
            node_logger_->info( "Wan IP: {}", wanip );
            node_logger_->info( "Lan IP: {}", lanip );

            std::string owner;

            constexpr uint16_t MAX_ATTEMPTS = 10;
            for ( uint16_t i = 0; i < MAX_ATTEMPTS; ++i )
            {
                uint16_t candidate_port = pubsubport_ + i;
                if ( upnp->CheckIfPortInUse( candidate_port, "TCP", owner ) )
                {
                    if ( owner == lanip )
                    {
                        node_logger_->info( "Port {} is already mapped by this device. Try using it.", candidate_port );
                        if ( upnp->OpenPort( candidate_port, candidate_port, "TCP", 3600 ) )
                        {
                            ret         = true;
                            pubsubport_ = candidate_port;
                            break;
                        }

                        node_logger_->error(
                            "Port {} is already mapped by this device. We tried using it, but could not. Will try other ports.",
                            candidate_port );
                        continue;
                    }
                    node_logger_->error( "Port {} already in use by {}", candidate_port, owner );
                    continue;
                }

                if ( upnp->OpenPort( candidate_port, candidate_port, "TCP", 3600 ) )
                {
                    node_logger_->info( "Successfully opened port {}", candidate_port );
                    ret         = true;
                    pubsubport_ = candidate_port;
                    break;
                }
                node_logger_->warn( "Failed to open port {}", candidate_port );
            }
            if ( !ret )
            {
                node_logger_->error( "Unable to open a usable UPnP port after {} attempts", MAX_ATTEMPTS );
                break;
            }

        } while ( 0 );

        return ret;
    }

    bool GeniusNode::InitDatabase()
    {
        bool ret = false;
        do
        {
            auto global_db_ret = crdt::GlobalDB::New( io_,
                                                      write_base_path_ + gnus_network_full_path_,
                                                      pubsub_,
                                                      crdt::CrdtOptions::DefaultOptions(),
                                                      graphsyncnetwork_,
                                                      scheduler_,
                                                      generator_ );
            if ( global_db_ret.has_error() )
            {
                node_logger_->error( "Error creating GlobalDB: {}", global_db_ret.error().message() );
                break;
            }
            tx_globaldb_ = std::move( global_db_ret.value() );

            tx_globaldb_->Start();

            ret = true;
        } while ( 0 );
        return ret;
    }

    bool GeniusNode::InitProcessingModules()
    {
        bool ret = true;

        task_queue_      = processing::TaskQueueImpl::New( tx_globaldb_, processing_channel_topic_ );
        processing_core_ = processing::ProcessingCoreImpl::New( task_queue_, 1, dev_config_.TokenID );

        task_result_storage_ = std::make_shared<processing::SubTaskResultStorageImpl>( tx_globaldb_,
                                                                                       processing_channel_topic_ );
        return ret;
    }

    void GeniusNode::MigrateDatabase( std::function<void( outcome::result<void> )> callback )
    {
        auto migrationManager = sgns::MigrationManager::New( io_,               // ioContext
                                                             pubsub_,           // pubSub
                                                             graphsyncnetwork_, // graphsync
                                                             scheduler_,        // scheduler
                                                             generator_,        // generator
                                                             write_base_path_,  // writeBasePath
                                                             base58key_,        // base58key
                                                             account_,
                                                             is_full_node_ );

        std::thread migration_thread(
            [manager = std::move( migrationManager ), cb = std::move( callback )]
            {
                auto migrationResult = manager->Migrate();
                if ( cb )
                {
                    cb( migrationResult );
                }
            } );
        migration_thread.detach();
    }

    void GeniusNode::ScheduleMigrationRetry()
    {
        std::thread(
            [weak_self = weak_from_this()]
            {
                std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
                if ( auto strong = weak_self.lock() )
                {
                    strong->StateTransition( NodeState::MIGRATING_DATABASE );
                }
            } )
            .detach();
    }

    void GeniusNode::ScheduleBlockchainRetry()
    {
        std::thread(
            [weak_self = weak_from_this()]
            {
                std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
                if ( auto strong = weak_self.lock() )
                {
                    auto current_state = strong->state_.load();
                    if ( current_state != NodeState::INITIALIZING_BLOCKCHAIN )
                    {
                        strong->node_logger_->debug( "Skipping blockchain retry, unexpected state: {}",
                                                     NodeStateToString( current_state ) );
                        return;
                    }
                    strong->StateTransition( NodeState::INITIALIZING_BLOCKCHAIN );
                }
            } )
            .detach();
    }

    base::Logger GeniusNode::ConfigureLogger( const std::string        &tag,
                                              const std::string        &logdir,
                                              spdlog::level::level_enum level )
    {
        auto logger = base::createLogger( tag, logdir );
        logger->set_level( level );
        if ( level != spdlog::level::off )
        {
            logger->flush_on( level );
        }
        return logger;
    }

    GeniusNode::~GeniusNode()
    {
        node_logger_->debug( "~GeniusNode CALLED" );

        if ( pubsub_ )
        {
            pubsub_->Stop(); // Stop activities of OtherClass
        }
        io_work_guard_.reset();
        if ( io_ )
        {
            io_->stop(); // Stop our io_context
        }
        for ( auto &t : io_threads_ )
        {
            if ( t.joinable() )
            {
                t.join();
            }
        }
        io_threads_.clear();
        stop_upnp = true;
        if ( upnp_thread.joinable() )
        {
            upnp_thread.join();
        }
        if ( processing_service_ )
        {
            processing_service_->StopProcessing();
        }
        if ( processing_callback_pool_ )
        {
            processing_callback_pool_->join();
            processing_callback_pool_.reset();
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        node_logger_->debug( "~GeniusNode FINISHED" );
    }

    void GeniusNode::RefreshUPNP( uint16_t pubsubport )
    {
        if ( upnp_thread.joinable() )
        {
            stop_upnp = true;   // Signal the existing thread to stop
            upnp_thread.join(); // Wait for it to finish
        }

        stop_upnp = false; // Reset the stop flag for the new thread

        upnp_thread = std::thread(
            [this, pubsubport]()
            {
                auto next_refresh_time = std::chrono::steady_clock::now() + std::chrono::minutes( 60 );
                auto upnp_shared       = std::make_shared<upnp::UPNP>();

                while ( !stop_upnp )
                {
                    if ( std::chrono::steady_clock::now() >= next_refresh_time )
                    {
                        std::weak_ptr<upnp::UPNP> upnp_weak = upnp_shared;

                        if ( auto upnp = upnp_weak.lock() )
                        {
                            if ( upnp->GetIGD() )
                            {
                                auto openedPort = upnp->OpenPort( pubsubport, pubsubport, "TCP", 3600 );
                                if ( !openedPort )
                                {
                                    GeniusNodeLogger()->error( "Failed to open port" );
                                }
                                else
                                {
                                    GeniusNodeLogger()->info( "Open Ports Success pubsub: {} ", pubsubport );
                                }
                            }
                            else
                            {
                                GeniusNodeLogger()->info( "No IGD" );
                            }
                        }
                        else
                        {
                            GeniusNodeLogger()->info( "UPNP weak_ptr expired" );
                            stop_upnp = true; // Signal thread to stop gracefully
                        }

                        next_refresh_time = std::chrono::steady_clock::now() + std::chrono::minutes( 60 );
                    }

                    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                }
            } );
    }

    void GeniusNode::AddPeer( const std::string &peer )
    {
        pubsub_->AddPeers( { peer } );
    }

    void GeniusNode::DHTInit()
    {
        // Encode the string to UTF-8 bytes
        std::string                temp = processing_grid_chanel_topic_ + sgns::version::GetNetAndVersionAppendix();
        std::vector<unsigned char> inputBytes( temp.begin(), temp.end() );

        // Compute the SHA-256 hash of the input bytes
        std::vector<unsigned char> hash( SHA256_DIGEST_LENGTH );
        SHA256( inputBytes.data(), inputBytes.size(), hash.data() );

        // Provide CID
        auto key = libp2p::multi::ContentIdentifierCodec::encodeCIDV0( hash.data(), hash.size() );
        pubsub_->GetDHT()->Start();
        pubsub_->ProvideCID( key );

        auto cidtest = libp2p::multi::ContentIdentifierCodec::decode( key );

        auto cidstring = libp2p::multi::ContentIdentifierCodec::toString( cidtest.value() );
        node_logger_->info( "CID Test:: {}", cidstring.value() );

        // Also Find providers
        pubsub_->StartFindingPeers( key );
    }

    std::string generate_uuid_with_ipfs_id( const std::string &ipfs_id )
    {
        // Hash the IPFS ID
        std::hash<std::string> hasher;
        uint64_t               id_hash = hasher( ipfs_id );

        // Get a high-resolution timestamp
        auto now       = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>( now.time_since_epoch() ).count();

        // Combine the IPFS ID hash and timestamp to create a seed
        uint64_t seed = id_hash ^ static_cast<uint64_t>( timestamp );

        // Seed the PRNG
        std::mt19937                                       gen( seed );
        boost::uuids::basic_random_generator<std::mt19937> uuid_gen( gen );

        // Generate UUID
        boost::uuids::uuid uuid = uuid_gen();
        return boost::uuids::to_string( uuid );
    }

    std::vector<std::string> GeniusNode::GetAvailableAccounts()
    {
        return GeniusAccount::GetAvailableAccounts( write_base_path_ );
    }

    outcome::result<void> GeniusNode::SelectAccount( std::string_view public_address )
    {
        if ( public_address == GetAddress() )
        {
            node_logger_->warn( "Address already active" );
            return std::errc::address_in_use;
        }
        auto addresses = GeniusAccount::GetAvailableAccounts( write_base_path_ );

        if ( std::find( addresses.cbegin(), addresses.cend(), public_address ) == addresses.cend() )
        {
            node_logger_->error( "Could not find requested address" );
            return std::errc::address_not_available;
        }

        auto account = GeniusAccount::NewFromPublicKey( TokenID::FromBytes( { 0x00 } ),
                                                        public_address,
                                                        this->is_full_node_ );

        if ( account == nullptr )
        {
            node_logger_->error( "Account not created" );
            return std::errc::address_not_available;
        }

        ResetProcessingMembers();

        auto tx_manager_result = GetTransactionManager();
        if ( tx_manager_result.has_value() )
        {
            auto manager = std::move( tx_manager_result.value() );
            manager->Stop();
        }
        transaction_manager_.reset();

        bridge_relayer_.reset();

        eth_watch_service_.reset();

        if ( blockchain_ )
        {
            BOOST_OUTCOME_TRY( this->blockchain_->Stop() );
        }
        blockchain_.reset();

        tx_globaldb_.reset();

        if ( account_ )
        {
            account_.swap( account );
        }
        else
        {
            account_ = account;
        }
        account.reset();

        BeginDBInitialization();

        return outcome::success();
    }

    void GeniusNode::ResetProcessingMembers()
    {
        processing_service_.reset();
        task_result_storage_.reset();
        processing_core_.reset();
        task_queue_.reset();
    }

    outcome::result<void> GeniusNode::TransferAccount( std::string_view public_address )
    {
        auto addresses = GeniusAccount::GetAvailableAccounts( write_base_path_ );

        if ( std::find( addresses.cbegin(), addresses.cend(), public_address ) == addresses.cend() )
        {
            node_logger_->error( "Tried to transfer to account that was not added to GeniusNode" );
            return std::errc::address_not_available;
        }

        auto balance = account_->GetUTXOManager().GetBalance();
        if ( balance > 0 )
        {
            BOOST_OUTCOME_TRY(
                auto hash,
                transaction_manager_->TransferFunds( balance, std::string( public_address ), this->GetTokenID() ) );

            if ( transaction_manager_->WaitForTransactionOutgoing( hash, std::chrono::seconds( 20 ) ) !=
                 TransactionManager::TransactionStatus::CONFIRMED )
            {
                node_logger_->error( "Failed to transfer fund in viable time" );
                return std::errc::interrupted;
            }
        }

        return SelectAccount( public_address );
    }

    outcome::result<void> GeniusNode::DeleteAccount( std::string_view public_address )
    {
        if ( public_address == GetAddress() )
        {
            node_logger_->error( "Can't delete active account" );
            return std::errc::address_not_available;
        }

        return GeniusAccount::DeleteAccount( public_address, write_base_path_ );
    }

    outcome::result<void> GeniusNode::MergeAccount( std::string_view public_address )
    {
        BOOST_OUTCOME_TRY( TransferAccount( public_address ) );
        return DeleteAccount( public_address );
    }

    outcome::result<void> GeniusNode::SetPayoutAddress( std::string_view payout_address )
    {
        BOOST_OUTCOME_TRY( account_->SaveInSecureStorage( "payout_address", std::string( payout_address ) ) );

        this->StateTransition( NodeState::INITIALIZING_PROCESSING );

        return outcome::success();
    }

    outcome::result<std::string> GeniusNode::ProcessImage( const std::string &jsondata )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        BOOST_OUTCOME_TRY( auto procmgr, sgns::sgprocessing::ProcessingManager::Create( jsondata ) );

        auto funds = GetProcessCost( procmgr );
        if ( funds <= 0 )
        {
            return outcome::failure( Error::PROCESS_COST_ERROR );
        }

        if ( account_->GetUTXOManager().GetBalance() < funds )
        {
            return outcome::failure( Error::INSUFFICIENT_FUNDS );
        }

        SGProcessing::Task task;
        auto               uuidstring = generate_uuid_with_ipfs_id( pubsub_->GetHost()->getId().toBase58() );

        //Make a small json to insert without extra indentation and spacing.
        json smalljson;
        sgns::to_json( smalljson, procmgr->GetProcessingData() );
        task.set_ipfs_block_id( uuidstring );
        task.set_json_data( smalljson.dump( -1 ) );
        task.set_random_seed( 0 );
        task.set_results_channel( ( boost::format( "RESULT_CHANNEL_ID_%1%" ) % ( 1 ) ).str() );
        //Get Processing Data
        auto procdata = procmgr->GetProcessingData();

        //Split into subtasks
        processing::ProcessTaskSplitter  taskSplitter;
        std::list<SGProcessing::SubTask> subTasks;
        //Make Copies, trying to use references for passes/input nodes may cause problems.
        auto passes = procdata.get_passes();
        for ( const auto &pass : passes )
        {
            auto input_nodes = pass.get_model().value().get_input_nodes();
            for ( auto &model : input_nodes )
            {
                json modeljson;
                sgns::to_json( modeljson, model );
                auto   index = procmgr->GetInputIndex( model.get_source().value() );
                size_t nChunks =
                    procdata.get_inputs()[index.value()].get_dimensions().value().get_chunk_count().value();
                rapidjson::StringBuffer                    buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer( buffer );

                taskSplitter.SplitTask( task,
                                        subTasks,
                                        modeljson.dump( -1 ),
                                        nChunks,
                                        false,
                                        pubsub_->GetHost()->getId().toBase58() );
            }
        }
        if ( subTasks.size() <= 0 )
        {
            return outcome::failure( Error::INVALID_JSON );
        }
        auto cut = sgns::TokenAmount::ParseMinions( dev_config_.Cut );
        if ( !cut )
        {
            return outcome::failure( cut.error() );
        }

        BOOST_OUTCOME_TRY( auto manager, GetTransactionManager() );
        BOOST_OUTCOME_TRY( auto result_pair,
                           manager->HoldEscrow( funds, std::string( dev_config_.Addr ), cut.value(), uuidstring ) );

        //TODO - Make it async to post the job data in case the transaction gets confirmed.
        auto [tx_id, escrow_data_pair] = result_pair;

        auto [escrow_path, escrow_data] = escrow_data_pair;

        task.set_escrow_path( escrow_path );

        BOOST_OUTCOME_TRY( auto crdt_transaction,
                           CreateEscrowInfoCRDTTransaction( escrow_path, std::move( escrow_data ) ) );

        auto enqueue_task_return = task_queue_->EnqueueTask( task, subTasks, crdt_transaction );
        if ( enqueue_task_return.has_failure() )
        {
            return outcome::failure( Error::DATABASE_WRITE_ERROR );
        }

        return tx_id;
    }

    uint64_t GeniusNode::GetProcessCost( std::shared_ptr<sgns::sgprocessing::ProcessingManager> &procmgr )
    {
        auto blockLen = procmgr->ParseBlockSize();
        if ( !blockLen )
        {
            node_logger_->error( "ParseBlockSize failed" );
            return 0;
        }
        node_logger_->trace( "Parsed totalBytes: {}", blockLen.value() );

        auto maybeGnusPrice = GetGNUSPrice();
        if ( !maybeGnusPrice )
        {
            node_logger_->error( "GetGNUSPrice failed" );
            return 0;
        }
        double gnusPrice = maybeGnusPrice.value();
        node_logger_->trace( "Retrieved GNUS price (USD/genius): {}", gnusPrice );

        auto rawMinionsRes = TokenAmount::CalculateCostMinions( blockLen.value(), gnusPrice );
        if ( !rawMinionsRes )
        {
            node_logger_->error( "TokenAmount::CalculateCostMinions failed" );
            return 0;
        }
        uint64_t rawMinions = rawMinionsRes.value();
        node_logger_->trace( "Raw cost in minions: {}", rawMinions );

        return rawMinions;
    }

    outcome::result<double> GeniusNode::GetGNUSPrice()
    {
        auto price = GetCoinprice( { "genius-ai" } );
        if ( !price )
        {
            return outcome::failure( Error::NO_PRICE );
        }
        return price.value()["genius-ai"];
    }

    std::string GeniusNode::GetVersion()
    {
        return sgns::version::SuperGeniusVersionFullString();
    }

    outcome::result<std::string> GeniusNode::MintTokens( uint64_t           amount,
                                                         const std::string &transaction_hash,
                                                         const std::string &chainid,
                                                         TokenID            tokenid,
                                                         std::string        destination )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction manager not ready", __func__ );
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }
        if ( destination.empty() )
        {
            destination = account_->GetAddress();
        }

        BOOST_OUTCOME_TRY( auto manager, GetTransactionManager() );
        BOOST_OUTCOME_TRY( auto tx_id, manager->MintFunds( amount, transaction_hash, chainid, tokenid, destination ) );

        node_logger_->debug( "{}: Mint transaction {} sent ", __func__, tx_id );
        return tx_id;
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::MintTokens( uint64_t           amount,
                                                                              const std::string &transaction_hash,
                                                                              const std::string &chainid,
                                                                              TokenID            tokenid,
                                                                              std::string        destination,
                                                                              std::chrono::milliseconds timeout )
    {
        BOOST_OUTCOME_TRY( auto tx_id, MintTokens( amount, transaction_hash, chainid, tokenid ) );

        BOOST_OUTCOME_TRY( auto finalized_result, WaitForFinalized( tx_id, timeout ) );

        auto [tx_status, duration] = finalized_result;

        if ( tx_status != TransactionManager::TransactionStatus::CONFIRMED )
        {
            node_logger_->error( "{}: transaction {} failed after {} ms", __func__, tx_id, duration );
            return outcome::failure( Error::TRANSACTION_FAILED );
        }

        node_logger_->debug( "{}: transaction {} sent in {} ms", __func__, tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::TransferFunds( uint64_t                  amount,
                                                                                 const std::string        &destination,
                                                                                 TokenID                   token_id,
                                                                                 std::chrono::milliseconds timeout )
    {
        BOOST_OUTCOME_TRY( auto &&tx_id, TransferFunds( amount, destination, token_id ) );

        BOOST_OUTCOME_TRY( auto finalized_result, WaitForFinalized( tx_id, timeout ) );

        auto [tx_status, duration] = finalized_result;

        if ( tx_status != TransactionManager::TransactionStatus::CONFIRMED )
        {
            node_logger_->error( "{}: transaction {} failed after {} ms", __func__, tx_id, duration );
            return outcome::failure( Error::TRANSACTION_FAILED );
        }

        node_logger_->debug( "{}: transaction {} sent in {} ms", __func__, tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    outcome::result<std::string> GeniusNode::TransferFunds( uint64_t           amount,
                                                            const std::string &destination,
                                                            TokenID            token_id )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction Manager is not ready", __func__ );
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }

        auto available_balance = account_->GetUTXOManager().GetBalance( token_id );
        if ( available_balance < amount )
        {
            node_logger_->error( "{}: insufficient local funds: requested={}, available={}",
                                 __func__,
                                 amount,
                                 available_balance );
            return outcome::failure( Error::INSUFFICIENT_FUNDS );
        }

        BOOST_OUTCOME_TRY( auto manager, GetTransactionManager() );
        BOOST_OUTCOME_TRY( auto tx_id, manager->TransferFunds( amount, destination, token_id ) );

        node_logger_->debug( "{}: transaction {} sent", __func__, tx_id );
        return tx_id;
    }

    outcome::result<std::string> GeniusNode::PayDev( uint64_t amount, TokenID token_id )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction Manager is not ready", __func__ );
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }

        auto available_balance = account_->GetUTXOManager().GetBalance( token_id );
        if ( available_balance < amount )
        {
            node_logger_->error( "{}: insufficient local funds: requested={}, available={}",
                                 __func__,
                                 amount,
                                 available_balance );
            return outcome::failure( Error::INSUFFICIENT_FUNDS );
        }

        BOOST_OUTCOME_TRY( auto &&manager, GetTransactionManager() );
        BOOST_OUTCOME_TRY( auto &&tx_id, manager->TransferFunds( amount, dev_config_.Addr, token_id ) );

        node_logger_->debug( "{}: transaction {} triggered ", __func__, tx_id );
        return tx_id;
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::PayDev( uint64_t                  amount,
                                                                          TokenID                   token_id,
                                                                          std::chrono::milliseconds timeout )
    {
        BOOST_OUTCOME_TRY( auto &&tx_id, PayDev( amount, token_id ) );

        BOOST_OUTCOME_TRY( auto finalized_result, WaitForFinalized( tx_id, timeout ) );

        auto [tx_status, duration] = finalized_result;

        if ( tx_status != TransactionManager::TransactionStatus::CONFIRMED )
        {
            node_logger_->error( "{}: transaction {} failed after {} ms", __func__, tx_id, duration );
            return outcome::failure( Error::TRANSACTION_FAILED );
        }

        node_logger_->debug( "{}: transaction {} sent in {} ms", __func__, tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    outcome::result<std::pair<TransactionManager::TransactionStatus, uint64_t>> GeniusNode::WaitForFinalized(
        const std::string        &tx_id,
        std::chrono::milliseconds timeout )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction Manager is not ready", __func__ );

            return outcome::failure( boost::system::error_code{} );
        }
        auto start_time = std::chrono::steady_clock::now();

        do
        {
            auto finalized_result = IsFinalized( tx_id );
            if ( finalized_result.has_value() )
            {
                auto end_time = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();
                node_logger_->debug( "{}: Transaction finalized with status {} and duration of {} ms",
                                     __func__,
                                     static_cast<int>( finalized_result.value() ),
                                     duration );

                return std::make_pair( finalized_result.value(), duration );
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        } while ( std::chrono::steady_clock::now() - start_time < timeout );

        node_logger_->error( "{}: Transaction not finalized within timeout of {} ms", __func__, timeout.count() );

        return outcome::failure( Error::TRANSACTION_NOT_FINALIZED );
    }

    std::optional<TransactionManager::TransactionStatus> GeniusNode::IsFinalized( const std::string &tx_id )
    {
        auto manager_result = GetTransactionManager();

        if ( manager_result.has_failure() )
        {
            node_logger_->error( "{}: Failed to get Transaction Manager: {}",
                                 __func__,
                                 manager_result.error().message() );
            return std::nullopt;
        }
        auto manager   = manager_result.value();
        auto tx_status = manager->GetOutgoingStatusByTxId( tx_id );
        if ( tx_status == TransactionManager::TransactionStatus::CONFIRMED ||
             tx_status == TransactionManager::TransactionStatus::FAILED ||
             tx_status == TransactionManager::TransactionStatus::INVALID )
        {
            return tx_status;
        }
        return std::nullopt;
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::PayEscrow(
        const std::string                       &escrow_path,
        const SGProcessing::TaskResult          &taskresult,
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction,
        std::chrono::milliseconds                timeout )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto start_time = std::chrono::steady_clock::now();

        BOOST_OUTCOME_TRY( auto manager, GetTransactionManager() );
        BOOST_OUTCOME_TRY( auto tx_id, manager->PayEscrow( escrow_path, taskresult, std::move( crdt_transaction ) ) );

        auto payescrow_result = manager->WaitForTransactionOutgoing( tx_id, timeout );

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();

        if ( payescrow_result != TransactionManager::TransactionStatus::CONFIRMED )
        {
            node_logger_->error( "Pay escrow transaction {} failed after {} ms", tx_id, duration );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
        }

        node_logger_->debug( "Pay escrow transaction {} completed in {} ms", tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    uint64_t GeniusNode::GetBalance()
    {
        return account_->GetUTXOManager().GetBalance();
    }

    uint64_t GeniusNode::GetBalance( const TokenID token_id )
    {
        return account_->GetUTXOManager().GetBalance( token_id );
    }

    uint64_t GeniusNode::GetBalance( const std::string &address )
    {
        return account_->GetUTXOManager().GetBalance( address );
    }

    uint64_t GeniusNode::GetBalance( const TokenID token_id, const std::string &address )
    {
        return account_->GetUTXOManager().GetBalance( token_id, address );
    }

    void GeniusNode::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult )
    {
        static constexpr std::string_view FUNC = __func__;
        boost::asio::post( *processing_callback_pool_,
                           [weak_self( weak_from_this() ), task_id, taskresult]()
                           {
                               if ( auto strong = weak_self.lock() )
                               {
                                   strong->node_logger_->info( "[{}]{}: SUCCESS PROCESSING TASK {}",
                                                               strong->account_->GetAddress().substr( 0, 8 ),
                                                               FUNC,
                                                               task_id );
                                   do
                                   {
                                       if ( strong->task_queue_->IsTaskCompleted( task_id ) )
                                       {
                                           strong->node_logger_->info( "[{}]{}: Task Already completed!",
                                                                       strong->account_->GetAddress().substr( 0, 8 ),
                                                                       FUNC );
                                           break;
                                       }
                                       if ( strong->GetTransactionManagerState() != TransactionManager::State::READY )
                                       {
                                           strong->node_logger_->info( "[{}]{}: Transactions are not ready",
                                                                       strong->account_->GetAddress().substr( 0, 8 ),
                                                                       FUNC );
                                           break;
                                       }
                                       strong->node_logger_->info( "[{}]{}: Transactions READY",
                                                                   strong->account_->GetAddress().substr( 0, 8 ),
                                                                   FUNC );
                                       auto maybe_task = strong->task_queue_->GetTask( task_id );
                                       if ( maybe_task.has_failure() )
                                       {
                                           strong->node_logger_->info( "[{}]{}: Task id {} not found in DB",
                                                                       strong->account_->GetAddress().substr( 0, 8 ),
                                                                       FUNC,
                                                                       task_id );
                                           break;
                                       }
                                       auto escrow_path          = maybe_task.value().escrow_path();
                                       auto complete_task_result = strong->task_queue_->CompleteTask( task_id,
                                                                                                      taskresult );
                                       if ( complete_task_result.has_failure() )
                                       {
                                           strong->node_logger_->error( "[{}]{}: Unable to complete task: {} ",
                                                                        strong->account_->GetAddress().substr( 0, 8 ),
                                                                        FUNC,
                                                                        task_id );
                                           break;
                                       }
                                       strong->node_logger_->info( "[{}]{}: Creating the payout transactions",
                                                                   strong->account_->GetAddress().substr( 0, 8 ),
                                                                   FUNC );
                                       auto pay_result = strong->PayEscrow( escrow_path,
                                                                            taskresult,
                                                                            std::move( complete_task_result.value() ) );
                                       if ( pay_result.has_failure() )
                                       {
                                           strong->node_logger_->error( "[{}]{}: Escrow not paid for task: {} ",
                                                                        strong->account_->GetAddress().substr( 0, 8 ),
                                                                        FUNC,
                                                                        task_id );
                                           break;
                                       }
                                       strong->node_logger_->info( "[{}]{}: Paid for task: {}",
                                                                   strong->account_->GetAddress().substr( 0, 8 ),
                                                                   FUNC,
                                                                   task_id );

                                   } while ( 0 );
                               }
                           } );
    }

    void GeniusNode::ProcessingError( const std::string &task_id )
    {
        boost::asio::post( *processing_callback_pool_,
                           [weak_self( weak_from_this() ), task_id]()
                           {
                               if ( auto strong = weak_self.lock() )
                               {
                                   strong->node_logger_->error( "[ {} ] ERROR PROCESSING SUBTASK ",
                                                                strong->account_->GetAddress().substr( 0, 8 ),
                                                                task_id );
                               }
                           } );
    }

    void GeniusNode::PrintDataStore() const
    {
        if ( tx_globaldb_ )
        {
            tx_globaldb_->PrintDataStore();
        }
        else
        {
            node_logger_->error( "{}: GlobalDB is not initialized", __func__ );
        }
    }

    void GeniusNode::StopProcessing()
    {
        if ( processing_service_ )
        {
            processing_service_->StopProcessing();
        }
        else
        {
            node_logger_->error( "{}: Processing service is not initialized", __func__ );
        }
    }

    void GeniusNode::StartProcessing()
    {
        if ( processing_service_ )
        {
            processing_service_->StartProcessing( processing_grid_chanel_topic_ );
        }
        else
        {
            node_logger_->error( "{}: Processing service is not initialized", __func__ );
        }
    }

    outcome::result<std::map<std::string, double>> GeniusNode::GetCoinprice( const std::vector<std::string> &tokenIds )
    {
        auto                          currentTime = std::chrono::system_clock::now();
        std::map<std::string, double> result;
        std::vector<std::string>      tokensToFetch;
        // Determine which tokens need to be fetched
        for ( const auto &tokenId : tokenIds )
        {
            auto it = m_tokenPriceCache.find( tokenId );

            if ( it != m_tokenPriceCache.end() && ( currentTime - it->second.lastUpdate ) < m_cacheValidityDuration )
            {
                // Use cached price if it's still valid
                result[tokenId] = it->second.price;
            }
            else
            {
                // Add to the list of tokens that need fresh data
                tokensToFetch.push_back( tokenId );
            }
        }

        // If we have tokens to fetch and we're not rate limited
        if ( !tokensToFetch.empty() && ( currentTime - m_lastApiCall ) >= MIN_API_CALL_INTERVAL )
        {
            sgns::CoinGeckoPriceRetriever retriever;
            auto                          newPricesResult = retriever.getCurrentPrices( tokensToFetch );

            if ( newPricesResult )
            {
                auto &newPrices = newPricesResult.value();
                m_lastApiCall   = currentTime;

                // Update the cache and result with new prices
                for ( const auto &[token, price] : newPrices )
                {
                    m_tokenPriceCache[token] = { price, currentTime };
                    result[token]            = price;
                }
            }
            else
            {
                // Handle the error case
                // If we have some cached data, continue with what we have
                if ( result.empty() )
                {
                    // Only return error if we have no data at all
                    return newPricesResult.error();
                }
                // Otherwise, continue with partial data and log the error
                // log("Failed to fetch prices for some tokens: " + newPricesResult.error().message());
            }
        }

        return result;
    }

    outcome::result<std::map<std::string, std::map<int64_t, double>>> GeniusNode::GetCoinPriceByDate(
        const std::vector<std::string> &tokenIds,
        const std::vector<int64_t>     &timestamps )
    {
        sgns::CoinGeckoPriceRetriever retriever;
        return retriever.getHistoricalPrices( tokenIds, timestamps );
    }

    outcome::result<std::map<std::string, std::map<int64_t, double>>> GeniusNode::GetCoinPricesByDateRange(
        const std::vector<std::string> &tokenIds,
        int64_t                         from,
        int64_t                         to )
    {
        sgns::CoinGeckoPriceRetriever retriever;
        return retriever.getHistoricalPriceRange( tokenIds, from, to );
    }

    outcome::result<std::string> GeniusNode::FormatTokens( uint64_t amount, TokenID tokenId )
    {
        if ( tokenId.IsGNUS() )
        {
            return TokenAmount::FormatMinions( amount );
        }
        if ( tokenId.Equals( dev_config_.TokenID ) )
        {
            auto child = TokenAmount::ConvertToChildToken( amount, dev_config_.TokenValueInGNUS );
            if ( !child )
            {
                return outcome::failure( child.error() );
            }
            return child.value();
        }
        return outcome::failure( make_error_code( GeniusNode::Error::TOKEN_ID_MISMATCH ) );
    }

    outcome::result<uint64_t> GeniusNode::ParseTokens( const std::string &str, TokenID tokenId )
    {
        if ( tokenId.IsGNUS() )
        {
            return TokenAmount::ParseMinions( str );
        }
        if ( tokenId.Equals( dev_config_.TokenID ) )
        {
            return TokenAmount::ConvertFromChildToken( str, dev_config_.TokenValueInGNUS );
        }
        return outcome::failure( make_error_code( GeniusNode::Error::TOKEN_ID_MISMATCH ) );
    }

    std::vector<std::vector<uint8_t>> GeniusNode::GetInTransactions() const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return {};
        }
        return manager_result.value()->GetInTransactions();
    }

    std::vector<std::vector<uint8_t>> GeniusNode::GetOutTransactions() const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return {};
        }
        return manager_result.value()->GetOutTransactions();
    }

    const std::vector<std::vector<uint8_t>> GeniusNode::GetTransactions(
        std::optional<TransactionManager::TransactionStatus> tx_status ) const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return {};
        }
        return manager_result.value()->GetTransactions( tx_status );
    }

    std::string GeniusNode::GetAddress() const
    {
        std::string address = "UNVAILABLE";
        if ( account_ )
        {
            address = account_->GetAddress();
        }
        return address;
    }

    // Wait for a transaction to be processed with a timeout
    TransactionManager::TransactionStatus GeniusNode::WaitForTransactionOutgoing( const std::string        &txId,
                                                                                  std::chrono::milliseconds timeout )
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return TransactionManager::TransactionStatus::INVALID;
        }
        return manager_result.value()->WaitForTransactionOutgoing( txId, timeout );
    }

    // Wait for a transaction to be processed with a timeout
    TransactionManager::TransactionStatus GeniusNode::WaitForTransactionIncoming( const std::string        &txId,
                                                                                  std::chrono::milliseconds timeout )
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return TransactionManager::TransactionStatus::INVALID;
        }
        return manager_result.value()->WaitForTransactionIncoming( txId, timeout );
    }

    TransactionManager::TransactionStatus GeniusNode::WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                                            std::chrono::milliseconds timeout )
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return TransactionManager::TransactionStatus::INVALID;
        }
        return manager_result.value()->WaitForEscrowRelease( originalEscrowId, timeout );
    }

    outcome::result<std::shared_ptr<TransactionManager>> GeniusNode::GetTransactionManager() const
    {
        if ( !transaction_manager_ )
        {
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }
        return transaction_manager_;
    }

    outcome::result<std::shared_ptr<crdt::AtomicTransaction>> GeniusNode::CreateEscrowInfoCRDTTransaction(
        std::string        path,
        sgns::base::Buffer value )
    {
        auto crdt_transaction = tx_globaldb_->BeginTransaction();

        sgns::crdt::HierarchicalKey key( path );

        BOOST_OUTCOME_TRY( crdt_transaction->Put( std::move( key ), std::move( value ) ) );

        return crdt_transaction;
    }

    TransactionManager::State GeniusNode::GetTransactionManagerState() const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return TransactionManager::State::CREATING;
        }
        return manager_result.value()->GetState();
    }

    void GeniusNode::SendTransactionAndProof( std::shared_ptr<GeniusTransaction> tx, std::vector<uint8_t> proof )
    {
        auto manager_result = GetTransactionManager();
        if ( manager_result.has_value() )
        {
            manager_result.value()->EnqueueTransaction( std::make_pair( tx, proof ) );
        }
        else
        {
            node_logger_->error( "{}: Transactions not ready", __func__ );
        }
    }

    void GeniusNode::ConfigureTransactionFilterTimeoutsMs( uint64_t timeframe_limit_ms, uint64_t mutability_window_ms )
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            node_logger_->error( "{}: Transactions not ready", __func__ );
            return;
        }
        auto manager = manager_result.value();
        manager->SetTimeFrameToleranceMs( timeframe_limit_ms );
        manager->SetMutabilityWindowMs( mutability_window_ms );
    }

    void GeniusNode::RotateLogFiles( const std::string &base_path )
    {
        std::filesystem::path basePath( base_path );

        // Define log file paths
        std::filesystem::path sgnslog_path      = basePath / "sgnslog.log";
        std::filesystem::path sgnslog2_path     = basePath / "sgnslog2.log";
        std::filesystem::path sgnslog_old_path  = basePath / "sgnslog.old.log";
        std::filesystem::path sgnslog2_old_path = basePath / "sgnslog2.old.log";

        try
        {
            // Handle sgnslog.log rotation
            if ( std::filesystem::exists( sgnslog_path ) )
            {
                // Delete old backup if it exists
                if ( std::filesystem::exists( sgnslog_old_path ) )
                {
                    std::filesystem::remove( sgnslog_old_path );
                    std::cout << "Deleted old backup: " << sgnslog_old_path << std::endl;
                }

                // Rename current log to backup
                std::filesystem::rename( sgnslog_path, sgnslog_old_path );
                std::cout << "Rotated log: " << sgnslog_path << " -> " << sgnslog_old_path << std::endl;
            }

            // Handle sgnslog2.log rotation
            if ( std::filesystem::exists( sgnslog2_path ) )
            {
                // Delete old backup if it exists
                if ( std::filesystem::exists( sgnslog2_old_path ) )
                {
                    std::filesystem::remove( sgnslog2_old_path );
                    std::cout << "Deleted old backup: " << sgnslog2_old_path << std::endl;
                }

                // Rename current log to backup
                std::filesystem::rename( sgnslog2_path, sgnslog2_old_path );
                std::cout << "Rotated log: " << sgnslog2_path << " -> " << sgnslog2_old_path << std::endl;
            }
        }
        catch ( const std::filesystem::filesystem_error &e )
        {
            std::cerr << "Log rotation error: " << e.what() << std::endl;
            // Continue execution - don't let log rotation failure stop the application
        }
    }

    void GeniusNode::ConfigureRpcEndpoint( const std::string &chain_id, std::vector<WeightedRpcEndpoint> endpoints )
    {
        if ( !transaction_manager_ )
        {
            node_logger_->warn( "ConfigureRpcEndpoint called before transaction manager is ready" );
            return;
        }
        transaction_manager_->GetPublicChainInputValidator().SetRpcEndpoints( chain_id, std::move( endpoints ) );
        node_logger_->info( "Configured {} RPC endpoint(s) for chain {}", endpoints.size(), chain_id );
    }

    std::filesystem::path GeniusNode::ResolveBridgeChainsConfigPath() const
    {
        std::filesystem::path bridge_chains_path;

        // Primary: use DevConfig_st BaseWritePath (writable on all platforms including Android)
        if ( !dev_config_.BaseWritePath.empty() )
        {
            bridge_chains_path = std::filesystem::path( dev_config_.BaseWritePath ) / "bridge_chains_config.json";
        }

        // Fallback: binary directory (finds CMake-installed or copied default)
        if ( bridge_chains_path.empty() || !std::filesystem::exists( bridge_chains_path ) )
        {
            try
            {
                auto bin_dir = boost::dll::program_location().parent_path();
                auto candidate = std::filesystem::path( bin_dir.string() ) / "bridge_chains_config.json";
                if ( std::filesystem::exists( candidate ) )
                {
                    bridge_chains_path = std::move( candidate );
                }
            }
            catch ( const std::exception &e )
            {
                node_logger_->warn( "ResolveBridgeChainsConfigPath: cannot determine binary location ({}), "
                                    "falling back to CWD",
                                    e.what() );
            }
        }

        // Final fallback: current working directory
        if ( bridge_chains_path.empty() || !std::filesystem::exists( bridge_chains_path ) )
        {
            bridge_chains_path = std::filesystem::current_path() / "bridge_chains_config.json";
        }

        return bridge_chains_path;
    }

    void GeniusNode::OnRpcEndpointsReady( std::vector<ChainContractPair> chains )
    {
        node_logger_->info( "GeniusNode: received {} chain(s) from provider — stored for catch-up scan",
                            chains.size() );
        catchup_chains_ = std::move( chains );

        // P2: If the catchup scan hasn't run yet (READY may have fired before
        // chains were populated), trigger it now that chains are available.
        if ( !catchup_scan_done_ && !catchup_chains_.empty() )
        {
            catchup_scan_done_ = true;
            node_logger_->info( "GeniusNode: chains arrived — triggering deferred catchup scan" );
            boost::asio::post( *io_,
                               [weak_self = weak_from_this()]
                               {
                                   if ( auto strong = weak_self.lock() )
                                   {
                                       strong->PerformStartupCatchupScan();
                                   }
                               } );
        }
    }

    void GeniusNode::InitializeAndStartBridge()
    {
        node_logger_->info( "InitializeAndStartBridge: thin orchestrator (D-01, D-03)" );

        // 1. Resolve config path (stays in GeniusNode per D-01)
        auto config_path = ResolveBridgeChainsConfigPath();
        node_logger_->info( "InitializeAndStartBridge: loading bridge chain config from {}",
                            config_path.string() );

        // 2. Construct provider
        rpc_endpoint_provider_ = std::make_unique<ChainRpcEndpointProvider>();

        // 3. Subscribe observers BEFORE post (D-03 ordering)
        rpc_endpoint_provider_->AddObserver( *this );
        if ( bridge_relayer_ )
        {
            rpc_endpoint_provider_->AddObserver( *bridge_relayer_ );
        }

        // 4. Post Initialize() to io_context — non-blocking
        boost::asio::post( *io_,
                           [weak_self = weak_from_this(),
                            config_path = std::move( config_path )]()
                           {
                               if ( auto strong = weak_self.lock() )
                               {
                                   auto &validator = strong->transaction_manager_->GetPublicChainInputValidator();
                                   strong->rpc_endpoint_provider_->Initialize( config_path, validator );
                               }
                           } );
    }

    void GeniusNode::PerformStartupCatchupScan()
    {
        node_logger_->info( "CatchUpScan: starting historical burn scan (D-20)" );

        if ( !transaction_manager_ )
        {
            node_logger_->warn( "CatchUpScan: transaction_manager_ not available — skipping" );
            return;
        }

        // D-11/D-12: catch-up scan queries BOTH v1 (BridgeSourceBurned) and v2
        // (BridgeOutInitiated) topic0 hashes.  EventFilter topics are
        // single-value-per-position (no OR-array support in serialization), so
        // two separate eth_getLogs calls are made per chain and the results are
        // merged with tx_hash deduplication.
        const std::string event_sig     = "BridgeSourceBurned(address,uint256,uint256,uint256,uint256,bytes)";
        auto              topic0_hash   = eth::abi::event_signature_hash( event_sig );
        std::string       topic0_hex    = rlp::base::parse::hex_bytes( topic0_hash.data(), topic0_hash.size() );

        // Bridge V2: bytes32 sgnsDestination + bool destinationYOdd (parity bit).
        const std::string event_sig_v2  = "BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool)";
        auto              topic0_hash_v2 = eth::abi::event_signature_hash( event_sig_v2 );
        std::string       topic0_hex_v2  = rlp::base::parse::hex_bytes( topic0_hash_v2.data(), topic0_hash_v2.size() );

        // D-20: Cap scan depth — default 10,000 blocks
        const uint64_t scan_depth = kBridgeCatchupScanDepth;

        size_t total_backfilled = 0;
        size_t total_skipped    = 0;
        size_t chains_scanned   = 0;

        // Iterate chains discovered via OnRpcEndpointsReady observer callback (D-02, D-03)
        // Uses catchup_chains_ populated by the observer — no hardcoded maps.
        auto &validator = transaction_manager_->GetPublicChainInputValidator();

        for ( const auto &chain_entry : catchup_chains_ )
        {
            auto rpc_url = validator.GetFirstRpcUrl( std::to_string( chain_entry.chain_id ) );
            if ( !rpc_url.has_value() )
            {
                node_logger_->debug( "CatchUpScan: no RPC endpoint for chain {} (id={}) — skipping",
                                     chain_entry.chain_name,
                                     chain_entry.chain_id );
                continue;
            }

            const std::string &contract_addr_str = chain_entry.contract_address;

            // RPC transport with 10-second timeout per T-05-13
            eth::rpc::RpcHttpTransportOptions opts;
            opts.timeout = std::chrono::seconds( 10 );
            eth::rpc::RpcHttpTransport transport( *rpc_url, opts );

            ++chains_scanned;

            // Parse contract address (eth::Address = rlp::Address = array<uint8_t, 20>)
            rlp::Address contract_addr{};
            if ( !rlp::base::parse::hex_array( contract_addr_str, contract_addr ) )
            {
                node_logger_->warn( "CatchUpScan: invalid bridge address {} for chain {}",
                                    contract_addr_str,
                                    chain_entry.chain_name );
                continue;
            }

            // Parse topic0 hex to Hash256 for EventFilter
            rlp::Hash256 topic0_hash256{};
            if ( !rlp::base::parse::hex_array( topic0_hex, topic0_hash256 ) )
            {
                node_logger_->warn( "CatchUpScan: invalid topic0 {} for chain {}", topic0_hex, chain_entry.chain_name );
                continue;
            }

            // Build EventFilter for eth_getLogs (v1 topic0)
            eth::EventFilter filter;
            filter.addresses.push_back( contract_addr );
            filter.topics.push_back( topic0_hash256 );

            // D-20: Query current block number to compute scan start
            // Scan from (current_block - scan_depth) to latest
            constexpr uint64_t kBlockNumberRequestId = 99;
            auto              block_number_req = eth::rpc::make_json_rpc_request( "eth_blockNumber",
                                                                                   boost::json::array{},
                                                                                   kBlockNumberRequestId );
            auto              block_number_resp = transport.call( block_number_req );
            uint64_t          current_block     = 0;

            if ( block_number_resp.has_value() )
            {
                auto parsed_block = eth::rpc::parse_block_number_response( *block_number_resp );
                if ( parsed_block.has_value() )
                {
                    current_block = *parsed_block;
                }
            }

            if ( current_block == 0 )
            {
                node_logger_->warn( "CatchUpScan: failed to query block number for chain {} — skipping",
                                    chain_entry.chain_name );
                continue;
            }

            // from_block: cap at scan_depth blocks from latest (D-20)
            // to_block: 0 = "latest"
            const uint64_t from_block = current_block > scan_depth ? current_block - scan_depth : 0;

            node_logger_->debug( "CatchUpScan: scanning chain {} from block {} to latest (current={}, depth={})",
                                 chain_entry.chain_name,
                                 from_block,
                                 current_block,
                                 scan_depth );

            // D-12: Shared tx_hash deduplication set across v1 and v2 query
            // results. A burn appearing in both queries (e.g. contract-version
            // overlap) is processed only once.
            std::set<std::string> seen_tx_hashes;

            // Helper: process one batch of logs (v1 or v2) through the dedup +
            // UTXO-check + MintFunds pipeline. `is_v2` selects the destination
            // construction path (D-13).
            auto process_logs = [&]( const std::vector<eth::rpc::RpcLog> &rpc_logs, bool is_v2 ) {
                // Insert burn UTXO as READY with UTXO_BRIDGE type (D-20)
                // MintFunds will later transition it to RESERVED → CONSUMED
                auto &utxo_mgr = account_->GetUTXOManager();

                for ( const auto &rpc_log : rpc_logs )
                {
                    std::string tx_hash_hex = rlp::base::parse::hex_bytes( rpc_log.tx_hash.data(),
                                                                           rpc_log.tx_hash.size() );

                    // Deduplicate across v1 and v2 query results (D-12).
                    if ( !seen_tx_hashes.insert( tx_hash_hex ).second )
                    {
                        ++total_skipped;
                        node_logger_->debug( "CatchUpScan: burn tx {} already seen this scan — skipping",
                                             tx_hash_hex );
                        continue;
                    }

                    // Parse tx_hash_hex to Hash256 for UTXO query
                    base::Hash256 burn_tx_hash;
                    if ( !rlp::base::parse::hex_array( tx_hash_hex, burn_tx_hash ) )
                    {
                        ++total_skipped;
                        continue;
                    }

                    // D-20: Check UTXO set (not in-memory TransactionManager state)
                    // Check if burn outpoint (tx_hash, output_idx=0) is already consumed or reserved
                    if ( utxo_mgr.IsOutPointConsumed( burn_tx_hash, 0 ) )
                    {
                        ++total_skipped;
                        node_logger_->debug( "CatchUpScan: burn tx {} already CONSUMED — skipping", tx_hash_hex );
                        continue;
                    }
                    if ( utxo_mgr.IsOutPointReserved( burn_tx_hash, 0 ) )
                    {
                        ++total_skipped;
                        node_logger_->debug( "CatchUpScan: burn tx {} already RESERVED — skipping", tx_hash_hex );
                        continue;
                    }

                    // D-13: For v2 logs, decompress the 32-byte X-only key from
                    // the event data before calling MintFunds. V1 logs keep the
                    // existing bare-bones empty destination (unchanged path).
                    std::string destination;
                    if ( is_v2 )
                    {
                        // BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool):
                        // sender is indexed (topics[1]); the remaining 6 params
                        // are ABI-encoded in log.data. Non-indexed layout:
                        //   [0] id (uint256) [1] amount (uint256)
                        //   [2] srcChainID (uint256) [3] destChainID (uint256)
                        //   [4] sgnsDestination (bytes32 — the X-only key)
                        //   [5] destinationYOdd (bool — Y parity: false=0x02, true=0x03)
                        static const std::string kEventSigV2 =
                            "BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool)";
                        const auto all_params = eth::cli::event_registry().params_for( kEventSigV2 );

                        std::vector<eth::abi::AbiParam> non_indexed_params;
                        non_indexed_params.reserve( all_params.size() );
                        for ( const auto &p : all_params )
                        {
                            if ( !p.indexed )
                            {
                                non_indexed_params.push_back( p );
                            }
                        }

                        auto decoded = eth::abi::decode_log_data( rpc_log.log.data, non_indexed_params );
                        // Expect 6 non-indexed params (id, amount, srcChainID,
                        // destChainID, sgnsDestination, destinationYOdd).
                        static constexpr size_t kExpectedV2DataParams = 6;
                        static constexpr size_t kSgnsDestinationIndex  = 4;
                        static constexpr size_t kDestinationYOddIndex  = 5;
                        if ( !decoded.has_value() || decoded.value().size() < kExpectedV2DataParams )
                        {
                            ++total_skipped;
                            node_logger_->warn( "CatchUpScan v2: failed to decode log data for tx {} — skipping",
                                                tx_hash_hex );
                            continue;
                        }

                        // sgnsDestination is bytes32 → codec::Hash256 variant.
                        if ( !std::holds_alternative<eth::codec::Hash256>(
                                decoded.value()[kSgnsDestinationIndex] ) )
                        {
                            ++total_skipped;
                            node_logger_->warn( "CatchUpScan v2: sgnsDestination not bytes32 for tx {} — skipping",
                                                tx_hash_hex );
                            continue;
                        }
                        const auto &x_bytes = std::get<eth::codec::Hash256>(
                            decoded.value()[kSgnsDestinationIndex] );

                        // destinationYOdd is bool.
                        if ( !std::holds_alternative<bool>( decoded.value()[kDestinationYOddIndex] ) )
                        {
                            ++total_skipped;
                            node_logger_->warn( "CatchUpScan v2: destinationYOdd not bool for tx {} — skipping",
                                                tx_hash_hex );
                            continue;
                        }
                        const bool destination_y_odd = std::get<bool>( decoded.value()[kDestinationYOddIndex] );

                        auto dest_opt = eth::DecompressXOnlyPubkey( x_bytes, destination_y_odd );
                        if ( !dest_opt.has_value() )
                        {
                            ++total_skipped;
                            node_logger_->warn( "CatchUpScan v2: X-only decompression failed for tx {} — skipping",
                                                tx_hash_hex );
                            continue;
                        }
                        destination = std::move( *dest_opt );
                    }

                    try
                    {
                        auto result = transaction_manager_->MintFunds(
                            0,           // amount — derived from burn event data on full decode
                            tx_hash_hex, // source-chain tx hash
                            std::to_string( chain_entry.chain_id ),
                            dev_config_.TokenID,
                            destination );

                        if ( result.has_value() )
                        {
                            ++total_backfilled;
                            node_logger_->info( "CatchUpScan: backfilled historical burn {} on chain {}",
                                                tx_hash_hex,
                                                chain_entry.chain_name );
                        }
                        else
                        {
                            node_logger_->debug( "CatchUpScan: MintFunds returned no value for tx {} — "
                                                 "likely already processed",
                                                 tx_hash_hex );
                            ++total_skipped;
                        }
                    }
                    catch ( const std::exception &e )
                    {
                        node_logger_->debug( "CatchUpScan: MintFunds threw for tx {}: {} — skipping",
                                             tx_hash_hex,
                                             e.what() );
                        ++total_skipped;
                    }
                }
            };

            // ── v1 query (BridgeSourceBurned) ───────────────────────────────
            auto v1_request  = eth::rpc::make_get_logs_request( filter, from_block, 0, 1 );
            auto v1_response = transport.call( v1_request );

            if ( !v1_response.has_value() )
            {
                // T-05-13: RPC timeout or failure — log and continue (best-effort).
                // A failed v1 query does not block the v2 query or other chains.
                node_logger_->warn( "CatchUpScan: v1 RPC call failed for chain {} (timeout/refused)",
                                    chain_entry.chain_name );
            }
            else
            {
                auto v1_logs = eth::rpc::parse_get_logs_response( *v1_response );
                if ( !v1_logs.has_value() )
                {
                    node_logger_->warn( "CatchUpScan: failed to parse v1 getLogs response for chain {}",
                                        chain_entry.chain_name );
                }
                else
                {
                    process_logs( v1_logs.value(), /*is_v2=*/false );
                }
            }

            // ── v2 query (BridgeOutInitiated) ───────────────────────────────
            // Same contract address + block range, v2 topic0 hash (D-12).
            rlp::Hash256 topic0_hash256_v2{};
            if ( !rlp::base::parse::hex_array( topic0_hex_v2, topic0_hash256_v2 ) )
            {
                node_logger_->warn( "CatchUpScan: invalid v2 topic0 {} for chain {}",
                                    topic0_hex_v2,
                                    chain_entry.chain_name );
            }
            else
            {
                eth::EventFilter filter_v2;
                filter_v2.addresses.push_back( contract_addr );
                filter_v2.topics.push_back( topic0_hash256_v2 );

                // Unique request id so v2 does not collide with v1 (id=1) or
                // blockNumber (id=99).
                constexpr uint64_t kV2LogsRequestId = 2;
                auto v2_request  = eth::rpc::make_get_logs_request( filter_v2, from_block, 0, kV2LogsRequestId );
                auto v2_response = transport.call( v2_request );

                if ( !v2_response.has_value() )
                {
                    node_logger_->warn( "CatchUpScan: v2 RPC call failed for chain {} (timeout/refused)",
                                        chain_entry.chain_name );
                }
                else
                {
                    auto v2_logs = eth::rpc::parse_get_logs_response( *v2_response );
                    if ( !v2_logs.has_value() )
                    {
                        node_logger_->warn( "CatchUpScan: failed to parse v2 getLogs response for chain {}",
                                            chain_entry.chain_name );
                    }
                    else
                    {
                        process_logs( v2_logs.value(), /*is_v2=*/true );
                    }
                }
            }
        }

        node_logger_->info( "CatchUpScan: scanned {} chains — {} historical burns backfilled, "
                            "{} skipped (already processed)",
                            chains_scanned,
                            total_backfilled,
                            total_skipped );
    }

    TransactionManager::TransactionStatus GeniusNode::GetTransactionStatus( const std::string &txId ) const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            node_logger_->error( "{}: Transactions not ready", __func__ );
            return TransactionManager::TransactionStatus::INVALID;
        }
        auto manager = manager_result.value();
        auto retval  = manager->GetOutgoingStatusByTxId( txId );
        if ( retval == TransactionManager::TransactionStatus::INVALID )
        {
            retval = manager->GetIncomingStatusByTxId( txId );
        }
        return retval;
    }

    void GeniusNode::TransactionStateChanged( TransactionManager::State old_state, TransactionManager::State new_state )
    {
        node_logger_->info( "Transaction Manager state changed from {} to {}", old_state, new_state );

        switch ( new_state )
        {
            case TransactionManager::State::READY:
                // D-20: Trigger startup catch-up scan once after CRDT sync completes.
                // Non-blocking — posted to io_ so the node proceeds normally.
                // P2: Require chains to be populated before marking the scan done.
                // Prevents permanent skip when READY fires before OnRpcEndpointsReady
                // populates catchup_chains_.
                if ( !catchup_scan_done_ && !catchup_chains_.empty() )
                {
                    catchup_scan_done_ = true;
                    boost::asio::post( *io_,
                                       [weak_self = weak_from_this()]
                                       {
                                           if ( auto strong = weak_self.lock() )
                                           {
                                               strong->PerformStartupCatchupScan();
                                           }
                                       } );
                }

                if ( processing_service_ == nullptr )
                {
                    StateTransition( NodeState::INITIALIZING_PROCESSING );
                }
                else if ( isprocessor_ )
                {
                    StartProcessing();
                }
                break;
            case TransactionManager::State::INITIALIZING:
            case TransactionManager::State::SYNCING:
                if ( isprocessor_ )
                {
                    StopProcessing();
                }
                break;
            case TransactionManager::State::CREATING:
            default:
                break;
        }
    }

    void GeniusNode::SetAuthorizedFullNodeAddress( const std::string &pub_address )
    {
        Blockchain::SetAuthorizedFullNodeAddress( pub_address );
        if ( blockchain_ )
        {
            blockchain_->Start();
        }
    }

    const std::string &GeniusNode::GetAuthorizedFullNodeAddress() const
    {
        return Blockchain::GetAuthorizedFullNodeAddress();
    }
}
