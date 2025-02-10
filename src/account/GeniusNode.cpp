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
#include "base/util.hpp"
#include "base/fixed_point.hpp"
#include "account/GeniusNode.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "FileManager.hpp"
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
        dev_config_( dev_config )
    {
        logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            GetLoggingSystem() ) );
        auto result    = logging_system->configure();
        if ( result.has_error )
        {
            throw std::runtime_error( "Could not configure logger" );
        }
        std::cout << "Log Result: " << result.message << std::endl;
        libp2p::log::setLoggingSystem( logging_system );
        libp2p::log::setLevelOfGroup( "SuperGeniusDemo", soralog::Level::ERROR_ );

        auto loggerGlobalDB = base::createLogger( "GlobalDB" );
        loggerGlobalDB->set_level( spdlog::level::off );

        auto loggerDAGSyncer = base::createLogger( "GraphsyncDAGSyncer" );
        loggerDAGSyncer->set_level( spdlog::level::off );

        auto loggerBroadcaster = base::createLogger( "PubSubBroadcasterExt" );
        loggerBroadcaster->set_level( spdlog::level::off );

        auto loggerDataStore = base::createLogger( "CrdtDatastore" );
        loggerDataStore->set_level( spdlog::level::off );

        auto loggerTransactions = base::createLogger( "TransactionManager" );
        loggerTransactions->set_level( spdlog::level::off );

        auto loggerQueue = base::createLogger( "ProcessingTaskQueueImpl" );
        loggerQueue->set_level( spdlog::level::off );

        auto loggerRocksDB = base::createLogger( "rocksdb" );
        loggerRocksDB->set_level( spdlog::level::off );


        auto logkad = base::createLogger( "Kademlia" );
        logkad->set_level( spdlog::level::off );

        auto logNoise = base::createLogger( "Noise" );
        logNoise->set_level( spdlog::level::off );
        auto logProcessingEngine = base::createLogger( "ProcessingEngine" );
        logProcessingEngine->set_level( spdlog::level::debug );

        auto loggerSubQueue = base::createLogger( "ProcessingSubTaskQueueAccessorImpl" );
        loggerSubQueue->set_level( spdlog::level::off );
        auto loggerProcServ = base::createLogger( "ProcessingService" );
        loggerProcServ->set_level( spdlog::level::off );

        auto loggerProcqm = base::createLogger( "ProcessingSubTaskQueueManager" );
        loggerProcqm->set_level( spdlog::level::off );
        auto tokenid = dev_config_.TokenID;

        auto pubsubport    = GenerateRandomPort( base_port,
                                              account_->GetAddress<std::string>() + std::to_string( tokenid ) );
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
            std::cout << "Wan IP: " << wanip << std::endl;
            std::cout << "Lan IP: " << lanip << std::endl;
            addresses.push_back( wanip );
            if ( !openedPort || !openedPort2 )
            {
                std::cerr << "Failed to open port" << std::endl;
            }
            else
            {
                std::cout << "Open Port Success" << std::endl;
            }
        }

        auto pubsubKeyPath =
            ( boost::format( "SuperGNUSNode.TestNet.1a.03.%s/pubs_processor" ) % account_->GetAddress<std::string>() ).str();

        pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>(
            crdt::KeyPairFileStorage( write_base_path_ + pubsubKeyPath ).GetKeyPair().value() );
        pubsub_->Start(
            pubsubport,
            { "/dns4/sg-fullnode-1.gnus.ai/tcp/40052/p2p/12D3KooWBqcxStdb4f9s4zT3bQiTDXYB56VJ77Rt7eEdjw4kXTwi" },
            lanip,
            addresses );

        globaldb_ = std::make_shared<crdt::GlobalDB>(
            io_,
            ( boost::format( write_base_path_ + "SuperGNUSNode.TestNet.1a.03.%s" ) % account_->GetAddress<std::string>() )
                .str(),
            graphsyncport,
            std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_, std::string( PROCESSING_CHANNEL ) ) );

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
            account_->GetAddress<std::string>() );
        processing_service_->SetChannelListRequestTimeout( boost::posix_time::milliseconds( 3000 ) );

        transaction_manager_ = std::make_shared<TransactionManager>(
            globaldb_,
            io_,
            account_,
            std::make_shared<crypto::HasherImpl>(),
            ( boost::format( write_base_path_ + "SuperGNUSNode.TestNet.1a.03.%s" ) %
              account_->GetAddress<std::string>() )
                .str(),
            pubsub_,
            upnp,
            graphsyncport );

        transaction_manager_->Start();
        if ( isprocessor_ )
        {
            processing_service_->StartProcessing( std::string( PROCESSING_GRID_CHANNEL ) );
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

                while ( !stop_upnp )
                {
                    if ( std::chrono::steady_clock::now() >= next_refresh_time )
                    {
                        // Refresh UPnP mappings
                        auto upnp = std::make_shared<upnp::UPNP>();
                        if ( upnp->GetIGD() )
                        {
                            auto openedPort  = upnp->OpenPort( pubsubport, pubsubport, "TCP", 3600 );
                            auto openedPort2 = upnp->OpenPort( graphsyncport, graphsyncport, "TCP", 3600 );
                            if ( !openedPort || !openedPort2 )
                            {
                                std::cerr << "Failed to open port" << std::endl;
                            }
                            else
                            {
                                std::cout << "Open Port Success" << std::endl;
                            }
                        }
                        else
                        {
                            std::cout << "No IGD" << std::endl;
                        }

                        // Update the next refresh time
                        next_refresh_time = std::chrono::steady_clock::now() + std::chrono::minutes( 60 );
                    }

                    // Sleep briefly to avoid busy-waiting
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
        std::string                temp = std::string( PROCESSING_CHANNEL );
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
        std::cout << "CID Test::" << cidstring.value() << std::endl;

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

    outcome::result<void> GeniusNode::ProcessImage( const std::string &image_path )
    {
        auto funds = GetProcessCost( image_path );
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
        task.set_json_data( image_path );
        task.set_random_seed( 0 );
        task.set_results_channel( ( boost::format( "RESULT_CHANNEL_ID_%1%" ) % ( 1 ) ).str() );

        rapidjson::Document document;
        document.Parse( image_path.c_str() );
        size_t           nSubTasks = 1;
        rapidjson::Value inputArray;
        if ( !document.HasMember( "input" ) && document["input"].IsArray() )
        {
            return outcome::failure( Error::PROCESS_INFO_MISSING );
        }
        inputArray = document["input"];
        nSubTasks  = inputArray.Size();

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


        OUTCOME_TRY( ( auto &&, escrow_path ),
                     transaction_manager_->HoldEscrow( funds,
                                                       uint256_t{ std::string( dev_config_.Addr ) },
                                                       cut.value(),
                                                       uuidstring ) );

        task.set_escrow_path( escrow_path );

        return task_queue_->EnqueueTask( task, subTasks );
    }

    uint64_t GeniusNode::GetProcessCost( const std::string &json_data )
    {
        uint64_t cost = 0;
        std::cout << "Received JSON data: " << json_data << std::endl;
        //Parse Json
        rapidjson::Document document;
        if ( document.Parse( json_data.c_str() ).HasParseError() )
        {
            std::cout << "Invalid JSON data provided" << std::endl;
            return 0;
        }
        size_t           nSubTasks = 1;
        rapidjson::Value inputArray;
        if ( document.HasMember( "input" ) && document["input"].IsArray() )
        {
            inputArray = document["input"];
            nSubTasks  = inputArray.Size();
        }
        else
        {
            std::cout << "This json lacks inputs" << std::endl;
            return 0;
        }
        uint64_t block_total_len = 0;
        for ( const auto &input : inputArray.GetArray() )
        {
            if ( input.HasMember( "block_len" ) && input["block_len"].IsUint64() )
            {
                auto block_len   = static_cast<uint64_t>( input["block_len"].GetUint64() );
                block_total_len += block_len;
                std::cout << "Block length: " << block_len << std::endl;
            }
            else
            {
                std::cout << "Missing or invalid block_len in input" << std::endl;
                return 0;
            }
        }
        auto result = sgns::fixed_point::divide( block_total_len, 2100000ULL );
        if ( !result )
        {
            return 0;
        }
        else
        {
            cost = result.value();
        }

        return std::max( cost, static_cast<uint64_t>( 1 ) );
    }

    outcome::result<void> GeniusNode::MintTokens( uint64_t          amount,
                                                  const std::string &transaction_hash,
                                                  const std::string &chainid,
                                                  const std::string &tokenid )
    {
        transaction_manager_->MintFunds( amount, transaction_hash, chainid, tokenid );

        return outcome::success();
    }

    uint64_t GeniusNode::GetBalance()
    {
        return transaction_manager_->GetBalance();
    }

    void GeniusNode::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult )
    {
        std::cout << "[" << account_->GetAddress<std::string>() << "] SUCCESS PROCESSING TASK " << task_id << std::endl;
        do
        {
            if ( task_queue_->IsTaskCompleted( task_id ) )
            {
                std::cout << "Task Already completed!" << std::endl;
                break;
            }

            auto maybe_escrow_path = task_queue_->GetTaskEscrow( task_id );
            if ( maybe_escrow_path.has_failure() )
            {
                std::cout << "No associated Escrow with the task " << std::endl;
                break;
            }
            if ( !transaction_manager_->PayEscrow( maybe_escrow_path.value(), taskresult ) )
            {
                std::cout << "Invalid results!" << std::endl;
                break;
                //throw std::runtime_error( "Invalid results!" );
            }
            task_queue_->CompleteTask( task_id, taskresult );

        } while ( 0 );
    }

    void GeniusNode::ProcessingError( const std::string &task_id )
    {
        //std::cout << "[" << account_->GetAddress<std::string>() << "] ERROR PROCESSING SUBTASK" << task_id << std::endl;
    }
}
