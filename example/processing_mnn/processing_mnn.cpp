//#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "processing_mnn.hpp"

#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <rapidjson/document.h>
#include <libp2p/injector/kademlia_injector.hpp>

#include "FileManager.hpp"
#include "base/logger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "processing/impl/processing_task_queue_impl.hpp"
#include "processing/processing_imagesplit.hpp"
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
const std::string logger_config( R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: gossip_pubsub_test
    sink: console
    level: debug
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )" );

std::vector<uint8_t> GetImageByCID( std::string cid )
{
    libp2p::protocol::kademlia::Config kademlia_config;
    kademlia_config.randomWalk.enabled  = true;
    kademlia_config.randomWalk.interval = std::chrono::seconds( 300 );
    kademlia_config.requestConcurency   = 20;
    auto injector                       = libp2p::injector::makeHostInjector(
        libp2p::injector::makeKademliaInjector( libp2p::injector::useKademliaConfig( kademlia_config ) ) );
    auto ioc = injector.create<std::shared_ptr<boost::asio::io_context>>();

    boost::asio::io_context::executor_type                                   executor = ioc->get_executor();
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard( executor );

    auto mainbuffers = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();

    //Get Image Data from settings.json
    FileManager::GetInstance().InitializeSingletons();
    string fileURL = "https://ipfs.filebase.io/ipfs/" + cid + "/settings.json";
    std::cout << "FILE URLL: " << fileURL << std::endl;
    auto data = FileManager::GetInstance().LoadASync(
        fileURL,
        false,
        false,
        ioc,
        [ioc]( const sgns::AsyncError::CustomResult &status )
        {
            if ( status.has_value() )
            {
                std::cout << "Success: " << status.value().message << std::endl;
            }
            else
            {
                std::cout << "Error: " << status.error() << std::endl;
            }
        },
        [&mainbuffers]( std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers )
        {
            std::cout << "Final Callback" << std::endl;

            if ( !buffers || ( buffers->first.empty() && buffers->second.empty() ) )
            {
                std::cout << "Buffer from AsyncIO is 0" << std::endl;
                return;
            }
            else
            {
                //Process settings json

                mainbuffers->first.insert( mainbuffers->first.end(), buffers->first.begin(), buffers->first.end() );
                mainbuffers->second.insert( mainbuffers->second.end(), buffers->second.begin(), buffers->second.end() );
            }
        },
        "file" );

    //Parse json
    size_t index = std::string::npos;
    for ( size_t i = 0; i < mainbuffers->first.size(); ++i )
    {
        if ( mainbuffers->first[i].find( "settings.json" ) != std::string::npos )
        {
            index = i;
            break;
        }
    }
    if ( index == std::string::npos )
    {
        std::cerr << "settings.json doesn't exist" << std::endl;
        return std::vector<uint8_t>();
    }
    std::vector<char>  &jsonData = mainbuffers->second[index];
    std::string         jsonString( jsonData.begin(), jsonData.end() );
    rapidjson::Document document;
    document.Parse( jsonString.c_str() );

    // Extract input image name
    std::string inputImage = "";
    if ( document.HasMember( "input" ) && document["input"].IsObject() )
    {
        const auto &input = document["input"];
        if ( input.HasMember( "image" ) && input["image"].IsString() )
        {
            inputImage = input["image"].GetString();
            std::cout << "Input Image: " << inputImage << std::endl;
        }
        else
        {
            std::cerr << "No Input file" << std::endl;
            return std::vector<uint8_t>();
        }
    }

    //Get Actual Image
    string            imageUrl = "https://ipfs.filebase.io/ipfs/" + cid + "/" + inputImage;
    std::vector<char> imageData;
    auto              data2 = FileManager::GetInstance().LoadASync(
        imageUrl,
        false,
        false,
        ioc,
        [ioc]( const sgns::AsyncError::CustomResult &status )
        {
            if ( status.has_value() )
            {
                std::cout << "Success: " << status.value().message << std::endl;
            }
            else
            {
                std::cout << "Error: " << status.error() << std::endl;
            }
        },
        [&imageData]( std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers )
        {
            std::cout << "Final Callback" << std::endl;

            if ( !buffers || ( buffers->first.empty() && buffers->second.empty() ) )
            {
                std::cout << "Buffer from AsyncIO is 0" << std::endl;
                return;
            }
            else
            {
                //Process settings json

                imageData.assign( buffers->second[0].begin(), buffers->second[0].end() );
            }
        },
        "file" );

    std::vector<uint8_t> output( imageData.size() );
    std::transform( imageData.begin(),
                    imageData.end(),
                    output.begin(),
                    []( char c ) { return static_cast<uint8_t>( c ); } );
    return output;
}

