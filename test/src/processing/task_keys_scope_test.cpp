/**
 * @file       task_keys_scope_test.cpp
 * @brief      Job-scope key/topic/path helper tests (D-08): public byte-stability,
 *             private /chain/<id>/ branching, and data-path key placement over CRDT.
 * @date       2026-09-01
 */

#include <gtest/gtest.h>

#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "crdt/hierarchical_key.hpp"
#include "processing/impl/TaskKeys.hpp"
#include "processing/impl/TaskQueueImpl.hpp"
#include "processing/impl/processing_subtask_result_storage_impl.hpp"
#include "processing/proto/SGProcessing.pb.h"
#include "testutil/storage/base_crdt_test.hpp"

using namespace sgns::processing;
using namespace sgns;

namespace
{
    /// Public scope: empty private_network_id (scope 0).
    const std::string kPublicScope = "";
    /// A valid private-network identity (0x-hex-32B shape, 15-01 encoding).
    const std::string kPrivateIdA = "0xabcd0123abcd0123abcd0123abcd0123abcd0123abcd0123abcd0123abcd0123";
    /// A second, distinct private-network identity.
    const std::string kPrivateIdB = "0x1111222233334444555566667777888899990000aaaabbbbccccddddeeeeffff";
} // namespace

// ---------------------------------------------------------------------------
// Public byte-stability: zero-arg == empty-scope overload == golden literal
// ---------------------------------------------------------------------------

TEST( TaskKeysScope, PublicBuildersAreByteStableAcrossForms )
{
    // std::string arguments (a bare literal would be ambiguous between the
    // std::string_view public overload and the const std::string& scoped overload).
    const std::string t1 = "t1";
    const std::string s1 = "s1";

    EXPECT_EQ( TaskKeys::TaskListKey(), TaskKeys::TaskListKey( kPublicScope ) );
    EXPECT_EQ( TaskKeys::SubTaskListKey(),
               TaskKeys::ScopePrefix( kPublicScope ) + TaskKeys::SubTaskListKey() );
    EXPECT_EQ( TaskKeys::SubTaskListKey( t1 ), TaskKeys::SubTaskListKey( kPublicScope, t1 ) );
    EXPECT_EQ( TaskKeys::TaskKey( t1 ), TaskKeys::TaskKey( kPublicScope, t1 ) );
    EXPECT_EQ( TaskKeys::SubTaskKey( t1, s1 ), TaskKeys::SubTaskKey( kPublicScope, t1, s1 ) );
    EXPECT_EQ( TaskKeys::ClaimableListKey(), TaskKeys::ClaimableListKey( kPublicScope ) );
    EXPECT_EQ( TaskKeys::ClaimableTaskKey( t1 ), TaskKeys::ClaimableTaskKey( kPublicScope, t1 ) );
    EXPECT_EQ( TaskKeys::ResultTaskKey( t1 ), TaskKeys::ResultTaskKey( kPublicScope, t1 ) );

    // Golden public composition (existing CRDT data must stay reachable).
    const std::string prefix = TaskKeys::ProcessingPrefix();
    EXPECT_EQ( TaskKeys::TaskListKey(), prefix + "/tasks" );
    EXPECT_EQ( TaskKeys::TaskKey( t1 ), prefix + "/tasks/t1" );
    EXPECT_EQ( TaskKeys::SubTaskListKey( t1 ), prefix + "/subtasks/t1" );
    EXPECT_EQ( TaskKeys::SubTaskKey( t1, s1 ), prefix + "/subtasks/t1/s1" );
    EXPECT_EQ( TaskKeys::ClaimableListKey(), prefix + "/claimable" );
    EXPECT_EQ( TaskKeys::ClaimableTaskKey( t1 ), prefix + "/claimable/t1" );

    // No public key may leak into the private branch namespace.
    for ( const auto &key : { TaskKeys::TaskListKey( kPublicScope ),
                              TaskKeys::TaskKey( kPublicScope, t1 ),
                              TaskKeys::SubTaskListKey( kPublicScope, t1 ),
                              TaskKeys::SubTaskKey( kPublicScope, t1, s1 ),
                              TaskKeys::ClaimableListKey( kPublicScope ),
                              TaskKeys::ClaimableTaskKey( kPublicScope, t1 ),
                              TaskKeys::ResultTaskKey( kPublicScope, t1 ) } )
    {
        EXPECT_EQ( key.find( "/chain/" ), std::string::npos ) << "public key branched: " << key;
    }

    // Plan-pinned shape checks.
    EXPECT_NE( TaskKeys::TaskKey( t1 ).find( "/tasks/t1" ), std::string::npos );
}

