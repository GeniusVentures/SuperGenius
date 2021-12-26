#include "processing/processing_engine.hpp"

#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <iostream>
#include <boost/functional/hash.hpp>

using namespace sgns::processing;

namespace
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(size_t processingMillisec)
            : m_processingMillisec(processingMillisec)
        {
        }

        void SplitTask(const SGProcessing::Task& task, SubTaskList& subTasks) override
        {
            auto subtask = std::make_unique<SGProcessing::SubTask>();
            subTasks.push_back(std::move(subtask));
        }

        void ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override 
        {
            if (m_processingMillisec > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_processingMillisec));
            }

            auto itResultHashes = m_chunkResultHashes.find(subTask.results_channel());

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

                result.add_chunk_hashes(chunkHash);
                boost::hash_combine(subTaskResultHash, chunkHash);
            }

            result.set_result_hash(subTaskResultHash);

            m_processedSubTasks.push_back(subTask);
            m_initialHashes.push_back(initialHashCode);
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
class ProcessingEngineTest : public ::testing::Test
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
        libp2p::log::setLevelOfGroup("processing_engine_test", soralog::Level::DEBUG);
    }
};
/**
 * @given A node is subscribed to result channed 
 * @when A result is published to the channel
 * @then The node receives the result
 */
TEST_F(ProcessingEngineTest, SubscribtionToResultChannel)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, {pubs1->GetLocalAddress()});

    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(pubs1, "RESULT_CHANNEL_ID");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
    {     
    });

    auto queueChannel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "QUEUE_CHANNEL_ID");
    queueChannel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});


    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>(0);

    auto nodeId = "NODE_1";
    ProcessingEngine engine(pubs1, nodeId, processingCore, [](const SGProcessing::TaskResult&) {});

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id("DIFFERENT_NODE_ID");

    auto item = queue->mutable_processing_queue()->add_items();
    auto subTask = queue->add_subtasks();
    subTask->set_results_channel("RESULT_CHANNEL_ID");

    auto processingQueue = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId);
    // The local queue wrapper doesn't own the queue
    processingQueue->ProcessSubTaskQueueMessage(queue.release());

    engine.StartQueueProcessing(processingQueue);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    SGProcessing::SubTaskResult result;    
    resultChannel.Publish(result.SerializeAsString());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pubs1->Stop();
    pubs2->Stop();

    ASSERT_EQ(1, engine.GetResults().size());
    EXPECT_EQ("RESULT_CHANNEL_ID", std::get<0>(engine.GetResults()[0]));
}

/**
 * @given A queue containing subtasks
 * @when Processing is started
 * @then ProcessingCore::ProcessSubTask is called for each subtask.
 */
TEST_F(ProcessingEngineTest, SubTaskProcessing)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "QUEUE_CHANNEL_ID");
    queueChannel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});


    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>(0);

    auto nodeId = "NODE_1";
    ProcessingEngine engine(pubs1, nodeId, processingCore, [](const SGProcessing::TaskResult&) {});

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    // Local queue wrapped owns the queue
    queue->mutable_processing_queue()->set_owner_node_id(nodeId);
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID1");
    }
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID2");
    }

    auto processingQueue = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId);
    processingQueue->ProcessSubTaskQueueMessage(queue.release());

    engine.StartQueueProcessing(processingQueue);

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    pubs1->Stop();

    ASSERT_EQ(2, processingCore->m_processedSubTasks.size());
    EXPECT_EQ("RESULT_CHANNEL_ID1", processingCore->m_processedSubTasks[0].results_channel());
    EXPECT_EQ("RESULT_CHANNEL_ID2", processingCore->m_processedSubTasks[1].results_channel());
}

/**
 * @given A queue containing 2 subtasks
 * @when 2 engined sequentually start the queue processing
 * @then Each of them processes only 1 subtask from the queue.
 */
TEST_F(ProcessingEngineTest, SharedSubTaskProcessing)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "QUEUE_CHANNEL_ID");
    queueChannel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});


    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>(500);

    auto nodeId1 = "NODE_1";
    auto nodeId2 = "NODE_2";

    ProcessingEngine engine1(pubs1, nodeId1, processingCore, [](const SGProcessing::TaskResult&) {});
    ProcessingEngine engine2(pubs1, nodeId2, processingCore, [](const SGProcessing::TaskResult&) {});

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    // Local queue wrapped owns the queue
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID1");
    }
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID2");
    }

    auto processingQueue1 = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId1);
    processingQueue1->ProcessSubTaskQueueMessage(queue.release());

    engine1.StartQueueProcessing(processingQueue1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto processingQueue2 = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId2);

    // Change queue owner
    SGProcessing::SubTaskQueueRequest queueRequest;
    queueRequest.set_node_id(nodeId2);
    auto updatedQueue = processingQueue1->ProcessSubTaskQueueRequestMessage(queueRequest);

    // Synchronize the queues
    processingQueue2->ProcessSubTaskQueueMessage(processingQueue1->GetQueueSnapshot().release());
    processingQueue1->ProcessSubTaskQueueMessage(processingQueue2->GetQueueSnapshot().release());

    engine2.StartQueueProcessing(processingQueue2);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pubs1->Stop();

    ASSERT_EQ(2, processingCore->m_initialHashes.size());
    EXPECT_EQ(static_cast<uint32_t>(std::hash<std::string>{}(nodeId1)), processingCore->m_initialHashes[0]);
    EXPECT_EQ(static_cast<uint32_t>(std::hash<std::string>{}(nodeId2)), processingCore->m_initialHashes[1]);
}

