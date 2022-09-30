#include "infura_ipfs.hpp"
#include "kompute_manager.hpp"
#include "processing_subtask_result_storage_impl.hpp"

#include <processing/processing_task_queue_impl.hpp>
#include <processing/processing_service.hpp>
#include <processing/processing_subtask_enqueuer_impl.hpp>

#include <crdt/globaldb/keypair_file_storage.hpp>
#include <crdt/globaldb/globaldb.hpp>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

using namespace sgns::processing;

namespace
{
class SubTaskStateStorageImpl : public SubTaskStateStorage
{
public:
    void ChangeSubTaskState(const std::string& subTaskId, SGProcessing::SubTaskState::Type state) override {}
    std::optional<SGProcessing::SubTaskState> GetSubTaskState(const std::string& subTaskId) override
    {
        return std::nullopt;
    }
};

class ProcessingCoreImpl : public ProcessingCore
{
public:
    ProcessingCoreImpl(std::shared_ptr<InfuraIPFS> ipfs)
        : _ipfs(ipfs)
    {
    }

    void  ProcessSubTask(
        const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
        uint32_t initialHashCode) override
    {
        result.set_ipfs_results_data_id((boost::format("%s_%s") % "SG_RESULT" % subTask.subtaskid()).str());

        size_t subTaskResultHash = initialHashCode;

        // @todo The cid should be received from task info
        std::string dataCID = "QmQsYvj8JBa9A4wmXvxci63iz6wtLYdBE5XsRrKKxCgY9e";

        std::string data;
        _ipfs->GetIPFSFile(dataCID, data);

        // Desirialize data
        std::stringstream ss(data);
        uint64_t spirvModuleSize;
        ss.read(reinterpret_cast<char*>(&spirvModuleSize), sizeof(uint64_t));

        std::vector<char> spirvModuleData(spirvModuleSize);
        ss.read(reinterpret_cast<char*>(spirvModuleData.data()), spirvModuleSize);

        std::vector<uint32_t> spirv(
            (uint32_t*)spirvModuleData.data(), (uint32_t*)(spirvModuleData.data() + spirvModuleSize));


        uint64_t dataToProcessSize;
        ss.read(reinterpret_cast<char*>(&dataToProcessSize), sizeof(uint64_t));

        std::vector<float> dataToProcess(dataToProcessSize);
        ss.read(reinterpret_cast<char*>(dataToProcess.data()), dataToProcessSize * sizeof(float));


        //std::cout << spirv.size() << std::endl;
        //std::cout << dataToProcess.size() << std::endl;

        KomputeManager km(spirv);

        for (int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx)
        {
            const auto& chunk = subTask.chunkstoprocess(chunkIdx);

            // @todo Prepare chunk data to process?
            const auto& chunkDataToProcess = dataToProcess;

            // Chunk result hash should be calculated
            std::vector<float> results;
            km.Evaluate(chunkDataToProcess, results);

            // @todo Save results
            //for (auto& r : results)
            //{
            //    std::cout << r << std::endl;
            //}

            size_t chunkHash = boost::hash_range(results.begin(), results.end());

            result.add_chunk_hashes(chunkHash);
            boost::hash_combine(subTaskResultHash, chunkHash);
        }

        result.set_result_hash(subTaskResultHash);
    }
private:
    std::shared_ptr<InfuraIPFS> _ipfs;

};

// cmd line options
struct Options
{
    size_t serviceIndex = 0;

