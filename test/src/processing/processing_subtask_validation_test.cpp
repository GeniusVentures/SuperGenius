#include "processing_service_test.hpp"

#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>

#include <boost/functional/hash.hpp>
#include <thread>

#include "testutil/wait_condition.hpp"
#include "base/logger.hpp"
#include "base/sgns_version.hpp"

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
    void SetUp() override
    {
        ProcessingServiceTest::SetUp( "subtask_validation_test", logger_config );
        ProcessingServiceTest::Initialize( 2, 50 );
    }

    // Helper method to create a subtask with specified number of chunks
    static SGProcessing::SubTask CreateTestSubTask( const std::string &subTaskId, int numChunks )
    {
        SGProcessing::SubTask subTask;
        subTask.set_subtaskid( subTaskId );
        subTask.set_ipfsblock( "test_ipfs_block" );
        subTask.set_json_data( "{}" );
        subTask.set_datalen( 1000 );

        for ( int i = 0; i < numChunks; ++i )
        {
            auto *chunk = subTask.add_chunkstoprocess();
            chunk->set_chunkid( "chunk_" + std::to_string( i ) );
            chunk->set_n_subchunks( 1 );
        }

        return subTask;
    }

    // Helper method to create a valid result with correct number of chunk hashes
    static SGProcessing::SubTaskResult CreateValidResult( const std::string &subTaskId, int numChunks )
    {
        SGProcessing::SubTaskResult result;
        result.set_subtaskid( subTaskId );
        result.set_node_address( std::string( 128, 'a' ) );
        result.set_developer_address( "0xcafe" );
        result.set_developer_cut( 350000 );
        result.set_token_id( std::string( 32, '\0' ) );

        // Create unique hashes for each chunk
        for ( int i = 0; i < numChunks; ++i )
        {
            std::string hash = "hash_" + std::to_string( i ) + "_" + subTaskId;
            result.add_chunk_hashes( hash );
        }

        return result;
    }

    // Helper method to create an invalid result (wrong number of hashes)
    static SGProcessing::SubTaskResult CreateInvalidResult( const std::string &subTaskId, int wrongNumChunks )
    {
        SGProcessing::SubTaskResult result;
        result.set_subtaskid( subTaskId );
        result.set_node_address( std::string( 128, 'a' ) );
        result.set_developer_address( "0xcafe" );
        result.set_developer_cut( 350000 );
        result.set_token_id( std::string( 32, '\0' ) );

        // Create wrong number of hashes
        for ( int i = 0; i < wrongNumChunks; ++i )
        {
            std::string hash = "invalid_hash_" + std::to_string( i );
            result.add_chunk_hashes( hash );
        }

        return result;
    }

    // Helper method to create result with duplicate hashes
    static SGProcessing::SubTaskResult CreateDuplicateHashResult( const std::string &subTaskId, int numChunks )
    {
        SGProcessing::SubTaskResult result;
        result.set_subtaskid( subTaskId );
        result.set_node_address( std::string( 128, 'a' ) );
        result.set_developer_address( "0xcafe" );
        result.set_developer_cut( 350000 );
        result.set_token_id( std::string( 32, '\0' ) );

        // Create duplicate hashes (all the same)
        for ( int i = 0; i < numChunks; ++i )
        {
            result.add_chunk_hashes( "duplicate_hash" );
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
TEST_F( SubTaskValidationTest, CompleteSubTask_ValidResult_AcceptsResult )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    // Create queue with one subtask (3 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto  subTask      = CreateTestSubTask( "SUBTASK_VALID", 3 );
    auto  item         = queue->mutable_processing_queue()->add_items();
    auto *queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask      = subTask;

    auto queueChannel           = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "VALIDATION_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>( queueChannel,
                                                                                   pubs1->GetAsioContext(),
                                                                                   nodeId1,
                                                                                   []( const std::string & ) {} );

    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> taskFinalized = false;
    std::atomic<bool> errorOccurred = false;
    std::string       errorMessage;

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        [&taskFinalized]( const SGProcessing::TaskResult & ) { taskFinalized = true; },
        [&errorOccurred, &errorMessage]( const std::string &error )
        {
            errorOccurred = true;
            errorMessage  = error;
        } );

    subTaskQueueAccessor->CreateResultsChannel( "validation_test" );

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue( [&connected]() { connected = true; } );

    ASSERT_WAIT_FOR_CONDITION( [&connected]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect to subtask queue",
                               &resultTime );

    // Test CompleteSubTask with valid result
    auto validResult = CreateValidResult( "SUBTASK_VALID", 3 );
    subTaskQueueAccessor->CompleteSubTask( "SUBTASK_VALID", validResult );

    // Wait a bit to see if any errors occur
    EXPECT_WAIT_FOR_CONDITION(
        [&errorOccurred]() { return !errorOccurred.load(); },
        std::chrono::milliseconds( 300 ),
        "Valid result should not trigger an error",
        nullptr );
    if ( errorOccurred.load() )
    {
        Color::PrintError( "Unexpected error: ", errorMessage );
    }
}