TEST( TaskKeysScope, ScopePrefixIsChainBranch )
{
    EXPECT_TRUE( TaskKeys::ScopePrefix( kPublicScope ).empty() );
    EXPECT_EQ( TaskKeys::ScopePrefix( kPrivateIdA ), "/chain/" + kPrivateIdA );
}

TEST( TaskKeysScope, PrivateScopeBranchesEveryBuilder )
{
    const std::string prefix = TaskKeys::ProcessingPrefix();

    EXPECT_EQ( TaskKeys::TaskListKey( kPrivateIdA ), "/chain/" + kPrivateIdA + prefix + "/tasks" );
    EXPECT_EQ( TaskKeys::TaskKey( kPrivateIdA, "t1" ), "/chain/" + kPrivateIdA + prefix + "/tasks/t1" );
    // Bare scoped subtask list is composed (no colliding 1-arg overload exists).
    EXPECT_EQ( TaskKeys::ScopePrefix( kPrivateIdA ) + TaskKeys::SubTaskListKey(),
               "/chain/" + kPrivateIdA + prefix + "/subtasks" );
    EXPECT_EQ( TaskKeys::SubTaskListKey( kPrivateIdA, "t1" ),
               "/chain/" + kPrivateIdA + prefix + "/subtasks/t1" );
    EXPECT_EQ( TaskKeys::SubTaskKey( kPrivateIdA, "t1", "s1" ),
               "/chain/" + kPrivateIdA + prefix + "/subtasks/t1/s1" );
    EXPECT_EQ( TaskKeys::ClaimableListKey( kPrivateIdA ), "/chain/" + kPrivateIdA + prefix + "/claimable" );
    EXPECT_EQ( TaskKeys::ClaimableTaskKey( kPrivateIdA, "t1" ),
               "/chain/" + kPrivateIdA + prefix + "/claimable/t1" );
}

TEST( TaskKeysScope, DistinctPrivateIdsNeverShareAKeyTree )
{
    EXPECT_NE( TaskKeys::TaskKey( kPrivateIdA, "t1" ), TaskKeys::TaskKey( kPrivateIdB, "t1" ) );
    EXPECT_EQ( TaskKeys::TaskKey( kPrivateIdA, "t1" ).find( kPrivateIdB ), std::string::npos );
    EXPECT_EQ( TaskKeys::TaskKey( kPrivateIdB, "t1" ).find( kPrivateIdA ), std::string::npos );
    EXPECT_NE( TaskKeys::ClaimableListKey( kPrivateIdA ), TaskKeys::ClaimableListKey( kPrivateIdB ) );
}

TEST( TaskKeysScope, SubTaskResultKeyPublicIsByteIdenticalToLegacyFormat )
{
    // Byte-identical to the previous boost::format( "results/%s" ) output.
    EXPECT_EQ( TaskKeys::SubTaskResultKey( kPublicScope, "s1" ), "results/s1" );
    EXPECT_EQ( TaskKeys::SubTaskResultKey( kPublicScope, "sub/with-id" ), "results/sub/with-id" );

    // Scoped branch lives under /chain/<id>/results/.
    EXPECT_EQ( TaskKeys::SubTaskResultKey( kPrivateIdA, "s1" ), "/chain/" + kPrivateIdA + "/results/s1" );
    const auto scoped = TaskKeys::SubTaskResultKey( kPrivateIdA, "s1" );
    EXPECT_EQ( scoped.rfind( "/chain/" + kPrivateIdA + "/results/", 0 ), 0 );
}

