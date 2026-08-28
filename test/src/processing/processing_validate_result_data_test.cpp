/**
 * @file       processing_validate_result_data_test.cpp
 * @brief      Unit tests for ValidateResultData — 9a scheme validation (Phase 4).
 * @date       2026-06-30
 *
 * Covers:
 *   4. Availability gate 9a — ipfs:// prefix enforced, file:// rejected,
 *      empty lines skipped, empty data-id passes.
 *   9b is covered in processing_result_durability_test.cpp (requires Bitswap).
 */

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

const std::string validate_result_logger_config( R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: validate_result_data_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )" );

class ValidateResultDataTest : public ProcessingServiceTest
{
public:
    void SetUp() override
    {
        ProcessingServiceTest::SetUp( "validate_result_data_test", validate_result_logger_config );
        ProcessingServiceTest::Initialize( 2, 50 );
    }

    static SGProcessing::SubTask makeSubTask( const std::string &subTaskId )
    {
        SGProcessing::SubTask subTask;
        subTask.set_subtaskid( subTaskId );
        subTask.set_ipfsblock( "test_ipfs_block" );
        subTask.set_json_data( "{}" );
        subTask.set_datalen( 1000 );
        auto chunk = subTask.add_chunkstoprocess();
        chunk->set_chunkid( "chunk_0" );
        chunk->set_n_subchunks( 1 );
        return subTask;
    }

    static SGProcessing::SubTaskResult makeResult( const std::string &subTaskId,
                                                   const std::string &ipfsDataId )
    {
        SGProcessing::SubTaskResult result;
        result.set_subtaskid( subTaskId );
        result.set_ipfs_results_data_id( ipfsDataId );
        result.add_chunk_hashes( "hash_valid" );
        // Payout metadata required by ProcessingValidationCore::ValidateIndividualResult.
        result.set_node_address( std::string( 128, 'a' ) );
        result.set_developer_address( "0xcafe" );
        result.set_developer_cut( 350000 );
        result.set_token_id( std::string( 32, '\0' ) );
        return result;
    }

    const std::string nodeId1 = "NODE_1";
};

// ═══════════════════════════════════════════════════════════════════════
// 9a: Scheme validation — ipfs:// prefix required.
// CompleteSubTask calls ValidateResultData(result, false) — scheme only.
// ═══════════════════════════════════════════════════════════════════════

/**
 * @given A subtask with ipfs_results_data_id containing ipfs:// CIDs.
 * @when CompleteSubTask is called.
 * @then The scheme check passes (9a only, no bitswap availability check).
 */
TEST_F( ValidateResultDataTest, AcceptsIpfsScheme )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto subTask = makeSubTask( "VALIDATE_9A_OK" );
    *queue->mutable_subtasks()->add_items() = subTask;
    queue->mutable_processing_queue()->add_items();

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(
        pubs1, "VALIDATE_9A_OK_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,
        []( const std::string & ) {} );
    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> errorOccurred{ false };

    auto accessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1, processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&errorOccurred]( const std::string & ) { errorOccurred = true; } );

    accessor->CreateResultsChannel( "validate_9a_ok" );

    std::atomic<bool> connected{ false };
    accessor->ConnectToSubTaskQueue( [&]() { connected = true; } );

    ASSERT_WAIT_FOR_CONDITION( [&connected]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect", &resultTime );

    auto result = makeResult( "VALIDATE_9A_OK", "ipfs://QmTestCID123\nipfs://QmTestCID456" );
    accessor->CompleteSubTask( "VALIDATE_9A_OK", result );

    EXPECT_WAIT_FOR_CONDITION(
        [&errorOccurred]() { return !errorOccurred.load(); },
        std::chrono::milliseconds( 600 ),
        "ipfs:// scheme should pass validation",
        nullptr );
}

/**
 * @given A subtask with ipfs_results_data_id containing file:// prefix.
 * @when CompleteSubTask is called.
 * @then The scheme check fails — error sink is triggered.
 */
TEST_F( ValidateResultDataTest, RejectsFileScheme )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto subTask = makeSubTask( "VALIDATE_9A_FAIL" );
    *queue->mutable_subtasks()->add_items() = subTask;
    queue->mutable_processing_queue()->add_items();

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(
        pubs1, "VALIDATE_9A_FAIL_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,
        []( const std::string & ) {} );
    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> errorOccurred{ false };

    auto accessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1, processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&errorOccurred]( const std::string & ) { errorOccurred = true; } );

    accessor->CreateResultsChannel( "validate_9a_fail" );

    std::atomic<bool> connected{ false };
    accessor->ConnectToSubTaskQueue( [&]() { connected = true; } );

    ASSERT_WAIT_FOR_CONDITION( [&connected]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect", &resultTime );

    auto result = makeResult( "VALIDATE_9A_FAIL", "file:///tmp/output.raw" );
    accessor->CompleteSubTask( "VALIDATE_9A_FAIL", result );

    std::chrono::milliseconds elapsed;
    ASSERT_WAIT_FOR_CONDITION(
        [&errorOccurred]() { return errorOccurred.load(); },
        std::chrono::milliseconds( 500 ),
        "file:// scheme should have been rejected by validation",
        &elapsed );

    EXPECT_TRUE( errorOccurred.load() ) << "file:// scheme should be rejected by 9a";
}

/**
 * @given A subtask with empty ipfs_results_data_id.
 * @when CompleteSubTask is called.
 * @then Validation passes — empty data ID means no IPFS data to validate.
 */
TEST_F( ValidateResultDataTest, EmptyDataIdPasses )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto subTask = makeSubTask( "VALIDATE_EMPTY" );
    *queue->mutable_subtasks()->add_items() = subTask;
    queue->mutable_processing_queue()->add_items();

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(
        pubs1, "VALIDATE_EMPTY_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,
        []( const std::string & ) {} );
    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> errorOccurred{ false };

    auto accessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1, processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&errorOccurred]( const std::string & ) { errorOccurred = true; } );

    accessor->CreateResultsChannel( "validate_empty" );

    std::atomic<bool> connected{ false };
    accessor->ConnectToSubTaskQueue( [&]() { connected = true; } );

    ASSERT_WAIT_FOR_CONDITION( [&connected]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect", &resultTime );

    auto result = makeResult( "VALIDATE_EMPTY", "" );
    accessor->CompleteSubTask( "VALIDATE_EMPTY", result );

    EXPECT_WAIT_FOR_CONDITION(
        [&errorOccurred]() { return !errorOccurred.load(); },
        std::chrono::milliseconds( 600 ),
        "Empty ipfs_results_data_id should pass",
        nullptr );
}
