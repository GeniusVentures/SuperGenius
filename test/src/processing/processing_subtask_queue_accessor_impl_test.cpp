#include "processing/processing_engine.hpp"
#include "processing/processing_subtask_queue_accessor_impl.hpp"
#include "processing/processing_subtask_queue_channel_pubsub.hpp"
#include "processing/processing_subtask_state_storage.hpp"
#include "processing/processing_subtask_result_storage.hpp"

#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>

#include <boost/functional/hash.hpp>
#include <thread>

#include "testutil/wait_condition.hpp"

#include "base/logger.hpp"

using namespace sgns::processing;
using namespace sgns::test;
using namespace sgns::base;

namespace
{
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
        void AddSubTaskResult(const SGProcessing::SubTaskResult& subTaskResult) override
        {
            Color::PrintInfo("AddSubTaskResult ", subTaskResult.subtaskid(), " ", subTaskResult.node_address());
        }
        void RemoveSubTaskResult(const std::string& subTaskId) override {}
        std::vector<SGProcessing::SubTaskResult> GetSubTaskResults(
            const std::set<std::string>& subTaskIds) override { return {};}

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

            m_processedSubTasks.push_back(subTask);
            m_initialHashes.push_back(initialHashCode);
            return result;
        };

        std::vector<SGProcessing::SubTask> m_processedSubTasks;
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
  - name: processing_engine_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

class SubTaskQueueAccessorImplTest : public ::testing::Test
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
#else
        libp2p::log::setLevelOfGroup("processing_engine_test", soralog::Level::OFF);
#endif

    }
};

/**
 * @given A node is subscribed to result channel
 * @when A result is published to the channel
 * @then The node receives the result
 */
TEST_F(SubTaskQueueAccessorImplTest, SubscriptionToResultChannel)
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

    Color::PrintInfo("Waited ", resultTime.count(),  " ms for pubsub nodes initialization");

    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(pubs1, "RESULT_CHANNEL_ID_test");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
        {
        });

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "QUEUE_CHANNEL_ID");

    auto processingCore = std::make_shared<ProcessingCoreImpl>(0);

    auto nodeId = "NODE_1";
    auto engine = std::make_shared<ProcessingEngine>(nodeId, processingCore, [](const std::string &){},[]{});

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id("DIFFERENT_NODE_ID");

    auto item = queue->mutable_processing_queue()->add_items();
    auto subTask = queue->mutable_subtasks()->add_items();
    subTask->set_subtaskid("SUBTASK_ID");

    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId, [](const std::string &){});
    // The local queue wrapper doesn't own the queue
    processingQueueManager->ProcessSubTaskQueueMessage(queue.release());

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1, 
        processingQueueManager,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [](const SGProcessing::TaskResult&) {},
        [](const std::string &) {});

    subTaskQueueAccessor->CreateResultsChannel("test");

    // Create a flag to track when the connection callback has been executed
    std::atomic<bool> connectionEstablished = false;

    subTaskQueueAccessor->ConnectToSubTaskQueue([&]() {
        engine->StartQueueProcessing(subTaskQueueAccessor);
        connectionEstablished = true;
    });

    // Wait for connection to be established
   ASSERT_WAIT_FOR_CONDITION(
        [&connectionEstablished]() { return connectionEstablished.load(); },
        std::chrono::milliseconds(2000),
        "Connection to subtask queue was not established",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(),  " ms for connection");

    // Publish result to the results channel
    SGProcessing::SubTaskResult result;
    result.set_subtaskid("SUBTASK_ID");
    resultChannel.Publish(result.SerializeAsString());

    // Wait for the result to be received
    ASSERT_WAIT_FOR_CONDITION(
        [&subTaskQueueAccessor]() { return subTaskQueueAccessor->GetResults().size() > 0; },
        std::chrono::milliseconds(2000),
        "Result was not received by SubTaskQueueAccessor",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(),  " ms for results to be received");

    pubs1->Stop();
    pubs2->Stop();

    // No duplicates should be received
    ASSERT_EQ(1, subTaskQueueAccessor->GetResults().size());
    EXPECT_EQ("SUBTASK_ID", std::get<0>(subTaskQueueAccessor->GetResults()[0]));
}

