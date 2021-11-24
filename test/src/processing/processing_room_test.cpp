#include "processing/processing_node.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>

#include <iostream>

using namespace sgns::processing;
const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_room_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

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

class ProcessingRoomTest : public ::testing::Test
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
        libp2p::log::setLevelOfGroup("processing_room_test", soralog::Level::DEBUG);
    }
};
/**
 * @given Slots in a processing room are available
 * @when A worker requests a joining the room.
 * @then The worker is successsfully joined the room.
 */
TEST_F(ProcessingRoomTest, AttachToProcessingRoom)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, {pubs1->GetLocalAddress()});

    auto processingCore = std::make_shared<ProcessingCoreImpl>();

    ProcessingNode node1(pubs1, 2, processingCore, [](const SGProcessing::TaskResult&) {});
    ProcessingNode node2(pubs2, 2, processingCore, [](const SGProcessing::TaskResult&) {});

    SGProcessing::Task task;
    task.set_ipfs_block_id("DATABLOCKID");
    node1.CreateProcessingHost(task);
    node2.AttachTo("DATABLOCKID", 500);

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    pubs2->Stop();
    pubs1->Stop();

    EXPECT_EQ(node1.GetRoom()->GetNodeIds().size(), 2);
    EXPECT_EQ(node2.GetRoom()->GetNodeIds().size(), 2);
}

/**
 * @given Slots availability is not anough to allow joining all peers
 * @when A worker requests a joining the room that exceeds room size limit
 * @then The worker is unsubscribed from processing channel
 */
TEST_F(ProcessingRoomTest, RoomSizeLimitExceeded)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, { pubs1->GetLocalAddress() });

    auto pubs3 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs3->Start(40001, { pubs1->GetLocalAddress() });

    auto processingCore = std::make_shared<ProcessingCoreImpl>();

    //auto logger = spdlog::get("GossipPubSub");
    //logger->set_level(spdlog::level::debug);
        
    ProcessingNode node1(pubs1, 2, processingCore, [](const SGProcessing::TaskResult&) {});
    ProcessingNode node2(pubs2, 2, processingCore, [](const SGProcessing::TaskResult&) {});
    ProcessingNode node3(pubs3, 2, processingCore, [](const SGProcessing::TaskResult&) {});

    SGProcessing::Task task;
    task.set_ipfs_block_id("DATABLOCKID");

    node1.CreateProcessingHost(task);
    node2.AttachTo("DATABLOCKID", 1000);
    // Wait for room {1, 2} construction
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    // 3 should be rejected due to room size limit
    node3.AttachTo("DATABLOCKID", 500);
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    pubs3->Stop();
    pubs2->Stop();
    pubs1->Stop();

    //auto ids1 = node1.GetRoom()->GetNodeIds();
    //std::cout << 1 << " " << pubs1->GetLocalAddress() << std::endl;
    //for (auto id : ids1)
    //    std::cout << id << std::endl;

    //auto ids2 = node2.GetRoom()->GetNodeIds();
    //std::cout << 2 << " " << pubs2->GetLocalAddress() << std::endl;
    //for (auto id : ids2)
    //    std::cout << id << std::endl;

    //auto ids3 = node3.GetRoom()->GetNodeIds();
    //std::cout << 3 << " " << pubs3->GetLocalAddress() << std::endl;
    //for (auto id : ids3)
    //    std::cout << id << std::endl;

    EXPECT_EQ(node1.GetRoom()->GetNodeIds().size(), 2);
    EXPECT_EQ(node2.GetRoom()->GetNodeIds().size(), 2);
    EXPECT_EQ(node3.GetRoom()->GetNodeIds().size(), 2);
    EXPECT_TRUE(node1.IsRoommate());
    EXPECT_TRUE(node2.IsRoommate());
    EXPECT_FALSE(node3.IsRoommate());

    EXPECT_TRUE(node1.GetRoom()->GetNodeIds() == node2.GetRoom()->GetNodeIds());
    EXPECT_TRUE(node1.GetRoom()->GetNodeIds() == node3.GetRoom()->GetNodeIds());
}
