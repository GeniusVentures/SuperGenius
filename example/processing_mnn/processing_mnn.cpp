#include "processing_mnn.hpp"

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
const std::string logger_config(R"(
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
  )");

int main(int argc, char* argv[])
{
    // prepare log system
    auto logging_system = std::make_shared<soralog::LoggingSystem>(
        std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config));
    logging_system->configure();

    libp2p::log::setLoggingSystem(logging_system);
    libp2p::log::setLevelOfGroup("gossip_pubsub_test", soralog::Level::ERROR_);
    auto loggerProcessingEngine = sgns::base::createLogger("ProcessingEngine");
    loggerProcessingEngine->set_level(spdlog::level::trace);

    auto loggerProcessingService = sgns::base::createLogger("ProcessingService");
    loggerProcessingService->set_level(spdlog::level::trace);

    auto loggerProcessingQueueManager = sgns::base::createLogger("ProcessingSubTaskQueueManager");
    loggerProcessingQueueManager->set_level(spdlog::level::debug);

    auto loggerGlobalDB = sgns::base::createLogger("GlobalDB");
    loggerGlobalDB->set_level(spdlog::level::trace);

    auto loggerDAGSyncer = sgns::base::createLogger("GraphsyncDAGSyncer");
    loggerDAGSyncer->set_level(spdlog::level::trace);

    auto loggerBroadcaster = sgns::base::createLogger("PubSubBroadcasterExt");
    loggerBroadcaster->set_level(spdlog::level::trace);
    //Chunk Options 
    std::vector<std::vector<uint32_t>> chunkOptions;
    chunkOptions.push_back({ 1080, 0, 4320, 5, 5, 24 });
    chunkOptions.push_back({ 540, 0, 4860, 10, 10, 99});
    chunkOptions.push_back({ 270, 0, 5130, 20, 20, 399});
    //Inputs
    const auto poseModel = argv[1];
    const auto inputImageFileName = argv[2];

    //Split Image into RGBA bytes
    //ImageSplitter imagesplit(inputImageFileName, 540, 4860, 48600);
    ImageSplitter imagesplit(inputImageFileName, 5400, 0, 4860000);
    // For 1350x900 broken into 135x90
    //bytes - 48,600
    //Block Stride - 540
    //Block Line Strike - 4860
    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    //Make Host Pubsubs
    std::vector<std::string> receivedMessages;
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage("CRDT.Datastore.TEST/pubs_dapp").GetKeyPair().value());

    //Start Pubsubs, add peers of other addresses. We'll probably use DHT Discovery bootstrapping in the future.
    pubs->Start(40001, { "/ip4/192.168.46.18/tcp/40002/p2p/12D3KooWRm16iwAdRsAYzGGXU9rq9ZqGbJqaP2kYxe4mCdhEQz67",
        "/ip4/192.168.46.18/tcp/40003/p2p/12D3KooWEAKCDGsZA4MvDVDEzx7pA8rD6UyN6AXsGDCYChWce4Zi",
        "/ip4/192.168.46.18/tcp/40004/p2p/12D3KooWKTNC88yV3g7bhBdTBxKqy1GT9GQhE6VP28BvVsUtdhX5",
        "/ip4/192.168.46.18/tcp/40005/p2p/12D3KooWJXWW1mXV1rxf7zuspGTyyk5irwyfXKNy71AcYzGqUT29",
        "/ip4/192.168.46.18/tcp/40006/p2p/12D3KooWELZFDj8qdMNKoXnc7bndgRQU4yhbaM7tqHCenjqpvn5P",
        "/ip4/192.168.46.18/tcp/40007/p2p/12D3KooWR5SynE87dtwYd62K3GRGnoabpMsmyTiLc9pLhxMEXMgY",
        "/ip4/192.168.46.18/tcp/40008/p2p/12D3KooWQp3fX4ZR8LevJMt8coBZa4AZK4VrsaQ22CxpWYiu5qj5",
        "/ip4/192.168.46.18/tcp/40009/p2p/12D3KooWJGKGW5ETP6zxouH2HSSPhHxu8gpF8AbCGugVDY118UyS",
        "/ip4/192.168.46.18/tcp/40010/p2p/12D3KooWDT7becpKYbZrH1ZfBibsfdMqHo6AMHr3b5aGCMLw91XW"});
    std::cout << "Check 5" << std::endl;
    const size_t maximalNodesCount = 1;

    //Make Tasks
    std::list<SGProcessing::Task> tasks;
    size_t nTasks = 2;
    // Put tasks to Global DB
    for (size_t taskIdx = 0; taskIdx < nTasks; ++taskIdx)
    {
        SGProcessing::Task task;
        //std::cout << "CID STRING:    " << libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value() << std::endl;
        //task.set_ipfs_block_id(libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value());
        task.set_ipfs_block_id("Qmbi9eFJSDyyoU2HiPJyGvwLb3rEacs78WUFbpKEYSzR47");
        //task.set_block_len(48600);
        //task.set_block_line_stride(540);
        //task.set_block_stride(4860);
        task.set_block_len(4860000);
        task.set_block_line_stride(5400);
        task.set_block_stride(0);
        task.set_random_seed(0);
        task.set_results_channel((boost::format("RESULT_CHANNEL_ID_%1%") % (taskIdx + 1)).str());
        tasks.push_back(std::move(task));
    }

    //Asio Context
    auto io = std::make_shared<boost::asio::io_context>();

    //Add to GlobalDB
    auto globalDB = std::make_shared<sgns::crdt::GlobalDB>(
        io, "CRDT.Datastore.TEST", 40000,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, "CRDT.Datastore.TEST.Channel"));
    
    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    globalDB->Init(crdtOptions);
    
    //Split tasks into subtasks
    auto taskQueue = std::make_shared<sgns::processing::ProcessingTaskQueueImpl>(globalDB);

    size_t nSubTasks = 1;
    size_t nChunks = 0;
    TaskSplitter taskSplitter(
        nSubTasks,
        nChunks,
        false);

    int chunkopt = 0;
    for (auto& task : tasks)
    {
        std::cout << "subtask" << std::endl;
        std::list<SGProcessing::SubTask> subTasks;
        taskSplitter.SplitTask(task, subTasks, imagesplit, chunkOptions.at(chunkopt));
        taskQueue->EnqueueTask(task, subTasks);
        chunkopt++;
    }
    
    //Run ASIO
    std::thread iothread([io]() { io->run(); });
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

}