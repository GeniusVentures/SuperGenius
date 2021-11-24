#include "processing/processing_subtask_queue.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>

using namespace sgns::processing;

namespace
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        void SplitTask(const SGProcessing::Task& task, SubTaskList& subTasks) override
        {
            for (const auto& subTask : m_subTasks)
            {
                auto newSubTask = std::make_unique<SGProcessing::SubTask>();
                newSubTask->CopyFrom(*subTask);
                subTasks.push_back(std::move(newSubTask));
            }
        }

        void  ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override {}

        SubTaskList m_subTasks;
    };
}

const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_subtask_queue_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

class ProcessingSubTaskQueueTest : public ::testing::Test
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
        libp2p::log::setLevelOfGroup("processing_subtask_queue_test", soralog::Level::DEBUG);
    }
};

/**
 * @given Processing task
 * @when A subtask queue is created
 * @then The created queue is published to processing channel.
 * The queue has specified owner, no subtasks are locked by default.
 */
TEST_F(ProcessingSubTaskQueueTest, QueueCreating)
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
    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_1");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }
    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_2");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

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
TEST_F(ProcessingSubTaskQueueTest, QueueOwnershipTransfer)
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
    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_1");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_2");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

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
TEST_F(ProcessingSubTaskQueueTest, GrabSubTaskWithoutOwnershipTransferring)
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
    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_1");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_2");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

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
 * Ownership is moved to the local node.
 */
TEST_F(ProcessingSubTaskQueueTest, GrabSubTaskWithOwnershipTransferring)
{
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet1;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet2;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, { pubs1->GetLocalAddress() });

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_1");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_2");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

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

/**
 * @given A subtask queue
 * @when New results added
 * @then Queue ignores results that are not relevant to subtasks located in the queue.
 */
TEST_F(ProcessingSubTaskQueueTest, AddUnexpectedResult)
{
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet1;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});


    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_1");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_2");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    auto pubs1Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    auto nodeId1 = pubs1->GetLocalAddress();

    ProcessingSubTaskQueue queue1(pubs1Channel, pubs1->GetAsioContext(), nodeId1, processingCore);

    // Create the queue on node1
    SGProcessing::Task task;
    queue1.CreateQueue(task);

    SGProcessing::SubTaskResult subTaskResult;
    ASSERT_TRUE(queue1.AddSubTaskResult("RESULT_CHANNEL_1", subTaskResult));
    ASSERT_FALSE(queue1.AddSubTaskResult("RESULT_CHANNEL_UNKNOWN", subTaskResult));

    pubs1->Stop();
}

/**
 * @given A subtask queue
 * @when Results for all subtasks added
 * @then Queue is marked as processed.
 */
TEST_F(ProcessingSubTaskQueueTest, CheckProcessedQueue)
{
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet1;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});


    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_1");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }
    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_2");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    auto pubs1Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    auto nodeId1 = pubs1->GetLocalAddress();

    ProcessingSubTaskQueue queue1(pubs1Channel, pubs1->GetAsioContext(), nodeId1, processingCore);

    // Create the queue on node1
    SGProcessing::Task task;
    queue1.CreateQueue(task);

    SGProcessing::SubTaskResult subTaskResult;
    queue1.AddSubTaskResult("RESULT_CHANNEL_1", subTaskResult);

    ASSERT_FALSE(queue1.IsProcessed());

    queue1.AddSubTaskResult("RESULT_CHANNEL_2", subTaskResult);

    ASSERT_TRUE(queue1.IsProcessed());

    pubs1->Stop();
}

/**
 * @given A subtask queue
 * @when Results for all subtasks added
 * @then Queue result hashes are valid by default.
 */
TEST_F(ProcessingSubTaskQueueTest, ValidateResults)
{
    // @todo extend the test to get determite invalid result hashes
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet1;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});


    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    // A single chunk is added to 2 subtasks
    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);
    chunk1.set_line_stride(1);
    chunk1.set_offset(0);
    chunk1.set_stride(1);
    chunk1.set_subchunk_height(10);
    chunk1.set_subchunk_width(10);

    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_1");
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_2");
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    auto pubs1Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    auto nodeId1 = pubs1->GetLocalAddress();

    ProcessingSubTaskQueue queue1(pubs1Channel, pubs1->GetAsioContext(), nodeId1, processingCore);

    // Create the queue on node1
    SGProcessing::Task task;
    queue1.CreateQueue(task);

    SGProcessing::SubTaskResult subTaskResult;
    subTaskResult.add_chunk_hashes(1);

    queue1.AddSubTaskResult("RESULT_CHANNEL_1", subTaskResult);

    ASSERT_FALSE(queue1.ValidateResults());

    queue1.AddSubTaskResult("RESULT_CHANNEL_2", subTaskResult);

    ASSERT_TRUE(queue1.ValidateResults());

    pubs1->Stop();
}

/**
 * @given A subtask queue
 * @when A task split does not create duplicated chunks
 * @then Queue creation failed.
 */
TEST_F(ProcessingSubTaskQueueTest, TaskSplitFailed)
{
    // @todo extend the test to get determite invalid result hashes
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet1;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_1");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_2");
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    auto pubs1Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    auto nodeId1 = pubs1->GetLocalAddress();

    ProcessingSubTaskQueue queue1(pubs1Channel, pubs1->GetAsioContext(), nodeId1, processingCore);

    // Create the queue on node1
    SGProcessing::Task task;
    ASSERT_FALSE(queue1.CreateQueue(task));

    pubs1->Stop();
}

/**
 * @given A subtask queue
 * @when A task split does not create duplicated chunks
 * @then Queue creation failed.
 */
TEST_F(ProcessingSubTaskQueueTest, TaskSplitSucceeded)
{
    // @todo extend the test to get determite invalid result hashes
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet1;

    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});


    auto processingCore = std::make_shared<ProcessingCoreImpl>();

    // A single chunk is added to 2 subtasks
    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);
    chunk1.set_line_stride(1);
    chunk1.set_offset(0);
    chunk1.set_stride(1);
    chunk1.set_subchunk_height(10);
    chunk1.set_subchunk_width(10);

    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_1");
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    {
        auto subtask = std::make_unique<SGProcessing::SubTask>();
        subtask->set_results_channel("RESULT_CHANNEL_2");
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
        processingCore->m_subTasks.push_back(std::move(subtask));
    }

    auto pubs1Channel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "PROCESSING_CHANNEL_ID");
    auto nodeId1 = pubs1->GetLocalAddress();

    ProcessingSubTaskQueue queue1(pubs1Channel, pubs1->GetAsioContext(), nodeId1, processingCore);

    // Create the queue on node1
    SGProcessing::Task task;
    ASSERT_TRUE(queue1.CreateQueue(task));

    pubs1->Stop();
}
