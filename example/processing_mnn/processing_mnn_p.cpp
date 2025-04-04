#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

#include "base/logger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "processing/impl/processing_core_impl.hpp"
#include "processing/impl/processing_subtask_result_storage_impl.hpp"
#include "processing/impl/processing_task_queue_impl.hpp"
#include "processing/processing_service.hpp"
#include "processing/processing_subtask_enqueuer_impl.hpp"
#include "processing/processors/processing_processor_mnn_image.hpp"
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
    level: warning
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
    loggerProcessingEngine->set_level(spdlog::level::debug);

    auto loggerProcessingService = sgns::base::createLogger("ProcessingService");
    loggerProcessingService->set_level(spdlog::level::debug);

    auto loggerProcessingQueueManager = sgns::base::createLogger("ProcessingSubTaskQueueManager");
    loggerProcessingQueueManager->set_level(spdlog::level::debug);

    auto loggerGlobalDB = sgns::base::createLogger("GlobalDB");
    loggerGlobalDB->set_level(spdlog::level::trace);

    auto loggerDAGSyncer = sgns::base::createLogger("GraphsyncDAGSyncer");
    loggerDAGSyncer->set_level(spdlog::level::trace);

    auto loggerBroadcaster = sgns::base::createLogger("PubSubBroadcasterExt");
    loggerBroadcaster->set_level(spdlog::level::trace);

    auto loggerValidcore = sgns::base::createLogger("ProcessingValidationCore");
    loggerValidcore->set_level(spdlog::level::trace);
    std::cout << "Check 1" << std::endl;
    //Inputs
    char* endPtr;
    size_t serviceindex = std::strtoul(argv[1], &endPtr, 10);
    

    //Split Image into RGBA bytes
    //ImageSplitter imagesplit(inputImageFileName, 128, 128);


    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    //Create Pubsub
    auto pubsubKeyPath = (boost::format("CRDT.Datastore.TEST.%d/pubs_processor") % serviceindex).str();
    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage(pubsubKeyPath).GetKeyPair().value());

    const size_t maximalNodesCount = 1;


    //Asio Context
    auto io = std::make_shared<boost::asio::io_context>();
    
    //Client
    pubs2->Start(40001 + serviceindex, { "/ip4/192.168.46.18/tcp/40001/p2p/12D3KooWGvxwN8q3GqM2McBcZgGjpc86qPbKGdTG63T2sJUhcibh" });
    
    //Create GlobalDB
    //size_t serviceindex = 1;
    auto globalDB2 = std::make_shared<sgns::crdt::GlobalDB>(
        io,
        (boost::format("CRDT.Datastore.TEST.%d") % serviceindex).str(),
        40010 + serviceindex,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs2, "CRDT.Datastore.TEST.Channel"));
    auto crdtOptions2 = sgns::crdt::CrdtOptions::DefaultOptions();
    auto initRes = globalDB2->Init(crdtOptions2);

    //Processing Service Values
    auto taskQueue2 = std::make_shared<sgns::processing::ProcessingTaskQueueImpl>(globalDB2);
    auto enqueuer2 = std::make_shared<SubTaskEnqueuerImpl>(taskQueue2);

    //Processing Core
    auto processingCore2 = std::make_shared<ProcessingCoreImpl>(
        globalDB2,
        1000000,
        2);
    processingCore2->RegisterProcessorFactory("mnnimage", []() { return std::make_unique<MNN_Image>(); });
    //processingCore2->SetProcessorByName("posenet");
    //Set Imagesplit, this replaces bitswap getting of file for now. Should use AsyncIOmanager in the future
    //processingCore2->setImageSplitter(imagesplit);
    //processingCore2->setModelFile(poseModel);

    ProcessingServiceImpl processingService(
        pubs2,
        maximalNodesCount,
        enqueuer2,
        std::make_shared<SubTaskStateStorageImpl>(),
        std::make_shared<SubTaskResultStorageImpl>(globalDB2),
        processingCore2);
    
    processingService.SetChannelListRequestTimeout(boost::posix_time::milliseconds(10000));


    processingService.StartProcessing(processingGridChannel);

    
    
    //Run ASIO
    std::thread iothread([io]() { io->run(); });
    // Gracefully shutdown on signal
    boost::asio::signal_set signals(*pubs2->GetAsioContext(), SIGINT, SIGTERM);
    signals.async_wait(
        [&pubs2, &io](const boost::system::error_code&, int)
        {
            pubs2->Stop();
            io->stop();
        });

    pubs2->Wait();
    iothread.join();

}