/**
 * @given A subtask with 3 chunks
 * @when CompleteSubTask is called with invalid result (wrong number of hashes)
 * @then The result should be rejected and not stored
 */
TEST_F( SubTaskValidationTest, CompleteSubTask_InvalidResult_RejectsResult )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    // Create queue with one subtask (3 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto  subTask      = CreateTestSubTask( "SUBTASK_INVALID", 3 );
    auto  item         = queue->mutable_processing_queue()->add_items();
    auto *queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask      = subTask;

    auto queueChannel           = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "VALIDATION_QUEUE_2" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>( queueChannel,
                                                                                   pubs1->GetAsioContext(),
                                                                                   nodeId1,
                                                                                   []( const std::string & ) {} );

    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> taskFinalized = false;
    std::atomic<bool> errorOccurred = false;

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        [&taskFinalized]( const SGProcessing::TaskResult & ) { taskFinalized = true; },
        [&errorOccurred]( const std::string & ) { errorOccurred = true; } );

    subTaskQueueAccessor->CreateResultsChannel( "validation_test_2" );

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue( [&connected]() { connected = true; } );

    ASSERT_WAIT_FOR_CONDITION( [&connected]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect to subtask queue",
                               &resultTime );

    // Test CompleteSubTask with invalid result (5 hashes instead of 3)
    auto invalidResult = CreateInvalidResult( "SUBTASK_INVALID", 5 );
    subTaskQueueAccessor->CompleteSubTask( "SUBTASK_INVALID", invalidResult );

    std::chrono::milliseconds elapsed;
    ASSERT_WAIT_FOR_CONDITION(
        [&errorOccurred]() { return errorOccurred.load(); },
        std::chrono::milliseconds( 500 ),
        "Validation did not reject invalid result within timeout",
        &elapsed );

    // Should have triggered validation error
    EXPECT_TRUE( errorOccurred.load() );
}

/**
 * @given A subtask queue with one subtask
 * @when An external result is published to the result channel
 * @then The result should be accepted if valid, rejected if invalid
 */
