#include "processing/impl/processing_task_queue_impl.hpp"

namespace sgns::processing
{
    outcome::result<void> ProcessingTaskQueueImpl::EnqueueTask( const SGProcessing::Task               &task,
                                                                const std::list<SGProcessing::SubTask> &subTasks )
    {
        if ( job_crdt_transaction_ )
        {
            //escrow needs to be sent/commited
            return outcome::failure( boost::system::error_code{} );
        }

        job_crdt_transaction_ = m_db->BeginTransaction();
        std::vector<crdt::GlobalDB::DataPair> data_vector;

        for ( auto &subTask : subTasks )
        {
            auto subTaskKey = ( boost::format( "subtasks/TASK_%s/%s" ) % task.ipfs_block_id() % subTask.subtaskid() )
                                  .str();
            sgns::crdt::HierarchicalKey key( subTaskKey );
            sgns::base::Buffer          value;
            value.put( subTask.SerializeAsString() );
            BOOST_OUTCOME_TRYV2( auto &&, job_crdt_transaction_->Put( std::move( key ), std::move( value ) ) );

            m_logger->debug( "[{}] placed to GlobalDB ", subTaskKey );
        }
        auto taskKey = ( boost::format( "tasks/TASK_%d" ) % task.ipfs_block_id() ).str();

        sgns::crdt::HierarchicalKey key( taskKey );
        sgns::base::Buffer          value;
        value.put( task.SerializeAsString() );

        BOOST_OUTCOME_TRYV2( auto &&, job_crdt_transaction_->Put( std::move( key ), std::move( value ) ) );
        m_logger->debug( "[{}] placed to GlobalDB ", taskKey );

        return outcome::success();
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
                    m_logger->debug( "Subtask check {}", subTask.chunkstoprocess_size() );
                    subTasks.push_back( std::move( subTask ) );
                }
                else
                {
                    m_logger->debug( "Undable to parse a subtask" );
                }
            }

            return true;
        }

        m_logger->debug( "NO_SUBTASKS_FOUND. TaskId {}", taskId );
        return false;
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

        return outcome::failure( boost::system::error_code{} );
    }

    outcome::result<std::shared_ptr<crdt::AtomicTransaction>> ProcessingTaskQueueImpl::CompleteTask(
        const std::string              &taskKey,
        const SGProcessing::TaskResult &taskResult )
    {
        sgns::base::Buffer          data;
        sgns::crdt::HierarchicalKey result_key( { "task_results/tasks/TASK_" + taskKey } );

        auto job_completion_transaction = m_db->BeginTransaction();
        data.put( taskResult.SerializeAsString() );
        BOOST_OUTCOME_TRYV2( auto &&, job_completion_transaction->Put( std::move( result_key ), std::move( data ) ) );

        //BOOST_OUTCOME_TRYV2( auto &&, job_completion_transaction->Commit() );

        m_logger->debug( "TASK_COMPLETED: {}, results stored", taskKey );
        return job_completion_transaction;
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

    outcome::result<std::string> ProcessingTaskQueueImpl::GetTaskEscrow( const std::string &taskId )
    {
        OUTCOME_TRY( ( auto &&, task_buffer ), m_db->Get( { "tasks/TASK_" + taskId } ) );

        SGProcessing::Task task;

        if ( !task.ParseFromArray( task_buffer.data(), task_buffer.size() ) )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        return task.escrow_path();
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

        auto res = m_db->Put( sgns::crdt::HierarchicalKey( "lock_" + taskKey ), lockData, m_processingTopic );
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

    outcome::result<void> ProcessingTaskQueueImpl::SendEscrow( std::string path, sgns::base::Buffer value )
    {
        if ( !job_crdt_transaction_ )
        {
            //task and subtasks need to be enqueued
            return outcome::failure( boost::system::error_code{} );
        }

        sgns::crdt::HierarchicalKey key( path );

        BOOST_OUTCOME_TRYV2( auto &&, job_crdt_transaction_->Put( std::move( key ), std::move( value ) ) );
        BOOST_OUTCOME_TRYV2( auto &&, job_crdt_transaction_->Commit() );

        ResetAtomicTransaction();

        return outcome::success();
    }

    void ProcessingTaskQueueImpl::ResetAtomicTransaction()
    {
        job_crdt_transaction_.reset();
    }
}
