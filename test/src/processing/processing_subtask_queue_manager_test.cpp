
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
        ProcessingServiceTest::SetUp("processing_subtask_queue_manager_test", logger_config);
        ProcessingServiceTest::Initialize(2, 50);
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

    ProcessingSubTaskQueueManager queueManager2(queueChannel2, context, nodeId2,[](const std::string &){});

    queueSubTaskChannel->queueOwnershipRequestSink =
        [context, &requestedOwnerIds, &queueManager2](const std::string& nodeId) {
            requestedOwnerIds.push_back(nodeId);
            context->post([&queueManager2, nodeId]() {
                SGProcessing::SubTaskQueueRequest request;
                request.set_node_id(nodeId);
                queueManager2.ProcessSubTaskQueueRequestMessage(request);
                });
        };
    queueSubTaskChannel->queuePublishingSink =
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
        return true;
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
}

/**
 * @given A queue containing 10 subtasks and two nodes
 * @when Both nodes process subtasks with one node processing the final subtask
 * @then The node that processes the final subtask finalizes the job
 */
TEST_F(ProcessingSubTaskQueueManagerTest, TwoNodesProcessingAndFinalizing)
{
    // Create a result channel for test with the correct ID
    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(m_pubsub_nodes[0], "RESULT_CHANNEL_ID_test");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});

    // Create a queue with 10 subtasks
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id("NODE_1");

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

    // Create a list of subtasks
    std::list<SGProcessing::SubTask> subTasks;

    // Add 10 subtasks to the list
    for (int i = 1; i <= 10; i++) {
        SGProcessing::SubTask subTask;
        subTask.set_subtaskid("SUBTASK_ID" + std::to_string(i));

        SGProcessing::ProcessingChunk chunk;
        chunk.set_chunkid("CHUNK_1");
        chunk.set_n_subchunks(1);

        auto chunkToProcess = subTask.add_chunkstoprocess();
        chunkToProcess->CopyFrom(chunk);

        subTasks.push_back(std::move(subTask));
    }

    m_processing_queues_channel_pub_subs[0]->SetQueueRequestSink(
      [qmWeak( std::weak_ptr<ProcessingSubTaskQueueManager>( m_processing_queues_managers[0] ) )](
          const SGProcessing::SubTaskQueueRequest &request )
      {
          auto qm = qmWeak.lock();
          if ( qm )
          {
              qm->ProcessSubTaskQueueRequestMessage( request );
              return true;
          }
          return false;
      } );

    m_processing_queues_channel_pub_subs[0]->SetQueueUpdateSink(
    [qmWeak(std::weak_ptr<ProcessingSubTaskQueueManager>(m_processing_queues_managers[0]))](
        SGProcessing::SubTaskQueue *queue)
    {
        auto qm = qmWeak.lock();
        if (qm)
        {
            qm->ProcessSubTaskQueueMessage(queue);
            return true;
        }
        return false;
    });

    m_processing_queues_channel_pub_subs[1]->SetQueueRequestSink(
      [qmWeak( std::weak_ptr<ProcessingSubTaskQueueManager>( m_processing_queues_managers[1] ) )](
          const SGProcessing::SubTaskQueueRequest &request )
      {
          auto qm = qmWeak.lock();
          if ( qm )
          {
              qm->ProcessSubTaskQueueRequestMessage( request );
              return true;
          }
          return false;
      } );

    m_processing_queues_channel_pub_subs[1]->SetQueueUpdateSink(
    [qmWeak(std::weak_ptr<ProcessingSubTaskQueueManager>(m_processing_queues_managers[1]))](
        SGProcessing::SubTaskQueue *queue)
    {
        auto qm = qmWeak.lock();
        if (qm)
        {
            qm->ProcessSubTaskQueueMessage(queue);
            return true;
        }
        return false;
    });

    auto listen_result =  m_processing_queues_channel_pub_subs[0]->Listen();
    ASSERT_TRUE(listen_result) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if (listen_result && std::holds_alternative<std::chrono::milliseconds>(listen_result.value())) {
        auto wait_time = std::get<std::chrono::milliseconds>(listen_result.value());
        Color::PrintInfo("Channel 1 Subscription established after ", wait_time.count(), " ms");
    }

    listen_result =  m_processing_queues_channel_pub_subs[1]->Listen();
    ASSERT_TRUE(listen_result) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if (listen_result && std::holds_alternative<std::chrono::milliseconds>(listen_result.value())) {
        auto wait_time = std::get<std::chrono::milliseconds>(listen_result.value());
        Color::PrintInfo("Channel 2 Subscription established after ", wait_time.count(), " ms");
    }

    m_processing_queues_accessors[0]->ConnectToSubTaskQueue([&]() {
    });

    m_processing_queues_accessors[1]->ConnectToSubTaskQueue([&]() {
    });

    // Create the queue - this will automatically publish it
    m_processing_queues_managers[0]->CreateQueue(subTasks);

    // Node2 creates the same queue but isn't the owner
    m_processing_queues_managers[1]->ProcessSubTaskQueueMessage(m_processing_queues_managers[0]->GetQueueSnapshot().release());

    // start engine2 first, so that it will send a message for queue ownership before engine1 can process all the
    m_processing_engines[1]->StartQueueProcessing(m_processing_queues_accessors[1]);

    m_processing_engines[0]->StartQueueProcessing(m_processing_queues_accessors[0]);

    std::chrono::milliseconds resultTime;
    // Wait for all subtasks to be processed by both nodes
    ASSERT_WAIT_FOR_CONDITION(
        ([this]() {
            return m_processing_cores[0]->m_processedSubTasks.size() +
                   m_processing_cores[1]->m_processedSubTasks.size() >= 10;
        }),
        std::chrono::milliseconds(20000),
        "Not all subtasks were processed",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for processing all subtasks");

    // Wait for either node to finalize the task
    ASSERT_WAIT_FOR_CONDITION(
        ([this]() {
            return m_IsTaskFinalized[0]->load() || m_IsTaskFinalized[1]->load();
        }),
        std::chrono::milliseconds(3000),
        "Task was not finalized by any node",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for task finalization");

    // Verify results
    size_t totalProcessed = m_processing_cores[0]->m_processedSubTasks.size() +
                            m_processing_cores[1]->m_processedSubTasks.size();
    ASSERT_EQ(10, totalProcessed);
    ASSERT_EQ(5, m_processing_cores[0]->m_processedSubTasks.size());
    ASSERT_EQ(5, m_processing_cores[1]->m_processedSubTasks.size());

}