TEST( TaskKeysScope, ScopedKeyPathJoinsWithExactlyOneSlash )
{
    // Public: raw path returned unchanged (escrow lock_id shape preserved byte-for-byte).
    EXPECT_EQ( TaskKeys::ScopedKeyPath( kPublicScope, "0xdeadbeef" ), "0xdeadbeef" );
    EXPECT_EQ( TaskKeys::ScopedKeyPath( kPublicScope, "/already/slashy" ), "/already/slashy" );

    // Scoped: /chain/<id>/<path> with a single slash between every segment.
    EXPECT_EQ( TaskKeys::ScopedKeyPath( kPrivateIdA, "0xdeadbeef" ), "/chain/" + kPrivateIdA + "/0xdeadbeef" );
    EXPECT_EQ( TaskKeys::ScopedKeyPath( kPrivateIdA, "0xdeadbeef" ).find( "//" ), std::string::npos );

    // Escrow-shaped lock_id (HoldEscrow produces "0x" + 64 hex chars).
    const std::string escrow_lock_id = "0x" + std::string( 64, 'f' );
    EXPECT_EQ( TaskKeys::ScopedKeyPath( kPrivateIdA, escrow_lock_id ),
               "/chain/" + kPrivateIdA + "/" + escrow_lock_id );
}

TEST( TaskKeysScope, ScopedTopicAppendsScopeSegmentOnlyWhenScoped )
{
    EXPECT_EQ( TaskKeys::ScopedTopic( "SGNUS.Processing.Channel", kPublicScope ), "SGNUS.Processing.Channel" );
    EXPECT_EQ( TaskKeys::ScopedTopic( "SGNUS.Jobs.Channel", kPublicScope ), "SGNUS.Jobs.Channel" );
    EXPECT_EQ( TaskKeys::ScopedTopic( "SGNUS.Processing.Channel", kPrivateIdA ),
               "SGNUS.Processing.Channel/" + kPrivateIdA );
    EXPECT_EQ( TaskKeys::ScopedTopic( "SGNUS.Jobs.Channel", kPrivateIdA ),
               "SGNUS.Jobs.Channel/" + kPrivateIdA );
    EXPECT_EQ( TaskKeys::ScopedTopic( "SGNUS.Processing.Channel", kPrivateIdA ).find( "//" ),
               std::string::npos );
}

// ---------------------------------------------------------------------------
// Data-path placement: queue and result storage write ONLY under their scope
// (precedent: task_queue_test.cpp CRDTFixture usage)
// ---------------------------------------------------------------------------

/// Helper to build a Task proto with minimal fields (task_queue_test.cpp idiom).
SGProcessing::Task MakeScopedTestTask( const std::string &taskId )
{
    SGProcessing::Task task;
    task.set_ipfs_block_id( taskId );
    task.set_json_data( R"({"name":"test","gnus_spec_version":1,"inputs":[],"outputs":[],"passes":[],"version":"1.0"})" );
    task.set_random_seed( 0.0f );
    task.set_results_channel( "test_channel" );
    return task;
}

/// Helper to build a SubTask proto with minimal fields.
SGProcessing::SubTask MakeScopedTestSubTask( const std::string &taskId, const std::string &subTaskId )
{
    SGProcessing::SubTask sub;
    sub.set_ipfsblock( taskId );
    sub.set_subtaskid( subTaskId );
    sub.set_json_data( R"({"source":"input:test_input"})" );
    return sub;
}

/// Helper to build a SubTaskResult with minimal fields.
SGProcessing::SubTaskResult MakeScopedTestResult( const std::string &subTaskId )
{
    SGProcessing::SubTaskResult result;
    result.set_subtaskid( subTaskId );
    result.set_result_hash( "deadbeef" );
    result.set_ipfs_results_data_id( "ipfs://QmTestCid" );
    result.set_node_address( "test_node" );
    return result;
}

class TaskKeysScopeDataPathTest : public test::CRDTFixture
{
public:
    TaskKeysScopeDataPathTest() : CRDTFixture( fs::path( "task_keys_scope_test" ) )
    {
        topic_ = "test_processing_topic";
        db_->AddListenTopic( topic_ );
    }

    std::string topic_;
};

