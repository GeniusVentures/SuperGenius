/**
 * @file       GeniusNode.cpp
 * @brief
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <memory>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "base/sgns_version.hpp"
#include "base/ScaledInteger.hpp"
#include "account/TokenAmount.hpp"
#include "account/GeniusNode.hpp"
#include "account/MigrationManager.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "upnp.hpp"
#include "processing/processing_imagesplit.hpp"
#include "processing/processing_tasksplit.hpp"
#include "processing/processing_subtask_enqueuer_impl.hpp"
#include "processing/processors/processing_processor_mnn_image.hpp"
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <thread>
#include <mutex>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/protocol/common/asio/asio_scheduler.hpp>

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
                                                 const char         *eth_private_key,
                                                 bool                autodht,
                                                 bool                isprocessor,
                                                 uint16_t            base_port,
                                                 bool                is_full_node,
                                                 bool                use_upnp )
    {
        auto instance = std::shared_ptr<GeniusNode>(
            new GeniusNode( dev_config, eth_private_key, autodht, isprocessor, base_port, is_full_node, use_upnp ) );

        instance->processing_service_ = std::make_shared<processing::ProcessingServiceImpl>(
            instance->pubsub_,
            MAX_NODES_COUNT,
            std::make_shared<processing::SubTaskEnqueuerImpl>( instance->task_queue_ ),
            instance->task_result_storage_,
            instance->processing_core_,
            [wptr( std::weak_ptr<GeniusNode>( instance ) )]( const std::string              &var,
                                                             const SGProcessing::TaskResult &taskresult )
            {
                if ( auto strong = wptr.lock() )
                {
                    strong->ProcessingDone( var, taskresult );
                }
            },
            [wptr( std::weak_ptr<GeniusNode>( instance ) )]( const std::string &var )
            {
                if ( auto strong = wptr.lock() )
                {
                    strong->ProcessingError( var );
                }
            },
            instance->account_->GetAddress() );
        instance->processing_service_->SetChannelListRequestTimeout( boost::posix_time::milliseconds( 3000 ) );

        instance->transaction_manager_->RegisterStateChangeCallback(
            [wptr( std::weak_ptr<GeniusNode>( instance ) )]( TransactionManager::State old_state,
                                                             TransactionManager::State new_state )
            {
                if ( auto strong = wptr.lock() )
                {
                    strong->TransactionStateChanged( old_state, new_state );
                }
            } );
        instance->transaction_manager_->Start();

        if ( instance->autodht_ )
        {
            instance->DHTInit();
        }
        if ( use_upnp )
        {
            instance->RefreshUPNP( instance->pubsubport_ );
        }

        instance->io_work_guard_.emplace( instance->io_->get_executor() );
        unsigned desired_threads = instance->io_thread_count_;
        if ( desired_threads == 0 )
        {
            desired_threads = GeniusNode::DEFAULT_IO_THREADS;
        }
        instance->io_threads_.reserve( desired_threads );
        for ( unsigned i = 0; i < desired_threads; ++i )
        {
            instance->io_threads_.emplace_back( [ctx = instance->io_]() { ctx->run(); } );
        }
        return instance;
    }

    GeniusNode::GeniusNode( const DevConfig_st &dev_config,
                            const char         *eth_private_key,
                            bool                autodht,
                            bool                isprocessor,
                            uint16_t            base_port,
                            bool                is_full_node,
                            bool                use_upnp ) :
        account_( GeniusAccount::New( dev_config.TokenID,
                                      std::make_shared<JSONSecureStorage>( dev_config.BaseWritePath ),
                                      eth_private_key,
                                      is_full_node ) ),
        io_( std::make_shared<boost::asio::io_context>() ),
        write_base_path_( dev_config.BaseWritePath ),
        autodht_( autodht ),
        isprocessor_( isprocessor ),
        dev_config_( dev_config ),
        processing_channel_topic_( std::string( PROCESSING_CHANNEL ) ),
        processing_grid_chanel_topic_( std::string( PROCESSING_GRID_CHANNEL ) ),
        m_lastApiCall( std::chrono::system_clock::now() - m_minApiCallInterval ),
        processing_callback_pool_( std::make_unique<boost::asio::thread_pool>( 1 ) )

    {
        // Rotate log files before initializing logging system
        rotateLogFiles( write_base_path_ );
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();

        if ( !InitLoggers( write_base_path_ ) )
        {
            throw std::runtime_error( "Could not configure loggers" );
        }

        node_logger_->info( sgns::version::SuperGeniusVersionText() );

        pubsubport_ = GenerateRandomPort( base_port, account_->GetAddress() );

        std::vector<std::string> addresses;
        std::string              lanip;
        std::string              wanip;
        if ( use_upnp )
        {
            // UPNP
            auto upnp = std::make_shared<upnp::UPNP>();

            if ( upnp->GetIGD() )
            {
                wanip = upnp->GetWanIP();
                lanip = upnp->GetLocalIP();
                node_logger_->info( "Wan IP: {}", wanip );
                node_logger_->info( "Lan IP: {}", lanip );

                bool        success = false;
                std::string owner;

                constexpr int max_attempts = 10;
                for ( int i = 0; i < max_attempts; ++i )
                {
                    int candidate_port = pubsubport_ + i;
                    if ( upnp->CheckIfPortInUse( candidate_port, "TCP", owner ) )
                    {
                        if ( owner == lanip )
                        {
                            node_logger_->info( "Port {} is already mapped by this device. Try using it.",
                                                candidate_port );
                            if ( upnp->OpenPort( candidate_port, candidate_port, "TCP", 3600 ) )
                            {
                                addresses.push_back( wanip );
                                success     = true;
                                pubsubport_ = candidate_port;
                                break;
                            }

                            node_logger_->error(
                                "Port {} is already mapped by this device. We tried using it, but could not. Will try other ports.",
                                candidate_port );
                            continue;
                        }
                        node_logger_->warn( "Port {} already in use by {}", candidate_port, owner );
                        continue;
                    }

                    if ( upnp->OpenPort( candidate_port, candidate_port, "TCP", 3600 ) )
                    {
                        node_logger_->info( "Successfully opened port {}", candidate_port );
                        addresses.push_back( wanip );
                        success     = true;
                        pubsubport_ = candidate_port;
                        break;
                    }
                    node_logger_->warn( "Failed to open port {}", candidate_port );
                }

                if ( !success )
                {
                    node_logger_->error( "Unable to open a usable UPnP port after {} attempts", max_attempts );
                }
            }
        }
        // Make a base58 out of our address
        std::string                tempaddress = account_->GetAddress();
        std::vector<unsigned char> inputBytes( tempaddress.begin(), tempaddress.end() );
        std::vector<unsigned char> hash( SHA256_DIGEST_LENGTH );
        SHA256( inputBytes.data(), inputBytes.size(), hash.data() );

        libp2p::protocol::kademlia::ContentId key( hash );
        auto                                  acc_cid = libp2p::multi::ContentIdentifierCodec::decode( key.data );
        auto maybe_base58 = libp2p::multi::ContentIdentifierCodec::toString( acc_cid.value() );
        if ( !maybe_base58 )
        {
            throw std::runtime_error( "We couldn't convert the account to base58" );
        }
        std::string base58key = maybe_base58.value();

        gnus_network_full_path_ = std::string( GNUS_NETWORK_PATH ) + version::GetNetAndVersionAppendix() + base58key;

        auto pubsubKeyPath = gnus_network_full_path_ + "/pubs_processor";

        //Set a pubsub config, use no signing because we can verify with proof and dag structure
        libp2p::protocol::gossip::Config config;
        config.echo_forward_mode       = false;
        config.sign_messages           = false;
        config.seen_cache_limit        = 10;
        config.heartbeat_interval_msec = std::chrono::milliseconds{ 500 };
        config.rw_timeout_msec         = std::chrono::seconds{ 30 };
        pubsub_                        = std::make_shared<ipfs_pubsub::GossipPubSub>(
            crdt::KeyPairFileStorage( write_base_path_ + pubsubKeyPath ).GetKeyPair().value(),
            config );
        auto pubs = pubsub_->Start( pubsubport_, {}, lanip, {} );
        account_->InitMessenger( pubsub_ );
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
        auto scheduler = std::make_shared<libp2p::protocol::AsioScheduler>( io_, libp2p::protocol::SchedulerConfig{} );
        auto generator = std::make_shared<ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
        auto graphsyncnetwork = std::make_shared<ipfs_lite::ipfs::graphsync::Network>( pubsub_->GetHost(), scheduler );

        auto migrationManager = sgns::MigrationManager::New( io_,              // ioContext
                                                             pubsub_,          // pubSub
                                                             graphsyncnetwork, // graphsync
                                                             scheduler,        // scheduler
                                                             generator,        // generator
                                                             write_base_path_, // writeBasePath
                                                             base58key         // base58key
        );

        auto migrationResult = migrationManager->Migrate();
        if ( migrationResult.has_error() )
        {
            throw std::runtime_error( std::string( "Database migration failed: " ) +
                                      migrationResult.error().message() );
        }

        auto global_db_ret = crdt::GlobalDB::New( io_,
                                                  write_base_path_ + gnus_network_full_path_,
                                                  pubsub_,
                                                  crdt::CrdtOptions::DefaultOptions(),
                                                  graphsyncnetwork,
                                                  scheduler,
                                                  generator );
        if ( global_db_ret.has_error() )
        {
            auto error = global_db_ret.error();
            throw std::runtime_error( error.message() );
        }
        tx_globaldb_ = std::move( global_db_ret.value() );
        tx_globaldb_->SetFullNode( is_full_node );
        tx_globaldb_->AddTopicName( processing_channel_topic_ );
        tx_globaldb_->AddListenTopic( processing_channel_topic_ );

        task_queue_ = std::make_shared<processing::ProcessingTaskQueueImpl>( tx_globaldb_, processing_channel_topic_ );
        processing_core_ = std::make_shared<processing::ProcessingCoreImpl>( tx_globaldb_, 1, dev_config.TokenID );
        processing_core_->RegisterProcessorFactory( "mnnimage",
                                                    [] { return std::make_unique<processing::MNN_Image>(); } );

        task_result_storage_ = std::make_shared<processing::SubTaskResultStorageImpl>( tx_globaldb_,
                                                                                       processing_channel_topic_ );

        transaction_manager_ = TransactionManager::New( tx_globaldb_,
                                                        io_,
                                                        account_,
                                                        std::make_shared<crypto::HasherImpl>(),
                                                        is_full_node );
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
            std::cout << "Logger Error" << std::endl;
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
        auto loggerAccountMessenger = ConfigureLogger( "AccountMessenger", logdir, spdlog::level::err );
        auto loggerGeniusAccount    = ConfigureLogger( "GeniusAccount", logdir, spdlog::level::err );
        auto loggerKeyPair          = ConfigureLogger( "KeyPairFileStorage", logdir, spdlog::level::err );
#else
        // Release mode
        node_logger_              = ConfigureLogger( "SuperGeniusNode", logdir, spdlog::level::trace );
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
        auto loggerAccountMessenger = ConfigureLogger( "AccountMessenger", logdir, spdlog::level::err );
        auto loggerGeniusAccount    = ConfigureLogger( "GeniusAccount", logdir, spdlog::level::err );
        auto loggerKeyPair          = ConfigureLogger( "KeyPairFileStorage", logdir, spdlog::level::err );
#endif

        // Logger initialization complete
        node_logger_->info( "Logger initialized successfully" );

        return true;
    }

    GeniusNode::~GeniusNode()
    {
        node_logger_->debug( "~GeniusNode CALLED" );

        if ( pubsub_ )
        {
            pubsub_->Stop(); // Stop activities of OtherClass
        }
        if ( io_ )
        {
            io_->stop(); // Stop our io_context
        }
        if ( io_work_guard_ )
        {
            io_work_guard_->reset();
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
        processing_service_->StopProcessing();
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
        libp2p::protocol::kademlia::ContentId key( hash );
        pubsub_->GetDHT()->Start();
        pubsub_->ProvideCID( key );

        auto cidtest = libp2p::multi::ContentIdentifierCodec::decode( key.data );

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

    outcome::result<std::string> GeniusNode::ProcessImage( const std::string &jsondata )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        BOOST_OUTCOME_TRYV2( auto &&, CheckProcessValidity( jsondata ) );

        auto funds = GetProcessCost( jsondata );
        if ( funds <= 0 )
        {
            return outcome::failure( Error::PROCESS_COST_ERROR );
        }

        if ( transaction_manager_->GetBalance() < funds )
        {
            return outcome::failure( Error::INSUFFICIENT_FUNDS );
        }

        SGProcessing::Task task;
        auto               uuidstring = generate_uuid_with_ipfs_id( pubsub_->GetHost()->getId().toBase58() );

        task.set_ipfs_block_id( uuidstring );
        task.set_json_data( jsondata );
        task.set_random_seed( 0 );
        task.set_results_channel( ( boost::format( "RESULT_CHANNEL_ID_%1%" ) % ( 1 ) ).str() );

        rapidjson::Document document;
        document.Parse( jsondata.c_str() );
        rapidjson::Value inputArray;

        inputArray = document["input"];

        processing::ProcessTaskSplitter  taskSplitter;
        std::list<SGProcessing::SubTask> subTasks;
        for ( const auto &input : inputArray.GetArray() )
        {
            size_t                                     nChunks = input["chunk_count"].GetInt();
            rapidjson::StringBuffer                    buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer( buffer );

            input.Accept( writer );
            std::string inputAsString = buffer.GetString();
            taskSplitter
                .SplitTask( task, subTasks, inputAsString, nChunks, false, pubsub_->GetHost()->getId().toBase58() );
        }
        auto cut = TokenAmount::ParseMinions( dev_config_.Cut );
        if ( !cut )
        {
            return outcome::failure( cut.error() );
        }

        OUTCOME_TRY(
            ( auto &&, result_pair ),
            transaction_manager_->HoldEscrow( funds, std::string( dev_config_.Addr ), cut.value(), uuidstring ) );

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

    outcome::result<void> GeniusNode::CheckProcessValidity( const std::string &jsondata )
    {
        rapidjson::Document document;
        document.Parse( jsondata.c_str() );

        if ( document.HasParseError() )
        {
            return outcome::failure( Error::INVALID_JSON );
        }

        // Check for required fields
        if ( !document.HasMember( "data" ) || !document["data"].IsObject() )
        {
            return outcome::failure( Error::PROCESS_INFO_MISSING );
        }

        if ( !document.HasMember( "model" ) || !document["model"].IsObject() )
        {
            return outcome::failure( Error::PROCESS_INFO_MISSING );
        }

        if ( !document.HasMember( "input" ) || !document["input"].IsArray() )
        {
            return outcome::failure( Error::PROCESS_INFO_MISSING );
        }

        // Extract and validate the model
        const auto &model = document["model"];
        if ( !model.HasMember( "name" ) || !model["name"].IsString() )
        {
            return outcome::failure( Error::PROCESS_INFO_MISSING );
        }

        std::string model_name      = model["name"].GetString();
        auto        processor_check = processing_core_->CheckRegisteredProcessor( model_name );
        if ( !processor_check )
        {
            return outcome::failure( Error::NO_PROCESSOR ); // Return the error if the processor is not registered
        }

        // Extract input array
        const auto &inputArray = document["input"];
        if ( inputArray.Size() == 0 )
        {
            return outcome::failure( Error::PROCESS_INFO_MISSING );
        }

        // Validate each input entry
        for ( auto &input : inputArray.GetArray() )
        {
            if ( !input.IsObject() )
            {
                return outcome::failure( Error::PROCESS_INFO_MISSING );
            }

            if ( !input.HasMember( "block_len" ) || !input["block_len"].IsUint64() )
            {
                return outcome::failure( Error::PROCESS_INFO_MISSING );
            }

            if ( !input.HasMember( "block_line_stride" ) || !input["block_line_stride"].IsUint64() )
            {
                return outcome::failure( Error::PROCESS_INFO_MISSING );
            }

            uint64_t block_len         = input["block_len"].GetUint64();
            uint64_t block_line_stride = input["block_line_stride"].GetUint64();

            // Ensure block_len is evenly divisible by block_line_stride
            if ( block_line_stride == 0 || ( block_len % block_line_stride ) != 0 )
            {
                return outcome::failure( Error::INVALID_BLOCK_PARAMETERS );
            }
        }

        return outcome::success();
    }

    outcome::result<uint64_t> GeniusNode::ParseBlockSize( const std::string &json_data )
    {
        node_logger_->info( "Received JSON data: {}", json_data );
        rapidjson::Document document;
        if ( document.Parse( json_data.c_str() ).HasParseError() )
        {
            node_logger_->error( "Invalid JSON data provided" );
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }

        rapidjson::Value inputArray;
        if ( document.HasMember( "input" ) && document["input"].IsArray() )
        {
            inputArray = document["input"];
        }
        else
        {
            node_logger_->error( "This JSON lacks inputs" );
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }

        uint64_t block_total_len = 0;
        for ( const auto &input : inputArray.GetArray() )
        {
            if ( input.HasMember( "block_len" ) && input["block_len"].IsUint64() )
            {
                uint64_t block_len  = input["block_len"].GetUint64();
                block_total_len    += block_len;
                node_logger_->info( "Block length (bytes): {}", block_len );
            }
            else
            {
                node_logger_->error( "Missing or invalid block_len in input" );
                return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
            }
        }

        node_logger_->trace( "Total block length: {}", block_total_len );
        return block_total_len;
    }

    uint64_t GeniusNode::GetProcessCost( const std::string &json_data )
    {
        auto blockLen = ParseBlockSize( json_data );
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

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::MintTokens( uint64_t           amount,
                                                                              const std::string &transaction_hash,
                                                                              const std::string &chainid,
                                                                              TokenID            tokenid,
                                                                              std::chrono::milliseconds timeout )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction manager not ready", __func__ );
            return outcome::failure( boost::system::error_code{} );
        }
        auto start_time = std::chrono::steady_clock::now();

        OUTCOME_TRY( auto &&tx_id, transaction_manager_->MintFunds( amount, transaction_hash, chainid, tokenid ) );

        auto mint_result = transaction_manager_->WaitForTransactionOutgoing( tx_id, timeout );

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();

        if ( mint_result != TransactionManager::TransactionStatus::CONFIRMED )
        {
            node_logger_->error( "Mint transaction {} failed after {} ms", tx_id, duration );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
        }

        node_logger_->debug( "Mint transaction {} completed in {} ms", tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::TransferFunds( uint64_t                  amount,
                                                                                 const std::string        &destination,
                                                                                 TokenID                   token_id,
                                                                                 std::chrono::milliseconds timeout )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto start_time = std::chrono::steady_clock::now();

        OUTCOME_TRY( auto &&tx_id, transaction_manager_->TransferFunds( amount, destination, token_id ) );

        auto transfer_result = transaction_manager_->WaitForTransactionOutgoing( tx_id, timeout );

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();

        if ( transfer_result != TransactionManager::TransactionStatus::CONFIRMED )
        {
            node_logger_->error( "TransferFunds transaction {} failed after {} ms", tx_id, duration );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
        }

        node_logger_->debug( "TransferFunds transaction {} completed in {} ms", tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::PayDev( uint64_t                  amount,
                                                                          TokenID                   token_id,
                                                                          std::chrono::milliseconds timeout )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto start_time = std::chrono::steady_clock::now();
        OUTCOME_TRY( auto &&tx_id, transaction_manager_->TransferFunds( amount, dev_config_.Addr, token_id ) );

        auto paydev_result = transaction_manager_->WaitForTransactionOutgoing( tx_id, timeout );

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();

        if ( paydev_result != TransactionManager::TransactionStatus::CONFIRMED )
        {
            node_logger_->error( "TransferFunds transaction {} failed after {} ms", tx_id, duration );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
        }

        node_logger_->debug( "TransferFunds transaction {} completed in {} ms", tx_id, duration );
        return std::make_pair( tx_id, duration );
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

        OUTCOME_TRY( auto &&tx_id,
                     transaction_manager_->PayEscrow( escrow_path, taskresult, std::move( crdt_transaction ) ) );

        auto payescrow_result = transaction_manager_->WaitForTransactionOutgoing( tx_id, timeout );

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
        return transaction_manager_->GetBalance();
    }

    uint64_t GeniusNode::GetBalance( const TokenID token_id )
    {
        return account_->GetBalance( token_id );
    }

    uint64_t GeniusNode::GetBalance( const std::string &address )
    {
        return account_->GetBalance( address );
    }

    uint64_t GeniusNode::GetBalance( const TokenID token_id, const std::string &address )
    {
        return account_->GetBalance( token_id, address );
    }

    void GeniusNode::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult )
    {
        boost::asio::post(
            *processing_callback_pool_,
            [weak_self( weak_from_this() ), task_id, taskresult]()
            {
                if ( auto strong = weak_self.lock() )
                {
                    strong->node_logger_->info( "[{}]{}: SUCCESS PROCESSING TASK {}",
                                                strong->account_->GetAddress().substr( 0, 8 ),
                                                __func__,
                                                task_id );
                    do
                    {
                        if ( strong->task_queue_->IsTaskCompleted( task_id ) )
                        {
                            strong->node_logger_->info( "[{}]{}: Task Already completed!",
                                                        strong->account_->GetAddress().substr( 0, 8 ),
                                                        __func__ );
                            break;
                        }
                        if ( strong->GetTransactionManagerState() != TransactionManager::State::READY )
                        {
                            strong->node_logger_->info( "[{}]{}: Transactions are not ready",
                                                        strong->account_->GetAddress().substr( 0, 8 ),
                                                        __func__ );
                            break;
                        }
                        strong->node_logger_->info( "[{}]{}: Transactions READY",
                                                    strong->account_->GetAddress().substr( 0, 8 ),
                                                    __func__ );
                        auto maybe_escrow_path = strong->task_queue_->GetTaskEscrow( task_id );
                        if ( maybe_escrow_path.has_failure() )
                        {
                            strong->node_logger_->info( "[{}]{}: No associated Escrow with the task id: {} ",
                                                        strong->account_->GetAddress().substr( 0, 8 ),
                                                        __func__,
                                                        task_id );
                            break;
                        }
                        auto complete_task_result = strong->task_queue_->CompleteTask( task_id, taskresult );
                        if ( complete_task_result.has_failure() )
                        {
                            strong->node_logger_->error( "[{}]{}: Unable to complete task: {} ",
                                                         strong->account_->GetAddress().substr( 0, 8 ),
                                                         __func__,
                                                         task_id );
                            break;
                        }
                        strong->node_logger_->info( "[{}]{}: Creating the payout transactions",
                                                    strong->account_->GetAddress().substr( 0, 8 ),
                                                    __func__ );
                        auto pay_result = strong->PayEscrow( maybe_escrow_path.value(),
                                                             taskresult,
                                                             std::move( complete_task_result.value() ) );
                        if ( pay_result.has_failure() )
                        {
                            strong->node_logger_->error( "[{}]{}: Escrow not paid for task: {} ",
                                                         strong->account_->GetAddress().substr( 0, 8 ),
                                                         __func__,
                                                         task_id );
                            break;
                        }
                        strong->node_logger_->info( "[{}]{}: Paid for task: {}",
                                                    strong->account_->GetAddress().substr( 0, 8 ),
                                                    __func__,
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

    void GeniusNode::PrintDataStore()
    {
        tx_globaldb_->PrintDataStore();
    }

    void GeniusNode::StopProcessing()
    {
        processing_service_->StopProcessing();
    }

    void GeniusNode::StartProcessing()
    {
        processing_service_->StartProcessing( processing_grid_chanel_topic_ );
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
        if ( !tokensToFetch.empty() && ( currentTime - m_lastApiCall ) >= m_minApiCallInterval )
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
        return transaction_manager_->WaitForTransactionOutgoing( txId, timeout );
    }

    // Wait for a transaction to be processed with a timeout
    TransactionManager::TransactionStatus GeniusNode::WaitForTransactionIncoming( const std::string        &txId,
                                                                                  std::chrono::milliseconds timeout )
    {
        return transaction_manager_->WaitForTransactionIncoming( txId, timeout );
    }

    TransactionManager::TransactionStatus GeniusNode::WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                                            std::chrono::milliseconds timeout )
    {
        return transaction_manager_->WaitForEscrowRelease( originalEscrowId, timeout );
    }

    TransactionManager::State GeniusNode::GetTransactionManagerState() const
    {
        return transaction_manager_->GetState();
    }

    void GeniusNode::SendTransactionAndProof( std::shared_ptr<IGeniusTransactions> tx, std::vector<uint8_t> proof )
    {
        transaction_manager_->EnqueueTransaction( std::make_pair( tx, proof ) );
    }

    void GeniusNode::ConfigureTransactionFilterTimeoutsMs( uint64_t timeframe_limit_ms, uint64_t mutability_window_ms )
    {
        transaction_manager_->SetTimeFrameToleranceMs( timeframe_limit_ms );
        transaction_manager_->SetMutabilityWindowMs( mutability_window_ms );
    }

    void GeniusNode::rotateLogFiles( const std::string &base_path )
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
        auto retval = transaction_manager_->GetOutgoingStatusByTxId( txId );
        if ( retval == TransactionManager::TransactionStatus::INVALID )
        {
            retval = transaction_manager_->GetIncomingStatusByTxId( txId );
        }
        return retval;
    }

    void GeniusNode::TransactionStateChanged( TransactionManager::State old_state, TransactionManager::State new_state )
    {
        node_logger_->info( "Transaction Manager State changed from {} to {}",
                            TransactionManager::StateToString( old_state ),
                            TransactionManager::StateToString( new_state ) );

        switch ( new_state )
        {
            case TransactionManager::State::READY:
                if ( isprocessor_ )
                {
                    StartProcessing();
                }
                break;
            case TransactionManager::State::INITIALIZING:
            case TransactionManager::State::SYNCHING:
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
}
