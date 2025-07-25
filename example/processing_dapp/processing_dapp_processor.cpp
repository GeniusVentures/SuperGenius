#include <iostream>
#include <thread>
#include <boost/program_options.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include "processing/impl/processing_task_queue_impl.hpp"
#include "processing/impl/processing_subtask_result_storage_impl.hpp"
#include "processing/processing_service.hpp"
#include "processing/processing_subtask_enqueuer_impl.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/protocol/common/asio/asio_scheduler.hpp>

using namespace sgns::processing;

namespace
{
    class SubTaskStateStorageImpl : public SubTaskStateStorage
    {
    public:
        void ChangeSubTaskState( const std::string &subTaskId, SGProcessing::SubTaskState::Type state ) override {}

        std::optional<SGProcessing::SubTaskState> GetSubTaskState( const std::string &subTaskId ) override
        {
            return std::nullopt;
        }
    };

    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl( std::shared_ptr<sgns::crdt::GlobalDB> db,
                            size_t                                subTaskProcessingTime,
                            size_t                                maximalProcessingSubTaskCount ) :
            m_db( std::move( db ) ),
            m_subTaskProcessingTime( subTaskProcessingTime ),
            m_maximalProcessingSubTaskCount( maximalProcessingSubTaskCount ),
            m_processingSubTaskCount( 0 )
        {
        }

        bool SetProcessingTypeFromJson( std::string jsondata ) override
        {
            return true; //TODO - This is wrong - Update this tests to the actual ProcessingCoreImpl on src/processing/impl
        }

        std::shared_ptr<std::pair<std::shared_ptr<std::vector<char>>, std::shared_ptr<std::vector<char>>>>
        GetCidForProc( std::string json_data, std::string base_json ) override
        {
            return nullptr;
        }

        void GetSubCidForProc( std::shared_ptr<boost::asio::io_context> ioc,
                               std::string                              url,
                               std::shared_ptr<std::vector<char>>       results ) override
        {
            return;
        }

        outcome::result<SGProcessing::SubTaskResult> ProcessSubTask( const SGProcessing::SubTask &subTask,
                                                                     uint32_t initialHashCode ) override
        {
            SGProcessing::SubTaskResult result;
            {
                std::scoped_lock<std::mutex> subTaskCountLock( m_subTaskCountMutex );
                ++m_processingSubTaskCount;
                if ( ( m_maximalProcessingSubTaskCount > 0 ) &&
                     ( m_processingSubTaskCount > m_maximalProcessingSubTaskCount ) )
                {
                    // Reset the counter to allow processing restart
                    m_processingSubTaskCount = 0;
                    //throw std::runtime_error("Maximal number of processed subtasks exceeded");
                    return outcome::failure( boost::system::error_code{} );
                }
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( m_subTaskProcessingTime ) );
            result.set_ipfs_results_data_id( ( boost::format( "%s_%s" ) % "RESULT_IPFS" % subTask.subtaskid() ).str() );

            bool   isValidationSubTask = ( subTask.subtaskid() == "subtask_validation" );
            size_t subTaskResultHash   = initialHashCode;
            for ( int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx )
            {
                const auto &chunk = subTask.chunkstoprocess( chunkIdx );

                // Chunk result hash should be calculated
                // Chunk data hash is calculated just as a stub
                size_t chunkHash = 0;
                if ( isValidationSubTask )
                {
                    chunkHash = ( (size_t)chunkIdx < m_validationChunkHashes.size() )
                                    ? m_validationChunkHashes[chunkIdx]
                                    : std::hash<std::string>{}( chunk.SerializeAsString() );
                }
                else
                {
                    chunkHash = ( (size_t)chunkIdx < m_chunkResulHashes.size() )
                                    ? m_chunkResulHashes[chunkIdx]
                                    : std::hash<std::string>{}( chunk.SerializeAsString() );
                }

                std::string chunkHashString( reinterpret_cast<const char *>( &chunkHash ), sizeof( chunkHash ) );
                result.add_chunk_hashes( chunkHashString );
                boost::hash_combine( subTaskResultHash, chunkHash );
            }
            std::string hashString( reinterpret_cast<const char *>( &subTaskResultHash ), sizeof( subTaskResultHash ) );
            result.set_result_hash( hashString );
            return result;
        }

