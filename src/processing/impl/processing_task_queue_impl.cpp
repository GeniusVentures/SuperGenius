#include <processing/impl/processing_task_queue_impl.hpp>

namespace sgns::processing
{
    void ProcessingTaskQueueImpl::EnqueueTask( const SGProcessing::Task               &task,
                                               const std::list<SGProcessing::SubTask> &subTasks )
    {
        auto               taskKey = ( boost::format( "tasks/TASK_%d" ) % task.ipfs_block_id() ).str();
        sgns::base::Buffer valueBuffer;
        valueBuffer.put( task.SerializeAsString() );
        auto setKeyResult = m_db->Put( sgns::crdt::HierarchicalKey( taskKey ), valueBuffer );
        if ( setKeyResult.has_failure() )
        {
            m_logger->debug( "Unable to put key-value to CRDT datastore." );
        }

        // Check if data put
        auto getKeyResult = m_db->Get( sgns::crdt::HierarchicalKey( taskKey ) );
        if ( getKeyResult.has_failure() )
        {
            m_logger->debug( "Unable to find key in CRDT datastore" );
        }
        else
        {
            m_logger->debug( "[{}] placed to GlobalDB ", taskKey );
        }
        for (auto& subTask : subTasks)
        {
            valueBuffer.clear();
            auto subTaskKey =
                ( boost::format( "subtasks/TASK_%s/%s" ) % task.ipfs_block_id() % subTask.subtaskid() ).str();
            valueBuffer.put( subTask.SerializeAsString() );
            auto setKeyResult = m_db->Put( sgns::crdt::HierarchicalKey( subTaskKey ), valueBuffer );
            if ( setKeyResult.has_failure() )
            {
                m_logger->debug( "Unable to put key-value to CRDT datastore." );
            }

            // Check if data put
            auto getKeyResult = m_db->Get( sgns::crdt::HierarchicalKey( taskKey ) );
            if ( getKeyResult.has_failure() )
            {
                m_logger->debug( "Unable to find key in CRDT datastore" );
            }
            else
            {
                m_logger->debug( "[{}] placed to GlobalDB ", taskKey );
            }
        }
    }

    bool ProcessingTaskQueueImpl::GetSubTasks( const std::string &taskId, std::list<SGProcessing::SubTask> &subTasks )
    {
        m_logger->debug( "SUBTASKS_REQUESTED. TaskId: {}", taskId );
        //if (IsTaskCompleted(taskId))
        //{
        //    m_logger->debug("TASK_COMPLETED. TaskId {}", taskId);
        //    return false;
        //}
        auto key           = ( boost::format( "subtasks/TASK_%s" ) % taskId ).str();
        auto querySubTasks = m_db->QueryKeyValues( key );

        if ( querySubTasks.has_failure() )
        {
            m_logger->info( "Unable list subtasks from CRDT datastore" );
            return false;
        }

        if ( querySubTasks.has_value() )
        {
            m_logger->debug( "SUBTASKS_FOUND {}", querySubTasks.value().size() );

            for ( auto element : querySubTasks.value() )
            {
                SGProcessing::SubTask subTask;
                if ( subTask.ParseFromArray( element.second.data(), element.second.size() ) )
                {
                    m_logger->debug("Subtask check {}", subTask.chunkstoprocess_size());
                    subTasks.push_back( std::move( subTask ) );
                }
                else
                {
                    m_logger->debug( "Undable to parse a subtask" );
                }
            }

            return true;
        }
        else
        {
            m_logger->debug( "NO_SUBTASKS_FOUND. TaskId {}", taskId );
            return false;
        }
    }

