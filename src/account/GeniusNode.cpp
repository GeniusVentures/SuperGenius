/**
 * @file       GeniusNode.cpp
 * @brief
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>

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
    GeniusNode::GeniusNode( const DevConfig_st &dev_config,
                            const char         *eth_private_key,
                            bool                autodht,
                            bool                isprocessor,
                            uint16_t            base_port,
                            bool                is_full_node ) :
        account_( std::make_shared<GeniusAccount>( dev_config.TokenID, dev_config.BaseWritePath, eth_private_key ) ),
        io_( std::make_shared<boost::asio::io_context>() ),
        write_base_path_( dev_config.BaseWritePath ),
        autodht_( autodht ),
        isprocessor_( isprocessor ),
        dev_config_( dev_config ),
        processing_channel_topic_(
            ( boost::format( std::string( PROCESSING_CHANNEL ) ) % sgns::version::SuperGeniusVersionMajor() ).str() ),
        processing_grid_chanel_topic_(
            ( boost::format( std::string( PROCESSING_GRID_CHANNEL ) ) % sgns::version::SuperGeniusVersionMajor() )
                .str() ),
        m_lastApiCall( std::chrono::system_clock::now() - m_minApiCallInterval )

    {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            GetLoggingSystem( write_base_path_ ) ) );
        auto result    = logging_system->configure();
        if ( result.has_error )
        {
            throw std::runtime_error( "Could not configure logger" );
        }
        std::cout << "Log Result: " << result.message << std::endl;
        libp2p::log::setLoggingSystem( logging_system );
        libp2p::log::setLevelOfGroup( "SuperGeniusDemo", soralog::Level::ERROR_ );
        std::string logdir = "";
#ifndef SGNS_DEBUGLOGS
        logdir = write_base_path_ + "/sgnslog2.log";
#endif
        node_logger               = base::createLogger( "SuperGeniusNode", logdir );
        auto loggerGlobalDB       = base::createLogger( "GlobalDB", logdir );
        auto loggerDAGSyncer      = base::createLogger( "GraphsyncDAGSyncer", logdir );
        auto loggerGraphsync      = base::createLogger( "graphsync", logdir );
        auto loggerBroadcaster    = base::createLogger( "PubSubBroadcasterExt", logdir );
        auto loggerDataStore      = base::createLogger( "CrdtDatastore", logdir );
        auto loggerTransactions   = base::createLogger( "TransactionManager", logdir );
        auto loggerMigration      = base::createLogger( "MigrationManager", logdir );
        auto loggerMigrationStep  = base::createLogger( "MigrationStep", logdir );
        auto loggerQueue          = base::createLogger( "ProcessingTaskQueueImpl", logdir );
        auto loggerRocksDB        = base::createLogger( "rocksdb", logdir );
        auto logkad               = base::createLogger( "Kademlia", logdir );
        auto logNoise             = base::createLogger( "Noise", logdir );
        auto logProcessingEngine  = base::createLogger( "ProcessingEngine", logdir );
        auto loggerSubQueue       = base::createLogger( "ProcessingSubTaskQueueAccessorImpl", logdir );
        auto loggerProcServ       = base::createLogger( "ProcessingService", logdir );
        auto loggerProcqm         = base::createLogger( "ProcessingSubTaskQueueManager", logdir );
        auto loggerUPNP           = base::createLogger( "UPNP", logdir );
        auto loggerProcessingNode = base::createLogger( "ProcessingNode", logdir );
        auto loggerGossipPubsub   = base::createLogger( "GossipPubSub", logdir );
#ifdef SGNS_DEBUGLOGS
        node_logger->set_level( spdlog::level::err );
        loggerGlobalDB->set_level( spdlog::level::err );
        loggerDAGSyncer->set_level( spdlog::level::err );
        loggerGraphsync->set_level( spdlog::level::err );
        loggerBroadcaster->set_level( spdlog::level::err );
        loggerDataStore->set_level( spdlog::level::err );
        loggerTransactions->set_level( spdlog::level::debug );
        loggerMigration->set_level( spdlog::level::err );
        loggerMigrationStep->set_level( spdlog::level::err );
        loggerQueue->set_level( spdlog::level::err );
        loggerRocksDB->set_level( spdlog::level::err );
        logkad->set_level( spdlog::level::err );
        logNoise->set_level( spdlog::level::err );
        logProcessingEngine->set_level( spdlog::level::err );
        loggerSubQueue->set_level( spdlog::level::err );
        loggerProcServ->set_level( spdlog::level::err );
        loggerProcqm->set_level( spdlog::level::err );
        loggerUPNP->set_level( spdlog::level::err );
        loggerProcessingNode->set_level( spdlog::level::err );
        loggerGossipPubsub->set_level( spdlog::level::err );
#else
        node_logger->set_level( spdlog::level::err );
        loggerGlobalDB->set_level( spdlog::level::err );
        loggerDAGSyncer->set_level( spdlog::level::err );
        loggerGraphsync->set_level( spdlog::level::err );
        loggerBroadcaster->set_level( spdlog::level::err );
        loggerDataStore->set_level( spdlog::level::err );
        loggerTransactions->set_level( spdlog::level::err );
        loggerMigration->set_level( spdlog::level::err );
        loggerMigrationStep->set_level( spdlog::level::err );
        loggerQueue->set_level( spdlog::level::err );
        loggerRocksDB->set_level( spdlog::level::err );
        logkad->set_level( spdlog::level::err );
        logNoise->set_level( spdlog::level::err );
        logProcessingEngine->set_level( spdlog::level::err );
        loggerSubQueue->set_level( spdlog::level::err );
        loggerProcServ->set_level( spdlog::level::err );
        loggerProcqm->set_level( spdlog::level::err );
        loggerUPNP->set_level( spdlog::level::err );
        loggerProcessingNode->set_level( spdlog::level::err );
        loggerGossipPubsub->set_level( spdlog::level::err );
#endif
        node_logger->info( sgns::version::SuperGeniusVersionText() );

        auto tokenid = dev_config_.TokenID;

        auto pubsubport = GenerateRandomPort( base_port, account_->GetAddress() );

        std::vector<std::string> addresses;
        // UPNP
        auto        upnp = std::make_shared<upnp::UPNP>();
        std::string lanip;

        if ( upnp->GetIGD() )
        {
            std::string wanip = upnp->GetWanIP();
            lanip             = upnp->GetLocalIP();
            node_logger->info( "Wan IP: {}", wanip );
            node_logger->info( "Lan IP: {}", lanip );

            const int   max_attempts = 10;
            bool        success      = false;
            std::string owner;

            for ( int i = 0; i < max_attempts; ++i )
            {
                int candidate_port = pubsubport + i;
                if ( upnp->CheckIfPortInUse( candidate_port, "TCP", owner ) )
                {
                    if ( owner == lanip )
                    {
                        node_logger->info( "Port {} is already mapped by this device. Try using it.", candidate_port );
                        if ( upnp->OpenPort( candidate_port, candidate_port, "TCP", 3600 ) )
                        {
                            addresses.push_back( wanip );
                            success    = true;
                            pubsubport = candidate_port;
                            break;
                        }
                        node_logger->error(
                            "Port {} is already mapped by this device. We tried using it, but could not. Will try other ports.",
                            candidate_port );
                        continue;
                    }
                    else
                    {
                        node_logger->warn( "Port {} already in use by {}", candidate_port, owner );
                        continue;
                    }
                }

                if ( upnp->OpenPort( candidate_port, candidate_port, "TCP", 3600 ) )
                {
                    node_logger->info( "Successfully opened port {}", candidate_port );
                    addresses.push_back( wanip );
                    success    = true;
                    pubsubport = candidate_port;
                    break;
                }
                else
                {
                    node_logger->warn( "Failed to open port {}", candidate_port );
                }
            }

            if ( !success )
            {
                node_logger->error( "Unable to open a usable UPnP port after {} attempts", max_attempts );
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
            std::runtime_error( "We couldn't convert the account to base58" );
        }
        std::string base58key = maybe_base58.value();

        gnus_network_full_path_ = ( boost::format( std::string( GNUS_NETWORK_PATH ) ) %
                                    sgns::version::SuperGeniusVersionMajor() % base58key )
                                      .str();

        auto pubsubKeyPath = gnus_network_full_path_ + "/pubs_processor";

        pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>(
            crdt::KeyPairFileStorage( write_base_path_ + pubsubKeyPath ).GetKeyPair().value() );
        auto pubs = pubsub_->Start( pubsubport, {}, lanip, addresses );
        pubs.wait();
        auto scheduler = std::make_shared<libp2p::protocol::AsioScheduler>( io_, libp2p::protocol::SchedulerConfig{} );
        auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
        auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubsub_->GetHost(),
                                                                                             scheduler );

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

        global_db_ret = crdt::GlobalDB::New( io_,
                                             write_base_path_ + gnus_network_full_path_,
                                             pubsub_,
                                             crdt::CrdtOptions::DefaultOptions(),
                                             graphsyncnetwork,
                                             scheduler,
                                             generator,
                                             tx_globaldb_->GetDataStore() );

        if ( global_db_ret.has_error() )
        {
            auto error = global_db_ret.error();
            throw std::runtime_error( error.message() );
        }
        job_globaldb_ = std::move( global_db_ret.value() );

        task_queue_      = std::make_shared<processing::ProcessingTaskQueueImpl>( job_globaldb_ );
        processing_core_ = std::make_shared<processing::ProcessingCoreImpl>( job_globaldb_,
                                                                             1000000,
                                                                             1,
                                                                             dev_config.TokenID );
        processing_core_->RegisterProcessorFactory( "mnnimage",
                                                    [] { return std::make_unique<processing::MNN_Image>(); } );

        task_result_storage_ = std::make_shared<processing::SubTaskResultStorageImpl>( job_globaldb_ );
        processing_service_  = std::make_shared<processing::ProcessingServiceImpl>(
            pubsub_,                                                          //
            MAX_NODES_COUNT,                                                  //
            std::make_shared<processing::SubTaskEnqueuerImpl>( task_queue_ ), //
            std::make_shared<processing::ProcessSubTaskStateStorage>(),       //
            task_result_storage_,                                             //
            processing_core_,                                                 //
            [this]( const std::string &var, const SGProcessing::TaskResult &taskresult )
            { ProcessingDone( var, taskresult ); }, //
            [this]( const std::string &var ) { ProcessingError( var ); },
            account_->GetAddress() );
        processing_service_->SetChannelListRequestTimeout( boost::posix_time::milliseconds( 3000 ) );

        auto migrationManager = sgns::MigrationManager::New( tx_globaldb_,     // newDb
                                                             io_,              // ioContext
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

        job_globaldb_->AddBroadcastTopic( processing_channel_topic_ );
        job_globaldb_->AddListenTopic( processing_channel_topic_ );
        job_globaldb_->Start();

        transaction_manager_ = std::make_shared<TransactionManager>( tx_globaldb_,
                                                                     io_,
                                                                     account_,
                                                                     std::make_shared<crypto::HasherImpl>(),
                                                                     is_full_node );

        transaction_manager_->Start();
        if ( isprocessor_ )
        {
            processing_service_->StartProcessing( processing_grid_chanel_topic_ );
        }

        if ( autodht_ )
        {
            DHTInit();
        }
        RefreshUPNP( pubsubport );

        io_thread = std::thread( [this]() { io_->run(); } );
    }

    GeniusNode::~GeniusNode()
    {
        node_logger->debug( "~GeniusNode CALLED" );

        if ( pubsub_ )
        {
            pubsub_->Stop(); // Stop activities of OtherClass
        }
        if ( io_ )
        {
            io_->stop(); // Stop our io_context
        }
        if ( io_thread.joinable() )
        {
            io_thread.join();
        }
        stop_upnp = true;
        if ( upnp_thread.joinable() )
        {
            upnp_thread.join();
        }
        processing_service_->StopProcessing();
    }

    void GeniusNode::RefreshUPNP( int pubsubport )
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
                                    node_logger->error( "Failed to open port" );
                                }
                                else
                                {
                                    node_logger->info( "Open Ports Success pubsub: {} ", pubsubport );
                                }
                            }
                            else
                            {
                                node_logger->info( "No IGD" );
                            }
                        }
                        else
                        {
                            node_logger->info( "UPNP weak_ptr expired" );
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
        std::string                temp = processing_grid_chanel_topic_;
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
        node_logger->info( "CID Test:: {}", cidstring.value() );

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
        // size_t           nSubTasks = 1;
        rapidjson::Value inputArray;

        inputArray = document["input"];
        // nSubTasks  = inputArray.Size();

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

            //}
            // imageindex++;
        }
        auto cut = sgns::TokenAmount::ParseMinions( dev_config_.Cut );
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
            task_queue_->ResetAtomicTransaction();
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
        node_logger->info( "Received JSON data: {}", json_data );
        rapidjson::Document document;
        if ( document.Parse( json_data.c_str() ).HasParseError() )
        {
            node_logger->error( "Invalid JSON data provided" );
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }

        rapidjson::Value inputArray;
        if ( document.HasMember( "input" ) && document["input"].IsArray() )
        {
            inputArray = document["input"];
        }
        else
        {
            node_logger->error( "This JSON lacks inputs" );
            return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
        }

        uint64_t block_total_len = 0;
        for ( const auto &input : inputArray.GetArray() )
        {
            if ( input.HasMember( "block_len" ) && input["block_len"].IsUint64() )
            {
                uint64_t block_len  = input["block_len"].GetUint64();
                block_total_len    += block_len;
                node_logger->info( "Block length (bytes): {}", block_len );
            }
            else
            {
                node_logger->error( "Missing or invalid block_len in input" );
                return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
            }
        }

        node_logger->trace( "Total block length: {}", block_total_len );
        return block_total_len;
    }

    uint64_t GeniusNode::GetProcessCost( const std::string &json_data )
    {
        auto blockLen = ParseBlockSize( json_data );
        if ( !blockLen )
        {
            node_logger->error( "ParseBlockSize failed" );
            return 0;
        }
        node_logger->trace( "Parsed totalBytes: {}", blockLen.value() );

        auto maybeGnusPrice = GetGNUSPrice();
        if ( !maybeGnusPrice )
        {
            node_logger->error( "GetGNUSPrice failed" );
            return 0;
        }
        double gnusPrice = maybeGnusPrice.value();
        node_logger->trace( "Retrieved GNUS price (USD/genius): {}", gnusPrice );

        auto rawMinionsRes = TokenAmount::CalculateCostMinions( blockLen.value(), gnusPrice );
        if ( !rawMinionsRes )
        {
            node_logger->error( "TokenAmount::CalculateCostMinions failed" );
            return 0;
        }
        uint64_t rawMinions = rawMinionsRes.value();
        node_logger->trace( "Raw cost in minions: {}", rawMinions );

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

    std::string GetVersion( void )
    {
        return sgns::version::SuperGeniusVersionFullString();
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::MintTokens( uint64_t           amount,
                                                                              const std::string &transaction_hash,
                                                                              const std::string &chainid,
                                                                              TokenID            tokenid,
                                                                              std::chrono::milliseconds timeout )
    {
        auto start_time = std::chrono::steady_clock::now();

        OUTCOME_TRY( auto &&tx_id, transaction_manager_->MintFunds( amount, transaction_hash, chainid, tokenid ) );

        bool success = transaction_manager_->WaitForTransactionOutgoing( tx_id, timeout );

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();

        if ( !success )
        {
            node_logger->error( "Mint transaction {} timed out after {} ms", tx_id, duration );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
        }

        node_logger->debug( "Mint transaction {} completed in {} ms", tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::TransferFunds( uint64_t                  amount,
                                                                                 const std::string        &destination,
                                                                                 TokenID                   token_id,
                                                                                 std::chrono::milliseconds timeout )
    {
        auto start_time = std::chrono::steady_clock::now();

        OUTCOME_TRY( auto &&tx_id, transaction_manager_->TransferFunds( amount, destination, token_id ) );

        bool success = transaction_manager_->WaitForTransactionOutgoing( tx_id, timeout );

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();

        if ( !success )
        {
            node_logger->error( "TransferFunds transaction {} timed out after {} ms", tx_id, duration );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
        }

        node_logger->debug( "TransferFunds transaction {} completed in {} ms", tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::PayDev( uint64_t                  amount,
                                                                          TokenID                   token_id,
                                                                          std::chrono::milliseconds timeout )
    {
        auto start_time = std::chrono::steady_clock::now();
        OUTCOME_TRY( auto &&tx_id, transaction_manager_->TransferFunds( amount, dev_config_.Addr, token_id ) );

        bool success = transaction_manager_->WaitForTransactionOutgoing( tx_id, timeout );

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();

        if ( !success )
        {
            node_logger->error( "TransferFunds transaction {} timed out after {} ms", tx_id, duration );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
        }

        node_logger->debug( "TransferFunds transaction {} completed in {} ms", tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::PayEscrow(
        const std::string                       &escrow_path,
        const SGProcessing::TaskResult          &taskresult,
        std::shared_ptr<crdt::AtomicTransaction> crdt_transaction,
        std::chrono::milliseconds                timeout )
    {
        auto start_time = std::chrono::steady_clock::now();

        OUTCOME_TRY( auto &&tx_id,
                     transaction_manager_->PayEscrow( escrow_path, taskresult, std::move( crdt_transaction ) ) );

        bool success = WaitForTransactionOutgoing( tx_id, timeout );

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();

        if ( !success )
        {
            node_logger->error( "Pay escrow transaction {} timed out after {} ms", tx_id, duration );
            return outcome::failure( boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
        }

        node_logger->debug( "Pay escrow transaction {} completed in {} ms", tx_id, duration );
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

    void GeniusNode::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult )
    {
        node_logger->info( "[ {} ] SUCCESS PROCESSING TASK {}", account_->GetAddress(), task_id );
        do
        {
            if ( task_queue_->IsTaskCompleted( task_id ) )
            {
                node_logger->info( "Task Already completed!" );
                break;
            }

            auto maybe_escrow_path = task_queue_->GetTaskEscrow( task_id );
            if ( maybe_escrow_path.has_failure() )
            {
                node_logger->info( "No associated Escrow with the task id: {} ", task_id );
                break;
            }
            auto complete_task_result = task_queue_->CompleteTask( task_id, taskresult );
            if ( complete_task_result.has_failure() )
            {
                node_logger->error( "Unable to complete task: {} ", task_id );
                break;
            }
            auto pay_result = PayEscrow( maybe_escrow_path.value(),
                                         taskresult,
                                         std::move( complete_task_result.value() ) );
            if ( pay_result.has_failure() )
            {
                node_logger->error( "Invalid results for task: {} ", task_id );
                break;
            }

        } while ( 0 );
    }

    void GeniusNode::ProcessingError( const std::string &task_id )
    {
        node_logger->error( "[ {} ] ERROR PROCESSING SUBTASK ", account_->GetAddress(), task_id );
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
    bool GeniusNode::WaitForTransactionOutgoing( const std::string &txId, std::chrono::milliseconds timeout )
    {
        return transaction_manager_->WaitForTransactionOutgoing( txId, timeout );
    }

    // Wait for a transaction to be processed with a timeout
    bool GeniusNode::WaitForTransactionIncoming( const std::string &txId, std::chrono::milliseconds timeout )
    {
        return transaction_manager_->WaitForTransactionIncoming( txId, timeout );
    }

    bool GeniusNode::WaitForEscrowRelease( const std::string &originalEscrowId, std::chrono::milliseconds timeout )
    {
        return transaction_manager_->WaitForEscrowRelease( originalEscrowId, timeout );
    }

    void GeniusNode::SendTransactionAndProof( std::shared_ptr<IGeniusTransactions> tx, std::vector<uint8_t> proof )
    {
        transaction_manager_->EnqueueTransaction( std::make_pair( tx, proof ) );
    }
}