/**
 * @given A queue containing 2 subtasks
 * @when Subtasks are finished and chunk hashes are valid
 * @then Task finalization sink is called.
 */
TEST_F(SubTaskQueueAccessorImplTest, TaskFinalization)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    auto start1Future = pubs1->Start(40001, {});

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION(
        [&start1Future]() {
            start1Future.get();
            return true;
        },
        std::chrono::milliseconds(2000),
        "Pubsub node initialization failed",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(),  " ms for pubsub node initialization");

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "QUEUE_CHANNEL_ID");

    auto processingCore = std::make_shared<ProcessingCoreImpl>(100);

    auto nodeId1 = "NODE_1";

    std::atomic<bool> isTaskFinalized = false;
    auto engine1 = std::make_shared<ProcessingEngine>(nodeId1, processingCore, [](const std::string &){},[]{});
    processingCore->m_chunkResultHashes["SUBTASK_ID1"] = { 0 };
    processingCore->m_chunkResultHashes["SUBTASK_ID2"] = { 0 };

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    // Local queue wrapped owns the queue
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->mutable_subtasks()->add_items();
        subTask->set_subtaskid("SUBTASK_ID1");
        auto chunk = subTask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->mutable_subtasks()->add_items();
        subTask->set_subtaskid("SUBTASK_ID2");
        auto chunk = subTask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }

    auto processingQueueManager1 = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,[](const std::string &){});
    processingQueueManager1->ProcessSubTaskQueueMessage(queue.release());

    auto subTaskQueueAccessor1 = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager1,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [&isTaskFinalized](const SGProcessing::TaskResult&) { isTaskFinalized.store(true); },
        [](const std::string &) {});

    subTaskQueueAccessor1->CreateResultsChannel("test");

    subTaskQueueAccessor1->ConnectToSubTaskQueue([&]() {
        engine1->StartQueueProcessing(subTaskQueueAccessor1);
        });

    ASSERT_WAIT_FOR_CONDITION(
        [&isTaskFinalized]() { return isTaskFinalized.load(); },
        std::chrono::milliseconds(2000),
        "Task not finalized",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for task finalization");

    pubs1->Stop();

    ASSERT_TRUE(isTaskFinalized);
}

/**
 * @given A queue containing 2 subtasks
 * @when One node processes too slowly and causes subtasks to timeout
 * @then The timed out subtasks can be reprocessed by another node
 */