    outcome::result<std::pair<std::string, SGProcessing::Task>> ProcessingTaskQueueImpl::GrabTask()
    {
        //m_logger->info( "GRAB_TASK" );
        OUTCOME_TRY( ( auto &&, queryTasks ), m_db->QueryKeyValues( "tasks" ) );

        //m_logger->info( "Task list grabbed from CRDT datastore" );

        bool                  task_grabbed = false;
        std::set<std::string> lockedTasks;
        SGProcessing::Task    task;
        //m_logger->info( "Number of tasks in Queue: {}", queryTasks.size() );
        for ( auto element : queryTasks )
        {
            auto taskKey = m_db->KeyToString( element.first );
            if ( !taskKey.has_value() )
            {
                m_logger->debug( "Unable to convert a key to string" );
                continue;
            }
            //std::cout << "Trying to get results from  " << "task_results/" + taskKey.value() << std::endl;
            auto maybe_previous_result = m_db->Get( { "task_results/" + taskKey.value() } );
            if ( maybe_previous_result )
            {
                m_logger->debug( "Task already processed" );
                continue;
            }

            if ( IsTaskLocked( taskKey.value() ) )
            {
                m_logger->debug( "TASK_PREVIOUSLY_LOCKED {}", taskKey.value() );
                lockedTasks.insert( taskKey.value() );
                continue;
            }
            m_logger->debug( "TASK_QUEUE_ITEM: {}, LOCKED: true", taskKey.value() );
            if ( !task.ParseFromArray( element.second.data(), element.second.size() ) )
            {
                m_logger->debug( "Couldn't parse the task from Protobuf" );
                //TODO - Decide what to do with an invalid task - Maybe error?
                continue;
            }
            if ( !LockTask( taskKey.value() ) )
            {
                m_logger->debug( "Failed to lock task" );
                continue;
            }
            m_logger->debug( "TASK_LOCKED {}", taskKey.value() );
            task_grabbed = true;
            break;
        }

        // No task was grabbed so far
        for ( auto lockedTask : lockedTasks )
        {
            if ( MoveExpiredTaskLock( lockedTask, task ) )
            {
                task_grabbed = true;
                break;
            }
        }

        if ( task_grabbed )
        {
            return std::make_pair( task.ipfs_block_id(), task );
        }
        else
        {
            return outcome::failure( boost::system::error_code{} );
        }
    }

    bool ProcessingTaskQueueImpl::CompleteTask( const std::string &taskKey, const SGProcessing::TaskResult &taskResult )
    {
        sgns::base::Buffer data;
        data.put( taskResult.SerializeAsString() );

        std::cout << "Completing the task and storing results on " << "task_results/" + taskKey << std::endl;
        m_db->Put( { "task_results/tasks/TASK_" + taskKey }, data );
        m_db->Remove( { "lock_tasks/TASK_" + taskKey } );
        //auto transaction = m_db->BeginTransaction();
        //transaction->AddToDelta( sgns::crdt::HierarchicalKey( "task_results/" + taskKey ), data );
        //transaction->RemoveFromDelta( sgns::crdt::HierarchicalKey( "lock_" + taskKey ) );
        //transaction->RemoveFromDelta( sgns::crdt::HierarchicalKey( taskKey ) );
        //
        //auto res = transaction->PublishDelta();
        m_logger->debug( "TASK_COMPLETED: {}", taskKey );
        return true;
    }

    bool ProcessingTaskQueueImpl::IsTaskCompleted( const std::string &taskId )
    {
        bool ret = false;
        if ( m_db->Get( { "task_results/tasks/TASK_" + taskId } ) )
        {
            ret = true;
        }
        return ret;
    }

    bool ProcessingTaskQueueImpl::IsTaskLocked( const std::string &taskKey )
    {
        auto lockData = m_db->Get( sgns::crdt::HierarchicalKey( "lock_" + taskKey ) );
        return !lockData.has_failure() && lockData.has_value();
    }

    bool ProcessingTaskQueueImpl::LockTask( const std::string &taskKey )
    {
        auto timestamp = std::chrono::system_clock::now();

        SGProcessing::TaskLock lock;
        lock.set_task_id( taskKey );
        lock.set_lock_timestamp( timestamp.time_since_epoch().count() );

        sgns::base::Buffer lockData;
        lockData.put( lock.SerializeAsString() );

        auto res = m_db->Put( sgns::crdt::HierarchicalKey( "lock_" + taskKey ), lockData );
        return !res.has_failure();
    }

    bool ProcessingTaskQueueImpl::MoveExpiredTaskLock( const std::string &taskKey, SGProcessing::Task &task )
    {
        auto timestamp = std::chrono::system_clock::now();

        auto lockData = m_db->Get( sgns::crdt::HierarchicalKey( "lock_" + taskKey ) );
        if ( !lockData.has_failure() && lockData.has_value() )
        {
            // Check task expiration
            SGProcessing::TaskLock lock;
            if ( lock.ParseFromArray( lockData.value().data(), lockData.value().size() ) )
            {
                auto expirationTime = std::chrono::system_clock::time_point(
                                          std::chrono::system_clock::duration( lock.lock_timestamp() ) ) +
                                      m_processingTimeout;
                if ( timestamp > expirationTime )
                {
                    auto taskData = m_db->Get( taskKey );

                    if ( !taskData.has_failure() )
                    {
                        if ( task.ParseFromArray( taskData.value().data(), taskData.value().size() ) )
                        {
                            if ( LockTask( taskKey ) )
                            {
                                m_logger->debug( "TASK_LOCK_MOVED {}", taskKey );
                                return true;
                            }
                        }
                    }
                    else
                    {
                        m_logger->debug( "Unable to find a task {}", taskKey );
                    }
                }
            }
        }
        return false;
    }
}