TEST_F( SubTaskValidationTest, OnResultReceived_ValidExternalResult_AcceptsResult )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    auto                      pubs2 = m_pubsub_nodes[1];
    std::chrono::milliseconds resultTime;

    // Create queue with one subtask (2 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    // Add a ProcessingQueueItem
    auto item = queue->mutable_processing_queue()->add_items();
    item->set_lock_timestamp( 0 );
    item->set_lock_node_id( "" );
    item->set_lock_expiration_timestamp( 0 );

    // Add the corresponding subtask
    auto  subTask      = CreateTestSubTask( "EXTERNAL_SUBTASK", 2 );
    auto *queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask      = subTask;

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "EXTERNAL_VALIDATION_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>( queueChannel,
                                                                                   pubs1->GetAsioContext(),
                                                                                   nodeId1,
                                                                                   []( const std::string & ) {} );

    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> errorOccurred = false;
    std::string       lastError;

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&errorOccurred, &lastError]( const std::string &error )
        {
            errorOccurred = true;
            lastError     = error;
            std::cout << "Error occurred: " << error << std::endl;
        } );

    subTaskQueueAccessor->CreateResultsChannel( "external_validation_test" );

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue( [&connected]() { connected = true; } );

    ASSERT_WAIT_FOR_CONDITION( [&connected]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect to subtask queue",
                               &resultTime );

    // Wait for queue to be properly initialized
    ASSERT_WAIT_FOR_CONDITION( [&processingQueueManager]() { return processingQueueManager->IsQueueInit(); },
                               std::chrono::milliseconds( 2000 ),
                               "Queue was not properly initialized",
                               &resultTime );

    // Create external result publisher
    std::string externalChannelId = "RESULT_CHANNEL_ID_external_validation_test" + sgns::version::GetNetAndVersionAppendix();
    GossipPubSubTopic externalResultChannel( pubs2, externalChannelId );
    auto             &subscriptionFuture = externalResultChannel.Subscribe(
        []( const boost::optional<const GossipPubSub::Message &> &message ) {},
        false );

    // Wait for pubsub connection
    ASSERT_WAIT_FOR_CONDITION(
        [&subscriptionFuture]()
        { return subscriptionFuture.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready; },
        std::chrono::milliseconds( 2000 ),
        "External result channel subscription was not established",
        &resultTime );

    // Create and debug the result
    auto validResult = CreateValidResult( "EXTERNAL_SUBTASK", 2 );

    // Publish valid external result
    externalResultChannel.Publish( validResult.SerializeAsString() );

    // Wait for result to be processed
    ASSERT_WAIT_FOR_CONDITION(
        [&subTaskQueueAccessor]()
        {
            auto results = subTaskQueueAccessor->GetResults();
            if ( results.size() > 0 )
            {
                std::cout << "Got results presumably" << std::endl;
                return true;
            }
            return false;
        },
        std::chrono::milliseconds( 4000 ),
        "Results were never received",
        &resultTime );

    auto results = subTaskQueueAccessor->GetResults();

    // Check that result was accepted
    EXPECT_GT( results.size(), 0 ) << "No results found - external result was rejected";

    if ( results.size() > 0 )
    {
        EXPECT_EQ( "EXTERNAL_SUBTASK", std::get<0>( results[0] ) );
    }
}

/**
 * @given A subtask queue with one subtask
 * @when An invalid external result is published to the result channel
 * @then The result should be rejected due to validation failure
 */
