#include "processing/processing_subtask_queue_manager.hpp"

// @todo Move to separate test suite
#include "processing/processing_validation_core.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>
#include <boost/functional/hash.hpp>

#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

#include "processing/processing_core.hpp"
#include "processing/processing_engine.hpp"
#include "processing/processing_subtask_queue_accessor_impl.hpp"
#include "processing/processing_subtask_queue_channel_pubsub.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns::processing;
using namespace sgns::test;
using namespace sgns::base;

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

        size_t GetActiveNodesCount() const override
        {
            return 2;
        }

        std::vector<libp2p::peer::PeerId> GetActiveNodes() const override
        {
            const char* node1String = "Node_1";
            const char* node2String = "Node_2";
            gsl::span<const uint8_t> node1(
                reinterpret_cast<const uint8_t*>(node1String),
                std::strlen(node1String)
            );
            gsl::span<const uint8_t> node2(
                reinterpret_cast<const uint8_t*>(node2String),
                std::strlen(node2String)
            );

            static libp2p::peer::PeerId peer1 = libp2p::peer::PeerId::fromBytes(node1).value();
            static libp2p::peer::PeerId peer2 = libp2p::peer::PeerId::fromBytes(node2).value();
            return { peer1, peer2 };
        }
    };

    class SubTaskStateStorageMock: public SubTaskStateStorage
    {
    public:
        void ChangeSubTaskState(const std::string& subTaskId, SGProcessing::SubTaskState::Type state) override {}
        std::optional<SGProcessing::SubTaskState> GetSubTaskState(const std::string& subTaskId) override
        {
            return std::nullopt;
        }
    };

    class SubTaskResultStorageMock : public SubTaskResultStorage
    {
    public:
        void AddSubTaskResult(const SGProcessing::SubTaskResult& subTaskResult) override {
            auto [_, success] = results.insert({subTaskResult.subtaskid(), subTaskResult});
            if (!success)
            {
                Color::PrintError("SubTaskResult ", subTaskResult.subtaskid(), " already added", " on node ", subTaskResult.node_address());
            }
            else
            {
                Color::PrintInfo("AddSubTaskResult ", subTaskResult.subtaskid(), " ", subTaskResult.node_address());
            }
        }

        void RemoveSubTaskResult(const std::string& subTaskId) override {
            results.erase(subTaskId);
        }

        std::vector<SGProcessing::SubTaskResult> GetSubTaskResults(const std::set<std::string>& subTaskIds) override {
            std::vector<SGProcessing::SubTaskResult> ret;
            for (const auto& id : subTaskIds) {
                if (results.count(id)) ret.push_back(results[id]);
        }
            return ret;
        }
    private:
        std::map<std::string, SGProcessing::SubTaskResult> results;
    };


 class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(size_t processingMillisec)
            : m_processingMillisec(processingMillisec)
        {
        }
        bool SetProcessingTypeFromJson(std::string jsondata) override
        {
            return true; //TODO - This is wrong - Update this tests to the actual ProcessingCoreImpl on src/processing/impl
        }
        std::shared_ptr<std::pair<std::shared_ptr<std::vector<char>>, std::shared_ptr<std::vector<char>>>>  GetCidForProc(std::string json_data, std::string base_json) override
        {
            return nullptr;
        }

        void GetSubCidForProc(std::shared_ptr<boost::asio::io_context> ioc, std::string url, std::shared_ptr<std::vector<char>> results) override
        {
            return;
        }

        libp2p::outcome::result<SGProcessing::SubTaskResult> ProcessSubTask(
        const SGProcessing::SubTask& subTask, uint32_t initialHashCode) override
        {
            SGProcessing::SubTaskResult result;
            if (m_processingMillisec > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_processingMillisec));
            }

            auto itResultHashes = m_chunkResultHashes.find(subTask.subtaskid());

            size_t subTaskResultHash = initialHashCode;
            for (int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx)
            {
                size_t chunkHash = 0;
                if (itResultHashes != m_chunkResultHashes.end())
                {
                    chunkHash = itResultHashes->second[chunkIdx];
                }
                else
                {
                    const auto& chunk = subTask.chunkstoprocess(chunkIdx);
                    // Chunk result hash should be calculated
                    // Chunk data hash is calculated just as a stub
                    chunkHash = std::hash<std::string>{}(chunk.SerializeAsString());
                }

                std::string chunkHashString(reinterpret_cast<const char*>(&chunkHash), sizeof(chunkHash));
                result.add_chunk_hashes(chunkHashString);
                boost::hash_combine(subTaskResultHash, chunkHash);
            }

            std::string hashString(reinterpret_cast<const char*>(&subTaskResultHash), sizeof(subTaskResultHash));
            result.set_result_hash(hashString);

            auto [_, success] = m_processedSubTasks.insert(subTask.subtaskid());
            if (!success) {
                Color::PrintError("SubTask ", subTask.subtaskid(), " already processed");
            }
            m_initialHashes.push_back(initialHashCode);
            return result;
        };

        std::set<std::string> m_processedSubTasks;
        std::vector<uint32_t> m_initialHashes;

        std::map<std::string, std::vector<size_t>> m_chunkResultHashes;
    private:
        size_t m_processingMillisec;
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
#ifdef SGNS_DEBUGLOGS
        libp2p::log::setLevelOfGroup("processing_engine_test", soralog::Level::DEBUG);

        auto loggerProcQM  = sgns::base::createLogger( "ProcessingSubTaskQueueManager" );
        loggerProcQM->set_level( spdlog::level::debug );

        loggerProcQM  = sgns::base::createLogger( "ProcessingSubTaskQueue");
        loggerProcQM->set_level( spdlog::level::debug );

        loggerProcQM  = sgns::base::createLogger( "ProcessingSubTaskQueueAccessorImpl");
        loggerProcQM->set_level( spdlog::level::debug );
