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
        OUTCOME_TRY( ( auto &&, task_result ), m_taskQueue->GrabTask() );

        auto [taskKey, task] = task_result;
        //std::string taskKey = maybe_grabbed.value().first;
        //SGProcessing::Task task    = maybe_grabbed.value().second; //TODO - Not used for anything. Rewrite this code

        subTaskQueueId = taskKey;

        m_taskQueue->GetSubTasks( taskKey, subTasks );

        m_logger->debug( "ENQUEUE_SUBTASKS: {}", subTasks.size() );
        return task;
    }

}
