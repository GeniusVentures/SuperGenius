#include "processing/impl/TaskQueueImpl.hpp"

#include <chrono>
#include <set>

#include "crdt/globaldb/globaldb.hpp"

namespace sgns::processing
{
    namespace
    {
        constexpr auto LOCK_TIMEOUT = std::chrono::seconds( 10 );

        bool IsTaskLocked( const std::shared_ptr<sgns::crdt::GlobalDB> &db, const std::string &taskKey )
        {
            const sgns::crdt::HierarchicalKey lockKey( TaskKeys::LockKey( taskKey ) );
            auto                              lockData = db->Get( lockKey );
            return !lockData.has_failure() && lockData.has_value();
        }

        bool LockTask( const std::shared_ptr<sgns::crdt::GlobalDB> &db,
                       const std::string                           &processingTopic,
                       const std::string                           &taskKey )
        {
            const auto timestamp = std::chrono::system_clock::now();

            const sgns::crdt::HierarchicalKey lockKey( TaskKeys::LockKey( taskKey ) );

            SGProcessing::TaskLock lock;
            lock.set_task_id( taskKey );
            lock.set_lock_timestamp(
                std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );

            sgns::base::Buffer lockData;
            lockData.put( lock.SerializeAsString() );

            auto result = db->Put( lockKey, lockData, { processingTopic } );
            return !result.has_failure();
        }

        bool MoveExpiredTaskLock( const std::shared_ptr<sgns::crdt::GlobalDB> &db,
                                  const std::string                           &processingTopic,
                                  const std::string                           &taskKey,
                                  SGProcessing::Task                          &task )
        {
            const auto                        timestamp = std::chrono::system_clock::now();
            const sgns::crdt::HierarchicalKey lockKey( TaskKeys::LockKey( taskKey ) );
            auto                              lockData = db->Get( lockKey );
            if ( lockData.has_failure() || !lockData.has_value() )
            {
                return false;
            }

            SGProcessing::TaskLock lock;
            if ( !lock.ParseFromArray( lockData.value().data(), lockData.value().size() ) )
            {
                return false;
            }

            const auto lockTimePoint = std::chrono::system_clock::time_point(
                std::chrono::milliseconds( lock.lock_timestamp() ) );
            const auto expirationTime = lockTimePoint + LOCK_TIMEOUT;
            if ( timestamp <= expirationTime )
            {
                return false;
            }

            auto taskData = db->Get( sgns::crdt::HierarchicalKey( taskKey ) );
            if ( taskData.has_failure() || !taskData.has_value() )
            {
                return false;
            }

            if ( !task.ParseFromArray( taskData.value().data(), taskData.value().size() ) )
            {
                return false;
            }

            return LockTask( db, processingTopic, taskKey );
        }
    } // namespace

