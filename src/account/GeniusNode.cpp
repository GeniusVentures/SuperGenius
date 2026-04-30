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

#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/uuid/uuid.hpp>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
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
#include "account/MigrationManager.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "upnp.hpp"
#include "processing/processing_tasksplit.hpp"
#include "processing/processing_subtask_enqueuer_impl.hpp"
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
        auto instance = std::shared_ptr<GeniusNode>(
            new GeniusNode( dev_config,
                            GeniusAccount::New( dev_config.TokenID, dev_config.BaseWritePath, is_full_node ),
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
        auto instance = std::shared_ptr<GeniusNode>( new GeniusNode(
            dev_config,
            GeniusAccount::New( dev_config.TokenID, eth_private_key, dev_config.BaseWritePath, is_full_node ),
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
        auto loggerMigration      = ConfigureLogger( "MigrationManager", logdir, spdlog::level::debug );
        auto loggerMigrationStep  = ConfigureLogger( "MigrationStep", logdir, spdlog::level::debug );
        auto loggerQueue          = ConfigureLogger( "ProcessingTaskQueueImpl", logdir, spdlog::level::err );
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
        auto loggerAccountMessenger  = ConfigureLogger( "AccountMessenger", logdir, spdlog::level::err );
        auto loggerGeniusAccount     = ConfigureLogger( "GeniusAccount", logdir, spdlog::level::err );
        auto loggerKeyPair           = ConfigureLogger( "KeyPairFileStorage", logdir, spdlog::level::err );
        auto loggerBlockchain        = ConfigureLogger( "Blockchain", logdir, spdlog::level::err );
        auto loggerValidator         = ConfigureLogger( "ValidatorRegistry", logdir, spdlog::level::trace );
        auto loggerProcMgr           = ConfigureLogger( "SGProcessingManager", logdir, spdlog::level::err );
        auto loggerProcessor         = ConfigureLogger( "SGProcessor", logdir, spdlog::level::err );
        auto loggerCrdtCallback      = ConfigureLogger( "CRDTCallbackManager", logdir, spdlog::level::err );
        auto loggerCoinPrices        = ConfigureLogger( "CoinPrices", logdir, spdlog::level::err );
        auto loggerUTXOManager       = ConfigureLogger( "UTXOManager", logdir, spdlog::level::err );
        auto loggerConsensusManager  = ConfigureLogger( "ConsensusManager", logdir, spdlog::level::trace );
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
        auto loggerQueue          = ConfigureLogger( "ProcessingTaskQueueImpl", logdir, spdlog::level::err );
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
        auto loggerAccountMessenger  = ConfigureLogger( "AccountMessenger", logdir, spdlog::level::err );
        auto loggerGeniusAccount     = ConfigureLogger( "GeniusAccount", logdir, spdlog::level::err );
        auto loggerKeyPair           = ConfigureLogger( "KeyPairFileStorage", logdir, spdlog::level::err );
        auto loggerBlockchain        = ConfigureLogger( "Blockchain", logdir, spdlog::level::err );
        auto loggerValidator         = ConfigureLogger( "ValidatorRegistry", logdir, spdlog::level::err );
        auto loggerProcMgr           = ConfigureLogger( "SGProcessingManager", logdir, spdlog::level::err );
        auto loggerProcessor         = ConfigureLogger( "SGProcessor", logdir, spdlog::level::err );
        auto loggerCrdtCallback      = ConfigureLogger( "CRDTCallbackManager", logdir, spdlog::level::err );
        auto loggerCoinPrices        = ConfigureLogger( "CoinPrices", logdir, spdlog::level::err );
        auto loggerUTXOManager       = ConfigureLogger( "UTXOManager", logdir, spdlog::level::err );
        auto loggerConsensusManager  = ConfigureLogger( "ConsensusManager", logdir, spdlog::level::err );

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
                (void)InitUPNP(); // Ignore UPNP init result for now
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

        task_queue_ = std::make_shared<processing::ProcessingTaskQueueImpl>( tx_globaldb_, processing_channel_topic_ );
        processing_core_ = std::make_shared<processing::ProcessingCoreImpl>( tx_globaldb_, 1, dev_config_.TokenID );

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
            return std::errc::address_not_available;
        }

        ResetProcessingMembers();

        this->transaction_manager_->Stop();
        this->transaction_manager_.reset();

        BOOST_OUTCOME_TRY( this->blockchain_->Stop() );
        this->blockchain_.reset();

        this->tx_globaldb_.reset();

        this->account_.swap( account );
        account.reset();

        this->BeginDBInitialization();

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

        auto [tx_id, escrow_data_pair] = result_pair;

        auto [escrow_path, escrow_data] = escrow_data_pair;

        task.set_escrow_path( escrow_path );

        auto enqueue_task_return = task_queue_->EnqueueTask( task, subTasks );
        if ( enqueue_task_return.has_failure() )
        {
            return outcome::failure( Error::DATABASE_WRITE_ERROR );
        }
        auto send_escrow_return = task_queue_->SendEscrow( escrow_path, std::move( escrow_data ) );
        if ( send_escrow_return.has_failure() )
        {
            task_queue_->ResetAtomicTransaction();
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
        boost::asio::post(
            *processing_callback_pool_,
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
                        auto maybe_escrow_path = strong->task_queue_->GetTaskEscrow( task_id );
                        if ( maybe_escrow_path.has_failure() )
                        {
                            strong->node_logger_->info( "[{}]{}: No associated Escrow with the task id: {} ",
                                                        strong->account_->GetAddress().substr( 0, 8 ),
                                                        FUNC,
                                                        task_id );
                            break;
                        }
                        auto complete_task_result = strong->task_queue_->CompleteTask( task_id, taskresult );
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
                        auto pay_result = strong->PayEscrow( maybe_escrow_path.value(),
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

    TransactionManager::State GeniusNode::GetTransactionManagerState() const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return TransactionManager::State::CREATING;
        }
        return manager_result.value()->GetState();
    }

    void GeniusNode::SendTransactionAndProof( std::shared_ptr<IGeniusTransactions> tx, std::vector<uint8_t> proof )
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
