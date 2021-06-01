#include "processing/processing_node.hpp"

#include <gtest/gtest.h>

#include <iostream>

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
/**
 * @given Slots in a processing room are available
 * @when A worker requests a joining the room.
 * @then The worker is successsfully joined the room.
 */
TEST(ProcessingRoomTest, AttachToProcessingRoom)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, {pubs1->GetLocalAddress()});

    auto processingCore = std::make_shared<ProcessingCoreImpl>();

    ProcessingNode node1(pubs1, 2, processingCore);
    ProcessingNode node2(pubs2, 2, processingCore);

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
TEST(ProcessingRoomTest, RoomSizeLimitExceeded)
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
        
    ProcessingNode node1(pubs1, 2, processingCore);
    ProcessingNode node2(pubs2, 2, processingCore);
    ProcessingNode node3(pubs3, 2, processingCore);

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