        std::vector<size_t> m_chunkResulHashes;
        std::vector<size_t> m_validationChunkHashes;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        size_t                                m_subTaskProcessingTime;
        size_t                                m_maximalProcessingSubTaskCount;

        std::mutex m_subTaskCountMutex;
        size_t     m_processingSubTaskCount;
    };

    // cmd line options
    struct Options
    {
        size_t serviceIndex              = 0;
        size_t subTaskProcessingTime     = 0; // ms
        size_t disconnect                = 0;
        size_t channelListRequestTimeout = 5000;
        // optional remote peer to connect to
        std::optional<std::string> remote;
        std::vector<size_t>        chunkResultHashes;
        std::vector<size_t>        validationChunkHashes;
        size_t maximalSubTaskCount = 0; // Maximal number of subtasks that can be processed by a single processing core
    };

    boost::optional<Options> parseCommandLine( int argc, char **argv )
    {
        namespace po = boost::program_options;
        try
        {
            Options     o;
            std::string remote;

            po::options_description desc( "processing service options" );
            desc.add_options()( "help,h", "print usage message" )( "remote,r",
                                                                   po::value( &remote ),
                                                                   "remote service multiaddress to connect to" )(
                "processingtime,p",
                po::value( &o.subTaskProcessingTime ),
                "subtask processing time (ms)" )( "disconnect,d", po::value( &o.disconnect ), "disconnect after (ms)" )(
                "channellisttimeout,t",
                po::value( &o.channelListRequestTimeout ),
                "chnnel list request timeout (ms)" )(
                "serviceindex,i",
                po::value( &o.serviceIndex ),
                "index of the service in computational grid (has to be a unique value)" )(
                "chunkresulthashes,h",
                po::value<std::vector<size_t>>()->multitoken(),
                "chunk result hashes" )(
                "maxsubtasks,m",
                po::value( &o.maximalSubTaskCount ),
                "maximal number of subtasks that can be processed by a single processing core" )(
                "validationchunkhashes",
                po::value<std::vector<size_t>>()->multitoken(),
                "validation chunk result hashes" );

            po::variables_map vm;
            po::store( parse_command_line( argc, argv, desc ), vm );
            po::notify( vm );

            if ( vm.count( "help" ) != 0 || argc == 1 )
            {
                std::cerr << desc << "\n";
                return boost::none;
            }

            if ( o.serviceIndex == 0 )
            {
                std::cerr << "Service index should be > 0\n";
                return boost::none;
            }

            if ( o.subTaskProcessingTime == 0 )
            {
                std::cerr << "SubTask processing time should be > 0\n";
                return boost::none;
            }

            if ( !remote.empty() )
            {
                o.remote = remote;
            }

            if ( !vm["chunkresulthashes"].empty() )
            {
                o.chunkResultHashes = vm["chunkresulthashes"].as<std::vector<size_t>>();
            }

            if ( !vm["validationchunkhashes"].empty() )
            {
                o.validationChunkHashes = vm["validationchunkhashes"].as<std::vector<size_t>>();
            }

            return o;
        }
        catch ( const std::exception &e )
        {
            std::cerr << e.what() << std::endl;
        }
        return boost::none;
    }
}