TEST_F( TaskKeysScopeDataPathTest, ScopedQueueWritesLandOnlyUnderChainBranch )
{
    auto queue = TaskQueueImpl::New( db_, topic_, kPrivateIdA );
    ASSERT_TRUE( queue );

    const std::string task_id = "scoped_task_1";
    auto              task    = MakeScopedTestTask( task_id );
    auto              sub     = MakeScopedTestSubTask( task_id, "sub_1" );
    ASSERT_TRUE( queue->EnqueueTask( task, { sub } ).has_value() );

    // Scoped task key: present.
    auto scoped_get = db_->Get( crdt::HierarchicalKey( TaskKeys::TaskKey( kPrivateIdA, task_id ) ) );
    EXPECT_TRUE( scoped_get.has_value() ) << "scoped task key missing after enqueue";

    // Public task key: absent (no public-namespace leak).
    auto public_get = db_->Get( crdt::HierarchicalKey( TaskKeys::TaskKey( kPublicScope, task_id ) ) );
    EXPECT_TRUE( public_get.has_failure() ) << "scoped enqueue leaked into the public task key";

    // Scoped claimable list contains the task; public claimable list is empty.
    auto scoped_claimable = db_->QueryKeyValues( TaskKeys::ClaimableListKey( kPrivateIdA ) );
    ASSERT_TRUE( scoped_claimable.has_value() );
    EXPECT_EQ( scoped_claimable.value().size(), 1u );

    auto public_claimable = db_->QueryKeyValues( TaskKeys::ClaimableListKey( kPublicScope ) );
    if ( public_claimable.has_value() )
    {
        EXPECT_TRUE( public_claimable.value().empty() ) << "scoped enqueue leaked into the public claimable list";
    }

    // Scoped subtask branch holds the subtask; public subtask branch does not.
    auto scoped_subtasks = db_->QueryKeyValues( TaskKeys::SubTaskListKey( kPrivateIdA, task_id ) );
    ASSERT_TRUE( scoped_subtasks.has_value() );
    EXPECT_EQ( scoped_subtasks.value().size(), 1u );

    auto public_subtasks = db_->QueryKeyValues( TaskKeys::SubTaskListKey( kPublicScope, task_id ) );
    if ( public_subtasks.has_value() )
    {
        EXPECT_TRUE( public_subtasks.value().empty() ) << "scoped enqueue leaked into the public subtask list";
    }

    // The queue's own readers resolve through the scope (no public-namespace reads).
    auto listed = queue->ListTaskKeys();
    ASSERT_EQ( listed.size(), 1u );
    EXPECT_EQ( listed[0], task_id );
}

TEST_F( TaskKeysScopeDataPathTest, ScopedResultStorageWritesLandOnlyUnderChainBranch )
{
    SubTaskResultStorageImpl storage( db_, topic_, kPrivateIdA );

    storage.AddSubTaskResult( MakeScopedTestResult( "sub_result_1" ) );

    // Reader resolves through the scope.
    auto results = storage.GetSubTaskResults( { "sub_result_1" } );
    ASSERT_EQ( results.size(), 1u );
    EXPECT_EQ( results[0].subtaskid(), "sub_result_1" );

    // Scoped key present in the DB...
    auto scoped_get = db_->Get( crdt::HierarchicalKey( TaskKeys::SubTaskResultKey( kPrivateIdA, "sub_result_1" ) ) );
    EXPECT_TRUE( scoped_get.has_value() ) << "scoped result key missing after AddSubTaskResult";

    // ...public results/<id> key absent.
    auto public_get = db_->Get( crdt::HierarchicalKey( TaskKeys::SubTaskResultKey( kPublicScope, "sub_result_1" ) ) );
    EXPECT_TRUE( public_get.has_failure() ) << "scoped result leaked into the public results/ namespace";
}

TEST_F( TaskKeysScopeDataPathTest, PublicQueueWithDefaultedScopeKeepsExactPublicKeys )
{
    // Defaulted trailing argument: existing callers compile unchanged and produce
    // today's exact public keys.
    auto queue = TaskQueueImpl::New( db_, topic_ );
    ASSERT_TRUE( queue );

    const std::string task_id = "public_task_1";
    auto              task    = MakeScopedTestTask( task_id );
    auto              sub     = MakeScopedTestSubTask( task_id, "sub_1" );
    ASSERT_TRUE( queue->EnqueueTask( task, { sub } ).has_value() );

    auto public_get = db_->Get( crdt::HierarchicalKey( TaskKeys::TaskKey( kPublicScope, task_id ) ) );
    EXPECT_TRUE( public_get.has_value() ) << "public task key missing after defaulted-scope enqueue";

    auto scoped_get = db_->Get( crdt::HierarchicalKey( TaskKeys::TaskKey( kPrivateIdA, task_id ) ) );
    EXPECT_TRUE( scoped_get.has_failure() ) << "public enqueue unexpectedly landed under a private branch";
}