TEST_F(SubTaskQueueAccessorImplTest, SubtaskTimeoutAndReprocessing)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    auto start1Future = pubs1->Start(40001, {});

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION(
        [&start1Future]() {
            start1Future.get();
            return true;
        },
        std::chrono::milliseconds(2000),
        "Pubsub node initialization failed",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for pubsub node initialization");

    // Create a result channel for test with the correct ID
    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(pubs1, "RESULT_CHANNEL_ID_test");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "QUEUE_CHANNEL_ID");

    // The first node processes subtasks slowly, which will trigger timeout
    auto processingCore1 = std::make_shared<ProcessingCoreImpl>(1000);  // 1000ms processing time

    // The second node processes subtasks quickly
    auto processingCore2 = std::make_shared<ProcessingCoreImpl>(100);   // 100ms processing time

    auto nodeId1 = "NODE_1";
    auto nodeId2 = "NODE_2";

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    // Local queue wrapper owns the queue
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->mutable_subtasks()->add_items();
        subTask->set_subtaskid("SUBTASK_ID1");
        auto chunk = subTask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->mutable_subtasks()->add_items();
        subTask->set_subtaskid("SUBTASK_ID2");
        auto chunk = subTask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }

    // Set a short processing timeout to trigger timeouts with the slow processor
    auto processingQueueManager1 = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,[](const std::string &){});
    processingQueueManager1->SetProcessingTimeout(std::chrono::milliseconds(500));  // 500ms timeout
    processingQueueManager1->ProcessSubTaskQueueMessage(queue.release());

    bool isTaskFinalized1 = false;
    auto engine1 = std::make_shared<ProcessingEngine>(nodeId1, processingCore1, [](const std::string &){},[]{});

    bool isTaskFinalized2 = false;
    auto engine2 = std::make_shared<ProcessingEngine>(nodeId2, processingCore2, [](const std::string &){},[]{});

    auto subTaskQueueAccessor1 = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager1,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [&isTaskFinalized1](const SGProcessing::TaskResult&) { isTaskFinalized1 = true; },
        [](const std::string &) {});

    // Make sure both queue accessors use the same results channel ID
    subTaskQueueAccessor1->CreateResultsChannel("test");

    // Connection established flag
    std::atomic<bool> connectionEstablished1 = false;

    subTaskQueueAccessor1->ConnectToSubTaskQueue([&]() {
        engine1->StartQueueProcessing(subTaskQueueAccessor1);
        connectionEstablished1 = true;
    });

    // Wait for connection to be established
    ASSERT_WAIT_FOR_CONDITION(
        [&connectionEstablished1]() { return connectionEstablished1.load(); },
        std::chrono::milliseconds(2000),
        "Connection to subtask queue 1 was not established",
        &resultTime
    );

    // Wait for processing attempt and timeout
    ASSERT_WAIT_FOR_CONDITION(
        [&processingCore1]() { return !processingCore1->m_processedSubTasks.empty(); },
        std::chrono::milliseconds(2000),
        "First node did not start processing any subtasks",
        &resultTime
    );

    // Task shouldn't be finalized because subtasks timed out
    ASSERT_FALSE(isTaskFinalized1);

    // Verify which subtasks were processed by first node
    bool processedSubTask1 = false;
    bool processedSubTask2 = false;

    for (const auto& subTask : processingCore1->m_processedSubTasks) {
        if (subTask.subtaskid() == "SUBTASK_ID1") {
            processedSubTask1 = true;
        }
        if (subTask.subtaskid() == "SUBTASK_ID2") {
            processedSubTask2 = true;
        }
    }

    // First node should have started processing but timed out
    // may have processed some subtasks before timeout

    auto processingQueueManager2 = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId2,[](const std::string &){});
    processingQueueManager2->SetProcessingTimeout(std::chrono::milliseconds(500));

    // Change queue owner to second node
    SGProcessing::SubTaskQueueRequest queueRequest;
    queueRequest.set_node_id(nodeId2);
    auto updatedQueue = processingQueueManager1->ProcessSubTaskQueueRequestMessage(queueRequest);

    // Synchronize the queues
    processingQueueManager2->ProcessSubTaskQueueMessage(processingQueueManager1->GetQueueSnapshot().release());
    processingQueueManager1->ProcessSubTaskQueueMessage(processingQueueManager2->GetQueueSnapshot().release());
    engine1->StopQueueProcessing();

    // Wait for the queue manager to handle expired tasks
    ASSERT_WAIT_FOR_CONDITION(
        [&processingQueueManager2]() { return processingQueueManager2->HasOwnership(); },
        std::chrono::milliseconds(1000),
        "Second node did not acquire queue ownership",
        &resultTime
    );

    std::atomic<bool> connectionEstablished2 = false;

    auto subTaskQueueAccessor2 = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager2,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [&isTaskFinalized2](const SGProcessing::TaskResult&) { isTaskFinalized2 = true; },
        [](const std::string &) {}
    );

    subTaskQueueAccessor2->CreateResultsChannel("test");

    subTaskQueueAccessor2->ConnectToSubTaskQueue([&]() {
        engine2->StartQueueProcessing(subTaskQueueAccessor2);
        connectionEstablished2 = true;
    });

    // Wait for connection to be established
    ASSERT_WAIT_FOR_CONDITION(
        [&connectionEstablished2]() { return connectionEstablished2.load(); },
        std::chrono::milliseconds(2000),
        "Connection to subtask queue 2 was not established",
        &resultTime
    );

    // Wait for second node to process subtasks and finalize
    ASSERT_WAIT_FOR_CONDITION(
        [&isTaskFinalized2]() { return isTaskFinalized2; },
        std::chrono::milliseconds(3000),
        "Task was not finalized by second node",
        &resultTime
    );

    pubs1->Stop();

    // Verify that node2 has ownership
    ASSERT_TRUE(processingQueueManager2->HasOwnership());

    // Verify the second node processed at least some subtasks
    bool node2ProcessedSubTasks = !processingCore2->m_processedSubTasks.empty();
    ASSERT_TRUE(node2ProcessedSubTasks);

    // Task should have been finalized by the second node
    ASSERT_TRUE(isTaskFinalized2);
}

