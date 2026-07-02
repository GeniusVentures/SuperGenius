/**
 * @file       task_queue_test.cpp
 * @brief      Tests for TaskQueueImpl CRDT operations and GetMyTaskIds pagination.
 * @date       2026-06-26
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "processing/impl/TaskQueueImpl.hpp"
#include "processing/impl/TaskKeys.hpp"
#include "processing/proto/SGProcessing.pb.h"
#include "testutil/storage/base_crdt_test.hpp"

using namespace sgns::processing;
using namespace sgns;

namespace
{
    /// Standalone pagination helper — mirrors GeniusNode::GetMyTaskIds logic.
    /// Returns up to @p limit entries from the end of @p ids, skipping @p offset
    /// entries from the end. Result is ordered oldest first within the window.
    std::vector<std::string> PaginateFromEnd( const std::vector<std::string> &ids,
                                              size_t                         limit,
                                              size_t                         offset )
    {
        if ( limit == 0 || ids.empty() )
        {
            return {};
        }

        const size_t total     = ids.size();
        const size_t start     = ( offset >= total ) ? 0 : ( total - offset );
        const size_t available = ( start >= limit ) ? ( start - limit ) : 0;
        const size_t count     = start - available;

        std::vector<std::string> result;
        result.reserve( count );
        for ( size_t i = available; i < start; ++i )
        {
            result.push_back( ids[i] );
        }
        return result;
    }

    /// Helper to build a Task proto with minimal fields.
    SGProcessing::Task MakeTask( const std::string &taskId, const std::string &escrowPath = "" )
    {
        SGProcessing::Task task;
        task.set_ipfs_block_id( taskId );
        task.set_json_data( R"({"name":"test","gnus_spec_version":1,"inputs":[],"outputs":[],"passes":[],"version":"1.0"})" );
        task.set_random_seed( 0.0f );
        task.set_results_channel( "test_channel" );
        if ( !escrowPath.empty() )
        {
            task.set_escrow_path( escrowPath );
        }
        return task;
    }

    /// Helper to build a SubTask proto with minimal fields.
    SGProcessing::SubTask MakeSubTask( const std::string &taskId,
                                       const std::string &subTaskId,
                                       const std::string &jsonData = R"({"source":"input:test_input"})" )
    {
        SGProcessing::SubTask sub;
        sub.set_ipfsblock( taskId );
        sub.set_subtaskid( subTaskId );
        sub.set_json_data( jsonData );
        return sub;
    }

    /// Helper to build a SubTaskResult with an output location.
    SGProcessing::SubTaskResult MakeSubTaskResult( const std::string &subTaskId,
                                                   const std::string &outputLocation )
    {
        SGProcessing::SubTaskResult r;
        r.set_subtaskid( subTaskId );
        r.set_result_hash( "deadbeef" );
        r.set_ipfs_results_data_id( outputLocation );
        r.set_node_address( "test_node" );
        return r;
    }
} // namespace

// ---------------------------------------------------------------------------
// PaginateFromEnd — unit tests for the pagination algorithm
// ---------------------------------------------------------------------------

class PaginateFromEndTest : public ::testing::Test
{
protected:
    std::vector<std::string> ids;

    void SetUp() override
    {
        ids.clear();
        for ( int i = 0; i < 10; ++i )
        {
            ids.push_back( "task_" + std::to_string( i ) );
        }
    }
};

TEST_F( PaginateFromEndTest, DefaultLimit50ReturnsAllWhenFewer )
{
    auto result = PaginateFromEnd( ids, 50, 0 );
    ASSERT_EQ( result.size(), 10u );
    EXPECT_EQ( result.front(), "task_0" );
    EXPECT_EQ( result.back(), "task_9" );
}

TEST_F( PaginateFromEndTest, LimitSmallerThanTotal )
{
    auto result = PaginateFromEnd( ids, 3, 0 );
    ASSERT_EQ( result.size(), 3u );
    EXPECT_EQ( result[0], "task_7" );
    EXPECT_EQ( result[1], "task_8" );
    EXPECT_EQ( result[2], "task_9" );
}

TEST_F( PaginateFromEndTest, OffsetSkipsNewest )
{
    auto result = PaginateFromEnd( ids, 3, 2 );
    ASSERT_EQ( result.size(), 3u );
    EXPECT_EQ( result[0], "task_5" );
    EXPECT_EQ( result[1], "task_6" );
    EXPECT_EQ( result[2], "task_7" );
}

TEST_F( PaginateFromEndTest, LimitEqualsTotal )
{
    auto result = PaginateFromEnd( ids, 10, 0 );
    ASSERT_EQ( result.size(), 10u );
    EXPECT_EQ( result.front(), "task_0" );
}

TEST_F( PaginateFromEndTest, OffsetEqualsTotalReturnsEmpty )
{
    auto result = PaginateFromEnd( ids, 5, 10 );
    EXPECT_TRUE( result.empty() );
}

TEST_F( PaginateFromEndTest, OffsetExceedsTotalReturnsEmpty )
{
    auto result = PaginateFromEnd( ids, 5, 100 );
    EXPECT_TRUE( result.empty() );
}

TEST_F( PaginateFromEndTest, LimitZeroReturnsEmpty )
{
    auto result = PaginateFromEnd( ids, 0, 0 );
    EXPECT_TRUE( result.empty() );
}

TEST_F( PaginateFromEndTest, EmptyInput )
{
    std::vector<std::string> empty;
    auto                     result = PaginateFromEnd( empty, 10, 0 );
    EXPECT_TRUE( result.empty() );
}

TEST_F( PaginateFromEndTest, LimitExceedsTotal )
{
    auto result = PaginateFromEnd( ids, 100, 0 );
    ASSERT_EQ( result.size(), 10u );
}

TEST_F( PaginateFromEndTest, SingleElement )
{
    std::vector<std::string> single = { "only" };
    auto                     result = PaginateFromEnd( single, 1, 0 );
    ASSERT_EQ( result.size(), 1u );
    EXPECT_EQ( result[0], "only" );
}

// ---------------------------------------------------------------------------
// TaskQueueImpl CRDT integration tests
// ---------------------------------------------------------------------------

class TaskQueueImplTest : public test::CRDTFixture
{
public:
    TaskQueueImplTest() : CRDTFixture( fs::path( "task_queue_test" ) )
    {
        topic_ = "test_processing_topic";

        // Add the topic to the DB so EnqueueTask/CompleteTask can use it
        db_->AddListenTopic( topic_ );

        queue_ = TaskQueueImpl::New( db_, topic_ );
        if ( !queue_ )
        {
            throw std::runtime_error( "TaskQueueImpl::New returned nullptr" );
        }
    }

    ~TaskQueueImplTest() override = default;

    std::shared_ptr<TaskQueueImpl> queue_;
    std::string                    topic_;
};

TEST_F( TaskQueueImplTest, ListTaskKeysEmptyOnNewDB )
{
    auto keys = queue_->ListTaskKeys();
    EXPECT_TRUE( keys.empty() );
}

TEST_F( TaskQueueImplTest, EnqueueAndListTaskKeys )
{
    auto        task    = MakeTask( "task_alpha" );
    std::string subJson = R"({"source":"input:test_input"})";
    auto        sub     = MakeSubTask( "task_alpha", "sub_1", subJson );
    std::list<SGProcessing::SubTask> subs = { sub };

    auto result = queue_->EnqueueTask( task, subs );
    ASSERT_TRUE( result.has_value() );

    auto keys = queue_->ListTaskKeys();
    ASSERT_EQ( keys.size(), 1u );
    EXPECT_EQ( keys[0], "task_alpha" );
}

TEST_F( TaskQueueImplTest, EnqueueMultipleAndListAll )
{
    auto t1 = MakeTask( "task_1" );
    auto t2 = MakeTask( "task_2" );
    auto t3 = MakeTask( "task_3" );

    std::string subJson = R"({"source":"input:test_input"})";
    auto        s1      = MakeSubTask( "task_1", "sub_1", subJson );
    auto        s2      = MakeSubTask( "task_2", "sub_2", subJson );
    auto        s3      = MakeSubTask( "task_3", "sub_3", subJson );

    ASSERT_TRUE( queue_->EnqueueTask( t1, { s1 } ).has_value() );
    ASSERT_TRUE( queue_->EnqueueTask( t2, { s2 } ).has_value() );
    ASSERT_TRUE( queue_->EnqueueTask( t3, { s3 } ).has_value() );

    auto keys = queue_->ListTaskKeys();
    ASSERT_EQ( keys.size(), 3u );

    // Verify all three IDs are present (order not guaranteed)
    std::set<std::string> keySet( keys.begin(), keys.end() );
    EXPECT_TRUE( keySet.count( "task_1" ) );
    EXPECT_TRUE( keySet.count( "task_2" ) );
    EXPECT_TRUE( keySet.count( "task_3" ) );
}

TEST_F( TaskQueueImplTest, IsTaskCompletedFalseBeforeCompletion )
{
    auto        task = MakeTask( "task_incomplete" );
    std::string subJson = R"({"source":"input:test_input"})";
    auto        sub  = MakeSubTask( "task_incomplete", "sub_x", subJson );

    ASSERT_TRUE( queue_->EnqueueTask( task, { sub } ).has_value() );
    EXPECT_FALSE( queue_->IsTaskCompleted( "task_incomplete" ) );
}

TEST_F( TaskQueueImplTest, CompleteTaskAndGetResult )
{
    const std::string taskId   = "task_result_test";
    const std::string subId    = "sub_result_1";
    const std::string location = "ipfs://QmTestCid12345";

    auto task = MakeTask( taskId );
    std::string subJson = R"({"source":"input:test_input"})";
    auto sub  = MakeSubTask( taskId, subId, subJson );
    ASSERT_TRUE( queue_->EnqueueTask( task, { sub } ).has_value() );

    // Build result with ipfs_results_data_id populated
    SGProcessing::TaskResult taskResult;
    auto                    *subResult = taskResult.add_subtask_results();
    *subResult = MakeSubTaskResult( subId, location );

    auto txn = queue_->CompleteTask( taskId, taskResult );
    ASSERT_TRUE( txn.has_value() );
    ASSERT_TRUE( txn.value()->Commit({ topic_ }).has_value() );

    // Verify task is marked completed
    EXPECT_TRUE( queue_->IsTaskCompleted( taskId ) );

    // Retrieve the result and verify ipfs_results_data_id
    auto retrieved = queue_->GetTaskResult( taskId );
    ASSERT_TRUE( retrieved.has_value() );
    ASSERT_EQ( retrieved.value().subtask_results_size(), 1 );
    EXPECT_EQ( retrieved.value().subtask_results( 0 ).subtaskid(), subId );
    EXPECT_EQ( retrieved.value().subtask_results( 0 ).ipfs_results_data_id(), location );
}

TEST_F( TaskQueueImplTest, GetTaskResultFailsForIncompleteTask )
{
    auto result = queue_->GetTaskResult( "nonexistent_task" );
    EXPECT_FALSE( result.has_value() );
}

TEST_F( TaskQueueImplTest, MultipleSubTaskResultsWithLocations )
{
    const std::string taskId    = "task_multi_result";
    const std::string subId1    = "sub_a";
    const std::string subId2    = "sub_b";
    const std::string location1 = "file://uuid-dir-aaa/";
    const std::string location2 = "ipfs://QmMultiCidBBB";

    auto task = MakeTask( taskId );
    std::string subJson = R"({"source":"input:test_input"})";
    auto sub1 = MakeSubTask( taskId, subId1, subJson );
    auto sub2 = MakeSubTask( taskId, subId2, subJson );
    ASSERT_TRUE( queue_->EnqueueTask( task, { sub1, sub2 } ).has_value() );

    SGProcessing::TaskResult taskResult;
    auto                    *sr1 = taskResult.add_subtask_results();
    *sr1 = MakeSubTaskResult( subId1, location1 );
    auto *sr2 = taskResult.add_subtask_results();
    *sr2 = MakeSubTaskResult( subId2, location2 );

    auto txn = queue_->CompleteTask( taskId, taskResult );
    ASSERT_TRUE( txn.has_value() );
    ASSERT_TRUE( txn.value()->Commit({ topic_ }).has_value() );

    auto retrieved = queue_->GetTaskResult( taskId );
    ASSERT_TRUE( retrieved.has_value() );
    ASSERT_EQ( retrieved.value().subtask_results_size(), 2 );

    // Collect locations into a set (order not guaranteed)
    std::set<std::string> locs;
    for ( const auto &sr : retrieved.value().subtask_results() )
    {
        locs.insert( sr.ipfs_results_data_id() );
    }
    EXPECT_TRUE( locs.count( location1 ) );
    EXPECT_TRUE( locs.count( location2 ) );
}

TEST_F( TaskQueueImplTest, ListTaskKeysSkipsEmptyEntries )
{
    // Enqueue one valid task
    auto task = MakeTask( "task_valid" );
    std::string subJson = R"({"source":"input:test_input"})";
    auto sub  = MakeSubTask( "task_valid", "sub_v", subJson );
    ASSERT_TRUE( queue_->EnqueueTask( task, { sub } ).has_value() );

    auto keys = queue_->ListTaskKeys();
    ASSERT_EQ( keys.size(), 1u );
    EXPECT_EQ( keys[0], "task_valid" );
}
