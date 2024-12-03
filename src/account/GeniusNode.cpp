/**
 * @file       GeniusNode.cpp
 * @brief      
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/random.hpp>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include "upnp.hpp"
#include "processing/processing_imagesplit.hpp"
#include "processing/processing_tasksplit.hpp"
#include "processing/processing_subtask_enqueuer_impl.hpp"
#include "processing/processors/processing_processor_mnn_image.hpp"
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

using namespace boost::multiprecision;
namespace br = boost::random;

namespace sgns
{
GeniusNode::GeniusNode( const DevConfig_st &dev_config, const char *eth_private_key, bool autodht, bool isprocessor ) :
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
    libp2p::log::setLevelOfGroup( "SuperGeniusDemo", soralog::Level::TRACE );

    auto loggerGlobalDB = base::createLogger( "GlobalDB" );
    loggerGlobalDB->set_level( spdlog::level::off );

    if ( isprocessor_ )
    {
        auto loggerDAGSyncer = base::createLogger( "GraphsyncDAGSyncer" );
        loggerDAGSyncer->set_level( spdlog::level::off );
    }
    else
    {
        auto loggerDAGSyncer = base::createLogger( "GraphsyncDAGSyncer" );
        loggerDAGSyncer->set_level( spdlog::level::off );
    }
    auto loggerBroadcaster = base::createLogger( "PubSubBroadcasterExt" );
    loggerBroadcaster->set_level( spdlog::level::off );

    auto loggerDataStore = base::createLogger( "CrdtDatastore" );
    loggerDataStore->set_level( spdlog::level::off );

    auto loggerTransactions = base::createLogger( "TransactionManager" );
    loggerTransactions->set_level( spdlog::level::off );

    auto loggerQueue = base::createLogger( "ProcessingTaskQueueImpl" );
    loggerQueue->set_level( spdlog::level::trace );

    auto loggerRocksDB = base::createLogger( "rocksdb" );
    loggerRocksDB->set_level( spdlog::level::off );

    auto loggerTM = base::createLogger( "TransactionManager" );
    loggerTM->set_level( spdlog::level::off );

    auto logkad = base::createLogger( "Kademlia" );
    logkad->set_level( spdlog::level::off );

    auto logNoise = base::createLogger( "Noise" );
    logNoise->set_level( spdlog::level::off );

    auto loggerSubQueue = base::createLogger( "ProcessingSubTaskQueueAccessorImpl" );
    loggerSubQueue->set_level( spdlog::level::trace );
    auto loggerProcServ = base::createLogger( "ProcessingService" );
    loggerProcServ->set_level( spdlog::level::trace );

    auto loggerProcqm = base::createLogger( "ProcessingSubTaskQueueManager" );
    loggerProcqm->set_level( spdlog::level::trace );

    auto pubsubport    = 40001 + GenerateRandomPort( account_->GetAddress<std::string>() );
    auto graphsyncport = 40010 + GenerateRandomPort( account_->GetAddress<std::string>() );

    std::vector<std::string> addresses;
    //UPNP
    auto        upnp = std::make_shared<upnp::UPNP>();
    std::string lanip;
    if ( upnp->GetIGD() )
    {
        auto openedPort  = upnp->OpenPort( pubsubport, pubsubport, "TCP", 1800 );
        auto openedPort2 = upnp->OpenPort( graphsyncport, graphsyncport, "TCP", 1800 );
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
        ( boost::format( "SuperGNUSNode.TestNet.%s/pubs_processor" ) % account_->GetAddress<std::string>() ).str();

    pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>(
        crdt::KeyPairFileStorage( write_base_path_ + pubsubKeyPath ).GetKeyPair().value() );
    pubsub_->Start( pubsubport,
                    { "/ip4/137.184.224.118/tcp/4001/p2p/12D3KooWKhqPn4nAXJRxyDESgqj3dBBBBVvmwFAShVkTdhioLwYG",
                      "/ip4/192.168.46.116/tcp/40813/p2p/12D3KooWEhjSt5fxdsqftw8A1XdzfQy2JNZGiqi4rmFDQPoinV9Y",
                      "/ip4/192.168.46.116/tcp/40096/p2p/12D3KooWHNgFsbvDU2JVpY6RrAsWnGujsmmargFtJRXHQvx8fWzE",
                      "/ip4/192.168.46.116/tcp/40697/p2p/12D3KooWJbnptXWuugfqwDJGgZVAHyAvN3LZMFpwHRhQGzuY58rT" },
                    lanip,
                    addresses );

    globaldb_ = std::make_shared<crdt::GlobalDB>(
        io_,
        ( boost::format( write_base_path_ + "SuperGNUSNode.TestNet.%s" ) % account_->GetAddress<std::string>() ).str(),
        graphsyncport,
        std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_, std::string( PROCESSING_CHANNEL ) ) );

    if ( !globaldb_->Init( crdt::CrdtOptions::DefaultOptions() ).has_value() )
    {
        throw std::runtime_error( "Could not start GlobalDB" );
    }

    task_queue_      = std::make_shared<processing::ProcessingTaskQueueImpl>( globaldb_ );
    processing_core_ = std::make_shared<processing::ProcessingCoreImpl>( globaldb_, 1000000, 1 );
    processing_core_->RegisterProcessorFactory( "mnnimage", [] { return std::make_unique<processing::MNN_Image>(); } );

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

    base::Buffer root_hash;
    root_hash.put( std::vector<uint8_t>( 32ul, 1 ) );
    hasher_ = std::make_shared<crypto::HasherImpl>();

    header_repo_ = std::make_shared<blockchain::KeyValueBlockHeaderRepository>(
        globaldb_,
        hasher_,
        ( boost::format( std::string( db_path_ ) ) % TEST_NET ).str() );
    auto maybe_block_storage = blockchain::KeyValueBlockStorage::create( root_hash,
                                                                         globaldb_,
                                                                         hasher_,
                                                                         header_repo_,
                                                                         []( auto & ) {} );

    if ( !maybe_block_storage )
    {
        std::cerr << "Error initializing blockchain: " << maybe_block_storage.error().message() << std::endl;
        throw std::runtime_error( "Error initializing blockchain" );
    }
    block_storage_       = std::move( maybe_block_storage.value() );
    transaction_manager_ = std::make_shared<TransactionManager>( globaldb_, io_, account_, hasher_, block_storage_ );

    transaction_manager_->Start();
    if ( isprocessor_ )
    {
        processing_service_->StartProcessing( std::string( PROCESSING_GRID_CHANNEL ) );
    }

    if ( autodht_ )
    {
        DHTInit();
    }

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

void GeniusNode::ProcessImage( const std::string &image_path )
{
    // std::cout << "---------------------------------------------------------------" << std::endl;
    // std::cout << "Process Image?" << transaction_manager_->GetBalance() << std::endl;
    // std::cout << "---------------------------------------------------------------" << std::endl;
    auto funds = GetProcessCost( image_path );
    if ( funds <= 0 )
    {
        return;
    }

    if ( transaction_manager_->GetBalance() >= funds )
    {
        //processing_service_->StopProcessing();

        SGProcessing::Task task;
        //boost::uuids::uuid uuid = boost::uuids::random_generator()();
        //boost::uuids::random_generator_pure gen;
        //boost::uuids::uuid uuid = gen();
        auto uuidstring = generate_uuid_with_ipfs_id( pubsub_->GetHost()->getId().toBase58() );
        //std::cout << "CID STRING:    " << libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value() << std::endl;
        //task.set_ipfs_block_id(libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value());
        task.set_ipfs_block_id( uuidstring );

        task.set_json_data( image_path );

        task.set_random_seed( 0 );
        task.set_results_channel( ( boost::format( "RESULT_CHANNEL_ID_%1%" ) % ( 1 ) ).str() );

        rapidjson::Document document;
        document.Parse( image_path.c_str() );
        size_t           nSubTasks = 1;
        rapidjson::Value inputArray;
        if ( document.HasMember( "input" ) && document["input"].IsArray() )
        {
            inputArray = document["input"];
            nSubTasks  = inputArray.Size();
        }
        else
        {
            std::cout << "This json lacks information" << std::endl;
            return;
        }
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

        auto maybe_escrow_path = transaction_manager_->HoldEscrow( funds,
                                                                   uint256_t{ std::string( dev_config_.Addr ) },
                                                                   dev_config_.Cut,
                                                                   uuidstring );
        task.set_escrow_path( maybe_escrow_path.value() );
        task_queue_->EnqueueTask( task, subTasks );
    }
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
    for ( const auto &input : inputArray.GetArray() )
    {
        if ( input.HasMember( "block_len" ) && input["block_len"].IsUint64() )
        {
            uint64_t block_len  = input["block_len"].GetUint64();
            cost               += ( block_len / 300000 );
            std::cout << "Block length: " << block_len << std::endl;
        }
        else
        {
            std::cout << "Missing or invalid block_len in input" << std::endl;
            return 0;
        }
    }
    return std::max( cost, static_cast<uint64_t>( 1 ) );
}

    void GeniusNode::MintTokens( uint64_t amount, std::string transaction_hash, std::string chainid, std::string tokenid )
    {
        transaction_manager_->MintFunds( amount, transaction_hash, chainid, tokenid );
    }

uint64_t GeniusNode::GetBalance()
{
    return transaction_manager_->GetBalance();
}

std::vector<base::Buffer> GeniusNode::GetBlocks()
{
    std::vector<base::Buffer> out;
    primitives::BlockNumber   id = 0;

    outcome::result<primitives::BlockHeader> retval = outcome::failure( boost::system::error_code{} );
    do
    {
        if ( retval = block_storage_->getBlockHeader( id ); retval )
        {
            if ( auto DAGHeader = retval.value(); DAGHeader.number == id )
            {
                if ( auto block = block_storage_->GetRawBlock( id ); block )
                {
                    out.push_back( std::move( block.value() ) );
                    id++;
                }
                else
                {
                    break;
                }
            }
        }
    } while ( retval );

    return out;
}

uint16_t GeniusNode::GenerateRandomPort( const std::string &address )
{
    uint32_t seed = static_cast<uint32_t>( uint256_t{ address } % 1000 );

    // Setup the random number generator
    br::mt19937                    rng( seed );    // Use the reduced number as seed
    br::uniform_int_distribution<> dist( 0, 999 ); // Range: 0 to 9999

    // Generate a 4-digit random number
    //return dist( rng );

    return static_cast<uint16_t>( seed );
}

void GeniusNode::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult )
{
    std::cout << "[" << account_->GetAddress<std::string>() << "] SUCESS PROCESSING TASK " << task_id << std::endl;
    if ( !task_queue_->IsTaskCompleted( task_id ) )
    {
        transaction_manager_->ProcessingDone( task_id, taskresult );
        task_queue_->CompleteTask( task_id, taskresult );
    }
    else
    {
        std::cout << "Task Already completed!" << std::endl;
    }
}

    void GeniusNode::ProcessingError( const std::string &task_id )
    {
        //std::cout << "[" << account_->GetAddress<std::string>() << "] ERROR PROCESSING SUBTASK" << task_id << std::endl;
    }

}
