#include "processing_service_test.hpp"

#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>

#include <boost/functional/hash.hpp>
#include <thread>

#include "testutil/wait_condition.hpp"
#include "base/logger.hpp"

using namespace sgns::processing;
using namespace sgns::test;
using namespace sgns::base;

const std::string logger_config( R"(
# ----------------
sinks:
 - name: console
   type: console
   color: true
groups:
 - name: subtask_validation_test
   sink: console
   level: info
   children:
   - name: libp2p
   - name: Gossip
# ----------------
 )" );

class SubTaskValidationTest : public ProcessingServiceTest
{
public:
    virtual void SetUp() override
    {
        ProcessingServiceTest::SetUp( "subtask_validation_test", logger_config );
        ProcessingServiceTest::Initialize( 2, 50 );
    }

    // Helper method to create a subtask with specified number of chunks
    SGProcessing::SubTask CreateTestSubTask(const std::string& subTaskId, int numChunks) {
        SGProcessing::SubTask subTask;
        subTask.set_subtaskid(subTaskId);
        subTask.set_ipfsblock("test_ipfs_block");
        subTask.set_json_data("{}");
        subTask.set_datalen(1000);
        
        for (int i = 0; i < numChunks; ++i) {
            auto* chunk = subTask.add_chunkstoprocess();
            chunk->set_chunkid("chunk_" + std::to_string(i));
            chunk->set_n_subchunks(1);
        }
        
        return subTask;
    }

    // Helper method to create a valid result with correct number of chunk hashes
    SGProcessing::SubTaskResult CreateValidResult(const std::string& subTaskId, int numChunks) {
        SGProcessing::SubTaskResult result;
        result.set_subtaskid(subTaskId);
        result.set_node_address("test_node");
        
        // Create unique hashes for each chunk
        for (int i = 0; i < numChunks; ++i) {
            std::string hash = "hash_" + std::to_string(i) + "_" + subTaskId;
            result.add_chunk_hashes(hash);
        }
        
        return result;
    }

    // Helper method to create an invalid result (wrong number of hashes)
    SGProcessing::SubTaskResult CreateInvalidResult(const std::string& subTaskId, int wrongNumChunks) {
        SGProcessing::SubTaskResult result;
        result.set_subtaskid(subTaskId);
        result.set_node_address("test_node");
        
        // Create wrong number of hashes
        for (int i = 0; i < wrongNumChunks; ++i) {
            std::string hash = "invalid_hash_" + std::to_string(i);
            result.add_chunk_hashes(hash);
        }
        
        return result;
    }

    // Helper method to create result with duplicate hashes
    SGProcessing::SubTaskResult CreateDuplicateHashResult(const std::string& subTaskId, int numChunks) {
        SGProcessing::SubTaskResult result;
        result.set_subtaskid(subTaskId);
        result.set_node_address("test_node");
        
        // Create duplicate hashes (all the same)
        for (int i = 0; i < numChunks; ++i) {
            result.add_chunk_hashes("duplicate_hash");
        }
        
        return result;
    }

    const std::string nodeId1 = "NODE_1";
    const std::string nodeId2 = "NODE_2";
};

/**
 * @given A subtask with 3 chunks
 * @when CompleteSubTask is called with a valid result (3 chunk hashes)
 * @then The result should be accepted and stored
 */
