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
#include "account/GeniusNode.hpp"
#include "processing/processing_imagesplit.hpp"
#include "processing/processing_tasksplit.hpp"
#include "processing/processing_subtask_enqueuer_impl.hpp"
#include "processing/processing_subtask_result_storage.hpp"
#include "processing/processors/processing_processor_mnn_posenet.hpp"
#include "processing/impl/processing_subtask_result_storage_impl.hpp"

#ifndef __cplusplus
extern "C"
{
#endif
    extern AccountKey   ACCOUNT_KEY;
    extern DevConfig_st DEV_CONFIG;
#ifndef __cplusplus
}
#endif
static const std::string logger_config( R"(
            # ----------------
            sinks:
              - name: console
                type: console
                color: true
            groups:
              - name: SuperGeniusDemo
                sink: console
                level: info
                children:
                  - name: libp2p
                  - name: Gossip
            # ----------------
              )" );

using namespace boost::multiprecision;
namespace br = boost::random;

namespace sgns
{
    GeniusNode::GeniusNode( const AccountKey &priv_key_data, const DevConfig_st &dev_config ) :
        account_( std::make_shared<GeniusAccount>( uint256_t{ std::string( priv_key_data ) }, 0, 0 ) ), //
        io_( std::make_shared<boost::asio::io_context>() ),                                             //
        node_base_addr_( priv_key_data ),                                                               //
        dev_config_( dev_config )                                                                       //
    {
        logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config ) );
        logging_system->configure();

        libp2p::log::setLoggingSystem( logging_system );
        libp2p::log::setLevelOfGroup( "SuperGNUSNode", soralog::Level::ERROR_ );

        auto loggerGlobalDB = base::createLogger( "GlobalDB" );
        loggerGlobalDB->set_level( spdlog::level::debug );

        auto loggerDAGSyncer = base::createLogger( "GraphsyncDAGSyncer" );
        loggerDAGSyncer->set_level( spdlog::level::debug );

        auto logkad = sgns::base::createLogger( "Kademlia" );
        logkad->set_level( spdlog::level::trace );

        auto logNoise = sgns::base::createLogger( "Noise" );
        logNoise->set_level( spdlog::level::trace );

        //auto loggerBroadcaster = base::createLogger( "PubSubBroadcasterExt" );
        //loggerBroadcaster->set_level( spdlog::level::debug );

        auto pubsubKeyPath = ( boost::format( "SuperGNUSNode.TestNet.%s/pubs_processor" ) % node_base_addr_ ).str();

        pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>(
            crdt::KeyPairFileStorage( pubsubKeyPath ).GetKeyPair().value() );
        pubsub_->Start( 40001 + GenerateRandomPort( node_base_addr_ ), { pubsub_->GetLocalAddress() } );

        globaldb_ = std::make_shared<crdt::GlobalDB>(
            io_, ( boost::format( "SuperGNUSNode.TestNet.%s" ) % node_base_addr_ ).str(),
            40010 + GenerateRandomPort( node_base_addr_ ),
            std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_, std::string( PROCESSING_CHANNEL ) ) );

        globaldb_->Init( crdt::CrdtOptions::DefaultOptions() );

        base::Buffer root_hash;
        root_hash.put( std::vector<uint8_t>( 32ul, 1 ) );
        hasher_ = std::make_shared<crypto::HasherImpl>();

        header_repo_ = std::make_shared<blockchain::KeyValueBlockHeaderRepository>(
            globaldb_, hasher_, ( boost::format( std::string( db_path_ ) ) % TEST_NET ).str() );
        auto maybe_block_storage =
            blockchain::KeyValueBlockStorage::create( root_hash, globaldb_, hasher_, header_repo_, []( auto & ) {} );

        if ( !maybe_block_storage )
        {
            std::cout << "Error initializing blockchain" << std::endl;
            throw std::runtime_error( "Error initializing blockchain" );
        }
        block_storage_       = std::move( maybe_block_storage.value() );
        transaction_manager_ = std::make_shared<TransactionManager>(
            globaldb_, io_, account_, hasher_, block_storage_,
            []( const std::string &var ) { std::cout << "FINISHED PROCESSING" << var << std::endl; } );

        task_queue_      = std::make_shared<processing::ProcessingTaskQueueImpl>( globaldb_ );
        processing_core_ = std::make_shared<processing::ProcessingCoreImpl>( globaldb_, 1000000, 2 );
        processing_core_->RegisterProcessorFactory( "posenet",
                                                    []() { return std::make_unique<processing::MNN_PoseNet>(); } );
        processing_service_ = std::make_shared<processing::ProcessingServiceImpl>(
            pubsub_,                                                             //
            MAX_NODES_COUNT,                                                     //
            std::make_shared<processing::SubTaskEnqueuerImpl>( task_queue_ ),    //
            std::make_shared<processing::ProcessSubTaskStateStorage>(),          //
            std::make_shared<processing::SubTaskResultStorageImpl>( globaldb_ ), //
            processing_core_,                                                    //
            [this]( const std::string &var ) { ProcessingDone( var ); },         //
            [this]( const std::string &var ) { ProcessingError( var ); } );
        processing_service_->SetChannelListRequestTimeout( boost::posix_time::milliseconds( 10000 ) );
        processing_service_->StartProcessing( std::string( PROCESSING_GRID_CHANNEL ) );

        transaction_manager_->Start();

        //DHTInit();

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
        pubsub_->GetDHT()->ProvideCID( key, true );

        auto cidtest = libp2p::multi::ContentIdentifierCodec::decode( key.data );

        auto cidstring = libp2p::multi::ContentIdentifierCodec::toString( cidtest.value() );
        std::cout << "CID Test::" << cidstring.value() << std::endl;

        //Also Find providers
        pubsub_->StartFindingPeers( io_, key );
    }

    void GeniusNode::ProcessImage( const std::string &image_path, uint16_t funds )
    {
        if ( transaction_manager_->GetBalance() >= funds )
        {
            auto                      mnn_image = GetImageByCID( "QmUDMvGQXbUKMsjmTzjf4ZuMx7tHx6Z4x8YH8RbwrgyGAf" );
            processing::ImageSplitter imagesplit( mnn_image, 5400, 0, 4860000 );
            std::vector<std::vector<uint32_t>> chunkOptions;
            chunkOptions.push_back( { 1080, 0, 4320, 5, 5, 24 } );
            std::list<SGProcessing::Task> tasks;
            size_t                        nTasks = 1;
            for ( size_t taskIdx = 0; taskIdx < nTasks; ++taskIdx )
            {
                SGProcessing::Task task;
                //std::cout << "CID STRING:    " << libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value() << std::endl;
                //task.set_ipfs_block_id(libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value());
                task.set_ipfs_block_id( "QmUDMvGQXbUKMsjmTzjf4ZuMx7tHx6Z4x8YH8RbwrgyGAf" );
                //task.set_block_len(48600);
                //task.set_block_line_stride(540);
                //task.set_block_stride(4860);
                task.set_block_len( 4860000 );
                task.set_block_line_stride( 5400 );
                task.set_block_stride( 0 );
                task.set_random_seed( 0 );
                task.set_results_channel( ( boost::format( "RESULT_CHANNEL_ID_%1%" ) % ( taskIdx + 1 ) ).str() );
                tasks.push_back( std::move( task ) );
            }

            size_t nSubTasks = chunkOptions.size();
            size_t nChunks   = 0;
            for ( auto &node : chunkOptions )
            {
                nChunks += node.at( 5 );
            }
            processing::ProcessTaskSplitter taskSplitter( nSubTasks, nChunks, false );

            for ( auto &task : tasks )
            {
                std::list<SGProcessing::SubTask> subTasks;
                taskSplitter.SplitTask( task, subTasks, imagesplit, chunkOptions );
                task_queue_->EnqueueTask( task, subTasks );
            }

            transaction_manager_->HoldEscrow( funds, nSubTasks, uint256_t{ std::string( dev_config_.Addr ) },
                                              dev_config_.Cut, "QmUDMvGQXbUKMsjmTzjf4ZuMx7tHx6Z4x8YH8RbwrgyGAf" );
        }
    }

    void GeniusNode::MintTokens( uint64_t amount )
    {
        transaction_manager_->MintFunds( amount );
    }

    std::vector<uint8_t> GeniusNode::GetImageByCID( std::string cid )
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
            fileURL, false, false, ioc, [ioc]( const int &status ) { std::cout << "status: " << status << std::endl; },
            [ioc, &mainbuffers](
                std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers )
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
                    mainbuffers->second.insert( mainbuffers->second.end(), buffers->second.begin(),
                                                buffers->second.end() );
                }
            },
            "file" );
        ioc->reset();
        ioc->run();

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
            imageUrl, false, false, ioc, [ioc]( const int &status ) { std::cout << "status: " << status << std::endl; },
            [ioc,
             &imageData]( std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers )
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
        ioc->reset();
        ioc->run();
        std::vector<uint8_t> output( imageData.size() );
        std::transform( imageData.begin(), imageData.end(), output.begin(),
                        []( char c ) { return static_cast<uint8_t>( c ); } );
        return output;
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

    void GeniusNode::ProcessingDone( const std::string subtask_id )
    {
        std::cout << "PROCESSING COMPLETE " << subtask_id << std::endl;
        transaction_manager_->ProcessingDone( subtask_id );
        //task_queue_->CompleteTask(subtask_id)
    }

    void GeniusNode::ProcessingError( const std::string subtask_id )
    {
        std::cout << "PROCESSING ERROR " << subtask_id << std::endl;
    }

    /*
    static GeniusNode instance( ACCOUNT_KEY, DEV_CONFIG );
    GeniusNode       &GeniusNode::GetInstance()
    {
        return instance;
    }
    */
}