#else
       libp2p::log::setLevelOfGroup("processing_engine_test", soralog::Level::OFF);
#endif
    }
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
    ProcessingSubTaskQueueManager queueManager(queueChannel, context, nodeId,[](const std::string &){});

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
TEST_F(ProcessingSubTaskQueueManagerTest, QueueOwnershipTransfer)
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
    ProcessingSubTaskQueueManager queueManager(queueChannel, context, nodeId1,[](const std::string &){});

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
TEST_F(ProcessingSubTaskQueueManagerTest, GrabSubTaskWithoutOwnershipTransferring)
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
    ProcessingSubTaskQueueManager queueManager(queueChannel, context, nodeId,[](const std::string &){});

    queueManager.CreateQueue(subTasks);

    queueManager.GrabSubTask([](boost::optional<const SGProcessing::SubTask&> subtask) {
        if (subtask)
        {
            // process subtask
        }
        return true;
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
TEST_F(ProcessingSubTaskQueueManagerTest, GrabSubTaskWithOwnershipTransferring)
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

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context, nodeId1,[](const std::string &){});

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
    ProcessingSubTaskQueueManager queueManager2(queueChannel2, context, nodeId2,[](const std::string &){});

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
    auto nodeId1 = "NODE1_ID";

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context, nodeId1,[](const std::string &){});

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
    auto nodeId1 = "NODE1_ID";

    ProcessingSubTaskQueueManager queueManager1(queueChannel1, context, nodeId1,[](const std::string &){});

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
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    auto start1Future = pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    auto start2Future = pubs2->Start(40002, { pubs1->GetLocalAddress() });

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION(
        ([&start1Future, &start2Future]() {
            start1Future.get();
            start2Future.get();
            return true;
        }),
        std::chrono::milliseconds(2000),
        "Pubsub nodes initialization failed",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for pubsub node initialization");

    // Create a result channel for test with the correct ID
    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(pubs1, "RESULT_CHANNEL_ID_test");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});

    // Both nodes process at the same speed
    auto processingCore1 = std::make_shared<ProcessingCoreImpl>( 50 );  // 50ms per subtask
    auto processingCore2 = std::make_shared<ProcessingCoreImpl>( 50 );  // 50ms per subtask

    auto nodeId1 = "NODE_1";
    auto nodeId2 = "NODE_2";

    // Create a queue with 10 subtasks
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);

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

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "QUEUE_CHANNEL_ID");
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs2, "QUEUE_CHANNEL_ID");

    // Setup queue managers
    auto processingQueueManager1 = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel1, pubs1->GetAsioContext(), nodeId1, [](const std::string &){});

    auto processingQueueManager2 = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel2, pubs2->GetAsioContext(), nodeId2, [](const std::string &){});

    queueChannel1->SetQueueRequestSink(
      [qmWeak( std::weak_ptr<ProcessingSubTaskQueueManager>( processingQueueManager1 ) )](
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

    queueChannel1->SetQueueUpdateSink(
    [qmWeak(std::weak_ptr<ProcessingSubTaskQueueManager>(processingQueueManager1))](
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

    queueChannel2->SetQueueRequestSink(
      [qmWeak( std::weak_ptr<ProcessingSubTaskQueueManager>( processingQueueManager2 ) )](
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

    queueChannel2->SetQueueUpdateSink(
    [qmWeak(std::weak_ptr<ProcessingSubTaskQueueManager>(processingQueueManager2))](
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

    auto listen_result = queueChannel1->Listen();
    ASSERT_TRUE(listen_result) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if (listen_result && std::holds_alternative<std::chrono::milliseconds>(listen_result.value())) {
        auto wait_time = std::get<std::chrono::milliseconds>(listen_result.value());
        Color::PrintInfo("Channel 1 Subscription established after ", wait_time.count(), " ms");
    }

    listen_result = queueChannel2->Listen();
    ASSERT_TRUE(listen_result) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if (listen_result && std::holds_alternative<std::chrono::milliseconds>(listen_result.value())) {
        auto wait_time = std::get<std::chrono::milliseconds>(listen_result.value());
        Color::PrintInfo("Channel 2 Subscription established after ", wait_time.count(), " ms");
    }
    // Flags to track finalization
    std::atomic<bool> isTaskFinalized1 = false;
    std::atomic<bool> isTaskFinalized2 = false;

    // Set up engines
    auto engine1 = std::make_shared<ProcessingEngine>(nodeId1, processingCore1, [](const std::string &){},[]{});
    auto engine2 = std::make_shared<ProcessingEngine>(nodeId2, processingCore2, [](const std::string &){},[]{});

    // Set up queue accessors
    auto subTaskQueueAccessor1 = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager1,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [&isTaskFinalized1](const SGProcessing::TaskResult&) {
            isTaskFinalized1 = true;
            Color::PrintInfo("Task finalized by node 1");
        },
        [](const std::string &) {});

    auto subTaskQueueAccessor2 = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs2,
        processingQueueManager2,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [&isTaskFinalized2](const SGProcessing::TaskResult&) {
            isTaskFinalized2 = true;
            Color::PrintInfo("Task finalized by node 2");
        },
        [](const std::string &) {});

    // Set up result channels
    subTaskQueueAccessor1->CreateResultsChannel("test");
    subTaskQueueAccessor2->CreateResultsChannel("test");

    subTaskQueueAccessor1->ConnectToSubTaskQueue([&]() {
    });

    subTaskQueueAccessor2->ConnectToSubTaskQueue([&]() {
    });

    // Create the queue - this will automatically publish it
    processingQueueManager1->CreateQueue(subTasks);

    // Node2 creates the same queue but isn't the owner
    processingQueueManager2->ProcessSubTaskQueueMessage(processingQueueManager1->GetQueueSnapshot().release());

    // start engine2 first, so that it will send a message for queue ownership before engine1 can process all the
    engine2->StartQueueProcessing(subTaskQueueAccessor2);

    engine1->StartQueueProcessing(subTaskQueueAccessor1);

    // Wait for all subtasks to be processed by both nodes
    ASSERT_WAIT_FOR_CONDITION(
        ([&processingCore1, &processingCore2]() {
            return processingCore1->m_processedSubTasks.size() +
                   processingCore2->m_processedSubTasks.size() >= 10;
        }),
        std::chrono::milliseconds(20000),
        "Not all subtasks were processed",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for processing all subtasks");

    // Wait for either node to finalize the task
    ASSERT_WAIT_FOR_CONDITION(
        ([&isTaskFinalized1, &isTaskFinalized2]() {
            return isTaskFinalized1.load() || isTaskFinalized2.load();
        }),
        std::chrono::milliseconds(3000),
        "Task was not finalized by any node",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for task finalization");

    // Cleanup
    pubs1->Stop();
    pubs2->Stop();

    // Verify results
    size_t totalProcessed = processingCore1->m_processedSubTasks.size() +
                            processingCore2->m_processedSubTasks.size();
    ASSERT_EQ(10, totalProcessed);
    ASSERT_EQ(5, processingCore1->m_processedSubTasks.size());
    ASSERT_EQ(5, processingCore2->m_processedSubTasks.size());


}
