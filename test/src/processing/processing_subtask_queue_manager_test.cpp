#include "processing/processing_subtask_queue_manager.hpp"

// @todo Move to separate test suite
#include "processing/processing_validation_core.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>

using namespace sgns::processing;

namespace
{
    class ProcessingSubTaskQueueChannelImpl : public ProcessingSubTaskQueueChannel
    {
    public:
        typedef std::function<void(const std::string& nodeId)> QueueOwnershipRequestSink;
        typedef std::function<void(std::shared_ptr<SGProcessing::SubTaskQueue> queue)> QueuePublishingSink;

        void RequestQueueOwnership(const std::string& nodeId) override
        {
            if (queueOwnershipRequestSink)
            {
                queueOwnershipRequestSink(nodeId);
            }
        }

        void PublishQueue(std::shared_ptr<SGProcessing::SubTaskQueue> queue) override
        {
            if (queuePublishingSink)
            {
                queuePublishingSink(queue);
            }
        }

        QueueOwnershipRequestSink queueOwnershipRequestSink;
        QueuePublishingSink queuePublishingSink;
    };
}

const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_subtask_queue_manager_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

class ProcessingSubTaskQueueManagerTest : public ::testing::Test
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
        libp2p::log::setLevelOfGroup("processing_subtask_queue_manager_test", soralog::Level::DEBUG);
    }
};

