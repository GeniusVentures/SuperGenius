
#include "processing_mock.hpp"
#include "processing_service_test.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>
#include <boost/functional/hash.hpp>

#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

#include "testutil/wait_condition.hpp"

using namespace sgns::processing;
using namespace sgns::test;
using namespace sgns::base;

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

class ProcessingSubTaskQueueManagerTest : public ProcessingServiceTest
{
public:
    void SetUp() override
    {
        //ProcessingServiceTest::SetUp("processing_subtask_queue_manager_test", logger_config);
        //ProcessingServiceTest::Initialize(2, 50);
    }
    const  std::string nodeId1 = "NODE_1";
    const  std::string nodeId2 = "NODE_2";
};

/**
 * @given Processing task
 * @when A subtask queue is created
 * @then The created queue is published to processing channel.
 * The queue has specified owner, no subtasks are locked by default.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, QueueCreating)
{
    auto context = std::make_shared<boost::asio::io_context>();

    std::vector<std::string> requestedOwnerIds;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto queueSubTaskChannel = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    queueSubTaskChannel->queueOwnershipRequestSink = [&requestedOwnerIds](const std::string& nodeId) {
        requestedOwnerIds.push_back(nodeId);
    };
    queueSubTaskChannel->queuePublishingSink = [&queueSnapshotSet](std::shared_ptr<SGProcessing::SubTaskQueue> queue) {
            queueSnapshotSet.push_back(*queue);
    };

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

    std::list<SGProcessing::SubTask> subTasks;
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

    // this uses mocks, so all good recreating this.
    ProcessingSubTaskQueueManager queueManager(queueSubTaskChannel, context, nodeId1,[](const std::string &){});
    queueManager.CreateQueue(subTasks);

    ASSERT_EQ(0, requestedOwnerIds.size());
    ASSERT_EQ(1, queueSnapshotSet.size());
    EXPECT_EQ(nodeId1, queueSnapshotSet[0].processing_queue().owner_node_id());
    ASSERT_EQ(2, queueSnapshotSet[0].processing_queue().items_size());
    EXPECT_EQ("", queueSnapshotSet[0].processing_queue().items(0).lock_node_id());
    EXPECT_EQ("", queueSnapshotSet[0].processing_queue().items(1).lock_node_id());
    context->stop();
}

/**
 * @given Subtask queue
 * @when Queue owner is changed
 * @then The queue with updated owner is published.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, QueueOwnershipTransfer)
{
    auto context = std::make_shared<boost::asio::io_context>();
    std::vector<std::string> requestedOwnerIds;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto queueSubTaskChannel = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    queueSubTaskChannel->queueOwnershipRequestSink = [&requestedOwnerIds](const std::string& nodeId) {
        requestedOwnerIds.push_back(nodeId);
    };
    queueSubTaskChannel->queuePublishingSink = [&queueSnapshotSet](std::shared_ptr<SGProcessing::SubTaskQueue> queue) {
        queueSnapshotSet.push_back(*queue);
    };

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

    std::list<SGProcessing::SubTask> subTasks;
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


    ProcessingSubTaskQueueManager queueManager(queueSubTaskChannel, context, nodeId1,[](const std::string &){});

    queueManager.CreateQueue(subTasks);
    queueManager.MoveOwnershipTo(nodeId2);

    ASSERT_EQ(2, queueSnapshotSet.size());
    EXPECT_EQ(nodeId2, queueSnapshotSet[1].processing_queue().owner_node_id());
    ASSERT_EQ(2, queueSnapshotSet[0].processing_queue().items_size());
    // The subtask is not locked by the new owner yet
    EXPECT_EQ("", queueSnapshotSet[0].processing_queue().items(0).lock_node_id());
    EXPECT_EQ("", queueSnapshotSet[0].processing_queue().items(1).lock_node_id());
    context->stop();
}

/**
 * @given Local node owns the subtask queue
 * @when New subtask is being grabbed
 * @then Queue snapshot is published that contains a lock on the grabbed subtask.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, GrabSubTaskWithoutOwnershipTransferring)
{
    auto context = std::make_shared<boost::asio::io_context>();
    std::vector<std::string> requestedOwnerIds;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto queueSubTaskChannel = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    queueSubTaskChannel->queueOwnershipRequestSink = [&requestedOwnerIds](const std::string& nodeId) {
        requestedOwnerIds.push_back(nodeId);
    };
    queueSubTaskChannel->queuePublishingSink = [&queueSnapshotSet](std::shared_ptr<SGProcessing::SubTaskQueue> queue) {
        queueSnapshotSet.push_back(*queue);
    };

    // A single chunk is added to 2 subtasks
    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

    std::list<SGProcessing::SubTask> subTasks;
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

    ProcessingSubTaskQueueManager queueManager(queueSubTaskChannel, context, nodeId1,[](const std::string &){});

    queueManager.CreateQueue(subTasks);

    queueManager.SetMaxSubtasksPerOwnership(2);

    queueManager.GrabSubTask([](boost::optional<const SGProcessing::SubTask&> subtask) {
        if (subtask)
        {
            // process subtask
        }
        return true;
        });

    ASSERT_EQ(2, queueSnapshotSet.size());
    EXPECT_EQ(nodeId1, queueSnapshotSet[1].processing_queue().owner_node_id());
    ASSERT_EQ(2, queueSnapshotSet[1].processing_queue().items_size());

    // The subtask is locked the queue owner
    EXPECT_EQ(nodeId1, queueSnapshotSet[1].processing_queue().items(0).lock_node_id());
    EXPECT_EQ("", queueSnapshotSet[1].processing_queue().items(1).lock_node_id());
    context->stop();
}

/**
 * @given Local node doesn't own the subtask queue
 * @when New subtask is being grabbed
 * @then Queue snapshot is published that contains a lock on the grabbed subtask.
 * Ownership is moved to the local node.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, GrabSubTaskWithOwnershipTransferring)
{
    auto context = std::make_shared<boost::asio::io_context>();
    auto queueSubTaskChannel = std::make_shared<ProcessingSubTaskQueueChannelImpl>();

    // A single chunk is added to 2 subtasks
    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

    std::list<SGProcessing::SubTask> subTasks;
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

    ProcessingSubTaskQueueManager queueManager1(queueSubTaskChannel, context, nodeId1,[](const std::string &){});

    std::vector<std::string> requestedOwnerIds;
    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelImpl>();
    queueChannel2->queueOwnershipRequestSink = 
        [context, &requestedOwnerIds, &queueManager1](const std::string& nodeId) {
        std::cout << "Queue Ownership Sink" << std::endl;
            requestedOwnerIds.push_back(nodeId);
            context->post([&queueManager1, nodeId]() {
                SGProcessing::SubTaskQueueRequest request;
                request.set_node_id(nodeId);
                queueManager1.ProcessSubTaskQueueRequestMessage(request);
            });
        };
    queueChannel2->queuePublishingSink =
        [context, &queueSnapshotSet, &queueManager1]( std::shared_ptr<SGProcessing::SubTaskQueue> queue )
    {
        // Only add to snapshot set if this node (NODE_2) is the owner
        if ( queue->processing_queue().owner_node_id() == "NODE_2" )
        {
            std::cout << "Node 2 publishing snapshot " << ( queueSnapshotSet.size() + 1 )
                      << ", owner: " << queue->processing_queue().owner_node_id() << std::endl;
            queueSnapshotSet.push_back( *queue );
        }
        // Always forward to other node for synchronization
        context->post(
            [&queueManager1, queue]()
            {
                auto pQueue = std::make_unique<SGProcessing::SubTaskQueue>();
                pQueue->CopyFrom( *queue );
                queueManager1.ProcessSubTaskQueueMessage( pQueue.release() );
            } );
    };

    ProcessingSubTaskQueueManager queueManager2(queueChannel2, context, nodeId2,[](const std::string &){});

    queueSubTaskChannel->queueOwnershipRequestSink =
        [context, &requestedOwnerIds, &queueManager2](const std::string& nodeId) {
            std::cout << "Queue Ownership Sink 2" << std::endl;
            requestedOwnerIds.push_back(nodeId);
            context->post([&queueManager2, nodeId]() {
                SGProcessing::SubTaskQueueRequest request;
                request.set_node_id(nodeId);
                queueManager2.ProcessSubTaskQueueRequestMessage(request);
                });
        };
    queueSubTaskChannel->queuePublishingSink =
        [context, &queueSnapshotSet, &queueManager2]( std::shared_ptr<SGProcessing::SubTaskQueue> queue )
    {
        // Only add to snapshot set if this node (NODE_1) is the owner
        if ( queue->processing_queue().owner_node_id() == "NODE_1" )
        {
            std::cout << "Node 1 publishing snapshot " << ( queueSnapshotSet.size() + 1 )
                      << ", owner: " << queue->processing_queue().owner_node_id() << std::endl;
            queueSnapshotSet.push_back( *queue );
        }
        // Always forward to other node for synchronization
        context->post(
            [&queueManager2, queue]()
            {
                auto pQueue = std::make_unique<SGProcessing::SubTaskQueue>();
                pQueue->CopyFrom( *queue );
                queueManager2.ProcessSubTaskQueueMessage( pQueue.release() );
            } );
    };

    // Create the queue on node1
    queueManager1.CreateQueue(subTasks);

    // Grab subtask on Node2
    queueManager2.GrabSubTask([](boost::optional<const SGProcessing::SubTask&> subtask) {
        if (subtask)
        {
            // process subtask
        }
        return true;
        });

    context->run();

    ASSERT_EQ(4, queueSnapshotSet.size());

    // Ownership is transferred to node2
    ASSERT_EQ(2, queueSnapshotSet[2].processing_queue().items_size());
    EXPECT_EQ(nodeId2, queueSnapshotSet[2].processing_queue().owner_node_id());

    // The subtask is locked by node2
    EXPECT_EQ(nodeId2, queueSnapshotSet[2].processing_queue().items(0).lock_node_id());
    EXPECT_EQ("", queueSnapshotSet[2].processing_queue().items(1).lock_node_id());
    context->stop();
}

/**
 * @given A subtask queue
 * @when Results for all subtasks added
 * @then Queue is marked as processed.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, CheckProcessedQueue)
{
    auto context = std::make_shared<boost::asio::io_context>();

    // A single chunk is added to 2 subtasks
    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

    std::list<SGProcessing::SubTask> subTasks;
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

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context, nodeId1,[](const std::string &){});

    // Create the queue on node1
    queueManager1.CreateQueue(subTasks);

    queueManager1.ChangeSubTaskProcessingStates({ "SUBTASK_1" }, true);

    ASSERT_FALSE(queueManager1.IsProcessed());

    queueManager1.ChangeSubTaskProcessingStates({ "SUBTASK_2" }, true);

    ASSERT_TRUE(queueManager1.IsProcessed());
    context->stop();
}

/**
 * @given A subtask queue
 * @when Results for all subtasks added
 * @then Queue result hashes are valid by default.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, ValidateResults)
{
    SGProcessing::SubTaskCollection subTasks;
    // A single chunk is added to 2 subtasks
    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

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
        auto validate_res = validationCore.ValidateResults(subTasks, results, invalidSubTaskIds);
        ASSERT_TRUE(validate_res.has_error());
    }

    subTaskResult.set_subtaskid("SUBTASK_2");
    results.emplace(subTaskResult.subtaskid(), subTaskResult);

    {
        std::set<std::string> invalidSubTaskIds;
        auto validate_res = validationCore.ValidateResults(subTasks, results, invalidSubTaskIds);
        ASSERT_FALSE(validate_res.has_error());
        ASSERT_EQ(0, invalidSubTaskIds.size());
    }
}

/**
 * @given A subtask queue
 * @when A task split does not create duplicated chunks
 * @then Queue creation failed.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, TaskSplitFailed)
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

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context, nodeId1 ,[](const std::string &){});

    // Create the queue on node1
    ASSERT_FALSE(queueManager1.CreateQueue(subTasks));
    context->stop();
}

/**
 * @given A subtask queue
 * @when A task split does not create duplicated chunks
 * @then Queue creation failed.
 */
TEST_F(ProcessingSubTaskQueueManagerTest, TaskSplitSucceeded)
{
    auto context = std::make_shared<boost::asio::io_context>();

    std::list<SGProcessing::SubTask> subTasks;

    // A single chunk is added to 2 subtasks
    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);


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

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context,nodeId1 ,[](const std::string &){});

    // Create the queue on node1
    ASSERT_TRUE(queueManager1.CreateQueue(subTasks));
    context->stop();
}

