#include "processing_subtask_enqueuer_impl.hpp"

#include <utility>

namespace sgns::processing
{
    SubTaskEnqueuerImpl::SubTaskEnqueuerImpl( std::shared_ptr<ProcessingTaskQueue> taskQueue ) :
        m_taskQueue( std::move( taskQueue ) )
    {
    }

    outcome::result<SGProcessing::Task> SubTaskEnqueuerImpl::EnqueueSubTasks(
        std::string                      &subTaskQueueId,
        std::list<SGProcessing::SubTask> &subTasks )
    {
        BOOST_OUTCOME_TRY( auto task_result, m_taskQueue->GrabTask() );

        auto [taskKey, task] = task_result;

        subTaskQueueId = taskKey;
        m_logger->debug( "ENQUEUE_SUBTASKS: taskKey={}, task.ipfs_block_id()={}", taskKey, task.ipfs_block_id() );

        m_taskQueue->GetSubTasks( taskKey, subTasks );

        m_logger->debug( "ENQUEUE_SUBTASKS: {} subtasks found", subTasks.size() );
        for ( const auto& subtask : subTasks )
        {
            m_logger->debug( "SUBTASK: id={}, ipfsblock={}", subtask.subtaskid(), subtask.ipfsblock() );
        }

        return task;
    }

    void SubTaskEnqueuerImpl::MarkTaskBad( const std::string &taskKey )
    {
        m_taskQueue->MarkTaskBad( taskKey );
    }

}
