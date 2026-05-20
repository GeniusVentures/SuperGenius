#include "processing/impl/TaskQueueImpl.hpp"

#include <chrono>
#include <set>

#include "crdt/globaldb/globaldb.hpp"

namespace sgns::processing
{
    namespace
    {
        constexpr auto LOCK_TIMEOUT = std::chrono::seconds( 10 );

        std::string SubTaskPrefixForTask( std::string_view taskId )
        {
            auto prefix = TaskKeys::SubTaskKey( taskId, "" );
            if ( !prefix.empty() && prefix.back() == '/' )
            {
                prefix.pop_back();
            }
            return prefix;
        }

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

        TaskQueueImplLogger()->debug( "Task with ID: {} enqueued to CRDT transaction", task.ipfs_block_id() );
        BOOST_OUTCOME_TRY( crdt_transaction->Commit( { processing_topic_ } ) );

        return outcome::success();
    }

    bool TaskQueueImpl::GetSubTasks( const std::string &taskId, std::list<SGProcessing::SubTask> &subTasks )
    {
        auto querySubTasks = db_->QueryKeyValues( SubTaskPrefixForTask( taskId ) );
        if ( querySubTasks.has_failure() || !querySubTasks.has_value() )
        {
            return false;
        }

        for ( const auto &element : querySubTasks.value() )
        {
            SGProcessing::SubTask subTask;
            if ( !subTask.ParseFromArray( element.second.data(), element.second.size() ) )
            {
                return false;
            }
            subTasks.push_back( std::move( subTask ) );
        }
        return true;
    }

    outcome::result<std::pair<std::string, SGProcessing::Task>> TaskQueueImpl::GrabTask()
    {
        BOOST_OUTCOME_TRY( auto queryTasks, db_->QueryKeyValues( TaskKeys::TaskListKey() ) );

        std::set<std::string> lockedTasks;
        for ( const auto &element : queryTasks )
        {
            auto maybeTaskKey = db_->KeyToString( element.first );
            if ( !maybeTaskKey.has_value() )
            {
                continue;
            }

            SGProcessing::Task task;
            if ( !task.ParseFromArray( element.second.data(), element.second.size() ) )
            {
                continue;
            }

            if ( IsTaskCompleted( task.ipfs_block_id() ) )
            {
                continue;
            }

            const auto &taskKey = maybeTaskKey.value();
            if ( IsTaskLocked( db_, taskKey ) )
            {
                lockedTasks.insert( taskKey );
                continue;
            }

            if ( !LockTask( db_, processing_topic_, taskKey ) )
            {
                continue;
            }

            return std::make_pair( task.ipfs_block_id(), task );
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
