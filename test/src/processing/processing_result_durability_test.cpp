/**
 * @file       processing_result_durability_test.cpp
 * @brief      Integration tests for IPFS result mirroring and availability gating (Phases 3-4).
 * @date       2026-06-30
 *
 * Covers verification items:
 *   3. Mirroring — full node replicates result blocks from pubsub.
 *   4. Availability gate 9a — scheme validation (ipfs:// prefix).
 *   5. Availability gate 9b — data fetch availability via HasBlock().
 */

#include "processing_service_test.hpp"

#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>
#include <libp2p/event/bus.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>

#include <boost/functional/hash.hpp>
#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>

#include <bitswap.hpp>

#include "testutil/wait_condition.hpp"
#include "base/logger.hpp"
#include "base/sgns_version.hpp"

using namespace sgns::processing;
using namespace sgns::test;
using namespace sgns::base;
namespace fs = std::filesystem;

const std::string durability_logger_config( R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: result_durability_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )" );

/**
 * @brief Test fixture with Bitswap wired into the processing accessors.
 *
 * Extends ProcessingServiceTest to create a real Bitswap instance from a pubsub
 * node, enabling end-to-end tests of mirror callbacks and availability gates.
 */
class ResultDurabilityTest : public ProcessingServiceTest
{
public:
    void SetUp() override
    {
        ProcessingServiceTest::SetUp( "result_durability_test", durability_logger_config );
        ProcessingServiceTest::Initialize( 2, 50 );

        // Create a real Bitswap from node 0's host for availability checks.
        auto host = m_pubsub_nodes[0]->GetHost();
        ASSERT_NE( host, nullptr );

        bitswap_event_bus_ = std::make_shared<libp2p::event::Bus>();
        bitswap_ = std::make_shared<sgns::ipfs_bitswap::Bitswap>(
            *host, *bitswap_event_bus_, m_pubsub_nodes[0]->GetAsioContext() );

        // Use a temp directory as cache dir so tests are isolated.
        temp_cache_dir_ = fs::temp_directory_path() / "sgns_test_durability";
        fs::create_directories( temp_cache_dir_ );
        bitswap_->setCacheDir( temp_cache_dir_.string() );
        bitswap_->initialize();

        // Wire bitswap to both accessors for availability checks.
        m_processing_queues_accessors[0]->setBitswap( bitswap_ );
        m_processing_queues_accessors[1]->setBitswap( bitswap_ );
    }

    void TearDown() override
    {
        // Reset bitswap before tearing down pubsub (bitswap holds host refs).
        if ( bitswap_ )
        {
            bitswap_.reset();
        }
        if ( bitswap_event_bus_ )
        {
            bitswap_event_bus_.reset();
        }

        // Clean up temp cache.
        std::error_code ec;
        fs::remove_all( temp_cache_dir_, ec );

        ProcessingServiceTest::TearDown();
    }

    /**
     * @brief Helper: publish a single block and return its CID string.
     */
    std::string publishTestBlock( const std::vector<uint8_t> &data )
    {
        std::promise<libp2p::multi::ContentIdentifier> cid_promise;
        auto cid_future = cid_promise.get_future();

        bitswap_->PublishData(
            data,
            [&cid_promise]( libp2p::outcome::result<libp2p::multi::ContentIdentifier> result )
            {
                if ( result )
                {
                    cid_promise.set_value( result.value() );
                }
            } );

        auto cid = cid_future.get();
        return libp2p::multi::ContentIdentifierCodec::toString( cid ).value();
    }

    /**
     * @brief Helper: create a SubTaskResult with the given ipfs_results_data_id.
     */
    static SGProcessing::SubTaskResult makeResult( const std::string       &subTaskId,
                                                   const std::string       &ipfsDataId,
                                                   const std::string       &nodeAddress = "test_node" )
    {
        SGProcessing::SubTaskResult result;
        result.set_subtaskid( subTaskId );
        result.set_node_address( nodeAddress );
        result.set_ipfs_results_data_id( ipfsDataId );
        // Add a minimal valid chunk hash so basic validation passes.
        std::string hash = "hash_" + subTaskId;
        result.add_chunk_hashes( hash );
        return result;
    }

    const std::string nodeId1 = "NODE_1";
    const std::string nodeId2 = "NODE_2";

    std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap_;
    std::shared_ptr<libp2p::event::Bus>          bitswap_event_bus_;
    fs::path                                     temp_cache_dir_;
};

// ═══════════════════════════════════════════════════════════════════════
// Verification 3: Mirroring — mirror callback is invoked on pubsub result.
// ═══════════════════════════════════════════════════════════════════════

/**
 * @given Two pubsub nodes with a full node (accessor 0) configured to mirror.
 * @when A worker (accessor 1) publishes a result with ipfs_results_data_id.
 * @then The mirror callback on accessor 0 is invoked with the data ID.
 */
