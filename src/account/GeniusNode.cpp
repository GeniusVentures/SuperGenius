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
#include "base/fixed_point.hpp"
#include "base/sgns_version.hpp"
#include "account/GeniusNode.hpp"
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
                            uint16_t            base_port ) :
        account_( std::make_shared<GeniusAccount>( static_cast<uint8_t>( dev_config.TokenID ),
                                                   dev_config.BaseWritePath,
                                                   eth_private_key ) ),
        io_( std::make_shared<boost::asio::io_context>() ),
        write_base_path_( dev_config.BaseWritePath ),
        autodht_( autodht ),
        isprocessor_( isprocessor ),
        dev_config_( dev_config ),
        processing_channel_topic_( (boost::format( std::string( PROCESSING_CHANNEL ) ) %
                                   sgns::version::SuperGeniusVersionMajor()).str() ),
        processing_grid_chanel_topic_( (boost::format( std::string( PROCESSING_GRID_CHANNEL ) ) %
                                       sgns::version::SuperGeniusVersionMajor()).str() ),
        m_lastApiCall( std::chrono::system_clock::now() - m_minApiCallInterval )

    //coinprices_(std::make_shared<CoinGeckoPriceRetriever>(io_))
    {
        //For some reason if this isn't initialized like this, it ends up completely wrong. 
        m_lastApiCall = std::chrono::system_clock::now() - m_minApiCallInterval;
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
        node_logger               = base::createLogger( "SuperGeniusDemo", logdir );
        auto loggerGlobalDB       = base::createLogger( "GlobalDB", logdir );
        auto loggerDAGSyncer      = base::createLogger( "GraphsyncDAGSyncer", logdir );
        auto loggerGraphsync      = base::createLogger( "graphsync", logdir );
        auto loggerBroadcaster    = base::createLogger( "PubSubBroadcasterExt", logdir );
        auto loggerDataStore      = base::createLogger( "CrdtDatastore", logdir );
        auto loggerTransactions   = base::createLogger( "TransactionManager", logdir );
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
        node_logger->set_level( spdlog::level::debug );
        loggerGlobalDB->set_level( spdlog::level::debug );
        loggerDAGSyncer->set_level( spdlog::level::err );
        loggerGraphsync->set_level( spdlog::level::err );
        loggerBroadcaster->set_level( spdlog::level::debug );
        loggerDataStore->set_level( spdlog::level::debug );
        loggerTransactions->set_level( spdlog::level::debug );
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
        node_logger->set_level( spdlog::level::debug );
        loggerGlobalDB->set_level( spdlog::level::debug );
        loggerDAGSyncer->set_level( spdlog::level::err );
        loggerGraphsync->set_level( spdlog::level::err );
        loggerBroadcaster->set_level( spdlog::level::debug );
        loggerDataStore->set_level( spdlog::level::debug );
        loggerTransactions->set_level( spdlog::level::debug );
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

        auto pubsubport    = GenerateRandomPort( base_port, account_->GetAddress() + std::to_string( tokenid ) );
        auto graphsyncport = pubsubport + 10;

        std::vector<std::string> addresses;
        //UPNP
        auto        upnp = std::make_shared<upnp::UPNP>();
        std::string lanip;
        if ( upnp->GetIGD() )
        {
            auto openedPort  = upnp->OpenPort( pubsubport, pubsubport, "TCP", 3600 );
            auto openedPort2 = upnp->OpenPort( graphsyncport, graphsyncport, "TCP", 3600 );
            auto wanip       = upnp->GetWanIP();
            lanip            = upnp->GetLocalIP();
            node_logger->info( "Wan IP: {}", wanip );
            node_logger->info( "Lan IP: {}", lanip );
            addresses.push_back( wanip );
            if ( !openedPort || !openedPort2 )
            {
                node_logger->error( "Failed to open port" );
            }
            else
            {
                node_logger->info( "Open Port Success" );
            }
        }
        //Make a base58 out of our address
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
        pubsub_->Start(
            pubsubport,
            { "/dns4/sg-fullnode-1.gnus.ai/tcp/40052/p2p/12D3KooWBqcxStdb4f9s4zT3bQiTDXYB56VJ77Rt7eEdjw4kXTwi" },
            lanip,
            addresses );

        globaldb_ = std::make_shared<crdt::GlobalDB>( io_,
                                                      write_base_path_ + gnus_network_full_path_,
                                                      graphsyncport,
                                                      std::vector<std::string>{},
                                                      pubsub_ );
        globaldb_->AddBroadcastTopic( processing_channel_topic_ );

        auto global_db_init_result = globaldb_->Init( crdt::CrdtOptions::DefaultOptions() );
        if ( global_db_init_result.has_error() )
        {
            auto error = global_db_init_result.error();
            throw std::runtime_error( error.message() );
            return;
        }

        task_queue_      = std::make_shared<processing::ProcessingTaskQueueImpl>( globaldb_ );
        processing_core_ = std::make_shared<processing::ProcessingCoreImpl>( globaldb_, 1000000, 1 );
        processing_core_->RegisterProcessorFactory( "mnnimage",
                                                    [] { return std::make_unique<processing::MNN_Image>(); } );

        task_result_storage_ = std::make_shared<processing::SubTaskResultStorageImpl>( globaldb_ );
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

        transaction_manager_ = std::make_shared<TransactionManager>( globaldb_,
                                                                     io_,
                                                                     account_,
                                                                     std::make_shared<crypto::HasherImpl>(),
                                                                     write_base_path_ + gnus_network_full_path_,
                                                                     pubsub_,
                                                                     upnp,
                                                                     graphsyncport );

        transaction_manager_->Start();
        if ( isprocessor_ )
        {
            processing_service_->StartProcessing( processing_grid_chanel_topic_ );
        }

        if ( autodht_ )
        {
            DHTInit();
        }
        RefreshUPNP( pubsubport, graphsyncport );

        io_thread = std::thread( [this]() { io_->run(); } );
    }

    GeniusNode::~GeniusNode()
    {
        if ( io_ )
        {
            io_->stop(); // Stop our io_context
        }
        if ( pubsub_ )
        {
            pubsub_->Stop(); // Stop activities of OtherClass
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
    }

    void GeniusNode::RefreshUPNP( int pubsubport, int graphsyncport )
    {
        if ( upnp_thread.joinable() )
        {
            stop_upnp = true;   // Signal the existing thread to stop
            upnp_thread.join(); // Wait for it to finish
        }

        stop_upnp = false; // Reset the stop flag for the new thread

        upnp_thread = std::thread(
            [this, pubsubport, graphsyncport]()
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
                                auto openedPort  = upnp->OpenPort( pubsubport, pubsubport, "TCP", 3600 );
                                auto openedPort2 = upnp->OpenPort( graphsyncport, graphsyncport, "TCP", 3600 );
                                if ( !openedPort || !openedPort2 )
                                {
                                    node_logger->error( "Failed to open port" );
                                }
                                else
                                {
                                    node_logger->info( "Open Ports Success pubsub: {} graphsync:{}",
                                                       pubsubport,
                                                       graphsyncport );
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
        //Provide CID
        libp2p::protocol::kademlia::ContentId key( hash );
        pubsub_->GetDHT()->Start();
        pubsub_->ProvideCID( key );

        auto cidtest = libp2p::multi::ContentIdentifierCodec::decode( key.data );

        auto cidstring = libp2p::multi::ContentIdentifierCodec::toString( cidtest.value() );
        node_logger->info( "CID Test:: {}", cidstring.value() );

        //Also Find providers
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
        //size_t           nSubTasks = 1;
        rapidjson::Value inputArray;

        inputArray = document["input"];
        //nSubTasks  = inputArray.Size();

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
            //imageindex++;
        }
        auto cut = sgns::fixed_point::fromString( dev_config_.Cut );
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

    uint64_t GeniusNode::GetProcessCost( const std::string &json_data )
    {
        uint64_t costMinions = 0;
        node_logger->info( "Received JSON data: {}", json_data );

        // Parse JSON using RapidJSON
        rapidjson::Document document;
        if ( document.Parse( json_data.c_str() ).HasParseError() )
        {
            node_logger->error( "Invalid JSON data provided" );
            return 0;
        }

        // "block_len" represents the number of bytes processed.
        rapidjson::Value inputArray;
        if ( document.HasMember( "input" ) && document["input"].IsArray() )
        {
            inputArray = document["input"];
        }
        else
        {
            node_logger->error( "This JSON lacks inputs" );
            return 0;
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
                return 0;
            }
        }
        // Get current GNUS price (USD per GNUS token)
        auto maybe_gnusPrice = GetGNUSPrice(); // e.g., 3.65463 USD per GNUS
        if ( !maybe_gnusPrice )
        {
            return 0;
        }
        auto gnusPrice = maybe_gnusPrice.value();

        // Using the assumption: 20 FLOPs per byte and each FLOP costs 5e-15 USD,
        // the cost per byte in USD is: 20 * 5e-15 = 1e-13 USD.
        // Converting this to a fixed-point constant with 9 decimals:
        //    1e-13 USD/byte * 1e9 = 1e-4, i.e., 0.0001, and in fixed point with 9 decimals, that's 100000.
        uint64_t fixed_cost_per_byte = 100000ULL; // represents 0.0001 in fixed point (precision 9)

        // Calculate the raw cost in minions in fixed point: (block_total_len * fixed_cost_per_byte)
        auto raw_cost_result = sgns::fixed_point::multiply( block_total_len, fixed_cost_per_byte, 9 );
        if ( !raw_cost_result )
        {
            node_logger->error( "Fixed-point multiplication error" );
            return 0;
        }
        uint64_t raw_cost = raw_cost_result.value();

        // Convert GNUS price to fixed-point representation with precision 9:
        uint64_t gnus_price_fixed = static_cast<uint64_t>( std::round( gnusPrice * 1e9 ) );

        // Now, the cost in minions (in fixed point) is raw_cost divided by gnus_price_fixed:
        auto cost_result = sgns::fixed_point::divide( raw_cost, gnus_price_fixed, 9 );
        if ( !cost_result )
        {
            node_logger->info( "Fixed-point division error" );
            return 0;
        }
        costMinions = cost_result.value();

        // Ensure at least one minion is charged.
        return std::max( costMinions, static_cast<uint64_t>( 1 ) );
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
                                                                              const std::string &tokenid,
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
                                                                                 std::chrono::milliseconds timeout )
    {
        auto start_time = std::chrono::steady_clock::now();

        OUTCOME_TRY( auto &&tx_id, transaction_manager_->TransferFunds( amount, destination ) );

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
                                                                                 std::chrono::milliseconds timeout )
    {
        auto start_time = std::chrono::steady_clock::now();
        OUTCOME_TRY( auto &&tx_id, transaction_manager_->TransferFunds( amount, dev_config_.Addr ) );

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

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::PayEscrow( const std::string &escrow_path,
                                                                             const SGProcessing::TaskResult &taskresult,
                                                                             std::chrono::milliseconds       timeout )
    {
        auto start_time = std::chrono::steady_clock::now();

        OUTCOME_TRY( auto &&tx_id, transaction_manager_->PayEscrow( escrow_path, taskresult ) );

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
            auto pay_result = PayEscrow( maybe_escrow_path.value(), taskresult );
            if ( pay_result.has_failure() )
            {
                node_logger->error( "Invalid results for task: {} ", task_id );
                break;
            }

            if ( !task_queue_->CompleteTask( task_id, taskresult ) )
            {
                node_logger->error( "Unable to complete task: {} ", task_id );
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
        globaldb_->PrintDataStore();
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

    std::string GeniusNode::FormatTokens( uint64_t amount )
    {
        return sgns::fixed_point::toString( amount );
    }

    outcome::result<uint64_t> GeniusNode::ParseTokens( const std::string &str )
    {
        return sgns::fixed_point::fromString( str );
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

    bool GeniusNode::WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                                   std::chrono::milliseconds timeout )
    {
        return transaction_manager_->WaitForEscrowRelease( originalEscrowId, timeout );
    }
}