TEST_F( SubTaskValidationTest, OnResultReceived_InvalidExternalResult_RejectsResult )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    auto                      pubs2 = m_pubsub_nodes[1]; // Use second node to simulate external result
    std::chrono::milliseconds resultTime;

    // Create queue with one subtask (2 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    // Add a ProcessingQueueItem
    auto item = queue->mutable_processing_queue()->add_items();
    item->set_lock_timestamp( 0 );
    item->set_lock_node_id( "" );
    item->set_lock_expiration_timestamp( 0 );

    // Add the corresponding subtask (2 chunks)
    auto  subTask      = CreateTestSubTask( "INVALID_EXTERNAL_SUBTASK", 2 );
    auto *queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask      = subTask;

    auto queueChannel           = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1,
                                                                               "INVALID_EXTERNAL_VALIDATION_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>( queueChannel,
                                                                                   pubs1->GetAsioContext(),
                                                                                   nodeId1,
                                                                                   []( const std::string & ) {} );

    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> errorOccurred        = false;
    auto              subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&errorOccurred]( const std::string & ) { errorOccurred = true; } );

    subTaskQueueAccessor->CreateResultsChannel( "invalid_external_validation_test" );

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue( [&connected]() { connected = true; } );

    ASSERT_WAIT_FOR_CONDITION( [&connected]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect to subtask queue",
                               &resultTime );

    // Wait for queue to be properly initialized
    ASSERT_WAIT_FOR_CONDITION( [&processingQueueManager]() { return processingQueueManager->IsQueueInit(); },
                               std::chrono::milliseconds( 2000 ),
                               "Queue was not properly initialized",
                               &resultTime );

    // Create external result publisher
    std::string externalChannelId = "RESULT_CHANNEL_ID_invalid_external_validation_test" + sgns::version::GetNetAndVersionAppendix();
    sgns::ipfs_pubsub::GossipPubSubTopic externalResultChannel( pubs2, externalChannelId );
    auto                                &subscriptionFuture = externalResultChannel.Subscribe(
        []( const boost::optional<const GossipPubSub::Message &> &message ) {},
        false );
    // Wait for pubsub connection
    ASSERT_WAIT_FOR_CONDITION(
        [&subscriptionFuture]()
        { return subscriptionFuture.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready; },
        std::chrono::milliseconds( 2000 ),
        "External result channel subscription was not established",
        &resultTime );

    // Publish invalid external result (4 hashes instead of 2)
    auto invalidResult = CreateInvalidResult( "INVALID_EXTERNAL_SUBTASK", 4 );
    externalResultChannel.Publish( invalidResult.SerializeAsString() );

    std::chrono::milliseconds elapsed;
    ASSERT_WAIT_FOR_CONDITION(
        [&errorOccurred]() { return errorOccurred.load(); },
        std::chrono::milliseconds( 2000 ),
        "Invalid external result was not rejected by validation",
        &elapsed );

    // Should have 0 results (rejected due to validation)
    auto results = subTaskQueueAccessor->GetResults();
    EXPECT_EQ( 0, results.size() ) << "Invalid result was not rejected by validation";
}

/**
 * @given A subtask with 3 chunks
 * @when CompleteSubTask is called with duplicate chunk hashes
 * @then The result should be rejected due to duplicate hashes
 */
TEST_F( SubTaskValidationTest, CompleteSubTask_DuplicateHashes_RejectsResult )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    // Create queue with one subtask (3 chunks)
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto  subTask      = CreateTestSubTask( "SUBTASK_DUPLICATE", 3 );
    auto  item         = queue->mutable_processing_queue()->add_items();
    auto *queueSubTask = queue->mutable_subtasks()->add_items();
    *queueSubTask      = subTask;

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "DUPLICATE_VALIDATION_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>( queueChannel,
                                                                                   pubs1->GetAsioContext(),
                                                                                   nodeId1,
                                                                                   []( const std::string & ) {} );

    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> errorOccurred = false;

    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&errorOccurred]( const std::string & ) { errorOccurred = true; } );

    subTaskQueueAccessor->CreateResultsChannel( "duplicate_validation_test" );

    std::atomic<bool> connected = false;
    subTaskQueueAccessor->ConnectToSubTaskQueue( [&connected]() { connected = true; } );

    ASSERT_WAIT_FOR_CONDITION( [&connected]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect to subtask queue",
                               &resultTime );

    // Test CompleteSubTask with duplicate hashes
    auto duplicateResult = CreateDuplicateHashResult( "SUBTASK_DUPLICATE", 3 );
    subTaskQueueAccessor->CompleteSubTask( "SUBTASK_DUPLICATE", duplicateResult );

    std::chrono::milliseconds elapsed;
    ASSERT_WAIT_FOR_CONDITION(
        [&errorOccurred]() { return errorOccurred.load(); },
        std::chrono::milliseconds( 500 ),
        "Validation did not reject duplicate hashes within timeout",
        &elapsed );

    // Should have triggered validation error
    EXPECT_TRUE( errorOccurred.load() );
}
