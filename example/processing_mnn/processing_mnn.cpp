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

    //Load Image
    const auto poseModel = argv[1];
    const auto inputImageFileName = argv[2];

    ImageSplitter imagesplit(inputImageFileName, 128, 128);
    std::cout << "Image Split Size: " << imagesplit.GetPartCount() << std::endl;
    //return 1;
    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    //Make Host Pubsubs
    std::vector<std::string> receivedMessages;
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage("CRDT.Datastore.TEST/pubs_dapp").GetKeyPair().value());


    //auto pubsubKeyPath = (boost::format("CRDT.Datastore.TEST.%d/pubs_processor") % 1).str();
    //auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
    //    sgns::crdt::KeyPairFileStorage(pubsubKeyPath).GetKeyPair().value());

    //Start Pubsubs, add peers of other addresses.
    pubs->Start(40001, { "/ip4/192.168.46.18/tcp/40002/p2p/12D3KooWDWi6q5Q67J7iox3P2xUoJtApE3eqtXB1c9wiJGDVTnAh" });

    

    const size_t maximalNodesCount = 1;

    std::list<SGProcessing::Task> tasks;
    size_t nTasks = 1;
    // Put tasks to Global DB
    for (size_t taskIdx = 0; taskIdx < nTasks; ++taskIdx)
    {
        // And wait for its processing
        //std::cout << "INdex " << taskIdx << std::endl;
        std::cout << "CIDString: " << libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value() << std::endl;
        SGProcessing::Task task;
        task.set_ipfs_block_id(libp2p::multi::ContentIdentifierCodec::toString(imagesplit.GetPartCID(taskIdx)).value());
        //task.set_ipfs_block_id((boost::format("IPFS_BLOCK_ID_%1%") % (taskIdx + 1)).str());
        task.set_block_len(imagesplit.GetPartSize(taskIdx));
        task.set_block_line_stride(imagesplit.GetPartStride(taskIdx));
        task.set_block_stride(imagesplit.GetPartSize(taskIdx));
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
    

    auto taskQueue = std::make_shared<sgns::processing::ProcessingTaskQueueImpl>(globalDB);
    size_t nSubTasks = 2;
    size_t nChunks = 2;
    TaskSplitter taskSplitter(
        nSubTasks,
        nChunks,
        false);

    for (auto& task : tasks)
    {
        std::list<SGProcessing::SubTask> subTasks;
        taskSplitter.SplitTask(task, subTasks, imagesplit);
        taskQueue->EnqueueTask(task, subTasks);
    }
    //std::cout << "LOCALADDRESS ::: " << pubs->GetLocalAddress() << std::endl;
    ////Client
    //pubs2->Start(40002, { pubs->GetLocalAddress() });

    ////GlobalDB
    //size_t serviceindex = 1;
    //auto globalDB2 = std::make_shared<sgns::crdt::GlobalDB>(
    //    io,
    //    (boost::format("CRDT.Datastore.TEST.%d") % serviceindex).str(),
    //    40010 + serviceindex,
    //    std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs2, "CRDT.Datastore.TEST.Channel"));
    //auto crdtOptions2 = sgns::crdt::CrdtOptions::DefaultOptions();
    //auto initRes = globalDB2->Init(crdtOptions2);
    ////Processing Service Values
    //auto taskQueue2 = std::make_shared<sgns::processing::ProcessingTaskQueueImpl>(globalDB2);
    ////auto processingCore = std::make_shared<ProcessingCoreImpl>(100000);
    //auto enqueuer2 = std::make_shared<SubTaskEnqueuerImpl>(taskQueue2);
    ////Processing Core
    //auto processingCore2 = std::make_shared<ProcessingCoreImpl>(
    //    globalDB2,
    //    1000000,
    //    2);

    //ProcessingServiceImpl processingService(
    //    pubs2,
    //    maximalNodesCount,
    //    enqueuer2,
    //    std::make_shared<SubTaskStateStorageImpl>(),
    //    std::make_shared<SubTaskResultStorageImpl>(globalDB2),
    //    processingCore2);

    //processingService.SetChannelListRequestTimeout(boost::posix_time::milliseconds(10000));

    //processingService.StartProcessing(processingGridChannel);

    
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