TEST_F( ResultDurabilityTest, MirrorCallbackInvokedOnPubsubResult )
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    std::chrono::milliseconds resultTime;

    // Set up mirror callback on accessor 0 (the "full node").
    std::atomic<bool> mirror_called{ false };
    std::string       mirrored_data_id;
    m_processing_queues_accessors[0]->setMirrorResultCallback(
        [&]( const std::string &dataId )
        {
            mirror_called.store( true );
            mirrored_data_id = dataId;
        } );

    // Create queue owned by NODE_2 so NODE_1 doesn't try to finalize.
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId2 );

    auto  subTask = queue->mutable_subtasks()->add_items();
    subTask->set_subtaskid( "MIRROR_TEST" );
    auto chunk = subTask->add_chunkstoprocess();
    chunk->set_chunkid( "CHUNK_1" );
    chunk->set_n_subchunks( 1 );

    auto item = queue->mutable_processing_queue()->add_items();

    // Feed the queue to both managers.
    for ( size_t i = 0; i < 2; ++i )
    {
        auto qcopy = std::make_unique<SGProcessing::SubTaskQueue>( *queue );
        m_processing_queues_managers[i]->ProcessSubTaskQueueMessage( qcopy.release() );
    }

    // Connect accessors.
    std::atomic<bool> connected0{ false };
    std::atomic<bool> connected1{ false };
    m_processing_queues_accessors[0]->ConnectToSubTaskQueue( [&]() { connected0 = true; } );
    m_processing_queues_accessors[1]->ConnectToSubTaskQueue( [&]() { connected1 = true; } );

    ASSERT_WAIT_FOR_CONDITION( [&]() { return connected0.load() && connected1.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect accessors",
                               &resultTime );

    // Publish a result from accessor 1 with an ipfs_results_data_id.
    SGProcessing::SubTaskResult result = makeResult(
        "MIRROR_TEST", "ipfs://QmTest123Mirror\nipfs://QmTest456Mirror" );

    // We need to get the result onto the pubsub channel.  CompleteSubTask on
    // accessor 1 will publish to the results channel, which accessor 0
    // receives via OnResultChannelMessage → mirror callback.
    m_processing_queues_accessors[1]->CompleteSubTask( "MIRROR_TEST", result );

    // Allow pubsub propagation.
    ASSERT_WAIT_FOR_CONDITION( [&]() { return mirror_called.load(); },
                               std::chrono::milliseconds( 10000 ),
                               "Mirror callback was not invoked",
                               &resultTime );

    EXPECT_TRUE( mirror_called.load() );
    EXPECT_FALSE( mirrored_data_id.empty() );
}

// ═══════════════════════════════════════════════════════════════════════
// Verification 4: Availability gate 9a — scheme validation.
// ═══════════════════════════════════════════════════════════════════════

/**
 * @given A subtask with ipfs:// output scheme.
 * @when CompleteSubTask is called with file:// prefix in ipfs_results_data_id.
 * @then The result is rejected (error sink triggered).
 */
TEST_F( ResultDurabilityTest, SchemeValidation_RejectsNonIpfsPrefix )
{
    auto pubs1 = m_pubsub_nodes[0];

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto  subTask = queue->mutable_subtasks()->add_items();
    subTask->set_subtaskid( "SCHEME_TEST" );
    auto chunk = subTask->add_chunkstoprocess();
    chunk->set_chunkid( "CHUNK_1" );
    chunk->set_n_subchunks( 1 );

    auto item = queue->mutable_processing_queue()->add_items();

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "SCHEME_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,
        []( const std::string & ) {} );
    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> error_occurred{ false };
    std::string       error_message;

    auto accessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&]( const std::string &err )
        {
            error_occurred = true;
            error_message  = err;
        } );

    accessor->CreateResultsChannel( "scheme_test" );

    std::atomic<bool> connected{ false };
    accessor->ConnectToSubTaskQueue( [&]() { connected = true; } );

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION( [&]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect",
                               &resultTime );

    // 9a: file:// prefix — should be rejected.
    auto badResult = makeResult( "SCHEME_TEST", "file:///tmp/out.raw" );
    accessor->CompleteSubTask( "SCHEME_TEST", badResult );

    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    EXPECT_TRUE( error_occurred.load() ) << "Scheme validation should reject file:// prefix";
}

/**
 * @given A subtask with ipfs:// output scheme.
 * @when CompleteSubTask is called with properly formatted ipfs:// CIDs.
 * @then The result is accepted (no error).
 */