int main( int argc, char *argv[] )
{
    auto options = parseCommandLine( argc, argv );
    if ( !options )
    {
        return 1;
    }

    const std::string logger_config( R"(
    # ----------------
    sinks:
      - name: console
        type: console
        color: true
    groups:
      - name: processing_dapp_processor
        sink: console
        level: info
        children:
          - name: libp2p
          - name: Gossip
    # ----------------
    )" );

    // prepare log system
    auto logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
        // Original LibP2P logging config
        std::make_shared<libp2p::log::Configurator>(),
        // Additional logging config for application
        logger_config ) );
    logging_system->configure();

    libp2p::log::setLoggingSystem( logging_system );

    auto loggerPubSub = libp2p::log::createLogger( "GossipPubSub" );
    //loggerPubSub->set_level(spdlog::level::trace);

    auto loggerProcessingEngine = sgns::base::createLogger( "ProcessingEngine" );
    loggerProcessingEngine->set_level( spdlog::level::trace );

    auto loggerProcessingService = sgns::base::createLogger( "ProcessingService" );
    loggerProcessingService->set_level( spdlog::level::trace );

    auto loggerProcessingTaskQueue = sgns::base::createLogger( "ProcessingTaskQueueImpl" );
    loggerProcessingTaskQueue->set_level( spdlog::level::debug );

    auto loggerProcessingSubTaskQueueManager = sgns::base::createLogger( "ProcessingSubTaskQueueManager" );
    loggerProcessingSubTaskQueueManager->set_level( spdlog::level::trace );

    auto loggerProcessingSubTaskQueue = sgns::base::createLogger( "ProcessingSubTaskQueue" );
    loggerProcessingSubTaskQueue->set_level( spdlog::level::debug );

    auto loggerProcessingSubTaskQueueAccessorImpl = sgns::base::createLogger( "ProcessingSubTaskQueueAccessorImpl" );
    loggerProcessingSubTaskQueueAccessorImpl->set_level( spdlog::level::debug );

    auto loggerGlobalDB = sgns::base::createLogger( "GlobalDB" );
    loggerGlobalDB->set_level( spdlog::level::debug );

    auto loggerDAGSyncer = sgns::base::createLogger( "GraphsyncDAGSyncer" );
    loggerDAGSyncer->set_level( spdlog::level::trace );

    auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
    loggerBroadcaster->set_level( spdlog::level::debug );

    auto loggerEnqueuer = sgns::base::createLogger( "SubTaskEnqueuerImpl" );
    loggerEnqueuer->set_level( spdlog::level::debug );

    auto loggerQueueChannel = sgns::base::createLogger( "ProcessingSubTaskQueueChannelPubSub" );
    loggerQueueChannel->set_level( spdlog::level::debug );

    //
    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    auto pubsubKeyPath = ( boost::format( "CRDT.Datastore.TEST.%d/pubs_processor" ) % options->serviceIndex ).str();
    auto pubs          = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage( pubsubKeyPath ).GetKeyPair().value() );

    std::vector<std::string> pubsubBootstrapPeers;
    if ( options->remote )
    {
        pubsubBootstrapPeers = std::vector( { *options->remote } );
    }
    pubs->Start( 40001, pubsubBootstrapPeers );

    const size_t maximalNodesCount = 1;

    boost::asio::deadline_timer timerToDisconnect( *pubs->GetAsioContext() );
    if ( options->disconnect > 0 )
    {
        timerToDisconnect.expires_from_now( boost::posix_time::milliseconds( options->disconnect ) );
        timerToDisconnect.async_wait(
            [pubs, &timerToDisconnect]( const boost::system::error_code &error )
            {
                timerToDisconnect.expires_at( boost::posix_time::pos_infin );
                pubs->Stop();
            } );
    }

    auto io          = std::make_shared<boost::asio::io_context>();
    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    auto scheduler   = std::make_shared<libp2p::protocol::AsioScheduler>( io, libp2p::protocol::SchedulerConfig{} );
    auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs->GetHost(), scheduler );
    auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
    auto globaldb_ret     = sgns::crdt::GlobalDB::New(
        io,
        ( boost::format( "CRDT.Datastore.TEST.%d" ) % options->serviceIndex ).str(),
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

    std::thread iothread( [io]() { io->run(); } );

    auto taskQueue = std::make_shared<ProcessingTaskQueueImpl>( globalDB, "test" );

    auto processingCore                     = std::make_shared<ProcessingCoreImpl>( globalDB,
                                                                options->subTaskProcessingTime,
                                                                options->maximalSubTaskCount );
    processingCore->m_chunkResulHashes      = options->chunkResultHashes;
    processingCore->m_validationChunkHashes = options->validationChunkHashes;

    auto enqueuer = std::make_shared<SubTaskEnqueuerImpl>( taskQueue );

    ProcessingServiceImpl processingService( pubs,
                                             maximalNodesCount,
                                             enqueuer,
                                             std::make_shared<SubTaskStateStorageImpl>(),
                                             std::make_shared<SubTaskResultStorageImpl>( globalDB, "test" ),
                                             processingCore );

    processingService.SetChannelListRequestTimeout(
        boost::posix_time::milliseconds( options->channelListRequestTimeout ) );

    processingService.StartProcessing( processingGridChannel );

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

    return 0;
}