TEST_F(SubTaskValidationTest, CompleteSubTask_ValidResult_AcceptsResult)
{
    auto pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    // Create queue with one subtask (3 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    
    auto subTask = CreateTestSubTask("SUBTASK_VALID", 3);
    auto item = queue->mutable_processing_queue()->add_items();
    auto* queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask = subTask;

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "VALIDATION_QUEUE");
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1, [](const std::string &){});
    
    processingQueueManager->ProcessSubTaskQueueMessage(queue.release());

    std::atomic<bool> taskFinalized = false;
    std::atomic<bool> errorOccurred = false;
    std::string errorMessage;

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [&taskFinalized](const SGProcessing::TaskResult&) { taskFinalized = true; },
        [&errorOccurred, &errorMessage](const std::string& error) { 
            errorOccurred = true; 
            errorMessage = error;
        });

    subTaskQueueAccessor->CreateResultsChannel("validation_test");

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue([&connected]() { connected = true; });

    ASSERT_WAIT_FOR_CONDITION(
        [&connected]() { return connected.load(); },
        std::chrono::milliseconds(2000),
        "Failed to connect to subtask queue",
        &resultTime
    );

    // Test CompleteSubTask with valid result
    auto validResult = CreateValidResult("SUBTASK_VALID", 3);
    subTaskQueueAccessor->CompleteSubTask("SUBTASK_VALID", validResult);

    // Wait a bit to see if any errors occur
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should not have triggered an error
    EXPECT_FALSE(errorOccurred.load());
    if (errorOccurred.load()) {
        Color::PrintError("Unexpected error: ", errorMessage);
    }
}

/**
 * @given A subtask with 3 chunks
 * @when CompleteSubTask is called with invalid result (wrong number of hashes)
 * @then The result should be rejected and not stored
 */
TEST_F(SubTaskValidationTest, CompleteSubTask_InvalidResult_RejectsResult)
{
    auto pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    // Create queue with one subtask (3 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    
    auto subTask = CreateTestSubTask("SUBTASK_INVALID", 3);
    auto item = queue->mutable_processing_queue()->add_items();
    auto* queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask = subTask;

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "VALIDATION_QUEUE_2");
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1, [](const std::string &){});
    
    processingQueueManager->ProcessSubTaskQueueMessage(queue.release());

    std::atomic<bool> taskFinalized = false;
    std::atomic<bool> errorOccurred = false;

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [&taskFinalized](const SGProcessing::TaskResult&) { taskFinalized = true; },
        [&errorOccurred](const std::string&) { errorOccurred = true; });

    subTaskQueueAccessor->CreateResultsChannel("validation_test_2");

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue([&connected]() { connected = true; });

    ASSERT_WAIT_FOR_CONDITION(
        [&connected]() { return connected.load(); },
        std::chrono::milliseconds(2000),
        "Failed to connect to subtask queue",
        &resultTime
    );

    // Test CompleteSubTask with invalid result (5 hashes instead of 3)
    auto invalidResult = CreateInvalidResult("SUBTASK_INVALID", 5);
    subTaskQueueAccessor->CompleteSubTask("SUBTASK_INVALID", invalidResult);

    // Wait a bit to see if validation catches the error
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should have triggered validation error (once you implement the validation)
    EXPECT_TRUE(errorOccurred.load());
}

/**
 * @given A subtask queue with one subtask
 * @when OnResultReceived gets a valid external result
 * @then The result should be accepted
 */
TEST_F(SubTaskValidationTest, OnResultReceived_ValidExternalResult_AcceptsResult)
{
    auto pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    // Create result channel
    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(pubs1, "EXTERNAL_RESULT_CHANNEL");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&>) {});

    // Create queue with one subtask (2 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    
    auto subTask = CreateTestSubTask("EXTERNAL_SUBTASK", 2);
    auto item = queue->mutable_processing_queue()->add_items();
    auto* queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask = subTask;

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "EXTERNAL_VALIDATION_QUEUE");
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1, [](const std::string &){});
    
    processingQueueManager->ProcessSubTaskQueueMessage(queue.release());

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [](const SGProcessing::TaskResult&) {},
        [](const std::string&) {});

    subTaskQueueAccessor->CreateResultsChannel("external_validation_test");

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue([&connected]() { connected = true; });

    ASSERT_WAIT_FOR_CONDITION(
        [&connected]() { return connected.load(); },
        std::chrono::milliseconds(2000),
        "Failed to connect to subtask queue",
        &resultTime
    );

    // Publish valid external result
    auto validResult = CreateValidResult("EXTERNAL_SUBTASK", 2);
    resultChannel.Publish(validResult.SerializeAsString());

    // Wait for result to be received
    ASSERT_WAIT_FOR_CONDITION(
        [&subTaskQueueAccessor]() { return subTaskQueueAccessor->GetResults().size() > 0; },
        std::chrono::milliseconds(2000),
        "Valid external result was not received",
        &resultTime
    );

    // Should have 1 result
    EXPECT_EQ(1, subTaskQueueAccessor->GetResults().size());
    EXPECT_EQ("EXTERNAL_SUBTASK", std::get<0>(subTaskQueueAccessor->GetResults()[0]));
}