    base::Logger TaskQueueImplLogger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "TaskQueueImpl" );
    }

    outcome::result<void> TaskQueueImpl::EnqueueTask( const SGProcessing::Task                &task,
                                                      const std::list<SGProcessing::SubTask>  &subTasks,
                                                      std::shared_ptr<crdt::AtomicTransaction> crdt_transaction )
    {
        if ( !crdt_transaction )
        {
            crdt_transaction = db_->BeginTransaction();
        }

        TaskQueueImplLogger()->debug( "Enqueuing task with ID: {}, number of subtasks: {}",
                                      task.ipfs_block_id(),
                                      subTasks.size() );

        for ( const auto &subTask : subTasks )
        {
            sgns::base::Buffer value;
            value.put( subTask.SerializeAsString() );
            TaskQueueImplLogger()->debug( "Enqueuing subtask: {}", subTask.subtaskid() );

            BOOST_OUTCOME_TRY( crdt_transaction->Put(
                sgns::crdt::HierarchicalKey( TaskKeys::SubTaskKey( task.ipfs_block_id(), subTask.subtaskid() ) ),
                std::move( value ) ) );
        }

        sgns::base::Buffer taskValue;
        taskValue.put( task.SerializeAsString() );
        BOOST_OUTCOME_TRY(
            crdt_transaction->Put( sgns::crdt::HierarchicalKey( TaskKeys::TaskKey( task.ipfs_block_id() ) ),
                                   std::move( taskValue ) ) );

        sgns::base::Buffer claimableValue;
        claimableValue.put( task.ipfs_block_id() );
        BOOST_OUTCOME_TRY(
            crdt_transaction->Put( sgns::crdt::HierarchicalKey( TaskKeys::ClaimableTaskKey( task.ipfs_block_id() ) ),
                                   std::move( claimableValue ) ) );

        TaskQueueImplLogger()->debug( "Task with ID: {} enqueued to CRDT transaction", task.ipfs_block_id() );
        BOOST_OUTCOME_TRY( crdt_transaction->Commit( { processing_topic_ } ) );

        return outcome::success();
    }

    outcome::result<SGProcessing::Task> TaskQueueImpl::GetTask( const std::string &taskId )
    {
        TaskQueueImplLogger()->debug( "Fetching task with ID: {}", taskId );
        BOOST_OUTCOME_TRY( auto task_buffer, db_->Get( sgns::crdt::HierarchicalKey( TaskKeys::TaskKey( taskId ) ) ) );

        SGProcessing::Task task;

        if ( !task.ParseFromArray( task_buffer.data(), task_buffer.size() ) )
        {
            TaskQueueImplLogger()->error( "Failed to parse from proto task with ID: {}", taskId );
            return outcome::failure( boost::system::error_code{} );
        }
        TaskQueueImplLogger()->debug( "Successfully fetched task with ID: {}", taskId );

        return task;
    }

    bool TaskQueueImpl::GetSubTasks( const std::string &taskId, std::list<SGProcessing::SubTask> &subTasks )
    {
        auto querySubTasks = db_->QueryKeyValues( sgns::crdt::HierarchicalKey( TaskKeys::SubTaskListKey( taskId ) ) );
        if ( querySubTasks.has_failure() || !querySubTasks.has_value() )
        {
            TaskQueueImplLogger()->error( "No subtasks found for task with ID: {}", taskId );
            return false;
        }

        for ( const auto &element : querySubTasks.value() )
        {
            SGProcessing::SubTask subTask;
            if ( !subTask.ParseFromArray( element.second.data(), element.second.size() ) )
            {
                TaskQueueImplLogger()->error( "Failed to parse subtask from proto task with ID: {}", taskId );
                return false;
            }
            subTasks.push_back( std::move( subTask ) );
        }
        TaskQueueImplLogger()->debug( "Successfully fetched subtasks for task with ID: {}", taskId );

        return true;
    }

    outcome::result<std::pair<std::string, SGProcessing::Task>> TaskQueueImpl::GrabTask()
    {
        BOOST_OUTCOME_TRY( auto queryClaimable, db_->QueryKeyValues( { TaskKeys::ClaimableListKey() } ) );

        TaskQueueImplLogger()->debug( "GrabTask scanning claimable list with {} candidates", queryClaimable.size() );
        std::set<std::string> lockedTasks;
        for ( const auto &element : queryClaimable )
        {
            if ( element.second.size() == 0 )
            {
                continue;
            }
            const auto taskId = std::string( reinterpret_cast<const char *>( element.second.data() ),
                                             element.second.size() );

            if ( IsTaskCompleted( taskId ) )
            {
                // Cleanup stale claimable marker for already completed task.
                (void)db_->Remove( sgns::crdt::HierarchicalKey( TaskKeys::ClaimableTaskKey( taskId ) ),
                                   { processing_topic_ } );
                continue;
            }

            const auto taskKey = TaskKeys::TaskKey( taskId );
            if ( IsTaskLocked( db_, taskKey ) )
            {
                lockedTasks.insert( taskKey );
                continue;
            }

            if ( !LockTask( db_, processing_topic_, taskKey ) )
            {
                continue;
            }

            // Task is no longer claimable once a lock is acquired.
            (void)db_->Remove( sgns::crdt::HierarchicalKey( TaskKeys::ClaimableTaskKey( taskId ) ),
                               { processing_topic_ } );

            BOOST_OUTCOME_TRY( auto task, GetTask( taskId ) );
            return std::make_pair( taskId, task );
        }

        for ( const auto &lockedTask : lockedTasks )
        {
            SGProcessing::Task task;
            if ( MoveExpiredTaskLock( db_, processing_topic_, lockedTask, task ) )
            {
                return std::make_pair( task.ipfs_block_id(), task );
            }
        }

        return outcome::failure( boost::system::error_code{} );
    }

    outcome::result<std::shared_ptr<crdt::AtomicTransaction>> TaskQueueImpl::CompleteTask(
        const std::string              &taskKey,
        const SGProcessing::TaskResult &taskResult )
    {
        auto completionTransaction = db_->BeginTransaction();

        TaskQueueImplLogger()->debug( "Completing task with ID: {}, result: {}", taskKey, taskResult.DebugString() );
        sgns::base::Buffer resultData;
        resultData.put( taskResult.SerializeAsString() );
        BOOST_OUTCOME_TRY(
            completionTransaction->Put( sgns::crdt::HierarchicalKey( TaskKeys::ResultTaskKey( taskKey ) ),
                                        std::move( resultData ) ) );

        return completionTransaction;
    }

    bool TaskQueueImpl::IsTaskCompleted( const std::string &taskId )
    {
        const sgns::crdt::HierarchicalKey resultKey( TaskKeys::ResultTaskKey( taskId ) );
        auto                              resultData = db_->Get( resultKey );
        return resultData.has_value();
    }

    void TaskQueueImpl::MarkTaskBad( const std::string &taskKey ) {}
}
