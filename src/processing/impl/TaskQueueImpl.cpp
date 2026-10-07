
#include <chrono>
#include <set>

#include "base/logger.hpp"
#include "processing/impl/TaskQueueImpl.hpp"
#include <processingbase/ProcessingManager.hpp>

namespace sgns::processing
{

    base::Logger TaskQueueImplLogger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "TaskQueueImpl" );
    }

    std::shared_ptr<TaskQueueImpl> TaskQueueImpl::New( std::shared_ptr<sgns::crdt::GlobalDB> db,
                                                       std::string                           processing_topic,
                                                       std::string                           private_network_id )
    {
        auto instance = std::shared_ptr<TaskQueueImpl>(
            new TaskQueueImpl( std::move( db ), std::move( processing_topic ), std::move( private_network_id ) ) );
        return instance;
    }

    TaskQueueImpl::TaskQueueImpl( std::shared_ptr<sgns::crdt::GlobalDB> db,
                                  std::string                           processing_topic,
                                  std::string                           private_network_id ) :
        db_( std::move( db ) ),
        processing_topic_( std::move( processing_topic ) ),
        network_scope_( std::move( private_network_id ) )
    {
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

            BOOST_OUTCOME_TRY(
                crdt_transaction->Put( sgns::crdt::HierarchicalKey( TaskKeys::SubTaskKey( network_scope_,
                                                                                          task.ipfs_block_id(),
                                                                                          subTask.subtaskid() ) ),
                                       std::move( value ) ) );
        }

        sgns::base::Buffer taskValue;
        taskValue.put( task.SerializeAsString() );
        BOOST_OUTCOME_TRY( crdt_transaction->Put(
            sgns::crdt::HierarchicalKey( TaskKeys::TaskKey( network_scope_, task.ipfs_block_id() ) ),
            std::move( taskValue ) ) );

        sgns::base::Buffer claimableValue;
        claimableValue.put( task.ipfs_block_id() );
        BOOST_OUTCOME_TRY( crdt_transaction->Put(
            sgns::crdt::HierarchicalKey( TaskKeys::ClaimableTaskKey( network_scope_, task.ipfs_block_id() ) ),
            std::move( claimableValue ) ) );

        TaskQueueImplLogger()->debug( "Task with ID: {} enqueued to CRDT transaction", task.ipfs_block_id() );
        BOOST_OUTCOME_TRY( crdt_transaction->Commit( { processing_topic_ } ) );

        return outcome::success();
    }

    outcome::result<SGProcessing::Task> TaskQueueImpl::GetTask( const std::string &taskId )
    {
        TaskQueueImplLogger()->debug( "Fetching task with ID: {}", taskId );
        BOOST_OUTCOME_TRY( auto task_buffer,
                           db_->Get( sgns::crdt::HierarchicalKey( TaskKeys::TaskKey( network_scope_, taskId ) ) ) );

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
        TaskQueueImplLogger()->debug( "Fetching subtasks for task with ID: {}", taskId );
        auto querySubTasks = db_->QueryKeyValues( TaskKeys::SubTaskListKey( network_scope_, taskId ) );
        if ( querySubTasks.has_failure() || !querySubTasks.has_value() )
        {
            TaskQueueImplLogger()->error( "No subtasks found for task with ID: {}", taskId );
            return false;
        }

        TaskQueueImplLogger()->debug( "Found {} subtask records for task with ID: {}",
                                      querySubTasks.value().size(),
                                      taskId );
        for ( const auto &element : querySubTasks.value() )
        {
            SGProcessing::SubTask subTask;
            if ( !subTask.ParseFromArray( element.second.data(), element.second.size() ) )
            {
                TaskQueueImplLogger()->error( "Failed to parse subtask from proto task with ID: {}", taskId );
                return false;
            }
            if ( !sgns::sgprocessing::ProcessingManager::IsProcessingModelValid( subTask.json_data() ) )
            {
                TaskQueueImplLogger()->error( "Subtask json data is invalid" );
                return false;
            }
            subTasks.push_back( std::move( subTask ) );
        }
        TaskQueueImplLogger()->debug( "Successfully fetched subtasks for task with ID: {}", taskId );

        return true;
    }

    outcome::result<std::pair<std::string, SGProcessing::Task>> TaskQueueImpl::GrabTask()
    {
        BOOST_OUTCOME_TRY( auto queryClaimable,
                           db_->QueryKeyValues( TaskKeys::ClaimableListKey( network_scope_ ) ) );

        TaskQueueImplLogger()->debug( "GrabTask scanning claimable list with {} candidates", queryClaimable.size() );
        std::set<std::string> lockedTasks;
        for ( const auto &element : queryClaimable )
        {
            if ( element.second.size() == 0 )
            {
                TaskQueueImplLogger()->debug( "Skipping empty claimable task entry" );
                continue;
            }
            const auto taskId = std::string( reinterpret_cast<const char *>( element.second.data() ),
                                             element.second.size() );
            TaskQueueImplLogger()->debug( "Evaluating claimable task candidate with ID: {}", taskId );

            if ( incompatible_jobs_.find( taskId ) != incompatible_jobs_.end() )
            {
                TaskQueueImplLogger()->debug( "Skipping incompatible task with ID: {}", taskId );
                continue;
            }
            const auto taskKey = TaskKeys::TaskKey( network_scope_, taskId );
            if ( IsTaskLocked( taskKey ) )
            {
                lockedTasks.insert( taskKey );
                continue;
            }

            if ( !LockTask( taskKey ) )
            {
                TaskQueueImplLogger()->debug( "Skipping task with ID: {} because lock acquisition failed", taskId );
                continue;
            }

            BOOST_OUTCOME_TRY( auto task, GetTask( taskId ) );
            if ( !sgns::sgprocessing::ProcessingManager::IsProcessingValid( task.json_data() ) )
            {
                TaskQueueImplLogger()->error( "Task with ID: {} has invalid processing data", taskId );
                MarkTaskBad( taskId );
                continue;
            }
            return std::make_pair( taskId, task );
        }

        for ( const auto &lockedTask : lockedTasks )
        {
            SGProcessing::Task task;
            if ( MoveExpiredTaskLock( lockedTask, task ) )
            {
                TaskQueueImplLogger()->debug( "Recovered expired-locked task with ID: {}", task.ipfs_block_id() );
                return std::make_pair( task.ipfs_block_id(), task );
            }
        }

        TaskQueueImplLogger()->debug( "No claimable task could be grabbed" );
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
        BOOST_OUTCOME_TRY( completionTransaction->Put(
            sgns::crdt::HierarchicalKey( TaskKeys::ResultTaskKey( network_scope_, taskKey ) ),
            std::move( resultData ) ) );
        BOOST_OUTCOME_TRY( completionTransaction->Remove(
            sgns::crdt::HierarchicalKey( TaskKeys::ClaimableTaskKey( network_scope_, taskKey ) ) ) );
        BOOST_OUTCOME_TRY( completionTransaction->AddTopic( processing_topic_ ) );

        return completionTransaction;
    }

    bool TaskQueueImpl::IsTaskCompleted( const std::string &taskId )
    {
        const sgns::crdt::HierarchicalKey resultKey( TaskKeys::ResultTaskKey( network_scope_, taskId ) );
        auto                              resultData = db_->Get( resultKey );
        if ( resultData.has_failure() )
        {
            TaskQueueImplLogger()->debug( "Task completion lookup failed for ID '{}'", taskId );
        }
        const bool isCompleted = resultData.has_value();
        TaskQueueImplLogger()->debug( "Task completion status for ID '{}': {}", taskId, isCompleted );
        return isCompleted;
    }

    void TaskQueueImpl::MarkTaskBad( const std::string &taskKey )
    {
        incompatible_jobs_.insert( taskKey );
        TaskQueueImplLogger()->debug( "Marked task with ID: {} as incompatible", taskKey );
    }

    bool TaskQueueImpl::IsTaskLocked( const std::string &taskKey )
    {
        const sgns::crdt::HierarchicalKey lockKey( TaskKeys::LockKey( taskKey ) );
        auto                              lockData = db_->Get( lockKey );
        const bool                        isLocked = !lockData.has_failure() && lockData.has_value();
        if ( lockData.has_failure() )
        {
            TaskQueueImplLogger()->debug( "Failed to check lock state for task key '{}'", taskKey );
        }
        else if ( isLocked )
        {
            TaskQueueImplLogger()->debug( "Task key '{}' is currently locked", taskKey );
        }
        return isLocked;
    }

    bool TaskQueueImpl::LockTask( const std::string &taskKey )
    {
        const auto timestamp = std::chrono::system_clock::now();

        const sgns::crdt::HierarchicalKey lockKey( TaskKeys::LockKey( taskKey ) );

        SGProcessing::TaskLock lock;
        lock.set_task_id( taskKey );
        lock.set_lock_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );

        sgns::base::Buffer lockData;
        lockData.put( lock.SerializeAsString() );

        auto result = db_->Put( lockKey, lockData, { processing_topic_ } );
        if ( result.has_failure() )
        {
            TaskQueueImplLogger()->debug( "Failed to lock task key '{}'", taskKey );
            return false;
        }

        TaskQueueImplLogger()->debug( "Lock acquired for task key '{}'", taskKey );
        return true;
    }

    bool TaskQueueImpl::MoveExpiredTaskLock( const std::string &taskKey, SGProcessing::Task &task )
    {
        const auto                        timestamp = std::chrono::system_clock::now();
        const sgns::crdt::HierarchicalKey lockKey( TaskKeys::LockKey( taskKey ) );
        auto                              lockData = db_->Get( lockKey );
        if ( lockData.has_failure() || !lockData.has_value() )
        {
            TaskQueueImplLogger()->debug( "No lock found to refresh for task key '{}'", taskKey );
            return false;
        }

        SGProcessing::TaskLock lock;
        if ( !lock.ParseFromArray( lockData.value().data(), lockData.value().size() ) )
        {
            TaskQueueImplLogger()->error( "Failed parsing lock for task key '{}'", taskKey );
            return false;
        }

        const auto lockTimePoint = std::chrono::system_clock::time_point(
            std::chrono::milliseconds( lock.lock_timestamp() ) );
        const auto expirationTime = lockTimePoint + LOCK_TIMEOUT;
        if ( timestamp <= expirationTime )
        {
            TaskQueueImplLogger()->debug( "Lock for task key '{}' has not expired yet", taskKey );
            return false;
        }

        auto taskData = db_->Get( sgns::crdt::HierarchicalKey( taskKey ) );
        if ( taskData.has_failure() || !taskData.has_value() )
        {
            TaskQueueImplLogger()->error( "Failed to load expired-locked task '{}'", taskKey );
            return false;
        }

        if ( !task.ParseFromArray( taskData.value().data(), taskData.value().size() ) )
        {
            TaskQueueImplLogger()->error( "Failed parsing expired-locked task '{}'", taskKey );
            return false;
        }

        const bool lockMoved = LockTask( taskKey );
        if ( lockMoved )
        {
            TaskQueueImplLogger()->debug( "Moved expired lock for task key '{}'", taskKey );
        }
        return lockMoved;
    }

    std::vector<std::string> TaskQueueImpl::ListTaskKeys()
    {
        TaskQueueImplLogger()->debug( "Listing all task keys" );
        auto queryResult = db_->QueryKeyValues( TaskKeys::TaskListKey( network_scope_ ) );
        if ( queryResult.has_failure() || !queryResult.has_value() )
        {
            TaskQueueImplLogger()->debug( "No tasks found in queue" );
            return {};
        }

        std::vector<std::string> taskIds;
        for ( const auto &element : queryResult.value() )
        {
            if ( element.second.empty() )
            {
                continue;
            }
            SGProcessing::Task task;
            if ( task.ParseFromArray( element.second.data(), element.second.size() ) )
            {
                taskIds.push_back( task.ipfs_block_id() );
            }
        }

        TaskQueueImplLogger()->debug( "Found {} tasks", taskIds.size() );
        return taskIds;
    }

    outcome::result<SGProcessing::TaskResult> TaskQueueImpl::GetTaskResult( const std::string &taskId )
    {
        TaskQueueImplLogger()->debug( "Fetching result for task: {}", taskId );
        BOOST_OUTCOME_TRY( auto resultBuffer,
                           db_->Get( sgns::crdt::HierarchicalKey( TaskKeys::ResultTaskKey( network_scope_, taskId ) ) ) );

        SGProcessing::TaskResult result;
        if ( !result.ParseFromArray( resultBuffer.data(), resultBuffer.size() ) )
        {
            TaskQueueImplLogger()->error( "Failed to parse task result for task: {}", taskId );
            return outcome::failure( boost::system::error_code{} );
        }

        TaskQueueImplLogger()->debug( "Successfully fetched result for task: {}", taskId );
        return result;
    }
}
