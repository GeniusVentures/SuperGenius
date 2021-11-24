
#include "processing/processing_service.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>

using namespace sgns::processing;

namespace
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        void SplitTask(const SGProcessing::Task& task, SubTaskList& subTasks) override {}

        void  ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override {};
    };
}

class ProcessingTaskQueueImpl : public ProcessingTaskQueue
{
public:
    bool GrabTask(std::string& taskKey, SGProcessing::Task& task) override
    {
        return false;
    }

    bool CompleteTask(const std::string& taskKey, const SGProcessing::TaskResult& task) override
    {
        return false;
    }
};

const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_service_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

class ProcessingServiceTest : public ::testing::Test
{
public:
    virtual void SetUp() override
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
        libp2p::log::setLevelOfGroup("processing_service_test", soralog::Level::DEBUG);
    }
};
/**
 * @given Empty room list
 * @when A room with available slots received
 * @then A processing node is created
 */
TEST_F(ProcessingServiceTest, ProcessingSlotsAreAvailable)
{
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    pubs->Start(40001, {});

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue = std::make_shared<ProcessingTaskQueueImpl>();
    ProcessingServiceImpl processingService(pubs, 1, 1, taskQueue, processingCore);


    sgns::ipfs_pubsub::GossipPubSubTopic gridChannel(pubs, "GRID_CHANNEL_ID");
    gridChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    processingService.Listen("GRID_CHANNEL_ID");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    SGProcessing::GridChannelMessage gridMessage;
    auto channelResponse = gridMessage.mutable_processing_channel_response();
    channelResponse->set_channel_id("PROCESSING_CHANNEL_ID");
    channelResponse->set_channel_capacity(1);
    channelResponse->set_channel_nodes_joined(0);
    gridChannel.Publish(gridMessage.SerializeAsString());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    pubs->Stop();

    EXPECT_EQ(processingService.GetProcessingNodesCount(), 1);
}

/**
 * @given Empty room list
 * @when A room without available slots is received
 * @then No new processing node is created
 */
TEST_F(ProcessingServiceTest, NoProcessingSlotsAvailable)
{
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    pubs->Start(40001, {});

    auto processingManager = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue = std::make_shared<ProcessingTaskQueueImpl>();
    ProcessingServiceImpl processingService(pubs, 1, 1, taskQueue, processingManager);


    sgns::ipfs_pubsub::GossipPubSubTopic gridChannel(pubs, "GRID_CHANNEL_ID");
    gridChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    processingService.Listen("GRID_CHANNEL_ID");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    SGProcessing::GridChannelMessage gridMessage;
    auto channelResponse = gridMessage.mutable_processing_channel_response();
    channelResponse->set_channel_id("PROCESSING_CHANNEL_ID");
    channelResponse->set_channel_capacity(1);
    channelResponse->set_channel_nodes_joined(1);
    gridChannel.Publish(gridMessage.SerializeAsString());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    pubs->Stop();

    EXPECT_EQ(processingService.GetProcessingNodesCount(), 0);
}