/**
 * @given A queue containing 2 subtasks
 * @when Subtasks are finished and chunk hashes are valid
 * @then Task finalization sink is called.
 */
TEST_F(ProcessingEngineTest, TaskFinalization)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "QUEUE_CHANNEL_ID");
    queueChannel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>(100);

    auto nodeId1 = "NODE_1";

    bool isTaskFinalized = false;
    ProcessingEngine engine1(pubs1, nodeId1, processingCore,
        [&isTaskFinalized](const SGProcessing::TaskResult&) { isTaskFinalized = true; });
    processingCore->m_chunkResultHashes["RESULT_CHANNEL_ID1"] = { 0 };
    processingCore->m_chunkResultHashes["RESULT_CHANNEL_ID2"] = { 0 };

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);
    chunk1.set_line_stride(1);
    chunk1.set_offset(0);
    chunk1.set_stride(1);
    chunk1.set_subchunk_height(10);
    chunk1.set_subchunk_width(10);

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    // Local queue wrapped owns the queue
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID1");
        auto chunk = subTask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID2");
        auto chunk = subTask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }

    auto processingQueue1 = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId1);
    processingQueue1->ProcessSubTaskQueueMessage(queue.release());

    engine1.StartQueueProcessing(processingQueue1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pubs1->Stop();

    ASSERT_TRUE(isTaskFinalized);
}

/**
 * @given A queue containing 2 subtasks
 * @when Subtasks contains invalid chunk hashes
 * @then The subtasks processing is restarted.
 */
TEST_F(ProcessingEngineTest, InvalidSubTasksRestart)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "QUEUE_CHANNEL_ID");
    queueChannel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // The processing core 1 has invalid chunk result hashes
    auto processingCore1 = std::make_shared<ProcessingCoreImpl>(500);
    processingCore1->m_chunkResultHashes["RESULT_CHANNEL_ID1"] = { 0 };
    processingCore1->m_chunkResultHashes["RESULT_CHANNEL_ID2"] = { 1 };


    // The processing core 2 has invalid chunk result hashes
    auto processingCore2 = std::make_shared<ProcessingCoreImpl>(100);
    processingCore2->m_chunkResultHashes["RESULT_CHANNEL_ID1"] = { 0 };
    processingCore2->m_chunkResultHashes["RESULT_CHANNEL_ID2"] = { 0 };

    auto nodeId1 = "NODE_1";
    auto nodeId2 = "NODE_2";

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);
    chunk1.set_line_stride(1);
    chunk1.set_offset(0);
    chunk1.set_stride(1);
    chunk1.set_subchunk_height(10);
    chunk1.set_subchunk_width(10);

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    // Local queue wrapped owns the queue
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID1");
        auto chunk = subTask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID2");
        auto chunk = subTask->add_chunkstoprocess();
        chunk->CopyFrom(chunk1);
    }

    auto processingQueue1 = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId1);
    processingQueue1->ProcessSubTaskQueueMessage(queue.release());

    bool isTaskFinalized1 = false;
    ProcessingEngine engine1(pubs1, nodeId1, processingCore1,
        [&isTaskFinalized1](const SGProcessing::TaskResult&) { isTaskFinalized1 = true; });

    bool isTaskFinalized2 = false;
    ProcessingEngine engine2(pubs1, nodeId2, processingCore2,
        [&isTaskFinalized2](const SGProcessing::TaskResult&) { isTaskFinalized2 = true; });

    engine1.StartQueueProcessing(processingQueue1);

    // Wait for queue processing by node1
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // No task finalization should be called when there are invalid chunk results
    ASSERT_FALSE(isTaskFinalized1);

    auto processingQueue2 = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId2);

    // Change queue owner
    SGProcessing::SubTaskQueueRequest queueRequest;
    queueRequest.set_node_id(nodeId2);
    auto updatedQueue = processingQueue1->ProcessSubTaskQueueRequestMessage(queueRequest);

    // Synchronize the queues
    processingQueue2->ProcessSubTaskQueueMessage(processingQueue1->GetQueueSnapshot().release());
    processingQueue1->ProcessSubTaskQueueMessage(processingQueue2->GetQueueSnapshot().release());
    engine1.StopQueueProcessing();

    // Wait for failed tasks expiration
    // @todo Automatically mark failed tasks as exired
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));

    engine2.StartQueueProcessing(processingQueue2);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pubs1->Stop();

    // Task should be finalized because chunks have valid hashes
    ASSERT_TRUE(isTaskFinalized2);
}