int main( int argc, char *argv[] )
{
    // prepare log system
    auto logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
        // Original LibP2P logging config
        std::make_shared<libp2p::log::Configurator>(),
        // Additional logging config for application
        logger_config ) );
    logging_system->configure();

    libp2p::log::setLoggingSystem( logging_system );
    libp2p::log::setLevelOfGroup( "gossip_pubsub_test", soralog::Level::ERROR_ );
    auto loggerProcessingEngine = sgns::base::createLogger( "ProcessingEngine" );
    loggerProcessingEngine->set_level( spdlog::level::trace );

    auto loggerProcessingService = sgns::base::createLogger( "ProcessingService" );
    loggerProcessingService->set_level( spdlog::level::trace );

    auto loggerProcessingQueueManager = sgns::base::createLogger( "ProcessingSubTaskQueueManager" );
    loggerProcessingQueueManager->set_level( spdlog::level::debug );

    auto loggerGlobalDB = sgns::base::createLogger( "GlobalDB" );
    loggerGlobalDB->set_level( spdlog::level::trace );

    auto loggerDAGSyncer = sgns::base::createLogger( "GraphsyncDAGSyncer" );
    loggerDAGSyncer->set_level( spdlog::level::trace );

    auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
    loggerBroadcaster->set_level( spdlog::level::trace );
    //Chunk Options
    std::vector<std::vector<uint32_t>> chunkOptions;
    chunkOptions.push_back( { 1080, 0, 4320, 5, 5, 24 } );
    //chunkOptions.push_back({ 540, 0, 4860, 10, 10, 99});
    //chunkOptions.push_back({ 270, 0, 5130, 20, 20, 399});
    //Inputs
    //const auto inputImageFileName = argv[1];

    auto imagetosplit = GetImageByCID( "QmUDMvGQXbUKMsjmTzjf4ZuMx7tHx6Z4x8YH8RbwrgyGAf" );
    if ( imagetosplit.size() == 0 )
    {
        return 0;
    }
    //Split Image into RGBA bytes
    //ImageSplitter imagesplit(inputImageFileName, 540, 4860, 48600);
    ImageSplitter imagesplit( imagetosplit, 5400, 0, 4860000, 4 );
    // For 1350x900 broken into 135x90
    //bytes - 48,600
    //Block Stride - 540
    //Block Line Strike - 4860
    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    //Make Host Pubsubs
    std::vector<std::string> receivedMessages;
    auto                     pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage( "CRDT.Datastore.TEST/pubs_dapp" ).GetKeyPair().value() );

    //Start Pubsubs, add peers of other addresses. We'll probably use DHT Discovery bootstrapping in the future.
    pubs->Start( 40001,
                 { "/ip4/192.168.46.18/tcp/40002/p2p/12D3KooWLrZzsShKdg17w5PxhM4JKb3EaSM9H7Q3D4EWZnA3NvXK",
                   "/ip4/192.168.46.18/tcp/40003/p2p/12D3KooWEAKCDGsZA4MvDVDEzx7pA8rD6UyN6AXsGDCYChWce4Zi",
                   "/ip4/192.168.46.18/tcp/40004/p2p/12D3KooWKTNC88yV3g7bhBdTBxKqy1GT9GQhE6VP28BvVsUtdhX5",
                   "/ip4/192.168.46.18/tcp/40005/p2p/12D3KooWJXWW1mXV1rxf7zuspGTyyk5irwyfXKNy71AcYzGqUT29",
                   "/ip4/192.168.46.18/tcp/40006/p2p/12D3KooWELZFDj8qdMNKoXnc7bndgRQU4yhbaM7tqHCenjqpvn5P",
                   "/ip4/192.168.46.18/tcp/40007/p2p/12D3KooWR5SynE87dtwYd62K3GRGnoabpMsmyTiLc9pLhxMEXMgY",
                   "/ip4/192.168.46.18/tcp/40008/p2p/12D3KooWQp3fX4ZR8LevJMt8coBZa4AZK4VrsaQ22CxpWYiu5qj5",
                   "/ip4/192.168.46.18/tcp/40009/p2p/12D3KooWJGKGW5ETP6zxouH2HSSPhHxu8gpF8AbCGugVDY118UyS",
                   "/ip4/192.168.46.18/tcp/40010/p2p/12D3KooWDT7becpKYbZrH1ZfBibsfdMqHo6AMHr3b5aGCMLw91XW" } );
    std::cout << "Check 5" << std::endl;
    const size_t maximalNodesCount = 1;

    //Make Tasks
    std::list<SGProcessing::Task> tasks;
    size_t                        nTasks = 1;
    // Put tasks to Global DB
    for ( size_t taskIdx = 0; taskIdx < nTasks; ++taskIdx )
    {
        SGProcessing::Task task;
        //std::cout << "CID STRING:    " << libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value() << std::endl;
        //task.set_ipfs_block_id(libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value());
        task.set_ipfs_block_id( "QmUDMvGQXbUKMsjmTzjf4ZuMx7tHx6Z4x8YH8RbwrgyGAf" );
        //task.set_block_len(48600);
        //task.set_block_line_stride(540);
        //task.set_block_stride(4860);
        //task.set_block_len(4860000);
        //task.set_block_line_stride(5400);
        //task.set_block_stride(0);
        task.set_random_seed( 0 );
        task.set_results_channel( ( boost::format( "RESULT_CHANNEL_ID_%1%" ) % ( taskIdx + 1 ) ).str() );
        tasks.push_back( std::move( task ) );
    }

    //Asio Context
    auto io          = std::make_shared<boost::asio::io_context>();
    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    auto scheduler   = std::make_shared<libp2p::protocol::AsioScheduler>( io, libp2p::protocol::SchedulerConfig{} );
    auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs->GetHost(), scheduler );
    auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

    //Add to GlobalDB
    auto globaldb_ret = sgns::crdt::GlobalDB::New( io,
                                                   "CRDT.Datastore.TEST",
                                                   pubs,
                                                   crdtOptions,
                                                   graphsyncnetwork,
                                                   scheduler,
                                                   generator );
    if ( globaldb_ret.has_error() )
    {
        return -1;
    }
    auto globalDB = std::move( globaldb_ret.value() );

    globalDB->AddListenTopic( "CRDT.Datastore.TEST.Channel" );
    globalDB->AddBroadcastTopic( "CRDT.Datastore.TEST.Channel" );
    globalDB->Start();

    //Split tasks into subtasks
    auto taskQueue = std::make_shared<sgns::processing::ProcessingTaskQueueImpl>( globalDB, "" );

    size_t       nSubTasks = chunkOptions.size();
    size_t       nChunks   = 0;
    TaskSplitter taskSplitter( nSubTasks, nChunks, false );

    for ( auto &task : tasks )
    {
        std::cout << "subtask" << std::endl;
        std::list<SGProcessing::SubTask> subTasks;
        taskSplitter.SplitTask( task, subTasks, imagesplit, chunkOptions );
        taskQueue->EnqueueTask( task, subTasks );
    }

    //Run ASIO
    std::thread iothread( [io]() { io->run(); } );
    // Gracefully shutdown on signal
    boost::asio::signal_set signals( *pubs->GetAsioContext(), SIGINT, SIGTERM );
    signals.async_wait(
        [&pubs, &io]( const boost::system::error_code &, int )
        {
            pubs->Stop();
            io->stop();
        } );

    pubs->Wait();
    iothread.join();
}