TEST_F( ResultDurabilityTest, SchemeValidation_AcceptsValidIpfsPrefix )
{
    auto pubs1 = m_pubsub_nodes[0];

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto  subTask = queue->mutable_subtasks()->add_items();
    subTask->set_subtaskid( "SCHEME_OK" );
    auto chunk = subTask->add_chunkstoprocess();
    chunk->set_chunkid( "CHUNK_1" );
    chunk->set_n_subchunks( 1 );

    auto item = queue->mutable_processing_queue()->add_items();

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "SCHEME_OK_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,
        []( const std::string & ) {} );
    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> error_occurred{ false };

    auto accessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&]( const std::string & ) { error_occurred = true; } );

    accessor->CreateResultsChannel( "scheme_ok_test" );

    std::atomic<bool> connected{ false };
    accessor->ConnectToSubTaskQueue( [&]() { connected = true; } );

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION( [&]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect",
                               &resultTime );

    // Valid: ipfs:// prefix with multi-line CIDs.
    auto goodResult = makeResult( "SCHEME_OK",
                                  "ipfs://QmValidCID1\nipfs://QmValidCID2" );
    accessor->CompleteSubTask( "SCHEME_OK", goodResult );

    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    EXPECT_FALSE( error_occurred.load() ) << "Valid ipfs:// scheme should be accepted (9a only, no bitswap check)";
}

/**
 * @given A result with empty ipfs_results_data_id.
 * @when CompleteSubTask is called.
 * @then The result passes validation (no IPFS data to validate).
 */
TEST_F( ResultDurabilityTest, SchemeValidation_EmptyDataIdPasses )
{
    auto pubs1 = m_pubsub_nodes[0];

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto  subTask = queue->mutable_subtasks()->add_items();
    subTask->set_subtaskid( "EMPTY_ID" );
    auto chunk = subTask->add_chunkstoprocess();
    chunk->set_chunkid( "CHUNK_1" );
    chunk->set_n_subchunks( 1 );

    auto item = queue->mutable_processing_queue()->add_items();

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "EMPTY_ID_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,
        []( const std::string & ) {} );
    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> error_occurred{ false };

    auto accessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&]( const std::string & ) { error_occurred = true; } );

    accessor->CreateResultsChannel( "empty_id_test" );

    std::atomic<bool> connected{ false };
    accessor->ConnectToSubTaskQueue( [&]() { connected = true; } );

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION( [&]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect",
                               &resultTime );

    // Empty ipfs_results_data_id — should pass (no IPFS data to validate).
    auto emptyResult = makeResult( "EMPTY_ID", "" );
    accessor->CompleteSubTask( "EMPTY_ID", emptyResult );

    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    EXPECT_FALSE( error_occurred.load() ) << "Empty ipfs_results_data_id should pass validation";
}

// ═══════════════════════════════════════════════════════════════════════
// Verification 5: Availability gate 9b — data fetch via HasBlock().
// ═══════════════════════════════════════════════════════════════════════

/**
 * @given A bitswap instance with a known block persisted.
 * @when OnResultReceived processes a result referencing that block.
 * @then The result is accepted because HasBlock() returns true.
 *
 * Note: This tests 9b indirectly through the pubsub OnResultReceived path.
 *       We publish a block, then publish a result referencing it.
 */
TEST_F( ResultDurabilityTest, AvailabilityGate_AcceptsWhenDataAvailable )
{
    // Publish a test block to populate bitswap.
    std::vector<uint8_t> testData = { 'h', 'e', 'l', 'l', 'o' };
    std::string          cidStr   = publishTestBlock( testData );
    ASSERT_FALSE( cidStr.empty() );

    // Verify HasBlock returns true for this CID.
    auto cid = libp2p::multi::ContentIdentifierCodec::fromString( cidStr );
    ASSERT_TRUE( cid.has_value() );
    EXPECT_TRUE( bitswap_->HasBlock( cid.value() ) );

    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    // Set up accessor 0 as owner, accessor 1 as worker.
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto  subTask = queue->mutable_subtasks()->add_items();
    subTask->set_subtaskid( "AVAIL_OK" );
    auto chunk = subTask->add_chunkstoprocess();
    chunk->set_chunkid( "CHUNK_1" );
    chunk->set_n_subchunks( 1 );

    auto item = queue->mutable_processing_queue()->add_items();

    for ( size_t i = 0; i < 2; ++i )
    {
        auto qcopy = std::make_unique<SGProcessing::SubTaskQueue>( *queue );
        m_processing_queues_managers[i]->ProcessSubTaskQueueMessage( qcopy.release() );
    }

    std::atomic<bool> connected0{ false };
    std::atomic<bool> connected1{ false };
    m_processing_queues_accessors[0]->ConnectToSubTaskQueue( [&]() { connected0 = true; } );
    m_processing_queues_accessors[1]->ConnectToSubTaskQueue( [&]() { connected1 = true; } );

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION( [&]() { return connected0.load() && connected1.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect",
                               &resultTime );

    // Publish result referencing the available CID from worker (accessor 1).
    auto goodResult = makeResult( "AVAIL_OK", "ipfs://" + cidStr );
    m_processing_queues_accessors[1]->CompleteSubTask( "AVAIL_OK", goodResult );

    // Wait for pubsub to deliver to owner (accessor 0).
    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return m_processing_queues_accessors[0]->GetResults().size() > 0; },
        std::chrono::milliseconds( 10000 ),
        "Result with available CID was not accepted",
        &resultTime );

    EXPECT_EQ( m_processing_queues_accessors[0]->GetResults().size(), 1u );
}

