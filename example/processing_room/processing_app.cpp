#include "processing/processing_service.hpp"
#include "processing/processing_subtask_enqueuer_impl.hpp"

#include <iostream>
#include <thread>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

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

    class SubTaskResultStorageImpl : public SubTaskResultStorage
    {
    public:
        void AddSubTaskResult( const SGProcessing::SubTaskResult &subTaskResult ) override {}

        void RemoveSubTaskResult( const std::string &subTaskId ) override {}

        std::vector<SGProcessing::SubTaskResult> GetSubTaskResults( const std::set<std::string> &subTaskIds ) override
        {
            return {};
        }
    };

    class TaskSplitter
    {
    public:
        TaskSplitter( size_t nSubtasks ) : m_nSubtasks( nSubtasks ) {}

        void SplitTask( const SGProcessing::Task &task, std::list<SGProcessing::SubTask> &subTasks )
        {
            for ( size_t i = 0; i < m_nSubtasks; ++i )
            {
                SGProcessing::SubTask subtask;
                subtask.set_ipfsblock( task.ipfs_block_id() );
                subtask.set_subtaskid( ( boost::format( "%s_subtask_%d" ) % task.results_channel() % i ).str() );
                subTasks.push_back( std::move( subtask ) );
            }
        }

    private:
        size_t m_nSubtasks;
    };

    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl( size_t subTaskProcessingTime ) : m_subTaskProcessingTime( subTaskProcessingTime ) {}

        bool SetProcessingTypeFromJson( std::string jsondata ) override
        {
            return true; //TODO - This is wrong - Update this tests to the actual ProcessingCoreImpl on src/processing/impl
        }

        std::shared_ptr<std::pair<std::shared_ptr<std::vector<char>>, std::shared_ptr<std::vector<char>>>>  GetCidForProc( std::string json_data,
                                                                                        std::string base_json ) override
        {
            return nullptr;
        }


        void GetSubCidForProc( std::shared_ptr<boost::asio::io_context> ioc, std::string                              url,
                               std::shared_ptr<std::vector<char>> results) override
        {
            return;
        }

        outcome::result<SGProcessing::SubTaskResult> ProcessSubTask( const SGProcessing::SubTask &subTask,
                                                                     uint32_t initialHashCode ) override
        {
            SGProcessing::SubTaskResult result;
            std::cout << "SubTask processing started. " << subTask.subtaskid() << std::endl;
            std::this_thread::sleep_for( std::chrono::milliseconds( m_subTaskProcessingTime ) );
            std::cout << "SubTask processed. " << subTask.subtaskid() << std::endl;
            result.set_ipfs_results_data_id( ( boost::format( "%s_%s" ) % "RESULT_IPFS" % subTask.subtaskid() ).str() );
            return result;
        }

    private:
        size_t m_subTaskProcessingTime;
    };

    class ProcessingTaskQueueImpl : public ProcessingTaskQueue
    {
    public:
        ProcessingTaskQueueImpl() {}

        outcome::result<void> EnqueueTask( const SGProcessing::Task               &task,
                                           const std::list<SGProcessing::SubTask> &subTasks ) override
        {
            m_tasks.push_back( task );
            m_subTasks.emplace( task.ipfs_block_id(), subTasks );
            return outcome::success();
        }

        bool GetSubTasks( const std::string &taskId, std::list<SGProcessing::SubTask> &subTasks ) override
        {
            auto it = m_subTasks.find( taskId );
            if ( it != m_subTasks.end() )
            {
                subTasks = it->second;
                return true;
            }

            return false;
        }

        outcome::result<std::pair<std::string, SGProcessing::Task>> GrabTask() override
        {
            if ( m_tasks.empty() )
            {
                return outcome::failure( boost::system::error_code{} );
            }

            SGProcessing::Task task;
            task = std::move( m_tasks.back() );
            m_tasks.pop_back();
            std::string taskKey = ( boost::format( "TASK_%d" ) % m_tasks.size() ).str();

            return std::make_pair( taskKey, task );
        }

        bool IsTaskCompleted( const std::string &taskId ) override
        {
            return true;
        }

        outcome::result<std::shared_ptr<crdt::AtomicTransaction>> CompleteTask( const std::string &taskKey, const SGProcessing::TaskResult &task ) override
        {
            return outcome::success();
        }

    private:
        std::list<SGProcessing::Task>                           m_tasks;
        std::map<std::string, std::list<SGProcessing::SubTask>> m_subTasks;
    };

    // cmd line options
    struct Options
    {
        size_t serviceIndex              = 0;
        size_t subTaskProcessingTime     = 0; // ms
        size_t disconnect                = 0;
        size_t nSubTasks                 = 5;
        size_t channelListRequestTimeout = 5000;
        // optional remote peer to connect to
        std::optional<std::string> remote;
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
                "nsubtasks,n",
                po::value( &o.nSubTasks ),
                "number of subtasks that task is split to" )( "channellisttimeout,t",
                                                              po::value( &o.channelListRequestTimeout ),
                                                              "chnnel list request timeout (ms)" )(
                "serviceindex,i",
                po::value( &o.serviceIndex ),
                "index of the service in computational grid (has to be a unique value)" );

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

    auto loggerPubSub = sgns::base::createLogger( "GossipPubSub" );
    //loggerPubSub->set_level(spdlog::level::trace);

    auto loggerProcessingEngine = sgns::base::createLogger( "ProcessingEngine" );
    loggerProcessingEngine->set_level( spdlog::level::trace );

    auto loggerProcessingService = sgns::base::createLogger( "ProcessingService" );
    loggerProcessingService->set_level( spdlog::level::trace );

    auto loggerProcessingQueueManager = sgns::base::createLogger( "ProcessingSubTaskQueueManager" );
    loggerProcessingQueueManager->set_level( spdlog::level::debug );

    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    const std::string logger_config( R"(
    # ----------------
    sinks:
      - name: console
        type: console
        color: true
    groups:
      - name: processing_app
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

    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();

    if ( options->serviceIndex == 1 )
    {
        std::string publicKey  = "z5b3BTS9wEgJxi9E8NHH6DT8Pj9xTmxBRgTaRUpBVox9a";
        std::string privateKey = "zGRXH26ag4k9jxTGXp2cg8n31CEkR2HN1SbHaKjaHnFTu";

        libp2p::crypto::KeyPair keyPair;
        auto                    codec = libp2p::multi::MultibaseCodecImpl();
        keyPair.publicKey             = { libp2p::crypto::PublicKey::Type::Ed25519, codec.decode( publicKey ).value() };
        keyPair.privateKey = { libp2p::crypto::PublicKey::Type::Ed25519, codec.decode( privateKey ).value() };

        pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
    }

    if ( options->remote )
    {
        pubs->Start( 40001, { *options->remote } );
    }
    else
    {
        pubs->Start( 40001, {} );
    }

    const size_t maximalNodesCount = 1;

    std::list<SGProcessing::Task> tasks;

    // Only a service with index equal to 1 has a locked task (job)
    if ( options->serviceIndex == 1 )
    {
        SGProcessing::Task task;
        task.set_ipfs_block_id( "IPFS_BLOCK_ID_1" );
        //task.set_block_len(1000);
        //task.set_block_line_stride(2);
        //task.set_block_stride(4);
        task.set_random_seed( 0 );
        task.set_results_channel( "RESULT_CHANNEL_ID_1" );
        tasks.push_back( std::move( task ) );
    }

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

    auto taskQueue    = std::make_shared<ProcessingTaskQueueImpl>();
    auto taskSplitter = std::make_shared<TaskSplitter>( options->nSubTasks );
    for ( auto &task : tasks )
    {
        std::list<SGProcessing::SubTask> subTasks;
        taskSplitter->SplitTask( task, subTasks );
        taskQueue->EnqueueTask( task, subTasks );
    }

    auto                  processingCore = std::make_shared<ProcessingCoreImpl>( options->subTaskProcessingTime );
    auto                  enqueuer       = std::make_shared<SubTaskEnqueuerImpl>( taskQueue );
    ProcessingServiceImpl processingService( pubs,
                                             maximalNodesCount,
                                             enqueuer,
                                             std::make_shared<SubTaskStateStorageImpl>(),
                                             std::make_shared<SubTaskResultStorageImpl>(),
                                             processingCore );

    processingService.SetChannelListRequestTimeout(
        boost::posix_time::milliseconds( options->channelListRequestTimeout ) );

    processingService.StartProcessing( processingGridChannel );

    // Gracefully shutdown on signal
    boost::asio::signal_set signals( *pubs->GetAsioContext(), SIGINT, SIGTERM );
    signals.async_wait( [&pubs]( const boost::system::error_code &, int ) { pubs->Stop(); } );

    pubs->Wait();

    return 0;
}
