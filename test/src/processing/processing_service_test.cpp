
#include "processing/processing_service.hpp"

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

class ProcessingTaksQueueImpl : public ProcessingTaksQueue
{
public:
    bool PopTask(SGProcessing::Task& task) override
    {
        return false;
    };
};

/**
 * @given Empty room list
 * @when A room with available slots received
 * @then A processing node is created
 */
TEST(ProcessingServiceTest, ProcessingSlotsAreAvailable)
{
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    pubs->Start(40001, {});

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue = std::make_shared<ProcessingTaksQueueImpl>();
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
TEST(ProcessingServiceTest, NoProcessingSlotsAvailable)
{
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    pubs->Start(40001, {});

    auto processingManager = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue = std::make_shared<ProcessingTaksQueueImpl>();
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