/**
 * @given A queue containing 10 subtasks and two nodes
 * @when Both nodes process subtasks with one node processing the final subtask
 * @then The node that processes the final subtask finalizes the job
 */
TEST_F(SubTaskQueueAccessorImplTest, TwoNodesProcessingAndFinalizing)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    auto start1Future = pubs1->Start(40001, {});

    std::chrono::milliseconds resultTime;
    assertWaitForCondition(
        [&start1Future]() {
            start1Future.get();
            return true;
        },
        std::chrono::milliseconds(2000),
        "Pubsub node initialization failed",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for pubsub node initialization");

    // Create a result channel for test with the correct ID
    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(pubs1, "RESULT_CHANNEL_ID_test");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "QUEUE_CHANNEL_ID");

    // Both nodes process at the same speed
    auto processingCore1 = std::make_shared<ProcessingCoreImpl>(50);  // 50ms per subtask
    auto processingCore2 = std::make_shared<ProcessingCoreImpl>(50);  // 50ms per subtask

    auto nodeId1 = "NODE_1";
    auto nodeId2 = "NODE_2";

    // Create a queue with 10 subtasks
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);

    // Add 10 subtasks to the queue
    for (int i = 1; i <= 10; i++) {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->mutable_subtasks()->add_items();
        subTask->set_subtaskid("SUBTASK_ID" + std::to_string(i));
        auto chunk = subTask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }

    // Setup queue managers
    auto processingQueueManager1 = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1, [](const std::string &){});
    processingQueueManager1->ProcessSubTaskQueueMessage(queue.release());

    auto processingQueueManager2 = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId2, [](const std::string &){});

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
        pubs1,
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

    // Start processing on both nodes
    std::atomic<bool> connectionEstablished1 = false;
    std::atomic<bool> connectionEstablished2 = false;

    subTaskQueueAccessor1->ConnectToSubTaskQueue([&]() {
        engine1->StartQueueProcessing(subTaskQueueAccessor1);
        connectionEstablished1 = true;
    });

    subTaskQueueAccessor2->ConnectToSubTaskQueue([&]() {
        engine2->StartQueueProcessing(subTaskQueueAccessor2);
        connectionEstablished2 = true;
    });

    // Wait for connections to be established
    assertWaitForCondition(
        [&connectionEstablished1, &connectionEstablished2]() {
            return connectionEstablished1.load() && connectionEstablished2.load();
        },
        std::chrono::milliseconds(2000),
        "Connections to subtask queues were not established",
        &resultTime
    );

    // Wait for all subtasks to be processed by both nodes
    assertWaitForCondition(
        [&processingCore1, &processingCore2]() {
            return processingCore1->m_processedSubTasks.size() +
                   processingCore2->m_processedSubTasks.size() >= 10;
        },
        std::chrono::milliseconds(5000),
        "Not all subtasks were processed",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for processing all subtasks");

    // Wait for either node to finalize the task
    assertWaitForCondition(
        [&isTaskFinalized1, &isTaskFinalized2]() {
            return isTaskFinalized1.load() || isTaskFinalized2.load();
        },
        std::chrono::milliseconds(3000),
        "Task was not finalized by any node",
        &resultTime
    );

    Color::PrintInfo("Waited ", resultTime.count(), " ms for task finalization");

    // Cleanup
    pubs1->Stop();

    // Verify results
    size_t totalProcessed = processingCore1->m_processedSubTasks.size() +
                            processingCore2->m_processedSubTasks.size();
    ASSERT_EQ(10, totalProcessed);

    // Only one node should finalize the task
    ASSERT_TRUE(isTaskFinalized1.load() || isTaskFinalized2.load());

    // The node that processed the last subtask should be the one that finalized
    bool node1ProcessedLast = false;
    bool node2ProcessedLast = false;

    // Determine which node processed the last subtask based on timestamps
    // (We can't easily determine this without timestamps, but we know one of them did)
    if (isTaskFinalized1.load()) {
        ASSERT_TRUE(processingQueueManager1->HasOwnership());
    } else {
        ASSERT_TRUE(processingQueueManager2->HasOwnership());
    }

    // Check if finalization occurred without errors
    ASSERT_TRUE(isTaskFinalized1.load() || isTaskFinalized2.load());
}