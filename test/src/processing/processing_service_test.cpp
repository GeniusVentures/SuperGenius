
#include "processing_service_test.hpp"
#include "processing/processing_service.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <gtest/gtest.h>
#include "testutil/wait_condition.hpp"

using namespace sgns::processing;
using namespace sgns::test;

const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_service_test
    sink: console
    level: off
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

void ProcessingServiceTest::SetUp()
{
    SetUp("processing_service_test", logger_config);
    Initialize(2, 50);
}

void ProcessingServiceTest::SetUp(std::string name, std::string loggerConfig)
{
    // prepare log system
    auto logSystem = std::make_shared<soralog::LoggingSystem>(
        std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            loggerConfig));
    auto result = logSystem->configure();

    libp2p::log::setLoggingSystem(logSystem);

    m_Logger = logSystem->getLogger("console", name);

#ifdef SGNS_DEBUGLOGS
    libp2p::log::setLevelOfGroup(name, soralog::Level::DEBUG);

    auto loggerProcQM  = sgns::base::createLogger( "ProcessingSubTaskQueueManager" );
    loggerProcQM->set_level( spdlog::level::debug );

    loggerProcQM  = sgns::base::createLogger( "ProcessingSubTaskQueue");
    loggerProcQM->set_level( spdlog::level::debug );

    loggerProcQM  = sgns::base::createLogger( "ProcessingSubTaskQueueAccessorImpl");
    loggerProcQM->set_level( spdlog::level::debug );
#else
    libp2p::log::setLevelOfGroup(name, soralog::Level::ERROR_);

    auto loggerGS  = sgns::base::createLogger( "GossipPubsub");
    loggerGS->set_level( spdlog::level::err );
#endif

}

void ProcessingServiceTest::TearDown()
{
    for (auto& pubs : m_pubsub_nodes)
    {
        pubs->Stop();
        pubs->Wait();
    }

    m_processing_queues_accessors.clear();
    m_IsTaskFinalized.clear();
    m_processing_engines.clear();
    m_processing_queues_managers.clear();
    m_processing_queues_channel_pub_subs.clear();
    m_processing_cores.clear();
    m_pubsub_futures.clear();
    m_pubsub_nodes.clear();
}

void ProcessingServiceTest::Initialize(uint64_t numNodes, size_t processingTime)
{
    // create 2 nodes default
    std::vector<std::string> bootstrap_nodes = {};
    for (size_t i = 0; i < numNodes; ++i)
    {
        auto pubsub_node = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
        m_pubsub_nodes.push_back(pubsub_node);
        m_pubsub_futures.push_back(m_pubsub_nodes[i]->Start(40001 + i, bootstrap_nodes));
        if (i == 0)
        {
            bootstrap_nodes = { pubsub_node->GetLocalAddress() };
        }
    }

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION(
        ([this]() {
            for (auto& pubs_future : m_pubsub_futures)
            {
                try
                {
                   pubs_future.get();
                } catch (const std::exception& e) {
                    m_Logger->error("Pubsub node start failed: {}", e.what());
                }
            }
            return true;
        }),
        std::chrono::milliseconds(2000),
        "Pubsub nodes start during initialization failed",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for pubsub node initialization");

    for (size_t i = 0; i < numNodes; ++i)
    {
        std::string nodeId = "NODE_" + std::to_string(i+1);
        auto pubsub_node = m_pubsub_nodes[i];
        // Both nodes process at the same speed
        auto proccessingCore =
            std::make_shared<sgns::test::ProcessingCoreImpl>( processingTime );
        m_processing_cores.push_back(proccessingCore);
        auto queuePubSubChannel =
            std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubsub_node, "QUEUE_CHANNEL_ID");
        m_processing_queues_channel_pub_subs.push_back(queuePubSubChannel);
        auto processingQueueManager =
            std::make_shared<ProcessingSubTaskQueueManager>(
                queuePubSubChannel, pubsub_node->GetAsioContext(), nodeId, [](const std::string &){});
        m_processing_queues_managers.push_back(processingQueueManager);
        auto processingEngine =
            std::make_shared<ProcessingEngine>(nodeId, proccessingCore, [](const std::string &){},[]{});
        m_processing_engines.push_back(processingEngine);
        m_IsTaskFinalized.push_back(std::make_unique<std::atomic<bool>>(false));
        auto queueAccessor =
            std::make_shared<SubTaskQueueAccessorImpl>(
            pubsub_node,
            processingQueueManager,
            std::make_shared<SubTaskStateStorageMock>(),
            std::make_shared<SubTaskResultStorageMock>(),
            [this, i, nodeId](const SGProcessing::TaskResult&)
            {
                m_IsTaskFinalized[i]->store(true);
                Color::PrintInfo("Task finalized by ", nodeId);
            },
            [](const std::string &) {});

        m_processing_queues_accessors.push_back(queueAccessor);
        queueAccessor->CreateResultsChannel("test");
    }

}

/**
 * @given Empty queue list
 * @when A queue channel received
 * @then A processing node is created
 */
TEST_F(ProcessingServiceTest, ProcessingSlotsAreAvailable)
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue = std::make_shared<ProcessingTaskQueueImpl>();
    auto enqueuer = std::make_shared<SubTaskEnqueuerImpl>(taskQueue);

    auto processingService = std::make_shared<ProcessingServiceImpl>(
        pubs1,
        1,
        enqueuer,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        processingCore);


    sgns::ipfs_pubsub::GossipPubSubTopic gridChannel1(pubs1, "GRID_CHANNEL_ID");
    sgns::ipfs_pubsub::GossipPubSubTopic gridChannel2(pubs2, "GRID_CHANNEL_ID");
    gridChannel1.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});
    gridChannel2.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    processingService->StartProcessing("GRID_CHANNEL_ID");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    SGProcessing::GridChannelMessage gridMessage;
    auto channelResponse = gridMessage.mutable_processing_channel_response();
    channelResponse->set_channel_id("PROCESSING_QUEUE_ID");
    gridChannel2.Publish(gridMessage.SerializeAsString());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    EXPECT_EQ(processingService->GetProcessingNodesCount(), 1);
}

/**
 * @given Empty queue list
 * @when No queue channel received
 * @then No new processing node is created
 */
// The test disabled due to processing room handling removed
// No room capacity is checked
TEST_F(ProcessingServiceTest, NoProcessingSlotsAvailable)
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue = std::make_shared<ProcessingTaskQueueImpl>();
    auto enqueuer = std::make_shared<SubTaskEnqueuerImpl>(taskQueue);

    auto processingService = std::make_shared<ProcessingServiceImpl>(
        pubs1,
        1,
        enqueuer,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        processingCore);


    sgns::ipfs_pubsub::GossipPubSubTopic gridChannel1(pubs1, "GRID_CHANNEL_ID");
    sgns::ipfs_pubsub::GossipPubSubTopic gridChannel2(pubs2, "GRID_CHANNEL_ID");
    gridChannel1.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});
    gridChannel2.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    processingService->StartProcessing("GRID_CHANNEL_ID");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // No queue channel message sent

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    EXPECT_EQ(processingService->GetProcessingNodesCount(), 0);
}