/**
 * @given Processing task
 * @when A subtask queue is created
 * @then The created queue is published to processing channel.
 * The queue has specified owner, no subtasks are locked by default.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, DISABLED_QueueCreating)
{
    auto context = std::make_shared<boost::asio::io_context>();

    std::vector<std::string> requestedOwnerIds;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    queueChannel->queueOwnershipRequestSink = [&requestedOwnerIds](const std::string& nodeId) {
        requestedOwnerIds.push_back(nodeId);
    };
    queueChannel->queuePublishingSink = [&queueSnapshotSet](std::shared_ptr<SGProcessing::SubTaskQueue> queue) {
            queueSnapshotSet.push_back(*queue);
    };

    std::list<SGProcessing::SubTask> subTasks;
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_1");
        subTasks.push_back(std::move(subtask));
    }
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_2");
        subTasks.push_back(std::move(subtask));
    }

    std::string nodeId = "SOME_ID";
    ProcessingSubTaskQueueManager queueManager(queueChannel, context, nodeId);

    queueManager.CreateQueue(subTasks);

    ASSERT_EQ(0, requestedOwnerIds.size());
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
TEST_F(ProcessingSubTaskQueueManagerTest, DISABLED_QueueOwnershipTransfer)
{
    auto context = std::make_shared<boost::asio::io_context>();
    std::vector<std::string> requestedOwnerIds;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    queueChannel->queueOwnershipRequestSink = [&requestedOwnerIds](const std::string& nodeId) {
        requestedOwnerIds.push_back(nodeId);
    };
    queueChannel->queuePublishingSink = [&queueSnapshotSet](std::shared_ptr<SGProcessing::SubTaskQueue> queue) {
        queueSnapshotSet.push_back(*queue);
    };

    std::list<SGProcessing::SubTask> subTasks;
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_1");
        subTasks.push_back(std::move(subtask));
    }

    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_2");
        subTasks.push_back(std::move(subtask));
    }

    auto nodeId1 = "OLD_QUEUE_OWNER";
    ProcessingSubTaskQueueManager queueManager(queueChannel, context, nodeId1);

    queueManager.CreateQueue(subTasks);

    auto nodeId2 = "NEW_QUEUE_OWNER";
    queueManager.MoveOwnershipTo(nodeId2);

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
TEST_F(ProcessingSubTaskQueueManagerTest, DISABLED_GrabSubTaskWithoutOwnershipTransferring)
{
    auto context = std::make_shared<boost::asio::io_context>();
    std::vector<std::string> requestedOwnerIds;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    queueChannel->queueOwnershipRequestSink = [&requestedOwnerIds](const std::string& nodeId) {
        requestedOwnerIds.push_back(nodeId);
    };
    queueChannel->queuePublishingSink = [&queueSnapshotSet](std::shared_ptr<SGProcessing::SubTaskQueue> queue) {
        queueSnapshotSet.push_back(*queue);
    };

    std::list<SGProcessing::SubTask> subTasks;
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_1");
        subTasks.push_back(std::move(subtask));
    }

    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_2");
        subTasks.push_back(std::move(subtask));
    }

    auto nodeId = "SOME_ID";
    ProcessingSubTaskQueueManager queueManager(queueChannel, context, nodeId);

    queueManager.CreateQueue(subTasks);

    queueManager.GrabSubTask([](boost::optional<const SGProcessing::SubTask&> subtask) {
        if (subtask)
        {
            // process subtask
        }
        });

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
TEST_F(ProcessingSubTaskQueueManagerTest, DISABLED_GrabSubTaskWithOwnershipTransferring)
{
    auto context = std::make_shared<boost::asio::io_context>();
    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelImpl>();

    std::list<SGProcessing::SubTask> subTasks;
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_1");
        subTasks.push_back(std::move(subtask));
    }
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_2");
        subTasks.push_back(std::move(subtask));
    }

    auto nodeId1 = "NODE1_ID";

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context, nodeId1);

    std::vector<std::string> requestedOwnerIds;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    queueChannel2->queueOwnershipRequestSink = 
        [context, &requestedOwnerIds, &queueManager1](const std::string& nodeId) {
            requestedOwnerIds.push_back(nodeId);
            context->post([&queueManager1, nodeId]() {
                SGProcessing::SubTaskQueueRequest request;
                request.set_node_id(nodeId);
                queueManager1.ProcessSubTaskQueueRequestMessage(request);
            });
        };
    queueChannel2->queuePublishingSink = 
        [context, &queueSnapshotSet, &queueManager1](std::shared_ptr<SGProcessing::SubTaskQueue> queue) {
        queueSnapshotSet.push_back(*queue);
        context->post([&queueManager1, queue]() {
            auto pQueue = std::make_unique<SGProcessing::SubTaskQueue>();
            pQueue->CopyFrom(*queue);
            queueManager1.ProcessSubTaskQueueMessage(pQueue.release());
        });
    };

    auto nodeId2 = "NODE2_ID";
    ProcessingSubTaskQueueManager queueManager2(queueChannel2, context, nodeId2);

    queueChannel1->queueOwnershipRequestSink =
        [context, &requestedOwnerIds, &queueManager2](const std::string& nodeId) {
            requestedOwnerIds.push_back(nodeId);
            context->post([&queueManager2, nodeId]() {
                SGProcessing::SubTaskQueueRequest request;
                request.set_node_id(nodeId);
                queueManager2.ProcessSubTaskQueueRequestMessage(request);
                });
        };
    queueChannel1->queuePublishingSink = 
        [context, &queueSnapshotSet, &queueManager2](std::shared_ptr<SGProcessing::SubTaskQueue> queue) {
            queueSnapshotSet.push_back(*queue);
            context->post([&queueManager2, queue]() {
                auto pQueue = std::make_unique<SGProcessing::SubTaskQueue>();
                pQueue->CopyFrom(*queue);
                queueManager2.ProcessSubTaskQueueMessage(pQueue.release());
            });
        };

    // Create the queue on node1
    queueManager1.CreateQueue(subTasks);

    // Grab subtask on Node2
    queueManager2.GrabSubTask([](boost::optional<const SGProcessing::SubTask&> subtask) {
        if (subtask)
        {
            // process subtask
        }
        });

    context->run();

    ASSERT_EQ(3, queueSnapshotSet.size());

    // Ownership is transferred to node2
    ASSERT_EQ(2, queueSnapshotSet[2].processing_queue().items_size());
    EXPECT_EQ(nodeId2, queueSnapshotSet[2].processing_queue().owner_node_id());

    // The subtask is locked by node2
    EXPECT_EQ(nodeId2, queueSnapshotSet[2].processing_queue().items(0).lock_node_id());
    EXPECT_EQ("", queueSnapshotSet[2].processing_queue().items(1).lock_node_id());
}

/**
 * @given A subtask queue
 * @when Results for all subtasks added
 * @then Queue is marked as processed.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, DISABLED_CheckProcessedQueue)
{
    auto context = std::make_shared<boost::asio::io_context>();

    std::list<SGProcessing::SubTask> subTasks;
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_1");
        subTasks.push_back(std::move(subtask));
    }
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_2");
        subTasks.push_back(std::move(subtask));
    }

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    auto nodeId1 = "NODE1_ID";

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context, nodeId1);

    // Create the queue on node1
    queueManager1.CreateQueue(subTasks);

    queueManager1.ChangeSubTaskProcessingStates({ "SUBTASK_1" }, true);

    ASSERT_FALSE(queueManager1.IsProcessed());

    queueManager1.ChangeSubTaskProcessingStates({ "SUBTASK_2" }, true);

    ASSERT_TRUE(queueManager1.IsProcessed());
}

/**
 * @given A subtask queue
 * @when Results for all subtasks added
 * @then Queue result hashes are valid by default.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, DISABLED_ValidateResults)
{
    SGProcessing::SubTaskCollection subTasks;
    // A single chunk is added to 2 subtasks
    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);
    //chunk1.set_line_stride(1);
    //chunk1.set_offset(0);
    //chunk1.set_stride(1);
    //chunk1.set_subchunk_height(10);
    //chunk1.set_subchunk_width(10);

    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid("SUBTASK_1");
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }

    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid("SUBTASK_2");
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }

    std::map<std::string, SGProcessing::SubTaskResult> results;
    SGProcessing::SubTaskResult subTaskResult;
    subTaskResult.add_chunk_hashes("1");
    subTaskResult.set_subtaskid("SUBTASK_1");

    results.emplace(subTaskResult.subtaskid(), subTaskResult);

    ProcessingValidationCore validationCore;
    {
        std::set<std::string> invalidSubTaskIds;
        ASSERT_FALSE(validationCore.ValidateResults(subTasks, results, invalidSubTaskIds));
    }

    subTaskResult.set_subtaskid("SUBTASK_2");
    results.emplace(subTaskResult.subtaskid(), subTaskResult);

    {
        std::set<std::string> invalidSubTaskIds;
        ASSERT_TRUE(validationCore.ValidateResults(subTasks, results, invalidSubTaskIds));
        ASSERT_EQ(0, invalidSubTaskIds.size());
    }
}

/**
 * @given A subtask queue
 * @when A task split does not create duplicated chunks
 * @then Queue creation failed.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, DISABLED_TaskSplitFailed)
{
    // @todo extend the test to get determite invalid result hashes
    auto context = std::make_shared<boost::asio::io_context>();

    std::list<SGProcessing::SubTask> subTasks;
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_1");
        subTasks.push_back(std::move(subtask));
    }

    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_2");
        subTasks.push_back(std::move(subtask));
    }

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    auto nodeId1 = "NODE1_ID";

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context, nodeId1);

    // Create the queue on node1
    ASSERT_FALSE(queueManager1.CreateQueue(subTasks));
}

/**
 * @given A subtask queue
 * @when A task split does not create duplicated chunks
 * @then Queue creation failed.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, DISABLED_TaskSplitSucceeded)
{
    // @todo extend the test to get determite invalid result hashes
    auto context = std::make_shared<boost::asio::io_context>();

    std::list<SGProcessing::SubTask> subTasks;

    // A single chunk is added to 2 subtasks
    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);
    //chunk1.set_line_stride(1);
    //chunk1.set_offset(0);
    //chunk1.set_stride(1);
    //chunk1.set_subchunk_height(10);
    //chunk1.set_subchunk_width(10);

    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_1");
        auto chunk = subtask.add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
        subTasks.push_back(std::move(subtask));
    }

    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid("SUBTASK_2");
        auto chunk = subtask.add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
        subTasks.push_back(std::move(subtask));
    }

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    auto nodeId1 = "NODE1_ID";

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context, nodeId1);

    // Create the queue on node1
    ASSERT_TRUE(queueManager1.CreateQueue(subTasks));
}
