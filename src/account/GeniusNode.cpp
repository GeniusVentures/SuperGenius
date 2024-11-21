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
    //GeniusNode GeniusNode::instance( DEV_CONFIG );

    GeniusNode::GeniusNode( const DevConfig_st &dev_config, const char *eth_private_key, bool autodht, bool isprocessor ) :
        account_( std::make_shared<GeniusAccount>( static_cast<uint8_t>( dev_config.TokenID ),
                                                   std::string( dev_config.BaseWritePath ),
                                                   eth_private_key ) ),
        io_( std::make_shared<boost::asio::io_context>() ),
        write_base_path_( dev_config.BaseWritePath ),
        dev_config_( dev_config ),
        autodht_(autodht),
        isprocessor_(isprocessor)
    {
        logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            GetLoggingSystem() ) );
        auto result = logging_system->configure();
        std::cout << "Log Result: " << result.message << std::endl;
        libp2p::log::setLoggingSystem( logging_system );
        libp2p::log::setLevelOfGroup( "SuperGeniusDemo", soralog::Level::TRACE );
        //libp2p::log::setLevelOfGroup("SuperGeniusDemoFile", soralog::Level::ERROR_);

        auto loggerGlobalDB = base::createLogger( "GlobalDB" );
        loggerGlobalDB->set_level( spdlog::level::off );

        auto loggerDAGSyncer = base::createLogger("GraphsyncDAGSyncer");
        loggerDAGSyncer->set_level( spdlog::level::off );

        auto loggerBroadcaster = base::createLogger("PubSubBroadcasterExt");
        loggerBroadcaster->set_level(spdlog::level::off);

        auto loggerDataStore = base::createLogger("CrdtDatastore");
        loggerDataStore->set_level(spdlog::level::off);
        
        auto loggerTransactions = base::createLogger("TransactionManager");
        loggerTransactions->set_level(spdlog::level::off);

        auto loggerQueue = base::createLogger("ProcessingTaskQueueImpl");
        loggerQueue->set_level(spdlog::level::trace);

        auto loggerRocksDB = base::createLogger("rocksdb");
        loggerRocksDB->set_level(spdlog::level::off);

        auto loggerTM = base::createLogger("TransactionManager");
        loggerTM->set_level(spdlog::level::off); 
        //auto loggerAutonat = base::createLogger("Autonat");
        //loggerDAGSyncer->set_level(spdlog::level::trace);

        //auto loggerAutonatMsg = base::createLogger("AutonatMsgProcessor");
        //loggerDAGSyncer->set_level(spdlog::level::trace);

        auto logkad = sgns::base::createLogger( "Kademlia" );
        logkad->set_level( spdlog::level::off );

        auto logNoise = sgns::base::createLogger( "Noise" );
        logNoise->set_level( spdlog::level::off );

        auto loggerSubQueue = base::createLogger("ProcessingSubTaskQueueAccessorImpl");
        loggerSubQueue->set_level(spdlog::level::trace);


        auto pubsubport    = 40001 + GenerateRandomPort( account_->GetAddress<std::string>() );
        auto graphsyncport = 40010 + GenerateRandomPort( account_->GetAddress<std::string>() );

        std::vector<std::string> addresses;
        //UPNP
        auto upnp   = std::make_shared<sgns::upnp::UPNP>();
        auto gotIGD = upnp->GetIGD();
        std::string lanip = "";
        if ( gotIGD )
        {

            auto openedPort = upnp->OpenPort( pubsubport, pubsubport, "TCP", 1800 );
            auto openedPort2 = upnp->OpenPort( graphsyncport, graphsyncport, "TCP", 1800 );
            auto wanip      = upnp->GetWanIP();
            auto lanip      = upnp->GetLocalIP();
            std::cout << "Wan IP: " << wanip << std::endl;
            std::cout << "Lan IP: " << lanip << std::endl;
            //addresses.push_back( lanip );
            addresses.push_back( wanip );
            if ( !openedPort )
            {
                std::cerr << "Failed to open port" << std::endl;
            }
            else
            {
                std::cout << "Open Port Success" << std::endl;
            }
        }

        //auto loggerBroadcaster = base::createLogger( "PubSubBroadcasterExt" );
        //loggerBroadcaster->set_level( spdlog::level::debug );

        auto pubsubKeyPath =
            ( boost::format("SuperGNUSNode.TestNet.%s/pubs_processor" ) % account_->GetAddress<std::string>() ).str();

        pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>(
            crdt::KeyPairFileStorage( write_base_path_ + pubsubKeyPath ).GetKeyPair().value() );
        pubsub_->Start(pubsubport, { "/ip4/137.184.224.118/tcp/4001/p2p/12D3KooWKhqPn4nAXJRxyDESgqj3dBBBBVvmwFAShVkTdhioLwYG", "/ip4/192.168.46.116/tcp/40813/p2p/12D3KooWEhjSt5fxdsqftw8A1XdzfQy2JNZGiqi4rmFDQPoinV9Y", "/ip4/192.168.46.116/tcp/40096/p2p/12D3KooWHNgFsbvDU2JVpY6RrAsWnGujsmmargFtJRXHQvx8fWzE", "/ip4/192.168.46.116/tcp/40697/p2p/12D3KooWJbnptXWuugfqwDJGgZVAHyAvN3LZMFpwHRhQGzuY58rT"}, lanip, addresses);
        
        globaldb_ = std::make_shared<crdt::GlobalDB>(
            io_,
            (boost::format(write_base_path_ + "SuperGNUSNode.TestNet.%s") % account_->GetAddress<std::string>()).str(),
            graphsyncport,
            std::make_shared<ipfs_pubsub::GossipPubSubTopic>(pubsub_, std::string(PROCESSING_CHANNEL)));

        globaldb_->Init( crdt::CrdtOptions::DefaultOptions() );

        task_queue_      = std::make_shared<processing::ProcessingTaskQueueImpl>( globaldb_ );
        processing_core_ = std::make_shared<processing::ProcessingCoreImpl>( globaldb_, 1000000, 2 );
        processing_core_->RegisterProcessorFactory( "mnnimage",
                                                    []() { return std::make_unique<processing::MNN_Image>(); } );

        task_result_storage_ = std::make_shared<processing::SubTaskResultStorageImpl>( globaldb_ );
        processing_service_  = std::make_shared<processing::ProcessingServiceImpl>(
            pubsub_,                                                          //
            MAX_NODES_COUNT,                                                  //
            std::make_shared<processing::SubTaskEnqueuerImpl>( task_queue_ ), //
            std::make_shared<processing::ProcessSubTaskStateStorage>(),       //
            task_result_storage_,                                             //
            processing_core_,                                                 //
            [this]( const std::string &var, const SGProcessing::TaskResult &taskresult) { ProcessingDone( var, taskresult ); },      //
            [this]( const std::string &var ) { ProcessingError( var ); } );
        processing_service_->SetChannelListRequestTimeout( boost::posix_time::milliseconds( 10000 ) );

        base::Buffer root_hash;
        root_hash.put( std::vector<uint8_t>( 32ul, 1 ) );
        hasher_ = std::make_shared<crypto::HasherImpl>();

        header_repo_ = std::make_shared<blockchain::KeyValueBlockHeaderRepository>(
            globaldb_, hasher_, ( boost::format( std::string( db_path_ ) ) % TEST_NET ).str() );
        auto maybe_block_storage =
            blockchain::KeyValueBlockStorage::create( root_hash, globaldb_, hasher_, header_repo_, []( auto & ) {} );

        if ( !maybe_block_storage )
        {
            std::cout << "Error initializing blockchain" << maybe_block_storage.error().message() << std::endl;
            throw std::runtime_error( "Error initializing blockchain" );
            return;
        }
        block_storage_       = std::move( maybe_block_storage.value() );
        transaction_manager_ = std::make_shared<TransactionManager>(
            globaldb_,
            io_,
            account_,
            hasher_,
            block_storage_,
            [this]( const std::string &var, const std::set<std::string> &vars ) { ProcessingFinished( var, vars ); } );

        transaction_manager_->Start();
        if(isprocessor_) processing_service_->StartProcessing( std::string( PROCESSING_GRID_CHANNEL ) );

        if(autodht_) DHTInit();

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

    void GeniusNode::ProcessImage( const std::string &image_path, uint16_t funds )
    {
        if ( transaction_manager_->GetBalance() >= funds )
        {
            processing_service_->StopProcessing();
            
            //std::vector<std::vector<uint32_t>> chunkOptions;
            //chunkOptions.push_back( { 1080, 0, 4320, 5, 5, 24 } );
            //std::list<SGProcessing::Task> tasks;
            //size_t                        nTasks = 1;
            //for ( size_t taskIdx = 0; taskIdx < nTasks; ++taskIdx )
            //{
                SGProcessing::Task task;
                boost::uuids::uuid uuid = boost::uuids::random_generator()();
                //std::cout << "CID STRING:    " << libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value() << std::endl;
                //task.set_ipfs_block_id(libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value());
                task.set_ipfs_block_id(boost::uuids::to_string(uuid));

                task.set_json_data(image_path);
                //task.set_block_len(48600);
                //task.set_block_line_stride(540);
                //task.set_block_stride(4860);
                //task.set_block_len( 4860000 );
                //task.set_block_line_stride( 5400 );
                //task.set_block_stride( 0 );
                task.set_random_seed( 0 );
                task.set_results_channel( ( boost::format( "RESULT_CHANNEL_ID_%1%" ) % ( 1 ) ).str() );
                //tasks.push_back( std::move( task ) );
            //}
            //Number of SubTasks is number of input Images.
            rapidjson::Document document;
            document.Parse(image_path.c_str());
            size_t nSubTasks = 1;
            rapidjson::Value inputArray;
            if (document.HasMember("input") && document["input"].IsArray()) {
                inputArray = document["input"];
                nSubTasks = inputArray.Size();
            }
            else {
                std::cout << "This json lacks information" << std::endl;
                return;
            }
            processing::ProcessTaskSplitter taskSplitter;
            //auto                               mnn_image = GetImageByCID(image_path);
            //if (mnn_image.size() != inputArray.Size())
            //{
            //    std::cout << "Input size does not match, did an image fail to be obtained?" << std::endl;
            //    return;
            //}
            //int imageindex = 0;
            std::list<SGProcessing::SubTask> subTasks;
            for (const auto& input : inputArray.GetArray())
            {
                //auto image = mnn_image[imageindex];
                //processing::ImageSplitter          imagesplit(image, input["block_line_stride"].GetInt(), input["block_stride"].GetInt(), input["block_len"].GetInt());

                size_t nChunks = input["chunk_count"].GetInt();               
                //Assumption: There will always be 1 task, with several subtasks per json. So this for loop isn't really doing anything.
                //for (auto& task : tasks)
                //{
                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                input.Accept(writer);
                std::string inputAsString = buffer.GetString();
                taskSplitter.SplitTask(task, subTasks, inputAsString, nChunks, false);
                    
                //}
                //imageindex++;
            }

            task_queue_->EnqueueTask(task, subTasks);
            transaction_manager_->HoldEscrow(
                funds, nSubTasks, uint256_t{ std::string( dev_config_.Addr ) }, dev_config_.Cut, boost::uuids::to_string(uuid) );
        }
    }

    void GeniusNode::MintTokens( uint64_t amount )
    {
        transaction_manager_->MintFunds( amount );
    }

    uint64_t GeniusNode::GetBalance()
    {
        return transaction_manager_->GetBalance();
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

    void GeniusNode::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult)
    {
        std::cout << "PROCESSING DONE " << task_id << std::endl;
        transaction_manager_->ProcessingDone(task_id, taskresult);
    }

    void GeniusNode::ProcessingError( const std::string &task_id )
    {
        std::cout << "PROCESSING ERROR " << task_id << std::endl;
    }

    void GeniusNode::ProcessingFinished( const std::string &task_id, const std::set<std::string> &subtasks_ids )
    {
        if ( transaction_manager_->ReleaseEscrow( task_id, !task_queue_->IsTaskCompleted( task_id ) ) )
        {
            SGProcessing::TaskResult result;
            std::cout << "PROCESSING FINISHED " << task_id << std::endl;
            auto subtask_results = task_result_storage_->GetSubTaskResults( subtasks_ids );

            SGProcessing::TaskResult taskResult;
            auto                     results = taskResult.mutable_subtask_results();
            for ( const auto &r : subtask_results )
            {
                auto result = results->Add();
                result->CopyFrom( r );
            }
            processing_service_->StartProcessing( std::string( PROCESSING_GRID_CHANNEL ) );
            task_queue_->CompleteTask( task_id, taskResult );
        }
    }

}