/**
 * @given A subtask queue with one subtask
 * @when OnResultReceived gets an invalid external result
 * @then The result should be rejected
 */
TEST_F(SubTaskValidationTest, OnResultReceived_InvalidExternalResult_RejectsResult)
{
    auto pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    // Create result channel
    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(pubs1, "INVALID_EXTERNAL_RESULT_CHANNEL");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&>) {});

    // Create queue with one subtask (2 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    
    auto subTask = CreateTestSubTask("INVALID_EXTERNAL_SUBTASK", 2);
    auto item = queue->mutable_processing_queue()->add_items();
    auto* queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask = subTask;

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "INVALID_EXTERNAL_VALIDATION_QUEUE");
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1, [](const std::string &){});
    
    processingQueueManager->ProcessSubTaskQueueMessage(queue.release());

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [](const SGProcessing::TaskResult&) {},
        [](const std::string&) {});

    subTaskQueueAccessor->CreateResultsChannel("invalid_external_validation_test");

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue([&connected]() { connected = true; });

    ASSERT_WAIT_FOR_CONDITION(
        [&connected]() { return connected.load(); },
        std::chrono::milliseconds(2000),
        "Failed to connect to subtask queue",
        &resultTime
    );

    // Publish invalid external result (4 hashes instead of 2)
    auto invalidResult = CreateInvalidResult("INVALID_EXTERNAL_SUBTASK", 4);
    resultChannel.Publish(invalidResult.SerializeAsString());

    // Wait a bit to see if result gets rejected
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Should have 0 results (rejected)
    EXPECT_EQ(0, subTaskQueueAccessor->GetResults().size());
}

/**
 * @given A subtask with 3 chunks  
 * @when CompleteSubTask is called with duplicate chunk hashes
 * @then The result should be rejected due to duplicate hashes
 */
TEST_F(SubTaskValidationTest, CompleteSubTask_DuplicateHashes_RejectsResult)
{
    auto pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    // Create queue with one subtask (3 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    
    auto subTask = CreateTestSubTask("SUBTASK_DUPLICATE", 3);
    auto item = queue->mutable_processing_queue()->add_items();
    auto* queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask = subTask;

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "DUPLICATE_VALIDATION_QUEUE");
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1, [](const std::string &){});
    
    processingQueueManager->ProcessSubTaskQueueMessage(queue.release());

    std::atomic<bool> errorOccurred = false;

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        [](const SGProcessing::TaskResult&) {},
        [&errorOccurred](const std::string&) { errorOccurred = true; });

    subTaskQueueAccessor->CreateResultsChannel("duplicate_validation_test");

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue([&connected]() { connected = true; });

    ASSERT_WAIT_FOR_CONDITION(
        [&connected]() { return connected.load(); },
        std::chrono::milliseconds(2000),
        "Failed to connect to subtask queue",
        &resultTime
    );

    // Test CompleteSubTask with duplicate hashes
    auto duplicateResult = CreateDuplicateHashResult("SUBTASK_DUPLICATE", 3);
    subTaskQueueAccessor->CompleteSubTask("SUBTASK_DUPLICATE", duplicateResult);

    // Wait a bit to see if validation catches the duplicate hashes
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should have triggered validation error (once you implement the validation)
    EXPECT_TRUE(errorOccurred.load());
}
