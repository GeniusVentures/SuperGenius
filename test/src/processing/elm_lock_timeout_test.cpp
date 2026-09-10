/**
 * @file elm_lock_timeout_test.cpp
 * @brief FUND-02 lock-timeout wiring tests: derived timeout reaches the
 *        published queue proto, SC-3 no-re-grab under a valid long lock,
 *        expiry at the boundary, and the 15s default parity (SC-5).
 *
 * Verification runs one case per process (isolated --gtest_filter
 * invocations) per machine guidance on libp2p teardown.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <list>
#include <memory>
#include <vector>

#include <boost/asio/io_context.hpp>

#include "processing/processing_clocks_elm.hpp"
#include "processing/processing_subtask_queue.hpp"
#include "processing/processing_subtask_queue_manager.hpp"
#include "processing_mock.hpp"

using namespace sgns::processing;

namespace
{
    // Subtask list where every subtask carries >= 1 chunk (CreateQueue
    // requires this; mirrors processing_subtask_queue_manager_test).
    std::list<SGProcessing::SubTask> BuildTwoSubTasks()
    {
        SGProcessing::ProcessingChunk chunk;
        chunk.set_chunkid( "CHUNK_1" );
        chunk.set_n_subchunks( 1 );

        std::list<SGProcessing::SubTask> subTasks;
        for ( const char *id : { "SUBTASK_1", "SUBTASK_2" } )
        {
            SGProcessing::SubTask subtask;
            subtask.set_subtaskid( id );
            auto c = subtask.add_chunkstoprocess();
            c->CopyFrom( chunk );
            subTasks.push_back( std::move( subtask ) );
        }
        return subTasks;
    }
} // namespace

// ---------------------------------------------------------------------------
// DeriveElmClocks feeds the wiring (ties to plan 01-02, FUND-02)
// ---------------------------------------------------------------------------

TEST( ElmLockTimeout, DeriveFeedsWiring )
{
    // 1.0h default: deadline 3600000ms + 60s grace == 3660000ms.
    const auto clocks = sgns::processing::DeriveElmClocks( 1.0 );
    EXPECT_EQ( std::chrono::duration_cast<std::chrono::milliseconds>( clocks.lockTimeout ).count(), 3660000 );
}

// ---------------------------------------------------------------------------
// Queue-level SC-3: no re-grab under a valid long lock; expiry at boundary
// ---------------------------------------------------------------------------

TEST( ElmLockTimeout, QueueLevel_NoRegrabWithinDerivedLock )
{
    // Deterministic timestamps: no clock, no node harness.
    uint64_t now = 1000;
    ProcessingSubTaskQueue queue( "NODE_1", [ &now ]() { return now; } );

    SGProcessing::ProcessingQueue snapshot;
    snapshot.set_owner_node_id( "NODE_1" );
    snapshot.set_processing_timeout_length( 3660000 ); // derived (1.0h + grace)
    snapshot.add_items();
    snapshot.add_items();
    std::vector<int> enabled = { 0, 1 };
    queue.CreateQueue( &snapshot, enabled );

    size_t grabbed = 0;
    ASSERT_TRUE( queue.GrabItem( grabbed, now ) );

    // t + 60s: WITHIN the derived lock (deadline 1h + 60s grace from grab).
    // UnlockExpiredItems must return false and leave the lock in place.
    EXPECT_FALSE( queue.UnlockExpiredItems( now + 60000 ) );

    // The queue proto (not just locals) must still hold the lock: re-inspect
    // via the published snapshot semantics -- GetLastLockTimestamp > 0 and
    // the queue still owns the item.
    EXPECT_GT( queue.GetLastLockTimestamp(), 0 );
}

TEST( ElmLockTimeout, QueueLevel_ExpiresAtBoundary )
{
    uint64_t now = 1000;
    ProcessingSubTaskQueue queue( "NODE_1", [ &now ]() { return now; } );

    SGProcessing::ProcessingQueue snapshot;
    snapshot.set_owner_node_id( "NODE_1" );
    snapshot.set_processing_timeout_length( 3660000 );
    snapshot.add_items();
    snapshot.add_items();
    std::vector<int> enabled = { 0, 1 };
    queue.CreateQueue( &snapshot, enabled );

    size_t grabbed = 0;
    ASSERT_TRUE( queue.GrabItem( grabbed, now ) );

    // Strict `now > expiration` comparison: t + 3660000 exactly is NOT
    // expired; t + 3660001 IS. Pin both.
    EXPECT_FALSE( queue.UnlockExpiredItems( now + 3660000 ) );
    EXPECT_TRUE( queue.UnlockExpiredItems( now + 3660001 ) );
    // After expiry the lock is cleared: no lock timestamp remains.
    EXPECT_EQ( queue.GetLastLockTimestamp(), 0 );
}

// ---------------------------------------------------------------------------
// Queue-manager level: derived timeout reaches the PUBLISHED queue proto
// (processing_timeout_length baked in at CreateQueue, the :76 write)
// ---------------------------------------------------------------------------

TEST( ElmLockTimeout, QueueManager_DerivedTimeoutInPublishedProto )
{
    auto context = std::make_shared<boost::asio::io_context>();

    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto channel = std::make_shared<sgns::test::ProcessingSubTaskQueueChannelImpl>();
    channel->queueOwnershipRequestSink = []( const std::string & ) {};
    channel->queuePublishingSink      = [ &queueSnapshotSet ]( std::shared_ptr<SGProcessing::SubTaskQueue> q ) {
        queueSnapshotSet.push_back( *q );
    };

    auto subTasks = BuildTwoSubTasks();

    ProcessingSubTaskQueueManager queueManager( channel, context, "NODE_1", []( const std::string & ) {} );
    // FUND-02 wiring: the derived timeout set BEFORE CreateQueue reaches the
    // published proto. Note the proto field is in NANOseconds
    // (std::chrono::system_clock::duration::count), so 3660000ms -> 3.66e10.
    queueManager.SetProcessingTimeout( std::chrono::milliseconds( 3660000 ) );
    ASSERT_TRUE( queueManager.CreateQueue( subTasks ) );

    ASSERT_EQ( queueSnapshotSet.size(), 1 );
    // The queue proto (not just the local member) holds the derived value.
    EXPECT_EQ( queueSnapshotSet[0].processing_queue().processing_timeout_length(),
               std::chrono::system_clock::duration( std::chrono::milliseconds( 3660000 ) ).count() );
    context->stop();
}

// ---------------------------------------------------------------------------
// SC-5 default parity: no SetProcessingTimeout call -> 15s default stands
// ---------------------------------------------------------------------------

TEST( ElmLockTimeout, QueueManager_Default15sUntouched )
{
    auto context = std::make_shared<boost::asio::io_context>();

    std::vector<SGProcessing::SubTaskQueue> queueSnapshotSet;
    auto channel = std::make_shared<sgns::test::ProcessingSubTaskQueueChannelImpl>();
    channel->queueOwnershipRequestSink = []( const std::string & ) {};
    channel->queuePublishingSink      = [ &queueSnapshotSet ]( std::shared_ptr<SGProcessing::SubTaskQueue> q ) {
        queueSnapshotSet.push_back( *q );
    };

    auto subTasks = BuildTwoSubTasks();

    ProcessingSubTaskQueueManager queueManager( channel, context, "NODE_1", []( const std::string & ) {} );
    // NO SetProcessingTimeout call: the 15s constructor default must stand
    // (proto field is nanoseconds: 15000ms -> 1.5e10).
    ASSERT_TRUE( queueManager.CreateQueue( subTasks ) );

    ASSERT_EQ( queueSnapshotSet.size(), 1 );
    EXPECT_EQ( queueSnapshotSet[0].processing_queue().processing_timeout_length(),
               std::chrono::system_clock::duration( std::chrono::milliseconds( 15000 ) ).count() );
    context->stop();
}
