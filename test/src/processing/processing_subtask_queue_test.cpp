#include "processing/processing_subtask_queue.hpp"

#include <gtest/gtest.h>

using namespace sgns::processing;

namespace
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        void SplitTask(const SGProcessing::Task& task, SubTaskList& subTasks) override
        {
            {
                auto subtask = std::make_unique<SGProcessing::SubTask>();
                subTasks.push_back(std::move(subtask));
            }

            {
                auto subtask = std::make_unique<SGProcessing::SubTask>();
                subTasks.push_back(std::move(subtask));
            }
        }

        void  ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override {}
    };
}

/**
 * @given Processing task
 * @when A subtask queue is created
 * @then The created queue is published to processing channel.
 * The queue has specified owner, no subtasks are locked by default.
 */
TEST(ProcessingSubTaskQueueTest, QueueCreating)
{
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, { pubs1->GetLocalAddress() });

    auto pubs1Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    pubs1Channel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
    {     
    });

    auto pubs2Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    pubs2Channel->Subscribe([&queueSnapshotSet](
        boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
    {
        if (message)
        {
            SGProcessing::ProcessingChannelMessage channelMessage;
            if (channelMessage.ParseFromArray(message->data.data(), static_cast<int>(message->data.size())))
            {
                if (channelMessage.has_subtask_queue())
                {
                    queueSnapshotSet.push_back(channelMessage.subtask_queue());
                }
            }

        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>();

    auto nodeId = pubs1->GetLocalAddress();
    ProcessingSubTaskQueue queue(pubs1Channel, pubs1->GetAsioContext(), nodeId, processingCore);

    SGProcessing::Task task;
    queue.CreateQueue(task);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    pubs1->Stop();

    ASSERT_EQ(1, queueSnapshotSet.size());
    EXPECT_EQ(nodeId, queueSnapshotSet[0].processing_queue().owner_node_id());
    ASSERT_EQ(2, queueSnapshotSet[0].processing_queue().items_size());
    EXPECT_EQ("", queueSnapshotSet[0].processing_queue().items(0).lock_node_id());
    EXPECT_EQ("", queueSnapshotSet[0].processing_queue().items(1).lock_node_id());
}

/**
 * @given Subtask queue
 * @when Queue owner is changed
 * @then The queue with updated owner is published.
 */
TEST(ProcessingSubTaskQueueTest, QueueOwnershipTransfer)
{
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, { pubs1->GetLocalAddress() });

    auto pubs1Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    pubs1Channel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
    {
    });

    auto pubs2Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    pubs2Channel->Subscribe([&queueSnapshotSet](
        boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
        {
            if (message)
            {
                SGProcessing::ProcessingChannelMessage channelMessage;
                if (channelMessage.ParseFromArray(message->data.data(), static_cast<int>(message->data.size())))
                {
                    if (channelMessage.has_subtask_queue())
                    {
                        queueSnapshotSet.push_back(channelMessage.subtask_queue());
                    }
                }

            }
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>();

    auto nodeId1 = pubs1->GetLocalAddress();
    ProcessingSubTaskQueue queue(pubs1Channel, pubs1->GetAsioContext(), nodeId1, processingCore);

    SGProcessing::Task task;
    queue.CreateQueue(task);

    auto nodeId2 = "NEW_QUEUE_OWNER";
    queue.MoveOwnershipTo(nodeId2);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    pubs1->Stop();

    ASSERT_EQ(2, queueSnapshotSet.size());
    EXPECT_EQ(nodeId2, queueSnapshotSet[1].processing_queue().owner_node_id());
    ASSERT_EQ(2, queueSnapshotSet[0].processing_queue().items_size());
    // The subtask is not locked by the new owner yet
    EXPECT_EQ("", queueSnapshotSet[0].processing_queue().items(0).lock_node_id());
    EXPECT_EQ("", queueSnapshotSet[0].processing_queue().items(1).lock_node_id());
}

/**
 * @given Local node owns the subtask queue
 * @when New subtask is being grabbed
 * @then Queue snapshot is published that contains a lock on the grabbed subtask.
 */
TEST(ProcessingSubTaskQueueTest, GrabSubTaskWithoutOwnershipTransferring)
{
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, { pubs1->GetLocalAddress() });

    auto pubs1Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    pubs1Channel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
        {
        });

    auto pubs2Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    pubs2Channel->Subscribe([&queueSnapshotSet](
        boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
        {
            if (message)
            {
                SGProcessing::ProcessingChannelMessage channelMessage;
                if (channelMessage.ParseFromArray(message->data.data(), static_cast<int>(message->data.size())))
                {
                    if (channelMessage.has_subtask_queue())
                    {
                        queueSnapshotSet.push_back(channelMessage.subtask_queue());
                    }
                }

            }
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>();

    auto nodeId = pubs1->GetLocalAddress();
    ProcessingSubTaskQueue queue(pubs1Channel, pubs1->GetAsioContext(), nodeId, processingCore);

    SGProcessing::Task task;
    queue.CreateQueue(task);

    queue.GrabSubTask([](boost::optional<const SGProcessing::SubTask&> subtask) {
        if (subtask)
        {
            // process subtask
        }
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    pubs1->Stop();

    ASSERT_EQ(2, queueSnapshotSet.size());
    EXPECT_EQ(nodeId, queueSnapshotSet[1].processing_queue().owner_node_id());
    ASSERT_EQ(2, queueSnapshotSet[1].processing_queue().items_size());

    // The subtask is locked the queue owner
    EXPECT_EQ(nodeId, queueSnapshotSet[1].processing_queue().items(0).lock_node_id());
    EXPECT_EQ("", queueSnapshotSet[1].processing_queue().items(1).lock_node_id());
}

/**
 * @given Local node doesn't own the subtask queue
 * @when New subtask is being grabbed
 * @then Queue snapshot is published that contains a lock on the grabbed subtask.
 * Ownershop is moved to the local node.
 */
TEST(ProcessingSubTaskQueueTest, GrabSubTaskWithOwnershipTransferring)
{
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet1;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet2;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, { pubs1->GetLocalAddress() });

    auto processingCore = std::make_shared<ProcessingCoreImpl>();

    auto pubs1Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    auto nodeId1 = pubs1->GetLocalAddress();

    ProcessingSubTaskQueue queue1(pubs1Channel, pubs1->GetAsioContext(), nodeId1, processingCore);
    pubs1Channel->Subscribe([&queue1, &queueSnapshotSet1](
        boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {
            if (message)
            {
                SGProcessing::ProcessingChannelMessage channelMessage;
                if (channelMessage.ParseFromArray(message->data.data(), static_cast<int>(message->data.size())))
                {
                    if (channelMessage.has_subtask_queue())
                    {
                        queueSnapshotSet1.push_back(channelMessage.subtask_queue());
                        queue1.ProcessSubTaskQueueMessage(channelMessage.release_subtask_queue());
                    }
                    else if (channelMessage.has_subtask_queue_request())
                    {
                        queue1.ProcessSubTaskQueueRequestMessage(channelMessage.subtask_queue_request());
                    }
                }
            }
        });

    auto pubs2Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs2, "PROCESSING_CHANNEL_ID");
    auto nodeId2 = pubs2->GetLocalAddress();

    ProcessingSubTaskQueue queue2(pubs2Channel, pubs2->GetAsioContext(), nodeId2, processingCore);
    pubs2Channel->Subscribe([&queueSnapshotSet2, &queue2](
        boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {
            if (message)
            {
                SGProcessing::ProcessingChannelMessage channelMessage;
                if (channelMessage.ParseFromArray(message->data.data(), static_cast<int>(message->data.size())))
                {
                    if (channelMessage.has_subtask_queue())
                    {
                        queueSnapshotSet2.push_back(channelMessage.subtask_queue());
                        queue2.ProcessSubTaskQueueMessage(channelMessage.release_subtask_queue());
                    }
                    else if (channelMessage.has_subtask_queue_request())
                    {
                        queue2.ProcessSubTaskQueueRequestMessage(channelMessage.subtask_queue_request());
                    }

                }
            }
        });

    // Wait for subscription
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Create the queue on node1
    SGProcessing::Task task;
    queue1.CreateQueue(task);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    // Grab subtask on Node2
    queue2.GrabSubTask([](boost::optional<const SGProcessing::SubTask&> subtask) {
        if (subtask)
        {
            // process subtask
        }
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    pubs1->Stop();
    pubs2->Stop();

    ASSERT_EQ(3, queueSnapshotSet2.size());

    // Ownership is transferred to node2
    ASSERT_EQ(2, queueSnapshotSet2[2].processing_queue().items_size());
    EXPECT_EQ(nodeId2, queueSnapshotSet2[2].processing_queue().owner_node_id());

    // The subtask is locked by node2
    EXPECT_EQ(nodeId2, queueSnapshotSet2[2].processing_queue().items(0).lock_node_id());
    EXPECT_EQ("", queueSnapshotSet2[2].processing_queue().items(1).lock_node_id());
}