    // optional remote peer to connect to
    std::optional<std::string> remote;
};

boost::optional<Options> parseCommandLine(int argc, char** argv)
{
    namespace po = boost::program_options;
    try
    {
        Options o;
        std::string remote;

        po::options_description desc("processing service options");
        desc.add_options()("help,h", "print usage message")
            ("remote,r", po::value(&remote), "remote service multiaddress to connect to")
            ("serviceindex,i", po::value(&o.serviceIndex), "index of the service in computational grid (has to be a unique value)");

        po::variables_map vm;
        po::store(parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help") != 0 || argc == 1)
        {
            std::cerr << desc << "\n";
            return boost::none;
        }

        if (o.serviceIndex == 0)
        {
            std::cerr << "Service index should be > 0\n";
            return boost::none;
        }

        if (!remote.empty())
        {
            o.remote = remote;
        }

        return o;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    return boost::none;
}
} // namespace

int main(int argc, char** argv)
{
    auto options = parseCommandLine(argc, argv);
    if (!options)
    {
        return 1;
    }

    const std::string logger_config(R"(
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
    )");

    // prepare log system
    auto logging_system = std::make_shared<soralog::LoggingSystem>(
        std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config));
    logging_system->configure();

    libp2p::log::setLoggingSystem(logging_system);

    auto loggerPubSub = libp2p::log::createLogger("GossipPubSub");
    //loggerPubSub->set_level(spdlog::level::trace);

    auto loggerProcessingEngine = sgns::base::createLogger("ProcessingEngine");
    loggerProcessingEngine->set_level(spdlog::level::trace);

    auto loggerProcessingService = sgns::base::createLogger("ProcessingService");
    loggerProcessingService->set_level(spdlog::level::trace);

    auto loggerProcessingTaskQueue = sgns::base::createLogger("ProcessingTaskQueueImpl");
    loggerProcessingTaskQueue->set_level(spdlog::level::debug);

    auto loggerProcessingSubTaskQueueManager = sgns::base::createLogger("ProcessingSubTaskQueueManager");
    loggerProcessingSubTaskQueueManager->set_level(spdlog::level::trace);

    auto loggerProcessingSubTaskQueue = sgns::base::createLogger("ProcessingSubTaskQueue");
    loggerProcessingSubTaskQueue->set_level(spdlog::level::debug);

    auto loggerProcessingSubTaskQueueAccessorImpl = sgns::base::createLogger("ProcessingSubTaskQueueAccessorImpl");
    loggerProcessingSubTaskQueueAccessorImpl->set_level(spdlog::level::debug);

    auto loggerGlobalDB = sgns::base::createLogger("GlobalDB");
    loggerGlobalDB->set_level(spdlog::level::debug);

    auto loggerDAGSyncer = sgns::base::createLogger("GraphsyncDAGSyncer");
    loggerDAGSyncer->set_level(spdlog::level::trace);

    auto loggerBroadcaster = sgns::base::createLogger("PubSubBroadcasterExt");
    loggerBroadcaster->set_level(spdlog::level::debug);

    auto loggerEnqueuer = sgns::base::createLogger("SubTaskEnqueuerImpl");
    loggerEnqueuer->set_level(spdlog::level::debug);

    auto loggerQueueChannel = sgns::base::createLogger("ProcessingSubTaskQueueChannelPubSub");
    loggerQueueChannel->set_level(spdlog::level::debug);

    std::ifstream autorizationKeyFile("authorizationKey.txt");

    if (!autorizationKeyFile.is_open())
    {
        std::cout << "Infura authorization key file not found" << std::endl;
        return EXIT_FAILURE;
    }
    std::string autorizationKey;
    std::getline(autorizationKeyFile, autorizationKey);

    auto ipfs = std::make_shared<InfuraIPFS>(autorizationKey);
    auto processingCore = std::make_shared<ProcessingCoreImpl>(ipfs);

    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    auto pubsubKeyPath = (boost::format("CRDT.Datastore.TEST.%d/pubs_processor") % options->serviceIndex).str();
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage(pubsubKeyPath).GetKeyPair().value());

    std::vector<std::string> pubsubBootstrapPeers;
    if (options->remote)
    {
        pubsubBootstrapPeers = std::move(std::vector({ *options->remote }));
    }
    pubs->Start(40001, pubsubBootstrapPeers);

    const size_t maximalNodesCount = 1;

    auto io = std::make_shared<boost::asio::io_context>();
    auto globalDB = std::make_shared<sgns::crdt::GlobalDB>(
        io,
        (boost::format("CRDT.Datastore.TEST.%d") % options->serviceIndex).str(),
        40010 + options->serviceIndex,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, "CRDT.Datastore.TEST.Channel"));

    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    auto initRes = globalDB->Init(crdtOptions);

    std::thread iothread([io]() { io->run(); });

    auto taskQueue = std::make_shared<ProcessingTaskQueueImpl>(globalDB);

    auto enqueuer = std::make_shared<SubTaskEnqueuerImpl>(taskQueue);

    ProcessingServiceImpl processingService(
        pubs,
        maximalNodesCount,
        enqueuer,
        std::make_shared<SubTaskStateStorageImpl>(),
        std::make_shared<SubTaskResultStorageImpl>(globalDB),
        processingCore);

    processingService.SetChannelListRequestTimeout(boost::posix_time::seconds(5));
    processingService.SetTaskProcessingFinalizationSink(
        [taskQueue, pubs](const std::string& subTaskQueueId, const SGProcessing::TaskResult& taskResult)
        {
            taskQueue->CompleteTask(subTaskQueueId, taskResult);

            std::string taskStatusChannel = (boost::format("TASK_STATUS_%s") % subTaskQueueId).str();
            auto taskCompleteChannel =
                std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, taskStatusChannel);
            taskCompleteChannel->Publish("COMPLETE");
            std::cout << "TASK_STATUS_CHANNEL_ID: " << taskStatusChannel << std::endl;
        });

    processingService.StartProcessing(processingGridChannel);

    // Gracefully shutdown on signal
    boost::asio::signal_set signals(*pubs->GetAsioContext(), SIGINT, SIGTERM);
    signals.async_wait(
        [&pubs, &io](const boost::system::error_code&, int)
        {
            pubs->Stop();
            io->stop();
        });

    pubs->Wait();
    iothread.join();

    return 0;
}