/**
 * @given A bitswap instance without a specific block.
 * @when OnResultReceived processes a result referencing that unknown block.
 * @then The result is rejected (not accumulated).
 */
TEST_F( ResultDurabilityTest, AvailabilityGate_RejectsWhenDataUnavailable )
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    // Use a CID that does not exist in bitswap.
    const std::string unknownCid = "QmUnknownCIDDeadBeef1234567890";

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto  subTask = queue->mutable_subtasks()->add_items();
    subTask->set_subtaskid( "AVAIL_FAIL" );
    auto chunk = subTask->add_chunkstoprocess();
    chunk->set_chunkid( "CHUNK_1" );
    chunk->set_n_subchunks( 1 );

    auto item = queue->mutable_processing_queue()->add_items();

    for ( size_t i = 0; i < 2; ++i )
    {
        auto qcopy = std::make_unique<SGProcessing::SubTaskQueue>( *queue );
        m_processing_queues_managers[i]->ProcessSubTaskQueueMessage( qcopy.release() );
    }

    std::atomic<bool> connected0{ false };
    std::atomic<bool> connected1{ false };
    m_processing_queues_accessors[0]->ConnectToSubTaskQueue( [&]() { connected0 = true; } );
    m_processing_queues_accessors[1]->ConnectToSubTaskQueue( [&]() { connected1 = true; } );

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION( [&]() { return connected0.load() && connected1.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect",
                               &resultTime );

    // Publish a result referencing an unknown CID from worker.
    auto badResult = makeResult( "AVAIL_FAIL", "ipfs://" + unknownCid );
    m_processing_queues_accessors[1]->CompleteSubTask( "AVAIL_FAIL", badResult );

    // Allow time for pubsub propagation.  Result should NOT appear on owner.
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // The result should not have been accumulated because 9b rejects it.
    EXPECT_EQ( m_processing_queues_accessors[0]->GetResults().size(), 0u )
        << "Result with unavailable CID should have been rejected";
}

/**
 * @given A result with mixed valid/invalid lines (empty lines between CIDs).
 * @when CompleteSubTask is called.
 * @then Empty lines are skipped; only non-empty lines are validated.
 */
TEST_F( ResultDurabilityTest, SchemeValidation_SkipsEmptyLines )
{
    auto pubs1 = m_pubsub_nodes[0];

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );

    auto  subTask = queue->mutable_subtasks()->add_items();
    subTask->set_subtaskid( "EMPTY_LINES" );
    auto chunk = subTask->add_chunkstoprocess();
    chunk->set_chunkid( "CHUNK_1" );
    chunk->set_n_subchunks( 1 );

    auto item = queue->mutable_processing_queue()->add_items();

    auto queueChannel = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "EMPTY_LINES_QUEUE" );
    auto processingQueueManager = std::make_shared<ProcessingSubTaskQueueManager>(
        queueChannel, pubs1->GetAsioContext(), nodeId1,
        []( const std::string & ) {} );
    processingQueueManager->ProcessSubTaskQueueMessage( queue.release() );

    std::atomic<bool> error_occurred{ false };

    auto accessor = std::make_shared<SubTaskQueueAccessorImpl>(
        pubs1,
        processingQueueManager,
        std::make_shared<SubTaskResultStorageMock>(),
        []( const SGProcessing::TaskResult & ) {},
        [&]( const std::string & ) { error_occurred = true; } );

    accessor->CreateResultsChannel( "empty_lines_test" );

    std::atomic<bool> connected{ false };
    accessor->ConnectToSubTaskQueue( [&]() { connected = true; } );

    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION( [&]() { return connected.load(); },
                               std::chrono::milliseconds( 2000 ),
                               "Failed to connect",
                               &resultTime );

    // Lines with empty lines between valid CIDs.
    auto result = makeResult( "EMPTY_LINES",
                              "ipfs://QmAlpha\n\nipfs://QmBeta\n\n" );
    accessor->CompleteSubTask( "EMPTY_LINES", result );

    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    EXPECT_FALSE( error_occurred.load() ) << "Empty lines should be skipped, valid CIDs should pass";
}
