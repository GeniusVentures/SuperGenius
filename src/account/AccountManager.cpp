/**
 * @file       AccountManager.cpp
 * @brief      
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/AccountManager.hpp"
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

namespace sgns
{
    AccountManager::AccountManager( const AccountKey &priv_key_data, const DevConfig_st &dev_config ) :
        account_( std::make_shared<GeniusAccount>( uint256_t{ std::string( priv_key_data ) }, 0, 0 ) ), //
        io_( std::make_shared<boost::asio::io_context>() ),                                             //
        node_base_addr_( priv_key_data ),                                                               //
        dev_config_( dev_config )                                                                       //
    {

        auto logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
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

        //auto loggerBroadcaster = base::createLogger( "PubSubBroadcasterExt" );
        //loggerBroadcaster->set_level( spdlog::level::debug );

        auto pubsubKeyPath = ( boost::format( "SuperGNUSNode.TestNet.%s/pubs_processor" ) % node_base_addr_ ).str();

        pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>( crdt::KeyPairFileStorage( pubsubKeyPath ).GetKeyPair().value() );
        pubsub_->Start( 40001, { pubsub_->GetLocalAddress() } );

        io_ = std::make_shared<boost::asio::io_context>();

        globaldb_ = std::make_shared<crdt::GlobalDB>( io_, ( boost::format( "SuperGNUSNode.TestNet.%s" ) % node_base_addr_ ).str(), 40010,
                                                      std::make_shared<ipfs_pubsub::GossipPubSubTopic>( pubsub_, "SGNUS.TestNet.Channel" ) );

        globaldb_->Init( crdt::CrdtOptions::DefaultOptions() );

        base::Buffer root_hash;
        root_hash.put( std::vector<uint8_t>( 32ul, 1 ) );
        hasher_ = std::make_shared<crypto::HasherImpl>();

        header_repo_             = std::make_shared<blockchain::KeyValueBlockHeaderRepository>( globaldb_, hasher_,
                                                                                    ( boost::format( std::string( db_path_ ) ) % TEST_NET ).str() );
        auto maybe_block_storage = blockchain::KeyValueBlockStorage::create( root_hash, globaldb_, hasher_, header_repo_, []( auto & ) {} );

        if ( !maybe_block_storage )
        {
            std::cout << "Error initializing blockchain" << std::endl;
            throw std::runtime_error( "Error initializing blockchain" );
        }
        block_storage_       = std::move( maybe_block_storage.value() );
        transaction_manager_ = std::make_shared<TransactionManager>( globaldb_, io_, account_, hasher_, block_storage_ );
        transaction_manager_->Start();

        task_queue_      = std::make_shared<processing::ProcessingTaskQueueImpl>( globaldb_ );
        processing_core_ = std::make_shared<processing::ProcessingCoreImpl>( globaldb_, 1000000, 2 );
        processing_core_->RegisterProcessorFactory( "posenet", []() { return std::make_unique<processing::MNN_PoseNet>(); } );
        processing_service_ =
            std::make_shared<processing::ProcessingServiceImpl>( pubsub_,                                                             //
                                                                 MAX_NODES_COUNT,                                                     //
                                                                 std::make_shared<processing::SubTaskEnqueuerImpl>( task_queue_ ),    //
                                                                 std::make_shared<processing::ProcessSubTaskStateStorage>(),          //
                                                                 std::make_shared<processing::SubTaskResultStorageImpl>( globaldb_ ), //
                                                                 processing_core_ );
        processing_service_->SetChannelListRequestTimeout( boost::posix_time::milliseconds( 10000 ) );
        processing_service_->StartProcessing( std::string( PROCESSING_GRID_CHANNEL ) );

        io_thread = std::thread( [this]() { io_->run(); } );
    }
    AccountManager::AccountManager() : account_( std::make_shared<GeniusAccount>( 0, 0, 0 ) ) //

    {
    }
    AccountManager::~AccountManager()
    {
        if ( signals_ )
        {
            signals_->cancel(); // Cancel any pending signals
        }
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

    void AccountManager::AddPeer( const std::string &peer )
    {
        //pubsub_->AddPeers( { peer } );
    }

    boost::optional<GeniusAccount> AccountManager::CreateAccount( const std::string &priv_key_data, const uint64_t &initial_amount )
    {
        uint256_t value_in_num{ priv_key_data };
        return GeniusAccount( value_in_num, initial_amount, 0 );
    }

    void AccountManager::ProcessImage( const std::string &image_path, uint16_t funds )
    {
        processing::ImageSplitter          imagesplit( image_path.data(), 5400, 0, 4860000 );
        std::vector<std::vector<uint32_t>> chunkOptions;
        chunkOptions.push_back( { 1080, 0, 4320, 5, 5, 24 } );
        std::list<SGProcessing::Task> tasks;
        size_t                        nTasks = 1;
        for ( size_t taskIdx = 0; taskIdx < nTasks; ++taskIdx )
        {
            SGProcessing::Task task;
            //std::cout << "CID STRING:    " << libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value() << std::endl;
            //task.set_ipfs_block_id(libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value());
            task.set_ipfs_block_id( "QmagrfcEhX6aVuFqrRoUU5K6yvjpNiCxnJA6o2tT38Kvxx" );
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

        size_t                          nSubTasks = chunkOptions.size();
        size_t                          nChunks   = 0;
        processing::ProcessTaskSplitter taskSplitter( nSubTasks, nChunks, false );

        for ( auto &task : tasks )
        {
            std::cout << "subtask" << std::endl;
            std::list<SGProcessing::SubTask> subTasks;
            taskSplitter.SplitTask( task, subTasks, imagesplit, chunkOptions );
            task_queue_->EnqueueTask( task, subTasks );
        }

        transaction_manager_->HoldEscrow( funds, nChunks, uint256_t{ std::string( dev_config_.Addr ) }, dev_config_.Cut,
                                          "QmagrfcEhX6aVuFqrRoUU5K6yvjpNiCxnJA6o2tT38Kvxx" );
    }
    void AccountManager::MintTokens( uint64_t amount )
    {
        transaction_manager_->MintFunds( amount );
    }
    /*
    static AccountManager instance( ACCOUNT_KEY, DEV_CONFIG );
    AccountManager       &AccountManager::GetInstance()
    {
        return instance;
    }
    */
}