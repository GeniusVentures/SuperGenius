#include "processing/processing_engine.hpp"
#include "processing/processing_subtask_queue_accessor_impl.hpp"
#include "processing/processing_subtask_queue_channel_pubsub.hpp"
#include "processing/processing_subtask_state_storage.hpp"
#include "processing/processing_subtask_result_storage.hpp"

#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <iostream>
#include <boost/functional/hash.hpp>
#include <thread>

using namespace sgns::processing;

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
        void AddSubTaskResult(const SGProcessing::SubTaskResult& subTaskResult) override {}
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

        outcome::result<SGProcessing::SubTaskResult> ProcessSubTask(
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
        libp2p::log::setLevelOfGroup("processing_engine_test", soralog::Level::DEBUG);
    }
};

/**
 * @given A node is subscribed to result channel
 * @when A result is published to the channel
 * @then The node receives the result
 */
TEST_F(SubTaskQueueAccessorImplTest, DISABLED_SubscribtionToResultChannel)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, { pubs1->GetLocalAddress() });

    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(pubs1, "RESULT_CHANNEL_ID");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
        {
        });

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "QUEUE_CHANNEL_ID");

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

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

    subTaskQueueAccessor->ConnectToSubTaskQueue([&]() {
        engine->StartQueueProcessing(subTaskQueueAccessor);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Publish result to the results channel
    SGProcessing::SubTaskResult result;
    result.set_subtaskid("SUBTASK_ID");
    resultChannel.Publish(result.SerializeAsString());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

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
TEST_F(SubTaskQueueAccessorImplTest, DISABLED_TaskFinalization)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "QUEUE_CHANNEL_ID");

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>(100);

    auto nodeId1 = "NODE_1";

    bool isTaskFinalized = false;
    auto engine1 = std::make_shared<ProcessingEngine>(nodeId1, processingCore, [](const std::string &){},[]{});
    processingCore->m_chunkResultHashes["SUBTASK_ID1"] = { 0 };
    processingCore->m_chunkResultHashes["SUBTASK_ID2"] = { 0 };

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);
    //chunk1.set_line_stride(1);
    //chunk1.set_offset(0);
    //chunk1.set_stride(1);
    //chunk1.set_subchunk_height(10);
    //chunk1.set_subchunk_width(10);

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
        [&isTaskFinalized](const SGProcessing::TaskResult&) { isTaskFinalized = true; },
        [](const std::string &) {});

    subTaskQueueAccessor1->ConnectToSubTaskQueue([&]() {
        engine1->StartQueueProcessing(subTaskQueueAccessor1);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pubs1->Stop();

    ASSERT_TRUE(isTaskFinalized);
}

/**
 * @given A queue containing 2 subtasks
 * @when Subtasks contains invalid chunk hashes
 * @then The subtasks processing is restarted.
 */
TEST_F(SubTaskQueueAccessorImplTest, DISABLED_InvalidSubTasksRestart)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "QUEUE_CHANNEL_ID");

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // The processing core 1 has invalid chunk result hashes
    auto processingCore1 = std::make_shared<ProcessingCoreImpl>(500);
    processingCore1->m_chunkResultHashes["SUBTASK_ID1"] = { 0 };
    processingCore1->m_chunkResultHashes["SUBTASK_ID2"] = { 1 };


    // The processing core 2 has invalid chunk result hashes
    auto processingCore2 = std::make_shared<ProcessingCoreImpl>(100);
    processingCore2->m_chunkResultHashes["SUBTASK_ID1"] = { 0 };
    processingCore2->m_chunkResultHashes["SUBTASK_ID2"] = { 0 };

    auto nodeId1 = "NODE_1";
    auto nodeId2 = "NODE_2";

    SGProcessing::ProcessingChunk chunk1;
    chunk1.set_chunkid("CHUNK_1");
    chunk1.set_n_subchunks(1);
    //chunk1.set_line_stride(1);
    //chunk1.set_offset(0);
    //chunk1.set_stride(1);
    //chunk1.set_subchunk_height(10);
    //chunk1.set_subchunk_width(10);

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

    auto processingQueueManager1 = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,[](const std::string &){});
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

    subTaskQueueAccessor1->ConnectToSubTaskQueue([&]() {
        engine1->StartQueueProcessing(subTaskQueueAccessor1);
        });

    // Wait for queue processing by node1
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // No task finalization should be called when there are invalid chunk results
    ASSERT_FALSE(isTaskFinalized1);

    auto processingQueueManager2 = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId2,[](const std::string &){});
    processingQueueManager2->SetProcessingTimeout(std::chrono::milliseconds(500));

    // Change queue owner
    SGProcessing::SubTaskQueueRequest queueRequest;
    queueRequest.set_node_id(nodeId2);
    auto updatedQueue = processingQueueManager1->ProcessSubTaskQueueRequestMessage(queueRequest);

    // Synchronize the queues
    processingQueueManager2->ProcessSubTaskQueueMessage(processingQueueManager1->GetQueueSnapshot().release());
    processingQueueManager1->ProcessSubTaskQueueMessage(processingQueueManager2->GetQueueSnapshot().release());
    engine1->StopQueueProcessing();

    // Wait for failed tasks expiration. Check processingQueueManager2->SetProcessingTimeout() call
    // @todo Automatically mark failed tasks as exired.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto subTaskQueueAccessor2 = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager2,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [&isTaskFinalized2](const SGProcessing::TaskResult&) { isTaskFinalized2 = true; },
        [](const std::string &) {}
    );

    subTaskQueueAccessor2->ConnectToSubTaskQueue([&]() {
        engine2->StartQueueProcessing(subTaskQueueAccessor2);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pubs1->Stop();

    // Task should be finalized because chunks have valid hashes
    ASSERT_TRUE(processingQueueManager2->HasOwnership());
    ASSERT_TRUE(isTaskFinalized2);
}
